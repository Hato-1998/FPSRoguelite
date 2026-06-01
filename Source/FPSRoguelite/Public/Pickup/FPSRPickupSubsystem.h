// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "FPSRPickupSubsystem.generated.h"

class AFPSRXPPickup;

/** Server-authoritative spawner + cap manager for XP pickups dropped on enemy death (P3-B).
 *  Tracks active gems; over MaxActivePickups, XP is granted directly to the party instead of
 *  spawning another actor (swarm performance guard, Game.MD §5). NOT GAS-based. */
UCLASS()
class FPSROGUELITE_API UFPSRPickupSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	/** Server: spawn an XP gem at Location worth XPValue. Over the active cap, grants the XP directly. */
	void SpawnXPPickup(const FVector& Location, int32 XPValue);

private:
	/** Check if this subsystem has server authority. */
	bool HasServerAuthority() const;

	/** Drop stale (collected/destroyed) entries from the active-pickup list. */
	void PruneActivePickups();

	/** Currently alive XP gems (lazily pruned of nulls). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<AFPSRXPPickup>> ActivePickups;

	/** Hard cap on simultaneously active XP gems (over cap -> grant XP directly, Game.MD §5). */
	static constexpr int32 MaxActivePickups = 150;
};
