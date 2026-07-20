// Copyright Epic Games, Inc. All Rights Reserved.

#include "Enemy/FPSREnemyMetricsSubsystem.h"
#include "Enemy/FPSREnemyBase.h"
#include "Enemy/FPSREnemySpawnSubsystem.h"

#include "ConvexVolume.h"
#include "Engine/Engine.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "SceneView.h"

// CSV_PROFILER_STATS is disabled whenever CSV_PROFILER_MINIMAL trims stats down too — profiling subsystems should
// gate on it rather than the coarser CSV_PROFILER (CsvProfilerConfig.h). Category default-enabled (true), matching
// other always-relevant gameplay categories (e.g. Engine's CharacterMovement/Replication categories) — the 5 stats
// below should show up in a capture without an extra -csvCategory filter.
#if CSV_PROFILER_STATS
CSV_DEFINE_CATEGORY(FPSREnemy, true);
#endif

// FTickableGameObject implementation — mirrors UFPSREnemySpawnSubsystem / UFPSRProjectileSubsystem's boilerplate.

void UFPSREnemyMetricsSubsystem::Tick(float DeltaTime)
{
#if CSV_PROFILER_STATS
	// Only pay for the registry walk while a CSV capture is actually running (dev/editor cost is capture-gated; in
	// Shipping ShouldCreateSubsystem already refuses to create this object at all, so this never even ticks there).
	if (!FCsvProfiler::Get()->IsCapturing())
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// ① ServerAlive (server-only): the spawn director's own alive count — the single source of truth for "how many
	// enemies exist", independent of any client's relevancy/dormancy view. Sampled BEFORE the local-pawn gate below:
	// ① is authority-side bookkeeping that stays valid on a frame with no local pawn (respawn / DBNO hand-off), and
	// gating it on one would punch holes in the ① series exactly while the swarm is at its most interesting.
	if (HasServerAuthority())
	{
		if (const UFPSREnemySpawnSubsystem* SpawnSub = World->GetSubsystem<UFPSREnemySpawnSubsystem>())
		{
			CSV_CUSTOM_STAT(FPSREnemy, ServerAlive, SpawnSub->GetAliveCount(), ECsvCustomStatOp::Set);
		}
	}

	// ②③④ are all "what ONE player experiences", so they measure against THIS client's own LOCAL pawn/view (see the
	// class header comment). GetFirstLocalPlayerController — NOT UWorld::GetFirstPlayerController, which returns the
	// head of a list that on a 4-player LISTEN SERVER also holds the three REMOTE clients' controllers (Engine.h:2984
	// says to prefer the local getter for exactly this reason). Picking a remote PC there would measure another
	// player's POV, and its null GetLocalPlayer would silently peg ③a VisibleFrustum at 0 with no error.
	APlayerController* PC = GEngine ? GEngine->GetFirstLocalPlayerController(World) : nullptr;
	const APawn* LocalPawn = PC ? PC->GetPawn() : nullptr;
	if (!LocalPawn)
	{
		return;
	}

	// ③a VisibleFrustum: build the local player's view frustum ONCE per tick (not once per enemy below). Occlusion
	// is ignored, so this is a strict upper bound on what could be seen — ③b below is the occlusion-aware counterpart
	// and must always come out <= this (that invariant is what caught the shadow-pass bug; see ③b).
	bool bHaveFrustum = false;
	FConvexVolume Frustum;
	if (const ULocalPlayer* LocalPlayer = PC->GetLocalPlayer())
	{
		if (LocalPlayer->ViewportClient)
		{
			FSceneViewProjectionData ProjectionData;
			if (LocalPlayer->GetProjectionData(LocalPlayer->ViewportClient->Viewport, ProjectionData))
			{
				GetViewFrustumBounds(Frustum, ProjectionData.ComputeViewProjectionMatrix(), true);
				bHaveFrustum = true;
			}
		}
	}

	// ③b window, sized in frames (see RenderRecencyFrames) so the gate reads the same on a 30fps and a 120fps machine.
	const float RenderRecencyTolerance = RenderRecencyFrames * FMath::Max(DeltaTime, UE_KINDA_SMALL_NUMBER);

	const FVector LocalPawnLocation = LocalPawn->GetActorLocation();
	// ④ Near15m reuses the movement LOD pass's own 15m S0 tier radius (single source — see GetTierS0RadiusSq's doc
	// comment) rather than duplicating the constant.
	const float NearRadiusSq = UFPSREnemySpawnSubsystem::GetTierS0RadiusSq();

	int32 RelevantAliveCount = 0;
	int32 Near15mCount = 0;
	int32 VisibleFrustumCount = 0;
	int32 VisibleRenderedCount = 0;

	// Single pass over the registry computes ②③④ together (no new world queries, no second traversal) — iterate
	// backwards so a stale entry can be RemoveAtSwap'd in place without disturbing the rest of the walk.
	for (int32 Index = Registry.Num() - 1; Index >= 0; --Index)
	{
		const AFPSREnemyBase* Enemy = Registry[Index].Get();
		if (!IsValid(Enemy))
		{
			Registry.RemoveAtSwap(Index); // opportunistic: drop an actor destroyed outside the normal pool path
			continue;
		}

		// ② RelevantAlive: IsHidden(), NOT a BeginPlay/EndPlay count — the pool never destroys actors (Deactivate
		// only hides + DORM_DormantAll), so BeginPlay fires once per actor's real lifetime, not once per activation.
		// IsHidden() == false is this client's actual "alive right now" signal (see class header for detail).
		if (Enemy->IsHidden())
		{
			continue;
		}
		++RelevantAliveCount;

		const FVector EnemyLocation = Enemy->GetActorLocation();

		if (FVector::DistSquared(LocalPawnLocation, EnemyLocation) <= NearRadiusSq)
		{
			++Near15mCount;
		}

		// ③a and ③b BOTH measure the MESH, so they must resolve it once and share it. ③a used to frustum-test a
		// hardcoded 40cm sphere at the ACTOR ORIGIN, which is NOT the volume the renderer culls with: SceneVisibility.cpp:599-622
		// (IsPrimitiveVisible) tests Bounds.BoxSphereBounds — an optional IntersectSphere pre-test on those bounds, then
		// IntersectBox on them. The melee mesh's AABB spans actor-relative Z ∈ [-98.87, +58.00] (~157cm) vs the sphere's
		// [-40, +40], so an enemy just above the top frustum plane had its body drawn (stamping ③b) while its origin sphere
		// sat outside (③a not counting it) — ③b > ③a with no bug anywhere. Testing the renderer's own bounds makes ③a a
		// STRUCTURAL superset of ③b: the renderer sphere-tests these identical bounds and then additionally box- and
		// occlusion-tests, so anything it draws necessarily passes this test. The invariant becomes a property of the
		// design rather than a hope. It also stops tracking BP overrides by hand — the old constant claimed to mirror the
		// C++ ctor's InitCapsuleSize(40, 90), but BP_EnemyMeleeBase overrides the capsule to 30/80 and the mesh transform
		// too, so a content-only edit could silently break the invariant with no code diff.
		if (const UPrimitiveComponent* EnemyMesh = Enemy->GetMesh())
		{
			// No frustum means there is no ③a this frame. Counting ③b anyway would emit ③a=0 alongside ③b=N — a fabricated
			// invariant violation indistinguishable in the CSV from a real one, which is exactly the signal ③ exists to give.
			// Skip both; the emit below drops the pair for this frame rather than reporting a false zero.
			if (bHaveFrustum)
			{
				const FBoxSphereBounds MeshBounds = EnemyMesh->Bounds;
				if (Frustum.IntersectSphere(MeshBounds.Origin, MeshBounds.SphereRadius))
				{
					++VisibleFrustumCount;
				}

				// ③b VisibleRendered (occlusion-aware): the mesh's ON-SCREEN render stamp, NOT AActor::WasRecentlyRendered.
				// That actor-level helper reads AActor::LastRenderTime, which the shadow passes also stamp (ShadowSetup.cpp
				// calls UpdateComponentLastRenderTime with bUpdateLastRenderTimeOnScreen=false) — so an enemy behind the
				// player casting a shadow into view counted as "on screen", and ③b came out ABOVE ③a (its own frustum upper
				// bound) on 48% of frames in the first real capture. GetLastRenderTimeOnScreen is NOT stamped only by the
				// on-screen visibility pass, though: UpdateComponentLastRenderTime has exactly FOUR engine call sites. Two
				// write the on-screen stamp — SceneVisibility.cpp:2108 (the legitimate on-screen pass) and RayTracing.cpp:1146,
				// the latter gated ONLY on DistanceToView < LastRenderTimeUpdateDistance (cvar default 5000.0f = 50m,
				// RayTracing.cpp:38-41), with NO frustum and NO occlusion test — so if ray tracing is ever enabled, ③b
				// silently degrades into "enemies within 50m", the same class of bug as the shadow pollution above, from a
				// second source. Currently inert: r.RayTracing defaults 0 and is ECVF_ReadOnly (DeferredShadingRenderer.cpp:
				// 212-218), and this project's Config/DefaultEngine.ini [/Script/Engine.RendererSettings] sets only
				// r.AllowStaticLighting=False and r.CustomDepth=3 — but flag it as a landmine for the cel/toon art pivot
				// (enabling Lumen HW RT or RT shadows/reflections would trip it). The other two call sites (ShadowSetup.cpp:
				// 2060, :2289) pass false and stamp only the actor, which is why AActor::LastRenderTime is unsafe above.
				// SEPARATELY: this project calls the COMPONENT getter, and UPrimitiveComponent::GetLastRenderTimeOnScreen
				// (PrimitiveComponent.cpp:5050-5057) short-circuits — `if (IsAlwaysVisible()) { return
				// GetWorld()->GetTimeSeconds(); }` — so if the mesh ever becomes Nanite (bIsAlwaysVisible is set true only by
				// Nanite proxies, NaniteSceneProxy.h:275/292), the getter synthesizes "rendered right now" for EVERY enemy and
				// ③b reads RelevantAlive: maximal over-report, not zero. Currently inert (SM_BroBot_VAT has Nanite disabled)
				// but the art pivot is actively replacing this mesh.
				if (World->TimeSince(EnemyMesh->GetLastRenderTimeOnScreen()) <= RenderRecencyTolerance)
				{
					++VisibleRenderedCount;
				}
			}
		}
	}

	CSV_CUSTOM_STAT(FPSREnemy, RelevantAlive, RelevantAliveCount, ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(FPSREnemy, Near15m, Near15mCount, ECsvCustomStatOp::Set);

	// A frame with no view (bHaveFrustum false) produces no ③ reading at all — honest, rather than emitting a false
	// zero that would drag the CSV's P50/P90 down for a frame that was never actually measured.
	if (bHaveFrustum)
	{
		CSV_CUSTOM_STAT(FPSREnemy, VisibleFrustum, VisibleFrustumCount, ECsvCustomStatOp::Set);
		CSV_CUSTOM_STAT(FPSREnemy, VisibleRendered, VisibleRenderedCount, ECsvCustomStatOp::Set);
	}
#endif // CSV_PROFILER_STATS
}

TStatId UFPSREnemyMetricsSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UFPSREnemyMetricsSubsystem, STATGROUP_Tickables);
}

