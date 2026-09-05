// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "Weapon/FPSRWeaponTypes.h"
#include "Combat/FPSRCritTypes.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif
#include "FPSRWeaponFragment.generated.h"

class APawn;
class AActor;
class AController;
class UWorld;
class UFPSRWeaponInstance;
class UAbilitySystemComponent;
struct FFPSRProjectileParams;

/**
 * Transient per-activation firing context passed to weapon behavior-fragment hooks. Plain struct (not a
 * USTRUCT) — never replicated or reflected, lives only on the stack during a single fire ability activation,
 * so hooks stay allocation-free.
 */
struct FFPSRFireContext
{
	APawn* Avatar = nullptr;
	AController* Controller = nullptr;
	UWorld* World = nullptr;
	UFPSRWeaponInstance* Instance = nullptr;

	/** Number of shots/traces this activation. ModifyShotCount adjusts it (clamped by the ability). */
	int32 ShotCount = 1;

	/** True on the server (authority) — hooks that mutate game state must gate on this. */
	bool bAuthority = false;

	/** Set by the NoSelfDamage card (PreFire): suppress instigator self-damage on this activation's explosions.
	 *  Knockback stays on (rocket jump without self-harm). Baked into projectile bSelfDamage / hitscan-AOE bAllowSelf. */
	bool bSuppressSelfDamage = false;
};

/**
 * Weapon behavior fragment (Game.MD §2-4-1 ②): a data-driven, stateless modifier that changes how a weapon
 * fires via composable hooks (multishot, pierce, healing beam, …). The behavior is a C++ subclass; the tuning
 * values live on the authored DataAsset instance. A UFPSRWeaponInstance accumulates references to these shared
 * assets (no per-instance state) — hooks run once per fire (Pre/ModifyShotCount/Post) or per hit (OnHitActor,
 * a handful per shot), so virtual dispatch stays cheap and hits never allocate.
 */
