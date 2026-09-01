// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/Actor.h"
#include "FPSRHealthPickup.generated.h"

class UStaticMeshComponent;

/** Map-placed health pickup (VIT1 requirement 3 / 회귀함정 5 "하강 나선" countermeasure — a downward-spiral player
 *  who keeps getting hit needs a way back up that ISN'T "wait for the shield", since Health never regens on its own
 *  (requirement 4): map pickups, lifesteal, revive, and a MaxHealth increase's immediate top-up are the only four
 *  routes). A sibling of AFPSRXPPickup: server-authoritative, non-GAS (routes through AFPSRCharacter::ApplyHealing),
 *  no magnet (recovering health is meant to be a deliberate detour, not something you passively walk through). */
UCLASS()
class FPSROGUELITE_API AFPSRHealthPickup : public AActor
{
	GENERATED_BODY()

public:
	AFPSRHealthPickup();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override; // server-only: collect-radius scan + combat-clock respawn check

	/** Server: mark available/unavailable, replicate, and apply the visual immediately on the listen-server host
	 *  (which gets no OnRep — same pattern as this project's other replicated-flag setters). */
	void SetAvailable(bool bInAvailable);

	UFUNCTION()
	void OnRep_Available();

	UPROPERTY(VisibleAnywhere, Category = "FPSR|Pickup")
	TObjectPtr<UStaticMeshComponent> Mesh;

	/** Flat heal amount. Both this and the fraction below can be authored together (0 disables an axis) — a flat
	 *  amount and a %-of-MaxHealth amount are different balance levers (a flat pack matters less to a high-MaxHealth
	 *  build; a % pack scales with it). */
	UPROPERTY(EditAnywhere, Category = "FPSR|Pickup", meta = (ClampMin = "0.0"))
	float HealFlat = 0.0f;

	UPROPERTY(EditAnywhere, Category = "FPSR|Pickup", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float HealMaxHealthFraction = 0.25f;

	/** Distance at which a nearby alive, missing-health player collects this (no magnet — VIT1 §5-8: recovering
	 *  health is a deliberate detour, not something you passively drift into). */
	UPROPERTY(EditAnywhere, Category = "FPSR|Pickup", meta = (ClampMin = "0.0"))
	float CollectRadius = 120.0f;

	/** 0 = single-use (never respawns after collection). >0 = seconds (on the freeze-paused combat clock, §5-6 — a
	 *  card-selection freeze doesn't secretly advance the respawn timer) before this pickup reactivates. */
	UPROPERTY(EditAnywhere, Category = "FPSR|Pickup", meta = (ClampMin = "0.0"))
	float RespawnSeconds = 45.0f;

	/** When true (default), a player already at full Health can't consume this pickup — so in 4-player co-op a
	 *  topped-up teammate passing through doesn't waste it for the one who actually needs it. */
	UPROPERTY(EditAnywhere, Category = "FPSR|Pickup")
	bool bRequireMissingHealth = true;

	UPROPERTY(ReplicatedUsing = OnRep_Available)
	bool bAvailable = true;

	/** Server-only, non-replicated: the combat-clock deadline (AFPSRGameState::GetCombatClockSeconds()) at which
	 *  this pickup reactivates. Meaningful only while !bAvailable && RespawnSeconds > 0. */
	float RespawnAtCombatTime = 0.0f;
};
