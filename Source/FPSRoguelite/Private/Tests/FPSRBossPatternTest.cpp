// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/FPSRAbilitySystemComponent.h"
#include "Boss/FPSRBossBase.h"
#include "Boss/FPSRBossHomingOrb.h"
#include "Boss/FPSRBossLaserMath.h"
#include "Boss/FPSRBossTypes.h"
#include "GameplayEffect.h"

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

	// --- The accessor and the hit test must agree ----------------------------------------------------------------
	// 🔴 G2 P2-1 regression. AFPSRBossBase::GetBeamState (what Blueprints draw with) once inlined a one-segment
	// formula while the hit test used the two-segment one, so the drawn beam ran ahead of the biting beam by
	// Speed x Grace — 45 deg at phase 1, 90 at phase 3. A player would jump through empty air and then be hit
	// standing still. PIE could not catch it because the debug overlay happened to use the correct side.
	// This asserts the property that makes that class of bug impossible: everyone calls the same function.
	{
		const float Start = 90.0f, Speed = 30.0f, GraceEnd = 105.0f;
		// Inside the grace the beam has not moved at all — the broken formula returned Start + Speed*(Now-StartClock).
		TestTrue(TEXT("no rotation before the grace ends"),
			FMath::IsNearlyEqual(BeamBaseAngleAt(Start, Speed, GraceEnd, 100.0f), Start, 0.001f));
		// And after it, the angle is measured from the GRACE END, not from the activation.
		TestTrue(TEXT("rotation is measured from the grace end"),
			FMath::IsNearlyEqual(BeamBaseAngleAt(Start, Speed, GraceEnd, 107.0f), Start + 60.0f, 0.001f));
		// The size of the bug that was: judged-from-activation would have been 45 degrees ahead here.
		const float BrokenOneSegment = Start + Speed * (107.0f - 100.0f);
		TestTrue(TEXT("the one-segment formula really does differ (guard against a silent revert)"),
			!FMath::IsNearlyEqual(BrokenOneSegment, BeamBaseAngleAt(Start, Speed, GraceEnd, 107.0f), 1.0f));
	}

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

	// The same trap with a worse blast radius: the orb derives straight from AActor (where bCanEverTick defaults to
	// false), and its whole grace/chase/divert/dwell state machine lives in Tick. Without the flag it would spawn,
	// hang in the air forever, and never chase, damage, or clean itself up — green build, green automation.
	const AFPSRBossHomingOrb* OrbCDO = GetDefault<AFPSRBossHomingOrb>();
	TestNotNull(TEXT("orb CDO"), OrbCDO);
	if (OrbCDO)
	{
		TestTrue(TEXT("orb CDO has bCanEverTick — its whole state machine is in Tick"),
			OrbCDO->PrimaryActorTick.bCanEverTick);
	}

	return true;
}

