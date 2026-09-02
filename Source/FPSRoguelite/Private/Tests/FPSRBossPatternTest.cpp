// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Boss/FPSRBossBase.h"
#include "Boss/FPSRBossLaserMath.h"
#include "Boss/FPSRBossTypes.h"

// Worldless: every predicate under test is pure arithmetic, plus one CDO read. No SpawnActor, no world.
//
// Helper names carry a Bp1 suffix because unity builds merge anonymous namespaces across translation units, so a
// generic helper name here would collide with an identically named one in another test file and the error would be
// reported in THAT file.
namespace
{
	/** Walk a relative-angle sequence through DidEnterBeam and count the hits, exactly as the ability's per-frame
	 *  loop does (Prev seeded from the first sample, so the first frame can never score). */
	int32 CountBeamHitsBp1(const TArray<float>& RelSequence, float MaxStep, float HalfWidth)
	{
		if (RelSequence.Num() == 0)
		{
			return 0;
		}
		int32 Hits = 0;
		float Prev = RelSequence[0];
		for (int32 Index = 0; Index < RelSequence.Num(); ++Index)
		{
			const float Cur = RelSequence[Index];
			if (FPSRBossLaser::DidEnterBeam(Prev, Cur, MaxStep, HalfWidth))
			{
				++Hits;
			}
			Prev = Cur;
		}
		return Hits;
	}
}

// ---------------------------------------------------------------------------------------------------------------
//  Phase
// ---------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSRBossPhaseTest, "FPSRoguelite.Boss.Phase",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFPSRBossPhaseTest::RunTest(const FString& Parameters)
{
	using namespace FPSRBoss;

	// Phase COUNT is the array length — that is the whole point of the data-driven design, so it is asserted first.
	const TArray<float> Empty;
	TestEqual(TEXT("no thresholds -> always phase 1 (full)"), ComputePhase(1.0f, Empty), 1);
	TestEqual(TEXT("no thresholds -> always phase 1 (dead)"), ComputePhase(0.0f, Empty), 1);

	const TArray<float> Three = { 0.66f, 0.33f };
	TestEqual(TEXT("above first threshold -> 1"), ComputePhase(1.0f, Three), 1);
	TestEqual(TEXT("just above first threshold -> 1"), ComputePhase(0.6601f, Three), 1);
	TestEqual(TEXT("exactly at first threshold -> 2 (boundary is inclusive)"), ComputePhase(0.66f, Three), 2);
	TestEqual(TEXT("between thresholds -> 2"), ComputePhase(0.5f, Three), 2);
	TestEqual(TEXT("exactly at second threshold -> 3"), ComputePhase(0.33f, Three), 3);
	TestEqual(TEXT("at death -> deepest phase"), ComputePhase(0.0f, Three), 3);

	const TArray<float> Five = { 0.8f, 0.6f, 0.4f, 0.2f };
	TestEqual(TEXT("five-phase boss reaches phase 5"), ComputePhase(0.1f, Five), 5);
	TestEqual(TEXT("five-phase boss mid"), ComputePhase(0.5f, Five), 3);

	// ComputePhase takes the DEEPEST match rather than the first, so a mis-ordered array still yields a sane phase
	// instead of a silently wrong one (IsDataValid warns the designer separately).
	const TArray<float> Misordered = { 0.33f, 0.66f };
	TestEqual(TEXT("mis-ordered thresholds still resolve to the deepest match"), ComputePhase(0.2f, Misordered), 3);

	// Monotonic latch: healing must never walk a phase back.
	TestEqual(TEXT("latch keeps the higher phase"), LatchPhase(3, 1), 3);
	TestEqual(TEXT("latch advances"), LatchPhase(1, 2), 2);
	TestEqual(TEXT("latch is idempotent"), LatchPhase(2, 2), 2);

	return true;
}

