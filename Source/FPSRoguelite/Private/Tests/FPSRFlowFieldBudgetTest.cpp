// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Enemy/FPSRFlowFieldComputer.h"
#include "UObject/StrongObjectPtr.h"
#include "HAL/PlatformTime.h"

#if WITH_AUTOMATION_TESTS

// P-0 (2026-07-07, U continuous-field pivot — Docs/Review/20260707-plan-continuous-field-arch.md):
// the reproving gate for the "unified 3x3 single grid" (U) multimap design.
//   (A) FlowField.CapGate       — the arithmetic content contract. An author-sized combat slot, tiled 3x3 into
//       ONE shared grid at 200cm quality, must fit MaxTotalCells / MaxGridDimPerAxis. D1-confirmed slots
//       (100-132m) pass; a slot past the ~132m boundary FAILS fast. That failure IS U's falsifiable prediction:
//       if the contract cannot be locked, U is invalid and we fall back to P (per-map) / tiled.
//   (B) FlowField.RecomputeBench — worldless timing of a near-cap (~39k-cell) RunBFS, the exact cost a door-open
//       recompute pays under U. Front-loads Performance §5's unmeasured recompute-ms risk BEFORE any content.

namespace
{
	constexpr int32 NLB = UFPSRFlowFieldComputer::NumLayers; // 2

	int32 SurfB(int32 Cell, int32 Rank) { return Cell * NLB + Rank; }

	// A full W x H flat grid: rank0 walkable everywhere, rank1 absent, no blocks, all orthogonal
	// rank0<->rank0 edges open. Feeds the near-cap recompute benchmark. Cell (CX,CY) = CY*W+CX.
	FFPSRFlowFieldSurfaceData MakeFullFlatGrid(int32 W, int32 H, float CellSize)
	{
		FFPSRFlowFieldSurfaceData D;
		D.GridDimX = W;
		D.GridDimY = H;
		D.GridOrigin = FVector::ZeroVector;
		D.CellSize = CellSize;
		const int32 NumCells = W * H;
		D.CellFloorZ.Init(MAX_flt, NumCells * NLB);
		D.BlockedField.Init(false, NumCells * NLB);
		D.EdgeMask.Init(0, NumCells * 2);
		for (int32 CY = 0; CY < H; ++CY)
		{
			for (int32 CX = 0; CX < W; ++CX)
			{
				const int32 Cell = CY * W + CX;
				D.CellFloorZ[SurfB(Cell, 0)] = 0.0f;                                      // rank 0 present
				if (CX + 1 < W) { D.EdgeMask[Cell * 2 + 0] |= (1u << (0 * NLB + 0)); }    // +X rank0->rank0
				if (CY + 1 < H) { D.EdgeMask[Cell * 2 + 1] |= (1u << (0 * NLB + 0)); }    // +Y rank0->rank0
			}
		}
		return D;
	}
}

// (A) Arithmetic cap gate — the D1 content contract for U.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSRFlowFieldCapGateTest, "FPSRoguelite.FlowField.CapGate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFPSRFlowFieldCapGateTest::RunTest(const FString& Parameters)
{
	using FBudget = UFPSRFlowFieldComputer::FUnifiedGridBudget;

	// Contract lock: if these compile-time caps ever change, the D1 ~132m boundary moves with them — flag it loudly.
	TestEqual(TEXT("MaxGridDimPerAxis cap == 256"), UFPSRFlowFieldComputer::GetMaxGridDimPerAxis(), 256);
	TestEqual(TEXT("MaxTotalCells cap == 40000"), UFPSRFlowFieldComputer::GetMaxTotalCells(), 40000);

	const float CellCm = 200.0f;
	// SlotCellsForSize (ceil at 200cm): 100m -> 50, 132m -> 66, 140m -> 70, 180m -> 90 cells.
	TestEqual(TEXT("100m slot -> 50 cells"), UFPSRFlowFieldComputer::SlotCellsForSize(100.0f * 100.0f, CellCm), 50);
	TestEqual(TEXT("132m slot -> 66 cells"), UFPSRFlowFieldComputer::SlotCellsForSize(132.0f * 100.0f, CellCm), 66);

	// (1) D1 lower end: 100m slots, no wall -> 150x150 = 22,500 cells. WITHIN (large margin).
	{
		const int32 S = UFPSRFlowFieldComputer::SlotCellsForSize(100.0f * 100.0f, CellCm); // 50
		const FBudget B = UFPSRFlowFieldComputer::CheckUnifiedGridBudget(S, S);
		AddInfo(B.Reason);
		TestEqual(TEXT("100m grid dim = 150"), B.GridDimX, 150);
		TestEqual(TEXT("100m total = 22500"), static_cast<int32>(B.TotalCells), 22500);
		TestTrue(TEXT("100m slots within U budget"), B.bWithinBudget);
	}

	// (2) D1 upper end: 132m slots, no wall -> 198x198 = 39,204 cells. WITHIN (near-cap, 98%).
	{
		const int32 S = UFPSRFlowFieldComputer::SlotCellsForSize(132.0f * 100.0f, CellCm); // 66
		const FBudget B = UFPSRFlowFieldComputer::CheckUnifiedGridBudget(S, S);
		AddInfo(B.Reason);
		TestEqual(TEXT("132m grid dim = 198"), B.GridDimX, 198);
		TestEqual(TEXT("132m total = 39204"), static_cast<int32>(B.TotalCells), 39204);
		TestTrue(TEXT("132m slots within U budget (near-cap)"), B.bWithinBudget);
	}

	// (3) Wall allowance tips the near-cap slot OVER: 132m + 2-cell wall/seam -> 202x202 = 40,804 > 40,000. FAILS.
	//     This is precisely the silent-coarsen gotcha the gate must catch (today's bake would just grow CellSize).
	{
		const int32 S = UFPSRFlowFieldComputer::SlotCellsForSize(132.0f * 100.0f, CellCm); // 66
		const FBudget B = UFPSRFlowFieldComputer::CheckUnifiedGridBudget(S, S, /*WallCellsPerSeam=*/2);
		AddInfo(B.Reason);
		TestEqual(TEXT("132m+wall grid dim = 202"), B.GridDimX, 202);
		TestFalse(TEXT("132m+wall exceeds total cap"), B.bTotalWithinCap);
		TestFalse(TEXT("132m+wall NOT within budget"), B.bWithinBudget);
	}

	// (4) Past the D1 boundary on total cells: 140m slots -> 210x210 = 44,100 > 40,000. FAILS on total (axis still ok).
	{
		const int32 S = UFPSRFlowFieldComputer::SlotCellsForSize(140.0f * 100.0f, CellCm); // 70
		const FBudget B = UFPSRFlowFieldComputer::CheckUnifiedGridBudget(S, S);
		AddInfo(B.Reason);
		TestTrue(TEXT("140m axis still within cap (210<=256)"), B.bAxisWithinCap);
		TestFalse(TEXT("140m exceeds total cap (44100>40000)"), B.bTotalWithinCap);
		TestFalse(TEXT("140m NOT within budget -> U invalid at this size"), B.bWithinBudget);
	}

	// (5) Past the per-axis cap: 180m slots -> 270 > 256. FAILS on axis (and total).
	{
		const int32 S = UFPSRFlowFieldComputer::SlotCellsForSize(180.0f * 100.0f, CellCm); // 90
		const FBudget B = UFPSRFlowFieldComputer::CheckUnifiedGridBudget(S, S);
		AddInfo(B.Reason);
		TestFalse(TEXT("180m exceeds axis cap (270>256)"), B.bAxisWithinCap);
		TestFalse(TEXT("180m NOT within budget"), B.bWithinBudget);
	}

	return true;
}

