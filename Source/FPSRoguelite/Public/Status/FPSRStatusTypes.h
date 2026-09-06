// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "FPSRStatusTypes.generated.h"

class AActor;
class UFPSRWeaponInstance;

/** STAT1 §5-1. Weak = one of the 4 base debuffs (slow / dot / armor-reduction / attack-slow); Strong = one of the 2
 *  combo payoffs (blind / root) a pair of Weak slots feeds into (§5-1's "재료쌍" — see
 *  UFPSRStatusEffectDataAsset::RequiredWeakSlots). Also the axis UFPSRVitalsProfileDataAsset's WeakResistScale /
 *  StrongResistScale (C단계 field) key off — a boss can be immune to hard CC (Strong) while still burning (Weak dot),
 *  which a single resist scale could not express (STAT1 §6 저항 row). */
UENUM(BlueprintType)
enum class EFPSRStatusKind : uint8
{
	Weak,
	Strong
};

/** STAT1 §5-1 / §7-1. Governs what happens when an ALREADY-ACTIVE slot is applied/fired again:
 *  RefreshDuration (default) extends SlotExpiry to NowStatusClock + duration (§10 unit test 1).
 *  Ignore means the reapplication attempt has NO EFFECT AT ALL — not merely "don't extend the timer": the existing
 *  expiry, its retrigger cooldown, and (for a combo re-fire) its two material bits are all left exactly as they
 *  were, as if the attempt never happened. FPSRStatus.cpp's Apply and its internal combo check share this rule. */
UENUM(BlueprintType)
enum class EFPSRStatusRefresh : uint8
{
	RefreshDuration,
	Ignore
};

/** One status effect's static definition (STAT1 §5-1) — a stateless, shared UPrimaryDataAsset (many enemies point at
 *  the same instance; nothing here is per-actor). SlotIndex (0..7) is this status's bit position in the runtime
 *  uint8 bitmask (UFPSREnemyHealthComponent::StatusBits) — the catalog below is the only place that maps a slot
 *  index back to one of these assets, so FPSRStatus's pure functions never hardcode which slot means what; that is
 *  entirely data.
 *
 *  Every effect axis below defaults to a no-op (1.0 multiplier / 0 flat / false) — §5-1's own rule is that each axis
 *  must have exactly one consumer (STAT1 §6's wiring table lists them; wiring itself is C단계). This unit only
 *  stores and resolves the values. */
