// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Enemy/FPSRFlowFieldComputer.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_AUTOMATION_TESTS

// v2 hover height ("부양 이동 v2" plan, 2026-08-14): worldless proof for the CellFloorZ bilinear sampler that
// replaces v1's 0.15s amortized scene-query + FInterpConstantTo glide (AFPSREnemyBase::ApplyGravity). Pure array
// math — no RunBFS / bFieldReady needed, because CellFloorZ is fixed at bake time, independent of the BFS/flow
// (see UFPSRFlowFieldComputer::SampleFloorZBilinear's header comment).

namespace
{
	constexpr int32 NLH = UFPSRFlowFieldComputer::NumLayers; // 2
	int32 SurfH(int32 Cell, int32 Rank) { return Cell * NLH + Rank; }

	// A single-layer WxH grid, every rank-0 surface at a UNIFORM FloorZ (rank 1 absent everywhere). No edges needed —
	// the bilinear sampler reads CellFloorZ directly and never touches EdgeMask/DistField.
	FFPSRFlowFieldSurfaceData MakeUniformGridH(int32 W, int32 H, float CellSize, const FVector& Origin, float FloorZ)
	{
		FFPSRFlowFieldSurfaceData D;
		D.GridDimX = W; D.GridDimY = H; D.GridOrigin = Origin; D.CellSize = CellSize;
		const int32 NumCells = W * H;
		D.CellFloorZ.Init(MAX_flt, NumCells * NLH);
		D.BlockedField.Init(false, NumCells * NLH);
		D.EdgeMask.Init(0, NumCells * 2);
		for (int32 Cell = 0; Cell < NumCells; ++Cell)
		{
			D.CellFloorZ[SurfH(Cell, 0)] = FloorZ;
		}
		return D;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSRFlowFieldHoverSampleTest, "FPSRoguelite.FlowField.HoverFloorSample",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFPSRFlowFieldHoverSampleTest::RunTest(const FString& Parameters)
{
	const float Cell = 100.0f;
	const float MaxDelta = 180.0f; // matches AFPSREnemyBase::MaxCrestStepUp, the real caller's budget

	// ---- (1) Flat uniform Z: interpolation anywhere inside a cell equals that Z ----
	{
		TStrongObjectPtr<UFPSRFlowFieldComputer> C(NewObject<UFPSRFlowFieldComputer>());
		C->BuildFromSurfaceData(MakeUniformGridH(4, 4, Cell, FVector::ZeroVector, 500.0f));

		float FloorZ = 0.0f;
		FVector Normal = FVector::ZeroVector;
		// An off-center point within cell (1,2) — not a cell center, so this exercises real interpolation weights.
		const FVector P(180.0f, 260.0f, 500.0f);
		TestTrue(TEXT("flat grid sample succeeds"), C->SampleFloorZBilinear(P, 500.0f, MaxDelta, FloorZ, &Normal));
		TestTrue(TEXT("flat grid interpolates to the uniform Z"), FMath::IsNearlyEqual(FloorZ, 500.0f, 1e-3f));
		TestTrue(TEXT("flat grid normal is straight up"), Normal.Equals(FVector::UpVector, 1e-3f));
	}

	// ---- (2) X-direction ramp: adjacent cells differ by 100 -> the midpoint between their centers is the average ----
	{
		const int32 W = 3, H = 1;
		FFPSRFlowFieldSurfaceData D = MakeUniformGridH(W, H, Cell, FVector::ZeroVector, 0.0f);
		D.CellFloorZ[SurfH(0, 0)] = 0.0f;   // cell (0,0), center X=50
		D.CellFloorZ[SurfH(1, 0)] = 100.0f; // cell (1,0), center X=150 — the anchor cell
		D.CellFloorZ[SurfH(2, 0)] = 100.0f;
		TStrongObjectPtr<UFPSRFlowFieldComputer> C(NewObject<UFPSRFlowFieldComputer>());
		C->BuildFromSurfaceData(D);

		float FloorZ = 0.0f;
		// X=100 sits exactly halfway between the two cell centers (50 and 150); Y=50 keeps FracY at 0 (single row).
		const FVector Mid(100.0f, 50.0f, 100.0f);
		TestTrue(TEXT("ramp midpoint sample succeeds"), C->SampleFloorZBilinear(Mid, 100.0f, MaxDelta, FloorZ));
		TestTrue(TEXT("ramp midpoint is the linear average (50)"), FMath::IsNearlyEqual(FloorZ, 50.0f, 1e-3f));
	}

	// ---- (3) A corner on a DIFFERENT layer (delta > MaxSurfaceDeltaCm) is excluded — anchor Z substituted instead ----
	{
		const int32 W = 2, H = 2;
		FFPSRFlowFieldSurfaceData D = MakeUniformGridH(W, H, Cell, FVector::ZeroVector, 200.0f); // all 4 cells @ 200
		D.CellFloorZ[SurfH(1, 0)] = 200.0f + 400.0f; // cell (1,0): a different storey, 400cm above (> 180 MaxDelta)
		TStrongObjectPtr<UFPSRFlowFieldComputer> C(NewObject<UFPSRFlowFieldComputer>());
		C->BuildFromSurfaceData(D);

		float FloorZ = 0.0f;
		// Sample point in the (0,0)/(1,0)/(0,1)/(1,1) quad, anchored on cell (0,0) @ Z=200.
		const FVector P(60.0f, 60.0f, 200.0f);
		TestTrue(TEXT("far-layer-corner sample succeeds"), C->SampleFloorZBilinear(P, 200.0f, MaxDelta, FloorZ));
		TestTrue(TEXT("far-layer corner is flattened to the anchor Z (no story jump)"), FMath::IsNearlyEqual(FloorZ, 200.0f, 1e-3f));
	}

	// ---- (4) A corner with NO surface (MAX_flt) is likewise flattened to the anchor Z ----
	{
		const int32 W = 2, H = 2;
		FFPSRFlowFieldSurfaceData D = MakeUniformGridH(W, H, Cell, FVector::ZeroVector, 150.0f);
		D.CellFloorZ[SurfH(1, 1)] = MAX_flt; // cell (1,1): no floor at all
		TStrongObjectPtr<UFPSRFlowFieldComputer> C(NewObject<UFPSRFlowFieldComputer>());
		C->BuildFromSurfaceData(D);

		float FloorZ = 0.0f;
		const FVector P(60.0f, 60.0f, 150.0f);
		TestTrue(TEXT("missing-corner sample succeeds"), C->SampleFloorZBilinear(P, 150.0f, MaxDelta, FloorZ));
		TestTrue(TEXT("missing corner is flattened to the anchor Z"), FMath::IsNearlyEqual(FloorZ, 150.0f, 1e-3f));
	}

	// ---- (5) Anchor cell has no surface within MaxLayerPickDrop of FootZ -> false. Off-grid -> false. Unbuilt -> false. ----
	{
		TStrongObjectPtr<UFPSRFlowFieldComputer> C(NewObject<UFPSRFlowFieldComputer>());
		C->BuildFromSurfaceData(MakeUniformGridH(3, 3, Cell, FVector::ZeroVector, 0.0f));

		float FloorZ = 0.0f;
		// AnchorFootZ 500cm above the only surface (Z=0) exceeds MaxLayerPickDrop (200) -> PickRankForFootZ finds none.
		TestFalse(TEXT("anchor cell with no surface near FootZ fails"), C->SampleFloorZBilinear(FVector(150.0f, 150.0f, 500.0f), 500.0f, MaxDelta, FloorZ));
		TestFalse(TEXT("off-grid location fails"), C->SampleFloorZBilinear(FVector(-9999.0f, -9999.0f, 0.0f), 0.0f, MaxDelta, FloorZ));

		// Unbuilt grid (no BuildFromSurfaceData call at all) -> false, not a crash.
		TStrongObjectPtr<UFPSRFlowFieldComputer> Empty(NewObject<UFPSRFlowFieldComputer>());
		TestFalse(TEXT("unbuilt grid fails"), Empty->SampleFloorZBilinear(FVector::ZeroVector, 0.0f, MaxDelta, FloorZ));
	}

	// ---- (6) Boundary clamp: a point whose interpolation quad reaches outside the grid still samples (edge Z extends) ----
	{
		const int32 W = 2, H = 2;
		TStrongObjectPtr<UFPSRFlowFieldComputer> C(NewObject<UFPSRFlowFieldComputer>());
		C->BuildFromSurfaceData(MakeUniformGridH(W, H, Cell, FVector::ZeroVector, 300.0f));

		float FloorZ = 0.0f;
		// Cell (0,0)'s center is at (50,50); a point near the grid's MIN corner (10,10) needs the (-1,-1) quad
		// corner, which is off-grid and must clamp to (0,0) instead of failing / reading out of bounds.
		const FVector NearMinCorner(10.0f, 10.0f, 300.0f);
		TestTrue(TEXT("near-edge sample succeeds (clamped corners)"), C->SampleFloorZBilinear(NearMinCorner, 300.0f, MaxDelta, FloorZ));
		TestTrue(TEXT("clamped edge sample matches the uniform Z"), FMath::IsNearlyEqual(FloorZ, 300.0f, 1e-3f));
	}

	// ---- (7) Look-ahead (SampleHoverFloorZ): ahead-higher adopts the max, ahead-lower keeps current, zero flow skips it ----
	{
		const int32 W = 3, H = 1;
		FFPSRFlowFieldSurfaceData D = MakeUniformGridH(W, H, Cell, FVector::ZeroVector, 100.0f);
		D.CellFloorZ[SurfH(0, 0)] = 50.0f;  // "behind" cell — genuinely LOWER than "here"
		D.CellFloorZ[SurfH(1, 0)] = 100.0f; // "here" cell
		D.CellFloorZ[SurfH(2, 0)] = 250.0f; // "ahead" cell (a step up), within MaxDelta of the anchor
		TStrongObjectPtr<UFPSRFlowFieldComputer> C(NewObject<UFPSRFlowFieldComputer>());
		C->BuildFromSurfaceData(D);

		// Sample AT cell (1,0)'s center (X=150,Y=50) so "here" == 100 exactly (FracX lands on the corner, no blend
		// from the neighbours) regardless of which direction is probed.
		const FVector Here(150.0f, 50.0f, 100.0f);
		float FloorZ = 0.0f;

		TestTrue(TEXT("look-ahead toward the higher step succeeds"), C->SampleHoverFloorZ(Here, FVector2D(1.0f, 0.0f), 100.0f, MaxDelta, FloorZ));
		TestTrue(TEXT("ahead-higher adopts the max (pre-rise before the step)"), FloorZ > 100.0f + 1.0f);

		TestTrue(TEXT("look-ahead away from the step succeeds"), C->SampleHoverFloorZ(Here, FVector2D(-1.0f, 0.0f), 100.0f, MaxDelta, FloorZ));
		TestTrue(TEXT("ahead-lower keeps the current Z"), FMath::IsNearlyEqual(FloorZ, 100.0f, 1e-3f));

		TestTrue(TEXT("zero flow direction succeeds (no look-ahead)"), C->SampleHoverFloorZ(Here, FVector2D::ZeroVector, 100.0f, MaxDelta, FloorZ));
		TestTrue(TEXT("zero flow direction samples current only"), FMath::IsNearlyEqual(FloorZ, 100.0f, 1e-3f));

		// A "here" sample failure (off-grid) propagates false regardless of FlowDirXY.
		TestFalse(TEXT("hover sample fails when the current position is off-grid"),
			C->SampleHoverFloorZ(FVector(-9999.0f, -9999.0f, 100.0f), FVector2D(1.0f, 0.0f), 100.0f, MaxDelta, FloorZ));
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
