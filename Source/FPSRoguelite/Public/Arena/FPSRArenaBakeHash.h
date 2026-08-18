// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class AFPSRArenaActor;

/** What a source scan found. Kept as a value so the five checks of ADR 0012 can compare digests without each
 *  re-deriving what "the source" means. */
struct FFPSRArenaBakeSourceDigest
{
	/** Hex SHA1 over the canonical description of every contributing component. Empty = the scan failed. */
	FString Hash;

	/** How many components went into the hash. Surfaced separately because "0 or 1" is the signature of a
	 *  collision-setup mistake, and a bare hash cannot say that. */
	int32 ComponentCount = 0;

	/** How many distinct actors those components belong to — the number stored on the bake asset. */
	int32 ActorCount = 0;

	bool IsValid() const { return !Hash.IsEmpty(); }
};

/**
 * The one definition of "what the arena bake reads" (ADR 0012).
 *
 * This lives in the RUNTIME module on purpose even though only the editor bakes. ADR 0011 E4 settled that a rule
 * with two implementations is a rule that will disagree with itself, and ADR 0012 puts five checks on this hash —
 * the editor baker, the save-time warning, the manual validate tool, the PreSubmit validator, the CI commandlet,
 * and the world-start alarm. The last one runs in a shipped-ish runtime, so an editor-only implementation would
 * force a second copy, and the copy that drifts is always the one nobody is looking at.
 *
 * WHAT COUNTS: components whose collision object type is ECC_WorldStatic with query collision enabled, whose
 * bounds fall inside the arena. That is not a taste call — it is exactly what UFPSRFlowFieldComputer's obstacle
 * probe traces (FCollisionObjectQueryParams::AddObjectTypesToQuery(ECC_WorldStatic)). If the two ever diverge the
 * hash starts guarding the wrong set and reports "fresh" for a mask that is stale, which is worse than no check.
 * Change one, change the other.
 *
 * Destructibles and randomly-placed props are excluded for free: ADR 0010 D7 already requires them NOT to be
 * WorldStatic, and ADR 0012 invariant 6 turns that into a hard rule. They reach the mask through
 * StampCellBlocked at runtime, so they must not move this hash.
 */
class FPSROGUELITE_API FFPSRArenaBakeHash
{
public:
	/**
	 * Scan Arena's level for the geometry the bake would see and hash it.
	 *
	 * Transforms are hashed RELATIVE TO THE ARENA (ADR 0012 invariant 8), so parking the arena somewhere else —
	 * or streaming its sublevel in at a different LevelTransform — does not read as "the level changed".
	 */
	static bool Compute(const AFPSRArenaActor& Arena, FFPSRArenaBakeSourceDigest& Out);
};
