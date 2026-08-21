// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Enemy/FPSRHoverWindowCore.h"
#include "HAL/PlatformTime.h"
#include "Math/RandomStream.h"

#if WITH_AUTOMATION_TESTS

// P0 performance spike for ADR 0009 (Docs/Architecture/0009-hover-swarm-local-3d-flow-window.md — the player-centred
// 3D local uniform grid window). Two gates:
//   (A) FlowField.HoverWindowCore  — correctness of the worldless wavefront core (FPSRHoverWindowCore.h/.cpp): the
//       structural reason the window has to be 3D rather than a 2D field with a per-column altitude band (안 A,
//       기각 사유 "같은 XY에 통행 가능 고도가 둘 이상인 맵을 표현 못 한다") is exercised directly via a slab-with-hole
//       case where the two Z-independent tunnel levels get genuinely independent distances.
//   (B) FlowField.HoverWindowBench — worldless timing/memory across the still-"성능 미측정" matrix (decision
//       §"의도적 미해소"): window size x occupancy pattern x neighbour density, front-loading the numbers the P0
//       slice exists to produce before any content/placement work.

namespace
{
	FFPSRHoverWindowDims MakeDims(int32 DimX, int32 DimY, int32 DimZ)
	{
		FFPSRHoverWindowDims D;
		D.DimX = DimX; D.DimY = DimY; D.DimZ = DimZ;
		return D;
	}

	TArray<uint64> MakeOpenOccupancy(const FFPSRHoverWindowDims& Dims)
	{
		TArray<uint64> Occupancy;
		Occupancy.SetNumZeroed(FPSRHoverWindow::OccupancyWords(Dims.NumCells()));
		return Occupancy;
	}

	// Decodes a flat cell index back to (X,Y,Z) the same way Propagate does internally — used only to walk a
	// StepDir chain in the correctness test, never on the bench's hot path.
	FIntVector DecodeCoords(int32 Cell, const FFPSRHoverWindowDims& Dims)
	{
		const int32 Slice = Dims.DimX * Dims.DimY;
		const int32 Z = Cell / Slice;
		const int32 Rem = Cell - Z * Slice;
		const int32 Y = Rem / Dims.DimX;
		const int32 X = Rem - Y * Dims.DimX;
		return FIntVector(X, Y, Z);
	}
}

