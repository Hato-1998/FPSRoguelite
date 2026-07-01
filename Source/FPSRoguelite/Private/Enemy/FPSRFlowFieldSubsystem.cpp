// Copyright Epic Games, Inc. All Rights Reserved.

#include "Enemy/FPSRFlowFieldSubsystem.h"
#include "Enemy/FPSRFlowFieldBoundsVolume.h"
#include "Core/FPSRGameState.h"
#include "Core/FPSRLogChannels.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerStart.h"
#include "CollisionShape.h"
#include "Engine/HitResult.h"
#include "Components/PrimitiveComponent.h"
#include "TimerManager.h"
#include "Containers/Queue.h"
#include "EngineUtils.h"

bool UFPSRFlowFieldSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}
	if (const UWorld* World = Cast<UWorld>(Outer))
	{
		return World->IsGameWorld();
	}
	return false;
}

bool UFPSRFlowFieldSubsystem::HasServerAuthority() const
{
	const UWorld* World = GetWorld();
	return World && World->GetNetMode() != NM_Client;
}

void UFPSRFlowFieldSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	// Grid Z anchor: the obstacle probe is taken at GridOrigin.Z + ObstacleProbeZ, so GridOrigin.Z MUST sit near the
	// playable floor — otherwise (floor far from the world origin, e.g. a basement at Z ~ -1000) the probe samples
	// wall/doorway geometry at the wrong height and mis-marks passable floor openings as blocked, jamming enemies.
	// Detect the floor under a PlayerStart (trace down); fall back to the start's Z, then to the origin.
	float FloorZ = 0.0f;
	for (TActorIterator<APlayerStart> It(&InWorld); It; ++It)
	{
		if (const APlayerStart* Start = *It)
		{
			const FVector StartLoc = Start->GetActorLocation();
			FHitResult Hit;
			FloorZ = InWorld.LineTraceSingleByChannel(Hit, StartLoc, StartLoc - FVector(0.0f, 0.0f, 5000.0f), ECC_WorldStatic)
				? Hit.ImpactPoint.Z : StartLoc.Z;
			break;
		}
	}

	// Data-driven grid bounds (Performance §5-2): if a designer placed an AFPSRFlowFieldBoundsVolume, size the grid to
	// its world AABB; otherwise fall back to the origin-centered HalfExtentFallback grid (existing maps unchanged).
	const AFPSRFlowFieldBoundsVolume* BoundsVolume = nullptr;
	for (TActorIterator<AFPSRFlowFieldBoundsVolume> It(&InWorld); It; ++It)
	{
		if (*It)
		{
			BoundsVolume = *It;
			break;
		}
	}

	float SizeX, SizeY;
	if (BoundsVolume)
	{
		const FBox WB = BoundsVolume->GetWorldBounds();
		const float Override = BoundsVolume->GetCellSizeOverride();
		ActiveCellSize = (Override > 0.0f) ? Override : DefaultCellSize;
		const float StepOverride = BoundsVolume->GetClimbableStepHeightOverride();
		ActiveClimbableStepHeight = (StepOverride > 0.0f) ? StepOverride : DefaultClimbableStepHeight;
		const float ApexOverride = BoundsVolume->GetProbeApexAboveOriginOverride();
		ActiveProbeApexAboveOrigin = (ApexOverride > 0.0f) ? ApexOverride : DefaultProbeApexAboveOrigin;
		GridOrigin = FVector(WB.Min.X, WB.Min.Y, FloorZ);
		SizeX = FMath::Max(ActiveCellSize, WB.GetSize().X);
		SizeY = FMath::Max(ActiveCellSize, WB.GetSize().Y);
		if (WB.GetSize().X < ActiveCellSize || WB.GetSize().Y < ActiveCellSize)
		{
			UE_LOG(LogFPSR, Warning, TEXT("[FlowField] Bounds volume %s has a near-zero extent (%s); check its box size."),
				*BoundsVolume->GetName(), *WB.GetSize().ToString());
		}
	}
	else
	{
		ActiveCellSize = DefaultCellSize;
		ActiveClimbableStepHeight = DefaultClimbableStepHeight;
		ActiveProbeApexAboveOrigin = DefaultProbeApexAboveOrigin;
		GridOrigin = FVector(-HalfExtentFallback, -HalfExtentFallback, FloorZ);
		SizeX = 2.0f * HalfExtentFallback;
		SizeY = 2.0f * HalfExtentFallback;
	}

	GridDimX = FMath::Max(1, FMath::CeilToInt(SizeX / ActiveCellSize));
	GridDimY = FMath::Max(1, FMath::CeilToInt(SizeY / ActiveCellSize));

	// Perf guard: if the grid would exceed the per-tick scan budget, GROW the cell size (coarser grid) so the cell
	// count fits while still covering the full region — clamping the dimensions instead would silently truncate the
	// playable area (a regression). CeilToInt keeps GridDim*ActiveCellSize >= Size, so coverage is preserved.
	if (static_cast<int64>(GridDimX) * GridDimY > MaxTotalCells || GridDimX > MaxGridDimPerAxis || GridDimY > MaxGridDimPerAxis)
	{
		const float CellForTotal = ActiveCellSize * FMath::Sqrt((static_cast<float>(GridDimX) * GridDimY) / static_cast<float>(MaxTotalCells));
		const float CellForAxisX = SizeX / static_cast<float>(MaxGridDimPerAxis);
		const float CellForAxisY = SizeY / static_cast<float>(MaxGridDimPerAxis);
		ActiveCellSize = FMath::Max3(CellForTotal, CellForAxisX, CellForAxisY);
		GridDimX = FMath::Max(1, FMath::CeilToInt(SizeX / ActiveCellSize));
		GridDimY = FMath::Max(1, FMath::CeilToInt(SizeY / ActiveCellSize));
		UE_LOG(LogFPSR, Warning,
			TEXT("[FlowField] Bounds exceed the cell budget (%d); grew cell size to %.0f -> %dx%d (%d cells). Coverage preserved; set CellSizeOverride or shrink the volume for finer routing."),
			MaxTotalCells, ActiveCellSize, GridDimX, GridDimY, GridDimX * GridDimY);
	}

	DistField.Init(MAX_int32, GridDimX * GridDimY);
	FlowField.Init(FVector2D::ZeroVector, GridDimX * GridDimY);
	BlockedField.Init(false, GridDimX * GridDimY);
	EdgeTraversable.Init(false, GridDimX * GridDimY * 2); // default blocked; BuildObstacleMask opens passable edges
	CellFloorZ.Init(GridOrigin.Z, GridDimX * GridDimY);  // default = grid floor; BuildObstacleMask refines per cell (MAX_flt = no floor)

	UE_LOG(LogFPSR, Log, TEXT("[FlowField] Grid %dx%d cell=%.0f origin=%s (%s)."),
		GridDimX, GridDimY, ActiveCellSize, *GridOrigin.ToString(),
		BoundsVolume ? TEXT("bounds volume") : TEXT("origin-centered fallback"));

	if (HasServerAuthority())
	{
		BuildObstacleMask(); // once: fixed map; BFS then routes around blocked cells

		InWorld.GetTimerManager().SetTimer(
			RecomputeTimerHandle, this, &UFPSRFlowFieldSubsystem::RecomputeField,
			FlowUpdateInterval, true);
	}
}

