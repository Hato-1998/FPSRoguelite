// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Enemy/FPSRFlowFieldComputer.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_AUTOMATION_TESTS

// Headless regression net for the worldless flow-field core (Codex consult 2026-07-06). Feeds hand-authored
// surface graphs into UFPSRFlowFieldComputer::BuildFromSurfaceData + RunBFS (NO world query) and asserts the
// BFS / steepest-descent / layer-pick invariants that a silent refactor regression would break — the pieces
// that build+ModuleLoads can't catch and that PIE can't reach this session (map content absent).

namespace
{
	constexpr int32 NL = UFPSRFlowFieldComputer::NumLayers; // 2

	int32 Surf(int32 Cell, int32 Rank) { return Cell * NL + Rank; }

	// A flat W x H single-layer grid: rank 0 walkable at FloorZ everywhere, rank 1 absent, no blocks, all
	// orthogonal rank0<->rank0 edges open. Cell (CX,CY) = CY*W+CX.
	FFPSRFlowFieldSurfaceData MakeFlatGrid(int32 W, int32 H, float CellSize, const FVector& Origin, float FloorZ)
	{
		FFPSRFlowFieldSurfaceData D;
		D.GridDimX = W;
		D.GridDimY = H;
		D.GridOrigin = Origin;
		D.CellSize = CellSize;
		const int32 NumCells = W * H;
		D.CellFloorZ.Init(MAX_flt, NumCells * NL);
		D.BlockedField.Init(false, NumCells * NL);
		D.EdgeMask.Init(0, NumCells * 2);
		for (int32 CY = 0; CY < H; ++CY)
		{
			for (int32 CX = 0; CX < W; ++CX)
			{
				const int32 Cell = CY * W + CX;
				D.CellFloorZ[Surf(Cell, 0)] = FloorZ;                 // rank 0 present
				if (CX + 1 < W) { D.EdgeMask[Cell * 2 + 0] |= (1u << (0 * NL + 0)); } // +X rank0->rank0
				if (CY + 1 < H) { D.EdgeMask[Cell * 2 + 1] |= (1u << (0 * NL + 0)); } // +Y rank0->rank0
			}
		}
		return D;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSRFlowFieldUnitTest, "FPSRoguelite.FlowField.Unit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFPSRFlowFieldUnitTest::RunTest(const FString& Parameters)
{
	const float Cell = 100.0f;

	// ---- (1) Flat grid: array sizes, source zero, Dist = manhattan, flow toward source, OOB zero ----
	{
		const int32 W = 6, H = 5;
		TStrongObjectPtr<UFPSRFlowFieldComputer> C(NewObject<UFPSRFlowFieldComputer>());
		C->BuildFromSurfaceData(MakeFlatGrid(W, H, Cell, FVector::ZeroVector, 0.0f));

		// Array sizes (via public reads at the extreme indices).
		TestEqual(TEXT("GridDimX"), C->GetGridDimX(), W);
		TestEqual(TEXT("GridDimY"), C->GetGridDimY(), H);
		TestTrue(TEXT("last surface floor readable"), C->GetCellFloorZ(Surf(W * H - 1, 0)) == 0.0f);

		const int32 SrcCell = 0; // (0,0)
		C->RunBFS({ Surf(SrcCell, 0) });
		TestTrue(TEXT("field ready after BFS with a source"), C->IsFieldReady());

		TestEqual(TEXT("source Dist is 0"), C->GetDist(Surf(SrcCell, 0)), 0);
		// 4-connected uniform BFS on an open grid -> Dist == manhattan from source.
		const int32 FarCell = 2 * W + 3; // (3,2)
		TestEqual(TEXT("Dist == manhattan (3,2)"), C->GetDist(Surf(FarCell, 0)), 3 + 2);
		// Source cell flow is zero (it's the minimum).
		TestTrue(TEXT("source flow is zero"), C->GetFlow(Surf(SrcCell, 0)).IsNearlyZero());
		// A cell east of the source flows back toward -X (toward the source side).
		const int32 EastCell = 0 * W + 3; // (3,0)
		TestTrue(TEXT("east cell flow points toward source (-X)"), C->GetFlow(Surf(EastCell, 0)).X < -0.01f);

		// Sample: an enemy actor standing on (3,0) (Z = floor + EnemyStandOffset 95) samples that cell's flow.
		const FVector EnemyLoc(3.5f * Cell, 0.5f * Cell, 95.0f);
		const FVector S = C->Sample(EnemyLoc);
		TestTrue(TEXT("sample at (3,0) points toward source (-X)"), S.X < -0.01f);
		TestTrue(TEXT("sample Z is flat"), FMath::IsNearlyZero(S.Z));

		// Out-of-bounds sample -> zero.
		TestTrue(TEXT("OOB sample is zero"), C->Sample(FVector(-9999.0f, -9999.0f, 95.0f)).IsNearlyZero());
	}

	// ---- (2) Empty source set -> not ready, sample zero ----
	{
		TStrongObjectPtr<UFPSRFlowFieldComputer> C(NewObject<UFPSRFlowFieldComputer>());
		C->BuildFromSurfaceData(MakeFlatGrid(4, 4, Cell, FVector::ZeroVector, 0.0f));
		C->RunBFS({});
		TestFalse(TEXT("no source -> field not ready"), C->IsFieldReady());
		TestTrue(TEXT("no source -> sample zero"), C->Sample(FVector(1.5f * Cell, 1.5f * Cell, 95.0f)).IsNearlyZero());
	}

	// ---- (3) Obstacle: a blocked wall column with a single gap forces a detour; a fully-walled cell is unreachable ----
	{
		const int32 W = 5, H = 5;
		FFPSRFlowFieldSurfaceData D = MakeFlatGrid(W, H, Cell, FVector::ZeroVector, 0.0f);
		// Wall at CX==2 for rows CY 0..3; the ONLY gap is the top row (2,4). Mark the wall surfaces occupancy-blocked.
		for (int32 CY = 0; CY < H - 1; ++CY)
		{
			D.BlockedField[Surf(CY * W + 2, 0)] = true;
		}
		TStrongObjectPtr<UFPSRFlowFieldComputer> C(NewObject<UFPSRFlowFieldComputer>());
		C->BuildFromSurfaceData(D);
		C->RunBFS({ Surf(0 /*(0,0)*/, 0) });

		// (4,0) is directly across the wall on the SAME row as the source (manhattan 4), but the only crossing is the
		// gap at the top (2,4) — so the real path climbs up, over, and back down: strictly longer than 4.
		const int32 Target = 0 * W + 4; // (4,0)
		TestTrue(TEXT("walled target still reachable via the top gap"), C->GetDist(Surf(Target, 0)) != MAX_int32);
		TestTrue(TEXT("detour Dist exceeds the straight-line manhattan"), C->GetDist(Surf(Target, 0)) > 4);
		// A blocked wall cell is never a through-path: the BFS never enters it, so its distance stays unreachable.
		TestTrue(TEXT("blocked wall cell has MAX dist (never entered by BFS)"), C->GetDist(Surf(2 * W + 2, 0)) == MAX_int32);
	}

	// ---- (4) U7 multi-layer: a disconnected upper deck (rank 1, no edges to it, not a source) stays unreachable ----
	{
		const int32 W = 3, H = 1;
		FFPSRFlowFieldSurfaceData D = MakeFlatGrid(W, H, Cell, FVector::ZeroVector, 0.0f);
		const int32 MidCell = 1; // (1,0)
		D.CellFloorZ[Surf(MidCell, 1)] = 300.0f; // an upper deck surface at the middle cell, with NO edge bits set to it
		TStrongObjectPtr<UFPSRFlowFieldComputer> C(NewObject<UFPSRFlowFieldComputer>());
		C->BuildFromSurfaceData(D);
		C->RunBFS({ Surf(0 /*(0,0)*/, 0) });

		TestTrue(TEXT("ground rank0 reachable across the row"), C->GetDist(Surf(2 /*(2,0)*/, 0)) != MAX_int32);
		TestTrue(TEXT("disconnected upper deck (rank1) is unreachable"), C->GetDist(Surf(MidCell, 1)) == MAX_int32);
		TestTrue(TEXT("disconnected upper deck flow is zero"), C->GetFlow(Surf(MidCell, 1)).IsNearlyZero());

		// PickRankForFootZ: ground foot picks rank0, deck-height foot picks rank1, a foot far below the deck rejects it.
		TestEqual(TEXT("foot at ground Z picks rank 0"), C->PickRankForFootZ(MidCell, 0.0f), 0);
		TestEqual(TEXT("foot at deck Z picks rank 1"), C->PickRankForFootZ(MidCell, 300.0f), 1);
		TestEqual(TEXT("foot near deck (within pick drop) picks rank 1"), C->PickRankForFootZ(MidCell, 290.0f), 1);
		// A cell with only rank0 present: any foot far above it (> MaxLayerPickDrop) resolves to none.
		TestEqual(TEXT("foot a storey above a single ground surface -> no rank"), C->PickRankForFootZ(0, 500.0f), INDEX_NONE);
	}

	// ---- (5) Multi-computer isolation: two independent computers with different origins/sources don't interfere ----
	{
		TStrongObjectPtr<UFPSRFlowFieldComputer> A(NewObject<UFPSRFlowFieldComputer>());
		TStrongObjectPtr<UFPSRFlowFieldComputer> B(NewObject<UFPSRFlowFieldComputer>());
		A->BuildFromSurfaceData(MakeFlatGrid(4, 4, Cell, FVector::ZeroVector, 0.0f));
		B->BuildFromSurfaceData(MakeFlatGrid(4, 4, Cell, FVector(100000.0f, 0.0f, 0.0f), 0.0f)); // far offset (multimap layout)
		A->RunBFS({ Surf(0, 0) });                 // A source at its (0,0)
		B->RunBFS({ Surf(3 * 4 + 3 /*(3,3)*/, 0) }); // B source at its (3,3)

		// A: cell (3,0) flows toward -X (A's source). B is unaffected by A.
		TestTrue(TEXT("A east cell flows toward A source"), A->GetFlow(Surf(3, 0)).X < -0.01f);
		// B: cell (0,3) flows toward +X (B's source at (3,3)).
		TestTrue(TEXT("B west cell flows toward B source"), B->GetFlow(Surf(3 * 4 + 0, 0)).X > 0.01f);
		// A sampling a location inside B's far grid returns zero (A's grid doesn't cover it).
		TestTrue(TEXT("A does not sample B's region"), A->Sample(FVector(100150.0f, 150.0f, 95.0f)).IsNearlyZero());
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
