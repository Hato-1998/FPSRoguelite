// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "FPSREnemyCosmeticLODSubsystem.generated.h"

class AFPSREnemyBase;

/** Drives every LOCAL-VIEWER cosmetic LOD consumer for the swarm from ONE batched per-viewer pass (Game.md §1 —
 *  per-actor cost first): dynamic shadow casting, screen-space health-bar visibility, and (LOD1) the animation
 *  freeze band. Renamed from UFPSREnemyShadowLODSubsystem now that shadow is no longer the only consumer (a
 *  Content/ search turns up zero references to the old name, so the rename is free — LOD1 §4).
 *
 *  Why this is not part of the movement LOD pass in UFPSREnemySpawnSubsystem, which already computes distance tiers:
 *  a cosmetic belongs to whoever is LOOKING, and that pass runs server-only. Driving these from it would leave every
 *  remote client rendering the full 200-300-enemy cosmetic load while the host got the saving — the exact
 *  "works in solo" shape this project's multiplayer baseline rejects. So this runs on every machine that renders
 *  (every net mode except a dedicated server) and measures against the LOCAL viewer. The radius SSOT is
 *  FPSREnemyTuning.h, shared with the server batch pass's own significance tiers so "near" keeps one meaning —
 *  even though the two evaluators are deliberately kept separate (a server-authoritative significance band and a
 *  per-viewer cosmetic band answer different questions in multiplayer and must not merge, LOD1 §3).
 *
 *  Enemies register themselves in BeginPlay and drop out in EndPlay: replicated enemies run BeginPlay on clients too,
 *  so the list is correct on every machine without asking the server-only pool for it, and it costs O(1) per enemy
 *  instead of a whole-world TActorIterator per pass. AFPSREnemyBase::GetViewerBand() is the read-only seam a future
 *  VFX consumer (U13) hooks into — this subsystem does not itself drive any VFX (LOD1 explicitly does not). */
UCLASS()
class FPSROGUELITE_API UFPSREnemyCosmeticLODSubsystem : public UTickableWorldSubsystem
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

	/** LOD1 §12-7: latch for the one-time (first Tick only) radius-alignment note below — not shown in the spec's
	 *  header sketch (§5-3), added because §12-7's "subsystem's first Tick, once" log needs per-instance state to
	 *  fire exactly once rather than every pass (mirrors UFPSRStageFadeSubsystem::bMaterialLoadAttempted's same
	 *  "run my one-time check exactly once" shape). See Tick()'s own comment. */
	bool bRadiusAlignmentLogged = false;
};
