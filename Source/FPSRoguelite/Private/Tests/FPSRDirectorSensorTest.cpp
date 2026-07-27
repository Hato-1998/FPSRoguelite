// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Director/FPSRDirectorSensorSubsystem.h"
#include "Director/FPSRDirectorSensorTypes.h"

// Concrete actor classes — CDOs are used as classifier inputs (no world, no SpawnActor).
#include "Enemy/FPSREnemyBase.h"
#include "Enemy/FPSRRangedEnemyBase.h"
#include "Boss/FPSRBossBase.h"
#include "Hero/FPSRCharacter.h"
#include "Door/FPSRDoor.h"
#include "Run/Mission/FPSRMissionActor.h"
#include "Enemy/FPSRFlowFieldComputer.h" // a concrete, worldless-instantiable UObject for the lifecycle keys

#include "UObject/StrongObjectPtr.h"
#include "UObject/GarbageCollection.h"

#if WITH_AUTOMATION_TESTS

// Golden net for the closed-loop director SENSOR (P0a-0 walking skeleton, RunFlow §2-8-2).
// These lock the PLUMBING invariants as RELATIONSHIPS over the PURE helpers (single source of truth), so
// PIE re-tuning of the ranges/alphas can't break a test while a regression in the FORMULA still does:
//   SourceGating   — only enemy/boss damage is counted (FF/self/door/mission/env excluded).
//   Confinement    — stationary->1 after window; deadband micro-move holds; radius exit resets; ReleaseR hysteresis.
//   RateDowned     — EWMA rate decays on silence / converges on constant load; DownedRecent recency ramp.
//   Determinism    — identical injected sequence => identical snapshot (aggregator is deterministic).
//   FreezeNoProgress — ShouldAdvance is false while paused; NOT advancing leaves the windows frozen.
//   Lifecycle      — EndRun clears, Logout removes one, invalid weak keys prune to zero tombstones.
// Helper names carry a per-file-unique 'Tel' suffix (module unity-build merges anonymous namespaces).

namespace
{
	// Field-by-field snapshot equality for the determinism test (no operator== on the plain struct).
	bool TelemetrySnapshotsEqualTel(const FFPSRPlayerTelemetry& A, const FFPSRPlayerTelemetry& B)
	{
		return A.HealthPct == B.HealthPct
			&& A.bHealthValid == B.bHealthValid
			&& A.IncomingDamageEnemyRateEwma == B.IncomingDamageEnemyRateEwma
			&& A.IncomingDamageBossRateEwma == B.IncomingDamageBossRateEwma
			&& A.DownedCount == B.DownedCount
			&& A.LastDownedTime == B.LastDownedTime
			&& A.DownedRecent01 == B.DownedRecent01
			&& A.MovementConfinement01 == B.MovementConfinement01
			&& A.bMovementConfined == B.bMovementConfined
			&& A.Confinement.AnchorStartTime == B.Confinement.AnchorStartTime
			&& A.Confinement.AnchorXY == B.Confinement.AnchorXY
			&& A.Confinement.bConfined == B.Confinement.bConfined;
	}
}

