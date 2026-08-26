// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Arena/FPSRArenaCells.h"
#include "Arena/FPSRArenaTypes.h"
#include "Enemy/FPSRFlowFieldComputer.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_AUTOMATION_TESTS

// P-B (2026-07-07, U continuous-field): worldless proof for door-cell stamping — the grid primitives a breaking door
// drives. A door in a slot wall is not caught by the WorldStatic bake, so the flow is told explicitly: intact = gap
// cells blocked; broken = gap unblocked + cross-slot edges opened from the neighbours' baked Z (Codex R1 Q1). The
// door-object -> cell mapping (which cells a given door owns) is the FPSRDoor/subsystem wiring, proven in-world (PIE).

namespace
{
	constexpr int32 NLD = UFPSRFlowFieldComputer::NumLayers; // 2
	int32 SurfD(int32 Cell, int32 Rank) { return Cell * NLD + Rank; }

	FFPSRFlowFieldSurfaceData MakeSlotD(int32 W, int32 H, float CellSize, const FVector& Origin)
	{
		FFPSRFlowFieldSurfaceData D;
		D.GridDimX = W; D.GridDimY = H; D.GridOrigin = Origin; D.CellSize = CellSize;
		const int32 NumCells = W * H;
		D.CellFloorZ.Init(MAX_flt, NumCells * NLD);
		D.BlockedField.Init(false, NumCells * NLD);
		D.EdgeMask.Init(0, NumCells * 2);
		for (int32 CY = 0; CY < H; ++CY)
		{
			for (int32 CX = 0; CX < W; ++CX)
			{
				const int32 Cell = CY * W + CX;
				D.CellFloorZ[SurfD(Cell, 0)] = Origin.Z;
				if (CX + 1 < W) { D.EdgeMask[Cell * 2 + 0] |= (1u << 0); }
				if (CY + 1 < H) { D.EdgeMask[Cell * 2 + 1] |= (1u << 0); }
			}
		}
		return D;
	}

