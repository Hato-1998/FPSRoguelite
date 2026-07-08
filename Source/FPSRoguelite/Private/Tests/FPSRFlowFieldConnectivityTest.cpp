// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Enemy/FPSRFlowFieldComputer.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_AUTOMATION_TESTS

// P-C (2026-07-07, U continuous-field): worldless proof for open-grid connectivity — the combat-gate core. A closed
// door / wall puts two cells in DIFFERENT connected components so "can damage cross?" is an O(1) label compare (replaces
// the MapId combat gate). Occupancy-blocked cells are crowding, not walls, so they never split a component. The
// CanAffectTarget contract change (+ explosion Center origin, call sites) is the server wiring, proven in-world (PIE).

namespace
{
	constexpr int32 NLC = UFPSRFlowFieldComputer::NumLayers; // 2
	int32 SurfCn(int32 Cell, int32 Rank) { return Cell * NLC + Rank; }

	FFPSRFlowFieldSurfaceData MakeSlotCn(int32 W, int32 H, float CellSize, const FVector& Origin)
	{
		FFPSRFlowFieldSurfaceData D;
		D.GridDimX = W; D.GridDimY = H; D.GridOrigin = Origin; D.CellSize = CellSize;
		const int32 NumCells = W * H;
		D.CellFloorZ.Init(MAX_flt, NumCells * NLC);
		D.BlockedField.Init(false, NumCells * NLC);
		D.EdgeMask.Init(0, NumCells * 2);
		for (int32 CY = 0; CY < H; ++CY)
		{
			for (int32 CX = 0; CX < W; ++CX)
			{
				const int32 Cell = CY * W + CX;
				D.CellFloorZ[SurfCn(Cell, 0)] = Origin.Z;
				if (CX + 1 < W) { D.EdgeMask[Cell * 2 + 0] |= (1u << 0); }
				if (CY + 1 < H) { D.EdgeMask[Cell * 2 + 1] |= (1u << 0); }
			}
		}
		return D;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSRFlowFieldConnectivityTest, "FPSRoguelite.FlowField.Connectivity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFPSRFlowFieldConnectivityTest::RunTest(const FString& Parameters)
{
	const float Cell = 100.0f;
	const FVector Origin(0, 0, 0);
	const int32 A00 = 0;          // (0,0) slot A
	const int32 A21 = 1 * 6 + 2;  // (2,1) slot A right-boundary
	const int32 B31 = 1 * 6 + 3;  // (3,1) slot B left-boundary
	const int32 B51 = 1 * 6 + 5;  // (5,1) slot B far

	TStrongObjectPtr<UFPSRFlowFieldComputer> C(NewObject<UFPSRFlowFieldComputer>());
	C->BuildEmptyGrid(6, 3, Origin, Cell);
	C->CommitSubregion(FIntPoint(0, 0), MakeSlotCn(3, 3, Cell, Origin));
	C->CommitSubregion(FIntPoint(3, 0), MakeSlotCn(3, 3, Cell, FVector(300, 0, 0)));

	// Connectivity is (re)built by RunBFS; it is source-independent, so both slots get labelled from a single A source.
	C->RunBFS({ SurfCn(A00, 0) });
	TestTrue(TEXT("cells within slot A are connected"), C->AreSurfacesConnected(SurfCn(A00, 0), SurfCn(A21, 0)));
	TestFalse(TEXT("across the CLOSED seam NOT connected (wall)"), C->AreSurfacesConnected(SurfCn(A21, 0), SurfCn(B31, 0)));
	TestFalse(TEXT("far slot B cell NOT connected to slot A while closed"), C->AreSurfacesConnected(SurfCn(A00, 0), SurfCn(B51, 0)));

	// Open a door across the seam -> after the rebuild, the two slots are one component.
	C->StampDoorEdgesOpen(A21, B31);
	C->RunBFS({ SurfCn(A00, 0) });
	TestTrue(TEXT("across the OPEN door connected"), C->AreSurfacesConnected(SurfCn(A21, 0), SurfCn(B31, 0)));
	TestTrue(TEXT("far slot B cell connected to slot A through the door"), C->AreSurfacesConnected(SurfCn(A00, 0), SurfCn(B51, 0)));

	// World-location variant: instigator in slot A, target in slot B, door open -> connected (damage crosses).
	const FVector InA(150.0f, 150.0f, 95.0f); // cell (1,1)
	const FVector InB(350.0f, 150.0f, 95.0f); // cell (3,1)
	TestTrue(TEXT("world locations across the open door are connected"), C->AreWorldLocationsConnected(InA, InB));
	// Z-tolerant (Codex R12): an AIRBORNE pawn (rocket-jump / knockback), far above its floor (> MaxLayerPickDrop), is still
	// combat-reachable — connectivity uses the XY column, not the foot-Z rank pick that would find no surface and gate it.
	const FVector InA_Air(150.0f, 150.0f, 95.0f + 600.0f); // cell (1,1), 6m airborne
	TestTrue(TEXT("airborne pawn over a connected cell is still reachable"), C->AreWorldLocationsConnected(InA_Air, InB));
	// Fail-closed: a location outside the grid is never connected (no damage across).
	TestFalse(TEXT("outside-grid location not connected (fail-closed)"), C->AreWorldLocationsConnected(InA, FVector(99999.0f, 0.0f, 95.0f)));

	// Stamp the door-gap cell BLOCKED (a closed/intact door, P-B): connectivity must treat it as a WALL, so the ONLY
	// crossing cell drops out and the two slots re-separate -> combat is gated at the closed door (Codex R8).
	C->StampCellBlocked(B31, 0, true);
	C->RunBFS({ SurfCn(A00, 0) });
	TestFalse(TEXT("blocked door-gap cell re-separates the slots (combat gated at closed door)"),
		C->AreSurfacesConnected(SurfCn(A00, 0), SurfCn(B51, 0)));
	TestFalse(TEXT("a blocked surface has no connectivity label"), C->AreSurfacesConnected(SurfCn(B31, 0), SurfCn(B51, 0)));
	TestFalse(TEXT("world locations gated at the closed (blocked) door"), C->AreWorldLocationsConnected(InA, InB));

	// Unblock the gap (door breaks) -> reconnected.
	C->StampCellBlocked(B31, 0, false);
	C->RunBFS({ SurfCn(A00, 0) });
	TestTrue(TEXT("unblocking the door-gap cell reconnects the slots"), C->AreSurfacesConnected(SurfCn(A00, 0), SurfCn(B51, 0)));

	// Close the door EDGE again (SetSurfaceEdge false) -> back to separate components (damage no longer crosses).
	C->SetSurfaceEdge(A21, 0, B31, 0, false);
	C->RunBFS({ SurfCn(A00, 0) });
	TestFalse(TEXT("re-closed door edge splits the component again"), C->AreWorldLocationsConnected(InA, InB));

	// Stale-topology guard (Codex R9): a mutation invalidates the field; a query BEFORE the next RunBFS fails-closed even
	// though the door is now open (labels are stale). It only reports connected again once RunBFS rebuilds.
	C->StampDoorEdgesOpen(A21, B31);
	TestFalse(TEXT("query fails-closed while the field is stale (mutated, pre-RunBFS)"), C->AreWorldLocationsConnected(InA, InB));
	C->RunBFS({ SurfCn(A00, 0) });
	TestTrue(TEXT("connectivity valid again after the RunBFS rebuild"), C->AreWorldLocationsConnected(InA, InB));

	// R15 (Codex): connectivity is rebuilt every RunBFS INDEPENDENT of flow sources. A source-less RunBFS (all players
	// airborne/unsnapped) leaves the flow field not-ready but connectivity VALID, so the combat gate still reports the true
	// topology (open door -> connected; closed wall -> blocked) instead of falling back to a lenient allow that leaks.
	C->RunBFS({}); // no player sources: door is still open
	TestFalse(TEXT("source-less RunBFS leaves the flow field not ready"), C->IsFieldReady());
	TestTrue(TEXT("connectivity valid across the OPEN door without flow sources"), C->AreWorldLocationsConnected(InA, InB));
	C->SetSurfaceEdge(A21, 0, B31, 0, false); // close the door edge
	C->RunBFS({});                             // still no sources
	TestFalse(TEXT("closed wall still BLOCKS without flow sources (no leak, R15)"), C->AreWorldLocationsConnected(InA, InB));

	// R13 (Codex): multi-layer floor precision — a GROUND origin must NOT reach a target on a DISCONNECTED UPPER DECK at the
	// same XY (no through-floor/ceiling damage), while ground<->ground stays connected. The column resolves to its specific
	// floor (highest surface at/below foot Z), so an any-rank match can't leak damage between separate vertical components.
	{
		FFPSRFlowFieldSurfaceData D;
		D.GridDimX = 2; D.GridDimY = 1; D.GridOrigin = FVector::ZeroVector; D.CellSize = 100.0f;
		D.CellFloorZ.Init(MAX_flt, 2 * NLC);
		D.BlockedField.Init(false, 2 * NLC);
		D.EdgeMask.Init(0, 2 * 2);
		D.CellFloorZ[SurfCn(0, 0)] = 0.0f;   // cell 0 ground
		D.CellFloorZ[SurfCn(0, 1)] = 300.0f; // cell 0 upper deck (no edges -> its own disconnected component)
		D.CellFloorZ[SurfCn(1, 0)] = 0.0f;   // cell 1 ground
		D.EdgeMask[0 * 2 + 0] |= (1u << 0);  // (0,0)r0 <-> (1,0)r0 ground edge open
		TStrongObjectPtr<UFPSRFlowFieldComputer> C2(NewObject<UFPSRFlowFieldComputer>());
		C2->BuildFromSurfaceData(D);
		C2->RunBFS({ SurfCn(0, 0) });
		const FVector Ground1(150.0f, 50.0f, 95.0f);       // cell 1, ground foot
		const FVector Ground0(50.0f, 50.0f, 95.0f);        // cell 0, ground foot
		const FVector Deck0(50.0f, 50.0f, 300.0f + 95.0f); // cell 0, upper-deck foot
		TestTrue(TEXT("ground<->ground across the open edge connected"), C2->AreWorldLocationsConnected(Ground1, Ground0));
		TestFalse(TEXT("ground origin does NOT reach a disconnected upper-deck target (no through-floor damage)"),
			C2->AreWorldLocationsConnected(Ground1, Deck0));
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