// (A) Correctness — worldless proof of the wavefront core.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSRHoverWindowCoreTest, "FPSRoguelite.FlowField.HoverWindowCore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFPSRHoverWindowCoreTest::RunTest(const FString& Parameters)
{
	const TConstArrayView<FIntVector> Offsets6 = FPSRHoverWindow::GetNeighborOffsets6();

	// ---- (1) Open 8x8x4 grid, single source at a corner: source integrates to 0, face neighbours to 1, the
	//          opposite corner to the exact Manhattan distance (6-neighbour BFS == Manhattan on an open grid). ----
	{
		const FFPSRHoverWindowDims Dims = MakeDims(8, 8, 4);
		const TArray<uint64> Occupancy = MakeOpenOccupancy(Dims);
		const int32 SourceCell = Dims.CellIndex(0, 0, 0);
		const TArray<int32> Sources = { SourceCell };

		FFPSRHoverWindowField Field;
		const int32 Seeded = FPSRHoverWindow::Propagate(Dims, Occupancy, Sources, Offsets6, Field);
		TestEqual(TEXT("open grid seeds exactly 1 source"), Seeded, 1);
		TestEqual(TEXT("source integrates to 0"), (int32)Field.Dist[SourceCell], 0);
		TestEqual(TEXT("+X face neighbour is 1"), (int32)Field.Dist[Dims.CellIndex(1, 0, 0)], 1);
		TestEqual(TEXT("+Y face neighbour is 1"), (int32)Field.Dist[Dims.CellIndex(0, 1, 0)], 1);
		TestEqual(TEXT("+Z face neighbour is 1"), (int32)Field.Dist[Dims.CellIndex(0, 0, 1)], 1);

		const int32 FarCorner = Dims.CellIndex(7, 7, 3);
		TestEqual(TEXT("opposite corner equals the Manhattan distance (7+7+3=17)"), (int32)Field.Dist[FarCorner], 17);

		// Gradient-follow: from an arbitrary interior cell, walking StepDir must strictly decrease Dist by 1 each
		// hop and terminate exactly at the source — this is the property agents actually consume at query time.
		int32 Current = Dims.CellIndex(5, 3, 2);
		int32 GuardSteps = 0;
		while (Field.Dist[Current] != 0)
		{
			const uint8 SD = Field.StepDir[Current];
			// A zero StepDir here would be an implementation bug (every non-source cell must carry one) — bail
			// out of the walk instead of indexing Offsets6[-1] on the failure path.
			if (!TestTrue(TEXT("gradient-follow cell has a non-zero StepDir before reaching the source"), SD != 0))
			{
				break;
			}
			const FIntVector Offset = Offsets6[SD - 1];
			const FIntVector C = DecodeCoords(Current, Dims);
			const int32 NextCell = Dims.CellIndex(C.X + Offset.X, C.Y + Offset.Y, C.Z + Offset.Z);
			TestEqual(TEXT("gradient-follow step decreases Dist by exactly 1"),
				(int32)Field.Dist[NextCell], (int32)Field.Dist[Current] - 1);
			Current = NextCell;
			// Guards against an accidental StepDir cycle; a real bug here would otherwise hang the test forever.
			if (!TestTrue(TEXT("gradient-follow terminates within NumCells hops"), ++GuardSteps <= Dims.NumCells()))
			{
				break;
			}
		}
		TestEqual(TEXT("gradient-follow arrives at the source cell"), Current, SourceCell);

		// Occupied cell: never visited, so it keeps the Unreached/0 defaults Propagate resets everything to.
		TArray<uint64> OccupancyWithBlock = MakeOpenOccupancy(Dims);
		const int32 BlockedCell = Dims.CellIndex(4, 4, 2);
		FPSRHoverWindow::SetOccupied(OccupancyWithBlock, BlockedCell);
		FFPSRHoverWindowField BlockedField;
		FPSRHoverWindow::Propagate(Dims, OccupancyWithBlock, Sources, Offsets6, BlockedField);
		TestEqual(TEXT("occupied cell stays Unreached"), BlockedField.Dist[BlockedCell], FPSRHoverWindow::UnreachedDist);
		TestEqual(TEXT("occupied cell StepDir stays 0"), (int32)BlockedField.StepDir[BlockedCell], 0);
	}

	// ---- (2) Overlapping layers — the structural reason the window is 3D and not a 2D field with a per-column
	//          altitude band (ADR 0009 안 A, 기각). A fully occupied Z=2 slab across an 8x8x5 grid partitions the
	//          space top from bottom; punching ONE hole reconnects it, and the same (X,Y) column then carries
	//          genuinely independent distances above vs. below the slab (a single column value could not). ----
	{
		const FFPSRHoverWindowDims Dims = MakeDims(8, 8, 5);
		const int32 SourceCell = Dims.CellIndex(0, 0, 0);
		const TArray<int32> Sources = { SourceCell };

		// (2a) No hole: the slab spans the full 8x8 extent, so every Z>=3 cell is unreachable — 6-neighbour BFS
		// can only cross a Z boundary one layer at a time, and every Z=2 cell is blocked.
		{
			TArray<uint64> Occupancy = MakeOpenOccupancy(Dims);
			for (int32 Y = 0; Y < Dims.DimY; ++Y)
			{
				for (int32 X = 0; X < Dims.DimX; ++X)
				{
					FPSRHoverWindow::SetOccupied(Occupancy, Dims.CellIndex(X, Y, 2));
				}
			}
			FFPSRHoverWindowField Field;
			FPSRHoverWindow::Propagate(Dims, Occupancy, Sources, Offsets6, Field);
			TestEqual(TEXT("unbroken slab: far corner above is unreached"),
				Field.Dist[Dims.CellIndex(7, 7, 4)], FPSRHoverWindow::UnreachedDist);
			TestEqual(TEXT("unbroken slab: near cell directly above is unreached"),
				Field.Dist[Dims.CellIndex(0, 0, 3)], FPSRHoverWindow::UnreachedDist);
		}

		// (2b) One hole at (4,4,2): Z>=3 becomes reachable ONLY by routing through that single open cell, so the
		// shortest path to any far-side cell is ShortestPath(source, hole) + ShortestPath(hole, target) — computed
		// by hand below via Manhattan distance (achievable exactly, since XY movement can stay clear of the slab
		// except at the hole itself).
		{
			TArray<uint64> Occupancy = MakeOpenOccupancy(Dims);
			for (int32 Y = 0; Y < Dims.DimY; ++Y)
			{
				for (int32 X = 0; X < Dims.DimX; ++X)
				{
					FPSRHoverWindow::SetOccupied(Occupancy, Dims.CellIndex(X, Y, 2));
				}
			}
			const int32 HoleCell = Dims.CellIndex(4, 4, 2);
			Occupancy[HoleCell >> 6] &= ~(1ull << (HoleCell & 63)); // punch the hole

			FFPSRHoverWindowField Field;
			FPSRHoverWindow::Propagate(Dims, Occupancy, Sources, Offsets6, Field);

			TestTrue(TEXT("hole reconnects the far corner above"),
				Field.Dist[Dims.CellIndex(7, 7, 4)] != FPSRHoverWindow::UnreachedDist);

			// Column (0,0): z=1 is reached directly (never touches the slab) while z=3 is forced through the hole
			// at (4,4,2) -> (4,4,3) -> back across to (0,0,3). Hand-computed: dist(0,0,1)=1;
			// dist(0,0,3) = Manhattan((0,0,0),(4,4,2)) + Manhattan((4,4,2),(0,0,3)) = 10 + 9 = 19.
			const int32 Z1 = (int32)Field.Dist[Dims.CellIndex(0, 0, 1)];
			const int32 Z3 = (int32)Field.Dist[Dims.CellIndex(0, 0, 3)];
			TestEqual(TEXT("below-slab column cell reaches its exact direct distance"), Z1, 1);
			TestEqual(TEXT("above-slab column cell reaches its exact detour-through-the-hole distance"), Z3, 19);
			TestTrue(TEXT("same (X,Y) column: below/above the slab are independent values, not one shared altitude"),
				Z1 != Z3);
		}
	}

	// ---- (3) Complete enclosure: a source with all 6 face neighbours occupied cannot expand at all — only the
	//          source cell itself integrates, everything else in the window stays unreached. ----
	{
		const FFPSRHoverWindowDims Dims = MakeDims(5, 5, 5);
		TArray<uint64> Occupancy = MakeOpenOccupancy(Dims);
		const int32 SourceCell = Dims.CellIndex(2, 2, 2);
		for (const FIntVector& Offset : Offsets6)
		{
			FPSRHoverWindow::SetOccupied(Occupancy, Dims.CellIndex(2 + Offset.X, 2 + Offset.Y, 2 + Offset.Z));
		}
		const TArray<int32> Sources = { SourceCell };

		FFPSRHoverWindowField Field;
		const int32 Seeded = FPSRHoverWindow::Propagate(Dims, Occupancy, Sources, Offsets6, Field);
		TestEqual(TEXT("enclosed source still seeds"), Seeded, 1);
		TestEqual(TEXT("enclosed source integrates to 0"), (int32)Field.Dist[SourceCell], 0);
		TestEqual(TEXT("enclosure: opposite corner is unreached"),
			Field.Dist[Dims.CellIndex(0, 0, 0)], FPSRHoverWindow::UnreachedDist);
		TestEqual(TEXT("enclosure: far cell is unreached"),
			Field.Dist[Dims.CellIndex(4, 4, 4)], FPSRHoverWindow::UnreachedDist);
	}

	// ---- (4) All sources occupied: nothing seeds -> Propagate returns 0. ----
	{
		const FFPSRHoverWindowDims Dims = MakeDims(4, 4, 4);
		TArray<uint64> Occupancy = MakeOpenOccupancy(Dims);
		const int32 SourceCell = Dims.CellIndex(1, 1, 1);
		FPSRHoverWindow::SetOccupied(Occupancy, SourceCell);
		const TArray<int32> Sources = { SourceCell };

		FFPSRHoverWindowField Field;
		const int32 Seeded = FPSRHoverWindow::Propagate(Dims, Occupancy, Sources, Offsets6, Field);
		TestEqual(TEXT("an occupied-only source list seeds nothing"), Seeded, 0);
	}

	// ---- (5) 26-neighbour Chebyshev vs. 6-neighbour Manhattan: a diagonal corner is genuinely closer under the
	//          26 table (uniform-cost diagonal moves), demonstrating why the neighbour table choice matters for
	//          cost SPREAD even before any real-distance weighting decision (ADR 0009 decision item 1). ----
	{
		const FFPSRHoverWindowDims Dims = MakeDims(4, 4, 4);
		const TArray<uint64> Occupancy = MakeOpenOccupancy(Dims);
		const int32 SourceCell = Dims.CellIndex(0, 0, 0);
		const int32 TargetCell = Dims.CellIndex(3, 3, 3);
		const TArray<int32> Sources = { SourceCell };

		FFPSRHoverWindowField Field6;
		FPSRHoverWindow::Propagate(Dims, Occupancy, Sources, Offsets6, Field6);
		FFPSRHoverWindowField Field26;
		FPSRHoverWindow::Propagate(Dims, Occupancy, Sources, FPSRHoverWindow::GetNeighborOffsets26(), Field26);

		TestEqual(TEXT("6-neighbour corner distance is the Manhattan distance (3+3+3=9)"),
			(int32)Field6.Dist[TargetCell], 9);
		TestEqual(TEXT("26-neighbour corner distance is the Chebyshev distance (max(3,3,3)=3)"),
			(int32)Field26.Dist[TargetCell], 3);
		TestTrue(TEXT("26-neighbour diagonal corner is strictly closer than 6-neighbour"),
			Field26.Dist[TargetCell] < Field6.Dist[TargetCell]);
	}

	return true;
}

