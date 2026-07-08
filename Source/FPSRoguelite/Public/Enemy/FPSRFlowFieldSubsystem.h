// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "GameplayTagContainer.h"
#include "FPSRFlowFieldSubsystem.generated.h"

class UFPSRFlowFieldComputer;
class AFPSRFlowFieldBoundsVolume;

/** Server-authoritative flow-field driver for swarm pathing (P2-B2, U7 multi-layer). Owns a per-map REGISTRY of
 *  UFPSRFlowFieldComputer instances keyed by MapId (multimap Tier 0) and drives their 0.2s recompute from a single
 *  scheduler. Enemies sample the field in O(1).
 *
 *  Refactor (Codex consult 2026-07-06): the grid/BFS/flow algorithm lives in UFPSRFlowFieldComputer (worldless core
 *  unit-tested by FPSRoguelite.FlowField.Unit). This subsystem owns discovery (bounds volume / floor Z), the recompute
 *  timer, and routing. An unset MapId is the "Default" single-map field, so an untagged L_Sandbox is unchanged.
 *
 *  S1b: registry + per-map bake/sample built here; the actual streamed-map bake is TRIGGERED by the MapStreamSubsystem
 *  on collision-ready (S3). At world begin, every bounds volume present in the persistent world is baked immediately. */
UCLASS()
class FPSROGUELITE_API UFPSRFlowFieldSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	/** Flow direction at WorldLocation, routed to the computer whose grid contains it (S1b bridge — used until enemies
	 *  carry a MapId in S2a). ZeroVector if no map covers the location / field not ready. */
	FVector SampleFlowDirection(const FVector& WorldLocation) const;

	/** Flow direction at WorldLocation from the given map's computer (S2a caller — the enemy passes its own MapId). An
	 *  unset MapId uses the Default field. If the location is outside the passed map's grid (mid-transition across a door),
	 *  retries against the map whose grid actually contains it so flow stays continuous at the boundary. */
	FVector SampleFlowDirection(const FGameplayTag& MapId, const FVector& WorldLocation) const;

	/** U (P-C combat gate): the unified continuous field if one is active (a bUnifiedExtent volume is present), else null.
	 *  FPSRCombat::CanAffectTarget uses it to gate damage/AOE on origin<->target open-grid connectivity (a closed door/wall
	 *  between them = blocked). Null => callers fall back to the MapId gate (single-map / pre-content). Server-authoritative. */
	const UFPSRFlowFieldComputer* GetUnifiedComputer() const { return UnifiedComputer; }

	/** Get (creating + baking if needed) the computer for MapId over BoundsVolume, anchored at FloorZ. Server-only. Used
	 *  at world begin (S1b) and by the MapStreamSubsystem on stream-in collision-ready (S3). Returns null off-authority. */
	UFPSRFlowFieldComputer* BakeMap(const FGameplayTag& MapId, const AFPSRFlowFieldBoundsVolume* BoundsVolume, float FloorZ);

	/** Discover the (now-loaded) bounds volume tagged MapId anywhere in the world and bake its per-map field, anchoring Z
	 *  from the volume's own box (a streamed sublevel need not contain a PlayerStart). Called by the MapStreamSubsystem
	 *  once a streamed map's collision is registered (S3). Returns false if no volume with that MapId is loaded. */
	bool BakeDiscoveredMap(const FGameplayTag& MapId);

	/** Whether a baked, ready computer exists for MapId (used by the stream/allocator gate before spawning into a map). */
	bool IsMapFieldReady(const FGameplayTag& MapId) const;

	/** True if WorldLocation is within MapId's grid (plus a small hysteresis margin, so an enemy near its own map's edge
	 *  doesn't flip-flop maps at the boundary). Used by the movement pass to fast-skip re-resolving an enemy's MapId while
	 *  it's still in its map. Unset/absent MapId with no computer -> false. */
	bool IsLocationInMap(const FGameplayTag& MapId, const FVector& WorldLocation) const;

	/** The MapId whose grid strictly contains WorldLocation (spatially separated maps -> at most one), or unset if none.
	 *  Used to re-resolve an enemy's MapId once it has left its previous map's grid (door crossing). */
	FGameplayTag FindMapIdForLocation(const FVector& WorldLocation) const;

	/** Remove a map's computer (stream-out). Tier 0 keeps maps loaded (LOD-cull only) so this is not exercised, but the
	 *  registry supports it; callers must ensure the map has no active enemies before evicting (S3 contract). */
	bool EvictMap(const FGameplayTag& MapId);

private:
	void RecomputeAllFields();
	bool HasServerAuthority() const;

	/** U (2026-07-07): build the single continuous grid from a bUnifiedExtent bounds volume and bake every currently-loaded
	 *  MapId'd slot volume into it. Called at world begin when such a volume exists. Server-only. */
	void BuildUnifiedField(UWorld& InWorld, const AFPSRFlowFieldBoundsVolume& UnifiedVolume);

	/** U: bake one slot volume into the unified grid at its computed cell offset (BakeSlotIntoUnifiedGrid). No-op if the
	 *  unified grid isn't built. Used at world begin and when a slot streams in (BakeDiscoveredMap). Returns bake success. */
	bool BakeSlotIntoUnified(UWorld& InWorld, const AFPSRFlowFieldBoundsVolume& Slot);

	/** Trace the floor Z under the first PlayerStart (Default grid Z anchor), or the start's Z / origin. */
	float DetectFloorZ(UWorld& InWorld) const;

	/** Trace the floor Z under a bounds volume's box center (per-map Z anchor for a streamed sublevel with no PlayerStart),
	 *  falling back to the box's world-min Z. */
	float DetectFloorZForVolume(UWorld& InWorld, const AFPSRFlowFieldBoundsVolume& Volume) const;

	/** The per-map flow-field computers, keyed by MapId (unset tag = Default single-map). Server-only. */
	UPROPERTY(Transient)
	TMap<FGameplayTag, TObjectPtr<UFPSRFlowFieldComputer>> Computers;

	/** U unified continuous field (2026-07-07): non-null when a bUnifiedExtent bounds volume is present -> ONE pre-sized grid
	 *  covers all slots (each MapId'd slot baked into it). Swarm flow samples THIS when set; the per-map registry above
	 *  coexists (still baked/recomputed for the allocator's IsMapFieldReady) but is unused for flow until removed in P-G. */
	UPROPERTY(Transient)
	TObjectPtr<UFPSRFlowFieldComputer> UnifiedComputer;

	FTimerHandle RecomputeTimerHandle;
};
