// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Enemy/FPSRFlowFieldComputer.h"
#include "Enemy/FPSRFlowFieldSubsystem.h" // ShouldRecomputeOnUnfreeze (pure freeze-edge predicate)
#include "UObject/StrongObjectPtr.h"

#if WITH_AUTOMATION_TESTS

// U P-F (2026-07-09, continuous-field): worldless proofs for the two load-bearing kernels of the new-run topology reset
// + freeze pre-unfreeze recompute.
//   (1) BaselineRestore — ExtractSurfaceData -> mutate (open a seam door) -> BuildFromSurfaceData(baseline) restores the
//       EXACT closed-seam surface graph (EdgeMask/CellFloorZ/BlockedField), so ResetDoorTopologyToBaseline atomically
//       closes every opened door. (The subsystem wiring — snapshot at world begin, restore at StartRun — is server-only
//       and PIE-proven; this proves the primitive it stands on.)
//   (2) FreezeGate — the ShouldRecomputeOnUnfreeze predicate fires ONLY on the unpause edge with a stale generation.
//
// NOTE: helper names carry a unique 'Bl' (baseline) suffix — the module unity-builds these test .cpp files together, which
// MERGES their anonymous namespaces, so a name shared with another test file (e.g. SurfB in the budget test) collides.

namespace
{
	constexpr int32 NLBl = UFPSRFlowFieldComputer::NumLayers; // 2
	int32 SurfBl(int32 Cell, int32 Rank) { return Cell * NLBl + Rank; }

	FFPSRFlowFieldSurfaceData MakeFlatSlotBl(int32 W, int32 H, float CellSize, const FVector& Origin)
	{
		FFPSRFlowFieldSurfaceData D;
		D.GridDimX = W; D.GridDimY = H; D.GridOrigin = Origin; D.CellSize = CellSize;
		const int32 NumCells = W * H;
		D.CellFloorZ.Init(MAX_flt, NumCells * NLBl);
		D.BlockedField.Init(false, NumCells * NLBl);
		D.EdgeMask.Init(0, NumCells * 2);
		for (int32 CY = 0; CY < H; ++CY)
		{
			for (int32 CX = 0; CX < W; ++CX)
			{
				const int32 Cell = CY * W + CX;
				D.CellFloorZ[SurfBl(Cell, 0)] = Origin.Z;
				if (CX + 1 < W) { D.EdgeMask[Cell * 2 + 0] |= (1u << 0); }
				if (CY + 1 < H) { D.EdgeMask[Cell * 2 + 1] |= (1u << 0); }
			}
		}
		return D;
	}