// ---------------------------------------------------------------------------------------------------------------
//  Sweeping laser geometry
// ---------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSRBossLaserSweepTest, "FPSRoguelite.Boss.LaserSweep",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFPSRBossLaserSweepTest::RunTest(const FString& Parameters)
{
	using namespace FPSRBossLaser;

	// --- WrapToPeriod: N equally spaced beams collapse to one point -------------------------------------------
	for (int32 BeamCount = 1; BeamCount <= 5; ++BeamCount)
	{
		const float Period = 360.0f / BeamCount;
		for (int32 Beam = 0; Beam < BeamCount; ++Beam)
		{
			const float Folded = WrapToPeriod(Beam * Period, Period);
			TestTrue(*FString::Printf(TEXT("beam %d of %d folds onto 0"), Beam, BeamCount), FMath::Abs(Folded) < 0.001f);
		}
	}
	TestTrue(TEXT("wrap keeps the result inside the half period"), FMath::Abs(WrapToPeriod(179.0f, 360.0f)) <= 180.0f);
	TestTrue(TEXT("wrap handles negatives"), FMath::Abs(WrapToPeriod(-350.0f, 360.0f) - 10.0f) < 0.001f);

	const float MaxStep = 180.0f; // single beam -> Period/2
	const float W = 3.0f;         // beam half width + capsule angular radius

	// --- The five cases the hit rule promises ------------------------------------------------------------------

	// 1. Slow pass (beam sweeps, player still): exactly one hit, NOT one per frame inside the band.
	//    A level trigger would score 5 here — that is the over-count this rule exists to prevent.
	TestEqual(TEXT("slow pass scores exactly once"),
		CountBeamHitsBp1({ 6.0f, 4.0f, 2.0f, 0.0f, -2.0f, -4.0f, -6.0f }, MaxStep, W), 1);

	// 2. Fast pass (one frame steps straight over the band): still exactly one hit.
	//    🔴 This is the case a pure "entered the band" test loses completely — |Cur| is never <= W.
	TestEqual(TEXT("fast pass over the band still scores once"),
		CountBeamHitsBp1({ 5.0f, -5.0f }, MaxStep, W), 1);
	TestEqual(TEXT("very fast pass scores once"),
		CountBeamHitsBp1({ 40.0f, -40.0f }, MaxStep, W), 1);

	// 3. Reversing while inside the band: no second hit (the exposure was already counted).
	TestEqual(TEXT("reversing inside the band does not re-score"),
		CountBeamHitsBp1({ 5.0f, 1.0f, 2.0f, 1.0f, 0.5f }, MaxStep, W), 1);

	// 4. Leaving and coming back: a genuine second exposure, so two hits.
	TestEqual(TEXT("leaving and re-entering scores twice"),
		CountBeamHitsBp1({ 5.0f, 1.0f, 5.0f, 1.0f }, MaxStep, W), 2);

	// 5. First frame never scores, whether the player starts inside or outside the band.
	TestEqual(TEXT("first frame outside scores nothing"), CountBeamHitsBp1({ 20.0f }, MaxStep, W), 0);
	TestEqual(TEXT("first frame inside scores nothing"), CountBeamHitsBp1({ 0.0f }, MaxStep, W), 0);
	TestEqual(TEXT("starting inside the band does not score on the way out"),
		CountBeamHitsBp1({ 0.0f, 2.0f, 5.0f, 20.0f }, MaxStep, W), 0);

	// --- Domain wrap must not read as a crossing ---------------------------------------------------------------
	// Two beams: the domain is +-90, and stepping across that seam is the array wrapping, not the beam passing.
	TestEqual(TEXT("domain wrap is not a hit"),
		CountBeamHitsBp1({ 89.0f, -89.0f }, 90.0f, W), 0);

	// --- Player crossing a nearly stationary beam ---------------------------------------------------------------
	// The relative angle is what matters, so it makes no difference WHICH of the two is moving. This is the case a
	// beam-arc test drops.
	TestEqual(TEXT("player walking through a stationary beam scores once"),
		CountBeamHitsBp1({ 8.0f, 5.0f, 1.0f, -3.0f, -8.0f }, MaxStep, W), 1);

	// --- Capsule angular radius --------------------------------------------------------------------------------
	TestTrue(TEXT("angular radius shrinks with distance"),
		CapsuleAngularRadiusDeg(40.0f, 5000.0f) < CapsuleAngularRadiusDeg(40.0f, 1300.0f));
	TestTrue(TEXT("angular radius does not blow up at zero distance"),
		FMath::IsFinite(CapsuleAngularRadiusDeg(40.0f, 0.0f)) && CapsuleAngularRadiusDeg(40.0f, 0.0f) <= 90.0f);

	// --- Closed-form angle, with the stationary grace segment --------------------------------------------------
	// Server and client must agree exactly for the same clock, which is why nobody integrates a delta.
	// GraceEnd = 100: the beam must NOT move before then. That still window is what makes a beam born at a random
	// cardinal survivable for whoever it lands on.
	TestTrue(TEXT("beam is stationary during the grace"),
		FMath::IsNearlyEqual(BeamBaseAngleAt(10.0f, 30.0f, 100.0f, 98.0f), 10.0f, 0.001f));
	TestTrue(TEXT("beam is still at the start angle exactly at the grace end"),
		FMath::IsNearlyEqual(BeamBaseAngleAt(10.0f, 30.0f, 100.0f, 100.0f), 10.0f, 0.001f));
	TestTrue(TEXT("beam sweeps only after the grace"),
		FMath::IsNearlyEqual(BeamBaseAngleAt(10.0f, 30.0f, 100.0f, 102.0f), 70.0f, 0.001f));

	// --- Cardinal spawn ------------------------------------------------------------------------------------------
	// Beams are born at 12/3/6/9 so a player can name the direction it came from.
	for (int32 Trial = 0; Trial < 32; ++Trial)
	{
		const float Cardinal = RandomCardinalDeg();
		const bool bIsCardinal = FMath::IsNearlyEqual(Cardinal, 0.0f) || FMath::IsNearlyEqual(Cardinal, 90.0f)
			|| FMath::IsNearlyEqual(Cardinal, 180.0f) || FMath::IsNearlyEqual(Cardinal, 270.0f);
		TestTrue(TEXT("start angle is one of the four cardinals"), bIsCardinal);
	}

	return true;
}

