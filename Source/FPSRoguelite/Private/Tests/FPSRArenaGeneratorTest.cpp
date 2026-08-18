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

	/** A stand-in for an authored prop group: one blocking part with passable ones around it, 3x2 cells. */
	FFPSRArenaPropSet MakeTestSet()
	{
		FFPSRArenaPropSet Set;
		auto Add = [&Set](int32 X, int32 Y, EFPSRArenaPropTier Tier, uint8 Variant)
		{
			FFPSRArenaPropSetEntry E;
			E.CellOffset = FIntPoint(X, Y);
			E.Tier = Tier;
			E.Variant = Variant;
			Set.Entries.Add(E);
		};
		Add(0, 0, EFPSRArenaPropTier::Blocking, 0);
		Add(-1, 0, EFPSRArenaPropTier::Passable, 1);
		Add(1, 0, EFPSRArenaPropTier::Passable, 1);
		Add(0, 1, EFPSRArenaPropTier::Passable, 2);
		Set.FootprintCells = FIntPoint(3, 2);
		Set.Weight = 1;
		Set.bAllowRotation = true;
		return Set;
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
	// Shipped defaults (160x160 @ 100cm, min corridor 3) plus one authored prop set, so every section below
	// exercises L1 rather than only the skeleton. The shipped default PropSets is EMPTY on purpose — that is the
	// baseline state for judging the authored skeleton alone — so a test using the raw defaults would silently
	// stop covering L1 entirely.
	FFPSRArenaGenParams Params;
	Params.PropSets.Add(MakeTestSet());
	const int32 SetSize = Params.PropSets[0].Entries.Num();
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

	// --- 4b. L1 micro props (ADR 0010 D5 / 0011 E6) -----------------------------------------------------
	// The 100-seed loop above already proves L1 cannot break the invariants — Generate runs it, so a prop that
	// pinched a corridor would surface as a validation failure there. What is left is the properties only L1 has.
	{
		FFPSRArenaLayout L;
		TestTrue(TEXT("build with props"), BuildFromProposal(31, Params, Origin, L));
		TestTrue(TEXT("L1 placed something"), L.Props.Num() > 0);

		int32 Blocking = 0, Passable = 0;
		TSet<FIntPoint> Occupied;
		for (const FFPSRArenaProp& P : L.Props)
		{
			if (P.Tier == EFPSRArenaPropTier::Blocking) { ++Blocking; } else { ++Passable; }

			TestTrue(TEXT("prop yaw is axis-aligned"),
				FMath::IsNearlyZero(FMath::Fmod(P.YawDegrees, 90.0f)));

			// A blocking prop must have taken its cell; a passable one must NOT have.
			const bool bCellOpen = FFPSRArenaGenerator::IsCellOpen(L, P.Cell.X, P.Cell.Y);
			if (P.Tier == EFPSRArenaPropTier::Blocking) { TestFalse(TEXT("blocking prop occupies its cell"), bCellOpen); }
			else { TestTrue(TEXT("passable prop leaves its cell open"), bCellOpen); }

			bool bAlready = false;
			Occupied.Add(P.Cell, &bAlready);
			TestFalse(TEXT("two props never share a cell"), bAlready);
		}
		TestTrue(TEXT("both tiers present"), Blocking > 0 && Passable > 0);

		// Sets land whole or not at all. A partially-applied group would mean the rollback path is broken, and a
		// half-placed group is exactly what a failed safety check must never leave behind.
		TestEqual(TEXT("prop count is a whole number of sets"), L.Props.Num() % SetSize, 0);

		// Determinism has to cover the props too — they are generated from the same seed and never replicated.
		FFPSRArenaLayout L2;
		TestTrue(TEXT("rebuild same seed"), BuildFromProposal(31, Params, Origin, L2));
		if (TestEqual(TEXT("same seed -> same prop count"), L2.Props.Num(), L.Props.Num()))
		{
			bool bAllSame = true;
			for (int32 i = 0; i < L.Props.Num(); ++i)
			{
				bAllSame &= (L.Props[i].Cell == L2.Props[i].Cell)
					&& (L.Props[i].Tier == L2.Props[i].Tier)
					&& (L.Props[i].Variant == L2.Props[i].Variant);
			}
			TestTrue(TEXT("same seed -> identical props"), bAllSame);
		}
	}

	// --- 4c. landmarks are never buried (ADR 0010 D6) ---------------------------------------------------
	{
		FFPSRArenaAuthoredInput Authored;
		FFPSRArenaAuthoredLandmark LM;
		LM.Location = FVector(Origin.X + 40.0 * Params.CellSize, Origin.Y + 40.0 * Params.CellSize, Origin.Z);
		LM.ReserveRadiusCells = 3;   // a landmark is a beacon, never a blocker (F6) — nothing to set for that here
		Authored.Landmarks.Add(LM);

		FFPSRArenaLayout L;
		TestTrue(TEXT("build with a landmark"), FFPSRArenaGenerator::Generate(9, Params, Origin, Authored, L));

		for (const FFPSRArenaProp& P : L.Props)
		{
			const int32 Cheb = FMath::Max(FMath::Abs(P.Cell.X - 40), FMath::Abs(P.Cell.Y - 40));
			TestTrue(*FString::Printf(TEXT("no prop inside the landmark reserve (cheb %d)"), Cheb),
				Cheb > LM.ReserveRadiusCells);
		}
	}

	// --- 4d. an empty set list means NO props (the skeleton-only baseline) -------------------------------
	{
		FFPSRArenaGenParams NoSets = Params;
		NoSets.PropSets.Reset();
		FFPSRArenaLayout L;
		TestTrue(TEXT("build with no prop sets"), BuildFromProposal(31, NoSets, Origin, L));
		TestEqual(TEXT("no authored sets -> no props at all"), L.Props.Num(), 0);
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

	// --- 7. ComputeDestructibleCells: footprint shape, ascending order, grid-edge clipping (ADR 0010 D7) --------
	// Pure geometry — no Generate() call, so L1 cannot interfere with these assertions.
	{
		const FIntPoint GridDims = Params.ArenaSizeCells; // shipped default 160x160

		// 1x1 footprint -> exactly its own anchor cell.
		{
			FFPSRArenaAuthoredDestructible D;
			D.Location = Origin + FVector(10.5 * Params.CellSize, 20.5 * Params.CellSize, 0.0);
			D.FootprintCells = FIntPoint(1, 1);
			TArray<int32> Cells;
			FFPSRArenaGenerator::ComputeDestructibleCells(D, Origin, Params.CellSize, GridDims, Cells);
			if (TestEqual(TEXT("1x1 footprint -> 1 cell"), Cells.Num(), 1))
			{
				TestEqual(TEXT("1x1 footprint anchor cell index"), Cells[0], 20 * GridDims.X + 10);
			}
		}

		// 2x3 footprint (width 2, height 3) -> 6 cells, growing +X/+Y from the anchor, in ascending index order.
		{
			FFPSRArenaAuthoredDestructible D;
			D.Location = Origin + FVector(5.5 * Params.CellSize, 5.5 * Params.CellSize, 0.0);
			D.FootprintCells = FIntPoint(2, 3);
			TArray<int32> Cells;
			FFPSRArenaGenerator::ComputeDestructibleCells(D, Origin, Params.CellSize, GridDims, Cells);
			TestEqual(TEXT("2x3 footprint -> 6 cells"), Cells.Num(), 6);

			TArray<int32> Expected;
			for (int32 DY = 0; DY < 3; ++DY)
			{
				for (int32 DX = 0; DX < 2; ++DX)
				{
					Expected.Add((5 + DY) * GridDims.X + (5 + DX));
				}
			}
			TestTrue(TEXT("2x3 footprint matches the expected cell set"), Cells == Expected);

			bool bAscending = true;
			for (int32 i = 1; i < Cells.Num(); ++i)
			{
				bAscending &= (Cells[i] > Cells[i - 1]);
			}
			TestTrue(TEXT("cell order is strictly ascending"), bAscending);
		}

		// Anchor at the grid's last cell + a footprint that overshoots the far edge -> off-grid cells silently
		// dropped (not clamped/wrapped back onto the grid).
		{
			FFPSRArenaAuthoredDestructible D;
			D.Location = Origin + FVector((GridDims.X - 1 + 0.5) * Params.CellSize, (GridDims.Y - 1 + 0.5) * Params.CellSize, 0.0);
			D.FootprintCells = FIntPoint(3, 3);
			TArray<int32> Cells;
			FFPSRArenaGenerator::ComputeDestructibleCells(D, Origin, Params.CellSize, GridDims, Cells);
			if (TestEqual(TEXT("off-grid footprint clipped to the single in-grid cell"), Cells.Num(), 1))
			{
				TestEqual(TEXT("clipped cell is the grid's last index"), Cells[0], (GridDims.Y - 1) * GridDims.X + (GridDims.X - 1));
			}
		}

		// Anchor entirely off-grid (negative) -> zero cells, not a crash or a negative index.
		{
			FFPSRArenaAuthoredDestructible D;
			D.Location = Origin + FVector(-5.0 * Params.CellSize, -5.0 * Params.CellSize, 0.0);
			D.FootprintCells = FIntPoint(2, 2);
			TArray<int32> Cells;
			FFPSRArenaGenerator::ComputeDestructibleCells(D, Origin, Params.CellSize, GridDims, Cells);
			TestEqual(TEXT("fully off-grid anchor -> 0 cells"), Cells.Num(), 0);
		}
	}

	// --- 8. an authored destructible blocks its cells; without one they stay open (ADR 0010 D7) -----------------
	{
		// Isolated from L1 (same reasoning as test 4d): a stray micro-prop landing on the exact cells under test
		// would make this flaky for reasons that have nothing to do with destructibles.
		FFPSRArenaGenParams NoProps = Params;
		NoProps.PropSets.Reset();

		FFPSRArenaAuthoredInput Authored;
		FFPSRArenaAuthoredDestructible D;
		D.Location = Origin + FVector(60.5 * NoProps.CellSize, 60.5 * NoProps.CellSize, 0.0);
		D.FootprintCells = FIntPoint(2, 2);
		Authored.Destructibles.Add(D);

		TArray<int32> ExpectedCells;
		FFPSRArenaGenerator::ComputeDestructibleCells(D, Origin, NoProps.CellSize, NoProps.ArenaSizeCells, ExpectedCells);
		TestTrue(TEXT("destructible authors a non-empty footprint for this test"), ExpectedCells.Num() > 0);

		FFPSRArenaLayout WithDestructible;
		TestTrue(TEXT("build with a destructible"), FFPSRArenaGenerator::Generate(5, NoProps, Origin, Authored, WithDestructible));
		if (TestEqual(TEXT("layout records one destructible's cells"), WithDestructible.DestructibleCells.Num(), 1))
		{
			TestTrue(TEXT("recorded cells match ComputeDestructibleCells"), WithDestructible.DestructibleCells[0] == ExpectedCells);
		}
		for (const int32 CellIdx : ExpectedCells)
		{
			const int32 CX = CellIdx % NoProps.ArenaSizeCells.X;
			const int32 CY = CellIdx / NoProps.ArenaSizeCells.X;
			TestFalse(*FString::Printf(TEXT("destructible cell (%d,%d) is blocked"), CX, CY),
				FFPSRArenaGenerator::IsCellOpen(WithDestructible, CX, CY));
		}

		FFPSRArenaLayout WithoutDestructible;
		TestTrue(TEXT("build without the destructible"),
			FFPSRArenaGenerator::Generate(5, NoProps, Origin, FFPSRArenaAuthoredInput(), WithoutDestructible));
		TestEqual(TEXT("no authored destructibles -> no recorded cell lists"), WithoutDestructible.DestructibleCells.Num(), 0);
		for (const int32 CellIdx : ExpectedCells)
		{
			const int32 CX = CellIdx % NoProps.ArenaSizeCells.X;
			const int32 CY = CellIdx / NoProps.ArenaSizeCells.X;
			TestTrue(*FString::Printf(TEXT("same cell (%d,%d) is open with no destructible authored"), CX, CY),
				FFPSRArenaGenerator::IsCellOpen(WithoutDestructible, CX, CY));
		}
	}

	// --- 9. determinism: same input -> identical destructible cell lists, twice (ADR 0010 invariant 10) ---------
	{
		FFPSRArenaGenParams NoProps = Params;
		NoProps.PropSets.Reset();

		FFPSRArenaAuthoredInput Authored;
		FFPSRArenaAuthoredDestructible D1, D2;
		D1.Location = Origin + FVector(30.5 * NoProps.CellSize, 30.5 * NoProps.CellSize, 0.0);
		D1.FootprintCells = FIntPoint(2, 1);
		D2.Location = Origin + FVector(90.5 * NoProps.CellSize, 12.5 * NoProps.CellSize, 0.0);
		D2.FootprintCells = FIntPoint(1, 4);
		Authored.Destructibles.Add(D1);
		Authored.Destructibles.Add(D2);

		FFPSRArenaLayout A, B;
		TestTrue(TEXT("build A with destructibles"), FFPSRArenaGenerator::Generate(11, NoProps, Origin, Authored, A));
		TestTrue(TEXT("build B with destructibles"), FFPSRArenaGenerator::Generate(11, NoProps, Origin, Authored, B));

		TestTrue(TEXT("same seed+authored input -> identical surface"), SurfaceEquals(A.Surface, B.Surface));
		if (TestEqual(TEXT("same number of destructible-cell entries"), A.DestructibleCells.Num(), B.DestructibleCells.Num()))
		{
			bool bAllSame = true;
			for (int32 i = 0; i < A.DestructibleCells.Num(); ++i)
			{
				bAllSame &= (A.DestructibleCells[i] == B.DestructibleCells[i]);
			}
			TestTrue(TEXT("destructible cell lists are identical across both builds"), bAllSame);
		}
	}

	// --- 10. L2 floor wiring traces (ADR 0010 D8) --------------------------------------------------------------
	// Derived from the L0 mask alone, BEFORE L1 runs — see FFPSRArenaGenerator::DeriveFloorTraces. Test 10b is the
	// load-bearing one for this slice: if it ever fails, L2 has started reading L1's micro-prop blocking, which
	// is exactly the "wiring gets laid wherever cells happen to be spare" self-contradiction ADR 0010 D8 called
	// out in the external proposal it rejected.
	{
		auto SegmentsEqual = [](const TArray<FFPSRArenaTraceSegment>& A, const TArray<FFPSRArenaTraceSegment>& B)
		{
			if (A.Num() != B.Num()) { return false; }
			for (int32 i = 0; i < A.Num(); ++i)
			{
				if (A[i].FromCell != B[i].FromCell || A[i].ToCell != B[i].ToCell
					|| A[i].WidthClass != B[i].WidthClass || A[i].bJunction != B[i].bJunction)
				{
					return false;
				}
			}
			return true;
		};

		// --- 10a. determinism: same (seed, params, authored) twice -> identical Traces + TraceJunctions ---------
		{
			FFPSRArenaLayout A, B;
			TestTrue(TEXT("trace determinism: build A"), BuildFromProposal(31, Params, Origin, A));
			TestTrue(TEXT("trace determinism: build B"), BuildFromProposal(31, Params, Origin, B));
			TestTrue(TEXT("trace determinism: same input -> identical Traces"), SegmentsEqual(A.Traces, B.Traces));
			TestTrue(TEXT("trace determinism: same input -> identical TraceJunctions"), A.TraceJunctions == B.TraceJunctions);
		}

		// --- 10b. L1 independence: props on vs off must not change Traces/TraceJunctions at all -----------------
		{
			FFPSRArenaGenParams NoProps = Params;
			NoProps.PropSets.Reset();

			FFPSRArenaLayout WithProps, WithoutProps;
			TestTrue(TEXT("trace L1-independence: build with props"), BuildFromProposal(31, Params, Origin, WithProps));
			TestTrue(TEXT("trace L1-independence: build without props"), BuildFromProposal(31, NoProps, Origin, WithoutProps));

			// If this is ever false the comparison below proves nothing (both sides would trivially agree on "no
			// props changed anything" because no props were placed on either side to begin with).
			TestTrue(TEXT("trace L1-independence: L1 actually placed props on the 'with' side"), WithProps.Props.Num() > 0);

			TestTrue(TEXT("trace L1-independence: Traces identical with/without L1 props"),
				SegmentsEqual(WithProps.Traces, WithoutProps.Traces));
			TestTrue(TEXT("trace L1-independence: TraceJunctions identical with/without L1 props"),
				WithProps.TraceJunctions == WithoutProps.TraceJunctions);
		}

		// --- 10c. wiring tracks CORRIDOR STRUCTURE, not floor area ---------------------------------------------
		// A completely empty arena is one solid rectangle of open cells, and the medial axis of a rectangle is a
		// POINT (a square) or a short line — there are no corridors, so there is nothing to wire, and the skeleton
		// collapses to too few cells to form a single adjacent pair. That is the correct answer, not a failure:
		// traces exist to say "this is where you can walk THROUGH", and an empty box has no through-route to mark.
		// Drop in a single blocker and the open space becomes a loop around it, which does wire up.
		{
			FFPSRArenaLayout Empty;
			TestTrue(TEXT("trace empty-arena: generates"),
				FFPSRArenaGenerator::Generate(1, Params, Origin, FFPSRArenaAuthoredInput(), Empty));
			TestEqual(TEXT("trace empty-arena: no corridor structure -> no wiring"), Empty.Traces.Num(), 0);

			// One blocker in the middle -> a ring of open space around it -> a wired loop.
			//
			// Deliberately NOT asserting a junction count here. The medial axis of the region between two nested
			// rectangles is a loop PLUS spurs running into the corners (where the inner and outer corners each
			// generate their own ridge), and those spurs are legitimate degree-3 cells. How many appear is emergent
			// corner geometry, not a designed contract — pinning it would be testing the shape of the arena rather
			// than the behaviour of the derivation. The junction contract that IS meaningful lives in 10d: a
			// multi-cluster lattice must produce branching.
			FFPSRArenaAuthoredInput OneBlocker;
			FFPSRArenaAuthoredBox Box;
			Box.Center = Origin + FVector(0.5 * Params.ArenaSizeCells.X * Params.CellSize,
				0.5 * Params.ArenaSizeCells.Y * Params.CellSize, 0.0);
			Box.HalfExtentXY = FVector2D(20.0 * Params.CellSize, 20.0 * Params.CellSize);
			OneBlocker.Blockers.Add(Box);

			FFPSRArenaLayout Ring;
			TestTrue(TEXT("trace one-blocker: generates"),
				FFPSRArenaGenerator::Generate(1, Params, Origin, OneBlocker, Ring));
			TestTrue(TEXT("trace one-blocker: a loop around it DOES wire up"), Ring.Traces.Num() > 0);

			// Internal consistency instead: every junction must actually sit on the wiring. A junction cell that no
			// segment touches would mean the degree pass and the segment pass disagreed about what the skeleton is,
			// and the via markers would render in mid-air away from any trace.
			TSet<FIntPoint> WiredCells;
			for (const FFPSRArenaTraceSegment& Seg : Ring.Traces)
			{
				WiredCells.Add(Seg.FromCell);
				WiredCells.Add(Seg.ToCell);
			}
			bool bEveryJunctionIsWired = true;
			for (const FIntPoint& Junction : Ring.TraceJunctions)
			{
				bEveryJunctionIsWired &= WiredCells.Contains(Junction);
			}
			TestTrue(TEXT("trace one-blocker: every junction cell lies on a segment"), bEveryJunctionIsWired);
		}

		// --- 10d. a multi-cluster (2x2) lattice produces branching corridors -> at least one junction -----------
		{
			FFPSRArenaGenParams Lattice2x2 = Params;
			Lattice2x2.SlotGridOptions = { FIntPoint(2, 2) };

			FFPSRArenaLayout L;
			TestTrue(TEXT("trace lattice 2x2: builds"), BuildFromProposal(2, Lattice2x2, Origin, L));
			TestTrue(TEXT("trace lattice 2x2: at least one junction"), L.TraceJunctions.Num() > 0);
		}

		// --- 10e. every segment connects two genuine 8-neighbours, and no pair appears twice --------------------
		{
			FFPSRArenaLayout L;
			TestTrue(TEXT("trace adjacency: build"), BuildFromProposal(31, Params, Origin, L));
			TestTrue(TEXT("trace adjacency: has traces to check"), L.Traces.Num() > 0);

			TSet<uint64> SeenPairs;
			bool bAllAdjacent = true;
			bool bNoDuplicates = true;
			for (const FFPSRArenaTraceSegment& Seg : L.Traces)
			{
				const int32 DX = FMath::Abs(Seg.ToCell.X - Seg.FromCell.X);
				const int32 DY = FMath::Abs(Seg.ToCell.Y - Seg.FromCell.Y);
				if (DX > 1 || DY > 1 || (DX == 0 && DY == 0)) { bAllAdjacent = false; }

				const int32 FromIdx = L.CellIndex(Seg.FromCell.X, Seg.FromCell.Y);
				const int32 ToIdx = L.CellIndex(Seg.ToCell.X, Seg.ToCell.Y);
				const uint64 PairKey = (static_cast<uint64>(static_cast<uint32>(FromIdx)) << 32) | static_cast<uint32>(ToIdx);
				bool bAlready = false;
				SeenPairs.Add(PairKey, &bAlready);
				if (bAlready) { bNoDuplicates = false; }
			}
			TestTrue(TEXT("trace adjacency: every segment's two cells are 8-neighbours"), bAllAdjacent);
			TestTrue(TEXT("trace adjacency: no duplicate segment pairs"), bNoDuplicates);
		}
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