	// Fresh 6x3 unified grid, two committed flat 3x3 slots (A cols 0-2, B cols 3-5) @Z=0; seam CLOSED by default.
	UFPSRFlowFieldComputer* MakeTwoSlotGridBl()
	{
		UFPSRFlowFieldComputer* C = NewObject<UFPSRFlowFieldComputer>();
		C->BuildEmptyGrid(6, 3, FVector(0, 0, 0), 100.0f);
		C->CommitSubregion(FIntPoint(0, 0), MakeFlatSlotBl(3, 3, 100.0f, FVector(0, 0, 0)));
		C->CommitSubregion(FIntPoint(3, 0), MakeFlatSlotBl(3, 3, 100.0f, FVector(300, 0, 0)));
		return C;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSRFlowFieldBaselineRestoreTest, "FPSRoguelite.FlowField.BaselineRestore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFPSRFlowFieldBaselineRestoreTest::RunTest(const FString& Parameters)
{
	const int32 UA0 = 0;          // (0,0) slot A origin — the BFS source
	const int32 A21 = 1 * 6 + 2;  // (2,1) slot A right-boundary, middle row
	const int32 B31 = 1 * 6 + 3;  // (3,1) slot B left-boundary, middle row
	const int32 B51 = 1 * 6 + 5;  // (5,1) slot B far, middle row

	TStrongObjectPtr<UFPSRFlowFieldComputer> C(MakeTwoSlotGridBl());

	// (1) Snapshot the closed-seam baseline (mirrors BuildUnifiedField's world-begin ExtractSurfaceData).
	FFPSRFlowFieldSurfaceData Baseline;
	C->ExtractSurfaceData(Baseline);

	// (2) Baseline is genuinely closed: slot B is unreachable from a source in A.
	C->RunBFS({ SurfBl(UA0, 0) });
	TestTrue(TEXT("baseline seam closed — slot B unreachable"), C->GetDist(SurfBl(B31, 0)) == MAX_int32);

	// (3) Mutate: open the seam door (2,1)<->(3,1). Slot B now crosses, and the EdgeMask differs from the baseline.
	const int32 Opened = C->StampDoorEdgesOpen(A21, B31);
	TestEqual(TEXT("seam door opens 1 rank-pair"), Opened, 1);
	C->RunBFS({ SurfBl(UA0, 0) });
	TestTrue(TEXT("slot B reachable through the opened door"), C->GetDist(SurfBl(B31, 0)) != MAX_int32);
	TestTrue(TEXT("slot B far cell reachable through the opened door"), C->GetDist(SurfBl(B51, 0)) != MAX_int32);
	{
		FFPSRFlowFieldSurfaceData Mutated;
		C->ExtractSurfaceData(Mutated);
		TestTrue(TEXT("opening the door changed the EdgeMask vs baseline"), Mutated.EdgeMask != Baseline.EdgeMask);
	}

	// (4) Restore the baseline atomically (mirrors ResetDoorTopologyToBaseline's BuildFromSurfaceData). The surface graph
	//     must match the baseline byte-for-byte, and the field must be back to closed (B unreachable again).
	C->BuildFromSurfaceData(Baseline);
	{
		FFPSRFlowFieldSurfaceData Restored;
		C->ExtractSurfaceData(Restored);
		TestTrue(TEXT("restored EdgeMask == baseline (all opened doors closed)"), Restored.EdgeMask == Baseline.EdgeMask);
		TestTrue(TEXT("restored CellFloorZ == baseline"), Restored.CellFloorZ == Baseline.CellFloorZ);
		TestTrue(TEXT("restored BlockedField == baseline"), Restored.BlockedField == Baseline.BlockedField);
	}
	C->RunBFS({ SurfBl(UA0, 0) });
	TestTrue(TEXT("after restore — slot B unreachable again (seam re-closed)"), C->GetDist(SurfBl(B31, 0)) == MAX_int32);

	// (5) Idempotent: restoring the already-restored baseline changes nothing.
	C->BuildFromSurfaceData(Baseline);
	{
		FFPSRFlowFieldSurfaceData Again;
		C->ExtractSurfaceData(Again);
		TestTrue(TEXT("double-restore is idempotent (EdgeMask unchanged)"), Again.EdgeMask == Baseline.EdgeMask);
	}

	return true;
}

// U P-F freeze pre-unfreeze predicate: ShouldRecomputeOnUnfreeze fires ONLY on the unpause edge with a stale generation.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSRFlowFieldFreezeGateTest, "FPSRoguelite.FlowField.FreezeGate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFPSRFlowFieldFreezeGateTest::RunTest(const FString& Parameters)
{
	using S = UFPSRFlowFieldSubsystem;

	// The single case that recomputes: was paused, now unpaused, and the field lags the topology (a door broke mid-freeze).
	TestTrue(TEXT("unpause edge + stale generation -> recompute"), S::ShouldRecomputeOnUnfreeze(true, false, 5, 4));

	// Unpause edge but the field is already current (no door broke while frozen) -> no redundant recompute.
	TestFalse(TEXT("unpause edge + generation current -> no recompute"), S::ShouldRecomputeOnUnfreeze(true, false, 5, 5));

	// Not an unpause edge:
	TestFalse(TEXT("still paused (was paused, still paused) -> no recompute"), S::ShouldRecomputeOnUnfreeze(true, true, 5, 4));
	TestFalse(TEXT("just paused (was not, now paused) -> no recompute"), S::ShouldRecomputeOnUnfreeze(false, true, 5, 4));
	TestFalse(TEXT("never paused (was not, now not) -> no recompute even if stale"), S::ShouldRecomputeOnUnfreeze(false, false, 5, 4));

	// The "never recomputed yet" sentinel (LastRecomputedGen = -1) counts as stale on an unpause edge.
	TestTrue(TEXT("unpause edge + never-computed sentinel -> recompute"), S::ShouldRecomputeOnUnfreeze(true, false, 0, -1));
	// ... but if generation 0 was already recomputed, the same unpause edge is a no-op (the common first-run freeze case).
	TestFalse(TEXT("unpause edge + gen 0 already computed -> no recompute"), S::ShouldRecomputeOnUnfreeze(true, false, 0, 0));

	return true;
}

#endif // WITH_AUTOMATION_TESTS