// ---------------------------------------------------------------------------------------------------------------
//  Selection order
// ---------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSRBossSelectionTest, "FPSRoguelite.Boss.Selection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFPSRBossSelectionTest::RunTest(const FString& Parameters)
{
	TArray<int32> Order;

	// Nothing eligible -> nothing to visit. The selector must not index an empty array.
	FPSRBoss::BuildSelectionOrder(EFPSRBossPatternSelection::Sequential, 0, 0, 0, Order);
	TestEqual(TEXT("no eligible patterns -> empty order"), Order.Num(), 0);

	// Sequential starts AT the cursor and wraps.
	FPSRBoss::BuildSelectionOrder(EFPSRBossPatternSelection::Sequential, 3, 1, 0, Order);
	TestEqual(TEXT("sequential visits all three"), Order.Num(), 3);
	TestEqual(TEXT("sequential starts at the cursor"), Order[0], 1);
	TestEqual(TEXT("sequential steps forward"), Order[1], 2);
	TestEqual(TEXT("sequential wraps to the front"), Order[2], 0);

	// 🔴 The property that is easy to lose: Random must still visit EVERY candidate. A "roll once and give up"
	// random would waste the trigger whenever the rolled pattern happened to be on cooldown, and the boss would
	// stand idle for a beat with nothing in the log to explain it.
	for (int32 RandomStart = 0; RandomStart < 3; ++RandomStart)
	{
		FPSRBoss::BuildSelectionOrder(EFPSRBossPatternSelection::Random, 3, 0, RandomStart, Order);
		TestEqual(TEXT("random visits all three"), Order.Num(), 3);
		TestEqual(TEXT("random honours its start"), Order[0], RandomStart);

		TArray<int32> Sorted = Order;
		Sorted.Sort();
		TestEqual(TEXT("random visits index 0 exactly once"), Sorted[0], 0);
		TestEqual(TEXT("random visits index 1 exactly once"), Sorted[1], 1);
		TestEqual(TEXT("random visits index 2 exactly once"), Sorted[2], 2);
	}

	// Random ignores the sequential cursor and Sequential ignores the roll — otherwise the two policies bleed into
	// each other and "Sequential" quietly stops being learnable.
	FPSRBoss::BuildSelectionOrder(EFPSRBossPatternSelection::Random, 4, 3, 1, Order);
	TestEqual(TEXT("random uses the roll, not the cursor"), Order[0], 1);
	FPSRBoss::BuildSelectionOrder(EFPSRBossPatternSelection::Sequential, 4, 3, 1, Order);
	TestEqual(TEXT("sequential uses the cursor, not the roll"), Order[0], 3);

	// A cursor that has run past the array (patterns removed from GrantedAbilities between activations, or an
	// eligibility list that shrank when a MinPhase gate closed) must fold back into range rather than producing an
	// out-of-bounds index into Eligible.
	FPSRBoss::BuildSelectionOrder(EFPSRBossPatternSelection::Sequential, 3, 7, 0, Order);
	TestEqual(TEXT("oversized cursor folds in range"), Order[0], 1);
	FPSRBoss::BuildSelectionOrder(EFPSRBossPatternSelection::Sequential, 3, -1, 0, Order);
	TestEqual(TEXT("negative cursor folds in range"), Order[0], 2);

	return true;
}

// ---------------------------------------------------------------------------------------------------------------
//  Authoring rules (the pure half of IsDataValid)
// ---------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSRBossAuthoringTest, "FPSRoguelite.Boss.Authoring",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFPSRBossAuthoringTest::RunTest(const FString& Parameters)
{
	using EIssue = FPSRBoss::ETriggerAuthoringIssue;

	FFPSRBossPatternTrigger Trigger;
	Trigger.Kind = EFPSRBossTriggerKind::Elapsed;
	Trigger.Threshold = 8.0f;
	TestTrue(TEXT("a normal elapsed trigger is fine"), FPSRBoss::ValidateTrigger(Trigger) == EIssue::None);

	Trigger.Threshold = 0.0f;
	TestTrue(TEXT("threshold 0 is rejected"), FPSRBoss::ValidateTrigger(Trigger) == EIssue::ThresholdNotPositive);

	Trigger.Kind = EFPSRBossTriggerKind::HealthBelow;
	Trigger.Threshold = 0.66f;
	TestTrue(TEXT("a normal health trigger is fine"), FPSRBoss::ValidateTrigger(Trigger) == EIssue::None);

	Trigger.Threshold = 1.0f;
	TestTrue(TEXT("HealthBelow at full health is rejected"),
		FPSRBoss::ValidateTrigger(Trigger) == EIssue::HealthThresholdFull);

	// A threshold above 1 is only wrong for HealthBelow — "every 5 patterns" is perfectly normal authoring, and the
	// rule must not confuse the two axes.
	Trigger.Kind = EFPSRBossTriggerKind::PatternCount;
	Trigger.Threshold = 5.0f;
	TestTrue(TEXT("a count trigger above 1 is fine"), FPSRBoss::ValidateTrigger(Trigger) == EIssue::None);

	// Marker peak: 4 players with a 1.4 s fuse and a 2.0 s interval means only the volley just fired is ever alive.
	TestEqual(TEXT("fuse shorter than the interval -> one volley alive"),
		FPSRBoss::EstimatePeakBlastMarks(4, 1.4f, 2.0f), 4);
	// A fuse three intervals long keeps three earlier volleys up alongside the new one.
	TestEqual(TEXT("fuse of three intervals -> four volleys alive"),
		FPSRBoss::EstimatePeakBlastMarks(4, 6.0f, 2.0f), 16);
	TestEqual(TEXT("solo play scales down"), FPSRBoss::EstimatePeakBlastMarks(1, 6.0f, 2.0f), 4);
	// Degenerate authoring must return 0 rather than dividing by zero.
	TestEqual(TEXT("zero interval is not a division"), FPSRBoss::EstimatePeakBlastMarks(4, 6.0f, 0.0f), 0);
	TestEqual(TEXT("no players -> no markers"), FPSRBoss::EstimatePeakBlastMarks(0, 6.0f, 2.0f), 0);

	return true;
}

