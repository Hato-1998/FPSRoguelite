// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "GameplayTagContainer.h"
#include "FPSRMapStreamSubsystem.generated.h"

/**
 * Server-authoritative multimap stream orchestrator (multimap Tier 0, §3 / Codex consult 2026-07-06). A giant Door break
 * asks this to stream in the adjacent authored sublevel (RequestStreamIn); when that level's collision is verified ready
 * (GetLevelStreamingState() == LoadedVisible — AddToWorld complete, components/collision registered), it:
 *   1. bakes the map's per-map flow field (UFPSRFlowFieldSubsystem::BakeDiscoveredMap — WorldStatic traces now hit),
 *   2. re-caches the enemy spawn points (the streamed sublevel's points weren't cached at world begin), and
 *   3. drops the boundary blocker(s) guarding that map so players may cross.
 * On a stream-in timeout it leaves the boundary blocker up (passage "sealed") and logs an error — it never opens on an
 * unverified signal (Codex R4: LoadStreamLevel fails silently on a bad level name).
 *
 * Client visibility is handled by the ENGINE (AGameModeBase::ReplicateStreamingStatus + FStreamLevelAction auto-replicate
 * server stream-in to every client + late joiner), so there is NO game-side level-visibility transport here (Codex R5).
 */
UCLASS()
class FPSROGUELITE_API UFPSRMapStreamSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Deinitialize() override;

	/** Server: begin streaming in LevelName (an authored sublevel of the persistent map) and, on verified collision-ready,
	 *  bake MapId's flow field + re-cache spawn points + open its boundary. Idempotent (ignores already-pending/ready maps).
	 *  Called from AFPSRDoor::HandleBroken. No-op off-authority / with an invalid MapId or level name. */
	void RequestStreamIn(const FGameplayTag& MapId, FName LevelName);

	/** True once MapId has streamed in and its collision was verified ready (field baked, boundary opened). */
	bool IsMapReady(const FGameplayTag& MapId) const { return ReadyMaps.Contains(MapId); }

private:
	bool HasServerAuthority() const;

	/** Timer: advance each pending stream; fire ready or time out. */
	void PollPending();

	/** True when LevelName's streaming level has reached LoadedVisible (AddToWorld complete = collision registered). */
	bool IsLevelCollisionReady(FName LevelName) const;

	/** Bake the field, re-cache spawn points, and drop the boundary blocker(s) for a now-ready map. */
	void HandleMapReady(const FGameplayTag& MapId);

	struct FPendingStream
	{
		FGameplayTag MapId;
		FName LevelName;
		float Elapsed = 0.0f;
	};

	TArray<FPendingStream> Pending;
	TSet<FGameplayTag> ReadyMaps;
	FTimerHandle PollTimer;
	int32 NextLatentUUID = 1;

	static constexpr float PollInterval = 0.2f;  // s between readiness checks
	static constexpr float StreamTimeout = 20.0f; // s before a stuck stream-in gives up (blocker stays)
};
