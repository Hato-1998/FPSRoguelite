// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "GameplayTagContainer.h"
#include "Enemy/FPSRFlowFieldComputer.h" // FFPSRFlowFieldSurfaceData (BakedBaseline by-value member, U P-F) + EFPSRFieldQuery
#include "FPSRFlowFieldSubsystem.generated.h"

class AActor;
class UFPSRFlowFieldComputer;
class AFPSRFlowFieldBoundsVolume;
class AFPSRGameState;

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

	/** U P-D: true if A and B are in the same open-grid connected component of the UNIFIED field (an open door connects them;
	 *  a closed door/wall separates them). False when there is no unified field (single-map) — callers then keep same-map
	 *  behavior (no regression). Server-authoritative, O(1) after RunBFS. */
	bool AreLocationsConnected(const FVector& A, const FVector& B) const;

	/** U P-D: path-distance (cells) from the nearest player to Loc on the unified field (front-chase range gate). OutStatus =
	 *  NoGrid when no unified field; else the computer's OK/OffGrid/SourceLess/Unreachable. Returns MAX_int32 for non-OK. */
	int32 GetFrontDistanceCells(const FVector& Loc, EFPSRFieldQuery& OutStatus) const;

	/** Get (creating + baking if needed) the computer for MapId over BoundsVolume, anchored at FloorZ. Server-only. Used
	 *  at world begin (S1b) and by the MapStreamSubsystem on stream-in collision-ready (S3). Returns null off-authority. */
	UFPSRFlowFieldComputer* BakeMap(const FGameplayTag& MapId, const AFPSRFlowFieldBoundsVolume* BoundsVolume, float FloorZ);

	/** Discover the (now-loaded) bounds volume tagged MapId anywhere in the world and bake its per-map field, anchoring Z
	 *  from the volume's own box (a streamed sublevel need not contain a PlayerStart). Called by the MapStreamSubsystem
	 *  once a streamed map's collision is registered (S3). Returns false if no volume with that MapId is loaded. */
	bool BakeDiscoveredMap(const FGameplayTag& MapId);

	/** U (P-B): a breakable seam door was destroyed (server) — open the unified grid's cross-seam edges the door spanned
	 *  and recompute NOW so swarm flow + the origin-aware combat gate cross it immediately. No-op with no unified field
	 *  (single-map / pre-content) or off authority — the closed seam kept the slots isolated, so nothing changes. Called by
	 *  AFPSRDoor::HandleBroken; the door->cell mapping is UFPSRFlowFieldComputer::MapDoorSeamCellPairs. */
	void NotifyDoorBroken(const AActor* Door);

	/** U (P-F): server-authoritative topology generation — bumped every time the unified grid's connectivity changes
	 *  (a seam door opens, a slot bakes in, or a new-run baseline reset). Late-join ack + the freeze pre-unfreeze
	 *  recompute key off this. 0 with no unified field (single-map / pre-content) — it never changes there, so every
	 *  client's gen-0 ack is instantly satisfied (no single-map regression). Server-only monotone counter. */
	int32 GetTopologyGeneration() const { return TopologyGeneration; }

	/** U (P-F): atomically restore the unified grid to its world-begin baked baseline (all seam doors closed) and bump
	 *  the generation, for a same-world re-run (StartRun). No-op — generation preserved — if the topology was never
	 *  mutated since the baseline (a first run: StartRun runs even then, so the dirty-flag guard keeps gen at 0) or with
	 *  no unified field. Server-only. Currently a future-path safety net: a new run reloads the whole map (fresh field),
	 *  so this only fires on a hypothetical in-place re-run — production structure, not dead code. */
	void ResetDoorTopologyToBaseline();

	/** U (P-F): pure predicate for the freeze pre-unfreeze recompute — true ONLY on the unpause edge (was paused, now
	 *  not) AND when the topology changed while frozen (a door broke during the freeze, so the flow field is a
	 *  generation behind). Any other run-state transition is a no-op. Static + worldless so FPSRoguelite.FlowField
	 *  regressions it headless (the exact edge logic HandleRunStateChanged uses). */
	static bool ShouldRecomputeOnUnfreeze(bool bWasPaused, bool bNowPaused, int32 TopologyGen, int32 LastRecomputedGen);

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

	/** U (P-F): GameState OnRunStateChanged handler (server) — on the unpause edge, if a door broke during the freeze
	 *  (the topology generation moved past the last recomputed generation), recompute the field NOW so the swarm + combat
	 *  gate are correct the instant the freeze lifts, not on the next 0.2s tick. UFUNCTION for AddDynamic. (Tier-0 inert:
	 *  a door can't break mid-freeze yet — future-proofing, but wired now.) */
	UFUNCTION()
	void HandleRunStateChanged();

	/** U (P-F): bind HandleRunStateChanged to the GameState's OnRunStateChanged (idempotent). Called at world begin and
	 *  lazily from RecomputeAllFields (the GameState may not exist yet at world begin). Seeds bWasPaused so an already-
	 *  paused bind doesn't miss the first unpause. Server-only. */
	void TryBindRunStateHandler();

	/** U (P-F): bump the topology generation (server) and mirror it to the replicated GameState so remote clients OnRep
	 *  and re-ack (Stage 2). Monotone (++ only). */
	void AdvanceTopologyGeneration();

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

	// --- U (P-F) topology generation + freeze pre-unfreeze + baked baseline (server-only; the replicated mirror lives on
	//     the GameState, Stage 2). ---

	/** Monotone server counter, bumped on every unified-grid connectivity change (seam open / slot bake / baseline reset). */
	int32 TopologyGeneration = 0;

	/** The generation the current flow field was last recomputed for (-1 = never). RecomputeAllFields stamps it on a
	 *  successful (non-frozen) recompute; the freeze pre-unfreeze handler recomputes when it lags TopologyGeneration. */
	int32 LastRecomputedGeneration = -1;

	/** True once a seam door / slot bake has changed the topology away from the baked baseline. Gates the new-run reset
	 *  to a no-op on a first (unmutated) run so the generation stays 0 (StartRun runs even on the first run). */
	bool bTopologyMutatedSinceBaseline = false;

	/** Whether BakedBaseline holds a valid world-begin snapshot (only captured when a unified field is built). */
	bool bHasBaseline = false;

	/** World-begin snapshot of the unified grid's surface graph (all seam doors closed), for ResetDoorTopologyToBaseline.
	 *  Plain member (POD arrays, no UObject refs -> no GC concern); server-only. */
	FFPSRFlowFieldSurfaceData BakedBaseline;

	/** Last-seen pause state for the OnRunStateChanged unpause-edge detection (seeded at bind time). */
	bool bWasPaused = false;

	/** Whether HandleRunStateChanged is bound to the GameState delegate (idempotent bind guard). */
	bool bRunStateHandlerBound = false;
};
