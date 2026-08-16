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
};
