// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "FPSREnemyHealthComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FFPSREnemyDeathSignature, AActor*, DeadActor, AActor*, Killer);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FFPSREnemyHealthChangedSignature, float, NewHealth, float, MaxHealth);

/** Lightweight, non-GAS health for swarm enemies. Server-authoritative; damage applied via the GAS->bridge. */
UCLASS(ClassGroup = (FPSR), meta = (BlueprintSpawnableComponent))
class FPSROGUELITE_API UFPSREnemyHealthComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UFPSREnemyHealthComponent();

	/** Server: apply damage and handle death. */
	void ApplyDamage(float DamageAmount, AActor* DamageInstigator, FGameplayTag DamageType = FGameplayTag());

	/** Server: reset health/dead flag for pooled reuse. */
	void ResetForReuse();

	/** Server: (re)initialize the health pool to NewMaxHealth (sets MaxHealth and full Health, clears dead). Used by
	 *  content-driven actors that size their health at runtime — e.g. the U3 boss applies its definition's value.
	 *  Swarm enemies don't call this (they author MaxHealth as the editor default). No-op off-authority / <= 0. */
	void InitializeMaxHealth(float NewMaxHealth);

	UFUNCTION(BlueprintPure, Category = "FPSR|Enemy")
	float GetHealth() const { return Health; }

	UFUNCTION(BlueprintPure, Category = "FPSR|Enemy")
	float GetMaxHealth() const { return MaxHealth; }

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
	 *  (below), so the NewHealth/MaxHealth percent is valid on both sides. (AFPSRDoor still gates its handler to
	 *  authority, so the door cosmetic stays server-driven and doesn't double-fire.) */
	UPROPERTY(BlueprintAssignable, Category = "FPSR|Enemy")
	FFPSREnemyHealthChangedSignature OnHealthChanged;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void OnRep_Health();

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

	UPROPERTY(Replicated)
	bool bDead = false;
};
