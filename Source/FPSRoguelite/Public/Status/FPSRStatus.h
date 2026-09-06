// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Status/FPSRStatusTypes.h"

/** STAT1 §5-4 / §7 — stateless rules for the swarm's lightweight status-effect bitmask. Precedent =
 *  Combat/FPSRVitals.h: every function here is pure (no world, no actor, no time-of-day concept beyond the
 *  NowStatusClock the CALLER passes in), so it costs nothing when nobody calls it and O(catalog size, <= 8) when it
 *  is called — it can never become a per-enemy tick or UObject (핵심원칙 1, 200-300 concurrent enemies).
 *
 *  🔴 STAT1 B단계 scope — this file defines the rules only. Nothing here reads
 *  AFPSRGameState::GetStatusClockSeconds, calls FPSRCombat::ApplyDamage, or touches an AActor or UFPSRWeaponInstance
 *  pointer (DotInstigator/DotSourceWeapon are written by the CALLER — UFPSREnemyHealthComponent::ApplyStatus — never by
 *  these functions; see that component's header for why). Wiring (movement/attack/damage hooks, the batch pass,
 *  cards, boss Tick) is STAT1 C단계. */
namespace FPSRStatus
{
	/** Server: apply/refresh one slot from Catalog. Rejects (returns false, touches nothing) when: Slot has no
	 *  catalog entry; NowStatusClock is still before SlotCooldownUntil[Slot] (§10 test 7); or the Kind-matched
	 *  resist (WeakResist for a Weak slot, StrongResist for Strong) is <= 0 (§10 test 5). Otherwise applies/refreshes
	 *  per Status->Refresh (§10 test 1) and immediately re-checks every Strong combo in catalog array order
	 *  (§7-1 — judged HERE, not deferred to a batch pass, so a same-instant material pair can't miss a step).
	 *  OutFired collects any Strong slots that fired or refreshed via that immediate combo check.
	 *  🔴 Cold-start anchor (명세 갭 — see FPSRStatus.cpp's file header): also stamps
	 *  State.LastStatusStepClock = NowStatusClock the moment InOutBits goes from fully-dormant (0) to non-zero, so
	 *  Advance's very next DoT step can't compute a bogus multi-second interval against a stale timestamp. */
	FPSROGUELITE_API bool Apply(uint8& InOutBits, FFPSRStatusServerState& State, const UFPSRStatusCatalogDataAsset& Catalog,
		uint8 Slot, float NowStatusClock, float WeakResist, float StrongResist,
		TArray<uint8, TInlineAllocator<8>>& OutFired);

	/** Server: advance one step = [State.LastStatusStepClock, NowStatusClock] on the STATUS clock (§6-1 — never
	 *  world time; the C단계 caller passes AFPSRGameState::GetStatusClockSeconds()). Order (§7-2): (1) expire
	 *  everything past its SlotExpiry (2) re-check combos in case a cooldown elapsed without a fresh Apply
	 *  (3) accumulate DoT for whatever was active at the START of this step, clamped to its own expiry if it fell
	 *  mid-step (§7-4). Updates State.LastStatusStepClock unconditionally before returning. Returns true if any bit
	 *  flipped or damage is owed. */
	/** WeakResist/StrongResist are the SAME already-resolved profile scales Apply takes, and for the same reason:
	 *  §7-2 has Advance re-check combos independently (a Strong slot whose RetriggerCooldownSeconds elapses while
	 *  both materials sit active fires here, with no new Apply call), so this pass must be able to enforce §6's
	 *  "보스는 하드 CC 면역, 도트는 받음" gate too. Nothing in the game ever applies a Strong slot directly —
	 *  Strong only ever turns on through a combo — so a resist-blind Advance would be the ONLY way a
	 *  StrongResist-gated target could still get hard-CC'd. */
	FPSROGUELITE_API bool Advance(uint8& InOutBits, FFPSRStatusServerState& State, const UFPSRStatusCatalogDataAsset& Catalog,
		float NowStatusClock, float WeakResist, float StrongResist, float& OutDotDamage,
		TArray<uint8, TInlineAllocator<8>>& OutExpired, TArray<uint8, TInlineAllocator<8>>& OutFired);

	/** Server (or any pure read given bits+catalog): fold every currently-active slot into one cache — multipliers
	 *  multiply, booleans OR (§7-5). Re-derive only when StatusBits itself changes; a same-bits reapply
	 *  (duration-only refresh) doesn't need a new Resolve call. */
	FPSROGUELITE_API FFPSRResolvedStatus Resolve(uint8 Bits, const UFPSRStatusCatalogDataAsset& Catalog);
}
