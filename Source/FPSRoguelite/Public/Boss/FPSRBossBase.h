// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/Character.h"
#include "FPSRBossBase.generated.h"

class UStaticMeshComponent;
class UFPSREnemyHealthComponent;
class UFPSRBossDefinitionDataAsset;
class UAnimMontage;

/** Boss scaffold (U3, D4) — a health-only target that closes the run's victory path. Reuses the swarm's
 *  UFPSREnemyHealthComponent so EVERY existing weapon path (hitscan / projectile / charge-laser / melee /
 *  explosion) deals damage, crits, friendly-fire and weakpoints with ZERO new damage code — the combat bridge
 *  identifies a damageable enemy by that component, not by class (FPSRCombatStatics ResolveDamage/ApplyDamage).
 *  On death it asks the GameMode to end the run in Victory (loose coupling, mirroring U2's player-defeat path).
 *
 *  Base class = engine ACharacter (NOT AFPSRCharacter — that is the player; inheriting it would route the boss
 *  through the friendly-fire branch). ACharacter is the production base so the long-term real boss can add AI
 *  navigation (CharacterMovement + an AIController running a StateTree) and GAS abilities (an ASC) WITHOUT
 *  re-parenting this BP. This scaffold deliberately ships none of that: movement is disabled (stationary box),
 *  no ASC/GAS is attached, and there is no per-tick logic — so there is nothing for the global level-up freeze
 *  (§2-2 bRunPaused) to gate here. When the real boss adds movement/attacks, those MUST gate on bRunPaused.
 *
 *  Collision: the capsule's object type is ECC_Pawn (mirroring swarm enemies). It MUST NOT be WorldStatic — the
 *  hitscan wall trace would then treat the boss body as a wall and block its own bullets (P7 §6, "most common
 *  trap"). The boss is a pawn but receives no knockback: the knockback dispatch keys on AFPSRCharacter (player)
 *  and AFPSREnemyBase (swarm), so a boss falls through to no-op — intended (bosses aren't punted around). */
UCLASS()
class FPSROGUELITE_API AFPSRBossBase : public ACharacter
{
	GENERATED_BODY()

public:
	AFPSRBossBase();

	/** The boss's non-GAS health component — exposed so the HUD boss bar (B11) can bind OnHealthChanged (now
	 *  client-fired via B12) and read GetHealth()/GetMaxHealth(). */
	UFUNCTION(BlueprintPure, Category = "FPSR|Boss")
	UFPSREnemyHealthComponent* GetHealthComponent() const { return HealthComponent; }

	/** Server: apply a boss definition's tuning (MaxHealth, …) to this instance. Called by the run director right
	 *  after spawn. Overrides DefaultMaxHealth. No-op off-authority / null definition. */
	void InitializeFromDefinition(const UFPSRBossDefinitionDataAsset* Definition);

	/** Play a montage on the boss's SKELETAL mesh (the inherited ACharacter Mesh — the boss BP assigns Prime_Helix +
	 *  its AnimBP there; U20 "boss skeletal" seam). The reusable animation entry point for future boss abilities / AI
	 *  and the death anim. No-op when the mesh has no AnimInstance (i.e. before content assigns a skeletal mesh/AnimBP)
	 *  or when Montage is null. Cosmetic. */
	UFUNCTION(BlueprintCallable, Category = "FPSR|Boss")
	void PlayBossMontage(UAnimMontage* Montage, float PlayRate = 1.0f);

protected:
	virtual void BeginPlay() override;

	/** Server: boss died — end the run in Victory via the GameMode (loose coupling; U2 NotifyPlayerDefeated mirror). */
	UFUNCTION()
	void HandleDeath(AActor* DeadActor, AActor* Killer);

	/** Play the boss death montage (U20). Bound to the health component's OnDeathCosmetic so CLIENTS play it on the
	 *  replicated death edge; also invoked from HandleDeath so the listen-server host / standalone play it. */
	UFUNCTION()
	void HandleDeathCosmetic();

	/** Optional montage played on the boss skeletal mesh on death (U20). Null = none (null-safe). Content-assigned. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Boss")
	TSoftObjectPtr<UAnimMontage> DeathMontage;

	/** Visible placeholder box (no collision — the capsule handles hits). Designers swap the mesh in the boss BP. */
	UPROPERTY(VisibleAnywhere, Category = "FPSR|Boss")
	TObjectPtr<UStaticMeshComponent> BodyMesh;

	/** Non-GAS health (shared swarm component) — the single reason every weapon path damages the boss for free. */
	UPROPERTY(VisibleAnywhere, Category = "FPSR|Boss")
	TObjectPtr<UFPSREnemyHealthComponent> HealthComponent;

	/** Max health used when no BossDefinition overrides it (the C++ fallback boss for FPSR.SkipToBoss before any
	 *  boss BP/definition exists). Kept testable so the kill->victory loop is verifiable in a PIE session — the
	 *  real boss's larger health is authored on its DA_BossDefinition (U4). Balance value. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Boss", meta = (ClampMin = "1.0"))
	float DefaultMaxHealth = 1000.0f;
};
