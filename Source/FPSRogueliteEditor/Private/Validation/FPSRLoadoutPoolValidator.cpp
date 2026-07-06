// Copyright Epic Games, Inc. All Rights Reserved.

#include "Validation/FPSRLoadoutPoolValidator.h"
#include "Weapon/FPSRLoadoutPoolDataAsset.h"
#include "Weapon/FPSRWeaponDataAsset.h"
#include "Misc/DataValidation.h"

#define LOCTEXT_NAMESPACE "FPSRLoadoutPoolValidator"

bool UFPSRLoadoutPoolValidator::CanValidateAsset_Implementation(const FAssetData& InAssetData, UObject* InAsset, FDataValidationContext& InContext) const
{
	return InAsset != nullptr && InAsset->IsA<UFPSRLoadoutPoolDataAsset>();
}

EDataValidationResult UFPSRLoadoutPoolValidator::ValidateLoadedAsset_Implementation(const FAssetData& InAssetData, UObject* InAsset, FDataValidationContext& Context)
{
	const UFPSRLoadoutPoolDataAsset* Pool = Cast<UFPSRLoadoutPoolDataAsset>(InAsset);
	if (Pool == nullptr)
	{
		AssetPasses(InAsset);
		return EDataValidationResult::Valid;
	}

	EDataValidationResult Result = EDataValidationResult::Valid;

	if (Pool->SelectableWeapons.Num() == 0)
	{
		AssetFails(InAsset, LOCTEXT("EmptyLoadout", "LoadoutPool has no SelectableWeapons — the lobby loadout pick list would be empty."));
		Result = EDataValidationResult::Invalid;
	}

	TSet<const UFPSRWeaponDataAsset*> Seen;
	for (int32 Index = 0; Index < Pool->SelectableWeapons.Num(); ++Index)
	{
		const UFPSRWeaponDataAsset* Weapon = Pool->SelectableWeapons[Index];
		if (Weapon == nullptr)
		{
			AssetFails(InAsset, FText::Format(LOCTEXT("NullEntry", "SelectableWeapons[{0}] is null — the lobby list has an empty slot."), FText::AsNumber(Index)));
			Result = EDataValidationResult::Invalid;
			continue;
		}
		if (Seen.Contains(Weapon))
		{
			AssetFails(InAsset, FText::Format(LOCTEXT("DupEntry", "SelectableWeapons[{0}] '{1}' is a duplicate entry."), FText::AsNumber(Index), FText::FromString(Weapon->GetName())));
			Result = EDataValidationResult::Invalid;
		}
		Seen.Add(Weapon);
	}

	if (Result == EDataValidationResult::Valid)
	{
		AssetPasses(InAsset);
	}
	return Result;
}

#undef LOCTEXT_NAMESPACE
