// Copyright Epic Games, Inc. All Rights Reserved.

#include "Weapon/FPSRWeaponDataAsset.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "AbilitySystem/Abilities/FPSRGA_WeaponFire_Projectile.h"

#define LOCTEXT_NAMESPACE "FPSRWeaponDataAsset"

namespace
{
	/** Does this mesh expose the named attachment point? Runtime attach (UPrimitiveComponent::DoesSocketExist) treats
	 *  BONES and SOCKETS alike, and this pack names many attach bones "SOCKET_*", so we accept either. NAME_None is the
	 *  component root (always valid). A null/unloaded mesh returns true so validation never false-positives on a soft
	 *  ref that simply isn't resolvable here. */
	bool SkeletalMeshHasAttachPoint(const USkeletalMesh* Mesh, FName Name)
	{
		if (Name.IsNone() || Mesh == nullptr)
		{
			return true;
		}
		if (Mesh->FindSocket(Name) != nullptr)
		{
			return true;
		}
		return Mesh->GetRefSkeleton().FindBoneIndex(Name) != INDEX_NONE;
	}
}

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

	// ChargeLaser with ChargeTime 0 fires the full-power beam instantly on click (no warm-up sequence).
	if (Archetype == EFPSRWeaponArchetype::ChargeLaser && BaseStats.ChargeTime <= 0.0f)
	{
		Context.AddWarning(LOCTEXT("ChargeLaserNoChargeTime", "ChargeLaser has ChargeTime <= 0 — a click fires the full-power beam instantly with no warm-up. Set ChargeTime > 0 for a charge-up sequence."));
	}

	// Ranged weapons consume ammo; a 0 magazine can never fire past the empty-mag gate.
	if (Archetype != EFPSRWeaponArchetype::Melee && BaseStats.MagSize <= 0)
	{
		Context.AddWarning(LOCTEXT("NoMagSize", "Ranged weapon has MagSize <= 0 — it will be permanently empty and unable to fire. Set MagSize > 0."));
	}

	// --- "Dead value" sanity (P0 data-validation seam): archetype-gated stat fields that are structurally required
	//     for that archetype to do ANYTHING. These are ERRORS (not warnings) — unlike the softer archetype-mismatch
	//     warnings above (AOE radius / charge time / mag size, which degrade to a lesser-but-functional weapon), a
	//     zero here means the weapon cannot function as its declared archetype at all. ---
	if (BaseStats.Damage <= 0.0f)
	{
		Context.AddError(LOCTEXT("NoDamage", "Weapon has Damage <= 0 — every hit will deal zero damage. Set Damage > 0."));
		Result = EDataValidationResult::Invalid;
	}
	if (BaseStats.FireRate <= 0.0f)
	{
		Context.AddError(LOCTEXT("NoFireRate", "Weapon has FireRate <= 0 — the fire loop's rate-of-fire timer would never re-arm. Set FireRate > 0."));
		Result = EDataValidationResult::Invalid;
	}
	if (Archetype != EFPSRWeaponArchetype::Melee)
	{
		if (BaseStats.MagSize <= 0)
		{
			Context.AddError(LOCTEXT("MagSizeNotPositive", "Non-melee weapon has MagSize <= 0 — it can never load a round. Set MagSize > 0 (or switch Archetype to Melee)."));
			Result = EDataValidationResult::Invalid;
		}
		if (BaseStats.ReloadTime <= 0.0f)
		{
			Context.AddError(LOCTEXT("ReloadTimeNotPositive", "Non-melee weapon has ReloadTime <= 0 — the reload timer would never complete. Set ReloadTime > 0."));
			Result = EDataValidationResult::Invalid;
		}
	}
	if (Archetype == EFPSRWeaponArchetype::Shotgun && BaseStats.PelletCount <= 0)
	{
		Context.AddError(LOCTEXT("PelletCountNotPositive", "Shotgun has PelletCount <= 0 — firing would spawn no pellets at all. Set PelletCount > 0."));
		Result = EDataValidationResult::Invalid;
	}
	// Projectile weapons are identified by their FireAbility (the Projectile GA), NOT the archetype — a FullAuto rifle
	// or a Shotgun can now fire projectiles. Validate the projectile stats whenever the weapon uses the Projectile GA.
	const bool bIsProjectileWeapon = FireAbility && FireAbility->IsChildOf(UFPSRGA_WeaponFire_Projectile::StaticClass());
	if (bIsProjectileWeapon)
	{
		if (BaseStats.ProjectileSpeed <= 0.0f)
		{
			Context.AddError(LOCTEXT("ProjectileSpeedNotPositive", "Projectile weapon has ProjectileSpeed <= 0 — the projectile would never travel toward its target. Set ProjectileSpeed > 0."));
			Result = EDataValidationResult::Invalid;
		}
		if (BaseStats.ProjectileLifetime <= 0.0f)
		{
			Context.AddError(LOCTEXT("ProjectileLifetimeNotPositive", "Projectile weapon has ProjectileLifetime <= 0 — the projectile would auto-release before it can ever hit anything. Set ProjectileLifetime > 0."));
			Result = EDataValidationResult::Invalid;
		}
		// Replicated-projectile budget guard (Game.MD §5, cap 64 with FIFO eviction). Peak in-flight per player for
		// a sustained-fire projectile weapon ≈ FireRate × Lifetime × PelletCount. Warn when that alone could crowd the
		// shared player cap (×4 players): a fast-firing weapon MUST use a SHORT lifetime + high speed so its rounds
		// clear quickly. (The default Lifetime 5s is fine for a slow launcher but far too long for a rifle.)
		const int32 Pellets = FMath::Max(1, BaseStats.PelletCount);
		const float PeakInFlight = BaseStats.FireRate * FMath::Max(0.0f, BaseStats.ProjectileLifetime) * Pellets;
		if (PeakInFlight > 12.0f)
		{
			Context.AddWarning(FText::Format(LOCTEXT("ProjectileBudgetHigh",
				"Projectile weapon's estimated peak in-flight rounds (FireRate {0} × Lifetime {1}s × Pellets {2} ≈ {3}) is high — with 4 players this can hit the ≤64 replicated-projectile cap and FIFO-evict teammates' projectiles (Game.MD §5). Shorten ProjectileLifetime and/or raise ProjectileSpeed so rounds clear fast."),
				FText::AsNumber(BaseStats.FireRate), FText::AsNumber(BaseStats.ProjectileLifetime),
				FText::AsNumber(Pellets), FText::AsNumber(FMath::RoundToInt(PeakInFlight))));
		}
	}
	if (Archetype == EFPSRWeaponArchetype::Melee)
	{
		if (BaseStats.MeleeRadius <= 0.0f)
		{
			Context.AddError(LOCTEXT("MeleeRadiusNotPositive", "Melee weapon has MeleeRadius <= 0 — the attack's hit sweep would find nothing. Set MeleeRadius > 0."));
			Result = EDataValidationResult::Invalid;
		}
		if (BaseStats.MeleeAttackDelay <= 0.0f)
		{
			Context.AddError(LOCTEXT("MeleeAttackDelayNotPositive", "Melee weapon has MeleeAttackDelay <= 0 — rapid clicks would never be rate-limited (or the attack loop may never re-arm, depending on the ability). Set MeleeAttackDelay > 0."));
			Result = EDataValidationResult::Invalid;
		}
	}

	// --- Socket / attach-point validation. A referenced socket that doesn't exist on the target mesh (a typo such as a
	//     space instead of '_', or the wrong socket) makes the part / muzzle flash silently fall back to the mesh
	//     origin — the exact class of bug that is invisible until you look at the assembled weapon in-game. Warnings
	//     (not errors): the game still runs, but the visual is wrong. ---
	USkeletalMesh* SkelWeapon = WeaponMesh1P.IsNull() ? nullptr : WeaponMesh1P.LoadSynchronous();

	// Modular cosmetic parts attach to the skeletal weapon mesh (WeaponMesh1P) at their Socket.
	for (const FFPSRWeaponPartAttachment& Part : WeaponParts1P)
	{
		if (Part.Part.IsNull() || Part.Socket.IsNone())
		{
			continue;
		}
		if (SkelWeapon && !SkeletalMeshHasAttachPoint(SkelWeapon, Part.Socket))
		{
			Context.AddWarning(FText::Format(LOCTEXT("PartSocketMissing",
				"WeaponParts1P part '{0}' references socket '{1}' that does not exist on WeaponMesh1P — it will attach at the mesh origin. Check for a typo (e.g. a space instead of '_')."),
				FText::FromString(Part.Part.GetAssetName()), FText::FromName(Part.Socket)));
		}
	}

	// The muzzle socket may live on the weapon mesh OR on a modular part (barrel / forestock carry SOCKET_Muzzle in
	// this pack). Only validate for firearms (a skeletal weapon mesh or at least one part is present).
	if (!MuzzleSocket.IsNone() && (SkelWeapon != nullptr || WeaponParts1P.Num() > 0))
	{
		bool bMuzzleFound = SkeletalMeshHasAttachPoint(SkelWeapon, MuzzleSocket) && SkelWeapon != nullptr;
		for (const FFPSRWeaponPartAttachment& Part : WeaponParts1P)
		{
			if (bMuzzleFound)
			{
				break;
			}
			if (Part.Part.IsNull())
			{
				continue;
			}
			if (const UStaticMesh* PartMesh = Part.Part.LoadSynchronous())
			{
				bMuzzleFound = PartMesh->FindSocket(MuzzleSocket) != nullptr;
			}
		}
		if (!bMuzzleFound)
		{
			Context.AddWarning(FText::Format(LOCTEXT("MuzzleSocketMissing",
				"MuzzleSocket '{0}' is not found on WeaponMesh1P or any modular part — the muzzle flash will spawn at the mesh origin. Put the muzzle socket on the barrel/forestock part (or the weapon mesh) and check for typos."),
				FText::FromName(MuzzleSocket)));
		}
	}

	// The procedural-ADS AimSocket may live on the weapon receiver OR on a sight part (iron sight / optic), same as the
	// muzzle. A typo makes DoesSocketExist fail at runtime (ADS silently stays at hip). Validate across receiver + parts.
	if (BaseStats.bHasADS && !AimSocket.IsNone() && (SkelWeapon != nullptr || WeaponParts1P.Num() > 0))
	{
		bool bAimFound = SkelWeapon != nullptr && SkeletalMeshHasAttachPoint(SkelWeapon, AimSocket);
		for (const FFPSRWeaponPartAttachment& Part : WeaponParts1P)
		{
			if (bAimFound)
			{
				break;
			}
			if (Part.Part.IsNull())
			{
				continue;
			}
			if (const UStaticMesh* PartMesh = Part.Part.LoadSynchronous())
			{
				bAimFound = PartMesh->FindSocket(AimSocket) != nullptr;
			}
		}
		if (!bAimFound)
		{
			Context.AddWarning(FText::Format(LOCTEXT("AimSocketMissing",
				"AimSocket '{0}' is not found on WeaponMesh1P or any modular part — procedural ADS will not align (it stays at hip). Put the aim socket on the sight part (iron sight / optic) or the weapon mesh (+X forward, +Z up), and check for typos (e.g. a space instead of '_')."),
				FText::FromName(AimSocket)));
		}
	}

	// The fire-part recoil bone (bolt / charging handle driven by UFPSRWeaponAnimInstance) must exist on the weapon mesh
	// — a typo means the AnimBP's ModifyBone targets nothing and the part never kicks. Only validate when a bone is set
	// (None = no moving part = no-op).
	if (!FirePartRecoilBone.IsNone() && SkelWeapon != nullptr && !SkeletalMeshHasAttachPoint(SkelWeapon, FirePartRecoilBone))
	{
		Context.AddWarning(FText::Format(LOCTEXT("FirePartRecoilBoneMissing",
			"FirePartRecoilBone '{0}' does not exist on WeaponMesh1P — the fire-part recoil (bolt / charging handle) will drive nothing. Check the bone name for typos."),
			FText::FromName(FirePartRecoilBone)));
	}

	return Result;
}

#undef LOCTEXT_NAMESPACE
#endif // WITH_EDITOR
