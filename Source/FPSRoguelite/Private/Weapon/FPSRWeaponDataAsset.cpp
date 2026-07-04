// Copyright Epic Games, Inc. All Rights Reserved.

#include "Weapon/FPSRWeaponDataAsset.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"

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

	return Result;
}

#undef LOCTEXT_NAMESPACE
#endif // WITH_EDITOR
