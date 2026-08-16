// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Arena/FPSRArenaGenerator.h"
#include "Enemy/FPSRFlowFieldComputer.h"

#if WITH_AUTOMATION_TESTS

// ADR 0010 (Docs/Architecture/0010-arena-topology-and-stage-transition.md) — worldless proof for the L0 skeleton.
//
// These CONFIRM properties the lattice construction already guarantees; the generator does not retry until they
// pass. That distinction matters: a generator that leans on validation has to answer "what do we do when it
// fails 20 times", and ADR 0010 deliberately designed that question out of existence for L0.
//
// Asserted here:
//   - invariant 10  same seed -> byte-identical layout (clients regenerate locally from the replicated seed;
//                   if this ever breaks, enemies walk through walls on someone's screen)
//   - D1            open cells form ONE connected component, and it contains at least one cycle (a kiting loop)
//   - invariant 6   every open cell sits in a corridor at least MinCorridorWidthCells wide on BOTH axes
//   - invariant 7   no concave pocket (no open cell walled on 3+ sides), clusters convex + never touching
//   - and all of the above over 100 random seeds, not just the lucky one.

namespace
{
	constexpr int32 NLA = UFPSRFlowFieldComputer::NumLayers;

	struct FArenaProbe
	{
		const FFPSRArenaLayout& L;
		int32 W = 0;
		int32 H = 0;

		explicit FArenaProbe(const FFPSRArenaLayout& InLayout)
			: L(InLayout), W(InLayout.GridDims.X), H(InLayout.GridDims.Y) {}

		bool Open(int32 CX, int32 CY) const { return FFPSRArenaGenerator::IsCellOpen(L, CX, CY); }

		int32 CountOpen() const
		{
			int32 N = 0;
			for (int32 CY = 0; CY < H; ++CY) { for (int32 CX = 0; CX < W; ++CX) { N += Open(CX, CY) ? 1 : 0; } }
			return N;
		}

		/** Orthogonal open<->open adjacencies, counted once each. */
		int32 CountOpenEdges() const
		{
			int32 E = 0;
			for (int32 CY = 0; CY < H; ++CY)
			{
				for (int32 CX = 0; CX < W; ++CX)
				{
					if (!Open(CX, CY)) { continue; }
					if (Open(CX + 1, CY)) { ++E; }
					if (Open(CX, CY + 1)) { ++E; }
				}
			}
			return E;
		}

		/** Flood fill from the first open cell; returns how many open cells it reached. */
		int32 FloodFromFirstOpen() const
		{
			TArray<bool> Seen;
			Seen.Init(false, W * H);
			TArray<int32> Stack;

			for (int32 CY = 0; CY < H && Stack.Num() == 0; ++CY)
			{
				for (int32 CX = 0; CX < W && Stack.Num() == 0; ++CX)
				{
					if (Open(CX, CY)) { Stack.Add(CY * W + CX); Seen[CY * W + CX] = true; }
				}
			}

			int32 Reached = 0;
			while (Stack.Num() > 0)
			{
				const int32 Cell = Stack.Pop();
				++Reached;
				const int32 CX = Cell % W;
				const int32 CY = Cell / W;
				const int32 DX[4] = { 1, -1, 0, 0 };
				const int32 DY[4] = { 0, 0, 1, -1 };
				for (int32 D = 0; D < 4; ++D)
				{
					const int32 NX = CX + DX[D];
					const int32 NY = CY + DY[D];
					if (!Open(NX, NY)) { continue; }
					const int32 NCell = NY * W + NX;
					if (Seen[NCell]) { continue; }
					Seen[NCell] = true;
					Stack.Add(NCell);
				}
			}
			return Reached;
		}

		/** Length of the maximal run of open cells along X (resp. Y) that contains this cell. */
		int32 RunX(int32 CX, int32 CY) const
		{
			int32 N = 1;
			for (int32 X = CX - 1; Open(X, CY); --X) { ++N; }
			for (int32 X = CX + 1; Open(X, CY); ++X) { ++N; }
			return N;
		}
		int32 RunY(int32 CX, int32 CY) const
		{
			int32 N = 1;
			for (int32 Y = CY - 1; Open(CX, Y); --Y) { ++N; }
			for (int32 Y = CY + 1; Open(CX, Y); ++Y) { ++N; }
			return N;
		}

		/** Narrowest corridor any open cell sits in (min over cells of min(RunX, RunY)). */
		int32 NarrowestCorridor() const
		{
			int32 Narrowest = MAX_int32;
			for (int32 CY = 0; CY < H; ++CY)
			{
				for (int32 CX = 0; CX < W; ++CX)
				{
					if (!Open(CX, CY)) { continue; }
					Narrowest = FMath::Min(Narrowest, FMath::Min(RunX(CX, CY), RunY(CX, CY)));
				}
			}
			return Narrowest;
		}

