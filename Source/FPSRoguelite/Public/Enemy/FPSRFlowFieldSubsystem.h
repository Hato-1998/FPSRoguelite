// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "GameplayTagContainer.h"
#include "Containers/ArrayView.h" // TConstArrayView (NotifyArenaCellsOpened)
#include "Enemy/FPSRFlowFieldComputer.h" // FFPSRFlowFieldSurfaceData (BakedBaseline by-value member, U P-F) + EFPSRFieldQuery
#include "Arena/FPSRArenaBakeDataAsset.h" // FFPSRArenaVoxelData (AdoptedVoxels/VoxelBaseline by-value members, ADR 0009 S2)
#include "FPSRFlowFieldSubsystem.generated.h"

class AActor;
class APawn;
class UFPSRFlowFieldComputer;
class AFPSRFlowFieldBoundsVolume;
class AFPSRGameState;
class AFPSRArenaActor;
class UFPSRHoverWindowSubsystem;

/** Which field actually answered a QueryFlow — S1 has exactly one source; S3 (ADR 0009 결정 1) adds Window3D.
 *  Consumers MUST NOT branch on this for behavior (invariant 6 — agents don't know which field they read);
 *  it exists for logging/telemetry only. */
enum class EFPSRFlowSource : uint8 { None, Surface2D, Window3D };

/** One movement-consumer flow query (ADR 0009 결정 5 — the single seam). Direction and hover-floor sampling
 *  keep their historical, DIFFERENT foot conventions (direction = Sample()'s internal EnemyStandOffset;
 *  hover floor = the CALLER's capsule-bottom FootZ) — the seam absorbs the duality as explicit inputs
 *  instead of silently unifying them, which would change layer picking. */
struct FFPSRFlowQuery
{
	/** Query anchor (enemy actor location). */
	FVector WorldPos = FVector::ZeroVector;
	/** true = resolve the horizontal steer direction (the 2D flow sample today). */
	bool bWantDirection = false;
	/** true = this agent can EXECUTE vertical guidance (a hover archetype — CurrentHoverHeight > 0, whose spring
	 *  consumes SeekZ). The Window3D route is gated on this (merge-gate P1): a GROUND agent handed the window's
	 *  free-air gradient has no means to climb a vertical step (its only Z mover is the walk step-up), so a pure
	 *  (0,0,+1) StepDir would zero its XY steering while suppressing both fallbacks — permanent stall under any
	 *  platform the target stands on. Ground agents stay on the 2D surface field, whose edges are walk-traversable
	 *  by construction. */
	bool bHoverCapable = false;
	/** true = resolve the hover terrain anchor (SampleHoverFloorZ today). */
	bool bWantHoverFloor = false;
	/** Hover-floor pick anchor: capsule BOTTOM Z (caller convention — see AFPSREnemyBase::ApplyGravity). */
	float HoverAnchorFootZ = 0.0f;
	/** Hover-floor look-ahead direction (zero = none). */
	FVector2D LookAheadDirXY = FVector2D::ZeroVector;
	/** Hover-floor corner/pick window (the caller's MaxCrestStepUp budget). */
	float MaxSurfaceDeltaCm = 0.0f;
	/** S3 (ADR 0009 P1): the player this query's caller is currently pursuing, if any. Null = never try the 3D
	 *  window (front-chase / no-target / exit-path callers all leave this unset, ADR §실패 흐름 — the window is
	 *  keyed by OWNING player pawn, so a query with no target player has nothing to match against). Non-null routes
	 *  bWantDirection through UFPSRHoverWindowSubsystem::QueryWindow FIRST, falling back to the 2D surface sample on
	 *  ANY miss (no window adopted, this pawn owns no slot, out of bounds/margin, or the cell unreached). */
	const APawn* TargetPlayerPawn = nullptr;
};

/** QueryFlow's answer. Only the parts requested are written; the rest keep these defaults. */
struct FFPSRFlowResult
{
	/** Horizontal steer direction (unit XY, Z=0 today; a 3D window may set Z in S3). Zero when invalid. */
	FVector Direction = FVector::ZeroVector;
	bool bDirectionValid = false;
	/** Hover terrain anchor surface. */
	float FloorZ = 0.0f;
	FVector FloorNormal = FVector::UpVector;
	bool bFloorValid = false;
	/** Vertical seek target for the hover spring (S3 window fills this; S1 never does). */
	float SeekZ = 0.0f;
	bool bSeekValid = false;
	EFPSRFlowSource Source = EFPSRFlowSource::None;
};

