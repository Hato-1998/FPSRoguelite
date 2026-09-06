// Copyright Epic Games, Inc. All Rights Reserved.

#include "Status/FPSRStatus.h"
#include "Math/UnrealMathUtility.h"

// -------------------------------------------------------------------------------------------------------------
// STAT1 B단계 명세 갭 — recorded here (not just in the implementation report) so the next reader hits the
// reasoning at the exact point it matters:
//
// (1) [RESOLVED in review] Advance's combo re-check needs a resist input, and §5-4's given signature had none.
//     §7-2 has Advance re-check combos on its own (a Strong slot whose RetriggerCooldownSeconds elapses while both
//     materials already sit active fires here, with no new Apply call), and §6's 저항 row exists so a boss can be
//     immune to hard CC (Strong) while still burning (Weak dot). Nothing ever applies a Strong slot directly —
//     Strong only turns on via a combo — so a resist-blind Advance would have been the ONLY way a StrongResist-gated
//     target could still get hard-CC'd. Fixed by giving Advance the same WeakResist/StrongResist parameters Apply
//     takes (spec §5-4 updated to match). An earlier draft instead pinned SlotCooldownUntil to float-max on an
//     immune target; that conflated "immune" with "cooling down" in one field and still leaked the PARTIAL-resist
//     case (0 < resist < 1) whose combo completion happened to be cooldown-deferred. Both are gone.
// (2) FFPSRResolvedStatus's field list is not given anywhere in STAT1.md (§5-3 names the type, never its
//     members). See Public/Status/FPSRStatusTypes.h's own header comment on the struct for the reconstruction.
//
// (3) Apply's cold-start anchor (LastStatusStepClock = NowStatusClock when InOutBits goes 0 -> non-zero) is an
//     ADDITION beyond §7-1's literal text, needed to actually close the bug §7-4 itself describes ("첫 프레임에
//     도트 폭탄을 맞는다"). §7-6's "reset LastStatusStepClock=0 on the 4 closure points" prevents a REUSED actor
//     from inheriting a PRIOR life's stale timestamp, but 0 is exactly as stale as any other value once
//     NowStatusClock is hundreds of seconds into a run — it does not, by itself, stop a fresh (or freshly
//     reused) actor's very FIRST status application from computing a multi-second bogus DoT interval. Anchoring
//     inside Apply closes it for both the reused-actor case and the first-ever-spawn case with one rule.
// -------------------------------------------------------------------------------------------------------------

namespace
{
	const UFPSRStatusEffectDataAsset* FindStatusForSlot(const UFPSRStatusCatalogDataAsset& Catalog, uint8 Slot)
	{
		for (const TObjectPtr<UFPSRStatusEffectDataAsset>& StatusPtr : Catalog.Statuses)
		{
			const UFPSRStatusEffectDataAsset* Status = StatusPtr;
			if (Status && Status->SlotIndex == Slot)
			{
				return Status;
			}
		}
		return nullptr;
	}

	/** Shared by Apply's immediate post-grant check and Advance's periodic recheck (§7-2). Iterates the catalog in
	 *  ARRAY order (§7-3 "카탈로그 배열 순서대로 판정") so an earlier Strong candidate's fire can consume materials a
	 *  LATER candidate in the SAME pass also wanted (§10 unit test 4 — multi-eligible, only the first fires).
	 *  StrongResist is the caller's real value from Apply, or 1.0 (unresisted) from Advance's own recheck — see this
	 *  file's header comment (1) for why a fully-immune target is still safe either way, and what is NOT covered. */
	void EvaluateCombos(uint8& InOutBits, FFPSRStatusServerState& State, const UFPSRStatusCatalogDataAsset& Catalog,
		float NowStatusClock, float StrongResist, TArray<uint8, TInlineAllocator<8>>& OutFired)
	{
		for (const TObjectPtr<UFPSRStatusEffectDataAsset>& StatusPtr : Catalog.Statuses)
		{
			const UFPSRStatusEffectDataAsset* Strong = StatusPtr;
			if (!Strong || Strong->Kind != EFPSRStatusKind::Strong || Strong->RequiredWeakSlots.Num() != 2
				|| Strong->SlotIndex >= 8)
			{
				continue; // malformed content — IsDataValid should already reject this; never trust it blindly here
			}
			const uint8 SlotA = Strong->RequiredWeakSlots[0];
			const uint8 SlotB = Strong->RequiredWeakSlots[1];
			if (SlotA >= 8 || SlotB >= 8)
			{
				continue;
			}

			const uint8 MaterialMask = static_cast<uint8>((1 << SlotA) | (1 << SlotB));
			if ((InOutBits & MaterialMask) != MaterialMask)
			{
				continue; // both materials not present (yet)
			}
			if (NowStatusClock < State.SlotCooldownUntil[Strong->SlotIndex])
			{
				continue; // still cooling down from a previous fire (§3-D lockdown lever)
			}
			if (StrongResist <= 0.0f)
			{
				continue; // immune target (e.g. boss StrongResistScale=0, §6 저항 row) — never fires, nothing to pin
			}

			const uint8 StrongBit = static_cast<uint8>(1 << Strong->SlotIndex);
			const bool bAlreadyActive = (InOutBits & StrongBit) != 0;
			if (!bAlreadyActive || Strong->Refresh == EFPSRStatusRefresh::RefreshDuration)
			{
				// Fresh fire, OR a refreshing re-fire (§10 unit test 8 — a Strong re-trigger extends the existing
				// bit's expiry). A refreshing re-fire is only reachable when bConsumeSources==false left the
				// materials up, or fresh materials were re-applied while the previous fire was still active.
				InOutBits |= StrongBit;
				State.SlotExpiry[Strong->SlotIndex] = NowStatusClock + Strong->DurationSeconds * StrongResist;
				State.SlotCooldownUntil[Strong->SlotIndex] = NowStatusClock + Strong->RetriggerCooldownSeconds;
				if (Strong->bConsumeSources)
				{
					InOutBits &= ~MaterialMask;
				}
				OutFired.Add(Strong->SlotIndex);
			}
			// Refresh==Ignore && already active: a true no-op (materials/cooldown/expiry all left untouched) — see
			// EFPSRStatusRefresh's own header comment for why "Ignore" means literally nothing happens here, not
			// merely "don't extend the timer".
		}
	}
}

