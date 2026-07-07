// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataEditor/FPSRDataEditorHelpers.h"

#include "Card/FPSRCardDataAsset.h"
#include "Card/FPSRCardEffect.h"
#include "Card/FPSRCardPoolDataAsset.h"
#include "Weapon/FPSRWeaponDataAsset.h"
#include "Weapon/FPSRLoadoutPoolDataAsset.h"
#include "Run/FPSRRunScheduleDataAsset.h"
#include "Run/Mission/FPSRMissionDataAsset.h"

#include "ScopedTransaction.h"
#include "FileHelpers.h"
#include "UObject/UnrealType.h"
#include "UObject/Package.h"

#define LOCTEXT_NAMESPACE "FPSRDataEditorHelpers"

namespace
{
	/** Shared membership-array add: Modify() the owner, AddUnique(Element), fire PostEditChangeProperty for the
	 *  named array property, MarkPackageDirty. Returns false (no-op) on null owner/element or if already present
	 *  (AddUnique made no change) so callers can distinguish "nothing happened" from a real edit. */
	template <typename TOwner, typename TElement>
	bool AddUniqueToArray(TOwner* Owner, TArray<TObjectPtr<TElement>>& Array, TElement* Element, FName ArrayPropertyName, const FText& TransactionText)
	{
		if (!Owner || !Element)
		{
			return false;
		}
		if (Array.Contains(Element))
		{
			return false; // already a member — AddUnique would be a silent no-op; report "no change" explicitly
		}

		const FScopedTransaction Transaction(TransactionText);
		Owner->Modify();
		Array.AddUnique(Element);

		FProperty* Prop = FindFProperty<FProperty>(TOwner::StaticClass(), ArrayPropertyName);
		FPropertyChangedEvent Evt(Prop, EPropertyChangeType::ArrayAdd);
		Owner->PostEditChangeProperty(Evt);
		Owner->MarkPackageDirty();
		return true;
	}

	/** Shared membership-array remove: Modify() the owner, Remove(Element), fire PostEditChangeProperty for the
	 *  named array property, MarkPackageDirty. Returns false (no-op) on null owner/element or if not present. */
	template <typename TOwner, typename TElement>
	bool RemoveFromArray(TOwner* Owner, TArray<TObjectPtr<TElement>>& Array, TElement* Element, FName ArrayPropertyName, const FText& TransactionText)
	{
		if (!Owner || !Element)
		{
			return false;
		}
		if (!Array.Contains(Element))
		{
			return false;
		}

		const FScopedTransaction Transaction(TransactionText);
		Owner->Modify();
		Array.Remove(Element);

		FProperty* Prop = FindFProperty<FProperty>(TOwner::StaticClass(), ArrayPropertyName);
		FPropertyChangedEvent Evt(Prop, EPropertyChangeType::ArrayRemove);
		Owner->PostEditChangeProperty(Evt);
		Owner->MarkPackageDirty();
		return true;
	}
}

TArray<EFPSRCardRoute> FFPSRDataEditorHelpers::GetCardEligibleRoutes(const UFPSRCardDataAsset* Card)
{
	TArray<EFPSRCardRoute> Eligible;
	if (!Card || Card->Effects.Num() == 0)
	{
		return Eligible; // no effects => nothing eligible (a card with no effects has no route to draw from)
	}

	// Intersection across effects: seed from the first effect's eligible set, then remove anything the remaining
	// effects don't also permit. Pure iteration over the polymorphic Effects array — no effect-type switch, so a
	// new UFPSRCardEffect subclass slots in for free as long as it overrides GetEditorEligibleRoutes.
	bool bSeeded = false;
	for (const TObjectPtr<UFPSRCardEffect>& Effect : Card->Effects)
	{
		if (!Effect)
		{
			continue;
		}
		const TArray<EFPSRCardRoute> ThisEffectRoutes = Effect->GetEditorEligibleRoutes();
		if (!bSeeded)
		{
			Eligible = ThisEffectRoutes;
			bSeeded = true;
			continue;
		}
		Eligible.RemoveAll([&ThisEffectRoutes](EFPSRCardRoute Route) { return !ThisEffectRoutes.Contains(Route); });
		if (Eligible.Num() == 0)
		{
			break; // already empty — no route survives an intersection with anything further
		}
	}
	return Eligible;
}

