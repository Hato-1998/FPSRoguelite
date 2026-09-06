// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h" // FTestWorldWrapper — engine's own manually-tickable test-world helper
#include "Core/FPSRGameState.h"
#include "Engine/World.h"

#if WITH_AUTOMATION_TESTS

// STAT1 §6-1 (A단계 — 상태 전용 시계만): the FIRST world-driving automation test in this codebase — every existing
// Private/Tests/*.cpp is explicitly worldless (see e.g. FPSRHoverWindowRuntimeTest.cpp's own "No UWorld/UObject
// anywhere in this file" comment), so there is no in-repo idiom to copy; this follows the ENGINE's own precedent
// instead (Engine/Source/Runtime/Engine/Private/TimerManagerTests.cpp, System.Engine.TimerManager). A world is not
// optional here: RefreshStatusFreezeState/GetStatusClockSeconds read World->GetTimeSeconds(), and every bug this
// test locks down (rev2's backward clock, rev3's refcount leak, overlap double-subtraction) is specifically about
// how the clock behaves across REAL elapsed time — rewriting this as a pure-function test would delete the exact
// axis under test.
//
// Idiom = FTestWorldWrapper (Engine/Public/Tests/AutomationCommon.h): a manually-tickable EWorldType::Game world.
// Deliberately SKIPPED: WorldWrapper.BeginPlayInTestWorld(). That helper drives World->SetGameMode(), which would
// resolve this project's GlobalDefaultGameMode (a Blueprint, Config/DefaultEngine.ini) and pull a whole content
// dependency chain into what should be a narrow, fast, hermetic test of GameState arithmetic alone — and it isn't
// needed: World->Tick() updates World->GetTimeSeconds() unconditionally (LevelTick.cpp), with no dependency on
// BeginPlay having run (FTestWorldWrapper::TickTestWorld's own GFrameCounter++ is explicitly conditional on
// HasBegunPlay(), proving ticking-before-BeginPlay is an anticipated, supported use). AFPSRGameState is
// SpawnActor'd directly instead. A freshly spawned actor with no NetDriver defaults to ROLE_Authority
// (Actor.cpp's AActor::AActor() -> SetRole(ROLE_Authority); AGameStateBase's own constructor never touches Role),
// so HasAuthority() reads true with no further setup — the same assumption every FPSR.* debug console command
// already makes when it calls World->GetGameState<AFPSRGameState>() and straight into a server-only setter.

namespace
{
	// World->Tick() clamps any single call's DeltaSeconds to AWorldSettings::MaxUndilatedFrameTime (this project's
	// default 0.4s, Engine/Config/BaseGame.ini) — a single 8-second Tick call would silently become an 0.4-second
	// one and every elapsed-time assertion below would be wrong. Stepping in slices under that clamp (same idea
	// TimerManagerTests.cpp's TimerTest_TickWorld uses, for the identical reason) keeps the math exact.
	void TickStatusTestWorld(FTestWorldWrapper& WorldWrapper, float TotalSeconds)
	{
		constexpr float StepSeconds = 0.1f;
		while (TotalSeconds > 0.0f)
		{
			const float Step = FMath::Min(TotalSeconds, StepSeconds);
			WorldWrapper.TickTestWorld(Step);
			TotalSeconds -= Step;
		}
	}

	// Loose enough to absorb float drift from dozens of 0.1s ticks; tight enough that a real logic bug (a whole
	// missed/duplicated freeze span, measured in whole seconds in every case below) still fails loudly.
	constexpr float StatusClockTolerance = 0.02f;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSRStatusClockTest, "FPSRoguelite.Status.Clock",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFPSRStatusClockTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper WorldWrapper;
	if (!WorldWrapper.CreateTestWorld(EWorldType::Game))
	{
		AddError(TEXT("CreateTestWorld(EWorldType::Game) failed — cannot exercise a world-backed clock"));
		return false;
	}
	UWorld* World = WorldWrapper.GetTestWorld();
	if (!World)
	{
		AddError(TEXT("GetTestWorld() returned null after a successful CreateTestWorld"));
		return false;
	}

