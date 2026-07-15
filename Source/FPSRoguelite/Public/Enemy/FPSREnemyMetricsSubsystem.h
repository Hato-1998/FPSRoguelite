// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "Tickable.h"
#include "UObject/WeakObjectPtr.h"
#include "FPSREnemyMetricsSubsystem.generated.h"

class AFPSREnemyBase;

/** S4: per-CLIENT "1인칭 가독성" (readability) metrics for the swarm (Docs/SSOT/Performance.md §5). Reports 5 CSV
 *  custom stats every captured frame — ServerAlive / RelevantAlive / VisibleFrustum / VisibleRendered / Near15m —
 *  so the §5 readability gate can finally be judged from real captures instead of guesswork (previously unmeasured).
 *
 *  WHY PER-CLIENT, NOT SERVER: ②③④ are all "what ONE player experiences", so each client measures against its OWN
 *  local pawn/view — never the server's any-player-nearby test the spawn director's tier pass uses (that answers a
 *  different question: "is any enemy near ANY player", used for movement/net LOD, Game.MD §5/§5-1). Mixing the two
 *  would silently report the wrong number for ④ in a 4-player game.
 *
 *  WHY IsHidden(), NOT BeginPlay/EndPlay COUNTING: the enemy pool never destroys actors — Deactivate() only hides
 *  them and sets DORM_DormantAll (see AFPSREnemyBase::Activate/Deactivate) — so BeginPlay/EndPlay each fire exactly
 *  ONCE per pooled actor's real lifetime, not once per activation. Registry membership is therefore "this actor
 *  exists in the pool", and IsHidden() == false is the correct "alive right now" test (RelevantAlive / ③ / ④ all
 *  gate on it); counting BeginPlay calls would report TotalSpawned instead, which is a different (and wrong) number.
 *
 *  Mirrors UFPSREnemySpawnSubsystem / UFPSRProjectileSubsystem's UWorldSubsystem + FTickableGameObject boilerplate.
 *  UNCONDITIONALLY declared (UHT needs a stable class shape) — the CSV-referencing cost lives entirely inside Tick's
 *  body (see .cpp), gated on CSV_PROFILER_STATS: "Profiling subsystems should be predicated on this rather than
 *  CSV_PROFILER" (CsvProfilerConfig.h). ShouldCreateSubsystem additionally refuses to create the object at all when
 *  CSV_PROFILER_STATS is 0 (Shipping, by default), so in that config there's no Tick registration, no registry
 *  churn, no per-frame cost whatsoever — not merely a cheap early-out. */
UCLASS()
class FPSROGUELITE_API UFPSREnemyMetricsSubsystem : public UWorldSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	// FTickableGameObject — recomputes the 5 readability stats once per CSV-captured frame (see Tick, CSV-gated).
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual ETickableTickType GetTickableTickType() const override;
	virtual bool IsTickable() const override;
	virtual UWorld* GetTickableGameObjectWorld() const override;

	/** Register an enemy actor (BeginPlay, ALL net modes — this is a per-client observer, not server-authoritative
	 *  state). Once per actor lifetime (see the pooling note above). Weak-referenced: never keeps an enemy alive. */
	void RegisterEnemy(AFPSREnemyBase* Enemy);

	/** Unregister an enemy actor (EndPlay). Symmetric with RegisterEnemy; safe to call even if never registered. */
	void UnregisterEnemy(AFPSREnemyBase* Enemy);

private:
	/** Check if this world has server authority (① ServerAlive is server-only; mirrors the sibling subsystems'
	 *  identical helper). */
	bool HasServerAuthority() const;

	/** Every enemy actor that has lived in this world (server or client). Weak so a pooled actor destroyed outside
	 *  the normal Deactivate path can't dangle — Tick() drops stale (!IsValid) entries opportunistically. Not a
	 *  UPROPERTY: TWeakObjectPtr needs no GC tracing, and this registry must NOT keep enemies alive. */
	TArray<TWeakObjectPtr<AFPSREnemyBase>> Registry;

	/** Approximate enemy collision radius (cm) for the ③a frustum sphere test. A NAMED CONSTANT rather than reaching
	 *  into AFPSREnemyBase's protected Capsule component — this subsystem is a read-only OBSERVER and must not couple
	 *  to enemy internals (AFPSREnemyBase is a 200-300 actor hot path; kept change-isolated per the task brief).
	 *  Mirrors AFPSREnemyBase's ctor InitCapsuleSize(40, 90) radius — update together if that ever changes. */
	static constexpr float EnemyApproxRadiusCm = 40.0f;

	/** How far back a render still counts as "on screen now" for ③b (seconds). Deliberately tighter than the engine's
	 *  0.2 default: at 60fps that default would hold an enemy 'visible' for ~12 frames after it left view, smearing the
	 *  P50/P90 the §5 gate reads. One tenth of a second still absorbs a dropped frame without inventing visibility. */
	static constexpr float RenderRecencyToleranceSec = 0.1f;
};