// ---------------------------------------------------------------------------------------------------------------
//  Pattern triggers — the cadence of the whole fight
// ---------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSRBossTriggerTest, "FPSRoguelite.Boss.Trigger",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFPSRBossTriggerTest::RunTest(const FString& Parameters)
{
	using namespace FPSRBoss;

	// Helper: run a trigger forward through a series of samples and count how many times it fires, writing the latch
	// back exactly as AFPSRBossBase::ServerConsumeAnyTrigger does.
	auto FireCountOverBp1 = [](FFPSRBossPatternTrigger Trigger, const TArray<float>& Elapsed,
		const TArray<int32>& Patterns, const TArray<float>& Health) -> int32
	{
		const int32 Steps = FMath::Max3(Elapsed.Num(), Patterns.Num(), Health.Num());
		int32 Fires = 0;
		for (int32 Index = 0; Index < Steps; ++Index)
		{
			const float E = Elapsed.IsValidIndex(Index) ? Elapsed[Index] : 0.0f;
			const int32 P = Patterns.IsValidIndex(Index) ? Patterns[Index] : 0;
			const float H = Health.IsValidIndex(Index) ? Health[Index] : 1.0f;
			int32 NewCount = Trigger.FireCount;
			if (ShouldTriggerFire(Trigger, E, P, H, NewCount))
			{
				Trigger.FireCount = NewCount;
				++Fires;
			}
		}
		return Fires;
	};

	// --- Elapsed --------------------------------------------------------------------------------------------------
	{
		FFPSRBossPatternTrigger T;
		T.Kind = EFPSRBossTriggerKind::Elapsed;
		T.Threshold = 5.0f;
		T.bRepeating = true;
		// Ticking past 5, 10, 15 fires three times — once per threshold crossed, not once per tick spent above it.
		TestEqual(TEXT("repeating Elapsed fires once per threshold"),
			FireCountOverBp1(T, { 0.f, 4.9f, 5.1f, 7.f, 9.9f, 10.1f, 14.f, 15.2f }, {}, {}), 3);

		// 🔴 A single sample that jumps far past several thresholds must still fire ONCE. If this latched to
		// FireCount+1 instead of to the crossed count, a long pause would come back as a burst of patterns.
		TestEqual(TEXT("a big jump does not produce a backlog burst"),
			FireCountOverBp1(T, { 0.f, 100.0f }, {}, {}), 1);

		T.bRepeating = false;
		TestEqual(TEXT("non-repeating Elapsed fires exactly once"),
			FireCountOverBp1(T, { 0.f, 5.1f, 9.f, 20.f, 60.f }, {}, {}), 1);
	}

	// --- PatternCount ---------------------------------------------------------------------------------------------
	{
		FFPSRBossPatternTrigger T;
		T.Kind = EFPSRBossTriggerKind::PatternCount;
		T.Threshold = 3.0f;
		T.bRepeating = true;
		TestEqual(TEXT("repeating PatternCount fires every N patterns"),
			FireCountOverBp1(T, {}, { 0, 1, 2, 3, 4, 5, 6, 7 }, {}), 2);

		T.bRepeating = false;
		TestEqual(TEXT("non-repeating PatternCount fires once"),
			FireCountOverBp1(T, {}, { 0, 3, 6, 9 }, {}), 1);
	}

	// --- HealthBelow ----------------------------------------------------------------------------------------------
	{
		FFPSRBossPatternTrigger T;
		T.Kind = EFPSRBossTriggerKind::HealthBelow;
		T.Threshold = 0.5f;
		T.bRepeating = true; // deliberately true — it must STILL be one-shot
		// Health only falls in a boss fight, so a repeating reading would mean "every tick forever after crossing".
		TestEqual(TEXT("HealthBelow is one-shot even when marked repeating"),
			FireCountOverBp1(T, {}, {}, { 1.0f, 0.8f, 0.49f, 0.4f, 0.2f, 0.0f }), 1);

		TestEqual(TEXT("HealthBelow does not fire above the threshold"),
			FireCountOverBp1(T, {}, {}, { 1.0f, 0.9f, 0.6f, 0.51f }), 0);
	}

	// --- Threshold hygiene ----------------------------------------------------------------------------------------
	{
		// A zero threshold must not divide by zero or fire unboundedly.
		FFPSRBossPatternTrigger T;
		T.Kind = EFPSRBossTriggerKind::Elapsed;
		T.Threshold = 0.0f;
		T.bRepeating = false;
		const int32 Fires = FireCountOverBp1(T, { 0.f, 1.f, 2.f }, {}, {});
		TestTrue(TEXT("a zero threshold stays bounded"), Fires <= 1);
	}

	return true;
}

// ---------------------------------------------------------------------------------------------------------------
//  Class defaults
// ---------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSRBossTickEnabledTest, "FPSRoguelite.Boss.TickEnabled",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFPSRBossTickEnabledTest::RunTest(const FString& Parameters)
{
	// 🔴 The boss's entire pattern driver lives in Tick. AActor defaults bCanEverTick to false and this class used to
	// set it false explicitly (it was a stationary, tickless scaffold), so flipping it is easy to lose in a merge —
	// and losing it produces a GREEN build and GREEN automation with every pattern silently inert. A CDO assertion is
	// the only thing that fails when that happens.
	const AFPSRBossBase* BossCDO = GetDefault<AFPSRBossBase>();
	TestNotNull(TEXT("boss CDO"), BossCDO);
	if (BossCDO)
	{
		TestTrue(TEXT("boss CDO has bCanEverTick — the pattern driver is in Tick"),
			BossCDO->PrimaryActorTick.bCanEverTick);
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
