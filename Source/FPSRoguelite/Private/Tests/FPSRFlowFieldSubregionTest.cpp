// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Enemy/FPSRFlowFieldComputer.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_AUTOMATION_TESTS

// P-A (2026-07-07, U continuous-field): worldless proof for the fixed-3x3 unified grid + subregion atomic bake.
// Codex R1 contracts (Docs/Review/_raw/20260707-PA-subregion-bake-R1-codex.md), all asserted here:
//   - two slots commit into ONE grid; the single field covers both (each flows to its own source);
//   - the inter-slot boundary is CLOSED by default (commit copies only internal edges) -> no cross-slot path;
//   - opening a boundary edge (SetSurfaceEdge, the P-B door-stamp primitive) makes the single field cross the seam;
//   - ClearSubregion on unload resets cells + clears boundary edges BOTH sides -> no ghost path (incl. diagonal);
//   - every topology mutation (Commit / Clear / SetSurfaceEdge) invalidates the field (no stale flow before RunBFS);
//   - fail-closed: a slot that is misaligned / out-of-bounds / wrong-cell-size / has no reachable floor is rejected.

namespace
{
	constexpr int32 NLS = UFPSRFlowFieldComputer::NumLayers; // 2
	int32 SurfS(int32 Cell, int32 Rank) { return Cell * NLS + Rank; }