UCLASS(Abstract, BlueprintType)
class FPSROGUELITE_API UFPSRWeaponFragment : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Fragment")
	FText DisplayName;

	/** Optional identity tag (dedup / future "swap" logic). Identity for HasFragment is the asset pointer. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Fragment")
	FGameplayTag FragmentTag;

	/** How many times this fragment can be stacked on one weapon. Each stack re-applies the hooks (e.g.
	 *  MultiShot's ExtraShots adds per stack), and the mission-reward offer keeps presenting the card until
	 *  the weapon holds MaxStacks copies. 1 = single pick (default). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Fragment", meta = (ClampMin = "1"))
	int32 MaxStacks = 1;

	/** Hook surface (default no-ops). OnProjectileSpawn mutates AOE projectile params; ModifyChargeTime adjusts
	 *  the ChargeLaser charge-up duration. */
	virtual void PreFire(FFPSRFireContext& Context) const {}
	virtual void ModifyShotCount(FFPSRFireContext& Context) const {}
	virtual void OnHitActor(const FFPSRFireContext& Context, AActor* HitActor, float& DamageInOut) const {}
	virtual void PostFire(const FFPSRFireContext& Context) const {}

	/** Projectile-spawn hook (AOE archetypes): mutate the projectile spawn params before it is acquired. */
	virtual void OnProjectileSpawn(const FFPSRFireContext& Context, FFPSRProjectileParams& ParamsInOut) const {}

	/** Charge-time hook (ChargeLaser): adjust the seconds-to-full-charge before the charge alpha is computed. */
	virtual void ModifyChargeTime(const FFPSRFireContext& Context, float& ChargeTimeInOut) const {}

	/** Fire-mode hook: a fragment can change the RESOLVED fire mode / burst count (e.g. FullAuto -> Burst). Applied in
	 *  UFPSRWeaponInstance::RecomputeResolved so the fire component (which reads resolved stats) picks it up with no
	 *  extra wiring. Stateless; runs only on stat re-resolution, not on the hot per-hit path. */
	virtual void ModifyFireMode(EFPSRFireMode& FireModeInOut, int32& BurstCountInOut) const {}

	/** Hitscan impact hook (server-only): called at each terminal impact point of a hitscan pellet so a fragment can
	 *  spawn an effect at the hit — e.g. ExplosiveRounds turns a rifle hit into a small AOE. bAllowSelf passes through
	 *  the NoSelfDamage suppression so a spawned explosion respects it. bOutHitEnemy: set true if this hook dealt real
	 *  damage to an enemy (e.g. a splash that connected) so the activation isn't counted as a miss — the caller OR-s
	 *  it across fragments before deciding OnMiss. */
	virtual void OnImpact(const FFPSRFireContext& Context, const FVector& ImpactPoint, bool bAllowSelf, bool& bOutHitEnemy) const {}

	/** Behavior-trigger hooks (U18c, §2-3-5). Fired by the shared FPSRWeaponHooks bridge from all 5 damage paths so
	 *  a fragment reacts to firing outcomes uniformly. All state-mutating overrides MUST gate on Context.bAuthority
	 *  (mirror UFPSRFragment_ExplosiveRounds::OnImpact) — these run on the hot damage path, so keep them allocation-free.
	 *  - OnAim   : ADS pressed/released (server-authoritative aiming RPC). bAiming = entering ADS.
	 *  - OnFire  : once per activation, right after the ammo commit (NOT PostFire — avoids the on-fire/on-hit race).
	 *  - OnMiss  : once per activation that landed no damage on any enemy (synchronous paths only).
	 *  - OnKill  : once per enemy this activation freshly killed (alive->dead; corpse re-hits excluded by bJustKilled).
	 *  - OnStatusKill : empty seam — D3 (status-effect kills) wires the call site; declared here only. */
	virtual void OnAim(const FFPSRFireContext& Context, bool bAiming) const {}
	virtual void OnFire(const FFPSRFireContext& Context) const {}
	virtual void OnMiss(const FFPSRFireContext& Context) const {}
	virtual void OnKill(const FFPSRFireContext& Context, AActor* KilledActor) const {}
	virtual void OnStatusKill(const FFPSRFireContext& Context, AActor* KilledActor) const {}

	/** Crit-rule hook (CRIT1): runs once per activation, right before the fire ability bakes its FFPSRCritContext —
	 *  a "resolution stage" hook at the same level as ModifyFireMode, not a per-hit one (cards 1/2/4 live here).
	 *  ⚠️ Stack-composition rule (fixed, G1 P2-5): ActiveFragments holds one element PER STACK (the same convention
	 *  MultiShot's per-stack ExtraShots relies on), so an override MUST combine as: ratios/adds with `+=`, bools
	 *  with `|=`, and HealEffect only if it is still unset (first one wins). */
	virtual void ModifyCrit(const FFPSRFireContext& Context, FFPSRCritContext& CritInOut) const {}

	/** Reload-COMPLETE hook (a cancelled reload via CancelReload never fires this). Server-authoritative. */
	virtual void OnReloadFinished(const FFPSRFireContext& Context) const {}

	/** Slide-ENTRY hook (rising edge only, fired once per slide). Server-authoritative. */
	virtual void OnSlideStarted(const FFPSRFireContext& Context) const {}
};

/**
 * Shared behavior-hook bridge (U18c §2-3-5). One choke point each so every damage path (Hitscan / ChargeLaser /
 * Melee / Projectile / Explosion) fires the trigger hooks identically instead of re-deriving the fragment list.
 * Each helper resolves the active fragments from Context.Instance and early-outs when there are none — empty-fast on
 * the hot path. The hooks themselves gate on Context.bAuthority; callers already invoke these inside server-only scopes.
 */