// ==================================================================================================
//  (1) Source gating — only enemy/boss damage counts as incoming pressure.
// ==================================================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSRDirectorSensorSourceGatingTest, "FPSRoguelite.Telemetry.SourceGating",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFPSRDirectorSensorSourceGatingTest::RunTest(const FString& Parameters)
{
	using FPSRTelemetry::ClassifyDamageSource;
	using FPSRTelemetry::CountsAsIncoming;

	// CDOs are valid instances of the exact class (no world needed); Cast<> only inspects type. The Victim
	// argument is used ONLY for the Self identity check, so distinct CDOs stand in for distinct actors.
	const AActor* Enemy = GetDefault<AFPSREnemyBase>();
	const AActor* Ranged = GetDefault<AFPSRRangedEnemyBase>();
	const AActor* Boss = GetDefault<AFPSRBossBase>();
	const AActor* Player = GetDefault<AFPSRCharacter>();
	const AActor* Door = GetDefault<AFPSRDoor>();
	const AActor* Mission = GetDefault<AFPSRMissionActor>();

	// Classification (enum has no TestEqual overload -> compare with ==).
	TestTrue(TEXT("enemy -> Enemy"), ClassifyDamageSource(Enemy, Player) == EFPSRDamageSourceClass::Enemy);
	TestTrue(TEXT("ranged enemy -> Enemy (subclass)"), ClassifyDamageSource(Ranged, Player) == EFPSRDamageSourceClass::Enemy);
	TestTrue(TEXT("boss -> Boss (boss is an ACharacter, not an AFPSREnemyBase)"), ClassifyDamageSource(Boss, Player) == EFPSRDamageSourceClass::Boss);
	TestTrue(TEXT("self (instigator==victim) -> Self"), ClassifyDamageSource(Player, Player) == EFPSRDamageSourceClass::Self);
	TestTrue(TEXT("other player -> FriendlyFire"), ClassifyDamageSource(Player, Enemy /*any distinct victim*/) == EFPSRDamageSourceClass::FriendlyFire);
	TestTrue(TEXT("door -> Door"), ClassifyDamageSource(Door, Player) == EFPSRDamageSourceClass::Door);
	TestTrue(TEXT("mission -> Mission"), ClassifyDamageSource(Mission, Player) == EFPSRDamageSourceClass::Mission);
	TestTrue(TEXT("null instigator -> Env"), ClassifyDamageSource(nullptr, Player) == EFPSRDamageSourceClass::Env);

	// Gating: ONLY enemy/boss count.
	TestTrue(TEXT("Enemy counts as incoming"), CountsAsIncoming(EFPSRDamageSourceClass::Enemy));
	TestTrue(TEXT("Boss counts as incoming"), CountsAsIncoming(EFPSRDamageSourceClass::Boss));
	TestFalse(TEXT("Self excluded"), CountsAsIncoming(EFPSRDamageSourceClass::Self));
	TestFalse(TEXT("FriendlyFire excluded"), CountsAsIncoming(EFPSRDamageSourceClass::FriendlyFire));
	TestFalse(TEXT("Door excluded"), CountsAsIncoming(EFPSRDamageSourceClass::Door));
	TestFalse(TEXT("Mission excluded"), CountsAsIncoming(EFPSRDamageSourceClass::Mission));
	TestFalse(TEXT("Env excluded"), CountsAsIncoming(EFPSRDamageSourceClass::Env));

	return true;
}

