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

protected:
	virtual void Tick(float DeltaSeconds) override;

	/** Find the nearest player pawn (2D), or nullptr if none. */
	APawn* FindNearestPlayer() const;

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
