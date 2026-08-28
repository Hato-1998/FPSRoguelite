// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Run/FPSRStageDirectorSubsystem.h"
#include "Run/FPSRStageFadeSubsystem.h"
#include "Core/FPSRGameState.h"
#include "Enemy/FPSRFlowFieldComputer.h"
#include "Arena/FPSRArenaTypes.h" // EFPSRArenaRole (NextCombatArenaIndex's role filter)
#include "Containers/ArrayView.h" // TConstArrayView

#if WITH_AUTOMATION_TESTS

// Headless invariant net for the ADR 0010 D6 stage-transition state machine's PURE, worldless predicates — no
// world, no SpawnActor (mirrors FPSREnemyFrontBudgetTest.cpp / FPSRDirectorSensorTest.cpp's style):
//   DecidePhaseAfterDealing / CanSwapNow — the Grace->{Pending,FadeOut} branch (invariant 8; Phase A phase split).
//   IsDealingOpen                       — the FIXED-time dealing window actually closes at its end timestamp.
//   ComputeStageSeed                    — deterministic + adjacent stages diverge (server computes once, replicates).
//   NextArenaIndex                      — cycling through N arenas, including the single-arena self-cycle case.
//   NextCombatArenaIndex                — the same cycle with boss arenas SKIPPED (보스 스테이지 라우팅).
//   EFPSRStageTransitionPhase::None==0  — the enum's default must be the "normal play" value (safe replication default).
//   FadeOut/FadeIn appended AFTER Swapping — Phase A must not reorder the pre-existing ordinals.
//   UFPSRFlowFieldComputer::FindNearestOpenCell — the Phase A carry-over snap's worldless ring-search core.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSRStageTransitionTest, "FPSRoguelite.Arena.StageTransition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFPSRStageTransitionTest::RunTest(const FString& Parameters)
{
	using U = UFPSRStageDirectorSubsystem;

	// --- (1) DecidePhaseAfterDealing: Grace closes to FadeOut when unpaused (Phase A — used to be Swapping),
	//         Pending when the card freeze is up. ------------------------------------------------------------------
	{
		TestTrue(TEXT("DecidePhaseAfterDealing(false) == FadeOut"),
			U::DecidePhaseAfterDealing(false) == EFPSRStageTransitionPhase::FadeOut);
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

		// 1 arena: cycles to itself. NextArenaIndex(0,1)==0 is still correct on its own, but the self-cycle is only
		// SOUND end-to-end because AFPSRArenaActor::SetArenaActive(true) resets every broken AFPSRArenaDestructible
		// in the grid back to intact (F2, ServerReset) — without that reset, a self-cycle would revisit an arena
		// whose suppressor is still bBroken (and 0 health, unbreakable) forever, and the very next transition could
		// never be triggered again. This predicate doesn't know about destructibles at all; this comment exists so
		// that dependency doesn't get silently assumed away if PerformSwap's step order ever changes.
		TestEqual(TEXT("1 arena: 0 -> 0 (self-cycle)"), U::NextArenaIndex(0, 1), 0);

		// No arenas: INDEX_NONE.
		TestEqual(TEXT("0 arenas -> INDEX_NONE"), U::NextArenaIndex(0, 0), INDEX_NONE);
		TestEqual(TEXT("negative count -> INDEX_NONE"), U::NextArenaIndex(0, -1), INDEX_NONE);
	}

	// --- (5b) NextCombatArenaIndex: the same cycle, but the suppressor path must SKIP boss arenas (보스 스테이지,
	//          2026-08-28). The boss stage carries no suppressor by design, so a party cycled into it early would be
	//          stuck there with no boss and no way out — this predicate is the only thing standing between the
	//          ordinary transition and that state. ------------------------------------------------------------------
	{
		using ER = EFPSRArenaRole;

		// 3 combat arenas: identical to NextArenaIndex — the role filter must be a no-op when nothing is filtered.
		const ER AllCombat[] = { ER::Combat, ER::Combat, ER::Combat };
		TestEqual(TEXT("3 combat: 0 -> 1"), U::NextCombatArenaIndex(0, AllCombat), 1);
		TestEqual(TEXT("3 combat: 2 -> 0 (wraps)"), U::NextCombatArenaIndex(2, AllCombat), 0);

		// The realistic authoring shape: two combat arenas + one boss arena last. The cycle must bounce OVER index 2
		// and come back to 0 — this is the case that would otherwise drop the party into the boss stage on the
		// second suppressor break of every run.
		const ER TwoCombatOneBoss[] = { ER::Combat, ER::Combat, ER::Boss };
		TestEqual(TEXT("combat,combat,boss: 0 -> 1"), U::NextCombatArenaIndex(0, TwoCombatOneBoss), 1);
		TestEqual(TEXT("combat,combat,boss: 1 -> 0 (skips the boss arena)"), U::NextCombatArenaIndex(1, TwoCombatOneBoss), 0);

		// A boss arena in the MIDDLE is skipped the same way — the filter is by role, not by position.
		const ER BossInMiddle[] = { ER::Combat, ER::Boss, ER::Combat };
		TestEqual(TEXT("combat,boss,combat: 0 -> 2 (skips index 1)"), U::NextCombatArenaIndex(0, BossInMiddle), 2);
		TestEqual(TEXT("combat,boss,combat: 2 -> 0 (wraps past index 1)"), U::NextCombatArenaIndex(2, BossInMiddle), 0);

		// One combat arena + one boss arena = the minimum shippable boss-stage map. The combat arena must self-cycle
		// (reseeded, same skeleton) rather than advance into the boss stage.
		const ER OneCombatOneBoss[] = { ER::Combat, ER::Boss };
		TestEqual(TEXT("combat,boss: 0 -> 0 (self-cycle, never the boss arena)"), U::NextCombatArenaIndex(0, OneCombatOneBoss), 0);

		// Degenerate inputs. An all-boss world is an authoring fault, and INDEX_NONE is what makes PerformSwap abort
		// loudly instead of swapping into the boss stage.
		const ER AllBoss[] = { ER::Boss, ER::Boss };
		TestEqual(TEXT("all boss -> INDEX_NONE"), U::NextCombatArenaIndex(0, AllBoss), INDEX_NONE);
		TestEqual(TEXT("empty roles -> INDEX_NONE"), U::NextCombatArenaIndex(0, TConstArrayView<ER>()), INDEX_NONE);

		// An unknown current index (INDEX_NONE — no active arena yet) must still find a destination rather than
		// stalling the run: the scan starts before the first entry.
		TestEqual(TEXT("unknown current (-1) -> first combat arena"), U::NextCombatArenaIndex(INDEX_NONE, BossInMiddle), 0);
	}

	// --- (6) EFPSRStageTransitionPhase::None must be 0 — the safe replication default (a fresh GameState, or a
	//         client that hasn't yet received the real value, must read as "no transition in progress"). Phase A:
	//         FadeOut/FadeIn must be appended AFTER Swapping, not interleaved with the pre-existing values — a
	//         regression here would silently reorder every already-replicated/saved ordinal. ----------------------
	{
		TestEqual(TEXT("EFPSRStageTransitionPhase::None == 0"),
			static_cast<uint8>(EFPSRStageTransitionPhase::None), static_cast<uint8>(0));

		TestEqual(TEXT("Pending == 1 (unchanged by the Phase A append)"),
			static_cast<uint8>(EFPSRStageTransitionPhase::Pending), static_cast<uint8>(1));
		TestEqual(TEXT("Grace == 2 (unchanged)"),
			static_cast<uint8>(EFPSRStageTransitionPhase::Grace), static_cast<uint8>(2));
		TestEqual(TEXT("Swapping == 3 (unchanged)"),
			static_cast<uint8>(EFPSRStageTransitionPhase::Swapping), static_cast<uint8>(3));
		TestEqual(TEXT("FadeOut == Swapping + 1 (appended right after Swapping)"),
			static_cast<uint8>(EFPSRStageTransitionPhase::FadeOut),
			static_cast<uint8>(static_cast<uint8>(EFPSRStageTransitionPhase::Swapping) + 1));
		TestEqual(TEXT("FadeIn == FadeOut + 1"),
			static_cast<uint8>(EFPSRStageTransitionPhase::FadeIn),
			static_cast<uint8>(static_cast<uint8>(EFPSRStageTransitionPhase::FadeOut) + 1));
	}

	// --- (7) UFPSRFlowFieldComputer::FindNearestOpenCell — the Phase A carry-over snap's worldless ring-search core.
	//         Synthetic 5x5 grid, single layer (rank 0), NumLayers==2 so rank 1 is always absent everywhere. -------
	{
		using C = UFPSRFlowFieldComputer;
		constexpr int32 NL = C::NumLayers; // 2
		constexpr int32 W = 5, H = 5;
		constexpr int32 NumCells = W * H;

		auto Surf = [](int32 Cell, int32 Rank) { return Cell * NL + Rank; };
		auto CellOf = [](int32 CX, int32 CY) { return CY * W + CX; };

		// All rank-0 surfaces present + open by default; rank 1 absent everywhere (MAX_flt).
		TArray<float> FloorZ;
		FloorZ.Init(MAX_flt, NumCells * NL);
		TArray<bool> Blocked;
		Blocked.Init(false, NumCells * NL);
		for (int32 Cell = 0; Cell < NumCells; ++Cell)
		{
			FloorZ[Surf(Cell, 0)] = 0.0f;
		}

		// (7a) Open self cell -> returns itself at radius 0.
		TestEqual(TEXT("open self cell -> itself"),
			C::FindNearestOpenCell(FloorZ, Blocked, W, H, 2, 2, 8), CellOf(2, 2));

		// (7b) Block the self cell -> nearest open cell is one of its radius-1 neighbours (still open); the FIXED
		//      scan order (dy outer -1..1, dx outer -1..1 within the ring, ring-perimeter only) picks (1,1) first
		//      (the smallest dy, then smallest dx, excluding the already-checked interior at radius 0).
		Blocked[Surf(CellOf(2, 2), 0)] = true;
		TestEqual(TEXT("blocked self cell -> nearest open cell via radius-1 scan order"),
			C::FindNearestOpenCell(FloorZ, Blocked, W, H, 2, 2, 8), CellOf(1, 1));

		// (7c) Determinism: two calls with the same (blocked) input resolve to the SAME cell (invariant 10).
		TestEqual(TEXT("determinism: repeat call -> same cell"),
			C::FindNearestOpenCell(FloorZ, Blocked, W, H, 2, 2, 8), CellOf(1, 1));

		// (7d) Every cell in the grid blocked -> no open cell anywhere -> INDEX_NONE even with a huge radius.
		TArray<bool> AllBlocked;
		AllBlocked.Init(true, NumCells * NL);
		TestEqual(TEXT("every cell blocked -> INDEX_NONE"),
			C::FindNearestOpenCell(FloorZ, AllBlocked, W, H, 2, 2, 8), INDEX_NONE);

		// (7e) Self blocked, radius too small to reach ANY open neighbour (all of radius 1 blocked too) -> INDEX_NONE
		//      within that small radius, even though an open cell exists further out (MaxRadiusCells is honored).
		TArray<bool> RingBlocked = Blocked; // self (2,2) already blocked from (7b)
		for (int32 dy = -1; dy <= 1; ++dy)
		{
			for (int32 dx = -1; dx <= 1; ++dx)
			{
				RingBlocked[Surf(CellOf(2 + dx, 2 + dy), 0)] = true; // blocks the whole radius-1 ring too
			}
		}
		TestEqual(TEXT("blocked within MaxRadiusCells -> INDEX_NONE (radius too small)"),
			C::FindNearestOpenCell(FloorZ, RingBlocked, W, H, 2, 2, 1), INDEX_NONE);
		// TestTrue + != (not TestNotEqual): TestNotEqual has no int32-specific overload (unlike TestEqual), so a
		// bare int32 pair is ambiguous between its float/double overloads and its generic template on MSVC (C2668).
		TestTrue(TEXT("same block pattern, larger radius -> finds the radius-2 opening"),
			C::FindNearestOpenCell(FloorZ, RingBlocked, W, H, 2, 2, 2) != INDEX_NONE);
	}

	// --- (8) UFPSRStageFadeSubsystem::ComputeStageFadeAlpha — the Phase B client fade driver's pure alpha calc. ------
	{
		using F = UFPSRStageFadeSubsystem;
		constexpr float FadeOutSeconds = 0.8f;
		constexpr float FadeInSeconds = 0.8f;

		// (8a) None/Grace/Pending -> 0, regardless of a (nonsensical here, but shouldn't matter) nonzero End.
		TestEqual(TEXT("None -> 0"),
			F::ComputeStageFadeAlpha(EFPSRStageTransitionPhase::None, 10.0f, 20.0f, FadeOutSeconds, FadeInSeconds), 0.0f);
		TestEqual(TEXT("Grace -> 0"),
			F::ComputeStageFadeAlpha(EFPSRStageTransitionPhase::Grace, 10.0f, 20.0f, FadeOutSeconds, FadeInSeconds), 0.0f);
		TestEqual(TEXT("Pending -> 0"),
			F::ComputeStageFadeAlpha(EFPSRStageTransitionPhase::Pending, 10.0f, 20.0f, FadeOutSeconds, FadeInSeconds), 0.0f);

		// (8b) FadeOut, halfway through (Now == End - FadeOutSeconds/2) -> ~0.5 (default float TestEqual tolerance).
		TestEqual(TEXT("FadeOut halfway -> 0.5"),
			F::ComputeStageFadeAlpha(EFPSRStageTransitionPhase::FadeOut, 19.6f, 20.0f, FadeOutSeconds, FadeInSeconds),
			0.5f);

		// (8c) FadeOut, End reached (Now == End) -> 1 (fully faded).
		TestEqual(TEXT("FadeOut End reached -> 1"),
			F::ComputeStageFadeAlpha(EFPSRStageTransitionPhase::FadeOut, 20.0f, 20.0f, FadeOutSeconds, FadeInSeconds), 1.0f);

		// (8d) Swapping -> always 1, however far Now sits from a (here irrelevant) End.
		TestEqual(TEXT("Swapping -> 1"),
			F::ComputeStageFadeAlpha(EFPSRStageTransitionPhase::Swapping, 5.0f, 0.0f, FadeOutSeconds, FadeInSeconds), 1.0f);

		// (8e) FadeIn, halfway through -> ~0.5.
		TestEqual(TEXT("FadeIn halfway -> 0.5"),
			F::ComputeStageFadeAlpha(EFPSRStageTransitionPhase::FadeIn, 19.6f, 20.0f, FadeOutSeconds, FadeInSeconds),
			0.5f);

		// (8f) FadeIn, End reached -> 0 (fully cleared).
		TestEqual(TEXT("FadeIn End reached -> 0"),
			F::ComputeStageFadeAlpha(EFPSRStageTransitionPhase::FadeIn, 20.0f, 20.0f, FadeOutSeconds, FadeInSeconds), 0.0f);

		// (8g) 0-length transition (End<=0, the hard-cut path EnterFadeOut/EnterFadeIn publish): FadeOut -> 1
		//      (already fully faded, matching the instant hard-cut), FadeIn -> 0 (already clear).
		TestEqual(TEXT("FadeOut, End<=0 -> 1"),
			F::ComputeStageFadeAlpha(EFPSRStageTransitionPhase::FadeOut, 5.0f, 0.0f, FadeOutSeconds, FadeInSeconds), 1.0f);
		TestEqual(TEXT("FadeIn, End<=0 -> 0"),
			F::ComputeStageFadeAlpha(EFPSRStageTransitionPhase::FadeIn, 5.0f, 0.0f, FadeOutSeconds, FadeInSeconds), 0.0f);

		// (8h) FadeOutSeconds/FadeInSeconds<=0 with a real (>0) End: defended against a 0-division rather than
		//      producing inf/NaN — reads the same as the 0-length case above.
		TestEqual(TEXT("FadeOut, FadeOutSeconds<=0 -> 1 (no 0-div)"),
			F::ComputeStageFadeAlpha(EFPSRStageTransitionPhase::FadeOut, 19.9f, 20.0f, 0.0f, FadeInSeconds), 1.0f);
		TestEqual(TEXT("FadeIn, FadeInSeconds<=0 -> 0 (no 0-div)"),
			F::ComputeStageFadeAlpha(EFPSRStageTransitionPhase::FadeIn, 19.9f, 20.0f, FadeOutSeconds, 0.0f), 0.0f);
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
