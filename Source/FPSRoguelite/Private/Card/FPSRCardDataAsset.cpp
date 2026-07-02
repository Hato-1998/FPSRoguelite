// Copyright Epic Games, Inc. All Rights Reserved.

#include "Card/FPSRCardDataAsset.h"
#include "Card/FPSRCardEffect.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"

#define LOCTEXT_NAMESPACE "FPSRCardDataAsset"

void UFPSRCardDataAsset::PostLoad()
{
	Super::PostLoad();
	RefreshOfferRarities();
}

void UFPSRCardDataAsset::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	// Keep OfferRarities in sync as the designer edits Effects' RarityTiers.
	RefreshOfferRarities();
}

void UFPSRCardDataAsset::RefreshOfferRarities()
{
	// The card offers the rarities of its FIRST magnitude-bearing effect; IsDataValid enforces every other
	// magnitude effect covers the same set, so a rolled rarity always has a tier in each effect.
	OfferRarities.Reset();
	for (const TObjectPtr<UFPSRCardEffect>& Effect : Effects)
	{
		if (Effect && Effect->RarityTiers.Num() > 0)
		{
			for (const FFPSRCardRarityTier& Tier : Effect->RarityTiers)
			{
				OfferRarities.AddUnique(Tier.Rarity);
			}
			break;
		}
	}
	OfferRarities.Sort([](const ECardRarity& A, const ECardRarity& B)
	{
		return static_cast<uint8>(A) < static_cast<uint8>(B);
	});
}

EDataValidationResult UFPSRCardDataAsset::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	if (CardId.IsNone())
	{
		Context.AddError(LOCTEXT("EmptyCardId", "Card has no CardId — the meta save needs a stable per-card key. Set a unique CardId (U10)."));
		Result = EDataValidationResult::Invalid;
	}

	if (Effects.Num() == 0)
	{
		Context.AddError(LOCTEXT("NoEffects", "Card has no Effects — it applies nothing. Add at least one effect."));
		Result = EDataValidationResult::Invalid;
	}

	int32 NumValidEffects = 0;
	for (const TObjectPtr<UFPSRCardEffect>& Effect : Effects)
	{
		if (!Effect)
		{
			Context.AddError(LOCTEXT("NullEffect", "Card has a null entry in Effects — remove it or assign an effect."));
			Result = EDataValidationResult::Invalid;
			continue;
		}
		++NumValidEffects;
		// Fold per-effect validation errors into the card's result (AddError alone doesn't fail the asset gate).
		const uint32 ErrorsBefore = Context.GetNumErrors();
		Effect->ValidateEffect(Context);
		if (Context.GetNumErrors() > ErrorsBefore)
		{
			Result = EDataValidationResult::Invalid;
		}
	}

	// Rarity coverage (§2-3-1): every magnitude-bearing effect must declare a tier for each offered rarity, or a
	// rolled rarity would apply 0 / nothing for that effect (silent partial application).
	for (const TObjectPtr<UFPSRCardEffect>& Effect : Effects)
	{
		if (!Effect || Effect->RarityTiers.Num() == 0)
		{
			continue;
		}
		for (const ECardRarity R : OfferRarities)
		{
			const bool bHasTier = Effect->RarityTiers.ContainsByPredicate(
				[R](const FFPSRCardRarityTier& T) { return T.Rarity == R; });
			if (!bHasTier)
			{
				const FString RarityStr = StaticEnum<ECardRarity>()->GetNameStringByValue(static_cast<int64>(R));
				Context.AddError(FText::Format(
					LOCTEXT("RarityCoverage", "An effect is missing a tier for rarity '{0}' that the card offers — all magnitude effects must cover the same rarities."),
					FText::FromString(RarityStr)));
				Result = EDataValidationResult::Invalid;
			}
		}
	}

	// 0-resolution guard (§ review residual): a magnitude-bearing card must surface at least one non-empty value line
	// at every rarity it offers — using the same GetDescription the entry widget consumes — or the selection UI shows
	// a rarity badge over a blank value slot. Magnitude-independent effects (passives/fragments/grants) return fixed
	// text so they never trip this; a legitimate per-effect 0 tier (e.g. a Legendary-only sub-effect) is fine as long
	// as a sibling effect still shows a value at that rarity.
	bool bHasMagnitudeEffect = false;
	for (const TObjectPtr<UFPSRCardEffect>& Effect : Effects)
	{
		if (Effect && Effect->RarityTiers.Num() > 0)
		{
			bHasMagnitudeEffect = true;
			break;
		}
	}
	if (bHasMagnitudeEffect)
	{
		for (const ECardRarity R : OfferRarities)
		{
			bool bAnyVisibleLine = false;
			for (const TObjectPtr<UFPSRCardEffect>& Effect : Effects)
			{
				if (Effect && !Effect->GetDescription(R, Effect->ResolveMagnitude(R)).IsEmpty())
				{
					bAnyVisibleLine = true;
					break;
				}
			}
			if (!bAnyVisibleLine)
			{
				const FString RarityStr = StaticEnum<ECardRarity>()->GetNameStringByValue(static_cast<int64>(R));
				Context.AddWarning(FText::Format(
					LOCTEXT("ZeroResolution", "Card offers rarity '{0}' but every effect resolves to no visible value there — the selection UI would show a rarity badge over a blank value. Give an effect a non-zero tier for this rarity, or drop it from OfferRarities."),
					FText::FromString(RarityStr)));
			}
		}
	}

	// Multi-effect cards must set CardFamily (the v1 AppliedEffect-GE-class family fallback was removed).
	if (NumValidEffects > 1 && !CardFamily.IsValid())
	{
		Context.AddError(LOCTEXT("MultiNoFamily", "Multi-effect card must set CardFamily — mutual-exclusion can no longer fall back to a GE class."));
		Result = EDataValidationResult::Invalid;
	}

	if (DisplayName.IsEmpty())
	{
		Context.AddWarning(LOCTEXT("NoDisplayName", "Card has no DisplayName — the selection UI will show no title."));
	}

	// Naming lint (§2-3-8): DA_Card_<Group>_<Theme> / DA_CardModifiers_<Behavior>.
	const FString AssetName = GetName();
	if (!AssetName.StartsWith(TEXT("DA_Card")))
	{
		Context.AddWarning(FText::Format(
			LOCTEXT("NameLint", "Card asset '{0}' should be named DA_Card_<Group>_<Theme> (or DA_CardModifiers_<Behavior>)."),
			FText::FromString(AssetName)));
	}

	return Result;
}

#undef LOCTEXT_NAMESPACE
#endif // WITH_EDITOR
