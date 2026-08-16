// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Arena/FPSRArenaGenerator.h"
#include "Arena/FPSRArenaValidator.h"
#include "Enemy/FPSRFlowFieldComputer.h"

#if WITH_AUTOMATION_TESTS

// ADR 0010 + 0011 — worldless proof for the arena pipeline.
//
// The route under test is the real one: propose a lattice (0011 E5, the editor button) -> hand it to the
// rasteriser as authored boxes (0011 E3) -> check with FFPSRArenaValidator (0011 E4). Feeding the proposal
// through the SAME rasteriser the runtime uses is the point — a test that skipped it would agree with the
// runtime only until one of the two paths was edited.
//
// Note what 0011 changed about these assertions. While L0 was generated, the lattice construction made pockets
// and narrow corridors impossible and this file merely confirmed it. Now a designer places the blockers, so the
// validator is a pipeline stage and this file is its regression net — if a check here is wrong, a broken arena
// ships.

namespace
{
	constexpr int32 NLA = UFPSRFlowFieldComputer::NumLayers;

	bool SurfaceEquals(const FFPSRFlowFieldSurfaceData& A, const FFPSRFlowFieldSurfaceData& B)
	{
		return A.GridDimX == B.GridDimX && A.GridDimY == B.GridDimY && A.GridOrigin.Equals(B.GridOrigin, 0.0)
			&& A.CellSize == B.CellSize && A.ClimbableStepHeight == B.ClimbableStepHeight
			&& A.CellFloorZ == B.CellFloorZ && A.BlockedField == B.BlockedField && A.EdgeMask == B.EdgeMask;
	}