	// --- (1) Full phase traversal: None -> Grace -> Pending -> FadeOut -> Swapping -> FadeIn -> None. The clock
	//         must be monotonic, and the ENTIRE transition span (all five non-None phases) must be excluded from
	//         it — checked at every intermediate phase, not just the first/last (§10 월드 12-②). ------------------
	{
		AFPSRGameState* GS = World->SpawnActor<AFPSRGameState>();
		if (!GS)
		{
			AddError(TEXT("(1) SpawnActor<AFPSRGameState> failed"));
			return false;
		}
		TestTrue(TEXT("(1) a freshly spawned actor with no NetDriver has authority"), GS->HasAuthority());

		const float ClockAtSpawn = GS->GetStatusClockSeconds();

		TickStatusTestWorld(WorldWrapper, 2.0f); // None — the clock must run at real speed
		TestEqual(TEXT("(1) unfrozen: 2s of world time -> 2s of clock time"),
			GS->GetStatusClockSeconds(), ClockAtSpawn + 2.0f, StatusClockTolerance);

		GS->SetStageTransition(EFPSRStageTransitionPhase::Grace, World->GetTimeSeconds() + 8.0f);
		const float ClockAtFreeze = GS->GetStatusClockSeconds();

		// Walk every remaining phase, ticking 1s in each. EVERY reading must stay pinned at ClockAtFreeze — rev3
		// broke on an INTERMEDIATE step, not the first or last, so this checks each one individually.
		TickStatusTestWorld(WorldWrapper, 1.0f);
		TestEqual(TEXT("(1) Grace: clock pinned"), GS->GetStatusClockSeconds(), ClockAtFreeze, StatusClockTolerance);

		GS->SetStageTransition(EFPSRStageTransitionPhase::Pending, 0.0f);
		TickStatusTestWorld(WorldWrapper, 1.0f);
		TestEqual(TEXT("(1) Pending: clock pinned"), GS->GetStatusClockSeconds(), ClockAtFreeze, StatusClockTolerance);

		GS->SetStageTransition(EFPSRStageTransitionPhase::FadeOut, World->GetTimeSeconds() + 0.8f);
		TickStatusTestWorld(WorldWrapper, 1.0f);
		TestEqual(TEXT("(1) FadeOut: clock pinned"), GS->GetStatusClockSeconds(), ClockAtFreeze, StatusClockTolerance);

		GS->SetStageTransition(EFPSRStageTransitionPhase::Swapping, 0.0f);
		TickStatusTestWorld(WorldWrapper, 1.0f);
		TestEqual(TEXT("(1) Swapping: clock pinned"), GS->GetStatusClockSeconds(), ClockAtFreeze, StatusClockTolerance);

		GS->SetStageTransition(EFPSRStageTransitionPhase::FadeIn, World->GetTimeSeconds() + 0.8f);
		TickStatusTestWorld(WorldWrapper, 1.0f);
		TestEqual(TEXT("(1) FadeIn: clock pinned"), GS->GetStatusClockSeconds(), ClockAtFreeze, StatusClockTolerance);

		// Back to None — the unfreeze edge. The five 1s spans above (5s of real time) must all be excluded in ONE
		// shot; the reading right at the edge (before any further tick) must still equal ClockAtFreeze exactly.
		GS->SetStageTransition(EFPSRStageTransitionPhase::None, 0.0f);
		TestEqual(TEXT("(1) None (post-transition, no tick yet): clock still == pre-freeze reading"),
			GS->GetStatusClockSeconds(), ClockAtFreeze, StatusClockTolerance);

		TickStatusTestWorld(WorldWrapper, 1.5f);
		TestEqual(TEXT("(1) None: clock resumes from where it left off"),
			GS->GetStatusClockSeconds(), ClockAtFreeze + 1.5f, StatusClockTolerance);
	}