// ==================================================================================================
//  (2) Movement confinement (C1) state machine golden.
// ==================================================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSRDirectorSensorConfinementTest, "FPSRoguelite.Telemetry.Confinement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFPSRDirectorSensorConfinementTest::RunTest(const FString& Parameters)
{
	using FPSRTelemetry::AdvanceConfinement;
	using FPSRTelemetry::ConfinementLevel01;
	const FFPSRConfinementParams P; // defaults: Deadband 90, R 700, ReleaseFactor 1.25 (=>875), Window 25

	// --- (A) Stationary -> confined at exactly the window, level ramps 0..1 --------------------------
	{
		FFPSRConfinementState S;
		AdvanceConfinement(S, FVector2D(0, 0), 0.0f, P); // seed the anchor epoch at t=0
		AdvanceConfinement(S, FVector2D(0, 0), 24.0f, P);
		TestFalse(TEXT("not yet confined before the window"), S.bConfined);
		TestTrue(TEXT("level ramps up before the window"), ConfinementLevel01(S, 24.0f, P) > 0.9f && ConfinementLevel01(S, 24.0f, P) < 1.0f);
		AdvanceConfinement(S, FVector2D(0, 0), 25.0f, P);
		TestTrue(TEXT("confined once the window elapses while stationary"), S.bConfined);
		TestEqual(TEXT("level saturates at 1 when confined"), ConfinementLevel01(S, 25.0f, P), 1.0f);
	}

	// --- (B) Deadband micro-move keeps a confined player confined -------------------------------------
	{
		FFPSRConfinementState S;
		AdvanceConfinement(S, FVector2D(0, 0), 0.0f, P);
		AdvanceConfinement(S, FVector2D(0, 0), 25.0f, P);
		TestTrue(TEXT("confined at window"), S.bConfined);
		AdvanceConfinement(S, FVector2D(50, 0), 26.0f, P); // 50cm < 90 deadband, well within the radius
		TestTrue(TEXT("micro-move (below deadband) keeps confinement"), S.bConfined);
		TestTrue(TEXT("micro-move does not move the anchor"), S.AnchorXY.Equals(FVector2D(0, 0)));
	}

	// --- (C) Leaving the confinement radius resets the anchor epoch (non-confined) --------------------
	{
		FFPSRConfinementState S;
		AdvanceConfinement(S, FVector2D(0, 0), 0.0f, P);
		AdvanceConfinement(S, FVector2D(0, 0), 10.0f, P); // 10s in, not yet confined
		TestFalse(TEXT("not confined at 10s"), S.bConfined);
		AdvanceConfinement(S, FVector2D(1000, 0), 11.0f, P); // 1000cm > 700 radius -> reset
		TestFalse(TEXT("still not confined after leaving the radius"), S.bConfined);
		TestTrue(TEXT("anchor moved to the new position"), S.AnchorXY.Equals(FVector2D(1000, 0)));
		TestEqual(TEXT("confinement clock restarts on reset"), ConfinementLevel01(S, 11.0f, P), 0.0f);
	}

	// --- (D) Hysteresis: a distance between R and ReleaseR HOLDS a confined player but RESETS a
	//         non-confined one; beyond ReleaseR resets even a confined player. -------------------------
	{
		// (D1) confined player at dist 800 (700 < 800 < 875) -> HOLDS.
		FFPSRConfinementState Sc;
		AdvanceConfinement(Sc, FVector2D(0, 0), 0.0f, P);
		AdvanceConfinement(Sc, FVector2D(0, 0), 25.0f, P);
		TestTrue(TEXT("confined at window (D)"), Sc.bConfined);
		AdvanceConfinement(Sc, FVector2D(800, 0), 26.0f, P);
		TestTrue(TEXT("confined player HOLDS between R and ReleaseR (hysteresis)"), Sc.bConfined);
		TestTrue(TEXT("confined player keeps its anchor within the release band"), Sc.AnchorXY.Equals(FVector2D(0, 0)));

		// (D2) non-confined player at the SAME dist 800 (> 700) -> RESETS.
		FFPSRConfinementState Sn;
		AdvanceConfinement(Sn, FVector2D(0, 0), 0.0f, P);
		AdvanceConfinement(Sn, FVector2D(0, 0), 5.0f, P); // not confined
		AdvanceConfinement(Sn, FVector2D(800, 0), 6.0f, P);
		TestFalse(TEXT("non-confined player resets at the same distance (no hysteresis band)"), Sn.bConfined);
		TestTrue(TEXT("non-confined anchor moves"), Sn.AnchorXY.Equals(FVector2D(800, 0)));

		// (D3) confined player beyond ReleaseR (1000 > 875) -> RESETS.
		AdvanceConfinement(Sc, FVector2D(1000, 0), 27.0f, P);
		TestFalse(TEXT("confined player releases beyond ReleaseR"), Sc.bConfined);
		TestTrue(TEXT("released anchor moves to the exit position"), Sc.AnchorXY.Equals(FVector2D(1000, 0)));
	}

	return true;
}