namespace FPSRStatus
{
	bool Apply(uint8& InOutBits, FFPSRStatusServerState& State, const UFPSRStatusCatalogDataAsset& Catalog,
		uint8 Slot, float NowStatusClock, float WeakResist, float StrongResist,
		TArray<uint8, TInlineAllocator<8>>& OutFired)
	{
		OutFired.Reset();
		if (Slot >= 8)
		{
			return false;
		}
		const UFPSRStatusEffectDataAsset* Status = FindStatusForSlot(Catalog, Slot);
		if (!Status)
		{
			return false; // no catalog entry owns this slot — nothing to apply
		}
		if (NowStatusClock < State.SlotCooldownUntil[Slot])
		{
			return false; // §7-1 / §10 test 7: cooldown still in the future -> reject, touch nothing
		}

		const float Resist = (Status->Kind == EFPSRStatusKind::Weak) ? WeakResist : StrongResist;
		if (Resist <= 0.0f)
		{
			return false; // §7-1 / §10 test 5: 0 resist -> reject entirely
		}

		// Cold-start anchor — see this file's header comment (3). Must run BEFORE the bit gets set below, since it
		// specifically tests "was every slot off going INTO this call".
		if (InOutBits == 0)
		{
			State.LastStatusStepClock = NowStatusClock;
		}

		const float EffectiveDuration = Status->DurationSeconds * Resist;
		const uint8 Bit = static_cast<uint8>(1 << Slot);
		const bool bAlreadyActive = (InOutBits & Bit) != 0;

		if (!bAlreadyActive || Status->Refresh == EFPSRStatusRefresh::RefreshDuration)
		{
			InOutBits |= Bit;
			State.SlotExpiry[Slot] = NowStatusClock + EffectiveDuration;
			State.SlotCooldownUntil[Slot] = NowStatusClock + Status->RetriggerCooldownSeconds;
		}
		// Refresh==Ignore && already active: no-op (see EvaluateCombos' identical rule above).

		// §7-1: combo judged immediately, inside Apply, so a same-instant material pair can't wait a batch-pass
		// step (up to ~8 frames at S3) before the Strong status lights up.
		EvaluateCombos(InOutBits, State, Catalog, NowStatusClock, StrongResist, OutFired);

		return true;
	}

