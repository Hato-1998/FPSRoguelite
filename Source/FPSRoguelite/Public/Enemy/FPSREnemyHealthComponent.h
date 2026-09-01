// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Combat/FPSRVitals.h"
#include "Combat/FPSRVitalsProfile.h"
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

	/** Client cosmetic (VIT1): fires when Shield replicates down on the (>0 -> 0) break edge — the enemy-side half of
	 *  requirement 6 (attacker gets a hit-marker, this enemy gets a cosmetic, the player's own break gets a warning).
	 *  🔴 Zero new replication — this is a RepNotify on the already-replicated Shield, same pattern as OnDeathCosmetic. */
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
};