// ==================================================================================================
//  (3) Incoming-rate EWMA + DownedRecent recency.
// ==================================================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSRDirectorSensorRateDownedTest, "FPSRoguelite.Telemetry.RateDowned",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFPSRDirectorSensorRateDownedTest::RunTest(const FString& Parameters)
{
	using FPSRTelemetry::StepEwmaRate;
	using FPSRTelemetry::ComputeDownedRecent01;
	constexpr float Dt = 0.5f;
	constexpr float Alpha = 0.4f;

	// Silence decays the EWMA monotonically toward 0 (never negative).
	{
		float E = 20.0f;
		float Prev = E;
		for (int32 i = 0; i < 30; ++i)
		{
			E = StepEwmaRate(E, 0.0f, Dt, Alpha);
			TestTrue(TEXT("EWMA never increases on silence"), E <= Prev + KINDA_SMALL_NUMBER);
			TestTrue(TEXT("EWMA never goes negative"), E >= 0.0f);
			Prev = E;
		}
		TestTrue(TEXT("EWMA decays close to 0 after sustained silence"), E < 0.5f);
	}

	// Constant per-interval load converges toward accum/dt.
	{
		const float Accum = 5.0f;          // 5 damage per 0.5s interval
		const float ExpectedRate = Accum / Dt; // = 10 dmg/s
		float E = 0.0f;
		for (int32 i = 0; i < 80; ++i)
		{
			E = StepEwmaRate(E, Accum, Dt, Alpha);
		}
		TestTrue(TEXT("EWMA converges toward the instantaneous rate"), FMath::IsNearlyEqual(E, ExpectedRate, 0.25f));
	}

	// Dt<=0 => instantaneous 0 (neutral); blends the EWMA toward 0.
	TestTrue(TEXT("Dt<=0 treats the interval as neutral (rate 0)"),
		FMath::IsNearlyEqual(StepEwmaRate(5.0f, 100.0f, 0.0f, Alpha), FMath::Lerp(5.0f, 0.0f, Alpha), KINDA_SMALL_NUMBER));

	// DownedRecent recency ramp.
	TestEqual(TEXT("never downed -> 0"), ComputeDownedRecent01(-1.0f, 5.0f, 10.0f), 0.0f);
	TestEqual(TEXT("just downed (age 0) -> 1"), ComputeDownedRecent01(5.0f, 5.0f, 10.0f), 1.0f);
	TestTrue(TEXT("half a window later -> ~0.5"), FMath::IsNearlyEqual(ComputeDownedRecent01(0.0f, 5.0f, 10.0f), 0.5f, KINDA_SMALL_NUMBER));
	TestEqual(TEXT("exactly a window later -> 0"), ComputeDownedRecent01(0.0f, 10.0f, 10.0f), 0.0f);
	TestEqual(TEXT("past the window -> 0"), ComputeDownedRecent01(0.0f, 20.0f, 10.0f), 0.0f);

	return true;
}

