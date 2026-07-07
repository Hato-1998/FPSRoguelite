// Copyright Epic Games, Inc. All Rights Reserved.

#include "Validation/FPSRWeaponValidator.h"
#include "Weapon/FPSRWeaponDataAsset.h"
#include "Card/FPSRCardDataAsset.h"
#include "Card/FPSRCardTypes.h"
#include "DataEditor/FPSRDataEditorHelpers.h"
#include "Misc/DataValidation.h"

#define LOCTEXT_NAMESPACE "FPSRWeaponValidator"

bool UFPSRWeaponValidator::CanValidateAsset_Implementation(const FAssetData& InAssetData, UObject* InAsset, FDataValidationContext& InContext) const
{
	return InAsset != nullptr && InAsset->IsA<UFPSRWeaponDataAsset>();
}

EDataValidationResult UFPSRWeaponValidator::ValidateLoadedAsset_Implementation(const FAssetData& InAssetData, UObject* InAsset, FDataValidationContext& Context)
{
	const UFPSRWeaponDataAsset* Weapon = Cast<UFPSRWeaponDataAsset>(InAsset);
	if (Weapon == nullptr)
	{
		AssetPasses(InAsset);
		return EDataValidationResult::Valid;
	}

	EDataValidationResult Result = EDataValidationResult::Valid;

	// Routing (H2 = hard error): a card wired into a weapon array whose route it isn't eligible for would be a
	// silent no-op (or a semantically wrong offer) at draw time. Null entries are skipped — the weapon's own
	// IsDataValid already reports those.
	auto CheckRouting = [&](const TArray<TObjectPtr<UFPSRCardDataAsset>>& List, const TCHAR* ArrayName, EFPSRCardRoute Route)
	{
		for (const TObjectPtr<UFPSRCardDataAsset>& Card : List)
		{
			if (!Card)
			{
				continue;
			}
			FText Reason;
			if (FFPSRDataEditorHelpers::CheckCardRoute(Card, Route, Reason) == EFPSRWiringVerdict::Blocked)
			{
				const FText CardLabel = Card->CardId.IsNone() ? FText::FromString(Card->GetName()) : FText::FromName(Card->CardId);
				Context.AddError(FText::Format(
					LOCTEXT("WeaponCardRoutingBlocked", "카드 '{0}' 가 무기 배열 '{1}'(라우트 {2})에 부적격 배선됨: {3}"),
					CardLabel, FText::FromString(ArrayName), FFPSRDataEditorHelpers::GetRouteDisplayText(Route), Reason));
				Result = EDataValidationResult::Invalid;
			}
		}
	};
	CheckRouting(Weapon->WeaponCards, TEXT("WeaponCards"), EFPSRCardRoute::LevelUpWeapon);
	CheckRouting(Weapon->UnlockableFeatures, TEXT("UnlockableFeatures"), EFPSRCardRoute::MissionClearWeaponFeature);

	if (Result == EDataValidationResult::Valid)
	{
		AssetPasses(InAsset);
	}
	return Result;
}

#undef LOCTEXT_NAMESPACE