// (B) Near-cap recompute benchmark — front-loads the "single-field recompute ms is unmeasured" risk (Perf §5).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSRFlowFieldRecomputeBenchTest, "FPSRoguelite.FlowField.RecomputeBench",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFPSRFlowFieldRecomputeBenchTest::RunTest(const FString& Parameters)
{
	// Near-cap grid at the D1 upper end: 198x198 = 39,204 cells (132m slots @200cm, no wall). This is the largest
	// single field U can legally carry, so its RunBFS is the worst-case per-door-open recompute cost under U.
	const int32 W = 198, H = 198;
	const float CellCm = 200.0f;
	const int32 TotalCells = W * H;
	TestEqual(TEXT("bench grid at near-cap (39204 cells)"), TotalCells, 39204);
	TestTrue(TEXT("bench grid is within the U budget"),
		UFPSRFlowFieldComputer::CheckUnifiedGridBudget(66, 66).bWithinBudget);

	TStrongObjectPtr<UFPSRFlowFieldComputer> C(NewObject<UFPSRFlowFieldComputer>());

	// One-time adopt (per-map stream-in cost, NOT per-door). Timed separately for context.
	const double BakeStart = FPlatformTime::Seconds();
	C->BuildFromSurfaceData(MakeFullFlatGrid(W, H, CellCm));
	const double BakeMs = (FPlatformTime::Seconds() - BakeStart) * 1000.0;

	// Per-door-open recompute = one full single RunBFS over the whole grid. Warm once, then take best-of-N.
	const TArray<int32> Source = { SurfB(0, 0) };
	C->RunBFS(Source); // warm
	TestTrue(TEXT("field ready after bench BFS"), C->IsFieldReady());
	// Sanity: source integrates to 0, the far corner is reachable (grid is fully connected).
	TestEqual(TEXT("source Dist == 0"), C->GetDist(SurfB(0, 0)), 0);
	TestTrue(TEXT("far corner reachable"), C->GetDist(SurfB(W * H - 1, 0)) != MAX_int32);

	const int32 Iters = 8;
	double BestMs = 0.0;
	double SumMs = 0.0;
	for (int32 i = 0; i < Iters; ++i)
	{
		const double T0 = FPlatformTime::Seconds();
		C->RunBFS(Source);
		const double Ms = (FPlatformTime::Seconds() - T0) * 1000.0;
		BestMs = (i == 0) ? Ms : FMath::Min(BestMs, Ms);
		SumMs += Ms;
	}
	const double AvgMs = SumMs / Iters;

	// The VALUE is the logged number — it feeds the P-A budget decision, not a pass/fail threshold.
	AddInfo(FString::Printf(
		TEXT("[U P-0 RecomputeBench] near-cap %dx%d=%d cells @%.0fcm: BuildFromSurfaceData=%.2fms, ")
		TEXT("RunBFS best=%.3fms avg=%.3fms over %d iters (single source)."),
		W, H, TotalCells, CellCm, BakeMs, BestMs, AvgMs, Iters));

	// Catastrophe guard only: a pure-array BFS over ~40k cells is expected in single-digit ms. Blowing past a full
	// second means the recompute is superlinear/broken and U's door-open hitch is unviable (=> partial BFS / smaller
	// slots / coarser cells), which is a design signal, not a green check. Loose to stay non-flaky on a loaded box.
	TestTrue(TEXT("near-cap RunBFS completes under 1000ms catastrophe ceiling"), BestMs < 1000.0);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