		/** Open cells walled on 3+ orthogonal sides (outside the grid counts as wall) — a pocket an enemy can
		 *  be pushed into and not steer out of. Should always be 0. */
		int32 CountPockets() const
		{
			int32 Pockets = 0;
			for (int32 CY = 0; CY < H; ++CY)
			{
				for (int32 CX = 0; CX < W; ++CX)
				{
					if (!Open(CX, CY)) { continue; }
					int32 Walls = 0;
					Walls += Open(CX + 1, CY) ? 0 : 1;
					Walls += Open(CX - 1, CY) ? 0 : 1;
					Walls += Open(CX, CY + 1) ? 0 : 1;
					Walls += Open(CX, CY - 1) ? 0 : 1;
					if (Walls >= 3) { ++Pockets; }
				}
			}
			return Pockets;
		}
	};

	/** Smallest orthogonal gap between two axis-aligned clusters; MAX_int32 if they do not share a band. */
	int32 ClusterGap(const FFPSRArenaCluster& A, const FFPSRArenaCluster& B)
	{
		const int32 GapX = FMath::Max(A.MinCell.X - B.MaxCell.X - 1, B.MinCell.X - A.MaxCell.X - 1);
		const int32 GapY = FMath::Max(A.MinCell.Y - B.MaxCell.Y - 1, B.MinCell.Y - A.MaxCell.Y - 1);
		return FMath::Max(GapX, GapY); // separated on at least one axis by this much
	}

