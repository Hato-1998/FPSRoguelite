// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/Pawn.h"
#include "FPSRMissionFleeTarget.generated.h"

class UCapsuleComponent;
class UStaticMeshComponent;
class UFPSREnemyHealthComponent;

/** Standalone high-HP target pawn spawned by the DefeatFleeing mission. Reuses the non-GAS enemy health
 *  component so the existing hitscan damage bridge applies damage; the owning mission drives its flee movement
 *  and completes when it dies. Independent of the swarm pool / movement subsystem. */
UCLASS()
class FPSROGUELITE_API AFPSRMissionFleeTarget : public APawn
{
	GENERATED_BODY()

public:
	AFPSRMissionFleeTarget();

	UFPSREnemyHealthComponent* GetHealthComponent() const { return HealthComponent; }

protected:
	UPROPERTY(VisibleAnywhere, Category = "Mission|FleeTarget")
	TObjectPtr<UCapsuleComponent> Capsule;

	UPROPERTY(VisibleAnywhere, Category = "Mission|FleeTarget")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(VisibleAnywhere, Category = "Mission|FleeTarget")
	TObjectPtr<UFPSREnemyHealthComponent> HealthComponent;
};
