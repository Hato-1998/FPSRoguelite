// Copyright Epic Games, Inc. All Rights Reserved.

#include "Arena/FPSRArenaValidator.h"
#include "Arena/FPSRArenaGenerator.h"

namespace
{
	/** Cheap read-only view over a layout's open/blocked cells, so the checks below read like their descriptions. */
	struct FArenaCells
	{
		const FFPSRArenaLayout& L;
		const int32 W;
		const int32 H;

		explicit FArenaCells(const FFPSRArenaLayout& InLayout)
			: L(InLayout), W(InLayout.GridDims.X), H(InLayout.GridDims.Y) {}

		bool Open(int32 CX, int32 CY) const { return FFPSRArenaGenerator::IsCellOpen(L, CX, CY); }

		/** Length of the maximal run of open cells along X (resp. Y) containing this cell. */
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
	};
}

FFPSRArenaValidationResult FFPSRArenaValidator::Validate(const FFPSRArenaLayout& Layout, const FFPSRArenaGenParams& Params)
{
	FFPSRArenaValidationResult R;

	if (!Layout.IsValid())
	{
		R.Errors.Add(TEXT("Layout is not valid (no grid). Nothing to check."));
		return R;
	}

	const FArenaCells C(Layout);
	const int32 MinWidth = FMath::Max(1, Params.MinCorridorWidthCells);

	// --- counts + narrowest corridor + pockets, in one sweep ------------------------------------------------
	R.NarrowestCorridor = MAX_int32;
	int32 SlackCells = 0;
	for (int32 CY = 0; CY < C.H; ++CY)
	{
		for (int32 CX = 0; CX < C.W; ++CX)
		{
			if (!C.Open(CX, CY)) { continue; }
			++R.OpenCells;

			if (C.Open(CX + 1, CY)) { ++R.OpenEdges; }
			if (C.Open(CX, CY + 1)) { ++R.OpenEdges; }

			const int32 Narrow = FMath::Min(C.RunX(CX, CY), C.RunY(CX, CY));
			R.NarrowestCorridor = FMath::Min(R.NarrowestCorridor, Narrow);
			if (Narrow > MinWidth) { ++SlackCells; }

			// Outside the grid counts as wall: an open cell hemmed in on three sides is a dead end no matter
			// whether the fourth side is geometry or the arena boundary.
			int32 Walls = 0;
			Walls += C.Open(CX + 1, CY) ? 0 : 1;
			Walls += C.Open(CX - 1, CY) ? 0 : 1;
			Walls += C.Open(CX, CY + 1) ? 0 : 1;
			Walls += C.Open(CX, CY - 1) ? 0 : 1;
			if (Walls >= 3) { ++R.PocketCells; }
		}
	}
	if (R.OpenCells == 0)
	{
		R.NarrowestCorridor = 0;
		R.Errors.Add(TEXT("No open cells at all — the arena is solid."));
		return R;
	}
	R.SlackFraction = static_cast<float>(SlackCells) / static_cast<float>(R.OpenCells);

	// L2 floor wiring: READ, not recomputed — Layout.Traces/TraceJunctions are already the ground truth produced
	// by FFPSRArenaGenerator::DeriveFloorTraces, and re-deriving them here would be a second implementation of
	// that algorithm that could silently disagree with the first.
	R.TraceSegments = Layout.Traces.Num();
	R.TraceJunctions = Layout.TraceJunctions.Num();

	// --- connectivity: flood from the first open cell -------------------------------------------------------
	{
		TArray<bool> Seen;
		Seen.Init(false, C.W * C.H);
		TArray<int32> Stack;
		for (int32 CY = 0; CY < C.H && Stack.Num() == 0; ++CY)
		{
			for (int32 CX = 0; CX < C.W && Stack.Num() == 0; ++CX)
			{
				if (C.Open(CX, CY)) { Stack.Add(CY * C.W + CX); Seen[CY * C.W + CX] = true; }
			}
		}
		while (Stack.Num() > 0)
		{
			const int32 Cell = Stack.Pop();
			++R.LargestComponent;
			const int32 CX = Cell % C.W;
			const int32 CY = Cell / C.W;
			const int32 DX[4] = { 1, -1, 0, 0 };
			const int32 DY[4] = { 0, 0, 1, -1 };
			for (int32 D = 0; D < 4; ++D)
			{
				const int32 NX = CX + DX[D];
				const int32 NY = CY + DY[D];
				if (!C.Open(NX, NY)) { continue; }
				const int32 NCell = NY * C.W + NX;
				if (Seen[NCell]) { continue; }
				Seen[NCell] = true;
				Stack.Add(NCell);
			}
		}
	}

	// --- verdicts -------------------------------------------------------------------------------------------
	if (R.LargestComponent != R.OpenCells)
	{
		R.Errors.Add(FString::Printf(
			TEXT("Arena is split: %d of %d open cells are unreachable from the rest. Every objective and spawn in a "
			     "cut-off pocket is unreachable too."),
			R.OpenCells - R.LargestComponent, R.OpenCells));
	}

	// Connected with |E| >= |V| means at least one independent cycle. Without one the arena is a tree: every route
	// out is the route you came in, which is the "loop that is really a dead end" ADR 0010 D1 exists to prevent.
	if (R.OpenEdges < R.OpenCells)
	{
		R.Errors.Add(FString::Printf(
			TEXT("No circulation loop: the open space is a tree (edges %d < cells %d). Kiting has nowhere to go — "
			     "every path doubles back."),
			R.OpenEdges, R.OpenCells));
	}

	if (R.NarrowestCorridor < MinWidth)
	{
		R.Errors.Add(FString::Printf(
			TEXT("Corridor too narrow: %d cells, minimum is %d. A corridor that cannot pass the player and a stream "
			     "of enemies side by side is a death trap, not a route."),
			R.NarrowestCorridor, MinWidth));
	}

	if (R.PocketCells > 0)
	{
		R.Errors.Add(FString::Printf(
			TEXT("%d concave pocket cell(s): open cells walled on 3+ sides. Enemies pushed in cannot steer out "
			     "(ADR 0010 invariant 7). Move the blocking volumes apart or square off the corner."),
			R.PocketCells));
	}

	// --- landmarks: none may be buried (ADR 0011 E4 check (5)) -----------------------------------------------
	// A landmark is the only thing that answers "which way am I facing" in a uniform-floor first-person arena
	// (see AFPSRArenaLandmark's class comment), so one sitting on a cell the mask calls closed is a real failure —
	// it looks authored but is unreachable/invisible from the floor the player actually walks. C.Open() already
	// reads false for an off-grid cell (FFPSRArenaGenerator::IsCellOpen bounds-checks first), so a single check
	// covers "off the grid entirely" and "on the grid but blocked" both.
	for (const FFPSRArenaAuthoredLandmark& Landmark : Layout.Landmarks)
	{
		const int32 LCX = FMath::FloorToInt((Landmark.Location.X - Layout.GridOrigin.X) / Layout.CellSize);
		const int32 LCY = FMath::FloorToInt((Landmark.Location.Y - Layout.GridOrigin.Y) / Layout.CellSize);
		if (!C.Open(LCX, LCY))
		{
			++R.BuriedLandmarks;
		}
	}
	if (R.BuriedLandmarks > 0)
	{
		R.Errors.Add(FString::Printf(
			TEXT("%d landmark(s) buried: anchor cell is off-grid or blocked. A buried landmark cannot do its one "
			     "job — the only wayfinding cue on a uniform floor — so move the landmark or the blocking volume."),
			R.BuriedLandmarks));
	}

	// L2 floor wiring is derived from L0 alone (ADR 0010 D8), and it tracks CORRIDOR structure rather than floor
	// area: the medial axis of one solid open rectangle is a point, so an arena with nothing in it wires up to
	// nothing. That is a correct answer for a structureless arena, not a derivation failure — which is why the
	// verdict here depends on whether anything was actually placed to route around.
	if (R.TraceSegments == 0)
	{
		if (Layout.Clusters.Num() > 0)
		{
			R.Errors.Add(FString::Printf(
				TEXT("No floor wiring despite %d blocking cluster(s): the open space should route around them, so the "
				     "L2 corridor-graph derivation failed."),
				Layout.Clusters.Num()));
		}
		else
		{
			R.Warnings.Add(TEXT("No floor wiring, because nothing is placed to route around — this arena is one empty "
				"box. Place blocking volumes to create corridors (ADR 0010 D1: the topology IS the blockers)."));
		}
	}

	// Warning, not an error: a single-loop arena is still a valid arena. But ADR 0010 D1 makes the crossing
	// points where circulation branches the visible heart of the topology, and a junction count of zero means the
	// wiring never forks — i.e. there is nowhere the run actually offers a choice, which is worth flagging even
	// though it does not by itself break anything.
	if (R.TraceJunctions == 0)
	{
		R.Warnings.Add(TEXT("No wiring junctions: the floor traces never fork, so the arena is probably one single loop. "
			"ADR 0010 D1 puts the crossing — where circulation branches and the player has to choose each lap — at the "
			"heart of the topology, and a layout with no fork never shows that choice."));
	}

	// Warning, not an error: a deliberately tight arena is a legitimate choice. But if there is no slack anywhere,
	// L1 has nothing to place into and every run of this arena will look identical — the variation quietly dies and
	// nobody notices, which is exactly why it is worth saying out loud (ADR 0011 실패 흐름 2).
	if (R.SlackFraction < 0.05f)
	{
		R.Warnings.Add(FString::Printf(
			TEXT("Almost no corridor slack (%.1f%% of open cells are wider than the minimum). Procedural micro-props "
			     "have nowhere to go, so every run of this arena will be laid out identically."),
			R.SlackFraction * 100.0f));
	}

	return R;
}

FString FFPSRArenaValidator::Summarize(const FFPSRArenaValidationResult& Result)
{
	return FString::Printf(
		TEXT("open=%d component=%d edges=%d narrowest=%d pockets=%d traces=%d junctions=%d slack=%.0f%% buried=%d -> %s (%d error(s), %d warning(s))"),
		Result.OpenCells, Result.LargestComponent, Result.OpenEdges, Result.NarrowestCorridor,
		Result.PocketCells, Result.TraceSegments, Result.TraceJunctions, Result.SlackFraction * 100.0f,
		Result.BuriedLandmarks,
		Result.Passed() ? TEXT("PASS") : TEXT("FAIL"), Result.Errors.Num(), Result.Warnings.Num());
}