	// --- (2) Freeze OVERLAP: a card-selection pause opens and closes WHILE a transition is already active, then the
	//         transition itself ends. The overlapped span must be excluded exactly ONCE (not double-subtracted),
	//         and the clock must never read lower than a previous reading — rev2's exact failure mode. -------------
	{
		AFPSRGameState* GS = World->SpawnActor<AFPSRGameState>();
		if (!GS)
		{
			AddError(TEXT("(2) SpawnActor<AFPSRGameState> failed"));
			return false;
		}

		TickStatusTestWorld(WorldWrapper, 1.0f); // None
		GS->SetStageTransition(EFPSRStageTransitionPhase::Grace, World->GetTimeSeconds() + 8.0f);
		const float ClockAtFreeze = GS->GetStatusClockSeconds();

		TickStatusTestWorld(WorldWrapper, 1.0f); // frozen via the transition alone
		TestEqual(TEXT("(2) transition-only freeze: clock pinned"),
			GS->GetStatusClockSeconds(), ClockAtFreeze, StatusClockTolerance);

		// The pause opens ON TOP of the already-active transition — the composite OR is already true, so this must
		// be a complete no-op (no second anchor, no double bookkeeping).
		GS->SetRunPaused(true);
		TickStatusTestWorld(WorldWrapper, 1.0f); // frozen via BOTH conditions at once
		const float ReadingDuringOverlap = GS->GetStatusClockSeconds();
		TestEqual(TEXT("(2) overlapped freeze: clock still pinned (no double-count)"),
			ReadingDuringOverlap, ClockAtFreeze, StatusClockTolerance);
		TestTrue(TEXT("(2) overlapped freeze: clock did not regress"),
			ReadingDuringOverlap >= ClockAtFreeze - StatusClockTolerance);

		// The pause closes, but the TRANSITION is still active — the composite OR must stay true (the other half of
		// the overlap: clearing ONE of two active freeze conditions must not itself unfreeze the clock).
		GS->SetRunPaused(false);
		TickStatusTestWorld(WorldWrapper, 1.0f); // still frozen — the transition alone keeps it up
		TestEqual(TEXT("(2) pause lifted but transition still active: clock still pinned"),
			GS->GetStatusClockSeconds(), ClockAtFreeze, StatusClockTolerance);

		// NOW both conditions clear. The full 3-second overlapped span (1 before + 1 during + 1 after the pause)
		// must be excluded in exactly one deduction.
		GS->SetStageTransition(EFPSRStageTransitionPhase::None, 0.0f);
		TestEqual(TEXT("(2) fully unfrozen (no tick yet): clock == pre-freeze reading, not lower"),
			GS->GetStatusClockSeconds(), ClockAtFreeze, StatusClockTolerance);

		TickStatusTestWorld(WorldWrapper, 1.0f);
		TestEqual(TEXT("(2) clock resumes correctly after the overlap"),
			GS->GetStatusClockSeconds(), ClockAtFreeze + 1.0f, StatusClockTolerance);
	}

	// --- (3) Same-phase reset: UFPSRStageDirectorSubsystem re-arms an ALREADY-Grace phase with a new
	//         PhaseEndServerTime mid-transition (FPSRStageDirectorSubsystem.cpp:504) — SetStageTransition's
	//         early-out compares (Phase, EndTime) as a PAIR, so a same-phase/different-end-time call still reaches
	//         RefreshStatusFreezeState. IsStageTransitionActive() reads unchanged across the re-arm, so this must
	//         be a complete no-op — exactly where rev3's refcount scheme leaked. --------------------------------
	{
		AFPSRGameState* GS = World->SpawnActor<AFPSRGameState>();
		if (!GS)
		{
			AddError(TEXT("(3) SpawnActor<AFPSRGameState> failed"));
			return false;
		}

		TickStatusTestWorld(WorldWrapper, 1.0f); // None
		GS->SetStageTransition(EFPSRStageTransitionPhase::Grace, World->GetTimeSeconds() + 8.0f);
		const float ClockAtFreeze = GS->GetStatusClockSeconds();

		TickStatusTestWorld(WorldWrapper, 1.0f); // frozen, span #1 (pre-re-arm)
		TestEqual(TEXT("(3) Grace before re-arm: clock pinned"),
			GS->GetStatusClockSeconds(), ClockAtFreeze, StatusClockTolerance);

		// Re-arm: SAME phase (Grace), a NEW (later) end time. Must bypass SetStageTransition's early-out (the pair
		// changed) yet leave the status clock completely undisturbed (the phase itself did not change).
		GS->SetStageTransition(EFPSRStageTransitionPhase::Grace, World->GetTimeSeconds() + 8.0f);
		TestEqual(TEXT("(3) immediately after re-arm: anchor untouched"),
			GS->GetStatusClockSeconds(), ClockAtFreeze, StatusClockTolerance);

		TickStatusTestWorld(WorldWrapper, 1.0f); // frozen, span #2 (post-re-arm)
		TestEqual(TEXT("(3) Grace after re-arm: still pinned"),
			GS->GetStatusClockSeconds(), ClockAtFreeze, StatusClockTolerance);

		// Unfreeze. BOTH 1-second spans (pre- and post-re-arm) must be excluded — a broken anchor that got reset AT
		// the re-arm would only exclude the second span and let the first leak into the clock as +1.0s.
		GS->SetStageTransition(EFPSRStageTransitionPhase::None, 0.0f);
		TestEqual(TEXT("(3) unfrozen: BOTH pre- and post-re-arm spans excluded"),
			GS->GetStatusClockSeconds(), ClockAtFreeze, StatusClockTolerance);

		TickStatusTestWorld(WorldWrapper, 1.0f);
		TestEqual(TEXT("(3) clock resumes correctly after the same-phase reset"),
			GS->GetStatusClockSeconds(), ClockAtFreeze + 1.0f, StatusClockTolerance);
	}

