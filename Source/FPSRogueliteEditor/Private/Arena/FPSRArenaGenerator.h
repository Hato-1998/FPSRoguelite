// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Arena/FPSRArenaTypes.h"

class AFPSRArenaActor;

/**
 * Turns what the designer placed into cells and props — an EDITOR-ONLY tool (ADR 0012).
 *
 * ## Why this is not in the runtime module any more
 *
 * ADR 0011 E5 demoted the L0 lattice pass to an editor "starting layout" button. ADR 0012 finished the job:
 * L1 and L2 became authored too, the mask now comes from an editor bake of the level's own collision, and the
 * runtime generates NOTHING (invariant 3 — what the editor shows IS what ships). Leaving the generator linked
 * into the shipping module would leave a live path back to the failure this ADR exists to close, where the
 * level a designer looks at and the mask the swarm uses came from different places.
 *
 * Two of the old class's functions could not come along, because they are not about generating: cell footprint
 * math for destructibles and the traversability predicate. They live in FFPSRArenaCells
 * (Runtime/Public/Arena/FPSRArenaCells.h) and are shared by both sides — see that header.
 *
 * ## Still deterministic, still world-free
 *
 * Touches NO world, subsystem or singleton, and the only randomness is FRandomStream. That mattered for a
 * different reason before (every client rebuilt the layout locally, so machines had to agree byte for byte);
 * now it matters because a designer pressing "propose layout" twice on the same seed should get the same
 * proposal. FMath::Rand shares global state and TMap/TSet iteration order is unspecified — either one breaks
 * that.
 *
 * ## Rasterised, not traced
 *
 * Authored blocking volumes are converted to cells by pure geometry; they are never discovered with world
 * traces. This is what the editor tools preview and validate against. The SHIPPING mask is a separate thing —
 * UFPSRArenaBakeDataAsset, baked from real collision (ADR 0012 axis 3).
 */
class FFPSRArenaGenerator
{
public:
	/**
	 * Build a layout from authored input. ArenaOrigin is the world-space min corner of cell (0,0); its Z is the
	 * arena floor. Returns false (leaving OutLayout default-constructed) only if Params are not geometrically
	 * satisfiable.
	 *
	 * Empty Authored is NOT an error: it produces a valid, entirely open arena. Whether that is acceptable is the
	 * validator's call, not the generator's — the generator does not invent a skeleton nobody placed.
	 */
	static bool Generate(int32 Seed, const FFPSRArenaGenParams& Params, const FVector& ArenaOrigin,
		const FFPSRArenaAuthoredInput& Authored, FFPSRArenaLayout& OutLayout);

	/**
	 * Gather Arena's authored marker actors and generate a layout for Seed, WITHOUT touching the actor's state.
	 *
	 * This used to be AFPSRArenaActor::BuildLayoutForSeed. It moved here with the generator because the runtime
	 * lost its last caller when ADR 0012 removed BuildLocalLayout — the authoring tools (propose / validate
	 * preview) are all that is left, and an actor method that only the editor calls is a runtime dependency on
	 * the generator in everything but name.
	 *
	 * Membership is the arena's own AFPSRArenaActor::ContainsWorldLocation, NOT the level the markers sit in:
	 * that is the one spatial test every marker, tool and spawn gate shares, so "which arena owns this actor"
	 * cannot drift between call sites.
	 */
	static bool BuildLayoutForSeed(const AFPSRArenaActor& Arena, int32 Seed, FFPSRArenaLayout& OutLayout);

	/**
	 * Propose a starting cluster lattice (ADR 0011 E5 — the editor tool's button). Deterministic in Seed.
	 * Guarantees, by construction rather than by checking: at least one circulation loop, corridor junctions,
	 * convex blockers only, and no two clusters closer than the minimum corridor width.
	 */
	static bool ProposeLatticeClusters(int32 Seed, const FFPSRArenaGenParams& Params,
		TArray<FFPSRArenaCluster>& OutClusters, FIntPoint& OutLattice);

	/** Cell-rect -> world box, so a proposal can be round-tripped through the same rasteriser Generate uses
	 *  instead of a second code path that agrees with it only until someone edits one of them. */
	static FFPSRArenaAuthoredBox ClusterToAuthoredBox(const FFPSRArenaCluster& Cluster,
		const FFPSRArenaGenParams& Params, const FVector& ArenaOrigin);

	/**
	 * Derive L2 floor wiring traces from the L0 corridor mask (ADR 0010 D8): Chebyshev-distance-transform the
	 * open-cell mask, thin it to a 1-cell-wide skeleton (Zhang-Suen), then emit one trace segment per adjacent
	 * skeleton-cell pair and flag the cells where the skeleton branches as junctions. Fills Layout.Traces and
	 * Layout.TraceJunctions from Layout.Surface/GridDims as they stand when called — MUST run after L0 (blockers,
	 * landmarks, destructibles) and BEFORE PlaceMicroProps (L1); see the call site in Generate() for why.
	 *
	 * Also fills OutSkeletonCells with the thinned skeleton mask itself (GridDims.X*GridDims.Y bool bitmap, same
	 * cell-index convention as Layout.Surface) so Generate() can hand it to PlaceMicroProps — L1 needs to know
	 * which cells are the L2 wiring so it can refuse to plant a BLOCKING prop on top of a bright "you can walk
	 * here" line (see PlaceMicroProps' Pass 1 fit test).
	 *
	 * Uses NO randomness whatsoever — not even FRandomStream. That is a STRONGER guarantee than this class's
	 * usual seed-determinism: the same authored skeleton produces the same wiring regardless of seed, because
	 * the seed is never read by this function at all.
	 */
	static void DeriveFloorTraces(FFPSRArenaLayout& Layout, TArray<bool>& OutSkeletonCells);
};