UCLASS(BlueprintType)
class FPSROGUELITE_API UFPSRStatusEffectDataAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Status.* — content/authoring identity only; FPSRStatus's pure functions never read this (they key off
	 *  SlotIndex). Reserved for a later cosmetic/UI lookup (icon, GMS event payload). */
	UPROPERTY(EditDefaultsOnly, Category = "Status", meta = (Categories = "Status"))
	FGameplayTag StatusTag;

	/** This status's bit position in the runtime bitmask (0..7 — the mask is a uint8). Must be unique within a
	 *  catalog (UFPSRStatusCatalogDataAsset::IsDataValid). */
	UPROPERTY(EditDefaultsOnly, Category = "Status", meta = (ClampMin = "0", ClampMax = "7"))
	uint8 SlotIndex = 0;

	UPROPERTY(EditDefaultsOnly, Category = "Status")
	EFPSRStatusKind Kind = EFPSRStatusKind::Weak;

	/** Base duration BEFORE the target's Kind-matched resist scale multiplies it (WeakResistScale/StrongResistScale,
	 *  C단계 UFPSRVitalsProfileDataAsset fields). FPSRStatus::Apply rejects the application entirely (returns false,
	 *  touches nothing) when the resolved resist is <= 0 — a "0 duration" is never itself the authoring convention
	 *  for immunity; that is the resist's job (§10 unit test 5). */
	UPROPERTY(EditDefaultsOnly, Category = "Status", meta = (ClampMin = "0.01"))
	float DurationSeconds = 5.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Status")
	EFPSRStatusRefresh Refresh = EFPSRStatusRefresh::RefreshDuration;

	// --- Effect axes (STAT1 §6 wiring table — every field below has exactly one C단계 consumer; 1.0/0/false = no-op) ---

	/** C단계 consumer: AFPSREnemyBase::GetEffectiveMoveSpeed() — a straight multiplier at the point of use (둔화). */
	UPROPERTY(EditDefaultsOnly, Category = "Status|Effects", meta = (ClampMin = "0.0", UIMax = "1.0"))
	float MoveSpeedMultiplier = 1.0f;

	/** C단계 consumer: the attack-interval threshold compare in ServerTickAttack (공격속도저하). >1 = slower attacks. */
	UPROPERTY(EditDefaultsOnly, Category = "Status|Effects", meta = (ClampMin = "0.0", UIMax = "3.0"))
	float AttackIntervalMultiplier = 1.0f;

	/** C단계 consumer: UFPSREnemyHealthComponent::ApplyDamage's per-instance mitigation layer (방어력감소). >1 = takes more. */
	UPROPERTY(EditDefaultsOnly, Category = "Status|Effects", meta = (ClampMin = "0.0", UIMax = "3.0"))
	float IncomingDamageMultiplier = 1.0f;

	/** C단계 consumer: the batch pass -> FPSRCombat::ApplyDamage bridge (도트데미지). 0 = this status deals no DoT. */
	UPROPERTY(EditDefaultsOnly, Category = "Status|Effects", meta = (ClampMin = "0.0"))
	float DamagePerSecond = 0.0f;

	/** Applies DamagePerSecond in discrete chunks (DamagePerSecond * this) every this-many seconds, instead of every
	 *  Advance step (§8 회계 — a tick costs a Health dirty-mark + OnHealthChanged broadcast; ticking every frame
	 *  multiplies that by the frame rate for no gameplay benefit). ClampMin 0.05 is a hard floor, not just a UI hint —
	 *  FPSRStatus::Advance also clamps defensively, since an imported/scripted 0 would otherwise turn the tick loop
	 *  into an infinite same-call spin. */
	UPROPERTY(EditDefaultsOnly, Category = "Status|Effects", meta = (ClampMin = "0.05"))
	float DotTickIntervalSeconds = 0.5f;

	/** C단계 consumer: the ranged-attack early-out (실명) — onset releases the ranged hold/token, STAT1 §6. */
	UPROPERTY(EditDefaultsOnly, Category = "Status|Effects")
	bool bDisableAttack = false;

	/** C단계 consumer: the move-speed hook forces 0 AND suppresses knockback impulses (속박), STAT1 §6. */
	UPROPERTY(EditDefaultsOnly, Category = "Status|Effects")
	bool bDisableMovement = false;

	// --- Strong-only: the combo this status fires FROM (STAT1 §5-1) ------------------------------------------------

	/** 🔴 This is a firing MATERIAL PAIR, not a "composition" (STAT1 §5-1 / G1r3): when the Strong status fires, both
	 *  of these Weak slots' bits turn off (if bConsumeSources), so the target stops taking their individual effects
	 *  too. To make a Strong status KEEP one of its ingredients' effects (e.g. "blinded but still slowed"), author
	 *  that value directly on THIS asset's own effect axes above — ingredients are consumed, not merged.
	 *  Must resolve to exactly 2 catalog entries, both Kind == Weak (UFPSRStatusCatalogDataAsset::IsDataValid). */
	UPROPERTY(EditDefaultsOnly, Category = "Status|Strong Only",
		meta = (EditCondition = "Kind == EFPSRStatusKind::Strong", EditConditionHides))
	TArray<uint8> RequiredWeakSlots;

	/** User decision (STAT1 §5-1) — default true: firing this Strong status turns off its 2 material bits. */
	UPROPERTY(EditDefaultsOnly, Category = "Status|Strong Only",
		meta = (EditCondition = "Kind == EFPSRStatusKind::Strong", EditConditionHides))
	bool bConsumeSources = true;

	/** Seconds before this slot can apply/fire again after a successful application (checked against the target's
	 *  own FFPSRStatusServerState::SlotCooldownUntil). Default 0 = "no cooldown, always available" (user decision
	 *  2026-09-06 — the Weak axes are meant to loop: consume -> immediately re-gather -> refire). This is STAT1's
	 *  ONLY lockdown-prevention lever (§3-D) — if PIE shows the swarm locked in Blind/Root permanently, raise THIS
	 *  number on the Strong status, not the code. Hidden for Weak content by default (§3-D expects it tuned on
	 *  Strong statuses), but the runtime mechanism itself is generic per-slot, not Strong-exclusive. */
	UPROPERTY(EditDefaultsOnly, Category = "Status|Strong Only",
		meta = (ClampMin = "0.0", EditCondition = "Kind == EFPSRStatusKind::Strong", EditConditionHides))
	float RetriggerCooldownSeconds = 0.0f;
};

/** STAT1 §5-2. The full authored set of statuses one enemy archetype can be affected by. A stateless, shared
 *  UPrimaryDataAsset — FPSRStatus's pure functions take it BY REFERENCE as a plain function argument (never resolved
 *  from a hardcoded path here, 핵심원칙 2) so where/how a caller obtains one is entirely a C단계 wiring concern. */
UCLASS(BlueprintType)
class FPSROGUELITE_API UFPSRStatusCatalogDataAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, Category = "Status")
	TArray<TObjectPtr<UFPSRStatusEffectDataAsset>> Statuses;

#if WITH_EDITOR
	/** STAT1 §5-2: (1) duplicate SlotIndex (2) SlotIndex > 7 (3) a Strong entry's RequiredWeakSlots isn't exactly 2,
	 *  or doesn't resolve to 2 Kind==Weak entries in THIS catalog (4) two different Strong entries share the same
	 *  (unordered) material pair (5) a Weak entry has a non-empty RequiredWeakSlots (authoring leftover — it can
	 *  never be read) (6) more than one entry has DamagePerSecond > 0 — warning only (§7-4's single-DoT-source
	 *  premise: FPSRStatus::Advance picks the first active match in array order; a second DoT-bearing status
	 *  wouldn't hard-fail, it would just silently never tick while the first stays active). */
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};

