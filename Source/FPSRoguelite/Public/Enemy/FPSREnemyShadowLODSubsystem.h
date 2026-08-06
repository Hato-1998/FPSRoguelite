// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "FPSREnemyShadowLODSubsystem.generated.h"

class AFPSREnemyBase;

/** Turns a swarm enemy's dynamic shadow on only while it is near THIS machine's viewer (Game.md §1 — per-actor cost
 *  first). Knobs live in UFPSREnemyRenderSettings.
 *
 *  Why this is not part of the movement LOD pass in UFPSREnemySpawnSubsystem, which already computes distance tiers:
 *  a shadow is cosmetic, so it belongs to whoever is LOOKING, and that pass runs server-only. Driving CastShadow from
 *  it would leave every remote client rendering the full 200-300 casters while the host got the saving — the exact
 *  "works in solo" shape this project's multiplayer baseline rejects. So this runs on every machine and measures
 *  against the local viewer. The RADIUS is still shared with that pass (GetTierS0RadiusSq's documented reuse) via the
 *  settings default, so "near" keeps one meaning.
 *
 *  Enemies register themselves in BeginPlay and drop out in EndPlay: replicated enemies run BeginPlay on clients too,
 *  so the list is correct on every machine without asking the server-only pool for it, and it costs O(1) per enemy
 *  instead of a whole-world TActorIterator per pass. */
UCLASS()
class FPSROGUELITE_API UFPSREnemyShadowLODSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Called from AFPSREnemyBase::BeginPlay on every machine that renders. Safe to call when the subsystem is absent. */
	void RegisterEnemy(AFPSREnemyBase* Enemy);

	/** Called from AFPSREnemyBase::EndPlay. */
	void UnregisterEnemy(AFPSREnemyBase* Enemy);

	//~ USubsystem
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	//~ End USubsystem

	//~ FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual ETickableTickType GetTickableTickType() const override;
	virtual bool IsTickable() const override;
	virtual UWorld* GetTickableGameObjectWorld() const override;
	//~ End FTickableGameObject

private:
	/** Registered enemies. Weak because a pooled enemy outlives any single spawn and a torn-down world can drop one
	 *  without EndPlay ordering guarantees; a stale entry is compacted on the next pass rather than dangling. */
	TArray<TWeakObjectPtr<AFPSREnemyBase>> RegisteredEnemies;

	/** Time since the last pass (the pass runs on an interval, not per frame). */
	float TimeSinceLastPass = 0.0f;
};
