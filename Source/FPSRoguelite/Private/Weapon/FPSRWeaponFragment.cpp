// Copyright Epic Games, Inc. All Rights Reserved.

#include "Weapon/FPSRWeaponFragment.h"
#include "Combat/FPSRCombatStatics.h"
#include "Weapon/FPSRWeaponInstance.h"

namespace FPSRWeaponHooks
{
	void NotifyFire(const FFPSRFireContext& Context)
	{
		if (!Context.Instance) { return; }
		for (const TObjectPtr<UFPSRWeaponFragment>& Frag : Context.Instance->GetActiveFragments())
		{
			if (Frag) { Frag->OnFire(Context); }
		}
	}

	void NotifyMiss(const FFPSRFireContext& Context)
	{
		if (!Context.Instance) { return; }
		for (const TObjectPtr<UFPSRWeaponFragment>& Frag : Context.Instance->GetActiveFragments())
		{
			if (Frag) { Frag->OnMiss(Context); }
		}
	}

	void NotifyKill(const FFPSRFireContext& Context, AActor* KilledActor)
	{
		if (!Context.Instance || !KilledActor) { return; }
		for (const TObjectPtr<UFPSRWeaponFragment>& Frag : Context.Instance->GetActiveFragments())
		{
			if (Frag) { Frag->OnKill(Context, KilledActor); }
		}
	}

	void NotifyAim(const FFPSRFireContext& Context, bool bAiming)
	{
		if (!Context.Instance) { return; }
		for (const TObjectPtr<UFPSRWeaponFragment>& Frag : Context.Instance->GetActiveFragments())
		{
			if (Frag) { Frag->OnAim(Context, bAiming); }
		}
	}
}

void UFPSRFragment_ExplosiveRounds::OnImpact(const FFPSRFireContext& Context, const FVector& ImpactPoint, bool bAllowSelf) const
{
	// Server-authoritative: spawn a small radial explosion at the bullet's impact. No crit on the splash (the
	// pellet already rolled its own crit); self/friendly damage and knockback follow the shared explosion rules.
	if (!Context.bAuthority || !Context.World || AOERadius <= 0.0f)
	{
		return;
	}

	const FPSRCombat::FKilledEnemies Killed = FPSRCombat::ApplyExplosion(Context.World, ImpactPoint, AOERadius, AOEDamage,
		/*CritChance*/ 0.0f, /*CritMultiplier*/ 1.0f, Context.Avatar, bAllowSelf, KnockbackStrength);

	// A splash kill (e.g. rifle + ExplosiveRounds) fires OnKill too — Context here is the live firing context, so the
	// bridge reaches this weapon's fragments directly (e.g. a reload-on-kill fragment on the same weapon).
	for (AActor* KilledActor : Killed)
	{
		FPSRWeaponHooks::NotifyKill(Context, KilledActor);
	}
}