void UFPSRFlowFieldSubsystem::BuildObstacleMask()
{
	UWorld* World = GetWorld();
	if (!World || GridDimX <= 0 || GridDimY <= 0)
	{
		return;
	}

	FCollisionObjectQueryParams ObjParams;
	ObjParams.AddObjectTypesToQuery(ECC_WorldStatic);
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(FPSRFlowObstacle), false);

	// U7 Part A — per-cell REACHABLE walking-surface height (2.5D height field). A naive "take the topmost walkable hit"
	// would wrongly accept a wall/roof/ceiling top (a walkable-normal surface below the apex) as floor, run the later
	// occupancy probe ABOVE it, and mark the cell/edges traversable — steering the swarm into walls or breaking indoor
	// door/wall connectivity (Codex P2). Instead, in three one-time steps on the fixed map:
	//   (1) collect EVERY up-facing static surface per cell (candidate walking heights) via one down multi-hit trace,
	//   (2) SEED from the known ground floor (GridOrigin.Z, the PlayerStart trace), then
	//   (3) FLOOD outward, accepting a neighbour surface only if it is within one climbable step (ActiveClimbableStepHeight)
	//       of the current cell's surface — so the walking surface climbs ramps/stairs onto platforms but NEVER jumps to a
	//       disconnected ceiling/obstacle top. Cells the flood never reaches have no reachable floor -> blocked, which
	//       preserves the pre-U7 "escape flow toward open ground" behaviour for obstacle-top cells.
	// Same ECC_WorldStatic channel the enemy ground-follow (AFPSREnemyBase::ApplyGravity) uses. Never in the 0.2s recompute.
	const int32 NumCells = GridDimX * GridDimY;
	const float ApexZ = GridOrigin.Z + ActiveProbeApexAboveOrigin;

	// Max height an agent can traverse between two orthogonally-adjacent cells:
	//  - a FLAT surface reached across a boundary is a vertical STEP -> up to one ClimbableStepHeight (mirrors MaxStepHeight);
	//  - a walkable RAMP surface changes height continuously, so its center-to-center rise over one cell can reach the
	//    max walkable grade (ActiveCellSize * tan(max walkable angle)); ApplyGravity climbs it incrementally each recheck.
	// Shared by the floor flood (below) and the Pass-2 edge gate so the reachable-floor and the BFS-traversable edges agree.
	const float MaxSlopeTan = FMath::Sqrt(FMath::Max(0.0f, 1.0f - WalkableNormalZ * WalkableNormalZ)) / WalkableNormalZ;
	const float RampAllowance = ActiveCellSize * MaxSlopeTan;
	auto MaxTraverseDelta = [&](bool bSloped) -> float
	{
		return bSloped ? FMath::Max(ActiveClimbableStepHeight, RampAllowance) : ActiveClimbableStepHeight;
	};

	// (1) Candidate up-facing surfaces per cell, {X=world Z, Y=surface normal Z} (build-time scratch; freed at function end).
	TArray<TArray<FVector2f>> CellCandidates;
	CellCandidates.SetNum(NumCells);
	for (int32 CY = 0; CY < GridDimY; ++CY)
	{
		for (int32 CX = 0; CX < GridDimX; ++CX)
		{
			const int32 Cell = CY * GridDimX + CX;
			const float CenterX = GridOrigin.X + (CX + 0.5f) * ActiveCellSize;
			const float CenterY = GridOrigin.Y + (CY + 0.5f) * ActiveCellSize;
			const FVector Apex(CenterX, CenterY, ApexZ);
			const FVector Base(CenterX, CenterY, ApexZ - MaxProbeDrop);

			// Collect ALL stacked walkable surfaces in this column (e.g. the floor UNDER a bridge/ceiling AND the bridge
			// top). A single LineTraceMultiByObjectType stops at the first blocking hit (engine: "only the single closest
			// blocking result will be generated"), hiding lower floors — so re-trace from the apex, ignoring each surface's
			// mesh in turn, until nothing remains below (capped at MaxColumnSurfaces). One-time on the fixed map.
			FCollisionQueryParams IterQP(SCENE_QUERY_STAT(FPSRFlowFloor), false);
			for (int32 Surface = 0; Surface < MaxColumnSurfaces; ++Surface)
			{
				FHitResult Hit;
				if (!World->LineTraceSingleByObjectType(Hit, Apex, Base, ObjParams, IterQP))
				{
					break; // no more static geometry below in this column
				}
				if (Hit.ImpactNormal.Z >= WalkableNormalZ) // up-facing (walkable) surface; rejects undersides / steep faces
				{
					CellCandidates[Cell].Add(FVector2f(static_cast<float>(Hit.ImpactPoint.Z), static_cast<float>(Hit.ImpactNormal.Z)));
				}
				const UPrimitiveComponent* HitComp = Hit.GetComponent();
				if (!HitComp)
				{
					break;
				}
				IterQP.AddIgnoredComponent(HitComp); // skip this surface's mesh so the next trace reveals the surface below it
			}
		}
	}

	// CellSloped[Cell] = the assigned walking surface is a ramp (not near-flat) — gates the ramp height allowance above.
	TArray<bool> CellSloped;
	CellSloped.Init(false, NumCells);

	// (2) Seed the flood from every cell holding a candidate within one step of the known (flat) ground floor.
	for (int32 i = 0; i < NumCells; ++i)
	{
		CellFloorZ[i] = MAX_flt; // unassigned
	}
	TQueue<int32> FloorFrontier;
	for (int32 Cell = 0; Cell < NumCells; ++Cell)
	{
		for (const FVector2f& Cand : CellCandidates[Cell])
		{
			if (FMath::Abs(Cand.X - GridOrigin.Z) <= ActiveClimbableStepHeight)
			{
				CellFloorZ[Cell] = Cand.X;
				CellSloped[Cell] = Cand.Y < FlatNormalZThreshold;
				FloorFrontier.Enqueue(Cell);
				break;
			}
		}
	}

	// (3) Flood: a neighbour inherits its candidate closest to the current surface, within the step-or-ramp allowance
	//     for that candidate (a flat ledge is step-limited, a ramp surface is grade-limited) — so the walking surface
	//     climbs ramps/stairs onto platforms but never jumps a cliff or onto a disconnected wall/ceiling top.
	static const int32 FDX[4] = { 1, -1, 0, 0 };
	static const int32 FDY[4] = { 0, 0, 1, -1 };
	int32 FloodCell;
	while (FloorFrontier.Dequeue(FloodCell))
	{
		const int32 CX = FloodCell % GridDimX;
		const int32 CY = FloodCell / GridDimX;
		const float H = CellFloorZ[FloodCell];
		const bool bFromSloped = CellSloped[FloodCell]; // leaving a ramp keeps the ramp allowance even onto a flat platform
		for (int32 N = 0; N < 4; ++N)
		{
			const int32 NX = CX + FDX[N];
			const int32 NY = CY + FDY[N];
			if (NX < 0 || NX >= GridDimX || NY < 0 || NY >= GridDimY)
			{
				continue;
			}
			const int32 NIdx = NY * GridDimX + NX;
			if (CellFloorZ[NIdx] != MAX_flt)
			{
				continue; // already assigned
			}
			float BestH = MAX_flt;
			bool bBestSloped = false;
			float BestDelta = MAX_flt;
			for (const FVector2f& Cand : CellCandidates[NIdx])
			{
				const bool bCandSloped = Cand.Y < FlatNormalZThreshold;
				const float D = FMath::Abs(Cand.X - H);
				// Either the cell we're leaving or the surface we're stepping onto being a ramp earns the ramp allowance
				// (matches the Pass-2 edge gate's "either sloped"); store the neighbour's OWN slope for its later edges.
				if (D <= MaxTraverseDelta(bFromSloped || bCandSloped) && D < BestDelta)
				{
					BestDelta = D;
					BestH = Cand.X;
					bBestSloped = bCandSloped;
				}
			}
			if (BestH != MAX_flt)
			{
				CellFloorZ[NIdx] = BestH;
				CellSloped[NIdx] = bBestSloped;
				FloorFrontier.Enqueue(NIdx);
			}
		}
	}

	// Cells the flood never reached have no reachable walking surface -> block (escape-flow toward open ground, not zero-flow).
	for (int32 Cell = 0; Cell < NumCells; ++Cell)
	{
		if (CellFloorZ[Cell] == MAX_flt)
		{
			BlockedField[Cell] = true;
		}
	}

	// Pass 2 — clearance-aware probing (balance/pass2) + U7 height gate. Both masks built once on the fixed map:
	//  (1) OCCUPANCY (BlockedField): an agent-footprint box at the cell CENTER, at THIS cell's own floor height, so a
	//      cell a capsule can stand in isn't blocked by a wall that merely clips its far edge (narrow-doorway fix).
	//  (2) EDGE (EdgeTraversable): opened only if BOTH cells have a floor, the inter-cell height change is traversable
	//      (MaxTraverseDelta — a flat ledge is step-limited, a ramp is grade-limited; a taller flat-to-flat delta is a
	//      cliff/wall), AND no thin wall straddles a footprint-wide gap on the shared boundary (U7 Part B: a
	//      footprint-width box, NOT the old full-cell width, so a doorway narrower than a cell on a boundary is no
	//      longer over-blocked while a full-boundary wall still is).
	const FCollisionShape OccupancyBox = FCollisionShape::MakeBox(
		FVector(AgentFootprintRadius, AgentFootprintRadius, ObstacleProbeHalfHeight));
	const FCollisionShape EdgeBox = FCollisionShape::MakeBox(
		FVector(AgentFootprintRadius, AgentFootprintRadius, ObstacleProbeHalfHeight));

	for (int32 CY = 0; CY < GridDimY; ++CY)
	{
		for (int32 CX = 0; CX < GridDimX; ++CX)
		{
			const int32 Cell = CY * GridDimX + CX;
			if (CellFloorZ[Cell] == MAX_flt)
			{
				continue; // no floor -> already blocked; its edges stay closed (default false)
			}
			const float CenterX = GridOrigin.X + (CX + 0.5f) * ActiveCellSize;
			const float CenterY = GridOrigin.Y + (CY + 0.5f) * ActiveCellSize;
			const float CellProbeZ = CellFloorZ[Cell] + ObstacleProbeZ; // knee/wall height above THIS cell's floor

			// (1) Occupancy: can a footprint-sized agent stand at this cell's center?
			if (World->OverlapAnyTestByObjectType(FVector(CenterX, CenterY, CellProbeZ), FQuat::Identity, ObjParams, OccupancyBox, QueryParams))
			{
				BlockedField[Cell] = true;
			}

			// (2a) +X edge (shared boundary with cell CX+1).
			if (CX + 1 < GridDimX)
			{
				const int32 NCell = Cell + 1;
				const float NFloor = CellFloorZ[NCell];
				if (NFloor != MAX_flt && FMath::Abs(CellFloorZ[Cell] - NFloor) <= MaxTraverseDelta(CellSloped[Cell] || CellSloped[NCell]))
				{
					const float EdgeZ = FMath::Max(CellFloorZ[Cell], NFloor) + ObstacleProbeZ;
					const FVector EdgeCenter(GridOrigin.X + (CX + 1) * ActiveCellSize, CenterY, EdgeZ);
					if (!World->OverlapAnyTestByObjectType(EdgeCenter, FQuat::Identity, ObjParams, EdgeBox, QueryParams))
					{
						EdgeTraversable[Cell * 2 + 0] = true;
					}
				}
			}

			// (2b) +Y edge (shared boundary with cell CY+1).
			if (CY + 1 < GridDimY)
			{
				const int32 NCell = Cell + GridDimX;
				const float NFloor = CellFloorZ[NCell];
				if (NFloor != MAX_flt && FMath::Abs(CellFloorZ[Cell] - NFloor) <= MaxTraverseDelta(CellSloped[Cell] || CellSloped[NCell]))
				{
					const float EdgeZ = FMath::Max(CellFloorZ[Cell], NFloor) + ObstacleProbeZ;
					const FVector EdgeCenter(CenterX, GridOrigin.Y + (CY + 1) * ActiveCellSize, EdgeZ);
					if (!World->OverlapAnyTestByObjectType(EdgeCenter, FQuat::Identity, ObjParams, EdgeBox, QueryParams))
					{
						EdgeTraversable[Cell * 2 + 1] = true;
					}
				}
			}
		}
	}

	int32 BlockedCount = 0;
	for (const bool bBlocked : BlockedField)
	{
		if (bBlocked)
		{
			++BlockedCount;
		}
	}
	UE_LOG(LogFPSR, Log, TEXT("[FlowField] Obstacle mask: %d/%d cells blocked (clearance+height-aware, footprint %.0fcm, step<=%.0fcm, ramp<=%.0fcm, apex+%.0fcm)."),
		BlockedCount, NumCells, AgentFootprintRadius, ActiveClimbableStepHeight, RampAllowance, ActiveProbeApexAboveOrigin);

	// Diagnostic (Codex P2): if almost every cell has no reachable floor, the flood likely never found the ground —
	// most often because the probe apex sits ABOVE a solid ceiling that the down-trace can't see past, or the floor
	// isn't a static mesh. Warn the designer to lower the bounds volume's ProbeApexAboveOriginOverride below the ceiling.
	if (NumCells > 0 && BlockedCount * 10 >= NumCells * 9)
	{
		UE_LOG(LogFPSR, Warning,
			TEXT("[FlowField] %d%% of cells have NO reachable floor — the flood may not have found the ground floor. If this map has a solid ceiling below the probe apex (apex = grid floor + %.0fcm), set the bounds volume's ProbeApexAboveOriginOverride below the ceiling, or ensure the floor is a separate WorldStatic mesh."),
			(BlockedCount * 100) / NumCells, ActiveProbeApexAboveOrigin);
	}
}

void UFPSRFlowFieldSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RecomputeTimerHandle);
	}
	Super::Deinitialize();
}

int32 UFPSRFlowFieldSubsystem::FindNearestOpenCell(int32 FromCell, const FVector& PlayerLocation) const
{
	if (FromCell == INDEX_NONE || GridDimX <= 0 || GridDimY <= 0)
	{
		return INDEX_NONE;
	}
	const UWorld* World = GetWorld();
	const int32 CX = FromCell % GridDimX;
	const int32 CY = FromCell / GridDimX;

	FCollisionObjectQueryParams ObjParams;
	ObjParams.AddObjectTypesToQuery(ECC_WorldStatic);
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(FPSRFlowSourceLOS), false);

	int32 Fallback = INDEX_NONE; // nearest open cell regardless of line-of-sight
	for (int32 R = 1; R <= SourceSearchRadius; ++R)
	{
		for (int32 dy = -R; dy <= R; ++dy)
		{
			for (int32 dx = -R; dx <= R; ++dx)
			{
				if (FMath::Max(FMath::Abs(dx), FMath::Abs(dy)) != R)
				{
					continue; // only the outer ring at radius R (nearest-first)
				}
				const int32 NX = CX + dx;
				const int32 NY = CY + dy;
				if (NX < 0 || NX >= GridDimX || NY < 0 || NY >= GridDimY)
				{
					continue;
				}
				const int32 NIdx = NY * GridDimX + NX;
				if (BlockedField[NIdx])
				{
					continue;
				}
				if (Fallback == INDEX_NONE)
				{
					Fallback = NIdx;
				}
				// Prefer a cell the player can actually see (no static wall between) — keeps the snapped source
				// on the player's side of the obstacle. First LOS-clear cell (nearest-first) wins.
				const FVector CellCenter(
					GridOrigin.X + (NX + 0.5f) * ActiveCellSize,
					GridOrigin.Y + (NY + 0.5f) * ActiveCellSize,
					PlayerLocation.Z);
				if (World == nullptr ||
					!World->LineTraceTestByObjectType(PlayerLocation, CellCenter, ObjParams, QueryParams))
				{
					return NIdx;
				}
			}
		}
	}
	return Fallback;
}