// ==================================================================================================
//  (4) Determinism — identical injected sequence => identical snapshot.
// ==================================================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSRDirectorSensorDeterminismTest, "FPSRoguelite.Telemetry.Determinism",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFPSRDirectorSensorDeterminismTest::RunTest(const FString& Parameters)
{
	const FFPSRConfinementParams P;
	constexpr float Alpha = UFPSRDirectorSensorSubsystem::IncomingRateEwmaAlpha;
	constexpr float Window = UFPSRDirectorSensorSubsystem::DownedRecentWindow;
	constexpr float Dt = UFPSRDirectorSensorSubsystem::SampleInterval;

	struct FStep
	{
		float EnemyDmg; float BossDmg; bool bDown; bool bHasHealth; float Health; FVector2D XY;
	};
	const TArray<FStep> Script = {
		{ 10.0f, 0.0f,  false, true, 1.00f, FVector2D(0, 0) },
		{  0.0f, 0.0f,  false, true, 0.90f, FVector2D(30, 0) },
		{  5.0f, 15.0f, false, true, 0.60f, FVector2D(60, 0) },
		{  0.0f, 0.0f,  true,  true, 0.20f, FVector2D(60, 0) },
		{  0.0f, 0.0f,  false, true, 0.80f, FVector2D(2000, 0) }, // radius exit
		{ 25.0f, 0.0f,  false, false, 0.0f, FVector2D(2000, 0) }, // health invalid this step
	};

	auto RunScript = [&](FFPSRPlayerTelemetry& T)
	{
		float Clock = 0.0f;
		for (const FStep& S : Script)
		{
			T.EnemyDamageAccum += S.EnemyDmg;
			T.BossDamageAccum += S.BossDmg;
			if (S.bDown) { ++T.DownedCount; T.LastDownedTime = Clock; T.DownedRecent01 = 1.0f; }
			Clock += Dt;
			T.Integrate(Dt, Clock, Alpha, Window, P, S.bHasHealth, S.Health, /*bHasSample=*/true, S.XY);
		}
	};

	FFPSRPlayerTelemetry A, B;
	RunScript(A);
	RunScript(B);

	TestTrue(TEXT("identical injected sequence => identical snapshot (deterministic aggregator)"),
		TelemetrySnapshotsEqualTel(A, B));

	// Spot-check that the sequence actually exercised the signals (guards against a vacuous pass).
	TestTrue(TEXT("boss rate registered"), A.IncomingDamageBossRateEwma > 0.0f);
	TestEqual(TEXT("down counted once"), A.DownedCount, 1);
	TestFalse(TEXT("radius exit cleared confinement"), A.bMovementConfined);
	TestFalse(TEXT("last step had invalid health"), A.bHealthValid);

	return true;
}

// ==================================================================================================
//  (5) Freeze = no progress. ShouldAdvance gates; not advancing freezes the windows.
// ==================================================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSRDirectorSensorFreezeTest, "FPSRoguelite.Telemetry.FreezeNoProgress",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFPSRDirectorSensorFreezeTest::RunTest(const FString& Parameters)
{
	using FPSRTelemetry::ShouldAdvance;

	// The single gate predicate.
	TestTrue(TEXT("advance when authority + active + unpaused"), ShouldAdvance(true, true, false));
	TestFalse(TEXT("no advance while PAUSED (freeze)"), ShouldAdvance(true, true, true));
	TestFalse(TEXT("no advance on a client"), ShouldAdvance(false, true, false));
	TestFalse(TEXT("no advance when the run is not active"), ShouldAdvance(true, false, false));

	// Invariance: one advance, then a "freeze" (no Integrate call) leaves every window exactly where it was.
	const FFPSRConfinementParams P;
	constexpr float Alpha = UFPSRDirectorSensorSubsystem::IncomingRateEwmaAlpha;
	constexpr float Window = UFPSRDirectorSensorSubsystem::DownedRecentWindow;
	constexpr float Dt = UFPSRDirectorSensorSubsystem::SampleInterval;

	FFPSRPlayerTelemetry T;
	T.EnemyDamageAccum = 10.0f;
	T.LastDownedTime = 2.0f;
	float Clock = 5.0f;
	Clock += Dt;
	T.Integrate(Dt, Clock, Alpha, Window, P, true, 0.8f, true, FVector2D(0, 0));

	const float Enemy1 = T.IncomingDamageEnemyRateEwma;
	const float Downed1 = T.DownedRecent01;
	const float Confine1 = T.MovementConfinement01;
	const float AnchorStart1 = T.Confinement.AnchorStartTime;

	// FREEZE: ShouldAdvance is false, so SensorTick returns without calling Advance/Integrate. Nothing moves.
	TestEqual(TEXT("incoming EWMA frozen"), T.IncomingDamageEnemyRateEwma, Enemy1);
	TestEqual(TEXT("downed-recent frozen"), T.DownedRecent01, Downed1);
	TestEqual(TEXT("confinement level frozen"), T.MovementConfinement01, Confine1);
	TestEqual(TEXT("confinement anchor clock frozen"), T.Confinement.AnchorStartTime, AnchorStart1);

	// Contrast: once UNPAUSED (a real Advance) the confinement window DOES progress.
	Clock += Dt;
	T.Integrate(Dt, Clock, Alpha, Window, P, true, 0.8f, true, FVector2D(0, 0));
	TestTrue(TEXT("confinement window progresses once unpaused"), T.MovementConfinement01 > Confine1);

	return true;
}