	bool SurfaceEquals(const FFPSRFlowFieldSurfaceData& A, const FFPSRFlowFieldSurfaceData& B)
	{
		return A.GridDimX == B.GridDimX && A.GridDimY == B.GridDimY && A.GridOrigin.Equals(B.GridOrigin, 0.0)
			&& A.CellSize == B.CellSize && A.ClimbableStepHeight == B.ClimbableStepHeight
			&& A.CellFloorZ == B.CellFloorZ && A.BlockedField == B.BlockedField && A.EdgeMask == B.EdgeMask;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSRArenaGeneratorTest, "FPSRoguelite.Arena.Generator",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFPSRArenaGeneratorTest::RunTest(const FString& Parameters)
{
	const FFPSRArenaGenParams Params;               // the shipped defaults: 80x80 @ 100cm, min corridor 3
	const FVector Origin(0.0, 0.0, 200.0);

	// --- 1. determinism (invariant 10) -----------------------------------------------------------------
	{
		FFPSRArenaLayout A, B;
		TestTrue(TEXT("generate A"), FFPSRArenaGenerator::Generate(4242, Params, Origin, A));
		TestTrue(TEXT("generate B"), FFPSRArenaGenerator::Generate(4242, Params, Origin, B));

		TestTrue(TEXT("same seed -> identical surface data"), SurfaceEquals(A.Surface, B.Surface));
		TestTrue(TEXT("same seed -> same lattice"), A.SlotGrid == B.SlotGrid);
		if (TestEqual(TEXT("same seed -> same cluster count"), A.Clusters.Num(), B.Clusters.Num()))
		{
			for (int32 i = 0; i < A.Clusters.Num(); ++i)
			{
				TestTrue(*FString::Printf(TEXT("cluster %d min"), i), A.Clusters[i].MinCell == B.Clusters[i].MinCell);
				TestTrue(*FString::Printf(TEXT("cluster %d max"), i), A.Clusters[i].MaxCell == B.Clusters[i].MaxCell);
			}
		}

		// A different seed must actually produce a different arena, or the seed plumbing is dead and every
		// "run 100 seeds" assertion below is really one seed run 100 times.
		FFPSRArenaLayout C;
		TestTrue(TEXT("generate C"), FFPSRArenaGenerator::Generate(4243, Params, Origin, C));
		TestFalse(TEXT("different seed -> different layout"), SurfaceEquals(A.Surface, C.Surface));
	}

	// --- 2. surface data shape matches what the flow field will adopt ----------------------------------
	{
		FFPSRArenaLayout L;
		TestTrue(TEXT("generate"), FFPSRArenaGenerator::Generate(7, Params, Origin, L));

		const int32 NumCells = Params.ArenaSizeCells.X * Params.ArenaSizeCells.Y;
		TestEqual(TEXT("CellFloorZ sized NumCells*NumLayers"), L.Surface.CellFloorZ.Num(), NumCells * NLA);
		TestEqual(TEXT("BlockedField sized NumCells*NumLayers"), L.Surface.BlockedField.Num(), NumCells * NLA);
		TestEqual(TEXT("EdgeMask sized NumCells*2"), L.Surface.EdgeMask.Num(), NumCells * 2);

		// Single plane: rank0 present everywhere, rank1 absent everywhere. If rank1 ever gains a surface the
		// arena has silently become multi-layer and the "NumLayers costs nothing here" claim is void.
		int32 Rank1Present = 0;
		int32 Rank0Absent = 0;
		for (int32 Cell = 0; Cell < NumCells; ++Cell)
		{
			if (L.Surface.CellFloorZ[Cell * NLA + 0] == MAX_flt) { ++Rank0Absent; }
			if (L.Surface.CellFloorZ[Cell * NLA + 1] != MAX_flt) { ++Rank1Present; }
		}
		TestEqual(TEXT("rank0 floor present in every cell"), Rank0Absent, 0);
		TestEqual(TEXT("rank1 absent in every cell (single plane)"), Rank1Present, 0);

		// Blocking must live in BlockedField, not in removed edges — the flow field reads the former.
		int32 Blocked = 0;
		for (int32 Cell = 0; Cell < NumCells; ++Cell) { Blocked += L.Surface.BlockedField[Cell * NLA + 0] ? 1 : 0; }
		TestTrue(TEXT("some cells are blocked (clusters exist)"), Blocked > 0);
		TestTrue(TEXT("most of the arena is open"), Blocked < NumCells / 2);
	}

	// --- 3. topology + geometry over 100 seeds (D1, invariants 6 & 7) ----------------------------------
	{
		int32 SeedsWithCycle = 0;
		for (int32 Seed = 1; Seed <= 100; ++Seed)
		{
			FFPSRArenaLayout L;
			if (!TestTrue(*FString::Printf(TEXT("seed %d generates"), Seed),
				FFPSRArenaGenerator::Generate(Seed, Params, Origin, L)))
			{
				continue;
			}

			const FArenaProbe P(L);
			const int32 OpenCells = P.CountOpen();
			const int32 Reached = P.FloodFromFirstOpen();
			const int32 OpenEdges = P.CountOpenEdges();

			TestEqual(*FString::Printf(TEXT("seed %d: open cells form ONE component"), Seed), Reached, OpenCells);

			// Connected + |E| >= |V| means at least one independent cycle (a tree would have exactly V-1).
			// This is the kiting loop: without it the arena is a dead end no matter how it looks.
			if (OpenEdges >= OpenCells) { ++SeedsWithCycle; }
			TestTrue(*FString::Printf(TEXT("seed %d: open graph contains a cycle (E=%d >= V=%d)"), Seed, OpenEdges, OpenCells),
				OpenEdges >= OpenCells);

			TestTrue(*FString::Printf(TEXT("seed %d: narrowest corridor >= MinCorridorWidthCells"), Seed),
				P.NarrowestCorridor() >= Params.MinCorridorWidthCells);

			TestEqual(*FString::Printf(TEXT("seed %d: no concave pocket"), Seed), P.CountPockets(), 0);

			TestTrue(*FString::Printf(TEXT("seed %d: cluster count within lattice"), Seed),
				L.Clusters.Num() >= 1 && L.Clusters.Num() <= L.SlotGrid.X * L.SlotGrid.Y);

			// Clusters must never touch: two adjacent rectangles could cup a pocket between them, which is
			// exactly the failure invariant 7 exists to make impossible.
			for (int32 i = 0; i < L.Clusters.Num(); ++i)
			{
				for (int32 j = i + 1; j < L.Clusters.Num(); ++j)
				{
					TestTrue(*FString::Printf(TEXT("seed %d: clusters %d,%d separated by >= MinCorridorWidthCells"), Seed, i, j),
						ClusterGap(L.Clusters[i], L.Clusters[j]) >= Params.MinCorridorWidthCells);
				}
			}
		}
		TestEqual(TEXT("every one of the 100 seeds has a kiting loop"), SeedsWithCycle, 100);
	}

	// --- 4. bad params fail fast rather than degrading -------------------------------------------------
	{
		FFPSRArenaGenParams TooSmall;
		TooSmall.ArenaSizeCells = FIntPoint(8, 8); // interior 2x2 cannot hold a cluster inset by 3
		FFPSRArenaLayout L;
		AddExpectedError(TEXT("Generate rejected"), EAutomationExpectedErrorFlags::Contains, 1);
		TestFalse(TEXT("undersized arena is rejected"), FFPSRArenaGenerator::Generate(1, TooSmall, Origin, L));
		TestFalse(TEXT("rejected layout is left invalid"), L.IsValid());
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