int32 UFPSRFlowFieldSubsystem::WorldToCellIndex(const FVector& WorldLocation) const
{
	if (GridDimX <= 0 || GridDimY <= 0)
	{
		return INDEX_NONE;
	}
	const int32 CX = FMath::FloorToInt((WorldLocation.X - GridOrigin.X) / ActiveCellSize);
	const int32 CY = FMath::FloorToInt((WorldLocation.Y - GridOrigin.Y) / ActiveCellSize);
	if (CX < 0 || CX >= GridDimX || CY < 0 || CY >= GridDimY)
	{
		return INDEX_NONE;
	}
	return CY * GridDimX + CX;
}

bool UFPSRFlowFieldSubsystem::IsEdgeTraversable(int32 CellA, int32 CellB) const
{
	if (GridDimX <= 0)
	{
		return false;
	}
	const int32 AX = CellA % GridDimX, AY = CellA / GridDimX;
	const int32 BX = CellB % GridDimX, BY = CellB / GridDimX;
	// Canonicalize to the lower cell's +X / +Y edge so both directions read the same stored entry.
	if (AY == BY && FMath::Abs(AX - BX) == 1)
	{
		const int32 LeftCell = (AX < BX) ? CellA : CellB; // owns the +X edge to its right neighbor
		return EdgeTraversable[LeftCell * 2 + 0];
	}
	if (AX == BX && FMath::Abs(AY - BY) == 1)
	{
		const int32 BottomCell = (AY < BY) ? CellA : CellB; // owns the +Y edge to its top neighbor
		return EdgeTraversable[BottomCell * 2 + 1];
	}
	return false; // not orthogonally adjacent — the BFS/flow passes only query the 4-neighborhood
}

