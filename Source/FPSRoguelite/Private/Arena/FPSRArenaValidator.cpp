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
		TEXT("open=%d component=%d edges=%d narrowest=%d pockets=%d slack=%.0f%% -> %s (%d error(s), %d warning(s))"),
		Result.OpenCells, Result.LargestComponent, Result.OpenEdges, Result.NarrowestCorridor,
		Result.PocketCells, Result.SlackFraction * 100.0f,
		Result.Passed() ? TEXT("PASS") : TEXT("FAIL"), Result.Errors.Num(), Result.Warnings.Num());
}