EFPSRWiringVerdict FFPSRDataEditorHelpers::CheckCardRoute(const UFPSRCardDataAsset* Card, EFPSRCardRoute Route, FText& OutReason)
{
	OutReason = FText::GetEmpty();
	if (!Card)
	{
		OutReason = LOCTEXT("CheckRoute_NoCard", "No card selected.");
		return EFPSRWiringVerdict::Blocked;
	}

	const TArray<EFPSRCardRoute> Eligible = GetCardEligibleRoutes(Card);
	if (!Eligible.Contains(Route))
	{
		OutReason = FText::Format(
			LOCTEXT("CheckRoute_Blocked", "'{0}' is not an eligible route for this card's effects — placing it here would be a silent no-op (or a semantically wrong offer) at draw time."),
			GetRouteDisplayText(Route));
		return EFPSRWiringVerdict::Blocked;
	}

	// H2-ambiguous case (UCardEffect_WeaponBehavior): both LevelUpWeapon and MissionClearWeaponFeature are
	// technically eligible, but the project convention is mission-clear for behavior fragments — warn (not block)
	// if a designer explicitly wires one into the level-up weapon pool instead.
	if (Route == EFPSRCardRoute::LevelUpWeapon && Eligible.Contains(EFPSRCardRoute::MissionClearWeaponFeature))
	{
		OutReason = LOCTEXT("CheckRoute_WarnBehaviorLevelUp", "This card is eligible for Level-Up: Weapon Card, but behavior-fragment cards are conventionally routed through Mission-Clear: Weapon Feature instead. Continue only if this is intentional.");
		return EFPSRWiringVerdict::Warn;
	}

	return EFPSRWiringVerdict::Allowed;
}

bool FFPSRDataEditorHelpers::AddCardToPool(UFPSRCardPoolDataAsset* Pool, UFPSRCardDataAsset* Card, bool bUnlockArray)
{
	if (!Pool)
	{
		return false;
	}
	return bUnlockArray
		? AddUniqueToArray(Pool, Pool->WeaponUnlockCards, Card, GET_MEMBER_NAME_CHECKED(UFPSRCardPoolDataAsset, WeaponUnlockCards), LOCTEXT("Transaction_AddCardToPoolUnlock", "Add Card to Pool (Weapon Unlock)"))
		: AddUniqueToArray(Pool, Pool->Cards, Card, GET_MEMBER_NAME_CHECKED(UFPSRCardPoolDataAsset, Cards), LOCTEXT("Transaction_AddCardToPool", "Add Card to Pool"));
}

bool FFPSRDataEditorHelpers::RemoveCardFromPool(UFPSRCardPoolDataAsset* Pool, UFPSRCardDataAsset* Card, bool bUnlockArray)
{
	if (!Pool)
	{
		return false;
	}
	return bUnlockArray
		? RemoveFromArray(Pool, Pool->WeaponUnlockCards, Card, GET_MEMBER_NAME_CHECKED(UFPSRCardPoolDataAsset, WeaponUnlockCards), LOCTEXT("Transaction_RemoveCardFromPoolUnlock", "Remove Card from Pool (Weapon Unlock)"))
		: RemoveFromArray(Pool, Pool->Cards, Card, GET_MEMBER_NAME_CHECKED(UFPSRCardPoolDataAsset, Cards), LOCTEXT("Transaction_RemoveCardFromPool", "Remove Card from Pool"));
}

bool FFPSRDataEditorHelpers::AddCardToWeapon(UFPSRWeaponDataAsset* Weapon, UFPSRCardDataAsset* Card, bool bUnlockableFeatures)
{
	if (!Weapon)
	{
		return false;
	}
	return bUnlockableFeatures
		? AddUniqueToArray(Weapon, Weapon->UnlockableFeatures, Card, GET_MEMBER_NAME_CHECKED(UFPSRWeaponDataAsset, UnlockableFeatures), LOCTEXT("Transaction_AddCardToWeaponFeature", "Add Card to Weapon (Unlockable Feature)"))
		: AddUniqueToArray(Weapon, Weapon->WeaponCards, Card, GET_MEMBER_NAME_CHECKED(UFPSRWeaponDataAsset, WeaponCards), LOCTEXT("Transaction_AddCardToWeapon", "Add Card to Weapon (Level-Up)"));
}

