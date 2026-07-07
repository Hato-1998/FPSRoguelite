// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "Card/FPSRCardTypes.h"
#include "Weapon/FPSRWeaponTypes.h"
#include "GameplayTagContainer.h"
#include "FPSRCardEffect.generated.h"

class UGameplayEffect;
class UFPSRWeaponFragment;
class UFPSRPassiveAbility;
class AController;
class AFPSRPlayerState;
class UFPSRAbilitySystemComponent;
class UFPSRWeaponInventoryComponent;
class UFPSRWeaponDataAsset;
#if WITH_EDITOR
class FDataValidationContext;

/** Editor-only magnitude unit classification for the Data Editor's bulk-arithmetic safety (P2). None = the effect
 *  ignores its magnitude at runtime (grant/passive/behavior). Percent = a fractional multiplier (raw 0.05 = +5%);
 *  Flat = a flat value. Additive bulk edits are only allowed across a unit-homogeneous selection (see helpers). */
enum class EFPSREditorMagnitudeUnit : uint8 { None, Percent, Flat };
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

	/** Index (into the target weapon's distinct-fragment list) of the fragment to DROP when granting a new behavior
	 *  fragment to a weapon already at its slot cap (U6 replacement flow). INDEX_NONE = no replacement (plain add /
	 *  under-cap). Server-validated against the resolved target weapon's list — a forged/out-of-range index is rejected. */
	int32 ReplaceFragmentIndex = INDEX_NONE;
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

	/** Editor-tool grid label: a rarity-independent one-line identity of this effect for the Data Editor magnitude
	 *  grid's Summary column (e.g. "Character GE", "Weapon Stat: Damage (all weapons)"). Base = the effect's class
	 *  display name. Override per subclass. Editor-only; no runtime cost. */
	virtual FText GetEditorGridLabel() const;

	/** Editor-tool routing: which closed draw routes this effect PERMITS a card to be placed in. The Data Editor
	 *  intersects this across a card's effects for its wiring preflight (a card in an ineligible route is blocked).
	 *  Declared per subclass so a NEW effect type surfaces in the tool with zero central edits (OCP). Base = empty. */
	virtual TArray<EFPSRCardRoute> GetEditorEligibleRoutes() const;

	/** Editor-tool (P2): the unit this effect's RarityTiers magnitude is expressed in, for the Data Editor's bulk
	 *  arithmetic safety (additive bulk edits require a unit-homogeneous selection). Base = None (magnitude-agnostic
	 *  effect — grant/passive/behavior). Override per subclass that actually reads a numeric magnitude. */
	virtual EFPSREditorMagnitudeUnit GetEditorMagnitudeUnit() const;
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

	/** UI-only: render this effect's magnitude as a percentage (×100, e.g. "+7.5%") instead of a flat value. Set true
	 *  for cards whose GE magnitude is a fractional multiplier (damage / crit-chance / pickup-radius / xp gain); leave
	 *  false for flat attributes (max health, luck, regen). Does not change Apply(). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Card Effect")
	bool bShowAsPercent = false;

	virtual void Apply(const FFPSRCardEffectContext& Context, float Magnitude) const override;
	virtual FText GetDescription(ECardRarity Rarity, float Magnitude) const override;
	virtual bool CanApply(const FFPSRCardEffectContext& Context) const override;
#if WITH_EDITOR
	virtual void ValidateEffect(FDataValidationContext& Context) const override;
	virtual FText GetEditorGridLabel() const override;
	virtual TArray<EFPSRCardRoute> GetEditorEligibleRoutes() const override;
	virtual EFPSREditorMagnitudeUnit GetEditorMagnitudeUnit() const override;
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
#if WITH_EDITOR
	virtual FText GetEditorGridLabel() const override;
	virtual TArray<EFPSRCardRoute> GetEditorEligibleRoutes() const override;
	virtual EFPSREditorMagnitudeUnit GetEditorMagnitudeUnit() const override;
#endif
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
	virtual FText GetEditorGridLabel() const override;
	virtual TArray<EFPSRCardRoute> GetEditorEligibleRoutes() const override;
#endif
};

/** Weapon-unlock effect (U18b): grants a brand-new weapon into a free inventory slot. */
UCLASS(meta = (DisplayName = "Grant Weapon (Unlock)"))
class FPSROGUELITE_API UCardEffect_GrantWeapon : public UFPSRCardEffect
{
	GENERATED_BODY()
public:
	/** The weapon DataAsset added to the player's inventory when this card is picked. */
	UPROPERTY(EditDefaultsOnly, Category = "Effect")
	TObjectPtr<UFPSRWeaponDataAsset> WeaponToGrant = nullptr;

	virtual void Apply(const FFPSRCardEffectContext& Context, float Magnitude) const override;
	virtual FText GetDescription(ECardRarity Rarity, float Magnitude) const override;
	virtual bool RequiresWeapon() const override { return false; }
	virtual bool CanApply(const FFPSRCardEffectContext& Context) const override;
#if WITH_EDITOR
	virtual void ValidateEffect(FDataValidationContext& Context) const override;
	virtual FText GetEditorGridLabel() const override;
	virtual TArray<EFPSRCardRoute> GetEditorEligibleRoutes() const override;
#endif
};

/** Character passive effect (U18c §2-3-5): grants a passive GameplayAbility to the player ASC — lifesteal, an
 *  always-on regen loop, etc. The granted handle is tracked on the PlayerState so the run-reset clears it (the ASC
 *  survives lobby<->run travel). Players are GAS-native, so character behaviors live as abilities, not fragments. */
UCLASS(meta = (DisplayName = "Character: Passive Ability"))
class FPSROGUELITE_API UCardEffect_CharacterPassive : public UFPSRCardEffect
{
	GENERATED_BODY()

public:
	/** Passive ability granted when this card is picked — a UFPSRPassiveAbility subclass (usually a content BP with
	 *  its heal GE / ratio authored). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Card Effect")
	TSubclassOf<UFPSRPassiveAbility> PassiveAbility;

	virtual void Apply(const FFPSRCardEffectContext& Context, float Magnitude) const override;
	virtual FText GetDescription(ECardRarity Rarity, float Magnitude) const override;
	virtual bool CanApply(const FFPSRCardEffectContext& Context) const override;
#if WITH_EDITOR
	virtual void ValidateEffect(FDataValidationContext& Context) const override;
	virtual FText GetEditorGridLabel() const override;
	virtual TArray<EFPSRCardRoute> GetEditorEligibleRoutes() const override;
#endif
};
