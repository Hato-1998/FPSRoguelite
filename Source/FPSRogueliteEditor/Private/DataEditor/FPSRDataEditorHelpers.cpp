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
		OutReason = LOCTEXT("CheckRoute_NoCard", "선택된 카드가 없습니다.");
		return EFPSRWiringVerdict::Blocked;
	}

	const TArray<EFPSRCardRoute> Eligible = GetCardEligibleRoutes(Card);
	if (Eligible.Num() == 0)
	{
		// Empty intersection: the card's effects don't share ANY common route (e.g. a CharacterGE effect — eligible
		// only for LevelUpGlobal — mixed onto the same card as a WeaponBehavior effect — eligible only for
		// MissionClearWeaponFeature). H2 = this is a hard error, never a warning: such a card cannot be wired
		// anywhere without being a silent no-op or a semantically wrong offer.
		OutReason = LOCTEXT("CheckRoute_EmptyIntersection", "이 카드의 효과들이 공통 라우트를 공유하지 않습니다(예: 레벨업 전용 효과와 미션 전용 효과가 한 카드에 섞임) — 어떤 풀에도 배선할 수 없습니다. 효과 구성을 확인하세요.");
		return EFPSRWiringVerdict::Blocked;
	}
	if (!Eligible.Contains(Route))
	{
		OutReason = FText::Format(
			LOCTEXT("CheckRoute_Blocked", "'{0}'은(는) 이 카드의 효과에 적격인 라우트가 아닙니다 — 여기 배선하면 드로우 시 무효(또는 의미상 잘못된 오퍼)가 됩니다."),
			GetRouteDisplayText(Route));
		return EFPSRWiringVerdict::Blocked;
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
		? AddUniqueToArray(Pool, Pool->WeaponUnlockCards, Card, GET_MEMBER_NAME_CHECKED(UFPSRCardPoolDataAsset, WeaponUnlockCards), LOCTEXT("Transaction_AddCardToPoolUnlock", "카드를 풀에 추가 (무기 언락)"))
		: AddUniqueToArray(Pool, Pool->Cards, Card, GET_MEMBER_NAME_CHECKED(UFPSRCardPoolDataAsset, Cards), LOCTEXT("Transaction_AddCardToPool", "카드를 풀에 추가"));
}

bool FFPSRDataEditorHelpers::RemoveCardFromPool(UFPSRCardPoolDataAsset* Pool, UFPSRCardDataAsset* Card, bool bUnlockArray)
{
	if (!Pool)
	{
		return false;
	}
	return bUnlockArray
		? RemoveFromArray(Pool, Pool->WeaponUnlockCards, Card, GET_MEMBER_NAME_CHECKED(UFPSRCardPoolDataAsset, WeaponUnlockCards), LOCTEXT("Transaction_RemoveCardFromPoolUnlock", "카드를 풀에서 제거 (무기 언락)"))
		: RemoveFromArray(Pool, Pool->Cards, Card, GET_MEMBER_NAME_CHECKED(UFPSRCardPoolDataAsset, Cards), LOCTEXT("Transaction_RemoveCardFromPool", "카드를 풀에서 제거"));
}

bool FFPSRDataEditorHelpers::AddCardToWeapon(UFPSRWeaponDataAsset* Weapon, UFPSRCardDataAsset* Card, bool bUnlockableFeatures)
{
	if (!Weapon)
	{
		return false;
	}
	return bUnlockableFeatures
		? AddUniqueToArray(Weapon, Weapon->UnlockableFeatures, Card, GET_MEMBER_NAME_CHECKED(UFPSRWeaponDataAsset, UnlockableFeatures), LOCTEXT("Transaction_AddCardToWeaponFeature", "카드를 무기에 추가 (언락 피처)"))
		: AddUniqueToArray(Weapon, Weapon->WeaponCards, Card, GET_MEMBER_NAME_CHECKED(UFPSRWeaponDataAsset, WeaponCards), LOCTEXT("Transaction_AddCardToWeapon", "카드를 무기에 추가 (레벨업)"));
}

bool FFPSRDataEditorHelpers::RemoveCardFromWeapon(UFPSRWeaponDataAsset* Weapon, UFPSRCardDataAsset* Card, bool bUnlockableFeatures)
{
	if (!Weapon)
	{
		return false;
	}
	return bUnlockableFeatures
		? RemoveFromArray(Weapon, Weapon->UnlockableFeatures, Card, GET_MEMBER_NAME_CHECKED(UFPSRWeaponDataAsset, UnlockableFeatures), LOCTEXT("Transaction_RemoveCardFromWeaponFeature", "카드를 무기에서 제거 (언락 피처)"))
		: RemoveFromArray(Weapon, Weapon->WeaponCards, Card, GET_MEMBER_NAME_CHECKED(UFPSRWeaponDataAsset, WeaponCards), LOCTEXT("Transaction_RemoveCardFromWeapon", "카드를 무기에서 제거 (레벨업)"));
}