	/** The editor tool's output, expressed the way the level would hand it to the runtime. */
	bool BuildFromProposal(int32 Seed, const FFPSRArenaGenParams& Params, const FVector& Origin, FFPSRArenaLayout& Out)
	{
		TArray<FFPSRArenaCluster> Clusters;
		FIntPoint Lattice;
		if (!FFPSRArenaGenerator::ProposeLatticeClusters(Seed, Params, Clusters, Lattice))
		{
			return false;
		}
		FFPSRArenaAuthoredInput Authored;
		Authored.Blockers.Reserve(Clusters.Num());
		for (const FFPSRArenaCluster& C : Clusters)
		{
			Authored.Blockers.Add(FFPSRArenaGenerator::ClusterToAuthoredBox(C, Params, Origin));
		}
		return FFPSRArenaGenerator::Generate(Seed, Params, Origin, Authored, Out);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSRArenaGeneratorTest, "FPSRoguelite.Arena.Generator",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFPSRArenaGeneratorTest::RunTest(const FString& Parameters)
{
	const FFPSRArenaGenParams Params;               // shipped defaults: 160x160 @ 100cm, min corridor 3
	const FVector Origin(0.0, 0.0, 200.0);

	// --- 1. determinism (ADR 0010 invariant 10) ---------------------------------------------------------
	{
		FFPSRArenaLayout A, B, C;
		TestTrue(TEXT("build A"), BuildFromProposal(4242, Params, Origin, A));
		TestTrue(TEXT("build B"), BuildFromProposal(4242, Params, Origin, B));
		TestTrue(TEXT("same seed -> identical surface data"), SurfaceEquals(A.Surface, B.Surface));

		// A different seed must actually produce a different arena, or the seed plumbing is dead and every
		// "100 seeds" assertion below is really one seed run 100 times.
		TestTrue(TEXT("build C"), BuildFromProposal(4243, Params, Origin, C));
		TestFalse(TEXT("different seed -> different layout"), SurfaceEquals(A.Surface, C.Surface));
	}

	// --- 2. surface shape is what the flow field will adopt ---------------------------------------------
	{
		FFPSRArenaLayout L;
		TestTrue(TEXT("build"), BuildFromProposal(7, Params, Origin, L));

		const int32 NumCells = Params.ArenaSizeCells.X * Params.ArenaSizeCells.Y;
		TestEqual(TEXT("CellFloorZ sized NumCells*NumLayers"), L.Surface.CellFloorZ.Num(), NumCells * NLA);
		TestEqual(TEXT("BlockedField sized NumCells*NumLayers"), L.Surface.BlockedField.Num(), NumCells * NLA);
		TestEqual(TEXT("EdgeMask sized NumCells*2"), L.Surface.EdgeMask.Num(), NumCells * 2);

		// Single plane: rank0 present everywhere, rank1 absent everywhere. If rank1 ever gains a surface the arena
		// has silently become multi-layer and "NumLayers costs nothing here" stops being true.
		int32 Rank1Present = 0, Rank0Absent = 0, Blocked = 0;
		for (int32 Cell = 0; Cell < NumCells; ++Cell)
		{
			if (L.Surface.CellFloorZ[Cell * NLA + 0] == MAX_flt) { ++Rank0Absent; }
			if (L.Surface.CellFloorZ[Cell * NLA + 1] != MAX_flt) { ++Rank1Present; }
			if (L.Surface.BlockedField[Cell * NLA + 0]) { ++Blocked; }
		}
		TestEqual(TEXT("rank0 floor present in every cell"), Rank0Absent, 0);
		TestEqual(TEXT("rank1 absent in every cell (single plane)"), Rank1Present, 0);
		TestTrue(TEXT("rasteriser actually blocked something"), Blocked > 0);
		TestTrue(TEXT("most of the arena is open"), Blocked < NumCells / 2);
	}

	// --- 3. an EMPTY authored input is legal and fully open ---------------------------------------------
	// The generator must not invent a skeleton nobody placed (0011 E5 moved that to an editor button). Whether an
	// empty arena is acceptable is the validator's call, not the generator's.
	{
		FFPSRArenaLayout L;
		TestTrue(TEXT("empty authored input still generates"),
			FFPSRArenaGenerator::Generate(1, Params, Origin, FFPSRArenaAuthoredInput(), L));
		TestEqual(TEXT("no clusters from empty input"), L.Clusters.Num(), 0);
		TestTrue(TEXT("empty arena is open at its centre"),
			FFPSRArenaGenerator::IsCellOpen(L, Params.ArenaSizeCells.X / 2, Params.ArenaSizeCells.Y / 2));
	}

	// --- 4. the validator agrees with the proposal, over 100 seeds --------------------------------------
	{
		for (int32 Seed = 1; Seed <= 100; ++Seed)
		{
			FFPSRArenaLayout L;
			if (!TestTrue(*FString::Printf(TEXT("seed %d builds"), Seed), BuildFromProposal(Seed, Params, Origin, L)))
			{
				continue;
			}

			const FFPSRArenaValidationResult V = FFPSRArenaValidator::Validate(L, Params);
			if (!V.Passed())
			{
				AddError(FString::Printf(TEXT("seed %d failed validation: %s | %s"),
					Seed, *FFPSRArenaValidator::Summarize(V), *FString::Join(V.Errors, TEXT(" / "))));
				continue;
			}

			// Spot-check the individual properties too, so a validator that silently stopped checking something
			// (an early return, an inverted comparison) cannot pass this test by reporting zero errors.
			TestEqual(*FString::Printf(TEXT("seed %d: one connected component"), Seed), V.LargestComponent, V.OpenCells);
			TestTrue(*FString::Printf(TEXT("seed %d: has a loop (E=%d >= V=%d)"), Seed, V.OpenEdges, V.OpenCells),
				V.OpenEdges >= V.OpenCells);
			TestTrue(*FString::Printf(TEXT("seed %d: corridor >= min"), Seed),
				V.NarrowestCorridor >= Params.MinCorridorWidthCells);
			TestEqual(*FString::Printf(TEXT("seed %d: no pockets"), Seed), V.PocketCells, 0);
		}
	}

	// --- 5. the validator actually FAILS a broken arena -------------------------------------------------
	// Without this the suite proves only that good input passes, which a validator that returns success
	// unconditionally would also satisfy.
	{
		FFPSRArenaGenParams Small = Params;
		Small.ArenaSizeCells = FIntPoint(40, 40);

		// One blocker straddling the full width: it severs the arena into two disconnected halves.
		FFPSRArenaAuthoredInput Wall;
		FFPSRArenaAuthoredBox Bar;
		Bar.Center = FVector(Origin.X + 20.0 * Small.CellSize, Origin.Y + 20.0 * Small.CellSize, Origin.Z);
		Bar.HalfExtentXY = FVector2D(40.0 * Small.CellSize, 1.0 * Small.CellSize);
		Wall.Blockers.Add(Bar);

		FFPSRArenaLayout L;
		TestTrue(TEXT("severed arena still generates"), FFPSRArenaGenerator::Generate(1, Small, Origin, Wall, L));

		const FFPSRArenaValidationResult V = FFPSRArenaValidator::Validate(L, Small);
		TestFalse(TEXT("validator rejects a severed arena"), V.Passed());
		TestTrue(TEXT("severed arena is reported as split"), V.LargestComponent < V.OpenCells);
	}

	// --- 6. bad params fail fast rather than degrading --------------------------------------------------
	{
		FFPSRArenaGenParams TooSmall;
		TooSmall.ArenaSizeCells = FIntPoint(8, 8); // interior 2x2 cannot hold a cluster inset by 3
		FFPSRArenaLayout L;
		AddExpectedError(TEXT("Generate rejected"), EAutomationExpectedErrorFlags::Contains, 1);
		TestFalse(TEXT("undersized arena is rejected"),
			FFPSRArenaGenerator::Generate(1, TooSmall, Origin, FFPSRArenaAuthoredInput(), L));
		TestFalse(TEXT("rejected layout is left invalid"), L.IsValid());
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