	// --- (4) ResetStatusClockForNewRun: called mid-freeze — the EndRunFreeze scenario the function exists for
	//         (EndRunFreeze latches a PERMANENT freeze, so without this reset a same-world run restart would
	//         inherit bStatusFrozen==true forever). Must both (a) clear a PRE-EXISTING nonzero accumulator, and
	//         (b) actually let the clock start moving again — not just report one plausible-looking number. --------
	{
		AFPSRGameState* GS = World->SpawnActor<AFPSRGameState>();
		if (!GS)
		{
			AddError(TEXT("(4) SpawnActor<AFPSRGameState> failed"));
			return false;
		}

		TickStatusTestWorld(WorldWrapper, 1.0f); // None

		// A throwaway freeze/unfreeze cycle FIRST, purely to make AccumulatedStatusFrozenSeconds nonzero before the
		// reset — otherwise this case can't distinguish "the accumulator got cleared" from "it was already 0".
		GS->SetRunPaused(true);
		TickStatusTestWorld(WorldWrapper, 1.0f);
		GS->SetRunPaused(false); // AccumulatedStatusFrozenSeconds is now ~1.0, nonzero
		const float ClockBeforeEnd = GS->GetStatusClockSeconds();

		TickStatusTestWorld(WorldWrapper, 0.5f); // None, clock flowing normally again

		GS->EndRunFreeze(); // the permanent, never-released freeze this function exists to recover from
		const float ClockAtEnd = GS->GetStatusClockSeconds();
		TestEqual(TEXT("(4) EndRunFreeze: clock pinned at the moment it was called"),
			ClockAtEnd, ClockBeforeEnd + 0.5f, StatusClockTolerance);

		TickStatusTestWorld(WorldWrapper, 1.0f); // still frozen — sanity check before testing the reset
		TestEqual(TEXT("(4) still frozen post-EndRunFreeze (sanity check)"),
			GS->GetStatusClockSeconds(), ClockAtEnd, StatusClockTolerance);

		GS->ResetStatusClockForNewRun();

		// Right after the reset (no tick yet): bStatusFrozen==false and AccumulatedStatusFrozenSeconds==0, so the
		// formula collapses to Now - 0 - 0 == Now. If the accumulator (nonzero from the throwaway cycle above) had
		// NOT been cleared, this would read (Now - ~1.0) instead — this assertion catches exactly that.
		const float ReadingRightAfterReset = GS->GetStatusClockSeconds();
		// Bind to a float first: UWorld::GetTimeSeconds() returns DOUBLE in 5.7 (LWC), and passing it straight in
		// makes TestEqual(TCHAR*, float, double, float) ambiguous between the float and double overloads (C2666).
		const float RawWorldTimeNow = static_cast<float>(World->GetTimeSeconds());
		TestEqual(TEXT("(4) right after reset: clock == raw world time (state fully clean)"),
			ReadingRightAfterReset, RawWorldTimeNow, StatusClockTolerance);

		// The clock must actually FLOW again. If the reset had silently no-op'd (bStatusFrozen still true, stale
		// anchor untouched), the formula's Now-dependent terms would cancel and this reading would be a CONSTANT no
		// matter how much time passes — ticking and re-reading catches that.
		TickStatusTestWorld(WorldWrapper, 1.0f);
		TestEqual(TEXT("(4) clock is flowing again after reset"),
			GS->GetStatusClockSeconds(), ReadingRightAfterReset + 1.0f, StatusClockTolerance);
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