bool FFPSRDataEditorHelpers::AddWeaponToLoadout(UFPSRLoadoutPoolDataAsset* Loadout, UFPSRWeaponDataAsset* Weapon)
{
	if (!Loadout)
	{
		return false;
	}
	return AddUniqueToArray(Loadout, Loadout->SelectableWeapons, Weapon, GET_MEMBER_NAME_CHECKED(UFPSRLoadoutPoolDataAsset, SelectableWeapons), LOCTEXT("Transaction_AddWeaponToLoadout", "무기를 로드아웃에 추가"));
}

bool FFPSRDataEditorHelpers::RemoveWeaponFromLoadout(UFPSRLoadoutPoolDataAsset* Loadout, UFPSRWeaponDataAsset* Weapon)
{
	if (!Loadout)
	{
		return false;
	}
	return RemoveFromArray(Loadout, Loadout->SelectableWeapons, Weapon, GET_MEMBER_NAME_CHECKED(UFPSRLoadoutPoolDataAsset, SelectableWeapons), LOCTEXT("Transaction_RemoveWeaponFromLoadout", "무기를 로드아웃에서 제거"));
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

	const FScopedTransaction Transaction(LOCTEXT("Transaction_AddMissionToWindow", "미션을 스케줄 윈도우에 추가"));
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

	const FScopedTransaction Transaction(LOCTEXT("Transaction_RemoveMissionFromWindow", "미션을 스케줄 윈도우에서 제거"));
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
		return LOCTEXT("Route_LevelUpGlobal", "레벨업: 전역 풀");
	case EFPSRCardRoute::MissionClearNewWeapon:
		return LOCTEXT("Route_MissionClearNewWeapon", "미션 클리어: 새 무기");
	case EFPSRCardRoute::LevelUpWeapon:
		return LOCTEXT("Route_LevelUpWeapon", "레벨업: 무기 카드");
	case EFPSRCardRoute::MissionClearWeaponFeature:
		return LOCTEXT("Route_MissionClearWeaponFeature", "미션 클리어: 무기 피처");
	default:
		return LOCTEXT("Route_Unknown", "(알 수 없는 라우트)");
	}
}

bool FFPSRDataEditorHelpers::SetCardEffectMagnitude(UFPSRCardDataAsset* Card, int32 EffectIndex, ECardRarity Rarity, float NewMagnitude)
{
	if (!Card)
	{
		return false;
	}
	const FScopedTransaction Transaction(LOCTEXT("Transaction_SetCardEffectMagnitude", "카드 효과 매그니튜드 편집"));
	return Card->SetEffectRarityMagnitude(EffectIndex, Rarity, NewMagnitude);
}

bool FFPSRDataEditorHelpers::CreateCardOfferRarity(UFPSRCardDataAsset* Card, ECardRarity Rarity)
{
	if (!Card)
	{
		return false;
	}
	const FScopedTransaction Transaction(LOCTEXT("Transaction_CreateCardOfferRarity", "카드 오퍼 등급 생성"));
	return Card->CreateEffectRarityTier(Rarity);
}

bool FFPSRDataEditorHelpers::DeleteCardOfferRarity(UFPSRCardDataAsset* Card, ECardRarity Rarity)
{
	if (!Card)
	{
		return false;
	}
	const FScopedTransaction Transaction(LOCTEXT("Transaction_DeleteCardOfferRarity", "카드 오퍼 등급 삭제"));
	return Card->DeleteEffectRarityTier(Rarity);
}