	bool Advance(uint8& InOutBits, FFPSRStatusServerState& State, const UFPSRStatusCatalogDataAsset& Catalog,
		float NowStatusClock, float WeakResist, float StrongResist, float& OutDotDamage,
		TArray<uint8, TInlineAllocator<8>>& OutExpired, TArray<uint8, TInlineAllocator<8>>& OutFired)
	{
		OutExpired.Reset();
		OutFired.Reset();
		OutDotDamage = 0.0f;

		const float StepStart = State.LastStatusStepClock;
		const float StepEnd = NowStatusClock;

		// Snapshot BEFORE expiry processing so a status that expires mid-step is still recognized as this step's DoT
		// source for the sliver of the step it WAS active (§7-4 활성 구간 클램프) — step 1 below clears its bit from
		// InOutBits, but SlotExpiry[Slot] itself is left alone (only ever moved forward by Apply/EvaluateCombos), so
		// it still gives an exact tail clamp even after the bit is gone.
		const uint8 BitsAtStepStart = InOutBits;

		bool bChanged = false;

		// 1) Expiry first (§7-2). Timestamp comparison only — no DeltaSeconds/stride correction (rev1's discarded
		// approach, §7-2's own warning: a stride factor helps a continuously-active effect, it buys nothing here).
		for (const TObjectPtr<UFPSRStatusEffectDataAsset>& StatusPtr : Catalog.Statuses)
		{
			const UFPSRStatusEffectDataAsset* Status = StatusPtr;
			if (!Status || Status->SlotIndex >= 8)
			{
				continue;
			}
			const uint8 Bit = static_cast<uint8>(1 << Status->SlotIndex);
			if ((InOutBits & Bit) != 0 && NowStatusClock >= State.SlotExpiry[Status->SlotIndex])
			{
				InOutBits &= ~Bit;
				OutExpired.Add(Status->SlotIndex);
				bChanged = true;
			}
		}

		// 2) Combo — catches a Strong slot whose RetriggerCooldownSeconds elapses with both materials already
		// sitting active and no fresh Apply call to re-trigger the check (§7-2 "부여 없이 성립하는 경우 대비"). No
		// resist input in this signature (§5-4) — see this file's header comment (1) for the residual gap this
		// leaves, and why a fully-immune target is still safe via Apply's cooldown pin.
		{
			TArray<uint8, TInlineAllocator<8>> ComboFired;
			EvaluateCombos(InOutBits, State, Catalog, NowStatusClock, StrongResist, ComboFired);
			if (ComboFired.Num() > 0)
			{
				OutFired.Append(ComboFired);
				bChanged = true;
			}
		}

		// 3) DoT (§7-4) — uses the PRE-expiry snapshot so a status that expired mid-step still pays out for the
		// active sliver of this step it covered, instead of silently losing it.
		const UFPSRStatusEffectDataAsset* DotSource = nullptr;
		for (const TObjectPtr<UFPSRStatusEffectDataAsset>& StatusPtr : Catalog.Statuses)
		{
			const UFPSRStatusEffectDataAsset* Status = StatusPtr;
			if (Status && Status->SlotIndex < 8 && Status->DamagePerSecond > 0.0f
				&& (BitsAtStepStart & (1 << Status->SlotIndex)) != 0)
			{
				DotSource = Status; // single-DoT-source premise (catalog IsDataValid warns otherwise) — first match
				break;              // wins, deterministic via catalog array order
			}
		}

		if (DotSource)
		{
			// Active-window clamp (§7-4): this step's own window is [StepStart, StepEnd]; the status's own window is
			// [grant, expiry]. There is no stored per-slot grant timestamp (§5-3's field list has none) — Apply's
			// cold-start anchor (header comment 3) keeps that safe: at the START of any Advance call, a bit that was
			// ALREADY active going in has been continuously active since at least StepStart (either it was active
			// last step too, or it just woke a fully-dormant actor, which re-anchored StepStart to its own grant
			// moment). The one clamp still needed is the TAIL, for a status that expires mid-step.
			const float IntervalEnd = FMath::Min(StepEnd, State.SlotExpiry[DotSource->SlotIndex]);
			const float ActiveSeconds = FMath::Max(0.0f, IntervalEnd - StepStart);

			// Defensive floor — ClampMin=0.05 on the UPROPERTY only restrains the editor slider (P2-1 머지 게이트
			// convention, matches FPSRCardPoolDataAsset/FPSRVitalsProfileDataAsset's own IsDataValid re-checks): an
			// imported/scripted 0 here would otherwise spin the loop below forever.
			const float TickInterval = FMath::Max(DotSource->DotTickIntervalSeconds, 0.05f);

			// Strict "<" (not "<="): DotAccumulator lands on EXACTLY 0.0 whenever ActiveSeconds is a whole multiple
			// of TickInterval, meaning "the next tick is due at this exact instant, not before it" — treating that
			// as one more due tick would over-count by one whenever the numbers divide evenly (e.g. 1.0s of active
			// time at a 0.5s interval must pay out exactly 2 ticks = DamagePerSecond * 1.0s, not 3).
			State.DotAccumulator -= ActiveSeconds;
			while (State.DotAccumulator < 0.0f)
			{
				OutDotDamage += DotSource->DamagePerSecond * TickInterval;
				State.DotAccumulator += TickInterval;
			}
		}

		State.LastStatusStepClock = NowStatusClock;
		return bChanged || OutDotDamage > 0.0f;
	}

	FFPSRResolvedStatus Resolve(uint8 Bits, const UFPSRStatusCatalogDataAsset& Catalog)
	{
		FFPSRResolvedStatus Resolved; // defaults = 1.0/1.0/1.0/false/false (no active status = no-op, §5-1)
		for (const TObjectPtr<UFPSRStatusEffectDataAsset>& StatusPtr : Catalog.Statuses)
		{
			const UFPSRStatusEffectDataAsset* Status = StatusPtr;
			if (!Status || Status->SlotIndex >= 8 || (Bits & (1 << Status->SlotIndex)) == 0)
			{
				continue;
			}
			// §7-5: multipliers multiply, booleans OR.
			Resolved.MoveSpeedMultiplier *= Status->MoveSpeedMultiplier;
			Resolved.AttackIntervalMultiplier *= Status->AttackIntervalMultiplier;
			Resolved.IncomingDamageMultiplier *= Status->IncomingDamageMultiplier;
			Resolved.bDisableAttack |= Status->bDisableAttack;
			Resolved.bDisableMovement |= Status->bDisableMovement;
		}
		return Resolved;
	}
}
