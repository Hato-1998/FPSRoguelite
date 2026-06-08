// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/Actor.h"
#include "FPSRMissionOrb.generated.h"

class USphereComponent;
class AFPSRMissionOrb;
class APawn;

DECLARE_MULTICAST_DELEGATE_TwoParams(FOnMissionOrbCollected, AFPSRMissionOrb* /*Orb*/, APawn* /*Collector*/);

/** Lightweight collectible orb spawned by missions (CollectOrbs / CarryNoHit). Server-authoritative overlap;
 *  replicated so clients see it. Broadcasts OnCollectedNative once when a player pawn first reaches it. */
UCLASS()
class FPSROGUELITE_API AFPSRMissionOrb : public AActor
{
	GENERATED_BODY()

public:
	AFPSRMissionOrb();

	/** Native delegate the owning mission subscribes to (server, fires once on first player overlap). */
	FOnMissionOrbCollected OnCollectedNative;

	/** Server: hide/disable (collected) or show/enable. Does not broadcast. */
	void SetCollected(bool bNewCollected);

	bool IsCollected() const { return bCollected; }

	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	UFUNCTION()
	void OnSphereBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UPROPERTY(VisibleAnywhere, Category = "Mission|Orb")
	TObjectPtr<USphereComponent> Sphere;

	UPROPERTY(Replicated)
	bool bCollected = false;
};