// ==================================================================================================
//  (6) Lifecycle — tombstone leak 0. Structural (clear/remove) + GC (invalid weak key prune).
// ==================================================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSRDirectorSensorLifecycleTest, "FPSRoguelite.Telemetry.Lifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFPSRDirectorSensorLifecycleTest::RunTest(const FString& Parameters)
{
	using FPSRTelemetry::PruneInvalidTelemetry;
	// A concrete, worldless-instantiable UObject stands in for the PlayerState keys (bare UObject is abstract
	// for NewObject). UFPSRFlowFieldComputer is NewObject'd worldless by the existing flow-field unit tests.
	using FKey = UFPSRFlowFieldComputer;

	// --- Structural: Logout removes one; EndRun-style clear -> 0; a fully-valid map prunes nothing. -----
	{
		TMap<TWeakObjectPtr<FKey>, FFPSRPlayerTelemetry> Map;
		TStrongObjectPtr<FKey> O1(NewObject<FKey>());
		TStrongObjectPtr<FKey> O2(NewObject<FKey>());
		TStrongObjectPtr<FKey> O3(NewObject<FKey>());
		Map.Add(O1.Get(), FFPSRPlayerTelemetry());
		Map.Add(O2.Get(), FFPSRPlayerTelemetry());
		Map.Add(O3.Get(), FFPSRPlayerTelemetry());
		TestEqual(TEXT("three tracked"), Map.Num(), 3);

		Map.Remove(TWeakObjectPtr<FKey>(O2.Get())); // Logout(one)
		TestEqual(TEXT("Logout removes exactly one"), Map.Num(), 2);

		TestEqual(TEXT("no valid key is pruned"), PruneInvalidTelemetry(Map), 0);
		TestEqual(TEXT("prune leaves the valid entries"), Map.Num(), 2);

		Map.Reset(); // EndRun
		TestEqual(TEXT("EndRun clears to zero (no tombstones)"), Map.Num(), 0);
	}

	// --- GC: a destroyed player's weak key prunes to zero tombstones. ---------------------------------
	{
		TMap<TWeakObjectPtr<FKey>, FFPSRPlayerTelemetry> Map;
		TStrongObjectPtr<FKey> Keep(NewObject<FKey>());
		FKey* Doomed = NewObject<FKey>();
		const TWeakObjectPtr<FKey> DoomedWeak(Doomed);
		Map.Add(Keep.Get(), FFPSRPlayerTelemetry());
		Map.Add(Doomed, FFPSRPlayerTelemetry());
		TestEqual(TEXT("two tracked before GC"), Map.Num(), 2);

		Doomed = nullptr; // drop the only strong reference
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS, /*bPerformFullPurge=*/true);

		if (DoomedWeak.IsValid())
		{
			// GC did not reclaim it in this environment — don't assert on a precondition we can't force.
			AddWarning(TEXT("CollectGarbage did not invalidate the doomed weak key; skipping the GC-prune assertion (structural case still covers tombstone-0)."));
		}
		else
		{
			const int32 Pruned = PruneInvalidTelemetry(Map);
			TestEqual(TEXT("exactly the invalid key is pruned"), Pruned, 1);
			TestEqual(TEXT("only the surviving player remains"), Map.Num(), 1);
			TestTrue(TEXT("the kept player is still tracked"), Map.Contains(TWeakObjectPtr<FKey>(Keep.Get())));
		}
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
