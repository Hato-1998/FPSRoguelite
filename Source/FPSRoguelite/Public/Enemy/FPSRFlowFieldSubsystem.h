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

/** Server-authoritative flow-field driver for swarm pathing (P2-B2, U7 multi-layer). Owns a single per-world
 *  UnifiedComputer (a multi-slot unified grid when bUnifiedMultiSlot, else a degenerate single-map grid) and drives
 *  its 0.2s recompute from one scheduler; SlotBounds resolves a location's MapId per slot. (P-G replaced the former
 *  per-map UFPSRFlowFieldComputer registry with this one unified computer.)
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

	/** The continuous flow field (P-G: ALWAYS built on the server — a real bUnifiedExtent multimap grid, OR a single
	 *  degenerate world-trace grid for a plain single-map). Used for flow sampling + origin<->target open-grid connectivity
	 *  (FPSRCombat::CanAffectTarget). Null only off-authority (clients never build it) / pre-build. Server-authoritative. */
	const UFPSRFlowFieldComputer* GetUnifiedComputer() const { return UnifiedComputer; }

	/** U (P-G): the unified computer ONLY when it is a real MULTI-SLOT field (a bUnifiedExtent volume with MapId'd slots) —
	 *  null for a single-map degenerate grid. This is the "is this multimap?" predicate: multimap-only behaviors (the
	 *  topology late-join ack gate, the combat connectivity gate, front-chase/spawn, the trickle drain) gate on THIS, so a
	 *  single-map run stays a strict no-op (no ack seal, combat allow-all) exactly as before P-G. Server-authoritative. */
	const UFPSRFlowFieldComputer* GetMultiSlotUnifiedComputer() const { return bUnifiedMultiSlot ? UnifiedComputer : nullptr; }

	/** U (P-H): the largest slot footprint DIAGONAL (cm, XY) across all baked slots — the footprint cap input for the swarm
	 *  net-cull sizing (UFPSREnemySpawnSubsystem::ComputeUnifiedNetCullRadius), so the uniform net-cull radius never spans the
	 *  whole 3x3 grid. 0 with no unified multi-slot field (single-map). Cached at bake. Server-authoritative. */
	float GetMaxSlotFootprintDiagonal() const { return MaxSlotFootprintDiagonalCm; }

	/** U P-D: true if A and B are in the same open-grid connected component of the UNIFIED field (an open door connects them;
	 *  a closed door/wall separates them). False when there is no unified field (single-map) — callers then keep same-map
	 *  behavior (no regression). Server-authoritative, O(1) after RunBFS. */
	bool AreLocationsConnected(const FVector& A, const FVector& B) const;

	/** U P-D: path-distance (cells) from the nearest player to Loc on the unified field (front-chase range gate). OutStatus =
	 *  NoGrid when no unified field; else the computer's OK/OffGrid/SourceLess/Unreachable. Returns MAX_int32 for non-OK. */
	int32 GetFrontDistanceCells(const FVector& Loc, EFPSRFieldQuery& OutStatus) const;

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

	/** True if WorldLocation is within MapId's slot AABB (plus a small hysteresis margin, so an enemy near its slot's edge
	 *  doesn't flip-flop maps at the boundary). Used by the movement pass to fast-skip re-resolving an enemy's MapId while
	 *  it's still in its slot. P-G: resolved from SlotBounds — an EMPTY table (single-map degenerate grid) returns true. */
	bool IsLocationInMap(const FGameplayTag& MapId, const FVector& WorldLocation) const;

	/** The MapId whose slot AABB strictly contains WorldLocation (spatially separated slots -> at most one), or unset if none.
	 *  Used to re-resolve an enemy's MapId once it has left its previous slot (door crossing). P-G: resolved from SlotBounds. */
	FGameplayTag FindMapIdForLocation(const FVector& WorldLocation) const;

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

	/** U (P-H): XY footprint diagonal (cm) of a slot's world AABB (0 if the box is invalid) — the net-cull footprint cap
	 *  accumulator (max'd into MaxSlotFootprintDiagonalCm at each slot bake). */
	static float SlotFootprintDiagonalXY(const FBox& SlotBox);

	/** U continuous field. P-G: ALWAYS built on the server — a real bUnifiedExtent grid (all MapId'd slots baked in), OR a
	 *  single degenerate world-trace grid for a plain single-map. Swarm flow + combat connectivity sample THIS. */
	UPROPERTY(Transient)
	TObjectPtr<UFPSRFlowFieldComputer> UnifiedComputer;

	/** U (P-G): true only when UnifiedComputer is a real MULTI-SLOT bUnifiedExtent grid (false for the degenerate single-map
	 *  grid). Gates the multimap-only behaviors (ack/combat-connectivity/front/trickle) via GetMultiSlotUnifiedComputer(). */
	bool bUnifiedMultiSlot = false;

	/** U (P-G): per-slot world AABB keyed by MapId, populated at bake (BuildUnifiedField / BakeDiscoveredMap). Replaces the
	 *  per-map registry as the source for FindMapIdForLocation / IsLocationInMap (which slot a location is in). Empty for a
	 *  single-map degenerate grid (one map, unset). POD member (no GC / UPROPERTY); Reset() in Deinitialize. Server-only. */
	TMap<FGameplayTag, FBox> SlotBounds;

	/** U (P-H): cached max slot footprint diagonal (cm, XY) over SlotBounds — the net-cull footprint cap input. Recomputed
	 *  (max) when a slot bakes in (BuildUnifiedField / BakeDiscoveredMap). 0 for a single-map degenerate grid. Server-only. */
	float MaxSlotFootprintDiagonalCm = 0.0f;

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