bool FFPSRDataEditorHelpers::RemoveCardFromWeapon(UFPSRWeaponDataAsset* Weapon, UFPSRCardDataAsset* Card, bool bUnlockableFeatures)
{
	if (!Weapon)
	{
		return false;
	}
	return bUnlockableFeatures
		? RemoveFromArray(Weapon, Weapon->UnlockableFeatures, Card, GET_MEMBER_NAME_CHECKED(UFPSRWeaponDataAsset, UnlockableFeatures), LOCTEXT("Transaction_RemoveCardFromWeaponFeature", "Remove Card from Weapon (Unlockable Feature)"))
		: RemoveFromArray(Weapon, Weapon->WeaponCards, Card, GET_MEMBER_NAME_CHECKED(UFPSRWeaponDataAsset, WeaponCards), LOCTEXT("Transaction_RemoveCardFromWeapon", "Remove Card from Weapon (Level-Up)"));
}

bool FFPSRDataEditorHelpers::AddWeaponToLoadout(UFPSRLoadoutPoolDataAsset* Loadout, UFPSRWeaponDataAsset* Weapon)
{
	if (!Loadout)
	{
		return false;
	}
	return AddUniqueToArray(Loadout, Loadout->SelectableWeapons, Weapon, GET_MEMBER_NAME_CHECKED(UFPSRLoadoutPoolDataAsset, SelectableWeapons), LOCTEXT("Transaction_AddWeaponToLoadout", "Add Weapon to Loadout"));
}

bool FFPSRDataEditorHelpers::RemoveWeaponFromLoadout(UFPSRLoadoutPoolDataAsset* Loadout, UFPSRWeaponDataAsset* Weapon)
{
	if (!Loadout)
	{
		return false;
	}
	return RemoveFromArray(Loadout, Loadout->SelectableWeapons, Weapon, GET_MEMBER_NAME_CHECKED(UFPSRLoadoutPoolDataAsset, SelectableWeapons), LOCTEXT("Transaction_RemoveWeaponFromLoadout", "Remove Weapon from Loadout"));
}

bool FFPSRDataEditorHelpers::AddMissionToScheduleWindow(UFPSRRunScheduleDataAsset* Schedule, int32 WindowIndex, UFPSRMissionDataAsset* Mission)
{
	if (!Schedule || !Mission || !Schedule->MissionWindows.IsValidIndex(WindowIndex))
	{
		return false;
	}
	TArray<TObjectPtr<UFPSRMissionDataAsset>>& Pool = Schedule->MissionWindows[WindowIndex].MissionPool;
	if (Pool.Contains(Mission))
	{
		return false;
	}

	const FScopedTransaction Transaction(LOCTEXT("Transaction_AddMissionToWindow", "Add Mission to Schedule Window"));
	Schedule->Modify();
	Pool.AddUnique(Mission);

	// MissionWindows is a plain (non-Instanced) UPROPERTY array of structs; the mutated field is the struct's own
	// MissionPool member, but the change notification is keyed on the outer MissionWindows property (there's no
	// nested-struct-member FProperty to point at here without a full property-node walk, and the outer property is
	// sufficient for PostEditChangeProperty's default handling — the schedule has no per-window derived state to
	// recompute, unlike UFPSRCardDataAsset::RefreshOfferRarities).
	FProperty* Prop = FindFProperty<FProperty>(UFPSRRunScheduleDataAsset::StaticClass(), GET_MEMBER_NAME_CHECKED(UFPSRRunScheduleDataAsset, MissionWindows));
	FPropertyChangedEvent Evt(Prop, EPropertyChangeType::ArrayAdd);
	Schedule->PostEditChangeProperty(Evt);
	Schedule->MarkPackageDirty();
	return true;
}

