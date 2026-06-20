// Copyright Epic Games, Inc. All Rights Reserved.

#include "Card/FPSRCardDataAsset.h"
#include "Card/FPSRCardEffect.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"

#define LOCTEXT_NAMESPACE "FPSRCardDataAsset"

void UFPSRCardDataAsset::PostLoad()
{
	Super::PostLoad();
	MigrateFromLegacy();
	RefreshOfferRarities();
}

void UFPSRCardDataAsset::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	// Keep OfferRarities in sync as the designer edits Effects' RarityTiers.
	RefreshOfferRarities();
}

void UFPSRCardDataAsset::MigrateFromLegacy()
{
	// Idempotent: only v1 cards (no v2 effects yet) migrate. Re-saved v2 assets skip this on every subsequent load.
	if (Effects.Num() > 0)
	{
		return;
	}

	UFPSRCardEffect* NewEffect = nullptr;
	switch (Scope)
	{
	case ECardScope::Character:
	{
		UCardEffect_CharacterGE* E = NewObject<UCardEffect_CharacterGE>(this);
		E->Effect = AppliedEffect;
		Group = ECardGroup::Character;
		NewEffect = E;
		break;
	}
	case ECardScope::AllWeapons:
	{
		// All-weapons stat -> a Weapon-stat effect that targets every weapon. Stays in the CHARACTER group/pool
		// (no source weapon, applies to the PlayerState) — preserving v1 central-pool membership & null TargetWeapon.
		UCardEffect_WeaponStat* E = NewObject<UCardEffect_WeaponStat>(this);
		E->Stat = WeaponStat;
		E->Op = WeaponStatOp;
		E->bThisWeaponOnly = false;
		Group = ECardGroup::Character;
		NewEffect = E;
		break;
	}
	case ECardScope::ThisWeapon:
	{
		if (GrantedFragment)
		{
			UCardEffect_WeaponBehavior* E = NewObject<UCardEffect_WeaponBehavior>(this);
			E->Fragment = GrantedFragment;
			NewEffect = E;
		}
		else
		{
			UCardEffect_WeaponStat* E = NewObject<UCardEffect_WeaponStat>(this);
			E->Stat = WeaponStat;
			E->Op = WeaponStatOp;
			E->bThisWeaponOnly = true;
			NewEffect = E;
		}
		Group = ECardGroup::Weapon;
		break;
	}
	default:
		break;
	}

	if (!NewEffect)
	{
		return;
	}

	// Copy the legacy per-rarity magnitudes onto the effect BEFORE clearing the card-level legacy data.
	NewEffect->RarityTiers = RarityTiers;
	Effects.Add(NewEffect);

	// Clear the migrated legacy fields so the re-saved asset carries no stale v1 data (effect now owns it). The
	// field declarations are removed in a follow-up commit after every asset is re-saved.
	RarityTiers.Reset();
	AppliedEffect = nullptr;
	GrantedFragment = nullptr;
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
