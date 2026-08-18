// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Editor-side authoring helpers for the arena (ADR 0011 E5 + E4).
 *
 * The starting-layout action is what is left of the old procedural L0. 0011 moved skeleton authoring to a
 * person, but the lattice construction it replaced had one property worth keeping: it could not produce a
 * broken arena. Demoting it to a button preserves that as a STARTING POINT — the designer begins from a layout
 * that already circulates, then edits it, and from the first edit onward the validator is what holds the line.
 *
 * The validate action exists because 0011 E4 puts the verdict in the editor. A runtime check arrives after the
 * map is built, with nobody around to fix it; this is the one that can still change the outcome.
 */
class FFPSRArenaAuthoringTool
{
public:
	/** Replace/'populate the current level with a proposed cluster layout, as real AFPSRArenaBlocker actors. */
	static void ProposeStartingLayout();

	/** Run the shared validator over the level's authored arena and report. */
	static void ValidateArenaInLevel();

	/**
	 * Bake every arena's obstacle mask from the level's collision into its UFPSRArenaBakeDataAsset (ADR 0012).
	 *
	 * This is the action that makes invariant 1 true. What ADR 0010 rejected was tracing the world AT RUNTIME —
	 * thousands of downtraces per stage transition, a cell size that quietly coarsens itself, a spare arena
	 * poisoning the PlayerStart anchor. None of those are objections to doing it once, here, with a person
	 * watching: the budget overflow becomes a refusal instead of a coarsen, and the anchor is the arena actor's
	 * own Z rather than whatever a downtrace found.
	 *
	 * Marks the assets dirty rather than saving them. Saving on the user's behalf would put a ~300 KB write
	 * behind a menu click they may have hit to see what would happen.
	 */
	static void BakeArenasInLevel();
};
