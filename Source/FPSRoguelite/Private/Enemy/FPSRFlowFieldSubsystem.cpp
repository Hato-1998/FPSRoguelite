// Copyright Epic Games, Inc. All Rights Reserved.

#include "Enemy/FPSRFlowFieldSubsystem.h"
#include "Enemy/FPSRFlowFieldBoundsVolume.h"
#include "Core/FPSRGameState.h"
#include "Core/FPSRLogChannels.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/PlayerStart.h"
#include "CollisionShape.h"
#include "Engine/HitResult.h"
#include "TimerManager.h"
#include "Containers/Queue.h"
#include "EngineUtils.h"

#if !UE_BUILD_SHIPPING
#include "DrawDebugHelpers.h"
#include "HAL/IConsoleManager.h"
static TAutoConsoleVariable<int32> CVarFlowFieldDebug(
	TEXT("FPSR.FlowField.Debug"), 0,
	TEXT("Draw the swarm flow field near players (1 = flow arrows + blocked cells, per surface at each layer's floor height; rank>=1 arrows tinted cyan). Dev only."),
	ECVF_Cheat);
#endif

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

	// Grid Z anchor: the obstacle probe is taken relative to GridOrigin.Z, so GridOrigin.Z MUST sit near the playable
	// floor — otherwise (floor far from the world origin, e.g. a basement at Z ~ -1000) the probe apex/ground-seed
	// heights are wrong and passable floor openings get mis-marked as blocked, jamming enemies. Detect the floor under
	// a PlayerStart (trace down); fall back to the start's Z, then to the origin.
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
	// playable area (a regression). CeilToInt keeps GridDim*ActiveCellSize >= Size, so coverage is preserved. NOTE (U7):
	// MaxTotalCells caps BASE (XY) cells; the multi-layer arrays are NumLayers x this many surface slots (see header).
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

	// Per-surface arrays (U7 multi-layer): sized NumCells * NumLayers, indexed by SurfIndex(Cell,Rank). CellFloorZ
	// defaults to MAX_flt = "no surface at this (cell,rank)"; BuildObstacleMask fills the reachable surfaces. EdgeMask is
	// per-cell (x2 directions), default 0 = all rank-pairs blocked (BuildObstacleMask opens the passable ones).
	const int32 NumSurfaces = GridDimX * GridDimY * NumLayers;
	DistField.Init(MAX_int32, NumSurfaces);
	FlowField.Init(FVector2D::ZeroVector, NumSurfaces);
	BlockedField.Init(false, NumSurfaces);
	CellFloorZ.Init(MAX_flt, NumSurfaces);
	EdgeMask.Init(0, GridDimX * GridDimY * 2);

	UE_LOG(LogFPSR, Log, TEXT("[FlowField] Grid %dx%d cell=%.0f layers=%d origin=%s (%s)."),
		GridDimX, GridDimY, ActiveCellSize, NumLayers, *GridOrigin.ToString(),
		BoundsVolume ? TEXT("bounds volume") : TEXT("origin-centered fallback"));

	if (HasServerAuthority())
	{
		BuildObstacleMask(); // once: fixed map; BFS then routes around blocked surfaces and between layers via stairs

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

	// Enforce the movement invariant (Codex P2): a FLAT ledge taller than the enemy per-recheck ground snap
	// (MaxClimbableStepHeight = GroundSnapTolerance) can be opened by the field but NOT climbed by ApplyGravity, jamming
	// enemies. Cap the step height regardless of any designer override; the (larger) ramp allowance is unaffected — a
	// continuous ramp is climbed incrementally across many rechecks, not in one snap.
	if (ActiveClimbableStepHeight > MaxClimbableStepHeight)
	{
		UE_LOG(LogFPSR, Warning,
			TEXT("[FlowField] ClimbableStepHeight %.0fcm exceeds the enemy ground-snap limit %.0fcm; clamping (a taller flat step can't be climbed)."),
			ActiveClimbableStepHeight, MaxClimbableStepHeight);
		ActiveClimbableStepHeight = MaxClimbableStepHeight;
	}

	FCollisionObjectQueryParams ObjParams;
	ObjParams.AddObjectTypesToQuery(ECC_WorldStatic);
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(FPSRFlowObstacle), false);

	// U7 multi-layer surface graph. On the fixed map, in build-time stages:
	//   (1) per column: collect EVERY up-facing static surface (Z-step re-trace) + a 5-point sub-sample to derive each
	//       surface's per-axis slope (ramp vs flat, for the grade allowance and R2 side-edge fix);
	//   (2) cluster each cell's candidates into up to NumLayers ranked surfaces (rank 0 = lowest) -> CellFloorZ;
	//   (3) FLOOD reachability over SURFACES (frontier keyed by Surf, NOT bare cell) from the ground floor + PlayerStart,
	//       accepting a neighbour surface only within one step/grade of the current surface — so ramps/stairs transitively
	//       lift the walking surface onto a deck but never jump to a disconnected wall/ceiling top;
	//   (4) bake per-surface occupancy (BlockedField) + per-edge rank-pairing (EdgeMask).
	// Same ECC_WorldStatic channel the enemy ground-follow (AFPSREnemyBase::ApplyGravity) uses. Never in the 0.2s recompute.
	const int32 NumCells = GridDimX * GridDimY;
	const int32 NumSurf = NumCells * NumLayers;
	const float ApexZ = GridOrigin.Z + ActiveProbeApexAboveOrigin;

	// Reset per-surface state (BuildObstacleMask is authoritative and idempotent — safe even if ever re-run on the same map).
	for (int32 i = 0; i < NumSurf; ++i) { CellFloorZ[i] = MAX_flt; BlockedField[i] = false; }
	for (int32 i = 0; i < NumCells * 2; ++i) { EdgeMask[i] = 0; }

	// Max height an agent can traverse across one cell boundary: a FLAT surface reached across a boundary is a vertical
	// STEP (<= one ClimbableStepHeight); a walkable RAMP surface changes height continuously so its center-to-center rise
	// can reach the max walkable grade (ActiveCellSize * tan(max walkable angle)); ApplyGravity climbs it incrementally.
	const float MaxSlopeTan = FMath::Sqrt(FMath::Max(0.0f, 1.0f - WalkableNormalZ * WalkableNormalZ)) / WalkableNormalZ;
	const float RampAllowance = ActiveCellSize * MaxSlopeTan;

	// CellFloorZ currently holds the Init default (MAX_flt); the clustering below writes each cell's ranked surface Zs.
	// AxisSloped[Surf*2 + axis] (build scratch): this surface is a ramp along X(0) / Y(1) — grade allowance is granted
	// ONLY along the sloped axis's edges (R2: a cliff BESIDE a ramp keeps the flat step limit, so it stays blocked).
	TArray<uint8> AxisSloped;
	AxisSloped.Init(0, NumSurf * 2);

	// Slope sub-sample: a surface's sub-hits within this Z band belong to it. Wide enough to include the ramp's own tilt
	// across the sub-radius (<= ~0.35 * RampAllowance from center) but far below a full-storey layer gap, so a sub-hit on
	// the OTHER layer is not misattributed (content constraint: layers are authored >= a storey apart).
	const float SubBand = 0.5f * RampAllowance + ActiveClimbableStepHeight;
	static const float SubDX[5] = { 0.0f, 1.0f, -1.0f, 0.0f, 0.0f };
	static const float SubDY[5] = { 0.0f, 0.0f, 0.0f, 1.0f, -1.0f };
	const float SubR = ActiveCellSize * 0.35f;

	int32 DroppedSurfaceCells = 0; // cells with more than NumLayers stacked walkables (extras dropped)

	// (1)+(2) per cell: collect candidates, cluster into ranks, derive per-surface per-axis slope.
	TArray<FVector2f> Candidates; // scratch, reused per cell: {X = surface Z, Y = normal Z}
	for (int32 CY = 0; CY < GridDimY; ++CY)
	{
		for (int32 CX = 0; CX < GridDimX; ++CX)
		{
			const int32 Cell = CY * GridDimX + CX;
			const float CenterX = GridOrigin.X + (CX + 0.5f) * ActiveCellSize;
			const float CenterY = GridOrigin.Y + (CY + 0.5f) * ActiveCellSize;
			const FVector Base(CenterX, CenterY, ApexZ - MaxProbeDrop);

			// Collect ALL stacked walkable surfaces in this column (floor UNDER a bridge AND the bridge top, even one
			// merged mesh). A single trace stops at the first blocking hit, so re-trace DOWN, restarting just below each
			// hit, until nothing remains below (capped). One-time on the fixed map.
			Candidates.Reset();
			FCollisionQueryParams IterQP(SCENE_QUERY_STAT(FPSRFlowFloor), false);
			float ProbeStartZ = ApexZ;
			for (int32 Surface = 0; Surface < MaxColumnSurfaces; ++Surface)
			{
				FHitResult Hit;
				if (!World->LineTraceSingleByObjectType(Hit, FVector(CenterX, CenterY, ProbeStartZ), Base, ObjParams, IterQP))
				{
					break; // no more static geometry below in this column
				}
				if (!Hit.bStartPenetrating && Hit.ImpactNormal.Z >= WalkableNormalZ) // rejects undersides / steep faces
				{
					Candidates.Add(FVector2f(static_cast<float>(Hit.ImpactPoint.Z), static_cast<float>(Hit.ImpactNormal.Z)));
				}
				const float NextStartZ = FMath::Min(static_cast<float>(Hit.ImpactPoint.Z), ProbeStartZ) - SurfaceProbeSkip;
				if (NextStartZ <= Base.Z)
				{
					break;
				}
				ProbeStartZ = NextStartZ;
			}

			// Cluster ascending by Z into up to NumLayers ranks; candidates within SurfaceProbeSkip are one physical
			// surface (dedupes a merged-mesh double hit). The lowest cluster -> rank 0.
			Candidates.Sort([](const FVector2f& A, const FVector2f& B) { return A.X < B.X; });
			int32 Assigned = 0;
			float LastClusterZ = -MAX_flt;
			for (const FVector2f& Cand : Candidates)
			{
				if (Assigned == 0 || (Cand.X - LastClusterZ) > SurfaceProbeSkip)
				{
					if (Assigned >= NumLayers)
					{
						++DroppedSurfaceCells; // more stacked walkables than NumLayers — drop the highest (bounded design)
						break;
					}
					CellFloorZ[SurfIndex(Cell, Assigned)] = Cand.X;
					LastClusterZ = Cand.X;
					++Assigned;
				}
			}
			if (Assigned == 0)
			{
				continue; // no walkable surface in this column
			}

			// Slope/staircase detection per surface per axis: sub-sample the ground (center + 4 mid-points), attribute
			// each hit to its rank by Z-band, and flag an axis sloped if the rank's Z span along that axis exceeds a step.
			// A cell's own steep in-cell staircase reads as sloped -> gets the ramp allowance (else it reads as too-big
			// flat steps and is wrongly unreachable). Only the slope axis is flagged, so a perpendicular cliff stays a step (R2).
			bool SubHit[5];
			float SubZ[5];
			for (int32 S = 0; S < 5; ++S)
			{
				const FVector SubStart(CenterX + SubDX[S] * SubR, CenterY + SubDY[S] * SubR, ApexZ);
				FHitResult SubHitR;
				if (World->LineTraceSingleByObjectType(SubHitR, SubStart, FVector(SubStart.X, SubStart.Y, ApexZ - MaxProbeDrop), ObjParams, IterQP) &&
					!SubHitR.bStartPenetrating && SubHitR.ImpactNormal.Z >= WalkableNormalZ)
				{
					SubHit[S] = true;
					SubZ[S] = static_cast<float>(SubHitR.ImpactPoint.Z);
				}
				else
				{
					SubHit[S] = false;
					SubZ[S] = 0.0f;
				}
			}
			for (int32 R = 0; R < Assigned; ++R)
			{
				const float RZ = CellFloorZ[SurfIndex(Cell, R)];
				auto Belongs = [&](int32 S) { return SubHit[S] && FMath::Abs(SubZ[S] - RZ) <= SubBand; };
				// X axis (center + +X + -X): sloped if the belonging sub-hits span more than a step.
				{
					float Mn = MAX_flt, Mx = -MAX_flt;
					const int32 XS[3] = { 0, 1, 2 };
					for (int32 k = 0; k < 3; ++k) { if (Belongs(XS[k])) { Mn = FMath::Min(Mn, SubZ[XS[k]]); Mx = FMath::Max(Mx, SubZ[XS[k]]); } }
					if (Mx > -MAX_flt && (Mx - Mn) > ActiveClimbableStepHeight) { AxisSloped[SurfIndex(Cell, R) * 2 + 0] = 1; }
				}
				// Y axis (center + +Y + -Y).
				{
					float Mn = MAX_flt, Mx = -MAX_flt;
					const int32 YS[3] = { 0, 3, 4 };
					for (int32 k = 0; k < 3; ++k) { if (Belongs(YS[k])) { Mn = FMath::Min(Mn, SubZ[YS[k]]); Mx = FMath::Max(Mx, SubZ[YS[k]]); } }
					if (Mx > -MAX_flt && (Mx - Mn) > ActiveClimbableStepHeight) { AxisSloped[SurfIndex(Cell, R) * 2 + 1] = 1; }
				}
			}
		}
	}

	// Per-edge max traversable height delta: sloped along the edge's axis -> grade allowance; else flat step limit.
	auto EdgeMaxDelta = [&](int32 SurfA, int32 SurfB, int32 Axis) -> float
	{
		const bool bSloped = AxisSloped[SurfA * 2 + Axis] != 0 || AxisSloped[SurfB * 2 + Axis] != 0;
		return bSloped ? FMath::Max(ActiveClimbableStepHeight, RampAllowance) : ActiveClimbableStepHeight;
	};

	// (3) Flood reachability over SURFACES. Frontier holds Surf indices (NOT bare cells) so a cell's second rank is a
	// distinct flood node — keying on bare cell would suppress it and silently regress to single-layer (Codex pin).
	TArray<bool> Reached;
	Reached.Init(false, NumSurf);
	TQueue<int32> Frontier;

	// (3a) PlayerStart seed (R3): trace the floor UNDER the start (not the capsule origin ~90cm up) and seed the cell's
	// rank whose clustered Z is nearest that traced floor — so a start on a ramp/deck seeds the correct surface.
	for (TActorIterator<APlayerStart> It(World); It; ++It)
	{
		const APlayerStart* Start = *It;
		if (!Start)
		{
			continue;
		}
		const FVector StartLoc = Start->GetActorLocation();
		const int32 SCell = WorldToCellIndex(StartLoc);
		if (SCell == INDEX_NONE)
		{
			continue;
		}
		FHitResult FloorHit;
		const float StartFloorZ = World->LineTraceSingleByChannel(FloorHit, StartLoc, StartLoc - FVector(0.0f, 0.0f, 5000.0f), ECC_WorldStatic)
			? static_cast<float>(FloorHit.ImpactPoint.Z) : (static_cast<float>(StartLoc.Z) - EnemyStandOffset);
		int32 BestRank = INDEX_NONE;
		float BestD = MAX_flt;
		for (int32 R = 0; R < NumLayers; ++R)
		{
			const float Z = CellFloorZ[SurfIndex(SCell, R)];
			if (Z != MAX_flt && FMath::Abs(Z - StartFloorZ) < BestD)
			{
				BestD = FMath::Abs(Z - StartFloorZ);
				BestRank = R;
			}
		}
		if (BestRank != INDEX_NONE)
		{
			const int32 SSurf = SurfIndex(SCell, BestRank);
			if (!Reached[SSurf])
			{
				Reached[SSurf] = true;
				Frontier.Enqueue(SSurf);
			}
		}
	}

	// (3b) Ground-plane seed — RANK 0 ONLY (Codex pin): a cell's LOWEST surface within one step of the known ground
	// floor is ground. Never seed a higher rank directly from GridOrigin.Z proximity, or a low disconnected mezzanine
	// would be falsely fed ground flow. (An authored low deck within a step of the ground is a hazard — warn once below.)
	for (int32 Cell = 0; Cell < NumCells; ++Cell)
	{
		const int32 S0 = SurfIndex(Cell, 0);
		if (!Reached[S0] && CellFloorZ[S0] != MAX_flt && FMath::Abs(CellFloorZ[S0] - GridOrigin.Z) <= ActiveClimbableStepHeight)
		{
			Reached[S0] = true;
			Frontier.Enqueue(S0);
		}
	}

	// (3c) Flood: a neighbour surface is reachable if its clustered Z is within the step/grade allowance of the current
	// surface. Every neighbour RANK is considered, so a staircase transitively lifts rank across cells (no inter-layer
	// edge). BFS order (nearest-ground-first) fixes the physical layer ordering.
	static const int32 FDX[4] = { 1, -1, 0, 0 };
	static const int32 FDY[4] = { 0, 0, 1, -1 };
	int32 CurSurf;
	while (Frontier.Dequeue(CurSurf))
	{
		const int32 Cell = CurSurf / NumLayers;
		const int32 CX = Cell % GridDimX;
		const int32 CY = Cell / GridDimX;
		const float H = CellFloorZ[CurSurf];
		for (int32 N = 0; N < 4; ++N)
		{
			const int32 NX = CX + FDX[N];
			const int32 NY = CY + FDY[N];
			if (NX < 0 || NX >= GridDimX || NY < 0 || NY >= GridDimY)
			{
				continue;
			}
			const int32 NCell = NY * GridDimX + NX;
			const int32 Axis = (N < 2) ? 0 : 1;
			for (int32 RB = 0; RB < NumLayers; ++RB)
			{
				const int32 NSurf = SurfIndex(NCell, RB);
				if (CellFloorZ[NSurf] == MAX_flt || Reached[NSurf])
				{
					continue;
				}
				if (FMath::Abs(CellFloorZ[NSurf] - H) <= EdgeMaxDelta(CurSurf, NSurf, Axis))
				{
					Reached[NSurf] = true;
					Frontier.Enqueue(NSurf);
				}
			}
		}
	}

	// Surfaces the flood never reached have no reachable path from the ground -> mark absent (MAX_flt). A cell with no
	// reachable surface is fully blocked; enemies there sample zero flow and fall back to direct-to-player.
	for (int32 Surf = 0; Surf < NumSurf; ++Surf)
	{
		if (!Reached[Surf])
		{
			CellFloorZ[Surf] = MAX_flt;
		}
	}

	// (4) Bake per-surface occupancy (BlockedField) + per-edge rank-pairing (EdgeMask). Both built once on the fixed map.
	const FCollisionShape OccupancyBox = FCollisionShape::MakeBox(
		FVector(AgentFootprintRadius, AgentFootprintRadius, ObstacleProbeHalfHeight));
	const FCollisionShape EdgeBox = FCollisionShape::MakeBox(
		FVector(AgentFootprintRadius, AgentFootprintRadius, ObstacleProbeHalfHeight));
	// R1: a sloped surface's own tread rises within the footprint by up to AgentFootprintRadius * tan(max grade); raise
	// the occupancy box above that so the ramp itself never false-blocks, while a pillar/railing standing proud of it
	// still overlaps (the old code skipped occupancy on sloped cells entirely, missing such obstacles).
	const float SlopedOccupancyRaise = AgentFootprintRadius * MaxSlopeTan + ObstacleProbeHalfHeight;

	for (int32 CY = 0; CY < GridDimY; ++CY)
	{
		for (int32 CX = 0; CX < GridDimX; ++CX)
		{
			const int32 Cell = CY * GridDimX + CX;
			const float CenterX = GridOrigin.X + (CX + 0.5f) * ActiveCellSize;
			const float CenterY = GridOrigin.Y + (CY + 0.5f) * ActiveCellSize;

			for (int32 R = 0; R < NumLayers; ++R)
			{
				const int32 Surf = SurfIndex(Cell, R);
				if (CellFloorZ[Surf] == MAX_flt)
				{
					continue; // absent surface
				}
				// Occupancy: can a footprint-sized agent stand here? Sloped surfaces probe higher (R1) so the ramp's own
				// tread isn't mistaken for an obstacle.
				const bool bSurfSloped = AxisSloped[Surf * 2 + 0] != 0 || AxisSloped[Surf * 2 + 1] != 0;
				const float ProbeZ = CellFloorZ[Surf] + (bSurfSloped ? SlopedOccupancyRaise : ObstacleProbeZ);
				if (World->OverlapAnyTestByObjectType(FVector(CenterX, CenterY, ProbeZ), FQuat::Identity, ObjParams, OccupancyBox, QueryParams))
				{
					BlockedField[Surf] = true;
				}
			}

			// +X edge (shared boundary with cell CX+1): set a rank-pairing bit for each (ra of this cell, rb of the
			// neighbour) whose height change is traversable and whose footprint-wide boundary box is clear.
			if (CX + 1 < GridDimX)
			{
				const int32 NCell = Cell + 1;
				for (int32 RA = 0; RA < NumLayers; ++RA)
				{
					const int32 SA = SurfIndex(Cell, RA);
					if (CellFloorZ[SA] == MAX_flt) { continue; }
					for (int32 RB = 0; RB < NumLayers; ++RB)
					{
						const int32 SB = SurfIndex(NCell, RB);
						if (CellFloorZ[SB] == MAX_flt) { continue; }
						if (FMath::Abs(CellFloorZ[SA] - CellFloorZ[SB]) <= EdgeMaxDelta(SA, SB, 0))
						{
							const float EdgeZ = FMath::Max(CellFloorZ[SA], CellFloorZ[SB]) + ObstacleProbeZ;
							const FVector EdgeCenter(GridOrigin.X + (CX + 1) * ActiveCellSize, CenterY, EdgeZ);
							if (!World->OverlapAnyTestByObjectType(EdgeCenter, FQuat::Identity, ObjParams, EdgeBox, QueryParams))
							{
								EdgeMask[Cell * 2 + 0] |= static_cast<uint8>(1u << (RA * NumLayers + RB));
							}
						}
					}
				}
			}

			// +Y edge (shared boundary with cell CY+1).
			if (CY + 1 < GridDimY)
			{
				const int32 NCell = Cell + GridDimX;
				for (int32 RA = 0; RA < NumLayers; ++RA)
				{
					const int32 SA = SurfIndex(Cell, RA);
					if (CellFloorZ[SA] == MAX_flt) { continue; }
					for (int32 RB = 0; RB < NumLayers; ++RB)
					{
						const int32 SB = SurfIndex(NCell, RB);
						if (CellFloorZ[SB] == MAX_flt) { continue; }
						if (FMath::Abs(CellFloorZ[SA] - CellFloorZ[SB]) <= EdgeMaxDelta(SA, SB, 1))
						{
							const float EdgeZ = FMath::Max(CellFloorZ[SA], CellFloorZ[SB]) + ObstacleProbeZ;
							const FVector EdgeCenter(CenterX, GridOrigin.Y + (CY + 1) * ActiveCellSize, EdgeZ);
							if (!World->OverlapAnyTestByObjectType(EdgeCenter, FQuat::Identity, ObjParams, EdgeBox, QueryParams))
							{
								EdgeMask[Cell * 2 + 1] |= static_cast<uint8>(1u << (RA * NumLayers + RB));
							}
						}
					}
				}
			}
		}
	}

	// Diagnostics: count cells with no reachable floor (all ranks absent) and cells that resolved a 2nd layer.
	int32 NoFloorCells = 0;
	int32 MultiLayerCells = 0;
	for (int32 Cell = 0; Cell < NumCells; ++Cell)
	{
		const bool bR0 = CellFloorZ[SurfIndex(Cell, 0)] != MAX_flt;
		const bool bR1 = (NumLayers > 1) && CellFloorZ[SurfIndex(Cell, 1)] != MAX_flt;
		if (!bR0 && !bR1) { ++NoFloorCells; }
		if (bR1) { ++MultiLayerCells; }
	}
	UE_LOG(LogFPSR, Log, TEXT("[FlowField] Obstacle mask: %d/%d cells no-floor, %d multi-layer (footprint %.0fcm, step<=%.0fcm, ramp<=%.0fcm, apex+%.0fcm, layers=%d)."),
		NoFloorCells, NumCells, MultiLayerCells, AgentFootprintRadius, ActiveClimbableStepHeight, RampAllowance, ActiveProbeApexAboveOrigin, NumLayers);
	if (DroppedSurfaceCells > 0)
	{
		UE_LOG(LogFPSR, Warning,
			TEXT("[FlowField] %d cell(s) have more than NumLayers(%d) stacked walkable surfaces; the highest were dropped (bounded design — raise NumLayers + widen EdgeMask for true 3+ layers)."),
			DroppedSurfaceCells, NumLayers);
	}
	// If almost every cell has no reachable floor, the flood likely never found the ground (probe apex above a solid
	// ceiling, or the floor isn't a static mesh). Warn the designer to lower the bounds volume's ProbeApexAboveOriginOverride.
	if (NumCells > 0 && NoFloorCells * 10 >= NumCells * 9)
	{
		UE_LOG(LogFPSR, Warning,
			TEXT("[FlowField] %d%% of cells have NO reachable floor — the flood may not have found the ground floor. If this map has a solid ceiling below the probe apex (apex = grid floor + %.0fcm), set the bounds volume's ProbeApexAboveOriginOverride below the ceiling, or ensure the floor is a separate WorldStatic mesh."),
			(NoFloorCells * 100) / NumCells, ActiveProbeApexAboveOrigin);
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

int32 UFPSRFlowFieldSubsystem::FindNearestOpenSurface(int32 FromCell, int32 FromRank, const FVector& PlayerLocation) const
{
	if (FromCell == INDEX_NONE || GridDimX <= 0 || GridDimY <= 0)
	{
		return INDEX_NONE;
	}
	const UWorld* World = GetWorld();
	const int32 CX = FromCell % GridDimX;
	const int32 CY = FromCell / GridDimX;

	// Height anchor for the layer-consistent snap: prefer the (possibly occupancy-blocked) surface the player was picked
	// at (keeps the snap on the player's own layer); else the player's foot Z.
	const int32 FromSurf = SurfIndex(FromCell, FMath::Clamp(FromRank, 0, NumLayers - 1));
	const float AnchorZ = (CellFloorZ[FromSurf] != MAX_flt) ? CellFloorZ[FromSurf] : (static_cast<float>(PlayerLocation.Z) - EnemyStandOffset);

	FCollisionObjectQueryParams ObjParams;
	ObjParams.AddObjectTypesToQuery(ECC_WorldStatic);
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(FPSRFlowSourceLOS), false);

	int32 Fallback = INDEX_NONE; // nearest open surface regardless of line-of-sight
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
				const int32 NCell = NY * GridDimX + NX;
				// Pick the nearest-height non-blocked valid surface in this candidate cell (stay on the player's layer).
				int32 BestRank = INDEX_NONE;
				float BestD = MAX_flt;
				for (int32 rb = 0; rb < NumLayers; ++rb)
				{
					const int32 S = SurfIndex(NCell, rb);
					if (CellFloorZ[S] == MAX_flt || BlockedField[S])
					{
						continue;
					}
					const float D = FMath::Abs(CellFloorZ[S] - AnchorZ);
					if (D < BestD)
					{
						BestD = D;
						BestRank = rb;
					}
				}
				if (BestRank == INDEX_NONE)
				{
					continue;
				}
				const int32 OpenSurf = SurfIndex(NCell, BestRank);
				if (Fallback == INDEX_NONE)
				{
					Fallback = OpenSurf;
				}
				// Prefer a surface the player can actually see (no static wall between) — keeps the snapped source on the
				// player's side of the obstacle. First LOS-clear surface (nearest-first) wins.
				const FVector CellCenter(
					GridOrigin.X + (NX + 0.5f) * ActiveCellSize,
					GridOrigin.Y + (NY + 0.5f) * ActiveCellSize,
					PlayerLocation.Z);
				if (World == nullptr ||
					!World->LineTraceTestByObjectType(PlayerLocation, CellCenter, ObjParams, QueryParams))
				{
					return OpenSurf;
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

int32 UFPSRFlowFieldSubsystem::PickRankForFootZ(int32 Cell, float FootZ) const
{
	const int32 Base = Cell * NumLayers;
	// 1) Lowest valid rank within GroundSnapTolerance of FootZ (== MaxClimbableStepHeight). This matches what
	//    ApplyGravity snaps to; iterating rank 0 first makes a degenerate <snap stacked pair resolve to the LOWER
	//    surface deterministically -> no frame-to-frame layer oscillation. On a flat map rank 0 matches immediately.
	for (int32 R = 0; R < NumLayers; ++R)
	{
		const float Z = CellFloorZ[Base + R];
		if (Z != MAX_flt && FMath::Abs(Z - FootZ) <= MaxClimbableStepHeight)
		{
			return R;
		}
	}
	// 2) Highest valid rank at or below FootZ — the surface the pawn stands on / is falling toward.
	int32 BelowBest = INDEX_NONE;
	float BelowZ = -MAX_flt;
	for (int32 R = 0; R < NumLayers; ++R)
	{
		const float Z = CellFloorZ[Base + R];
		if (Z != MAX_flt && Z <= FootZ && Z > BelowZ)
		{
			BelowZ = Z;
			BelowBest = R;
		}
	}
	if (BelowBest != INDEX_NONE)
	{
		return BelowBest;
	}
	// 3) Lowest valid rank (all valid surfaces sit above FootZ — pawn below the lowest floor, e.g. falling into a pit).
	for (int32 R = 0; R < NumLayers; ++R)
	{
		if (CellFloorZ[Base + R] != MAX_flt)
		{
			return R;
		}
	}
	return INDEX_NONE; // no valid surface at this cell
}

bool UFPSRFlowFieldSubsystem::IsSurfaceEdgeTraversable(int32 CellA, int32 RankA, int32 CellB, int32 RankB) const
{
	if (GridDimX <= 0)
	{
		return false;
	}
	const int32 AX = CellA % GridDimX, AY = CellA / GridDimX;
	const int32 BX = CellB % GridDimX, BY = CellB / GridDimX;
	// Canonicalize to the lower cell's +X / +Y edge byte; the rank bit-index (leftRank*NumLayers + rightRank) reads the
	// same stored entry from both directions (swap the ranks when reading B->A).
	if (AY == BY && FMath::Abs(AX - BX) == 1)
	{
		const bool bALeft = AX < BX;
		const int32 LeftCell = bALeft ? CellA : CellB;
		const int32 LeftRank = bALeft ? RankA : RankB;
		const int32 RightRank = bALeft ? RankB : RankA;
		return (EdgeMask[LeftCell * 2 + 0] >> (LeftRank * NumLayers + RightRank)) & 1u;
	}
	if (AX == BX && FMath::Abs(AY - BY) == 1)
	{
		const bool bABottom = AY < BY;
		const int32 BottomCell = bABottom ? CellA : CellB;
		const int32 BottomRank = bABottom ? RankA : RankB;
		const int32 TopRank = bABottom ? RankB : RankA;
		return (EdgeMask[BottomCell * 2 + 1] >> (BottomRank * NumLayers + TopRank)) & 1u;
	}
	return false; // not orthogonally adjacent — the BFS/flow passes only query the 4-neighborhood
}

void UFPSRFlowFieldSubsystem::RecomputeField()
{
	// PERF GUARDRAIL (U7): this 0.2s multi-source BFS + steepest-descent runs over up to MaxTotalCells*NumLayers surfaces
	// and the swarm samples the result every tick — it MUST stay pure O(1) array math. NO per-cell trace / height
	// re-sample here: all obstacle / height / clearance data is pre-baked ONCE into BlockedField / EdgeMask / CellFloorZ
	// by BuildObstacleMask. The ONLY world query below is the pre-existing FindNearestOpenSurface player-source LOS snap,
	// which is bounded by the PLAYER count (<= party size), NOT per-enemy, and only fires when a player stands in a
	// blocked cell. Empty upper-layer surfaces (CellFloorZ == MAX_flt) hit a one-branch early-out (flat-map no regression).
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

	const int32 NumSurf = GridDimX * GridDimY * NumLayers;
	for (int32 i = 0; i < NumSurf; ++i)
	{
		DistField[i] = MAX_int32;
	}

	// Seed BFS from every alive player's SURFACE (multi-source -> field points to NEAREST player). The player's layer is
	// picked from its foot Z, so a player on an upper deck seeds the deck surface (the swarm then routes up the stairs).
	TQueue<int32> Frontier;
	bool bAnySource = false;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		if (const APlayerController* PC = It->Get())
		{
			if (const APawn* PlayerPawn = PC->GetPawn())
			{
				const FVector PawnLoc = PlayerPawn->GetActorLocation();
				const int32 Cell = WorldToCellIndex(PawnLoc);
				if (Cell == INDEX_NONE)
				{
					continue;
				}
				// Foot Z: subtract the pawn's own capsule half-height (players differ from enemies). Robust to a few cm
				// error since layers are authored a storey apart.
				float FootOffset = EnemyStandOffset;
				if (const ACharacter* Ch = Cast<ACharacter>(PlayerPawn))
				{
					if (const UCapsuleComponent* Cap = Ch->GetCapsuleComponent())
					{
						FootOffset = Cap->GetScaledCapsuleHalfHeight();
					}
				}
				const float FootZ = static_cast<float>(PawnLoc.Z) - FootOffset;
				const int32 Rank = PickRankForFootZ(Cell, FootZ);
				int32 Surf = (Rank != INDEX_NONE) ? SurfIndex(Cell, Rank) : INDEX_NONE;
				// If the player's cell has NO valid open surface at its layer — either no reachable surface at all
				// (Rank == INDEX_NONE, a no-floor / coarse-edge cell) or the picked surface is absent / an occupancy-
				// blocked wall — snap to the nearest open surface so the BFS still expands from walkable ground. Omitting
				// this (Codex) would drop the player's source: in single-player bAnySource stays false and the whole
				// swarm falls back to direct XY chase (the pre-U7 code always snapped a blocked source).
				if (Surf == INDEX_NONE || CellFloorZ[Surf] == MAX_flt || BlockedField[Surf])
				{
					Surf = FindNearestOpenSurface(Cell, (Rank == INDEX_NONE) ? 0 : Rank, PawnLoc);
				}
				if (Surf != INDEX_NONE && DistField[Surf] != 0)
				{
					DistField[Surf] = 0;
					Frontier.Enqueue(Surf);
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

	// 4-connected BFS (uniform cost) over the surface graph -> integration field.
	static const int32 DX4[4] = { 1, -1, 0, 0 };
	static const int32 DY4[4] = { 0, 0, 1, -1 };
	int32 Current;
	while (Frontier.Dequeue(Current))
	{
		const int32 CurDist = DistField[Current];
		const int32 Cell = Current / NumLayers;
		const int32 Rank = Current - Cell * NumLayers;
		const int32 CX = Cell % GridDimX;
		const int32 CY = Cell / GridDimX;
		for (int32 N = 0; N < 4; ++N)
		{
			const int32 NX = CX + DX4[N];
			const int32 NY = CY + DY4[N];
			if (NX < 0 || NX >= GridDimX || NY < 0 || NY >= GridDimY)
			{
				continue;
			}
			const int32 NCell = NY * GridDimX + NX;
			for (int32 RB = 0; RB < NumLayers; ++RB)
			{
				const int32 NSurf = SurfIndex(NCell, RB);
				if (CellFloorZ[NSurf] == MAX_flt || BlockedField[NSurf])
				{
					continue; // absent, or an occupancy-blocked wall surface — never propagate flow through it
				}
				if (!IsSurfaceEdgeTraversable(Cell, Rank, NCell, RB))
				{
					continue; // a thin wall / non-traversable height change on the shared boundary blocks this edge
				}
				if (DistField[NSurf] > CurDist + 1)
				{
					DistField[NSurf] = CurDist + 1;
					Frontier.Enqueue(NSurf);
				}
			}
		}
	}

	// Flow per surface: steepest descent toward the lowest-distance reachable neighbour surface.
	static const int32 DX8[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };
	static const int32 DY8[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
	for (int32 CY = 0; CY < GridDimY; ++CY)
	{
		for (int32 CX = 0; CX < GridDimX; ++CX)
		{
			const int32 Cell = CY * GridDimX + CX;
			for (int32 Rank = 0; Rank < NumLayers; ++Rank)
			{
				const int32 Surf = SurfIndex(Cell, Rank);
				if (CellFloorZ[Surf] == MAX_flt)
				{
					FlowField[Surf] = FVector2D::ZeroVector; // absent surface — never sampled
					continue;
				}

				// Steepest descent for EVERY valid surface, including occupancy-blocked ones (DistField == MAX): an enemy
				// standing in a partially-obstructed surface still gets an escape direction toward the nearest reachable
				// open neighbour instead of zero flow (which would jam it against geometry). Codex.
				int32 BestDist = DistField[Surf];
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
					const int32 NCell = NY * GridDimX + NX;
					if (N < 4)
					{
						// Orthogonal: consider each reachable neighbour rank. Don't point across a blocked edge (a thin
						// boundary wall the BFS routed around) unless THIS surface is itself blocked (keep escape).
						for (int32 RB = 0; RB < NumLayers; ++RB)
						{
							const int32 NSurf = SurfIndex(NCell, RB);
							if (CellFloorZ[NSurf] == MAX_flt)
							{
								continue;
							}
							if (!BlockedField[Surf] && !IsSurfaceEdgeTraversable(Cell, Rank, NCell, RB))
							{
								continue;
							}
							if (DistField[NSurf] < BestDist)
							{
								BestDist = DistField[NSurf];
								BestNX = NX;
								BestNY = NY;
							}
						}
					}
					else if (Rank == 0)
					{
						// Diagonal corner-clearance — RANK 0 (ground plane) only; upper layers use 4-connected flow (a safe
						// simplification: still correct, just not diagonally smoothed). Take a diagonal only if BOTH
						// orthogonal ground cells are open, AND — for a non-escaping surface — the corner edges are all
						// traversable so the diagonal can't cut a thin wall or a height/step-gated cliff.
						const int32 NSurf = SurfIndex(NCell, 0);
						if (CellFloorZ[NSurf] == MAX_flt)
						{
							continue;
						}
						const int32 OrthoA = CY * GridDimX + NX; // (NX, CY)
						const int32 OrthoB = NY * GridDimX + CX; // (CX, NY)
						const int32 SA0 = SurfIndex(OrthoA, 0);
						const int32 SB0 = SurfIndex(OrthoB, 0);
						if (CellFloorZ[SA0] == MAX_flt || CellFloorZ[SB0] == MAX_flt || BlockedField[SA0] || BlockedField[SB0])
						{
							continue;
						}
						if (!BlockedField[Surf] &&
							(!IsSurfaceEdgeTraversable(Cell, 0, OrthoA, 0) || !IsSurfaceEdgeTraversable(Cell, 0, OrthoB, 0) ||
							 !IsSurfaceEdgeTraversable(OrthoA, 0, NCell, 0) || !IsSurfaceEdgeTraversable(OrthoB, 0, NCell, 0)))
						{
							continue;
						}
						if (DistField[NSurf] < BestDist)
						{
							BestDist = DistField[NSurf];
							BestNX = NX;
							BestNY = NY;
						}
					}
				}

				if (BestNX >= 0)
				{
					const FVector2D Dir(static_cast<float>(BestNX - CX), static_cast<float>(BestNY - CY));
					FlowField[Surf] = Dir.GetSafeNormal();
				}
				else
				{
					FlowField[Surf] = FVector2D::ZeroVector; // at/near a source
				}
			}
		}
	}

	bFieldReady = true;

#if !UE_BUILD_SHIPPING
	// Dev viz (FPSR.FlowField.Debug 1): per-surface flow arrows (rank 0 green, upper ranks cyan) + blocked/no-floor boxes
	// near each player, drawn AT each surface's own floor height so a stacked mezzanine shows a ground arrow and a deck
	// arrow at different Z. Radius-limited around players so it never iterates the whole grid.
	if (CVarFlowFieldDebug.GetValueOnAnyThread() > 0)
	{
		const float CellHalf = ActiveCellSize * 0.5f;
		const float DrawLife = FlowUpdateInterval * 1.2f;
		const int32 DrawRadius = 25; // cells around each player
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			const APlayerController* PC = It->Get();
			const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
			const int32 PCell = Pawn ? WorldToCellIndex(Pawn->GetActorLocation()) : INDEX_NONE;
			if (PCell == INDEX_NONE)
			{
				continue;
			}
			const int32 PCX = PCell % GridDimX, PCY = PCell / GridDimX;
			for (int32 dy = -DrawRadius; dy <= DrawRadius; ++dy)
			{
				for (int32 dx = -DrawRadius; dx <= DrawRadius; ++dx)
				{
					const int32 CX = PCX + dx, CY = PCY + dy;
					if (CX < 0 || CX >= GridDimX || CY < 0 || CY >= GridDimY)
					{
						continue;
					}
					const int32 Cell = CY * GridDimX + CX;
					const float WX = GridOrigin.X + (CX + 0.5f) * ActiveCellSize;
					const float WY = GridOrigin.Y + (CY + 0.5f) * ActiveCellSize;
					for (int32 Rank = 0; Rank < NumLayers; ++Rank)
					{
						const int32 Surf = SurfIndex(Cell, Rank);
						const bool bAbsent = (CellFloorZ[Surf] == MAX_flt);
						if (bAbsent && Rank > 0)
						{
							continue; // don't clutter with empty upper-layer slots (the flat-map majority)
						}
						const float WZ = (bAbsent ? GridOrigin.Z : CellFloorZ[Surf]) + 10.0f;
						if (bAbsent || BlockedField[Surf])
						{
							// Red = no reachable floor (flood never reached / not walkable); Orange = surface exists but occupancy-blocked.
							const FColor BlockColor = bAbsent ? FColor::Red : FColor::Orange;
							DrawDebugBox(World, FVector(WX, WY, WZ), FVector(CellHalf * 0.8f, CellHalf * 0.8f, 4.0f), BlockColor, false, DrawLife);
						}
						else
						{
							const FVector2D F = FlowField[Surf];
							const FVector Start(WX, WY, WZ);
							const FColor ArrowColor = (Rank == 0) ? FColor::Green : FColor::Cyan; // upper layers tinted cyan
							DrawDebugDirectionalArrow(World, Start, Start + FVector(F.X, F.Y, 0.0f) * (CellHalf * 0.9f), 20.0f, ArrowColor, false, DrawLife, 0, 2.0f);
						}
					}
				}
			}
		}
	}
#endif
}

FVector UFPSRFlowFieldSubsystem::SampleFlowDirection(const FVector& WorldLocation) const
{
	if (!bFieldReady)
	{
		return FVector::ZeroVector;
	}
	const int32 Cell = WorldToCellIndex(WorldLocation);
	if (Cell == INDEX_NONE)
	{
		return FVector::ZeroVector;
	}
	// Convert the enemy's actor Z to the surface it stands on and pick that layer's flow — pure arithmetic, no query.
	const float FootZ = static_cast<float>(WorldLocation.Z) - EnemyStandOffset;
	const int32 Rank = PickRankForFootZ(Cell, FootZ);
	if (Rank == INDEX_NONE)
	{
		return FVector::ZeroVector;
	}
	const FVector2D F = FlowField[SurfIndex(Cell, Rank)];
	return FVector(F.X, F.Y, 0.0f);
}
