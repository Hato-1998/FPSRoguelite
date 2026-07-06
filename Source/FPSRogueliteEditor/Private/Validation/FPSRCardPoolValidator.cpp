// Copyright Epic Games, Inc. All Rights Reserved.

#include "Validation/FPSRCardPoolValidator.h"
#include "Validation/FPSRAnchoredValidationService.h"
#include "Card/FPSRCardPoolDataAsset.h"
#include "Card/FPSRCardDataAsset.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetRegistry/ARFilter.h"
#include "Misc/DataValidation.h"
#include "GameplayTagContainer.h"

#define LOCTEXT_NAMESPACE "FPSRCardPoolValidator"

namespace
{
	// Fallback minimum candidate count for a viable draw when no fixed offer-size constant exists in the project
	// (DrawCards/DrawWeaponUnlockOffer take Count as a caller parameter, not a DataAsset-authored constant — see
	// UFPSRCardSubsystem::DrawCards). 3 matches a typical "pick one of three" level-up offer; tune here if the
	// project settles on a different number later.
	constexpr int32 kMinViableCandidates = 3;
}

bool UFPSRCardPoolValidator::CanValidateAsset_Implementation(const FAssetData& InAssetData, UObject* InAsset, FDataValidationContext& InContext) const
{
	return InAsset != nullptr && InAsset->IsA<UFPSRCardPoolDataAsset>();
}

EDataValidationResult UFPSRCardPoolValidator::ValidateLoadedAsset_Implementation(const FAssetData& InAssetData, UObject* InAsset, FDataValidationContext& Context)
{
	const UFPSRCardPoolDataAsset* Pool = Cast<UFPSRCardPoolDataAsset>(InAsset);
	if (Pool == nullptr)
	{
		AssetPasses(InAsset);
		return EDataValidationResult::Valid;
	}

	// Save usecase: keep it to per-asset checks only (no cross-project asset registry scan on every Ctrl+S). The
	// full cross-pool pass runs on Manual / Commandlet / Script / PreSubmit (the Tools menu entry and the CI
	// commandlet both request one of those).
	const bool bCheapOnly = Context.GetValidationUsecase() == EDataValidationUsecase::Save;

	EDataValidationResult Result = ValidateCheapSelfChecks(Pool, Context);
	if (!bCheapOnly)
	{
		const EDataValidationResult CrossResult = ValidateCrossPoolChecks(Pool, Context);
		if (CrossResult == EDataValidationResult::Invalid)
		{
			Result = EDataValidationResult::Invalid;
		}
	}

	if (Result == EDataValidationResult::Valid)
	{
		AssetPasses(InAsset);
	}
	return Result;
}

EDataValidationResult UFPSRCardPoolValidator::ValidateCheapSelfChecks(const UFPSRCardPoolDataAsset* Pool, FDataValidationContext& Context)
{
	EDataValidationResult Result = EDataValidationResult::Valid;

	auto CheckNulls = [&](const TArray<TObjectPtr<UFPSRCardDataAsset>>& List, const TCHAR* ListName)
	{
		for (int32 Index = 0; Index < List.Num(); ++Index)
		{
			if (List[Index] == nullptr)
			{
				Context.AddError(FText::Format(
					LOCTEXT("NullCardEntry", "{0}[{1}] is null — the draw would offer an empty slot."),
					FText::FromString(ListName), FText::AsNumber(Index)));
				Result = EDataValidationResult::Invalid;
			}
		}
	};
	CheckNulls(Pool->Cards, TEXT("Cards"));
	CheckNulls(Pool->WeaponUnlockCards, TEXT("WeaponUnlockCards"));

	if (Pool->CommonWeight <= 0.0f && Pool->RareWeight <= 0.0f && Pool->EpicWeight <= 0.0f && Pool->LegendaryWeight <= 0.0f)
	{
		Context.AddWarning(LOCTEXT("AllRarityWeightsZero", "Every rarity weight is <= 0 — the draw can never pick a rarity, so nothing will ever be offered from this pool."));
	}

	return Result;
}

