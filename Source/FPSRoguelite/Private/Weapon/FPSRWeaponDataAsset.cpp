// Copyright Epic Games, Inc. All Rights Reserved.

#include "Weapon/FPSRWeaponDataAsset.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"

#define LOCTEXT_NAMESPACE "FPSRWeaponDataAsset"

EDataValidationResult UFPSRWeaponDataAsset::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	// Every weapon fires by activating its FireAbility — a missing one means the weapon does nothing.
	if (!FireAbility)
	{
		Context.AddError(LOCTEXT("NoFireAbility", "Weapon has no FireAbility — it will never fire. Assign the matching GA (Hitscan / Projectile / ChargeLaser / Melee)."));
		Result = EDataValidationResult::Invalid;
	}

	const EFPSRWeaponArchetype Archetype = BaseStats.Archetype;

	// AOE archetype with no blast radius silently degrades to a single-target projectile (no explosion).
	if (Archetype == EFPSRWeaponArchetype::AOE && BaseStats.AOERadius <= 0.0f)
	{
		Context.AddWarning(LOCTEXT("AOENoRadius", "AOE weapon has AOERadius <= 0 — it will only hit a single target (no explosion). Set AOERadius > 0 for a blast."));
	}

	// ChargeLaser with ChargeTime 0 reaches full charge instantly (the charge alpha is forced to 1).
	if (Archetype == EFPSRWeaponArchetype::ChargeLaser && BaseStats.ChargeTime <= 0.0f)
	{
		Context.AddWarning(LOCTEXT("ChargeLaserNoChargeTime", "ChargeLaser has ChargeTime <= 0 — every shot is an instant full charge. Set ChargeTime > 0 for a hold-to-charge ramp."));
	}

	// Ranged weapons consume ammo; a 0 magazine can never fire past the empty-mag gate.
	if (Archetype != EFPSRWeaponArchetype::Melee && BaseStats.MagSize <= 0)
	{
		Context.AddWarning(LOCTEXT("NoMagSize", "Ranged weapon has MagSize <= 0 — it will be permanently empty and unable to fire. Set MagSize > 0."));
	}

	return Result;
}

#undef LOCTEXT_NAMESPACE
#endif // WITH_EDITOR
