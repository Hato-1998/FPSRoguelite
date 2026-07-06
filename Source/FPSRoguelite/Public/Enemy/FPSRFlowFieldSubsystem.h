// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "FPSRFlowFieldSubsystem.generated.h"

class UFPSRFlowFieldComputer;

/** Server-authoritative flow-field driver for swarm pathing (P2-B2, U7 multi-layer). Owns one or more
 *  UFPSRFlowFieldComputer instances (one per map — S1a keeps a single Default computer; the multimap
 *  registry lands in S1b) and drives their 0.2s recompute. Enemies sample the field in O(1).
 *
 *  Refactor (Codex consult 2026-07-06): the grid/BFS/flow algorithm moved into UFPSRFlowFieldComputer so a
 *  worldless core can be unit-tested (FPSRoguelite.FlowField.Unit) and so the field can be keyed per map.
 *  This subsystem now owns discovery (bounds volume / floor Z), the recompute timer, and the sample forward. */
UCLASS()
class FPSROGUELITE_API UFPSRFlowFieldSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	/** Returns the normalized flow direction (XY, Z=0) at WorldLocation, or ZeroVector if outside the grid /
	 *  field not ready / no reachable surface. Callers should fall back to a direct-to-player direction on ZeroVector.
	 *  (S1a: samples the single Default computer. S1b adds a MapId overload.) */
	FVector SampleFlowDirection(const FVector& WorldLocation) const;

private:
	void RecomputeField();
	bool HasServerAuthority() const;

	/** Trace the floor Z under the first PlayerStart (grid Z anchor), or fall back to the start's Z / origin. */
	float DetectFloorZ(UWorld& InWorld) const;

	/** The single-map flow-field computer (S1a). Server-only; created in OnWorldBeginPlay. */
	UPROPERTY(Transient)
	TObjectPtr<UFPSRFlowFieldComputer> DefaultComputer;

	FTimerHandle RecomputeTimerHandle;
};
