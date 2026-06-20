// Copyright Epic Games, Inc. All Rights Reserved.

#include "Card/FPSRCardEffect.h"
#include "Core/FPSRPlayerState.h"
#include "AbilitySystem/FPSRAbilitySystemComponent.h"
#include "Weapon/FPSRWeaponInventoryComponent.h"
#include "Weapon/FPSRWeaponInstance.h"
#include "Weapon/FPSRWeaponFragment.h"
#include "AbilitySystemComponent.h"
#include "GameplayEffect.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "FPSRCardEffect"

namespace
{
	/** Format a magnitude like the v1 card widget did: percent for fractional multipliers, integer when whole,
	 *  otherwise up to 2 decimals (never truncating e.g. +0.25 to a misleading +0). */
	FString FormatCardMagnitude(float Mag, bool bPercent)
	{
		if (bPercent)
		{
			return FString::Printf(TEXT("%+d%%"), FMath::RoundToInt(Mag * 100.0f));
		}
		if (FMath::IsNearlyEqual(Mag, FMath::RoundToFloat(Mag)))
		{
			return FString::Printf(TEXT("%+d"), FMath::RoundToInt(Mag));
		}
		return FString::Printf(TEXT("%+.2f"), Mag);
	}

	/** Resolve the target weapon instance for a weapon effect: the card's source weapon if set (anti-cheat: server
	 *  built the offer from owned weapons, an unowned target resolves to null), else the currently equipped one. */
	UFPSRWeaponInstance* ResolveTargetInstance(const FFPSRCardEffectContext& Context)
	{
		if (!Context.Inventory)
		{
			return nullptr;
		}
		return Context.TargetWeapon
			? Context.Inventory->GetInstanceForWeapon(Context.TargetWeapon)
			: Context.Inventory->GetCurrentInstance();
	}
}

float UFPSRCardEffect::ResolveMagnitude(ECardRarity Rarity) const
{
	for (const FFPSRCardRarityTier& Tier : RarityTiers)
	{
		if (Tier.Rarity == Rarity)
		{
			return Tier.Magnitude;
		}
	}
	return 0.0f;
}

FText UFPSRCardEffect::GetDescription(ECardRarity Rarity, float Magnitude) const
{
	return FText::GetEmpty();
}

// ---------------------------------------------------------------------------------------------------------------
// UCardEffect_CharacterGE — applies a GameplayEffect to the player ASC (v1 Character scope, FPSRCardSubsystem.cpp:230).
// ---------------------------------------------------------------------------------------------------------------
void UCardEffect_CharacterGE::Apply(const FFPSRCardEffectContext& Context, float Magnitude) const
{
	if (!Effect || !Context.ASC)
	{
		return;
	}
	FGameplayEffectContextHandle Ctx = Context.ASC->MakeEffectContext();
	Ctx.AddSourceObject(Context.PS);
	FGameplayEffectSpecHandle Spec = Context.ASC->MakeOutgoingSpec(Effect, 1.0f, Ctx);
	if (Spec.IsValid())
	{
		// For GEs whose modifier uses SetByCaller (tag SetByCaller.CardMagnitude). Harmless for fixed GEs.
		static const FGameplayTag CardMagnitudeTag = FGameplayTag::RequestGameplayTag(FName("SetByCaller.CardMagnitude"));
		Spec.Data->SetSetByCallerMagnitude(CardMagnitudeTag, Magnitude);
		Context.ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
	}
}

FText UCardEffect_CharacterGE::GetDescription(ECardRarity Rarity, float Magnitude) const
{
	// The card's authored Description carries the human text; this slot shows the rolled numeric value (flat — the
	// GE's modified attribute isn't introspected cheaply, matching v1 character-scope magnitude formatting).
	return FText::FromString(FormatCardMagnitude(Magnitude, /*bPercent*/ false));
}

bool UCardEffect_CharacterGE::CanApply(const FFPSRCardEffectContext& Context) const
{
	return Context.ASC != nullptr;
}

#if WITH_EDITOR
void UCardEffect_CharacterGE::ValidateEffect(FDataValidationContext& Context) const
{
	if (!Effect)
	{
		Context.AddWarning(LOCTEXT("CharGENoEffect", "Character GE effect has no Effect (GameplayEffect) — it applies nothing."));
	}
}
#endif

// ---------------------------------------------------------------------------------------------------------------
// UCardEffect_WeaponStat — stat modifier (v1 ThisWeapon-stat / AllWeapons, FPSRCardSubsystem.cpp:256-283).
// ---------------------------------------------------------------------------------------------------------------
void UCardEffect_WeaponStat::Apply(const FFPSRCardEffectContext& Context, float Magnitude) const
{
	const FFPSRWeaponStatMod Mod{ Stat, Op, Magnitude };
	if (bThisWeaponOnly)
	{
		if (UFPSRWeaponInstance* Instance = ResolveTargetInstance(Context))
		{
			Instance->AddModifier(Mod);
		}
	}
	else if (Context.PS)
	{
		Context.PS->AddAllWeaponsModifier(Mod);
	}
}

FText UCardEffect_WeaponStat::GetDescription(ECardRarity Rarity, float Magnitude) const
{
	const bool bPercent = (Op == EFPSRWeaponModOp::PercentMultiply);
	const FString StatName = StaticEnum<EFPSRWeaponStat>()
		? StaticEnum<EFPSRWeaponStat>()->GetDisplayNameTextByValue(static_cast<int64>(Stat)).ToString()
		: FString(TEXT("Stat"));
	const FString ScopeSuffix = bThisWeaponOnly ? FString() : FString(TEXT(" (all weapons)"));
	return FText::FromString(FString::Printf(TEXT("%s %s%s"), *StatName, *FormatCardMagnitude(Magnitude, bPercent), *ScopeSuffix));
}

bool UCardEffect_WeaponStat::CanApply(const FFPSRCardEffectContext& Context) const
{
	if (bThisWeaponOnly)
	{
		return ResolveTargetInstance(Context) != nullptr;
	}
	return Context.PS != nullptr;
}

// ---------------------------------------------------------------------------------------------------------------
// UCardEffect_WeaponBehavior — grants a behavior fragment (v1 ThisWeapon + GrantedFragment, FPSRCardSubsystem.cpp:270).
// ---------------------------------------------------------------------------------------------------------------
void UCardEffect_WeaponBehavior::Apply(const FFPSRCardEffectContext& Context, float Magnitude) const
{
	if (!Fragment)
	{
		return;
	}
	if (UFPSRWeaponInstance* Instance = ResolveTargetInstance(Context))
	{
		Instance->AddFragment(Fragment); // MaxStacks gate is inside AddFragment
	}
}

FText UCardEffect_WeaponBehavior::GetDescription(ECardRarity Rarity, float Magnitude) const
{
	if (Fragment && !Fragment->DisplayName.IsEmpty())
	{
		return Fragment->DisplayName;
	}
	return LOCTEXT("WeaponModifier", "Weapon Modifier");
}

bool UCardEffect_WeaponBehavior::CanApply(const FFPSRCardEffectContext& Context) const
{
	return Fragment != nullptr && ResolveTargetInstance(Context) != nullptr;
}

#if WITH_EDITOR
void UCardEffect_WeaponBehavior::ValidateEffect(FDataValidationContext& Context) const
{
	if (!Fragment)
	{
		Context.AddError(LOCTEXT("BehaviorNoFragment", "Weapon behavior effect has no Fragment — it grants nothing."));
	}
}
#endif

#undef LOCTEXT_NAMESPACE