/** STAT1 §5-3. Server-only per-actor status runtime state — deliberately NOT a USTRUCT (mirrors
 *  Combat/FPSRVitals.h's FFPSRDamageSpec/FPool/FMitigation: never replicated, never Blueprint-exposed, so paying for
 *  UHT reflection here buys nothing). Not POD either — the two TWeakObjectPtr members need real construction/
 *  destruction, same precedent as a plain (non-UPROPERTY) TWeakObjectPtr member elsewhere in this codebase
 *  (Weapon/FPSRProjectileTypes.h's FFPSRFireContext::WeaponInstance, Enemy/FPSREnemySpawnSubsystem.h's
 *  LastGroundedZByPlayer map) — a weak reference needs no GC tracking to stay safe, so it needs no UPROPERTY either. */
struct FFPSRStatusServerState
{
	/** Expiry timestamp per slot, on the STATUS clock (AFPSRGameState::GetStatusClockSeconds — STAT1 §6-1), NOT the
	 *  combat clock. Meaningless while the slot's bit is 0. */
	float SlotExpiry[8] = {};

	/** Retrigger-cooldown timestamp per slot, same clock axis. A slot may apply/fire again once
	 *  NowStatusClock >= this (FPSRStatus::Apply §7-1 / its internal combo check). Default 0 everywhere = never
	 *  blocks — RetriggerCooldownSeconds > 0 on the content is what raises it. */
	float SlotCooldownUntil[8] = {};

	/** The status clock time of this actor's last Advance step — the start of "the interval since we last looked"
	 *  (§7-4). 0 until the first status ever lands on this life (FPSRStatus::Apply's dormant-wake anchor re-stamps
	 *  this the moment a fully-idle actor's first bit goes up). MUST be reset at every §7-6 closure point, or a
	 *  reused actor's stale value turns its first DoT step into a multi-second burst. */
	float LastStatusStepClock = 0.0f;

	/** Seconds remaining until DoT's next discrete tick (§7-4) — counts DOWN, <= 0 fires (and rolls the remainder
	 *  forward, so one unusually large Advance step still pays out the right NUMBER of ticks, not one lump sum). */
	float DotAccumulator = 0.0f;

	/** Kill-credit for a DoT tick (STAT1 §7-1 "마지막 시전자 승계" — the most recent successful ApplyStatus caller
	 *  wins, regardless of which slot they applied; there is only one DoT source active at a time by catalog
	 *  convention, so one shared instigator is enough). Null-safe: if the instigator goes away mid-DoT, the DoT
	 *  keeps ticking with a null instigator rather than stopping (§7-4). */
	TWeakObjectPtr<AActor> DotInstigator;

	/** So a DoT kill can still build FFPSRFireContext for FPSRWeaponHooks::NotifyStatusKill (C단계) — without this
	 *  the kill-credit hook has no weapon instance to attribute the kill to and is a permanent no-op (STAT1 G1-15). */
	TWeakObjectPtr<UFPSRWeaponInstance> DotSourceWeapon;
};

/** STAT1 §5-3/§7-5 — the per-frame-cheap CACHE FPSRStatus::Resolve folds StatusBits into, so a hot per-hit/per-tick
 *  consumer (move speed, attack interval, incoming damage, attack/move disable) never re-walks the catalog. Mirrors
 *  Combat/FPSRVitals.h's FResult: plain struct, never replicated/Blueprint-exposed (only StatusBits itself
 *  replicates, §5-3) — every consumer in §6's wiring table is a SERVER authoritative code path, so a client has no
 *  legitimate reader for this and UHT reflection would buy nothing.
 *
 *  🔴 명세 갭 (see the STAT1 B단계 implementation report): STAT1.md never spells out this struct's field list — §5-3
 *  says only "FFPSRResolvedStatus — §5-3", with no members shown. This shape is reverse-engineered from §5-1's 6
 *  effect axes intersected with §7-5's "배율은 곱, 불리언은 OR" and §10 unit test 6's wording. DamagePerSecond is
 *  deliberately EXCLUDED: DoT already has its own dedicated, stateful accumulation path
 *  (FFPSRStatusServerState::DotAccumulator via FPSRStatus::Advance's OutDotDamage) that consumes elapsed TIME, not
 *  just current bits — it isn't a "resolve cache" operation, and the catalog's single-DoT-source validation warning
 *  assumes exactly one active source rather than a combinable multi-source total. */
struct FFPSRResolvedStatus
{
	float MoveSpeedMultiplier = 1.0f;
	float AttackIntervalMultiplier = 1.0f;
	float IncomingDamageMultiplier = 1.0f;
	bool bDisableAttack = false;
	bool bDisableMovement = false;
};
