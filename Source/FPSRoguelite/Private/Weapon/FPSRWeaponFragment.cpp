// Copyright Epic Games, Inc. All Rights Reserved.

#include "Weapon/FPSRWeaponFragment.h"
#include "Combat/FPSRCombatStatics.h"

void UFPSRFragment_ExplosiveRounds::OnImpact(const FFPSRFireContext& Context, const FVector& ImpactPoint, bool bAllowSelf) const
{
	// Server-authoritative: spawn a small radial explosion at the bullet's impact. No crit on the splash (the
	// pellet already rolled its own crit); self/friendly damage and knockback follow the shared explosion rules.
	if (!Context.bAuthority || !Context.World || AOERadius <= 0.0f)
	{
		return;
	}

	FPSRCombat::ApplyExplosion(Context.World, ImpactPoint, AOERadius, AOEDamage,
		/*CritChance*/ 0.0f, /*CritMultiplier*/ 1.0f, Context.Avatar, bAllowSelf, KnockbackStrength);
}
