// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Combat/FPSRVitals.h"
#include "Combat/FPSRVitalsProfile.h"
#include "Status/FPSRStatusTypes.h"
#include "FPSREnemyHealthComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FFPSREnemyDeathSignature, AActor*, DeadActor, AActor*, Killer);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FFPSREnemyHealthChangedSignature, float, NewHealth, float, MaxHealth);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FFPSREnemyDeathCosmeticSignature);
/** VIT1: fires on the client shield RepNotify's break edge (Shield >0 -> 0), mirroring OnDeathCosmetic's shape —
 *  no payload, the widget re-reads GetShield()/GetMaxShield() itself. Zero new replication (§7 — a RepNotify on the
 *  already-replicated Shield). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FFPSREnemyShieldBrokenSignature);

/** Lightweight, non-GAS health for swarm enemies. Server-authoritative; damage applied via the GAS->bridge. */
UCLASS(ClassGroup = (FPSR), meta = (BlueprintSpawnableComponent))
class FPSROGUELITE_API UFPSREnemyHealthComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UFPSREnemyHealthComponent();

	/** Server: apply damage and handle death. Returns this hit's actual vitals outcome (shield/health spent, whether
	 *  the shield broke) so the caller (FPSRCombat::ApplyDamage) can drive hit-markers / lifesteal / kill-credit off
	 *  the real numbers instead of a before/after Health diff.
	 *  🔴 VIT1 signature change: the trailing `FGameplayTag DamageType` is replaced by `FFPSRDamageSpec` (adds the
	 *  anti-shield multiplier); the return type changes from void to FPSRVitals::FResult. */
	FPSRVitals::FResult ApplyDamage(float DamageAmount, AActor* DamageInstigator, const FFPSRDamageSpec& Spec = FFPSRDamageSpec());

	/** Server: reset health/dead flag for pooled reuse. VIT1: also resets the shield pool to full and re-anchors the
	 *  regen state (mirrors the fresh-spawn anchor rule in AFPSRCharacter — one shared rule, two storages). */
	void ResetForReuse();

	/** Server: (re)initialize the health pool to NewMaxHealth (sets MaxHealth and full Health, clears dead). Used by
	 *  content-driven actors that size their health at runtime — e.g. the U3 boss applies its definition's value.
	 *  Swarm enemies don't call this (they author MaxHealth as the editor default). No-op off-authority / <= 0.
	 *  🔴 VIT1 regression trap 7: this legacy entry point EXPLICITLY zeroes the shield pool (MaxShield/Shield and the
	 *  regen anchors) so a boss / AFPSRDestructible calling this never inherits a leftover shield from a prior
	 *  InitializeVitals life on the same pooled actor. */
	void InitializeMaxHealth(float NewMaxHealth);

	/** Server: (re)initialize BOTH layers from a profile x deck resolution baked once at spawn. Called by the spawn
	 *  subsystem right after Activate()/ResetForReuse() so the new spec overwrites the stale/default pool
	 *  (VIT1 §8 — order matters). No-op off-authority or MaxHealth <= 0. */
	void InitializeVitals(const FFPSRResolvedVitals& Resolved);

	UFUNCTION(BlueprintPure, Category = "FPSR|Enemy")
	float GetHealth() const { return Health; }

	UFUNCTION(BlueprintPure, Category = "FPSR|Enemy")
	float GetMaxHealth() const { return MaxHealth; }

	UFUNCTION(BlueprintPure, Category = "FPSR|Enemy")
	float GetShield() const { return Shield; }

	UFUNCTION(BlueprintPure, Category = "FPSR|Enemy")
	float GetMaxShield() const { return MaxShield; }

	/** Requirement 4 / sibling units (e.g. "shield up = status-effect resist"): true/false is derived from the
	 *  replicated Shield/MaxShield, so server and client always agree with no extra replication. */
	UFUNCTION(BlueprintPure, Category = "FPSR|Enemy")
	bool IsShieldBroken() const { return FPSRVitals::IsShieldBroken(Shield, MaxShield); }

	/** Server: settle the delayed shield regen up to right now. ApplyDamage's entry already calls this, so gameplay
	 *  code never needs to — only an external query (HUD debug, a status-effect check) that wants the CURRENT value
	 *  rather than the value as of the last hit. Idempotent; a no-op off-authority. */
	void CatchUpShieldRegen();

	UFUNCTION(BlueprintPure, Category = "FPSR|Enemy")
	bool IsDead() const { return bDead; }

	/** True if this owner counts as an ENEMY for combat credit (kill markers / kill triggers / on-damage GAS event
	 *  such as lifesteal). A destructible non-enemy (a door) sets this false: it still takes/loses health and is
	 *  destroyed, but breaking it never fires on-kill fragments, kill credit, or lifesteal (see FPSRCombat::ApplyDamage). */
	UFUNCTION(BlueprintPure, Category = "FPSR|Enemy")
	bool CountsAsKill() const { return bCountsAsKill; }

	/** Server/setup: set whether this owner counts as an enemy for combat credit (default true = swarm enemy). */
	void SetCountsAsKill(bool bInCountsAsKill) { bCountsAsKill = bInCountsAsKill; }

	/** STAT1 §6 저항 행: read-only accessor mirroring AFPSREnemyBase::GetVitalsProfile's shape — the resolved
	 *  profile THIS COMPONENT already mitigates ApplyDamage against (below), so a caller resolving
	 *  WeakResistScale/StrongResistScale off this can never disagree with the damage-mitigation coefficients the same
	 *  hit used. Null = no profile authored yet (VIT1 §11-1) — callers must fall back to 1.0/1.0, never 0
	 *  (ResolveDefense's own null rule, this component's ApplyDamage) — a 0 fallback would make every enemy without
	 *  an authored profile completely status-immune. */
	const UFPSRVitalsProfileDataAsset* GetVitalsProfile() const { return VitalsProfile; }

	// --- STAT1 (B단계 — data/pure-function core; movement/attack/damage hooks, the batch pass, cards and boss Tick
	//     are C단계): public API only (§7-7) — StatusBits/StatusServer/ResolvedStatus/bStatusDriverPresent below stay
	//     private/protected, everything outside this component reaches status state through these 6 entry points. ---

	/** Server: apply/refresh Slot from Catalog (see FPSRStatus::Apply for the reject/refresh/combo contract).
	 *  Silently rejects (§5-6) when bStatusDriverPresent is false — that flag is the target-actor gate: a door or
	 *  mission-flee-target shares this component but has no C단계 driver ever calling AdvanceStatus, so a bit that
	 *  landed on one of them would never expire. WeakResist/StrongResist are the caller's already-resolved profile
	 *  scale (kept a parameter, same reason Catalog is one — resolving them from a profile is C단계's job, §9).
	 *  Instigator/SourceWeapon are stored for DoT kill-credit ONLY (§7-1 "마지막 시전자 승계") — the pure
	 *  FPSRStatus::Apply function never touches actor/weapon references, so this wrapper owns that bookkeeping,
	 *  unconditionally on every successful apply (there is only one shared DoT-credit slot, §5-3). Returns whatever
	 *  FPSRStatus::Apply returned (false = rejected by cooldown/resist/no-catalog-entry, or the driver gate above). */
	bool ApplyStatus(const UFPSRStatusCatalogDataAsset* Catalog, uint8 Slot, float WeakResist, float StrongResist,
		AActor* Instigator, UFPSRWeaponInstance* SourceWeapon, TArray<uint8, TInlineAllocator<8>>& OutFired);

	/** Server: advance this actor's status state one step (see FPSRStatus::Advance). Re-derives ResolvedStatus (§7-5
	 *  cache) whenever StatusBits actually changed. OutDotDamage is handed back RAW — this component deliberately
	 *  does NOT call ApplyDamage itself; §6 routes DoT through the batch pass -> FPSRCombat::ApplyDamage bridge so
	 *  lifesteal/bWasEnemy/mission-tracking axes stay alive (a direct call here would bypass all of that). No-op
	 *  (false, OutDotDamage 0) off-authority or when bStatusDriverPresent is false. */
	bool AdvanceStatus(const UFPSRStatusCatalogDataAsset* Catalog, float WeakResist, float StrongResist,
		float& OutDotDamage,
		TArray<uint8, TInlineAllocator<8>>& OutExpired, TArray<uint8, TInlineAllocator<8>>& OutFired);

	/** The cached per-frame-cheap resolved multipliers/flags (§7-5) — safe to read on either side, but every
	 *  documented consumer (§6's wiring table) is a SERVER authoritative code path, so a client reads only the
	 *  default no-op struct (nothing ever populates it there; StatusBits itself is what replicates for cosmetics). */
	const FFPSRResolvedStatus& GetResolvedStatus() const { return ResolvedStatus; }

	/** Server: the §7-6 lifecycle closure — clears every status field back to its cold/never-applied state. Public
	 *  and idempotent so it is safe to call from all 4 closure points (STAT1 §7-6): this phase wires it into
	 *  ResetForReuse() only; EnterDyingState/Deactivate/ServerResetEliteForStageCarry are C단계's job to also call
	 *  this from. Calling it on an already-clear state (e.g. an actor that never had a status applied) is a cheap
	 *  no-op, not an error. */
	void ClearStatusForReuse();

	/** True if Slot's bit is currently set. Reads the replicated StatusBits, so this is valid on both server and
	 *  client (unlike GetResolvedStatus, which is server-only in practice). */
	bool HasStatus(uint8 Slot) const { return Slot < 8 && (StatusBits & (1 << Slot)) != 0; }

	/** Server/setup: the §5-6 target gate. Set true exactly when this actor gains a status-progression driver
	 *  (AFPSREnemyBase entering ActiveEnemies membership; AFPSRBossBase enabling its own Tick under HasAuthority())
	 *  and false the moment that driver goes away (§5-6's table — including the swarm's death-dwell window, which is
	 *  NOT "still in ActiveEnemies"). A door / AFPSRMissionFleeTarget / AFPSRBossHomingOrb never calls this, so
	 *  ApplyStatus silently no-ops on them forever — see ApplyStatus's own comment. Wiring the call sites themselves
	 *  is C단계; this phase only needs the flag and the gate it drives to exist and be correct. */
	void SetStatusDriverPresent(bool bInPresent) { bStatusDriverPresent = bInPresent; }

	UPROPERTY(BlueprintAssignable, Category = "FPSR|Enemy")
	FFPSREnemyDeathSignature OnDeath;

	/** Health change (post-clamp), fired on every applied hit including the lethal one. Fires on the SERVER from the
	 *  authoritative damage path (ApplyDamage) AND on CLIENTS from OnRep_Health once replicated Health/MaxHealth
	 *  arrive — so a client HUD / world-space health bar can bind here directly (B12). MaxHealth now replicates
	 *  (below), so the NewHealth/MaxHealth percent is valid on both sides. (AFPSRDoor — retired 2026-08-24 — used to
	 *  gate its handler to authority so the door cosmetic stayed server-driven; see git history.) */
	UPROPERTY(BlueprintAssignable, Category = "FPSR|Enemy")
	FFPSREnemyHealthChangedSignature OnHealthChanged;

	/** CLIENT-side death notify (U20): fired from OnRep_bDead when bDead replicates true, so a client can play a death
	 *  cosmetic (VAT death state) without new replication. NOT the server OnDeath path (which drives XP + pool release
	 *  and must stay authority-only). The listen-server host has no OnRep, so it drives death cosmetics from its own
	 *  authoritative death path instead. */
	UPROPERTY(BlueprintAssignable, Category = "FPSR|Enemy")
	FFPSREnemyDeathCosmeticSignature OnDeathCosmetic;

	/** Shield-break cosmetic (VIT1): fires on the (>0 -> 0) break edge — the enemy-side half of requirement 6 (the
	 *  attacker gets a hit-marker, this enemy gets a cosmetic, the player's own break gets a warning).
	 *  🔴 Zero new replication — the client half is a RepNotify on the already-replicated Shield.
	 *  🔴 Fires on BOTH sides, like OnHealthChanged and unlike OnDeathCosmetic: the authority raises it from
	 *  ApplyDamage (OnRep never runs there, so a listen-server host would otherwise see no shield-break cosmetic all
	 *  session, and standalone none at all — G2 red-team 2026-09-02), clients from OnRep_Shield's local edge check. */
	UPROPERTY(BlueprintAssignable, Category = "FPSR|Enemy")
	FFPSREnemyShieldBrokenSignature OnShieldBrokenCosmetic;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void OnRep_Health();

	/** Client RepNotify on the (already-replicated) death flag — no new replication, just a notify. Broadcasts
	 *  OnDeathCosmetic on the death edge so clients play the death animation. */
	UFUNCTION()
	void OnRep_bDead();

	/** VIT1: client RepNotify shared by Shield and MaxShield (mirrors OnRep_Health's Health/MaxHealth sharing, and
	 *  for the same reason — B12 needs a correct percent on either field's change). Re-broadcasts OnHealthChanged as
	 *  a generic "vitals changed, repaint" ping (VIT1 §8 initial-sync note — no new delegate: a shield-only hit
	 *  wouldn't otherwise reach a client-bound health bar, since Health/MaxHealth themselves didn't change) and fires
	 *  OnShieldBrokenCosmetic on the break edge. */
	UFUNCTION()
	void OnRep_Shield();

	/** STAT1 (B단계): declared now, body deliberately EMPTY — the cosmetic client-edge broadcast (mirrors
	 *  OnRep_Shield's break-edge GMS pattern, §8) is D단계 wiring, not this phase's. Declaring it here now (rather
	 *  than in D단계) is what lets StatusBits use ReplicatedUsing today without a forward-declared-then-defined-later
	 *  RepNotify split. */
	UFUNCTION()
	void OnRep_StatusBits();

	/** Replicated so clients compute a correct NewHealth/MaxHealth percent for the health bar (B12). Swarm enemies
	 *  author it as the editor default; content actors (boss/door) set it at runtime via InitializeMaxHealth. Shares
	 *  OnRep_Health, which re-broadcasts OnHealthChanged on the client whenever Health OR MaxHealth replicates. */
	UPROPERTY(EditAnywhere, ReplicatedUsing = OnRep_Health, Category = "FPSR|Enemy")
	float MaxHealth = 50.0f;

	/** When false, this owner is destructible but NOT an enemy for combat credit (no kill/enemy-hit/lifesteal —
	 *  see CountsAsKill). Default true preserves all swarm-enemy behavior (no regression). Doors set this false. */
	UPROPERTY(EditAnywhere, Category = "FPSR|Enemy")
	bool bCountsAsKill = true;

	UPROPERTY(ReplicatedUsing = OnRep_Health)
	float Health = 50.0f;

	UPROPERTY(ReplicatedUsing = OnRep_bDead)
	bool bDead = false;

	// --- VIT1: shield pool (replicated) — 0/0 is "no shield" (requirement 1), the current no-shield behavior. ------

	/** Client shield-bar percent (mirrors why MaxHealth itself replicates, B12). Baked once at InitializeVitals /
	 *  InitializeMaxHealth; never changes outside those two calls. */
	UPROPERTY(ReplicatedUsing = OnRep_Shield)
	float MaxShield = 0.0f;

	UPROPERTY(ReplicatedUsing = OnRep_Shield)
	float Shield = 0.0f;

	// --- VIT1: resolved spec baked once at spawn (server-only, non-replicated — §10 perf budget). ------------------

	float ShieldRegenPerSecond = 0.0f;
	float ShieldRegenDelaySeconds = 3.0f;
	float ShieldBrokenRegenDelaySeconds = 6.0f;

	/** Kept only for ResolveDefense lookups at damage time (coefficients are NOT baked — DefenseByDamageType can, in
	 *  principle, hold many entries the resolved spec shouldn't copy). Null = no profile (VIT1 §5-2's zero-regression
	 *  path — every layer coefficient resolves to 1.0). */
	UPROPERTY()
	TObjectPtr<const UFPSRVitalsProfileDataAsset> VitalsProfile = nullptr;

	// --- VIT1: delayed-regen state (server-only, non-replicated) — these two numbers are all ComputeRegeneratedShield
	//     needs, which is what lets regen be tickless: only ApplyDamage (on a hit) ever writes them. ------------------

	float ShieldAtLastDamage = 0.0f;
	float LastDamageCombatTime = -1.0e9f;

	/** Client-only, non-replicated: the last Shield value THIS CLIENT observed, so OnRep_Shield can detect the
	 *  (>0 -> 0) break edge locally without a dedicated "did it just break" flag over the wire. */
	float LastKnownShieldForCosmetic = 0.0f;

	// --- STAT1: lightweight status effects (B단계 — data/pure-function core + storage only; §5-3/§8). ---------------

	/** The ONLY status field that replicates (§8 복제표 — the 6th Push Model property on this component). Must stay
	 *  a DIRECT member (not a struct field) — this repo's MARK_PROPERTY_DIRTY_FROM_NAME/DOREPLIFETIME_WITH_PARAMS_
	 *  FAST calls only ever take class-direct UPROPERTYs (§5-3 / G1-11), matching every other property below. */
	UPROPERTY(ReplicatedUsing = OnRep_StatusBits)
	uint8 StatusBits = 0;

	/** Server-only, non-replicated (§5-3). Not POD (2 TWeakObjectPtr members) — see FFPSRStatusServerState's own
	 *  header comment for why that still needs no UPROPERTY/GC tracking. */
	FFPSRStatusServerState StatusServer;

	/** Server-only cache, re-derived by AdvanceStatus/ApplyStatus whenever StatusBits changes (§7-5). */
	FFPSRResolvedStatus ResolvedStatus;

	/** §5-6 target gate — see SetStatusDriverPresent's header comment. Default false: a freshly-constructed actor
	 *  (of ANY of the 5 kinds sharing this component, §3-B) has no driver until something explicitly registers one,
	 *  so ApplyStatus silently no-ops until that happens — the safe default for "unknown until proven otherwise". */
	bool bStatusDriverPresent = false;
};