	// A slot-local W x H flat grid at Origin: rank0 floor everywhere, all INTERNAL orthogonal rank0<->rank0 edges open.
	FFPSRFlowFieldSurfaceData MakeSlot(int32 W, int32 H, float CellSize, const FVector& Origin)
	{
		FFPSRFlowFieldSurfaceData D;
		D.GridDimX = W; D.GridDimY = H; D.GridOrigin = Origin; D.CellSize = CellSize;
		const int32 NumCells = W * H;
		D.CellFloorZ.Init(MAX_flt, NumCells * NLS);
		D.BlockedField.Init(false, NumCells * NLS);
		D.EdgeMask.Init(0, NumCells * 2);
		for (int32 CY = 0; CY < H; ++CY)
		{
			for (int32 CX = 0; CX < W; ++CX)
			{
				const int32 Cell = CY * W + CX;
				D.CellFloorZ[SurfS(Cell, 0)] = Origin.Z;
				if (CX + 1 < W) { D.EdgeMask[Cell * 2 + 0] |= (1u << (0 * NLS + 0)); }
				if (CY + 1 < H) { D.EdgeMask[Cell * 2 + 1] |= (1u << (0 * NLS + 0)); }
			}
		}
		return D;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSRFlowFieldSubregionTest, "FPSRoguelite.FlowField.Subregion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFPSRFlowFieldSubregionTest::RunTest(const FString& Parameters)
{
	const float Cell = 100.0f;
	const FVector Origin(0, 0, 0);

	// Unified 6x3 grid = two 3x3 slots side by side: slot A cols 0-2 (offset (0,0)), slot B cols 3-5 (offset (3,0)).
	const int32 UA0 = 0 * 6 + 0;    // unified (0,0) = slot A origin
	const int32 UAr = 0 * 6 + 2;    // unified (2,0) = slot A right-boundary cell
	const int32 UB0 = 0 * 6 + 3;    // unified (3,0) = slot B left-boundary cell
	const int32 UBf = 0 * 6 + 5;    // unified (5,0) = slot B far cell

	TStrongObjectPtr<UFPSRFlowFieldComputer> C(NewObject<UFPSRFlowFieldComputer>());
	C->BuildEmptyGrid(6, 3, Origin, Cell);
	TestEqual(TEXT("empty grid dim X"), C->GetGridDimX(), 6);
	TestEqual(TEXT("empty grid dim Y"), C->GetGridDimY(), 3);
	TestFalse(TEXT("empty grid not ready"), C->IsFieldReady());
	TestTrue(TEXT("empty grid cell absent"), C->GetCellFloorZ(SurfS(UA0, 0)) == MAX_flt);

	// --- Commit both slots (slot B origin must be cell-aligned: Origin + (3,0)*Cell = (300,0,0)). ---
	TestTrue(TEXT("commit slot A"), C->CommitSubregion(FIntPoint(0, 0), MakeSlot(3, 3, Cell, Origin)));
	TestTrue(TEXT("commit slot B"), C->CommitSubregion(FIntPoint(3, 0), MakeSlot(3, 3, Cell, FVector(300, 0, 0))));
	TestFalse(TEXT("commit invalidates field"), C->IsFieldReady());
	TestTrue(TEXT("slot A cell present after commit"), C->GetCellFloorZ(SurfS(UA0, 0)) == 0.0f);
	TestTrue(TEXT("slot B cell present after commit"), C->GetCellFloorZ(SurfS(UB0, 0)) == 0.0f);

	// --- Boundary CLOSED by default: a source in slot A leaves slot B unreachable (no cross-slot edge was copied). ---
	C->RunBFS({ SurfS(UA0, 0) });
	TestTrue(TEXT("field ready after BFS"), C->IsFieldReady());
	TestTrue(TEXT("slot A reachable"), C->GetDist(SurfS(UAr, 0)) != MAX_int32);
	TestTrue(TEXT("slot B UNREACHABLE across the closed seam"), C->GetDist(SurfS(UB0, 0)) == MAX_int32);

	// --- Single field covers BOTH slots: a source in each -> both slots get a valid field. ---
	C->RunBFS({ SurfS(UA0, 0), SurfS(UB0, 0) });
	TestEqual(TEXT("slot A source dist 0"), C->GetDist(SurfS(UA0, 0)), 0);
	TestEqual(TEXT("slot B source dist 0"), C->GetDist(SurfS(UB0, 0)), 0);
	TestTrue(TEXT("slot B far cell reachable from its own source"), C->GetDist(SurfS(UBf, 0)) != MAX_int32);

	// --- Open the seam across (2,y)-(3,y) for all rows (P-B door-stamp primitive) -> the single field crosses it. ---
	for (int32 Y = 0; Y < 3; ++Y)
	{
		C->SetSurfaceEdge(Y * 6 + 2, 0, Y * 6 + 3, 0, /*bOpen=*/true);
	}
	TestFalse(TEXT("edge stamp invalidates field"), C->IsFieldReady());
	C->RunBFS({ SurfS(UA0, 0) });
	TestTrue(TEXT("slot B now REACHABLE across the open seam"), C->GetDist(SurfS(UB0, 0)) != MAX_int32);
	TestEqual(TEXT("slot B near cell dist = 3 across seam (0,0)->(3,0)"), C->GetDist(SurfS(UB0, 0)), 3);

	// --- ClearSubregion(slot B) on unload: cells absent + boundary edges cleared BOTH sides -> no ghost path. ---
	C->ClearSubregion(FIntPoint(3, 0), FIntPoint(3, 3));
	TestFalse(TEXT("clear invalidates field"), C->IsFieldReady());
	TestTrue(TEXT("cleared slot B cell absent"), C->GetCellFloorZ(SurfS(UB0, 0)) == MAX_flt);
	// The opened seam edge lived on cell (2,0) (OUTSIDE slot B) -> the both-sides clear must have removed it.
	TestFalse(TEXT("seam edge (2,0)->(3,0) not traversable after clear (both-side boundary clear)"),
		C->IsSurfaceEdgeTraversable(2, 0, 3, 0));
	C->RunBFS({ SurfS(UA0, 0) });
	TestTrue(TEXT("slot A still reachable after clearing B"), C->GetDist(SurfS(UAr, 0)) != MAX_int32);
	TestTrue(TEXT("cleared slot B unreachable (no ghost path)"), C->GetDist(SurfS(UB0, 0)) == MAX_int32);
	TestTrue(TEXT("cleared slot B flow is zero"), C->GetFlow(SurfS(UB0, 0)).IsNearlyZero());
	// Slot A's right-boundary cell must not retain any flow INTO the cleared slot B (diagonal ghost kill).
	TestTrue(TEXT("A boundary cell (2,1) has no +X flow into cleared B"),
		C->GetFlow(SurfS(1 * 6 + 2, 0)).X <= 0.01f);

	// --- Fail-closed rejections (each returns false and does NOT mutate the grid). ---
	{
		FFPSRFlowFieldSurfaceData NoFloor = MakeSlot(3, 3, Cell, FVector(300, 0, 0));
		for (float& Z : NoFloor.CellFloorZ) { Z = MAX_flt; }
		TestFalse(TEXT("no-reachable-floor slot rejected"), C->CommitSubregion(FIntPoint(3, 0), NoFloor));
		TestFalse(TEXT("misaligned origin rejected"), C->CommitSubregion(FIntPoint(3, 0), MakeSlot(3, 3, Cell, FVector(350, 0, 0))));
		TestFalse(TEXT("out-of-bounds offset rejected"), C->CommitSubregion(FIntPoint(4, 0), MakeSlot(3, 3, Cell, FVector(400, 0, 0))));
		TestFalse(TEXT("cell-size mismatch rejected"), C->CommitSubregion(FIntPoint(3, 0), MakeSlot(3, 3, 200.0f, FVector(300, 0, 0))));
		FFPSRFlowFieldSurfaceData BadStep = MakeSlot(3, 3, Cell, FVector(300, 0, 0));
		BadStep.ClimbableStepHeight = 60.0f; // != unified default 45 -> U's uniform-step contract rejects it (Codex R3)
		TestFalse(TEXT("step-mismatch slot rejected"), C->CommitSubregion(FIntPoint(3, 0), BadStep));
	}

	// --- ClimbableStepHeight survives the surface-data adopt round-trip (Codex R4): BuildFromSurfaceData must carry it so
	//     ExtractSurfaceData reports the real baked step, not the default. ---
	{
		FFPSRFlowFieldSurfaceData In = MakeSlot(2, 2, Cell, FVector::ZeroVector);
		In.ClimbableStepHeight = 60.0f;
		TStrongObjectPtr<UFPSRFlowFieldComputer> RT(NewObject<UFPSRFlowFieldComputer>());
		RT->BuildFromSurfaceData(In);
		FFPSRFlowFieldSurfaceData Out;
		RT->ExtractSurfaceData(Out);
		TestEqual(TEXT("ClimbableStepHeight carried through BuildFromSurfaceData -> ExtractSurfaceData"), Out.ClimbableStepHeight, 60.0f);

		// An over-cap step is clamped to the ground-snap limit (60cm) on adopt (Codex R5) so door stamps never exceed it.
		FFPSRFlowFieldSurfaceData Over = MakeSlot(2, 2, Cell, FVector::ZeroVector);
		Over.ClimbableStepHeight = 80.0f;
		RT->BuildFromSurfaceData(Over);
		RT->ExtractSurfaceData(Out);
		TestEqual(TEXT("over-cap ClimbableStepHeight clamped to 60 on adopt"), Out.ClimbableStepHeight, 60.0f);
	}

	// --- Fail-closed re-bake (Codex R6): a FAILED re-commit over an EXISTING slot SEALS it (clears the stale data), so a
	//     re-stream with missing collision never leaves the old slot traversable. ---
	{
		TStrongObjectPtr<UFPSRFlowFieldComputer> C2(NewObject<UFPSRFlowFieldComputer>());
		C2->BuildEmptyGrid(3, 3, FVector::ZeroVector, Cell);
		TestTrue(TEXT("initial slot commit"), C2->CommitSubregion(FIntPoint(0, 0), MakeSlot(3, 3, Cell, FVector::ZeroVector)));
		TestTrue(TEXT("slot present after initial commit"), C2->GetCellFloorZ(SurfS(0, 0)) == 0.0f);

		FFPSRFlowFieldSurfaceData NoFloor2 = MakeSlot(3, 3, Cell, FVector::ZeroVector);
		for (float& Z : NoFloor2.CellFloorZ) { Z = MAX_flt; }
		TestFalse(TEXT("failed re-bake returns false"), C2->CommitSubregion(FIntPoint(0, 0), NoFloor2));
		TestTrue(TEXT("failed re-bake SEALED the prior slot (cell now absent)"), C2->GetCellFloorZ(SurfS(0, 0)) == MAX_flt);
	}

	// --- Re-commit clears stale door edges (Codex R7): a door opened before a slot re-bake must NOT survive the re-commit
	//     (the new boundary geometry may not support it); the seal-on-commit clears it so doors are re-stamped afterward. ---
	{
		TStrongObjectPtr<UFPSRFlowFieldComputer> C3(NewObject<UFPSRFlowFieldComputer>());
		C3->BuildEmptyGrid(6, 3, Origin, Cell);
		C3->CommitSubregion(FIntPoint(0, 0), MakeSlot(3, 3, Cell, Origin));
		C3->CommitSubregion(FIntPoint(3, 0), MakeSlot(3, 3, Cell, FVector(300, 0, 0)));
		C3->SetSurfaceEdge(1 * 6 + 2, 0, 1 * 6 + 3, 0, true); // open a door on the A|B seam at row 1
		TestTrue(TEXT("door open before re-commit"), C3->IsSurfaceEdgeTraversable(1 * 6 + 2, 0, 1 * 6 + 3, 0));
		TestTrue(TEXT("re-commit slot B"), C3->CommitSubregion(FIntPoint(3, 0), MakeSlot(3, 3, Cell, FVector(300, 0, 0))));
		TestFalse(TEXT("stale door edge cleared by re-commit (must be re-stamped)"), C3->IsSurfaceEdgeTraversable(1 * 6 + 2, 0, 1 * 6 + 3, 0));
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