void UFPSRFlowFieldSubsystem::RecomputeField()
{
	// PERF GUARDRAIL (U7): this 0.2s multi-source BFS + steepest-descent runs over up to MaxTotalCells and the 500-enemy
	// swarm samples the result every tick — it MUST stay pure O(1) array math. NO world/collision query, NO per-cell
	// trace, NO height re-sample here: all obstacle / height / clearance data is pre-baked ONCE into BlockedField /
	// EdgeTraversable / CellFloorZ by BuildObstacleMask. Keep it that way (§5-2, adversarial perf audit).
	if (!HasServerAuthority() || GridDimX <= 0 || GridDimY <= 0)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Skip the recompute during the global freeze (§2-2): enemy movement is gated off, so nothing samples the
	// field while paused — recomputing toward stationary players is wasted work (W1 P3-2).
	if (const AFPSRGameState* GS = World->GetGameState<AFPSRGameState>())
	{
		if (GS->IsRunPaused())
		{
			return;
		}
	}

	const int32 NumCells = GridDimX * GridDimY;
	for (int32 i = 0; i < NumCells; ++i)
	{
		DistField[i] = MAX_int32;
	}

	// Seed BFS from every alive player's cell (multi-source -> field points to NEAREST player).
	TQueue<int32> Frontier;
	bool bAnySource = false;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		if (const APlayerController* PC = It->Get())
		{
			if (const APawn* PlayerPawn = PC->GetPawn())
			{
				int32 Cell = WorldToCellIndex(PlayerPawn->GetActorLocation());
					// If the player's coarse cell is blocked (standing next to geometry), seed from the nearest
					// open cell instead — otherwise the BFS expands from a wall cell across the obstacle.
					if (Cell != INDEX_NONE && BlockedField[Cell])
					{
						Cell = FindNearestOpenCell(Cell, PlayerPawn->GetActorLocation());
					}
				if (Cell != INDEX_NONE && DistField[Cell] != 0)
				{
					DistField[Cell] = 0;
					Frontier.Enqueue(Cell);
					bAnySource = true;
				}
			}
		}
	}

	if (!bAnySource)
	{
		bFieldReady = false;
		return;
	}

	// 4-connected BFS (uniform cost) -> integration field.
	static const int32 DX4[4] = { 1, -1, 0, 0 };
	static const int32 DY4[4] = { 0, 0, 1, -1 };
	int32 Current;
	while (Frontier.Dequeue(Current))
	{
		const int32 CurDist = DistField[Current];
		const int32 CX = Current % GridDimX;
		const int32 CY = Current / GridDimX;
		for (int32 N = 0; N < 4; ++N)
		{
			const int32 NX = CX + DX4[N];
			const int32 NY = CY + DY4[N];
			if (NX < 0 || NX >= GridDimX || NY < 0 || NY >= GridDimY)
			{
				continue;
			}
			const int32 NIdx = NY * GridDimX + NX;
			if (BlockedField[NIdx])
			{
				continue; // never propagate flow through static obstacles (walls/buildings)
			}
			if (!IsEdgeTraversable(Current, NIdx))
			{
				continue; // Part B: a thin wall on the shared boundary blocks this edge even if both cells are open
			}
			if (DistField[NIdx] > CurDist + 1)
			{
				DistField[NIdx] = CurDist + 1;
				Frontier.Enqueue(NIdx);
			}
		}
	}

	// Flow per cell: steepest descent toward the lowest-distance of the 8 neighbors.
	static const int32 DX8[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };
	static const int32 DY8[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
	for (int32 CY = 0; CY < GridDimY; ++CY)
	{
		for (int32 CX = 0; CX < GridDimX; ++CX)
		{
			const int32 Idx = CY * GridDimX + CX;

			// Compute steepest descent for EVERY cell, including blocked / unreachable ones (DistField == MAX):
			// an enemy standing in a partially-obstructed coarse cell still gets an escape direction toward the
			// nearest reachable open neighbor instead of zero flow (which would jam it against geometry). Codex.
			int32 BestDist = DistField[Idx];
			int32 BestNX = -1;
			int32 BestNY = -1;
			for (int32 N = 0; N < 8; ++N)
			{
				const int32 NX = CX + DX8[N];
				const int32 NY = CY + DY8[N];
				if (NX < 0 || NX >= GridDimX || NY < 0 || NY >= GridDimY)
				{
					continue;
				}
				const int32 NIdx = NY * GridDimX + NX;
				// Don't skip blocked neighbors outright: unreachable ones already have MAX distance (never
				// chosen below), while a blocked cell with a finite distance is a player SOURCE we must still be
				// able to point at (a player standing next to geometry, Codex P2).
				if (N < 4)
				{
					// Orthogonal: must NOT point the flow across a blocked edge — a thin boundary wall blocks an
					// edge even when both cells are open (Part B). The BFS routes around it, so without this gate
					// this cell could point straight at a lower-distance neighbor through the wall and jam enemies
					// against it (the failure mode the BFS-only gate misses). EXCEPTION: if THIS cell is itself
					// blocked, keep the escape behavior (steer toward any open neighbor rather than jam). An open
					// cell always has its BFS-parent edge open, so it still gets a valid descent direction.
					if (!BlockedField[Idx] && !IsEdgeTraversable(Idx, NIdx))
					{
						continue;
					}
				}
				else
				{
					// Diagonal corner-clearance: take a diagonal only if BOTH orthogonal CELLS are open (the swept
					// capsule can't clip a blocked corner), AND — for a non-escaping open cell — BOTH orthogonal
					// EDGES are open, so the diagonal can't cut across a thin wall the edge test rejects (Part B).
					const int32 OrthoA = CY * GridDimX + NX; // (NX, CY)
					const int32 OrthoB = NY * GridDimX + CX; // (CX, NY)
					if (BlockedField[OrthoA] || BlockedField[OrthoB])
					{
						continue;
					}
					if (!BlockedField[Idx] && (!IsEdgeTraversable(Idx, OrthoA) || !IsEdgeTraversable(Idx, OrthoB)))
					{
						continue;
					}
				}
				if (DistField[NIdx] < BestDist)
				{
					BestDist = DistField[NIdx];
					BestNX = NX;
					BestNY = NY;
				}
			}

			if (BestNX >= 0)
			{
				const FVector2D Dir(static_cast<float>(BestNX - CX), static_cast<float>(BestNY - CY));
				FlowField[Idx] = Dir.GetSafeNormal();
			}
			else
			{
				FlowField[Idx] = FVector2D::ZeroVector; // at/near a source
			}
		}
	}

	bFieldReady = true;
}

FVector UFPSRFlowFieldSubsystem::SampleFlowDirection(const FVector& WorldLocation) const
{
	if (!bFieldReady)
	{
		return FVector::ZeroVector;
	}
	const int32 Idx = WorldToCellIndex(WorldLocation);
	if (Idx == INDEX_NONE)
	{
		return FVector::ZeroVector;
	}
	const FVector2D F = FlowField[Idx];
	return FVector(F.X, F.Y, 0.0f);
}