namespace FPSRWeaponHooks
{
	/** Fire OnFire on every active fragment (once per activation). */
	FPSROGUELITE_API void NotifyFire(const FFPSRFireContext& Context);
	/** Fire OnMiss on every active fragment (activation dealt no enemy damage). */
	FPSROGUELITE_API void NotifyMiss(const FFPSRFireContext& Context);
	/** Fire OnKill on every active fragment for one freshly-killed enemy. */
	FPSROGUELITE_API void NotifyKill(const FFPSRFireContext& Context, AActor* KilledActor);
	/** Fire OnAim on every active fragment (ADS press/release). */
	FPSROGUELITE_API void NotifyAim(const FFPSRFireContext& Context, bool bAiming);

	/** The ONLY place a crit context gets built (CRIT1): combines the ASC's global crit attributes with the weapon
	 *  instance's active timed buffs, then runs every active fragment's ModifyCrit over the result. ASC null ->
	 *  {Chance 0, Multiplier 1} (an enemy projectile / non-GAS instigator never crits — the existing contract). */
	FPSROGUELITE_API FFPSRCritContext BuildCritContext(const FFPSRFireContext& Context, const UAbilitySystemComponent* ASC);

	/** Notify the current weapon's fragments that a reload just completed. There is no live activation to carry a
	 *  context, so this synthesizes one (same shape as AFPSRCharacter::ServerSetAiming_Implementation's OnAim one). */
	FPSROGUELITE_API void NotifyReloadFinished(APawn* Avatar, UFPSRWeaponInstance* Instance);

	/** Notify a slide entry. Resolves Avatar -> inventory -> the CURRENTLY EQUIPPED instance itself (keeps the CMC
	 *  from having to know about weapons at all — the call site is one line). No-op without authority.
	 *  ⚠️ Deliberate boundary: the buff only lands on whichever weapon was in hand AT the moment of the slide — sliding
	 *  with an SMG equipped gives the rifle nothing (matches the card's "while holding this weapon" wording). */
	FPSROGUELITE_API void NotifySlideStarted(APawn* Avatar);
}

/** Reference fragment: fires extra shots/pellets per activation (e.g. 2-round multishot, shotgun spread). */
UCLASS()
class FPSROGUELITE_API UFPSRFragment_MultiShot : public UFPSRWeaponFragment
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Fragment", meta = (ClampMin = "1"))
	int32 ExtraShots = 1;

	virtual void ModifyShotCount(FFPSRFireContext& Context) const override
	{
		Context.ShotCount += ExtraShots;
	}
};

/** Reference fragment: flat bonus damage applied per hit (exercises the per-hit OnHitActor hook). */
UCLASS()
class FPSROGUELITE_API UFPSRFragment_OnHitBonusDamage : public UFPSRWeaponFragment
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Fragment")
	float BonusDamage = 10.0f;

	virtual void OnHitActor(const FFPSRFireContext& Context, AActor* HitActor, float& DamageInOut) const override
	{
		DamageInOut += BonusDamage;
	}
};

/** Card A — NoSelfDamage: suppress the instigator's self-damage from explosions for this weapon (the auto-damage
 *  of self-fired AOE / explosive rounds is nullified). Knockback is unaffected, so rocket-jumping still works. */
UCLASS()
class FPSROGUELITE_API UFPSRFragment_NoSelfDamage : public UFPSRWeaponFragment
{
	GENERATED_BODY()

public:
	virtual void PreFire(FFPSRFireContext& Context) const override
	{
		Context.bSuppressSelfDamage = true;
	}
};

/** Card B — ExplosiveRounds: turns each hitscan impact into a small radial explosion (rifle → splash). Damage and
 *  knockback follow the same self/friendly rules as any explosion (the spawned blast is server-authoritative). */