/** Server-authoritative flow-field driver for swarm pathing (P2-B2, U7 multi-layer). Owns a single per-world
 *  UnifiedComputer (a multi-slot unified grid when bUnifiedMultiSlot, else a degenerate single-map grid) and drives
 *  its 0.2s recompute from one scheduler; SlotBounds resolves a location's MapId per slot. (P-G replaced the former
 *  per-map UFPSRFlowFieldComputer registry with this one unified computer.)
 *
 *  Refactor (Codex consult 2026-07-06): the grid/BFS/flow algorithm lives in UFPSRFlowFieldComputer (worldless core
 *  unit-tested by FPSRoguelite.FlowField.Unit). This subsystem owns discovery (bounds volume / floor Z), the recompute
 *  timer, and routing. An unset MapId is the "Default" single-map field, so an untagged L_Map1_City is unchanged.
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

	/** THE movement-consumer seam (ADR 0009 결정 5): every enemy-movement read of any flow field goes through
	 *  here, so swapping the field behind it (the S3 3D window) is a local edit to this function alone.
	 *  Non-movement consumers (damage connectivity, front distance, spawn validation, stamping) deliberately
	 *  do NOT use this — they are 2D-for-life (ADR 0009 수정 ③). */
	bool QueryFlow(const FFPSRFlowQuery& Query, FFPSRFlowResult& Out) const;

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

	/** ADR 0010: adopt an arena's BAKED field — surface AND (ADR 0009 S2) voxel occupancy — as the live one.
	 *  Promoted from the old AdoptArenaSurface(Surface) (2026-08-21, ADR 0009 P1): pulling straight from the
	 *  Arena actor rather than a caller-supplied FFPSRFlowFieldSurfaceData means the surface and the voxel field
	 *  can never come from two different bake assets/transforms by caller mistake — both are fetched from the
	 *  SAME Arena.GetBakeData() + Arena.GetActorTransform() inside this one call.
	 *
	 *  Also re-baselines both fields: a regenerated/swapped arena IS the new topology, and keeping the previous
	 *  snapshot would let ResetDoorTopologyToBaseline resurrect the old arena's walls (2D) or solids (voxel).
	 *  Bumps the topology generation and recomputes, so swarm flow and the origin-aware combat gate are correct
	 *  on return. Server-only; false off authority or when the arena has no usable baked SURFACE (2D remains the
	 *  hard requirement — invariant 5, callers must NOT substitute a world-trace bake). A missing/stale VOXEL bake
	 *  is a soft downgrade, not a rejection: AdoptedVoxels/VoxelBaseline go empty and S3's hover window simply has
	 *  no field here yet (logged once) — see .cpp for why this asymmetry is deliberate. */
	bool AdoptArenaField(const AFPSRArenaActor& Arena);

	/** S2 (ADR 0009 P1): the arena's baked WORLD-space voxel occupancy field, adopted alongside the flow surface
	 *  by AdoptArenaField. The intended reader is S3's hover-swarm window (not yet built) — nothing in this
	 *  subsystem consumes it itself. Empty (HasAdoptedVoxels()==false) until an arena WITH baked voxels has been
	 *  adopted; see AdoptArenaField's soft-downgrade note. Server-authoritative. */
	const FFPSRArenaVoxelData& GetAdoptedVoxels() const { return AdoptedVoxels; }

	/** True only when GetAdoptedVoxels() holds a usable bake (dims + Occupancy length all check out via
	 *  FFPSRArenaVoxelData::IsBakedVoxels()) — the cheap gate a consumer should check before reading it. */
	bool HasAdoptedVoxels() const { return AdoptedVoxels.IsBakedVoxels(); }

	/** U (P-B): a breakable seam door was destroyed (server) — open the unified grid's cross-seam edges the door spanned
	 *  and recompute NOW so swarm flow + the origin-aware combat gate cross it immediately. No-op with no unified field
	 *  (single-map / pre-content) or off authority — the closed seam kept the slots isolated, so nothing changes. The
	 *  door->cell mapping is UFPSRFlowFieldComputer::MapDoorSeamCellPairs.
	 *
	 *  ⚠️ CALLER-LESS since 2026-08-24 — its only caller was AFPSRDoor::HandleBrokenAuthority, and the door class was
	 *  retired (the suppressor replaces it). Kept rather than deleted, on the same footing as the rest of the U
	 *  multi-slot seam code ADR 0010 shelved ("존치하되 쓰지 않는다"): this is the multi-SLOT seam path, which
	 *  NotifyArenaCellsOpened below deliberately does NOT cover (an arena is always a single grid). Anything that
	 *  opens a seam BETWEEN slots has to come back through here. */
	void NotifyDoorBroken(const AActor* Door);

	/** ADR 0010 D7: an arena destructible broke and its AUTHORED cells (FFPSRArenaCells::ComputeDestructibleCells)
	 *  should open — blocked -> open stamp, then a topology generation bump + recompute. Unlike NotifyDoorBroken,
	 *  several of these can land in the SAME frame (an explosion chaining multiple props), so the recompute is
	 *  coalesced to ONE per frame via SetTimerForNextTick rather than run immediately — ADR 0010 D7: "제한해야 할
	 *  것은 동시 파괴 빈도이며, 폭발로 여러 개가 한 프레임에 터지면 BFS를 1회로 합친다". No bUnifiedMultiSlot gate
	 *  (unlike NotifyDoorBroken) — the arena is always a single grid, never a multi-slot seam. Off-authority or no
	 *  grid built yet -> no-op. Called by AFPSRArenaDestructible::HandleBrokenAuthority. */
	void NotifyArenaCellsOpened(TConstArrayView<int32> Cells);

	/** S2 (ADR 0009 P1): voxel-only counterpart to NotifyArenaCellsOpened, called at the SAME break event
	 *  (AFPSRArenaDestructible::HandleBrokenAuthority) but taking the prop's full 3D world AABB rather than its
	 *  2D footprint cells — a destructible has height, so its voxel footprint is not "the 2D cells, extruded".
	 *  Clears AdoptedVoxels' occupancy in place (FFPSRArenaVoxelData::ClearOccupiedAABB). Deliberately triggers NO
	 *  generation bump / recompute of its own: the sibling NotifyArenaCellsOpened call at the same call site
	 *  already advances TopologyGeneration for this break, and a future S3 window is expected to key its
	 *  re-sample off THAT one counter rather than a second, voxel-private one. No-op off authority or with no
	 *  baked voxel field adopted (single warning already logged at adopt time — this stays silent per-call). */
	void NotifyArenaVolumeOpened(const FBox& WorldBounds);

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

	/** Phase A stage-transition carry-over: nearest OPEN location to InWorld on the LIVE adopted grid (thin forward
	 *  to UFPSRFlowFieldComputer::FindNearestOpenLocation — see that function for the exact rules). False with no
	 *  built grid (UnifiedComputer null — off authority, or before the first AdoptArenaField/bake). Array-only, no
	 *  world query (D7). Used by UFPSREnemySpawnSubsystem::CarryEnemiesToNewStage to snap a carried enemy's
	 *  post-delta candidate position onto a walkable cell of the NEW arena. */
	bool FindNearestOpenLocation(const FVector& InWorld, int32 MaxRadiusCells, FVector& OutWorld) const;

	/** Merge-gate P3 교정: the shared MaxRadiusCells budget for a stage-transition carry-over's FindNearestOpenLocation
	 *  snap — enforces in ONE place the contract that a gem lands within the same search radius of the new arena's
	 *  walkable mask as the enemy that might have dropped it. Both UFPSREnemySpawnSubsystem::CarryEnemiesToNewStage
	 *  and UFPSRPickupSubsystem::CarryPickupsToNewStage already include this header for FindNearestOpenLocation
	 *  itself, so both now read this constant from here instead of each keeping its own "kept identical" copy. */
	static constexpr int32 CarrySnapMaxRadiusCells = 16;

