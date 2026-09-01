// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/FPSRVitals.h"
#include "Math/UnrealMathUtility.h"

namespace FPSRVitals
{
	FResult ApplyDamage(FPool& InOutPool, float Incoming, const FFPSRDamageSpec& Spec, const FMitigation& Mit)
	{
		FResult Result;
		if (Incoming <= 0.0f)
		{
			return Result;
		}

		const float ArmorKeep = 1.0f - FMath::Clamp(Mit.DirectionalArmorDR, 0.0f, 1.0f);
		// Invulnerability floor (unvariant V1 — VIT1 §5-1): no mitigation STACK may zero out a hit. MaxTotalReduction
		// is clamped to <= 0.99 by the profile's UPROPERTY + IsDataValid, so MinKeep is always > 0.
		const float MinKeep = 1.0f - FMath::Clamp(Mit.MaxTotalReduction, 0.0f, 1.0f);

		// The floor does NOT apply when Spec.ShieldDamageMultiplier == 0 — that is an authored "this hit ignores
		// shields entirely" weapon (SDM 0), not a mitigation stack that happens to reach 100%. The floor exists to
		// stop unintended overlap from reaching invulnerability, not to close a deliberately-opened bypass.
		const float ShieldKeep = (Spec.ShieldDamageMultiplier == 0.0f)
			? 0.0f
			: FMath::Max(Spec.ShieldDamageMultiplier * Mit.ShieldDefense * ArmorKeep, MinKeep);
		const float HealthKeep = FMath::Max(Mit.HealthDefense * ArmorKeep, MinKeep);

		const float WantShield = Incoming * ShieldKeep;
		Result.ShieldSpent = FMath::Min(InOutPool.Shield, WantShield);
		// Fraction of the ORIGINAL hit the shield actually absorbed — 0 when WantShield is 0 (SDM 0, or no shield
		// mitigation at all) so Overflow below correctly carries the WHOLE hit to health instead of dividing by zero.
		const float Consumed = (WantShield > 0.0f) ? (Result.ShieldSpent / WantShield) : 0.0f;
		// The carry-over (VIT1 §2 goal 5 / §5-1 V3): grows with how much of the hit the shield COULDN'T absorb
		// (already-empty shield, or a hit bigger than what's left), so a shield never "gates" the excess away.
		const float Overflow = Incoming * (1.0f - Consumed);
		Result.HealthSpent = FMath::Min(InOutPool.Health, Overflow * HealthKeep);

		const bool bShieldWasUp = InOutPool.Shield > 0.0f;
		InOutPool.Shield -= Result.ShieldSpent;
		InOutPool.Health -= Result.HealthSpent;

		Result.bShieldBroke = bShieldWasUp && InOutPool.Shield <= 0.0f;
		Result.bLethal = InOutPool.Health <= 0.0f;
		return Result;
	}

	float ComputeRegeneratedShield(float ShieldAtLastDamage, float MaxShield, float ElapsedSinceDamage,
		float RegenPerSecond, float PartialDelaySeconds, float BrokenDelaySeconds)
	{
		if (MaxShield <= 0.0f)
		{
			return 0.0f;
		}

		// Halo-style dual delay (user decision 2026-09-01): a hit that took the shield all the way to 0 earns the
		// LONGER "broken" delay; a hit that only chipped it earns the shorter "partial" delay. Reading the anchor
		// value alone (no separate "did this hit break it" flag) is what keeps this formula idempotent — the caller
		// only ever needs to remember the post-hit shield value and the time of that hit.
		const float Delay = (ShieldAtLastDamage <= 0.0f) ? BrokenDelaySeconds : PartialDelaySeconds;
		if (ElapsedSinceDamage < Delay)
		{
			return FMath::Clamp(ShieldAtLastDamage, 0.0f, MaxShield);
		}

		const float RegenElapsed = ElapsedSinceDamage - Delay;
		return FMath::Clamp(ShieldAtLastDamage + RegenPerSecond * RegenElapsed, 0.0f, MaxShield);
	}
}
