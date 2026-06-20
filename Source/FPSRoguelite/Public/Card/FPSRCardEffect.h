// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "Card/FPSRCardTypes.h"
#include "Weapon/FPSRWeaponTypes.h"
#include "GameplayTagContainer.h"
#include "FPSRCardEffect.generated.h"

class UGameplayEffect;
class UFPSRWeaponFragment;
class AController;
class AFPSRPlayerState;
class UFPSRAbilitySystemComponent;
class UFPSRWeaponInventoryComponent;
class UFPSRWeaponDataAsset;
#if WITH_EDITOR
class FDataValidationContext;
#endif

/** Server-side context for applying a card effect. Built on the server from the selecting player; never replicated
 *  (effects live as inline subobjects of an always-loaded card asset and never cross the wire). */
struct FFPSRCardEffectContext
{
	AController* Player = nullptr;
	AFPSRPlayerState* PS = nullptr;
	UFPSRAbilitySystemComponent* ASC = nullptr;
	UFPSRWeaponInventoryComponent* Inventory = nullptr;
	/** The weapon this card targets (weapon-group cards). null for character/all-weapons effects. */
	UFPSRWeaponDataAsset* TargetWeapon = nullptr;
};

/**
 * Polymorphic card effect (U18a, §2-3-1). A card owns an Instanced array of these; ApplyCard loops them
 * effect-type-agnostically (Apply / ResolveMagnitude). A NEW effect type = one subclass (~40 lines), zero central
 * edits (OCP / extensibility directive). Inline subobject of the card DataAsset — never replicated.
 */
UCLASS(Abstract, EditInlineNew, DefaultToInstanced, CollapseCategories)
class FPSROGUELITE_API UFPSRCardEffect : public UObject
{
	GENERATED_BODY()

public:
	/** Per-rarity magnitude for THIS effect. A card rolls one rarity (Card->OfferRarities); each effect reads its
	 *  own magnitude here — so two effects on one card can scale differently ("fire rate + / damage -"). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Card Effect")
	TArray<FFPSRCardRarityTier> RarityTiers;

	/** Magnitude for the given rolled rarity (0 if this effect declares no tier for it). The single source of an
	 *  effect's numeric value — Apply/GetDescription receive it pre-resolved so the central loop stays type-agnostic. */
	float ResolveMagnitude(ECardRarity Rarity) const;

	/** Server: apply this effect to the player. Magnitude is pre-resolved by the caller (ResolveMagnitude(Draw.Rarity)).
	 *  Must not fail once CanApply() returned true (the apply pass is transactional — see UFPSRCardSubsystem::ApplyCard). */
	virtual void Apply(const FFPSRCardEffectContext& Context, float Magnitude) const PURE_VIRTUAL(UFPSRCardEffect::Apply, );

	/** UI: a short auto-generated description line for this effect at the rolled rarity/magnitude (§2-3-8). */
	virtual FText GetDescription(ECardRarity Rarity, float Magnitude) const;

	/** Draw-time gate: does the player need to own a weapon for this effect to be meaningful? Mirrors v1's
	 *  "weapon-scope cards join the pool only once a weapon is owned" — effect-based so the routing is unchanged. */
	virtual bool RequiresWeapon() const { return false; }

	/** Apply-time gate: can this effect apply right now? Must be a COMPLETE precondition check (ASC/inventory/target
	 *  resolvable) so Apply() cannot fail afterwards — preserves the v1 reject-without-consume contract. */
	virtual bool CanApply(const FFPSRCardEffectContext& Context) const { return true; }

	/** Forward-compat elemental seam (§2-3-7): empty = Physical. U18a ships no elemental subclass. */
	virtual FGameplayTag GetDamageTypeTag() const { return FGameplayTag(); }

#if WITH_EDITOR
	/** Editor validation of this effect's OWN fields. Append errors/warnings to Context. */
	virtual void ValidateEffect(FDataValidationContext& Context) const {}
#endif
};

/** Character effect: applies a GameplayEffect to the player ASC (SetByCaller.CardMagnitude). v1 Character scope. */
UCLASS(meta = (DisplayName = "Character: Gameplay Effect"))
class FPSROGUELITE_API UCardEffect_CharacterGE : public UFPSRCardEffect
{
	GENERATED_BODY()

public:
	/** GE applied to the player ASC. Author the magnitude modifier as Set By Caller (tag SetByCaller.CardMagnitude)
	 *  so the rolled-rarity magnitude applies from a single GE + single card (§2-3-1). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Card Effect")
	TSubclassOf<UGameplayEffect> Effect;

	virtual void Apply(const FFPSRCardEffectContext& Context, float Magnitude) const override;
	virtual FText GetDescription(ECardRarity Rarity, float Magnitude) const override;
	virtual bool CanApply(const FFPSRCardEffectContext& Context) const override;
#if WITH_EDITOR
	virtual void ValidateEffect(FDataValidationContext& Context) const override;
#endif
};

/** Weapon stat effect: a numeric stat modifier. bThisWeaponOnly = the card's target weapon; otherwise all weapons
 *  (PlayerState). v1 ThisWeapon (stat) / AllWeapons scope. */
UCLASS(meta = (DisplayName = "Weapon: Stat Modifier"))
class FPSROGUELITE_API UCardEffect_WeaponStat : public UFPSRCardEffect
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Card Effect")
	EFPSRWeaponStat Stat = EFPSRWeaponStat::FireRate;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Card Effect")
	EFPSRWeaponModOp Op = EFPSRWeaponModOp::PercentMultiply;

	/** true = applies only to the card's target weapon (this-weapon, larger numbers). false = applies to every owned
	 *  weapon via the PlayerState (all-weapons, smaller numbers; respects each weapon's AllWeaponsStatExclusions). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Card Effect")
	bool bThisWeaponOnly = true;

	virtual void Apply(const FFPSRCardEffectContext& Context, float Magnitude) const override;
	virtual FText GetDescription(ECardRarity Rarity, float Magnitude) const override;
	virtual bool RequiresWeapon() const override { return true; }
	virtual bool CanApply(const FFPSRCardEffectContext& Context) const override;
};

/** Weapon behavior effect: grants a behavior fragment to the card's target weapon instance. v1 ThisWeapon +
 *  GrantedFragment. Behavior cards stay mission-reward only in U18a (routing reshuffle is U18b). */
UCLASS(meta = (DisplayName = "Weapon: Behavior Fragment"))
class FPSROGUELITE_API UCardEffect_WeaponBehavior : public UFPSRCardEffect
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Card Effect")
	TObjectPtr<UFPSRWeaponFragment> Fragment = nullptr;

	virtual void Apply(const FFPSRCardEffectContext& Context, float Magnitude) const override;
	virtual FText GetDescription(ECardRarity Rarity, float Magnitude) const override;
	virtual bool RequiresWeapon() const override { return true; }
	virtual bool CanApply(const FFPSRCardEffectContext& Context) const override;
#if WITH_EDITOR
	virtual void ValidateEffect(FDataValidationContext& Context) const override;
#endif
};
