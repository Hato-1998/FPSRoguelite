// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
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

#endif // WITH_AUTOMATION_TESTS