// (B) Bench matrix — window size x occupancy pattern x neighbour density. Worldless: no scene query, matching the
// production design's offline-baked occupancy (ADR 0009 decision item 3 / invariant 7).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSRHoverWindowBenchTest, "FPSRoguelite.FlowField.HoverWindowBench",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	enum class EFPSRHoverBenchPattern : uint8
	{
		Open,          // 0% occupied — the propagation-cost floor.
		CityLike30Pct, // ~30% occupied, per-cell independent — a dense-obstacle ceiling estimate.
		LayeredSlabs,  // every 8th Z-layer fully occupied but for a handful of holes — the terrain shape §"겹층" argues for.
	};

	// Deterministic per-cell occupancy for the bench (fixed seed 20260821 — 2026-08-21, this spike's authoring
	// date) so the reported numbers are reproducible across runs/machines rather than noise from run to run.
	TArray<uint64> MakeBenchOccupancy(EFPSRHoverBenchPattern Pattern, const FFPSRHoverWindowDims& Dims, int32 SourceCell)
	{
		const int32 NumCells = Dims.NumCells();
		TArray<uint64> Occupancy;
		Occupancy.SetNumZeroed(FPSRHoverWindow::OccupancyWords(NumCells));

		constexpr int32 BenchSeed = 20260821;
		switch (Pattern)
		{
		case EFPSRHoverBenchPattern::Open:
			break;

		case EFPSRHoverBenchPattern::CityLike30Pct:
		{
			FRandomStream Rng(BenchSeed);
			constexpr float OccupiedFraction = 0.30f;
			for (int32 Cell = 0; Cell < NumCells; ++Cell)
			{
				if (Rng.FRand() < OccupiedFraction)
				{
					FPSRHoverWindow::SetOccupied(Occupancy, Cell);
				}
			}
			break;
		}

		case EFPSRHoverBenchPattern::LayeredSlabs:
		{
			FRandomStream Rng(BenchSeed);
			constexpr int32 SlabPeriod = 8;
			constexpr int32 HolesPerPlane = 4;
			for (int32 Z = 0; Z < Dims.DimZ; ++Z)
			{
				if (Z % SlabPeriod != 0)
				{
					continue;
				}
				for (int32 Y = 0; Y < Dims.DimY; ++Y)
				{
					for (int32 X = 0; X < Dims.DimX; ++X)
					{
						FPSRHoverWindow::SetOccupied(Occupancy, Dims.CellIndex(X, Y, Z));
					}
				}
				for (int32 H = 0; H < HolesPerPlane; ++H)
				{
					const int32 HX = Rng.RandRange(0, Dims.DimX - 1);
					const int32 HY = Rng.RandRange(0, Dims.DimY - 1);
					const int32 HoleCell = Dims.CellIndex(HX, HY, Z);
					Occupancy[HoleCell >> 6] &= ~(1ull << (HoleCell & 63)); // punch the hole
				}
			}
			break;
		}
		}

		// Never occupy the source itself — an occupied source seeds nothing (Propagate returns 0), which would
		// time a near-instant no-op instead of the real flood cost every combination is meant to measure.
		Occupancy[SourceCell >> 6] &= ~(1ull << (SourceCell & 63));
		return Occupancy;
	}

	const TCHAR* PatternName(EFPSRHoverBenchPattern Pattern)
	{
		switch (Pattern)
		{
		case EFPSRHoverBenchPattern::Open: return TEXT("open0pct");
		case EFPSRHoverBenchPattern::CityLike30Pct: return TEXT("city30pct");
		case EFPSRHoverBenchPattern::LayeredSlabs: return TEXT("layeredSlabs");
		default: return TEXT("unknown");
		}
	}
}

