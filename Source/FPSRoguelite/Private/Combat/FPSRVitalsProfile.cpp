// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/FPSRVitalsProfile.h"
#include "Math/UnrealMathUtility.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

void UFPSRVitalsProfileDataAsset::ResolveDefense(const FGameplayTag& DamageType, float& OutShieldDefense, float& OutHealthDefense) const
{
	// Exact match -> default (empty-tag) entry -> 1.0. A full linear scan (no early-out on exact match) so a
	// duplicate default entry is harmless here even though IsDataValid already flags it as authoring error.
	const FFPSRVitalsDefenseEntry* ExactMatch = nullptr;
	const FFPSRVitalsDefenseEntry* DefaultEntry = nullptr;
	for (const FFPSRVitalsDefenseEntry& Entry : DefenseByDamageType)
	{
		if (Entry.DamageType == DamageType)
		{
			ExactMatch = &Entry;
		}
		if (!Entry.DamageType.IsValid())
		{
			DefaultEntry = &Entry;
		}
	}

	const FFPSRVitalsDefenseEntry* Chosen = ExactMatch ? ExactMatch : DefaultEntry;
	OutShieldDefense = Chosen ? Chosen->ShieldDefense : 1.0f;
	OutHealthDefense = Chosen ? Chosen->HealthDefense : 1.0f;
}

FFPSRResolvedVitals FFPSRResolvedVitals::Resolve(const UFPSRVitalsProfileDataAsset* Profile,
	const FFPSRVitalsDeckModifier& Deck, float FallbackMaxHealth)
{
	FFPSRResolvedVitals Resolved;
	if (!Profile)
	{
		// Profile null = the current no-shield behavior: the caller's own MaxHealth (BP editor default / boss
		// definition / destructible durability) stays authoritative until the VIT1 §11-1 content migration lands.
		// Zero-regression path.
		Resolved.MaxHealth = FallbackMaxHealth;
		Resolved.MaxShield = 0.0f;
		Resolved.Profile = nullptr;
		return Resolved;
	}

	// The deck scales AMOUNT only (never coefficients — see FFPSRVitalsDeckModifier's own comment).
	Resolved.MaxHealth = FMath::Max(1.0f, Profile->MaxHealth * Deck.MaxHealthScale);
	Resolved.MaxShield = FMath::Max(0.0f, Profile->MaxShield * Deck.MaxShieldScale);
	Resolved.ShieldRegenPerSecond = FMath::Max(0.0f, Profile->ShieldRegenPerSecond * Deck.ShieldRegenScale);
	Resolved.ShieldRegenDelaySeconds = FMath::Max(0.0f, Profile->ShieldRegenDelaySeconds);
	Resolved.ShieldBrokenRegenDelaySeconds = FMath::Max(0.0f, Profile->ShieldBrokenRegenDelaySeconds);
	Resolved.Profile = Profile;
	return Resolved;
}

#if WITH_EDITOR
#define LOCTEXT_NAMESPACE "FPSRVitalsProfileDataAsset"

EDataValidationResult UFPSRVitalsProfileDataAsset::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	TSet<FGameplayTag> SeenTags;
	SeenTags.Reserve(DefenseByDamageType.Num());
	for (int32 Index = 0; Index < DefenseByDamageType.Num(); ++Index)
	{
		const FFPSRVitalsDefenseEntry& Entry = DefenseByDamageType[Index];

		bool bAlreadySeen = false;
		SeenTags.Add(Entry.DamageType, &bAlreadySeen);
		if (bAlreadySeen)
		{
			Context.AddError(FText::Format(
				LOCTEXT("DuplicateDamageType", "DefenseByDamageType[{0}] repeats a DamageType already listed earlier (each type — including the empty/default entry — may appear at most once)."),
				FText::AsNumber(Index)));
			Result = EDataValidationResult::Invalid;
		}

		// The empty (default) entry is always valid — only a NON-empty tag must live under DamageType.*.
		if (Entry.DamageType.IsValid() && !Entry.DamageType.ToString().StartsWith(TEXT("DamageType.")))
		{
			Context.AddError(FText::Format(
				LOCTEXT("BadDamageTypeTag", "DefenseByDamageType[{0}] tag '{1}' is not under DamageType.* — leave it empty for the default entry, or pick a DamageType.* tag."),
				FText::AsNumber(Index), FText::FromString(Entry.DamageType.ToString())));
			Result = EDataValidationResult::Invalid;
		}
	}

	// A shield that can never refill is usually a forgotten field, not intent (a genuine one-charge shield is rare
	// enough to deserve a nudge either way) — warning, not an error.
	if (MaxShield > 0.0f && ShieldRegenPerSecond <= 0.0f)
	{
		Context.AddWarning(LOCTEXT("ShieldNeverRegens",
			"MaxShield > 0 but ShieldRegenPerSecond is 0 — this shield will never refill once spent. Fine for an intentional one-charge shield; double-check it wasn't just left at the default."));
	}

	// ResolveDefense is a per-hit linear scan (VIT1 §10 perf budget) — keep the list short.
	if (DefenseByDamageType.Num() > 8)
	{
		Context.AddWarning(LOCTEXT("TooManyDefenseEntries",
			"DefenseByDamageType has more than 8 entries — ResolveDefense scans it on every hit against this profile. Consider trimming it."));
	}

	// 🔴 The only real backstop behind invariant V1 (VIT1 §5-1: no mitigation combination may fully block a hit).
	// ClampMax on the UPROPERTY only restrains editor slider input — it does nothing against an imported or
	// script-authored value — so this must be re-checked here as a hard error.
	if (MaxTotalReduction >= 1.0f)
	{
		Context.AddError(LOCTEXT("MaxTotalReductionTooHigh",
			"MaxTotalReduction >= 1.0 would let mitigation stack to a hard 0-damage block, breaking invariant V1 (no combination of mitigations may fully block a hit that has any pool left). Keep it <= 0.99."));
		Result = EDataValidationResult::Invalid;
	}

	return Result;
}

#undef LOCTEXT_NAMESPACE
#endif // WITH_EDITOR