EDataValidationResult UFPSRCardPoolValidator::ValidateCrossPoolChecks(const UFPSRCardPoolDataAsset* Pool, FDataValidationContext& Context)
{
	EDataValidationResult Result = EDataValidationResult::Valid;

	// --- Global CardId uniqueness: two DISTINCT card assets sharing a CardId anywhere in the project. The same card
	//     asset listed in multiple pools is fine (that's how a card can be offered from more than one route) — only
	//     flag when the CardId maps to more than one underlying UFPSRCardDataAsset. Scans every card pool once so an
	//     N-pool project doesn't do N scans; this pool's own report only surfaces collisions that involve ITS cards. ---
	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	FARFilter CardFilter;
	CardFilter.ClassPaths.Add(UFPSRCardDataAsset::StaticClass()->GetClassPathName());
	TArray<FAssetData> AllCardAssets;
	AssetRegistry.GetAssets(CardFilter, AllCardAssets);

	TMap<FName, TArray<FName>> CardIdToAssetPackages;
	TSet<FName> ThisPoolPackages;
	for (const TObjectPtr<UFPSRCardDataAsset>& Card : Pool->Cards)
	{
		if (Card)
		{
			ThisPoolPackages.Add(Card->GetPackage()->GetFName());
		}
	}
	for (const TObjectPtr<UFPSRCardDataAsset>& Card : Pool->WeaponUnlockCards)
	{
		if (Card)
		{
			ThisPoolPackages.Add(Card->GetPackage()->GetFName());
		}
	}

	// CardId isn't AssetRegistrySearchable (no UPROPERTY meta tag), so the value isn't queryable straight off the
	// FAssetData — load each card to read it. Only reached on Manual/Commandlet/Script/PreSubmit (never Save), so the
	// one-time load cost across the project's card count is acceptable.
	for (const FAssetData& CardAssetData : AllCardAssets)
	{
		// Honor the same scratch/Dev/Test exclusion as anchored discovery: an abandoned or sandbox-folder card that
		// happens to reuse a CardId must never fail an anchored build (the validation contract says excluded/unreachable
		// content doesn't gate CI). A dup CardId between two SHIPPING cards is still flagged below.
		if (FFPSRAnchoredValidationService::IsExcludedPath(CardAssetData.PackagePath))
		{
			continue;
		}
		const UFPSRCardDataAsset* CardAsset = Cast<UFPSRCardDataAsset>(CardAssetData.GetAsset());
		if (CardAsset == nullptr || CardAsset->CardId.IsNone())
		{
			continue; // empty CardId is flagged by the card's own IsDataValid, not here
		}
		CardIdToAssetPackages.FindOrAdd(CardAsset->CardId).Add(CardAssetData.PackageName);
	}

	for (const TPair<FName, TArray<FName>>& Entry : CardIdToAssetPackages)
	{
		const TArray<FName>& Packages = Entry.Value;
		if (Packages.Num() <= 1)
		{
			continue;
		}
		// Only report if at least one of the colliding assets is one of THIS pool's cards, so a single global
		// collision doesn't spam every pool that never touches it.
		const bool bInvolvesThisPool = Packages.ContainsByPredicate([&ThisPoolPackages](FName Package) { return ThisPoolPackages.Contains(Package); });
		if (!bInvolvesThisPool)
		{
			continue;
		}
		Context.AddError(FText::Format(
			LOCTEXT("GlobalDuplicateCardId", "CardId '{0}' is shared by {1} distinct card assets across the project — the meta save keys unlocks by CardId, so this would alias unlocks between unrelated cards. Give each card a unique CardId."),
			FText::FromName(Entry.Key), FText::AsNumber(Packages.Num())));
		Result = EDataValidationResult::Invalid;
	}

	// --- CardFamily conflict: no established mutual-exclusion rule beyond "same family = pick one" (see
	//     FPSRCardDataAsset.cpp's MultiNoFamily check, which already requires multi-effect cards to set CardFamily).
	//     There is currently no documented rule for what makes two DIFFERENT families "conflict" with each other, so
	//     this is intentionally left as a TODO rather than inventing a rule. ---
	// TODO(CardFamily): once a cross-family conflict rule is specified, add a pool-level check here.

	// --- Offer viability: rarity coverage + candidate count after CardFamily de-dup (mutually exclusive families
	//     only ever offer one representative). Warning-only — an under-populated pool still functions, it's just a
	//     thin draw designers should know about. ---
	TArray<const UFPSRCardDataAsset*> DistinctCandidates; // one representative per CardFamily (empty family = itself)
	TSet<FGameplayTag> SeenFamilies;
	for (const TObjectPtr<UFPSRCardDataAsset>& Card : Pool->Cards)
	{
		if (!Card)
		{
			continue;
		}
		if (Card->CardFamily.IsValid())
		{
			if (SeenFamilies.Contains(Card->CardFamily))
			{
				continue;
			}
			SeenFamilies.Add(Card->CardFamily);
		}
		DistinctCandidates.Add(Card);
	}

	if (DistinctCandidates.Num() < kMinViableCandidates)
	{
		Context.AddWarning(FText::Format(
			LOCTEXT("ThinOfferPool", "Only {0} distinct candidate(s) in Cards after CardFamily de-dup (need >= {1} for a healthy draw) — level-up offers from this pool may repeat or come up short."),
			FText::AsNumber(DistinctCandidates.Num()), FText::AsNumber(kMinViableCandidates)));
	}

	TArray<ECardRarity> OfferableRarities;
	if (Pool->CommonWeight > 0.0f) { OfferableRarities.Add(ECardRarity::Common); }
	if (Pool->RareWeight > 0.0f) { OfferableRarities.Add(ECardRarity::Rare); }
	if (Pool->EpicWeight > 0.0f) { OfferableRarities.Add(ECardRarity::Epic); }
	if (Pool->LegendaryWeight > 0.0f) { OfferableRarities.Add(ECardRarity::Legendary); }

	for (const ECardRarity Rarity : OfferableRarities)
	{
		const bool bHasCandidate = DistinctCandidates.ContainsByPredicate([Rarity](const UFPSRCardDataAsset* Card)
		{
			return Card->OfferRarities.Contains(Rarity);
		});
		if (!bHasCandidate)
		{
			const FString RarityStr = StaticEnum<ECardRarity>()->GetNameStringByValue(static_cast<int64>(Rarity));
			Context.AddWarning(FText::Format(
				LOCTEXT("RarityNoCandidate", "Pool's rarity weight for '{0}' is > 0 but no card in Cards offers that rarity — that rarity can be rolled and then find nothing to draw."),
				FText::FromString(RarityStr)));
		}
	}

	// TODO(routing): add surface-leakage check after CombatWeaponCard §2-3-4 routing spec is locked.

	return Result;
}

#undef LOCTEXT_NAMESPACE
