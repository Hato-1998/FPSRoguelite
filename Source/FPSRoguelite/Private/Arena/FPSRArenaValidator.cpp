// Copyright Epic Games, Inc. All Rights Reserved.

#include "Arena/FPSRArenaValidator.h"
#include "Arena/FPSRArenaCells.h"

namespace
{
	/** Cheap read-only view over a layout's open/blocked cells, so the checks below read like their descriptions. */
	struct FArenaCells
	{
		const FFPSRArenaLayout& L;
		const int32 W;
		const int32 H;

		/** P3 redteam fix: RunX/RunY used to walk outward from the query cell to the nearest wall on every call —
		 *  O(W+H) each — and the swap-frame scan below (Validate's main sweep) calls them once per open cell, so
		 *  the whole pass was O(OpenCells * (W+H)), worst case ~8.2M cell visits at 160x160. These two arrays make
		 *  every RunX/RunY call an O(1) lookup instead — see ComputeAxisRunLengths for how they are built. Same
		 *  DEFINITION as the old walk (self cell included, maximal contiguous open run along that axis); only the
		 *  cost changed. */
		TArray<int32> RunXLen;
		TArray<int32> RunYLen;

		explicit FArenaCells(const FFPSRArenaLayout& InLayout)
			: L(InLayout), W(InLayout.GridDims.X), H(InLayout.GridDims.Y)
		{
			ComputeAxisRunLengths(/*bAlongX=*/true, RunXLen);
			ComputeAxisRunLengths(/*bAlongX=*/false, RunYLen);
		}

		bool Open(int32 CX, int32 CY) const { return FFPSRArenaCells::IsCellOpen(L, CX, CY); }

		/** Length of the maximal run of open cells along X (resp. Y) containing this cell. O(1) — see RunXLen/RunYLen.
		 *  Bounds-checked defensively (every real caller already only asks about an open, in-grid cell). */
		int32 RunX(int32 CX, int32 CY) const
		{
			return (CX >= 0 && CY >= 0 && CX < W && CY < H) ? RunXLen[CY * W + CX] : 0;
		}
		int32 RunY(int32 CX, int32 CY) const
		{
			return (CX >= 0 && CY >= 0 && CX < W && CY < H) ? RunYLen[CY * W + CX] : 0;
		}

		/** Two-pass O(W*H) run length for one axis: a forward pass accumulates a consecutive-open count along the
		 *  row (resp. column); a backward pass then propagates each run's final (i.e. FULL) length back across
		 *  every cell inside that run — every open cell in one contiguous run shares that same containing-run
		 *  length by definition, so the value the run's last cell computed is the answer for all of them. */
		void ComputeAxisRunLengths(bool bAlongX, TArray<int32>& OutRun) const
		{
			OutRun.Init(0, W * H);
			if (bAlongX)
			{
				for (int32 CY = 0; CY < H; ++CY)
				{
					int32 Running = 0;
					for (int32 CX = 0; CX < W; ++CX)
					{
						Running = Open(CX, CY) ? Running + 1 : 0;
						OutRun[CY * W + CX] = Running;
					}
					for (int32 CX = W - 2; CX >= 0; --CX)
					{
						if (Open(CX, CY) && Open(CX + 1, CY)) { OutRun[CY * W + CX] = OutRun[CY * W + CX + 1]; }
					}
				}
			}
			else
			{
				for (int32 CX = 0; CX < W; ++CX)
				{
					int32 Running = 0;
					for (int32 CY = 0; CY < H; ++CY)
					{
						Running = Open(CX, CY) ? Running + 1 : 0;
						OutRun[CY * W + CX] = Running;
					}
					for (int32 CY = H - 2; CY >= 0; --CY)
					{
						if (Open(CX, CY) && Open(CX, CY + 1)) { OutRun[CY * W + CX] = OutRun[(CY + 1) * W + CX]; }
					}
				}
			}
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
	// reads false for an off-grid cell (FFPSRArenaCells::IsCellOpen bounds-checks first), so a single check
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
	// ADR 0012: a BAKED arena has no derived wiring to check. Its corridors are authored geometry and its floor
	// traces are meshes a person placed, so "the L2 derivation produced nothing" is not a finding about the arena —
	// there was no derivation. Asking anyway would fire on every healthy baked arena.
	if (R.TraceSegments == 0 && !Layout.bFromBake)
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

	// The baked equivalent of the "empty box" check, and the one that actually matters (ADR 0012 invariant 2:
	// collision IS the mask). If the bake found nothing to block with, every cell is open — which looks like a
	// clean PASS while meaning the exact opposite: the mask the swarm will use has no walls in it. The usual cause
	// is geometry that is not ECC_WorldStatic with query collision, so the obstacle probe passes straight through
	// it. This is an ERROR rather than a warning because a wall-less arena is not a playable arena, and the
	// failure is silent everywhere else — the level looks completely normal on screen.
	if (Layout.bFromBake && R.OpenCells == C.W * C.H)
	{
		R.Errors.Add(FString::Printf(
			TEXT("The bake found NO obstacles — all %d cells are open. The swarm would walk through every wall in "
			     "this level. Check that the blocking geometry is ECC_WorldStatic with Query collision enabled: "
			     "anything else is invisible to the obstacle probe (ADR 0012 invariant 2)."),
			R.OpenCells));
	}

	// Warning, not an error: a single-loop arena is still a valid arena. But ADR 0010 D1 makes the crossing
	// points where circulation branches the visible heart of the topology, and a junction count of zero means the
	// wiring never forks — i.e. there is nowhere the run actually offers a choice, which is worth flagging even
	// though it does not by itself break anything.
	if (R.TraceJunctions == 0 && !Layout.bFromBake)
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