ETickableTickType UFPSREnemyMetricsSubsystem::GetTickableTickType() const
{
	// Never tick the CDO/template.
	return IsTemplate() ? ETickableTickType::Never : ETickableTickType::Conditional;
}

bool UFPSREnemyMetricsSubsystem::IsTickable() const
{
	const UWorld* World = GetWorld();
	return World != nullptr && World->IsGameWorld();
}

UWorld* UFPSREnemyMetricsSubsystem::GetTickableGameObjectWorld() const
{
	return GetWorld();
}

bool UFPSREnemyMetricsSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
#if !CSV_PROFILER_STATS
	// Shipping (by default): CSV custom stats compile to no-ops, so this subsystem would have nothing to do. Refuse
	// creation entirely rather than merely early-returning in Tick — no Tick registration, no registry churn, no
	// per-frame cost whatsoever in that configuration.
	return false;
#else
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}
	if (const UWorld* World = Cast<UWorld>(Outer))
	{
		return World->IsGameWorld();
	}
	return false;
#endif
}

void UFPSREnemyMetricsSubsystem::RegisterEnemy(AFPSREnemyBase* Enemy)
{
	if (Enemy)
	{
		// AddUnique, not Add: BeginPlay is once-per-lifetime today, so this only guards a future path that re-enters it
		// (a double entry would double-count that enemy in every stat). The linear scan is bounded by the pool cap and
		// runs once per actor lifetime — off the per-frame path entirely.
		Registry.AddUnique(Enemy);
	}
}

void UFPSREnemyMetricsSubsystem::UnregisterEnemy(AFPSREnemyBase* Enemy)
{
	if (!Enemy)
	{
		return;
	}
	Registry.RemoveAll([Enemy](const TWeakObjectPtr<AFPSREnemyBase>& P) { return P.Get() == Enemy; });
}

bool UFPSREnemyMetricsSubsystem::HasServerAuthority() const
{
	const UWorld* World = GetWorld();
	return World && (World->GetNetMode() != NM_Client);
}
