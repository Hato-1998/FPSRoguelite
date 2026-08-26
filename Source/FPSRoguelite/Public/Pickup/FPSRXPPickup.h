// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/Actor.h"
#include "FPSRXPPickup.generated.h"

class UStaticMeshComponent;

/** Server-authoritative XP gem dropped on enemy death (P3-B). A sphere placeholder that magnetizes
 *  toward the nearest player and, on contact, grants shared XP to the party then destroys itself.
 *  NOT GAS-based; movement/collection run server-side and replicate to clients. */
UCLASS()
class FPSROGUELITE_API AFPSRXPPickup : public AActor
{
	GENERATED_BODY()

public:
	AFPSRXPPickup();

	/** Server: set the XP this gem grants on collection (called by the pickup subsystem on spawn). */
	void SetXPValue(int32 InValue) { XPValue = InValue; }

	/** Server: 스테이지 스왑 때 이 젬을 새 아레나의 대응 위치로 옮긴다
	 *  (UFPSRPickupSubsystem::CarryPickupsToNewStage). AFPSREnemyBase::ServerRelocateForStageCarry 와 같은 역할. */
	void ServerRelocateForStageCarry(const FVector& NewLocation);

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Server: bank this gem to Collector (their XPGain multiplier applied) and destroy it. The single collect path —
	 *  now reached only by the normal radius collect in Tick. It used to also serve a stage-transition instant
	 *  collect (ADR 0010 D6); that caller is gone (사용자 결정 2026-08-25) — a gem caught in a transition now carries
	 *  over to the new arena instead (see ServerRelocateForStageCarry) and is picked up the normal way afterward. */
	void CollectBy(class APawn* Collector);

	/** Read the given player's CombatSet PickupRadius multiplier (>=0.01), or 1.0 if unavailable. */
	float GetCollectorPickupRadiusMult(class APawn* Pawn) const;

	/** Read the given player's CombatSet XPGain multiplier (>=0), or 1.0 if unavailable. */
	float GetCollectorXPGainMult(class APawn* Pawn) const;

	UPROPERTY(VisibleAnywhere, Category = "FPSR|Pickup")
	TObjectPtr<UStaticMeshComponent> Mesh;

	/** XP granted to the party on collection. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Pickup")
	int32 XPValue = 5;

	/** Distance at which the gem is collected and grants XP. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Pickup")
	float CollectRadius = 100.0f;

	/** Distance at which the gem begins magnetizing toward the player. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Pickup")
	float MagnetRadius = 500.0f;

	/** Speed (cm/s) the gem moves toward the player while magnetizing. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Pickup")
	float MagnetSpeed = 800.0f;
};