int32 FFPSRDataEditorHelpers::BulkApplyMagnitude(const TArray<FFPSRMagnitudeCellRef>& Cells, EFPSRBulkMagnitudeOp Op, float Operand, FText& OutStatus)
{
	OutStatus = FText::GetEmpty();

	// Pass 1: gather the valid cells (card alive, effect index valid, effect non-null, unit != None, tier exists for
	// that rarity) and cache each one's current unit — this pass must NOT mutate anything, since a mixed-unit Add
	// selection has to be detected and fully refused before any cell is touched.
	struct FValidCell
	{
		UFPSRCardDataAsset* Card = nullptr;
		int32 EffectIndex = INDEX_NONE;
		ECardRarity Rarity = ECardRarity::Common;
		float CurrentMagnitude = 0.0f;
		EFPSREditorMagnitudeUnit Unit = EFPSREditorMagnitudeUnit::None;
	};
	TArray<FValidCell> ValidCells;
	ValidCells.Reserve(Cells.Num());

	for (const FFPSRMagnitudeCellRef& Cell : Cells)
	{
		UFPSRCardDataAsset* Card = Cell.Card.Get();
		if (!Card || !Card->Effects.IsValidIndex(Cell.EffectIndex) || !Card->Effects[Cell.EffectIndex])
		{
			continue;
		}
		const UFPSRCardEffect* Effect = Card->Effects[Cell.EffectIndex];
		const EFPSREditorMagnitudeUnit Unit = Effect->GetEditorMagnitudeUnit();
		if (Unit == EFPSREditorMagnitudeUnit::None)
		{
			continue;
		}
		const FFPSRCardRarityTier* Tier = Effect->RarityTiers.FindByPredicate(
			[Rarity = Cell.Rarity](const FFPSRCardRarityTier& T) { return T.Rarity == Rarity; });
		if (!Tier)
		{
			continue;
		}
		ValidCells.Add(FValidCell{ Card, Cell.EffectIndex, Cell.Rarity, Tier->Magnitude, Unit });
	}

	if (ValidCells.Num() == 0)
	{
		OutStatus = LOCTEXT("BulkApply_NoTargets", "대상 티어가 없습니다.");
		return 0;
	}

	float AddRaw = 0.0f;
	if (Op == EFPSRBulkMagnitudeOp::Add)
	{
		// Unit-sensitive: every affected effect must share the same unit (Percent OR Flat). A mixed selection would
		// otherwise silently apply a percentage-point delta to a flat stat (or vice-versa) — refuse instead.
		EFPSREditorMagnitudeUnit CommonUnit = ValidCells[0].Unit;
		bool bMixed = false;
		for (const FValidCell& ValidCell : ValidCells)
		{
			if (ValidCell.Unit != CommonUnit)
			{
				bMixed = true;
				break;
			}
		}
		if (bMixed)
		{
			OutStatus = LOCTEXT("BulkApply_MixedUnits", "단위가 다른 효과가 섞여 있어 덧셈 일괄연산을 적용하지 않았습니다(퍼센트/고정값 혼합). 곱셈(×)은 안전하게 적용됩니다.");
			return 0;
		}
		AddRaw = (CommonUnit == EFPSREditorMagnitudeUnit::Percent) ? (Operand / 100.0f) : Operand;
	}

	const FScopedTransaction Transaction(LOCTEXT("Transaction_BulkApplyMagnitude", "카드 매그니튜드 일괄 연산"));
	int32 NumChanged = 0;
	for (const FValidCell& ValidCell : ValidCells)
	{
		const float NewMagnitude = (Op == EFPSRBulkMagnitudeOp::Multiply)
			? ValidCell.CurrentMagnitude * Operand
			: ValidCell.CurrentMagnitude + AddRaw;
		// Direct call (not SetCardEffectMagnitude) — that helper opens its OWN FScopedTransaction per call, which
		// would fragment this bulk op into N separate undo steps instead of one.
		if (ValidCell.Card->SetEffectRarityMagnitude(ValidCell.EffectIndex, ValidCell.Rarity, NewMagnitude))
		{
			++NumChanged;
		}
	}

	OutStatus = FText::Format(LOCTEXT("BulkApply_Applied", "{0}개 셀에 적용됨."), FText::AsNumber(NumChanged));
	return NumChanged;
}

bool FFPSRDataEditorHelpers::SetCardGroup(UFPSRCardDataAsset* Card, ECardGroup NewGroup)
{
	if (!Card || Card->Group == NewGroup)
	{
		return false;
	}
	const FScopedTransaction Transaction(LOCTEXT("Transaction_SetCardGroup", "카드 그룹 설정"));
	Card->Modify();
	Card->Group = NewGroup;
	FProperty* Prop = FindFProperty<FProperty>(UFPSRCardDataAsset::StaticClass(), GET_MEMBER_NAME_CHECKED(UFPSRCardDataAsset, Group));
	FPropertyChangedEvent Evt(Prop, EPropertyChangeType::ValueSet);
	Card->PostEditChangeProperty(Evt);
	Card->MarkPackageDirty();
	return true;
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
