// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Run/FPSRStageDirectorSubsystem.h"
#include "Core/FPSRGameState.h"

#if WITH_AUTOMATION_TESTS

// Headless invariant net for the ADR 0010 D6 stage-transition state machine's PURE, worldless predicates — no
// world, no SpawnActor (mirrors FPSREnemyFrontBudgetTest.cpp / FPSRDirectorSensorTest.cpp's style):
//   DecidePhaseAfterDealing / CanSwapNow — the Grace->{Pending,Swapping} branch (invariant 8).
//   IsDealingOpen                       — the FIXED-time dealing window actually closes at its end timestamp.
//   ComputeStageSeed                    — deterministic + adjacent stages diverge (server computes once, replicates).
//   NextArenaIndex                      — cycling through N arenas, including the single-arena self-cycle case.
//   EFPSRStageTransitionPhase::None==0  — the enum's default must be the "normal play" value (safe replication default).

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSRStageTransitionTest, "FPSRoguelite.Arena.StageTransition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFPSRStageTransitionTest::RunTest(const FString& Parameters)
{
	using U = UFPSRStageDirectorSubsystem;

	// --- (1) DecidePhaseAfterDealing: Grace closes to Swapping when unpaused, Pending when the card freeze is up. ---
	{
		TestTrue(TEXT("DecidePhaseAfterDealing(false) == Swapping"),
			U::DecidePhaseAfterDealing(false) == EFPSRStageTransitionPhase::Swapping);
		TestTrue(TEXT("DecidePhaseAfterDealing(true) == Pending"),
			U::DecidePhaseAfterDealing(true) == EFPSRStageTransitionPhase::Pending);
	}

	// --- (2) CanSwapNow: only when the card freeze is NOT up. ------------------------------------------------------
	{
		TestTrue(TEXT("CanSwapNow(false) == true"), U::CanSwapNow(false) == true);
		TestTrue(TEXT("CanSwapNow(true) == false"), U::CanSwapNow(true) == false);
	}

	// --- (3) IsDealingOpen: the FIXED dealing window actually closes at its end timestamp. --------------------------
	{
		TestTrue(TEXT("Grace + Now < End -> open"),
			U::IsDealingOpen(EFPSRStageTransitionPhase::Grace, 10.0f, 20.0f));
		TestFalse(TEXT("Grace + Now >= End -> CLOSED (fixed-time window actually closes)"),
			U::IsDealingOpen(EFPSRStageTransitionPhase::Grace, 20.0f, 20.0f));
		TestFalse(TEXT("Grace + Now > End -> closed"),
			U::IsDealingOpen(EFPSRStageTransitionPhase::Grace, 25.0f, 20.0f));

		// Every other phase reads closed even with an end timestamp still in the future — the predicate is
		// Grace-exclusive, not just a time comparison.
		TestFalse(TEXT("Pending, End in future -> closed"),
			U::IsDealingOpen(EFPSRStageTransitionPhase::Pending, 10.0f, 20.0f));
		TestFalse(TEXT("Swapping, End in future -> closed"),
			U::IsDealingOpen(EFPSRStageTransitionPhase::Swapping, 10.0f, 20.0f));
		TestFalse(TEXT("None, End in future -> closed"),
			U::IsDealingOpen(EFPSRStageTransitionPhase::None, 10.0f, 20.0f));
	}

	// --- (4) ComputeStageSeed: deterministic, adjacent stages diverge, same index -> same seed. ---------------------
	{
		const int32 BaseSeed = 12345;
		// Same (BaseSeed, StageIndex) -> same seed every call (the server computes this ONCE and replicates the
		// result; every client must be able to re-derive the identical value from the same two inputs).
		TestEqual(TEXT("Same stage index -> same seed (deterministic)"),
			U::ComputeStageSeed(BaseSeed, 3), U::ComputeStageSeed(BaseSeed, 3));
		TestNotEqual(TEXT("Adjacent stages produce different seeds (0 vs 1)"),
			U::ComputeStageSeed(BaseSeed, 0), U::ComputeStageSeed(BaseSeed, 1));
		TestNotEqual(TEXT("Adjacent stages produce different seeds (1 vs 2)"),
			U::ComputeStageSeed(BaseSeed, 1), U::ComputeStageSeed(BaseSeed, 2));
		// Locks the exact formula (via the named constant, not a re-typed literal) so a regression that changes the
		// SHAPE of ComputeStageSeed — not just its stride — is caught, not just its determinism/divergence properties.
		TestEqual(TEXT("ComputeStageSeed matches BaseSeed + StageIndex * StageSeedStride"),
			U::ComputeStageSeed(BaseSeed, 5), BaseSeed + 5 * U::StageSeedStride);
	}

	// --- (5) NextArenaIndex: cycles through N arenas; single arena cycles to itself; Count<=0 -> INDEX_NONE. --------
	{
		// 3 arenas: 0 -> 1 -> 2 -> 0.
		TestEqual(TEXT("3 arenas: 0 -> 1"), U::NextArenaIndex(0, 3), 1);
		TestEqual(TEXT("3 arenas: 1 -> 2"), U::NextArenaIndex(1, 3), 2);
		TestEqual(TEXT("3 arenas: 2 -> 0 (wraps)"), U::NextArenaIndex(2, 3), 0);

		// 1 arena: cycles to itself (the intended single-arena behavior, not an authoring gap).
		TestEqual(TEXT("1 arena: 0 -> 0 (self-cycle)"), U::NextArenaIndex(0, 1), 0);

		// No arenas: INDEX_NONE.
		TestEqual(TEXT("0 arenas -> INDEX_NONE"), U::NextArenaIndex(0, 0), INDEX_NONE);
		TestEqual(TEXT("negative count -> INDEX_NONE"), U::NextArenaIndex(0, -1), INDEX_NONE);
	}

	// --- (6) EFPSRStageTransitionPhase::None must be 0 — the safe replication default (a fresh GameState, or a
	//         client that hasn't yet received the real value, must read as "no transition in progress"). -----------
	{
		TestEqual(TEXT("EFPSRStageTransitionPhase::None == 0"),
			static_cast<uint8>(EFPSRStageTransitionPhase::None), static_cast<uint8>(0));
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