UCLASS()
class FPSROGUELITE_API UFPSRFragment_ExplosiveRounds : public UFPSRWeaponFragment
{
	GENERATED_BODY()

public:
	/** Radius of the per-impact explosion (cm). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Fragment", meta = (ClampMin = "0"))
	float AOERadius = 150.0f;

	/** Damage dealt at the center of each per-impact explosion (before self/friendly resolution). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Fragment", meta = (ClampMin = "0"))
	float AOEDamage = 20.0f;

	/** Radial knockback impulse of the per-impact explosion (cm/s); 0 = none. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Fragment", meta = (ClampMin = "0"))
	float KnockbackStrength = 0.0f;

	virtual void OnImpact(const FFPSRFireContext& Context, const FVector& ImpactPoint, bool bAllowSelf, bool& bOutHitEnemy) const override;
};

/** Feature B (U18c): refill magazine rounds whenever an activation lands NO damage on any enemy — "suppressive
 *  fire pays off" (e.g. LMG miss → top up). Fires on the OnMiss trigger, server-authoritative. */
UCLASS()
class FPSROGUELITE_API UFPSRFragment_AmmoOnMiss : public UFPSRWeaponFragment
{
	GENERATED_BODY()

public:
	/** Rounds added back to the magazine on each miss (clamped to MagSize). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Fragment", meta = (ClampMin = "1"))
	int32 RefillAmount = 1;

	virtual void OnMiss(const FFPSRFireContext& Context) const override;
};

/** Feature C (U18c): reload on kill — a freshly-killed enemy refills this weapon (shotgun / bazooka payoff). Fires
 *  on the OnKill trigger (alive->dead transition only), server-authoritative. bInstantRefill tops the mag to full
 *  immediately; otherwise it kicks off the weapon's normal timed reload. */
UCLASS()
class FPSROGUELITE_API UFPSRFragment_ReloadOnKill : public UFPSRWeaponFragment
{
	GENERATED_BODY()

public:
	/** true = instantly fill the magazine to MagSize on a kill; false = start the weapon's timed reload instead. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Fragment")
	bool bInstantRefill = true;

	virtual void OnKill(const FFPSRFireContext& Context, AActor* KilledActor) const override;
};

/** Burst-fire fragment (task 1): converts the weapon's fire mode to Burst so one trigger pull fires BurstCount rounds.
 *  Replaces the removed dedicated Burst-Rifle weapon — the rifle stays FullAuto by default and unlocks this as a
 *  mission feature. Applied via ModifyFireMode during stat resolution; the fire component reads the resolved mode. */
UCLASS()
class FPSROGUELITE_API UFPSRFragment_BurstFire : public UFPSRWeaponFragment
{
	GENERATED_BODY()

public:
	/** Rounds fired per trigger pull once this fragment is active. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Fragment", meta = (ClampMin = "2"))
	int32 BurstCount = 3;

	virtual void ModifyFireMode(EFPSRFireMode& FireModeInOut, int32& BurstCountInOut) const override
	{
		FireModeInOut = EFPSRFireMode::Burst;
		BurstCountInOut = FMath::Max(2, BurstCount);
	}
};

/** Behavior-less MARKER fragment (W-U2): adds NO fire behavior — every base hook stays a no-op. It exists ONLY to be
 *  detected by a read-only stack count, i.e. a part slot's EvolutionFragment (FFPSRWeaponPartAttachment): an unlock
 *  card (UCardEffect_WeaponBehavior) grants this marker, and the weapon's part evolution(스택 진화) then swaps the
 *  sight to a scope while it is held — a "저격줌 강화" unlock. Keeps the §2-A isolation contract (parts read fragments
 *  read-only; this fragment never touches gameplay/fire). Author DisplayName per use (e.g. "저격 스코프 언락"). */
UCLASS()
class FPSROGUELITE_API UFPSRFragment_Marker : public UFPSRWeaponFragment
{
	GENERATED_BODY()
};

/** CRIT1 card 1 — Critical Overkill: re-lands a fraction of a crit's actual damage on the same target as a second
 *  instance. That second instance is not itself a crit and never re-triggers a rider (deliberately interacts with
 *  lifesteal / OnHitActor-style fragments — user decision 2026-09-05). */
UCLASS()
class FPSROGUELITE_API UFPSRFragment_CritBonusInstance : public UFPSRWeaponFragment
{
	GENERATED_BODY()
public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Fragment", meta = (ClampMin = "0.0"))
	float BonusRatio = 0.5f;
	virtual void ModifyCrit(const FFPSRFireContext& Context, FFPSRCritContext& CritInOut) const override;
};

/** CRIT1 card 2 — Critical Leech. The heal GE can be the same asset an existing lifesteal card already uses (both
 *  read the same SetByCaller contract). */
UCLASS()
class FPSROGUELITE_API UFPSRFragment_CritLifesteal : public UFPSRWeaponFragment
{
	GENERATED_BODY()
public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Fragment", meta = (ClampMin = "0.0"))
	float HealRatio = 0.10f;
	/** Instant heal GE. Null = the heal quietly no-ops (an unauthored DA can't break the build or a smoke test). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Fragment")
	TSubclassOf<UGameplayEffect> HealEffect;
	virtual void ModifyCrit(const FFPSRFireContext& Context, FFPSRCritContext& CritInOut) const override;

#if WITH_EDITOR
	/** A HealRatio with no HealEffect is a card that heals nothing and says nothing (the runtime silently
	 *  no-ops). The whole point of this seam is that the failure is loud at authoring time (G2 P3). */
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif

};

/** CRIT1 card 3 — Reload Rush. */
UCLASS()
class FPSROGUELITE_API UFPSRFragment_CritOnReload : public UFPSRWeaponFragment
{
	GENERATED_BODY()
public:
	/** Absolute add to crit CHANCE (0.20 = +20%p — same unit as the existing CritChance card). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Fragment", meta = (ClampMin = "0.0"))
	float CritChanceAdd = 0.20f;
	/** Absolute add to crit MULTIPLIER (0.20 = 1.5 -> 1.7 — same unit as the existing CritMult card). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Fragment", meta = (ClampMin = "0.0"))
	float CritMultiplierAdd = 0.20f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Fragment", meta = (ClampMin = "0.0"))
	float Duration = 5.0f;
	virtual void OnReloadFinished(const FFPSRFireContext& Context) const override;

#if WITH_EDITOR
	/** A timed buff REFRESHES on re-application, so a second stack would silently add nothing — that contradicts
	 *  the base class's "each stack re-applies the hooks" contract (G2 P2). Reject MaxStacks > 1 at authoring time
	 *  rather than shipping a knob that reads as supported and isn't. */
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif

};

/** CRIT1 card 4 — Weakpoint Precision. No tunable magnitude (a marker-shaped rule). */
UCLASS()
class FPSROGUELITE_API UFPSRFragment_WeakpointAlwaysCrit : public UFPSRWeaponFragment
{
	GENERATED_BODY()
public:
	virtual void ModifyCrit(const FFPSRFireContext& Context, FFPSRCritContext& CritInOut) const override;
};

/** CRIT1 card 5 — Slide Focus. */
UCLASS()
class FPSROGUELITE_API UFPSRFragment_CritOnSlide : public UFPSRWeaponFragment
{
	GENERATED_BODY()
public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Fragment", meta = (ClampMin = "0.0"))
	float CritChanceAdd = 0.40f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Fragment", meta = (ClampMin = "0.0"))
	float Duration = 5.0f;
	virtual void OnSlideStarted(const FFPSRFireContext& Context) const override;

#if WITH_EDITOR
	/** A timed buff REFRESHES on re-application, so a second stack would silently add nothing — that contradicts
	 *  the base class's "each stack re-applies the hooks" contract (G2 P2). Reject MaxStacks > 1 at authoring time
	 *  rather than shipping a knob that reads as supported and isn't. */
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif

};
