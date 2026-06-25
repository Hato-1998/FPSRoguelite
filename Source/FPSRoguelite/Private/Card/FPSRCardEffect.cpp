// Copyright Epic Games, Inc. All Rights Reserved.

#include "Card/FPSRCardEffect.h"
#include "Core/FPSRPlayerState.h"
#include "AbilitySystem/FPSRAbilitySystemComponent.h"
#include "AbilitySystem/Abilities/FPSRPassiveAbility.h"
#include "Weapon/FPSRWeaponInventoryComponent.h"
#include "Weapon/FPSRWeaponInstance.h"
#include "Weapon/FPSRWeaponFragment.h"
#include "Weapon/FPSRWeaponDataAsset.h"
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
			// Whole-percent → integer ("+7%"); fractional → one decimal ("+7.5%") so e.g. +2.5% isn't shown as +3%.
			const float Pct = Mag * 100.0f;
			if (FMath::IsNearlyEqual(Pct, FMath::RoundToFloat(Pct)))
			{
				return FString::Printf(TEXT("%+d%%"), FMath::RoundToInt(Pct));
			}
			return FString::Printf(TEXT("%+.1f%%"), Pct);
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
	// A zero-magnitude roll is a no-op (e.g. a per-rarity tier intentionally left at 0) — suppress the line so the
	// tooltip doesn't render a stray "+0", mirroring UCardEffect_WeaponStat. This also lets the card-level
	// 0-resolution guard (IsDataValid) and the entry widget key consistently off "did any effect emit a line".
	if (FMath::IsNearlyZero(Magnitude))
	{
		return FText::GetEmpty();
	}
	// The card's authored Description carries the human text; this slot shows the rolled numeric value. bShowAsPercent
	// renders fractional-multiplier attributes (damage/crit-chance/pickup/xp) as "+7.5%"; flat attributes as "+15".
	return FText::FromString(FormatCardMagnitude(Magnitude, bShowAsPercent));
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
	// A zero-magnitude stat mod is a no-op — e.g. a rarity tier absent from this effect resolves to 0 via
	// ResolveMagnitude (a Legendary-only sub-effect on an otherwise all-tier card). Suppress its line so the
	// card tooltip doesn't render a stray "+0" for the rarities that intentionally carry no bonus.
	if (FMath::IsNearlyZero(Magnitude))
	{
		return FText::GetEmpty();
	}
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
	if (!Fragment)
	{
		return false;
	}
	UFPSRWeaponInstance* Instance = ResolveTargetInstance(Context);
	// Reject (without consuming the pick) when there's no target instance OR the fragment is already maxed on it —
	// belt-and-suspenders with the draw-time stack gate so a stale/maxed pick never silently wastes a selection.
	return Instance != nullptr && Instance->GetFragmentStackCount(Fragment) < FMath::Max(Fragment->MaxStacks, 1);
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

// ---------------------------------------------------------------------------------------------------------------
// UCardEffect_GrantWeapon — grants a brand-new weapon (U18b).
// ---------------------------------------------------------------------------------------------------------------
void UCardEffect_GrantWeapon::Apply(const FFPSRCardEffectContext& Context, float /*Magnitude*/) const
{
	// Server-authoritative: add the new weapon to the first free slot (CanApply already gated ownership/slot).
	if (WeaponToGrant && Context.Inventory)
	{
		Context.Inventory->AddWeapon(WeaponToGrant);
	}
}

FText UCardEffect_GrantWeapon::GetDescription(ECardRarity /*Rarity*/, float /*Magnitude*/) const
{
	const FText WeaponName = WeaponToGrant ? WeaponToGrant->DisplayName : LOCTEXT("UnknownWeapon", "Weapon");
	return FText::Format(LOCTEXT("UnlockWeaponFmt", "Unlock: {0}"), WeaponName);
}

bool UCardEffect_GrantWeapon::CanApply(const FFPSRCardEffectContext& Context) const
{
	// Transactional: applicable only when a slot is free AND the weapon isn't already owned (else the pick is
	// rejected without being consumed, so the offer stays up — same invariant as the other effects).
	if (!WeaponToGrant || !Context.Inventory)
	{
		return false;
	}
	return Context.Inventory->HasFreeSlot() && !Context.Inventory->GetOwnedWeapons().Contains(WeaponToGrant);
}

#if WITH_EDITOR
void UCardEffect_GrantWeapon::ValidateEffect(FDataValidationContext& Context) const
{
	if (!WeaponToGrant)
	{
		Context.AddError(LOCTEXT("GrantWeaponNoWeapon", "GrantWeapon effect has no WeaponToGrant set."));
	}
}
#endif

// ---------------------------------------------------------------------------------------------------------------
// UCardEffect_CharacterPassive — grants a passive GameplayAbility to the player ASC (U18c).
// ---------------------------------------------------------------------------------------------------------------
void UCardEffect_CharacterPassive::Apply(const FFPSRCardEffectContext& Context, float /*Magnitude*/) const
{
	// Server-authoritative: grant the passive and record its handle on the PlayerState so the run-reset clears it
	// (the ASC survives lobby<->run travel). Stacking is allowed — picking the same passive twice grants two specs
	// (e.g. 2× lifesteal), matching how weapon fragments stack. (CanApply already gated ASC/PS validity.)
	if (!PassiveAbility || !Context.ASC || !Context.PS)
	{
		return;
	}
	const FGameplayAbilitySpec Spec(PassiveAbility, 1, INDEX_NONE, nullptr);
	const FGameplayAbilitySpecHandle Handle = Context.ASC->GiveAbility(Spec);

	const UFPSRPassiveAbility* PassiveCDO = PassiveAbility.GetDefaultObject();
	const bool bListens = PassiveCDO && PassiveCDO->RequiresDealtDamageEvent();
	Context.PS->AddCardGrantedAbility(Handle, bListens);
}

FText UCardEffect_CharacterPassive::GetDescription(ECardRarity /*Rarity*/, float /*Magnitude*/) const
{
	// Passive GAs carry their own flavor; the card's DisplayName is the authored description. Keep this non-empty so
	// the multi-effect aggregator never renders a blank line.
	return LOCTEXT("CharacterPassiveDesc", "Passive ability");
}

bool UCardEffect_CharacterPassive::CanApply(const FFPSRCardEffectContext& Context) const
{
	// Complete precondition: the ASC (grant target) and PlayerState (handle bookkeeping) must resolve.
	return PassiveAbility != nullptr && Context.ASC != nullptr && Context.PS != nullptr;
}

#if WITH_EDITOR
void UCardEffect_CharacterPassive::ValidateEffect(FDataValidationContext& Context) const
{
	if (!PassiveAbility)
	{
		Context.AddError(LOCTEXT("PassiveNoAbility", "Character passive effect has no PassiveAbility set — it grants nothing."));
	}
}
#endif

#undef LOCTEXT_NAMESPACE
