// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Arena/FPSRArenaTypes.h"

/**
 * Arena cell arithmetic that survives in the SHIPPING module (ADR 0012).
 *
 * 0012 retired runtime arena generation, so `FFPSRArenaGenerator` moved to FPSRogueliteEditor. Two of its
 * functions could not go with it — they are on live gameplay paths and have nothing to do with generating a
 * layout:
 *
 *   - ComputeDestructibleCells: AFPSRArenaDestructible calls it when a prop breaks, to know which cells to
 *     open in the flow field. A BAKED arena still has destructibles (invariant 6 keeps them out of the bake
 *     precisely so they can move at runtime), so this is not legacy.
 *   - IsCellOpen: FFPSRArenaValidator's traversability predicate. The validator still runs at world start as
 *     an alarm (0011 E4), which is a runtime path.
 *
 * Everything here is pure geometry — no world, no subsystem, no randomness — which is what makes it safe to
 * share between the editor generator and the runtime.
 */
class FPSROGUELITE_API FFPSRArenaCells
{
public:
	/** Surface index for (cell, rank). MUST match UFPSRFlowFieldComputer's own Surf(Cell, Rank) — this is the
	 *  ONE definition of that packing outside the computer itself, so the editor generator forwards to it
	 *  rather than restating the formula (a second copy is a second thing to forget when NumLayers changes). */
	static FORCEINLINE int32 SurfIndex(int32 Cell, int32 Rank)
	{
		return Cell * UFPSRFlowFieldComputer::NumLayers + Rank;
	}

	/** Cell indices an authored destructible occupies (its footprint, anchored at Location, growing +X/+Y).
	 *  Off-grid cells are dropped; the surviving indices come out in ascending order. Both the editor generator
	 *  (blocking these cells while the prop is intact) and the destructible actor (opening them at
	 *  HandleBrokenAuthority) call this SAME function, so the two sides can never disagree about which cells
	 *  one prop owns. */
	static void ComputeDestructibleCells(const FFPSRArenaAuthoredDestructible& Destructible,
		const FVector& ArenaOrigin, float CellSize, const FIntPoint& GridDims, TArray<int32>& OutCells);

	/** True if the cell is inside the grid and not blocked. Mirrors the flow field's own traversability
	 *  predicate (surface exists AND not blocked), so layout and field never disagree about what is walkable. */
	static bool IsCellOpen(const FFPSRArenaLayout& Layout, int32 CX, int32 CY);
};