bool FFPSRDataEditorHelpers::RemoveMissionFromScheduleWindow(UFPSRRunScheduleDataAsset* Schedule, int32 WindowIndex, UFPSRMissionDataAsset* Mission)
{
	if (!Schedule || !Mission || !Schedule->MissionWindows.IsValidIndex(WindowIndex))
	{
		return false;
	}
	TArray<TObjectPtr<UFPSRMissionDataAsset>>& Pool = Schedule->MissionWindows[WindowIndex].MissionPool;
	if (!Pool.Contains(Mission))
	{
		return false;
	}

	const FScopedTransaction Transaction(LOCTEXT("Transaction_RemoveMissionFromWindow", "Remove Mission from Schedule Window"));
	Schedule->Modify();
	Pool.Remove(Mission);

	FProperty* Prop = FindFProperty<FProperty>(UFPSRRunScheduleDataAsset::StaticClass(), GET_MEMBER_NAME_CHECKED(UFPSRRunScheduleDataAsset, MissionWindows));
	FPropertyChangedEvent Evt(Prop, EPropertyChangeType::ArrayRemove);
	Schedule->PostEditChangeProperty(Evt);
	Schedule->MarkPackageDirty();
	return true;
}

FText FFPSRDataEditorHelpers::GetRouteDisplayText(EFPSRCardRoute Route)
{
	// Closed table for a closed enum (EFPSRCardRoute is fixed by the C++ schema — see FPSRCardTypes.h) — a switch
	// here is the correct shape, unlike the OPEN card-effect axis (GetCardEligibleRoutes), which never switches.
	switch (Route)
	{
	case EFPSRCardRoute::LevelUpGlobal:
		return LOCTEXT("Route_LevelUpGlobal", "Level-Up: Global Pool");
	case EFPSRCardRoute::MissionClearNewWeapon:
		return LOCTEXT("Route_MissionClearNewWeapon", "Mission-Clear: New Weapon");
	case EFPSRCardRoute::LevelUpWeapon:
		return LOCTEXT("Route_LevelUpWeapon", "Level-Up: Weapon Card");
	case EFPSRCardRoute::MissionClearWeaponFeature:
		return LOCTEXT("Route_MissionClearWeaponFeature", "Mission-Clear: Weapon Feature");
	default:
		return LOCTEXT("Route_Unknown", "(unknown route)");
	}
}

bool FFPSRDataEditorHelpers::SetCardEffectMagnitude(UFPSRCardDataAsset* Card, int32 EffectIndex, ECardRarity Rarity, float NewMagnitude)
{
	if (!Card)
	{
		return false;
	}
	const FScopedTransaction Transaction(LOCTEXT("Transaction_SetCardEffectMagnitude", "Edit Card Effect Magnitude"));
	return Card->SetEffectRarityMagnitude(EffectIndex, Rarity, NewMagnitude);
}

int32 FFPSRDataEditorHelpers::SavePackages(const TArray<UPackage*>& Packages)
{
	// Count only the actually-dirty subset up front (bOnlyDirty below silently skips clean packages), so the
	// reported count matches what SavePackages will really write to disk rather than the caller's tracked-package
	// list size (which may include packages the user already saved through some other path).
	int32 DirtyCount = 0;
	TArray<UPackage*> ToSave;
	ToSave.Reserve(Packages.Num());
	for (UPackage* Package : Packages)
	{
		if (Package && Package->IsDirty())
		{
			++DirtyCount;
			ToSave.Add(Package);
		}
	}
	if (DirtyCount == 0)
	{
		return 0;
	}

	// bOnlyDirty=true, no dialog (the Data Editor's "Save Modified + Rescan" button is the explicit save gesture —
	// no need to re-prompt which packages to save, the caller already scoped the list to what it touched).
	const bool bSuccess = UEditorLoadingAndSavingUtils::SavePackages(ToSave, /*bOnlyDirty=*/true);
	return bSuccess ? DirtyCount : 0;
}

#undef LOCTEXT_NAMESPACE
