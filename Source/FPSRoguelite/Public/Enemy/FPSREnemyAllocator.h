// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Map-aware swarm budget apportionment (multimap Tier 0, "the heart of the design" — Codex consult 2026-07-05).
 *
 * Pure, world-independent math: split a single GLOBAL alive target across the occupied maps so the total never exceeds
 * the global cap (per-map caps are forbidden — they collapse the host budget). Exercised headless by the allocator
 * unit test. The spawn subsystem builds the per-map player counts, calls Apportion, then fills/drains toward the result.
 *
 * Weighting (temp Tier-0 policy — the content-aware allocator is Tier 1): weight = players + (players>=2 ? GroupBonus : 0),
 * so a 2+ front out-weights two solo maps in AGGREGATE without inverting per-capita density (a solo map is never starved).
 * Empty maps (0 players) get target 0.
 */
namespace FPSREnemyAllocator
{
	/** Weight for a map with the given committed player count (0 players -> 0). Temp Tier-0 policy. */
	FORCEINLINE int32 MapWeight(int32 PlayerCount, int32 GroupBonus)
	{
		if (PlayerCount <= 0)
		{
			return 0;
		}
		return PlayerCount + (PlayerCount >= 2 ? GroupBonus : 0);
	}

	/**
	 * Largest-remainder (Hamilton) apportionment of GlobalTarget across maps by MapWeight(PlayerCount).
	 * OutTargets is sized to PlayerCounts.Num(); OutTargets[i] is map i's alive target. Guarantees:
	 *   - sum(OutTargets) == GlobalTarget exactly (when total weight > 0 and GlobalTarget >= 0), no rounding drift;
	 *   - OutTargets[i] == 0 for any map with PlayerCounts[i] == 0 (empty map -> 0);
	 *   - each OutTargets[i] >= 0.
	 * GlobalTarget MUST already be clamped by the caller to (GlobalAliveCap - SeedReserve) so the reserve headroom is
	 * preserved below the hard cap for new-map entry seeding.
	 */
	FPSROGUELITE_API void Apportion(const TArray<int32>& PlayerCounts, int32 GlobalTarget, int32 GroupBonus, TArray<int32>& OutTargets);
}
