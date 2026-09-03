// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class APlayerController;

/** Shared "may this player be targeted right now" rule (BOSS1).
 *
 *  🔴 A PREDICATE, not a gather function. The swarm's movement/attack pass gathers four index-aligned arrays in one
 *  loop with inline allocators (UFPSREnemySpawnSubsystem), and it also refreshes a per-player cache while it walks —
 *  a "collect the eligible players" helper would have to either duplicate that loop or hand back an allocation, on a
 *  path that runs every pass at swarm scale. Extracting only the RULE keeps that loop exactly as it was (one pass,
 *  zero allocations) while making the rule itself impossible to state differently in two places.
 *
 *  Before this existed, the boss's patterns were re-implementing the same two checks inline, which is precisely how
 *  "downed players stop drawing aggro" ends up true for the swarm and false for the boss. */
namespace FPSRTargeting
{
	/** Server: true when PC's player is a legal target this pass.
	 *
	 *  Excluded:
	 *   - not alive (DBNO downed or dead, U9 B17) — a downed teammate stops drawing aggro and also takes no contact
	 *     damage, so targeting them would just waste the attack.
	 *   - a late joiner that has not acked the current topology (U P-F), when bRequireTopologyAck is set. Only
	 *     meaningful with a unified (multimap) field; single-map has no topology to confirm, so callers pass false
	 *     and the check is a strict no-op there.
	 *
	 *  ⚠️ A missing PlayerState is treated as ELIGIBLE, matching the swarm's long-standing behaviour: both checks are
	 *  written as "PS && <disqualifying>", so a pawn whose PlayerState has not replicated yet stays targetable rather
	 *  than becoming briefly invisible to every enemy. Fail-open is the intended direction here.
	 *
	 *  @param Now                    Server time used for the ack timeout (the caller's pass timestamp).
	 *  @param bRequireTopologyAck    Pass true only when a unified multi-slot field is active. */
	FPSROGUELITE_API bool IsEligibleTarget(const APlayerController* PC, float Now, bool bRequireTopologyAck);
}