private:
	/** Flow direction at WorldLocation from the given map's computer. An unset MapId uses the Default field. If the
	 *  location is outside the passed map's grid (mid-transition across a door), retries against the map whose grid
	 *  actually contains it so flow stays continuous at the boundary. QueryFlow is the only movement-consumption
	 *  surface (ADR 0009 결정 5) — do not call this directly outside QueryFlow's implementation. */
	FVector SampleFlowDirection(const FGameplayTag& MapId, const FVector& WorldLocation) const;

	/** U hover height v2: routing wrapper for UFPSRFlowFieldComputer::SampleHoverFloorZ on the unified field (same
	 *  P-G "one continuous field" routing as SampleFlowDirection — no per-map MapId needed). False with no
	 *  UnifiedComputer (off-authority / pre-build). See the computer's header comment for the sampler contract.
	 *  QueryFlow is the only movement-consumption surface (ADR 0009 결정 5) — do not call this directly outside
	 *  QueryFlow's implementation. */
	bool SampleHoverFloorZ(const FVector& Loc, const FVector2D& FlowDirXY, float AnchorFootZ, float MaxSurfaceDeltaCm, float& OutFloorZ, FVector* OutFloorNormal = nullptr) const;

	/** Shared body of AdoptArenaField for BOTH call sites (2026-08-21, ADR 0009 P1 — collapses what used to be two
	 *  independent code paths: OnWorldBeginPlay built the field inline, ServerRegenerate went through
	 *  AdoptArenaSurface). Builds UnifiedComputer + BakedBaseline from the arena's baked surface and, if the bake
	 *  has voxels, AdoptedVoxels + VoxelBaseline from the same bake asset/transform. Returns false ONLY when the
	 *  arena has no usable baked SURFACE (voxel absence is a soft downgrade handled internally, never a rejection).
	 *
	 *  bBumpGenerationAndRecompute=false is world-begin's case: OnWorldBeginPlay already calls RecomputeAllFields()
	 *  itself exactly once after this returns, and bumping the generation here as well would push a FRESH world's
	 *  TopologyGeneration off 0 — breaking ResetDoorTopologyToBaseline's "a first, unmutated run stays at
	 *  generation 0 so a fresh client's gen-0 ack is instantly satisfied" contract. true is every other caller
	 *  (a stage swap): matches AdoptArenaSurface's old swap-time contract exactly (immediate bump + recompute). */
	bool AdoptArenaFieldInternal(const AFPSRArenaActor& Arena, bool bBumpGenerationAndRecompute);

	/** Merge-gate P2-2 교정: stamp every INTACT AFPSRArenaDestructible in Arena as blocked on the unified grid — the
	 *  2D counterpart of the editor bake, which only probes ECC_WorldStatic and never sees these
	 *  ECC_FPSRDestructible meshes (AFPSRArenaDestructible's constructor comment). Shared by BOTH the adoption-time
	 *  stamp (AdoptArenaFieldInternal) and the break-time RE-stamp (NotifyArenaCellsOpened) — if the two ever
	 *  diverge, an overlapping pair of props can leave a broken one's cell open while its still-intact neighbour is
	 *  still physically standing on it (ComputeCellsFromBoundsXY's Min/Max FloorToInt lets two touching props both
	 *  claim a shared boundary cell, and StampCellBlocked has no owner/refcount concept — whichever prop stamps
	 *  LAST wins the cell). Out-params are zeroed then filled with what was actually stamped; the false return (no
	 *  unified grid yet, or Arena has no usable gen params) leaves them at 0 — callers decide whether that is worth
	 *  logging. */
	bool StampIntactDestructibles(const AFPSRArenaActor& Arena, int32& OutStampedProps, int32& OutStampedCells);

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

	/** Coalescing guard for NotifyArenaCellsOpened: true once this frame's deferred recompute has already been
	 *  scheduled via SetTimerForNextTick, so N destructibles breaking in the same frame produce ONE generation bump
	 *  + ONE BFS instead of N of each (ADR 0010 D7). Reset back to false by the deferred callback itself. */
	bool bArenaOpenRecomputePending = false;

	/** Whether BakedBaseline holds a valid world-begin snapshot (only captured when a unified field is built). */
	bool bHasBaseline = false;

	/** World-begin snapshot of the unified grid's surface graph (all seam doors closed), for ResetDoorTopologyToBaseline.
	 *  Plain member (POD arrays, no UObject refs -> no GC concern); server-only. */
	FFPSRFlowFieldSurfaceData BakedBaseline;

	// --- S2 (ADR 0009 P1): baked 3D voxel occupancy, adopted alongside BakedBaseline/UnifiedComputer by
	//     AdoptArenaFieldInternal. Same lifetime/ownership rules as the surface pair above (plain POD-array
	//     members, no UObject refs, server-only) — the voxel field is transport-only cargo riding the existing
	//     adopt/stamp/reset machinery, not a parallel subsystem. ---

	/** Live WORLD-space voxel occupancy for the currently-adopted arena. Empty (FFPSRArenaVoxelData::IsBakedVoxels()
	 *  false) until an arena WITH baked voxels has been adopted — see AdoptArenaField's soft-downgrade note.
	 *  GetAdoptedVoxels() is the read-only public seam; S3's hover window is the intended (future) consumer. */
	FFPSRArenaVoxelData AdoptedVoxels;

	/** Adopt-time snapshot of AdoptedVoxels, restored by ResetDoorTopologyToBaseline — the voxel-field mirror of
	 *  BakedBaseline. */
	FFPSRArenaVoxelData VoxelBaseline;

	/** Last-seen pause state for the OnRunStateChanged unpause-edge detection (seeded at bind time). */
	bool bWasPaused = false;

	/** Whether HandleRunStateChanged is bound to the GameState delegate (idempotent bind guard). */
	bool bRunStateHandlerBound = false;

	/** S3 (ADR 0009 P1): lazily-resolved, cached the same way GetWorld()->GetSubsystem<T>() results are elsewhere in
	 *  this module — one GetSubsystem lookup per world, not per QueryFlow call. Mutable because QueryFlow (the
	 *  seam this cache serves) is const; TWeakObjectPtr needs no UPROPERTY to stay GC-safe (same reasoning
	 *  UFPSREnemySpawnSubsystem::LastGroundedZByPlayer's key documents). */
	mutable TWeakObjectPtr<UFPSRHoverWindowSubsystem> CachedHoverWindowSubsystem;
};