bool FFPSRHoverWindowBenchTest::RunTest(const FString& Parameters)
{
	struct FWindowSize { int32 DimX, DimY, DimZ; };
	const FWindowSize Sizes[] = { {64, 64, 24}, {96, 96, 32}, {128, 128, 32} };
	const EFPSRHoverBenchPattern Patterns[] =
	{
		EFPSRHoverBenchPattern::Open, EFPSRHoverBenchPattern::CityLike30Pct, EFPSRHoverBenchPattern::LayeredSlabs
	};
	const int32 Iters = 8;

	// Captured for the single catastrophe assert below: the largest window x densest connectivity table combo.
	double WorstCaseBestMs = -1.0;

	for (const FWindowSize& Size : Sizes)
	{
		const FFPSRHoverWindowDims Dims = MakeDims(Size.DimX, Size.DimY, Size.DimZ);
		const int32 NumCells = Dims.NumCells();
		const int32 SourceCell = Dims.CellIndex(Dims.DimX / 2, Dims.DimY / 2, Dims.DimZ / 2);
		const TArray<int32> Sources = { SourceCell };

		for (const EFPSRHoverBenchPattern Pattern : Patterns)
		{
			const TArray<uint64> Occupancy = MakeBenchOccupancy(Pattern, Dims, SourceCell);

			for (int32 NeighborChoice = 0; NeighborChoice < 2; ++NeighborChoice)
			{
				const bool bUse26 = (NeighborChoice == 1);
				const TConstArrayView<FIntVector> Offsets =
					bUse26 ? FPSRHoverWindow::GetNeighborOffsets26() : FPSRHoverWindow::GetNeighborOffsets6();
				const int32 NeighborCount = bUse26 ? 26 : 6;

				FFPSRHoverWindowField Field;
				FPSRHoverWindow::Propagate(Dims, Occupancy, Sources, Offsets, Field); // warm-up, discarded

				double BestMs = 0.0;
				double SumMs = 0.0;
				for (int32 i = 0; i < Iters; ++i)
				{
					const double T0 = FPlatformTime::Seconds();
					FPSRHoverWindow::Propagate(Dims, Occupancy, Sources, Offsets, Field);
					const double Ms = (FPlatformTime::Seconds() - T0) * 1000.0;
					BestMs = (i == 0) ? Ms : FMath::Min(BestMs, Ms);
					SumMs += Ms;
				}
				const double AvgMs = SumMs / Iters;

				// Dist(2B) + StepDir(1B) per cell, plus the occupancy bitfield actually carried for this window.
				const double MemKB = (NumCells * (sizeof(uint16) + sizeof(uint8))
					+ FPSRHoverWindow::OccupancyWords(NumCells) * sizeof(uint64)) / 1024.0;

				AddInfo(FString::Printf(
					TEXT("[0009 P0 HoverWindowBench] %dx%dx%d=%d cells, pattern=%s, neighbors=%d: ")
					TEXT("best=%.3fms avg=%.3fms over %d iters, mem=%.1fKB"),
					Dims.DimX, Dims.DimY, Dims.DimZ, NumCells, PatternName(Pattern), NeighborCount,
					BestMs, AvgMs, Iters, MemKB));

				if (Size.DimX == 128 && Size.DimY == 128 && Size.DimZ == 32
					&& Pattern == EFPSRHoverBenchPattern::Open && bUse26)
				{
					WorstCaseBestMs = BestMs;
				}
			}
		}
	}

	// 4-window co-op approximation (ADR 0009 결정 item 4 — a window per player, up to 4): four independent
	// 96x96x32 open/6-neighbour propagations run back to back and summed. This is NOT the real staggered
	// round-robin scheduler (invariant 5 defers that) — it's the crude "all four land in the same frame" ceiling.
	{
		const FFPSRHoverWindowDims Dims = MakeDims(96, 96, 32);
		const int32 SourceCell = Dims.CellIndex(48, 48, 16);
		const TArray<int32> Sources = { SourceCell };
		const TArray<uint64> Occupancy = MakeBenchOccupancy(EFPSRHoverBenchPattern::Open, Dims, SourceCell);
		const TConstArrayView<FIntVector> Offsets6 = FPSRHoverWindow::GetNeighborOffsets6();

		FFPSRHoverWindowField Field;
		FPSRHoverWindow::Propagate(Dims, Occupancy, Sources, Offsets6, Field); // warm-up, discarded

		double SumMs = 0.0;
		for (int32 W = 0; W < 4; ++W)
		{
			const double T0 = FPlatformTime::Seconds();
			FPSRHoverWindow::Propagate(Dims, Occupancy, Sources, Offsets6, Field);
			SumMs += (FPlatformTime::Seconds() - T0) * 1000.0;
		}
		AddInfo(FString::Printf(
			TEXT("[0009 P0 HoverWindowBench] 4-window co-op approximation (96x96x32 open/6n x4 sequential): total=%.3fms"),
			SumMs));
	}

	// Catastrophe guard only (RecomputeBench precedent, FPSRFlowFieldBudgetTest.cpp): the VALUE is the logged
	// AddInfo numbers above, feeding the P0 window/cell-size decision — this is a "did something go
	// superlinear/broken" tripwire, not a green check / perf target.
	TestTrue(TEXT("worst-case window (128x128x32, open, 26-neighbor) best completes under 1000ms catastrophe ceiling"),
		WorstCaseBestMs >= 0.0 && WorstCaseBestMs < 1000.0);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