// ---------------------------------------------------------------------------------------------------------------
//  Time-axis guard
// ---------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSRBossTimeAxisGuardTest, "FPSRoguelite.Boss.TimeAxisGuard",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFPSRBossTimeAxisGuardTest::RunTest(const FString& Parameters)
{
	// 🔴 This guard is the ONLY runtime enforcement of the §2-2 freeze contract on the actor-owned ASCs — everything
	// else about "no duration or periodic GEs" is a comment. It is also silent when it breaks: a duration GE that
	// slips through simply keeps counting down during a level-up freeze, which surfaces as a balance complaint
	// months later rather than as a bug.
	UFPSRAbilitySystemComponent* ASC = NewObject<UFPSRAbilitySystemComponent>(GetTransientPackage());
	TestNotNull(TEXT("ASC"), ASC);
	if (!ASC)
	{
		return false;
	}

	TestEqual(TEXT("no queries before opting in"), ASC->GameplayEffectApplicationQueries.Num(), 0);
	ASC->EnableTimeAxisGuard();
	ASC->EnableTimeAxisGuard();
	TestEqual(TEXT("EnableTimeAxisGuard is idempotent"), ASC->GameplayEffectApplicationQueries.Num(), 1);
	if (ASC->GameplayEffectApplicationQueries.Num() != 1)
	{
		return false;
	}

	UGameplayEffect* InstantGE = NewObject<UGameplayEffect>(GetTransientPackage());
	InstantGE->DurationPolicy = EGameplayEffectDurationType::Instant;
	const FGameplayEffectSpec InstantSpec(InstantGE, FGameplayEffectContextHandle(), 1.0f);

	UGameplayEffect* DurationGE = NewObject<UGameplayEffect>(GetTransientPackage());
	DurationGE->DurationPolicy = EGameplayEffectDurationType::HasDuration;
	DurationGE->DurationMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(5.0f));
	const FGameplayEffectSpec DurationSpec(DurationGE, FGameplayEffectContextHandle(), 1.0f);

	// Infinite + Period is the hole a DurationPolicy-only check leaves open: it never expires, so a policy check
	// waves it through, but it re-executes forever on the world timer.
	UGameplayEffect* PeriodicGE = NewObject<UGameplayEffect>(GetTransientPackage());
	PeriodicGE->DurationPolicy = EGameplayEffectDurationType::Infinite;
	PeriodicGE->Period = FScalableFloat(1.0f);
	const FGameplayEffectSpec PeriodicSpec(PeriodicGE, FGameplayEffectContextHandle(), 1.0f);

	TestFalse(TEXT("an instant GE carries no unpausable timer"), FPSRAbilitySystem::IsTimeBasedEffect(InstantSpec));
	TestTrue(TEXT("a HasDuration GE does"), FPSRAbilitySystem::IsTimeBasedEffect(DurationSpec));
	TestTrue(TEXT("an infinite PERIODIC GE does too"), FPSRAbilitySystem::IsTimeBasedEffect(PeriodicSpec));

	// Drive the ALLOW path through the live delegate, so registration and the adapter's polarity are both covered.
	// The reject path is asserted on the pure rule above instead: the delegate deliberately ensure()s there, and an
	// automation run captures that ensure's whole callstack as test errors — which is why the rule was split out.
	// An inverted return in the adapter still fails here, because the same two lines serve both directions.
	TestTrue(TEXT("the registered query lets an instant GE through"),
		ASC->GameplayEffectApplicationQueries[0].Execute(ASC->GetActiveGameplayEffects(), InstantSpec));

	return true;
}

#endif // WITH_AUTOMATION_TESTS