	// Fresh 6x3 unified grid, two committed 3x3 slots (A cols 0-2 @ZA, B cols 3-5 @ZB); seam CLOSED by default.
	UFPSRFlowFieldComputer* MakeTwoSlotGrid(float ZA, float ZB)
	{
		UFPSRFlowFieldComputer* C = NewObject<UFPSRFlowFieldComputer>();
		C->BuildEmptyGrid(6, 3, FVector(0, 0, 0), 100.0f);
		C->CommitSubregion(FIntPoint(0, 0), MakeSlotD(3, 3, 100.0f, FVector(0, 0, ZA)));
		C->CommitSubregion(FIntPoint(3, 0), MakeSlotD(3, 3, 100.0f, FVector(300, 0, ZB)));
		return C;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSRFlowFieldDoorStampTest, "FPSRoguelite.FlowField.DoorStamp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFPSRFlowFieldDoorStampTest::RunTest(const FString& Parameters)
{
	const int32 UA0 = 0;          // (0,0) slot A origin
	const int32 A21 = 1 * 6 + 2;  // (2,1) slot A right-boundary, middle row
	const int32 B31 = 1 * 6 + 3;  // (3,1) slot B left-boundary, middle row
	const int32 B51 = 1 * 6 + 5;  // (5,1) slot B far, middle row
	const int32 B30 = 0 * 6 + 3;  // (3,0) slot B, bottom row

	// ---- (1) Flat door: open one cell-pair across the seam -> field crosses ONLY through that cell. ----
	{
		TStrongObjectPtr<UFPSRFlowFieldComputer> C(MakeTwoSlotGrid(0.0f, 0.0f));
		C->RunBFS({ SurfD(UA0, 0) });
		TestTrue(TEXT("slot B unreachable before the door opens"), C->GetDist(SurfD(B31, 0)) == MAX_int32);

		const int32 Opened = C->StampDoorEdgesOpen(A21, B31);
		TestEqual(TEXT("flat door opens exactly 1 rank-pair"), Opened, 1);
		TestFalse(TEXT("door stamp invalidates the field"), C->IsFieldReady());

		C->RunBFS({ SurfD(UA0, 0) });
		TestTrue(TEXT("slot B reachable through the open door"), C->GetDist(SurfD(B31, 0)) != MAX_int32);
		TestTrue(TEXT("slot B far cell reachable"), C->GetDist(SurfD(B51, 0)) != MAX_int32);
		// The door is only at row 1, so (3,0) must route THROUGH (3,1): its dist exceeds the straight manhattan of 3.
		TestTrue(TEXT("(3,0) routes through the row-1 door (dist > 3)"), C->GetDist(SurfD(B30, 0)) > 3);
	}

	// ---- (2) Z-incompatible door: a raised slot B (Z=500, > climbable step) cannot be door-connected. ----
	{
		TStrongObjectPtr<UFPSRFlowFieldComputer> C(MakeTwoSlotGrid(0.0f, 500.0f));
		const int32 Opened = C->StampDoorEdgesOpen(A21, B31);
		TestEqual(TEXT("Z-incompatible door opens 0 edges"), Opened, 0);
		C->RunBFS({ SurfD(UA0, 0) });
		TestTrue(TEXT("raised slot B stays unreachable (step too high)"), C->GetDist(SurfD(B31, 0)) == MAX_int32);
	}

	// ---- (2b) A step just above the ACTIVE bake step (50cm > 45cm default) but below the 60cm hard cap is still rejected,
	//      so a door never opens a seam the bake itself would not have (Codex R2 P2). ----
	{
		TStrongObjectPtr<UFPSRFlowFieldComputer> C(MakeTwoSlotGrid(0.0f, 50.0f));
		TestEqual(TEXT("50cm step (> active 45cm, < 60cm cap) door opens 0 edges"), C->StampDoorEdgesOpen(A21, B31), 0);
	}

	// ---- (3) Intact-door gap cell blocked: stamping the destination cell blocked closes the passage; unblock reopens. ----
	{
		TStrongObjectPtr<UFPSRFlowFieldComputer> C(MakeTwoSlotGrid(0.0f, 0.0f));
		C->StampDoorEdgesOpen(A21, B31);
		C->StampCellBlocked(B31, 0, true); // door "intact" -> gap cell blocked
		TestFalse(TEXT("cell block invalidates the field"), C->IsFieldReady());
		C->RunBFS({ SurfD(UA0, 0) });
		TestTrue(TEXT("blocked door cell is not a through-path"), C->GetDist(SurfD(B31, 0)) == MAX_int32);
		TestTrue(TEXT("slot B beyond the blocked cell unreachable"), C->GetDist(SurfD(B51, 0)) == MAX_int32);

		C->StampCellBlocked(B31, 0, false); // door breaks -> gap unblocked
		C->RunBFS({ SurfD(UA0, 0) });
		TestTrue(TEXT("unblocked door cell reachable"), C->GetDist(SurfD(B31, 0)) != MAX_int32);
	}

	// ---- (4) Non-adjacent / out-of-bounds door pairs are rejected (0 edges, no crash). ----
	{
		TStrongObjectPtr<UFPSRFlowFieldComputer> C(MakeTwoSlotGrid(0.0f, 0.0f));
		TestEqual(TEXT("non-adjacent cells open 0"), C->StampDoorEdgesOpen(UA0, B51), 0);
		TestEqual(TEXT("out-of-bounds cell opens 0"), C->StampDoorEdgesOpen(A21, 9999), 0);
	}

	return true;
}

// P-B door->cell mapping (② FPSRDoor wiring): MapDoorSeamCellPairs is pure grid geometry (WorldToCellIndex + adjacency,
// no floor read), so it is proven worldless here; the actor-bounds source + StampDoorEdgesOpen + recompute is the
// subsystem/FPSRDoor wiring, proven in-world (PIE). 6x3 grid, cell 100, origin (0,0,0): flat index = cy*6+cx.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSRFlowFieldDoorMapTest, "FPSRoguelite.FlowField.DoorMap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFPSRFlowFieldDoorMapTest::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<UFPSRFlowFieldComputer> C(NewObject<UFPSRFlowFieldComputer>());
	C->BuildEmptyGrid(6, 3, FVector(0, 0, 0), 100.0f);

	auto HasPair = [](const TArray<TPair<int32, int32>>& Pairs, int32 A, int32 B)
	{
		for (const TPair<int32, int32>& P : Pairs)
		{
			if ((P.Key == A && P.Value == B) || (P.Key == B && P.Value == A)) { return true; }
		}
		return false;
	};

	// (1) X-cross door on the seam X=300 (col 2|3 boundary), spanning rows 0-2 -> one pair per row.
	{
		TArray<TPair<int32, int32>> Pairs;
		C->MapDoorSeamCellPairs(FBox(FVector(290, 20, -10), FVector(310, 280, 10)), Pairs);
		TestEqual(TEXT("X-cross door maps 3 pairs (one per spanned row)"), Pairs.Num(), 3);
		TestTrue(TEXT("row0 pair (2,3)"), HasPair(Pairs, 2, 3));
		TestTrue(TEXT("row1 pair (8,9)"), HasPair(Pairs, 8, 9));
		TestTrue(TEXT("row2 pair (14,15)"), HasPair(Pairs, 14, 15));
	}

	// (2) Y-cross door on the seam Y=100 (row 0|1 boundary), spanning cols 0-2 -> one pair per col.
	{
		TArray<TPair<int32, int32>> Pairs;
		C->MapDoorSeamCellPairs(FBox(FVector(20, 90, -10), FVector(280, 110, 10)), Pairs);
		TestEqual(TEXT("Y-cross door maps 3 pairs (one per spanned col)"), Pairs.Num(), 3);
		TestTrue(TEXT("col0 pair (0,6)"), HasPair(Pairs, 0, 6));
		TestTrue(TEXT("col1 pair (1,7)"), HasPair(Pairs, 1, 7));
		TestTrue(TEXT("col2 pair (2,8)"), HasPair(Pairs, 2, 8));
	}

	// (3) Off-grid door -> no pairs (the NotifyDoorBroken warn/no-op path).
	{
		TArray<TPair<int32, int32>> Pairs;
		C->MapDoorSeamCellPairs(FBox(FVector(1000, 20, -10), FVector(1020, 280, 10)), Pairs);
		TestEqual(TEXT("off-grid door maps 0 pairs"), Pairs.Num(), 0);
	}

	// (4) Every mapped pair is orthogonally adjacent (the StampDoorEdgesOpen precondition).
	{
		TArray<TPair<int32, int32>> Pairs;
		C->MapDoorSeamCellPairs(FBox(FVector(290, 20, -10), FVector(310, 280, 10)), Pairs);
		for (const TPair<int32, int32>& P : Pairs)
		{
			const int32 AX = P.Key % 6, AY = P.Key / 6, BX = P.Value % 6, BY = P.Value / 6;
			const bool bAdj = (AY == BY && FMath::Abs(AX - BX) == 1) || (AX == BX && FMath::Abs(AY - BY) == 1);
			TestTrue(TEXT("mapped pair is orthogonally adjacent"), bAdj);
		}
	}

	return true;
}

// U P-D front-chase core: GetPathDistanceCells status + monotone path-distance + AreWorldLocationsConnected. Worldless
// (the enemy movement/drain wiring that consumes these is proven in-world, PIE). 6x3 grid, cell 100, origin (0,0,0).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSRFlowFieldFrontDistanceTest, "FPSRoguelite.FlowField.FrontDistance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFPSRFlowFieldFrontDistanceTest::RunTest(const FString& Parameters)
{
	// Two flat slots A(cols 0-2)/B(cols 3-5) @Z=0; seam CLOSED by default.
	TStrongObjectPtr<UFPSRFlowFieldComputer> C(MakeTwoSlotGrid(0.0f, 0.0f));
	EFPSRFieldQuery St = EFPSRFieldQuery::NoGrid;

	// No sources -> distance meaningless = SourceLess (connectivity still valid). Cell centers use foot Z within layer pick.
	C->RunBFS({});
	C->GetPathDistanceCells(FVector(50, 50, 0), St);
	TestEqual(TEXT("no sources -> SourceLess"), static_cast<int32>(St), static_cast<int32>(EFPSRFieldQuery::SourceLess));

	// Source in A, door CLOSED: A cell OK (finite), B cell Unreachable (different component), outside = OffGrid, A/B disconnected.
	C->RunBFS({ SurfD(0, 0) });
	const int32 dA = C->GetPathDistanceCells(FVector(250, 50, 0), St); // (2,0) in A
	TestEqual(TEXT("A reachable -> OK"), static_cast<int32>(St), static_cast<int32>(EFPSRFieldQuery::OK));
	TestTrue(TEXT("A distance finite"), dA >= 0 && dA < MAX_int32);
	C->GetPathDistanceCells(FVector(350, 50, 0), St); // (3,0) in B behind the closed seam
	TestEqual(TEXT("B behind closed seam -> Unreachable"), static_cast<int32>(St), static_cast<int32>(EFPSRFieldQuery::Unreachable));
	C->GetPathDistanceCells(FVector(9999, 50, 0), St);
	TestEqual(TEXT("outside grid -> OffGrid"), static_cast<int32>(St), static_cast<int32>(EFPSRFieldQuery::OffGrid));
	TestFalse(TEXT("A,B not connected while door closed"), C->AreWorldLocationsConnected(FVector(50, 50, 0), FVector(350, 50, 0)));

	// Open the door (2,0)<->(3,0): B becomes OK and strictly farther from the source than A; A<->B now connected.
	C->StampDoorEdgesOpen(2, 3);
	C->RunBFS({ SurfD(0, 0) });
	const int32 dA2 = C->GetPathDistanceCells(FVector(250, 50, 0), St);
	TestEqual(TEXT("A still OK after open"), static_cast<int32>(St), static_cast<int32>(EFPSRFieldQuery::OK));
	const int32 dB2 = C->GetPathDistanceCells(FVector(350, 50, 0), St);
	TestEqual(TEXT("B reachable through open door -> OK"), static_cast<int32>(St), static_cast<int32>(EFPSRFieldQuery::OK));
	TestTrue(TEXT("B farther than A (monotone path distance through the door)"), dB2 > dA2);
	TestTrue(TEXT("A,B connected after door open"), C->AreWorldLocationsConnected(FVector(50, 50, 0), FVector(350, 50, 0)));

	return true;
}

// D7 2D destructible-cell stamp: worldless proof for FFPSRArenaCells::ComputeCellsFromBoundsXY, the bounds-to-cell
// half of the fix that lets AFPSRArenaDestructible's own mesh bounds block 2D flow-field cells the same way
// FFPSRArenaVoxelData::SetOccupiedAABB already blocks 3D voxels at arena adoption (see
// UFPSRFlowFieldSubsystem::AdoptArenaFieldInternal's 2D stamp, added alongside this test). Pure grid arithmetic —
// no world/actor spawn needed, exactly like FFPSRArenaCells::ComputeDestructibleCells above; the actor-bounds
// source + StampCellBlocked + subsystem wiring is proven in-world (PIE).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSRArenaCellsFromBoundsTest, "FPSRoguelite.FlowField.ArenaCellsFromBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFPSRArenaCellsFromBoundsTest::RunTest(const FString& Parameters)
{
	const FVector ArenaOrigin(-8000.0f, -8000.0f, 0.0f);
	const float CellSize = 100.0f;
	const FIntPoint GridDims(160, 160);

	// ---- (1) A 4m box (half-size 200) centered on the world origin covers a clean 5x5=25 block, ascending. ----
	// Half-size 200 (a 400cm box), NOT the "500cm box = half-size 250" a naive 5m/100cm = "5x5" guess reaches for:
	// MinCX and MaxCX both use FloorToInt (see ComputeCellsFromBoundsXY), so ANY box whose WIDTH is an exact
	// multiple of CellSize touches (width/CellSize)+1 cells along that axis, regardless of where it sits relative
	// to the grid — provably so, since FloorToInt(Max/CS) - FloorToInt(Min/CS) == width/CS for any alignment. A
	// 500cm box (5x cell size) therefore always touches 6 cells, never 5; 400cm (4x) is what actually lands on 5.
	{
		const FBox Box(FVector(-200.0f, -200.0f, -10.0f), FVector(200.0f, 200.0f, 10.0f));
		TArray<int32> Cells;
		FFPSRArenaCells::ComputeCellsFromBoundsXY(Box, ArenaOrigin, CellSize, GridDims, Cells);

		TestEqual(TEXT("4m box centered on a grid line covers 25 cells (5x5)"), Cells.Num(), 25);
		// merge-gate P3 교정: a regression that changes Cells.Num() away from 25 must not ALSO crash this
		// automation session by indexing Cells[0]/Cells.Last() out of bounds — the verification tool is not
		// allowed to be the first thing that breaks when the thing it verifies regresses.
		if (Cells.Num() == 25)
		{
			TestEqual(TEXT("first cell is (78,78)"), Cells[0], 78 * GridDims.X + 78);
			TestEqual(TEXT("last cell is (82,82)"), Cells.Last(), 82 * GridDims.X + 82);
			for (int32 i = 1; i < Cells.Num(); ++i)
			{
				TestTrue(TEXT("cells come out strictly ascending"), Cells[i] > Cells[i - 1]);
			}
		}
	}

	// ---- (2) A box hanging off the grid's negative corner is CLIPPED: negative CX/CY are dropped, not wrapped
	//      or clamped into range (the untrimmed straddle is CX,CY in {-1,0}, 4 combinations; only (0,0) is
	//      actually on-grid). ----
	{
		const FBox Box(FVector(-8050.0f, -8050.0f, -10.0f), FVector(-7950.0f, -7950.0f, 10.0f));
		TArray<int32> Cells;
		FFPSRArenaCells::ComputeCellsFromBoundsXY(Box, ArenaOrigin, CellSize, GridDims, Cells);

		TestEqual(TEXT("off-grid corner box clips down to the single on-grid cell"), Cells.Num(), 1);
		if (Cells.Num() == 1) // same regression-safety guard as test (1) above — never index on a failed Num()
		{
			TestEqual(TEXT("the surviving cell is (0,0)"), Cells[0], 0);
		}
	}

	// ---- (3) An invalid box (FBox(ForceInit), the same "no mesh authored" sentinel GetVoxelBounds() returns)
	//      yields an empty array, not a crash or a spurious full-grid stamp. ----
	{
		TArray<int32> Cells;
		FFPSRArenaCells::ComputeCellsFromBoundsXY(FBox(ForceInit), ArenaOrigin, CellSize, GridDims, Cells);
		TestEqual(TEXT("invalid box yields no cells"), Cells.Num(), 0);
	}

	// ---- (4) ComputeDestructibleCells (authored override: anchor + grow +X/+Y only) and ComputeCellsFromBoundsXY
	//      (auto: bounds centered on that same point) MUST disagree for the same nominal location. Pinning this
	//      down keeps a future "just call the other one, they're basically the same" refactor from silently
	//      breaking AFPSRArenaDestructible::ComputeGridCells's branch — the two are deliberately NOT
	//      interchangeable (see the FootprintCells UPROPERTY comment: an authored footprint anchored at the actor
	//      only grows toward +X/+Y, while a bounds box grows symmetrically around its center). ----
	{
		const FVector Location(-2450.0f, -2450.0f, 0.0f); // local (5550,5550) -> anchor cell (55,55)

		FFPSRArenaAuthoredDestructible Authored;
		Authored.Location = Location;
		Authored.FootprintCells = FIntPoint(2, 2);
		TArray<int32> AnchoredCells;
		FFPSRArenaCells::ComputeDestructibleCells(Authored, ArenaOrigin, CellSize, GridDims, AnchoredCells);

		const FBox CenteredBox(Location - FVector(100.0f, 100.0f, 10.0f), Location + FVector(100.0f, 100.0f, 10.0f));
		TArray<int32> BoundsCells;
		FFPSRArenaCells::ComputeCellsFromBoundsXY(CenteredBox, ArenaOrigin, CellSize, GridDims, BoundsCells);

		TestEqual(TEXT("anchored footprint covers 4 cells (2x2 growing from the anchor)"), AnchoredCells.Num(), 4);
		TestEqual(TEXT("centered bounds covers 9 cells (3x3 around the center)"), BoundsCells.Num(), 9);

		const int32 CellBelowAnchor = 54 * GridDims.X + 54; // (54,54): -X/-Y of the anchor cell (55,55)
		TestTrue(TEXT("centered bounds reaches -X/-Y of the anchor"), BoundsCells.Contains(CellBelowAnchor));
		TestFalse(TEXT("anchored footprint never reaches -X/-Y of its anchor (+X/+Y growth only)"),
			AnchoredCells.Contains(CellBelowAnchor));
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
