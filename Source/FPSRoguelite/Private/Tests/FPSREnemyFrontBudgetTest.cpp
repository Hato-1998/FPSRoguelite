// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Enemy/FPSREnemySpawnSubsystem.h"
#include "Enemy/FPSRFlowFieldComputer.h" // EFPSRFieldQuery

#if WITH_AUTOMATION_TESTS

// Headless invariant net for the multimap U P-E front-pressure / trickle-drain PURE helpers (Codex + Opus adversarial gate).
// No world: exercises the exact static formulas the director calls (single source of truth), asserting the properties the
// two reviewers flagged as load-bearing — RELATIONSHIPS not magic tunable values, so PIE re-tuning of the budgets/ranges
// doesn't break the test while a regression in the FORMULA still does.
//   #1  front reserve is subtracted HONESTLY from the physical steady (the front never inflates the physical target),
//   #4  the drain clock's elapsed is clamped (a long freeze can't burst-drain the whole rear),
//   #6  a SourceLess / OffGrid reading is HOLD (never rear) so the source-less window never drains the near-door front.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSREnemyFrontBudgetTest, "FPSRoguelite.Allocator.FrontBudget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFPSREnemyFrontBudgetTest::RunTest(const FString& Parameters)
{
	using U = UFPSREnemySpawnSubsystem;
	constexpr int32 Big = 100000; // a target far above any cap, so PhysicalSteady is bound by (Cap - Seed - FrontReserved)

	// --- (A) Front reserve: 0 for no front, non-decreasing, and bounded (ceiling) --------------------------------------
	TestEqual(TEXT("FrontReserved(0)==0 (no active front)"), U::ComputeFrontReserved(0), 0);
	TestEqual(TEXT("FrontReserved(<0)==0"), U::ComputeFrontReserved(-3), 0);
	TestTrue(TEXT("FrontReserved(1) > 0 (an active front reserves some budget)"), U::ComputeFrontReserved(1) > 0);
	for (int32 n = 1; n <= 20; ++n)
	{
		TestTrue(TEXT("FrontReserved non-decreasing in slot count"),
			U::ComputeFrontReserved(n) >= U::ComputeFrontReserved(n - 1));
	}
	// Bounded: growing the slot count without limit must plateau at a ceiling (never runs away).
	TestEqual(TEXT("FrontReserved plateaus (ceiling caps many fronts)"),
		U::ComputeFrontReserved(50), U::ComputeFrontReserved(1000));

	// --- (B) Physical steady HONESTY (gate #1): the front reserve is subtracted EXACTLY from the physical steady ---------
	const int32 SteadyNoFront = U::ComputePhysicalSteady(Big, 0); // == GlobalAliveCap - SeedReserve
	TestTrue(TEXT("SteadyNoFront > 0"), SteadyNoFront > 0);
	for (int32 n = 0; n <= 20; ++n)
	{
		const int32 R = U::ComputeFrontReserved(n);
		const int32 Steady = U::ComputePhysicalSteady(Big, R);
		// The physical target drops by EXACTLY the reserve (no lying, no stealing) ...
		TestEqual(TEXT("PhysicalSteady drops by exactly FrontReserved"), SteadyNoFront - Steady, R);
		// ... and physical + front == the pre-P-E steady envelope (the front redistributes, never inflates the total).
		TestEqual(TEXT("PhysicalSteady + FrontReserved == pre-P-E steady"), Steady + R, SteadyNoFront);
	}
	// Never negative even if the reserve exceeds the whole steady.
	TestEqual(TEXT("PhysicalSteady clamps at 0"), U::ComputePhysicalSteady(0, U::ComputeFrontReserved(1000)), 0);
	// A modest target below the cap is honored verbatim (reserve 0).
	TestEqual(TEXT("PhysicalSteady honors a small target"), U::ComputePhysicalSteady(5, 0), 5);
	TestTrue(TEXT("PhysicalSteady <= target"), U::ComputePhysicalSteady(5, 0) <= 5);

	// --- (C) Rear classification (gate #6): only far-OK or Unreachable is rear; SourceLess/OffGrid/NoGrid = HOLD ---------
	TestTrue(TEXT("OK far -> rear"), U::IsRearStatus(EFPSRFieldQuery::OK, Big));
	TestFalse(TEXT("OK near (0) -> not rear"), U::IsRearStatus(EFPSRFieldQuery::OK, 0));
	TestTrue(TEXT("Unreachable -> rear (disconnected component)"), U::IsRearStatus(EFPSRFieldQuery::Unreachable, MAX_int32));
	TestTrue(TEXT("Unreachable rear regardless of dist"), U::IsRearStatus(EFPSRFieldQuery::Unreachable, 0));
	// The critical fail-closed cases — a source-less / off-grid window must NEVER drain the front:
	TestFalse(TEXT("SourceLess -> HOLD (never rear)"), U::IsRearStatus(EFPSRFieldQuery::SourceLess, MAX_int32));
	TestFalse(TEXT("SourceLess HOLD regardless of dist"), U::IsRearStatus(EFPSRFieldQuery::SourceLess, Big));
	TestFalse(TEXT("OffGrid -> HOLD"), U::IsRearStatus(EFPSRFieldQuery::OffGrid, Big));
	TestFalse(TEXT("NoGrid -> HOLD"), U::IsRearStatus(EFPSRFieldQuery::NoGrid, Big));
	// Monotone in distance for OK: once rear, stays rear as it gets farther (no oscillation).
	{
		bool bSeenRear = false;
		for (int32 d = 0; d <= 400; d += 5)
		{
			const bool bRear = U::IsRearStatus(EFPSRFieldQuery::OK, d);
			if (bRear) { bSeenRear = true; }
			else { TestFalse(TEXT("OK rear-ness never toggles back to non-rear as dist grows"), bSeenRear); }
		}
		TestTrue(TEXT("some far OK distance is rear"), bSeenRear);
	}

	// --- (D) Drain-dt clamp (gate #4): a freeze can't accrue a burst; a normal tick passes through --------------------
	constexpr float Interval = 0.1f;
	TestEqual(TEXT("negative elapsed clamps to 0"), U::ClampDrainDt(-5.0f, Interval), 0.0f);
	TestEqual(TEXT("a normal one-interval tick passes through"), U::ClampDrainDt(Interval, Interval), Interval);
	TestEqual(TEXT("a sub-interval elapsed passes through"), U::ClampDrainDt(0.05f, Interval), 0.05f);
	// Freeze burst: a huge elapsed is clamped to a small bound INDEPENDENT of how long the freeze was.
	const float FreezeA = U::ClampDrainDt(1000.0f, Interval);
	const float FreezeB = U::ClampDrainDt(5000.0f, Interval);
	TestEqual(TEXT("freeze elapsed clamps to a fixed bound (independent of freeze length)"), FreezeA, FreezeB);
	TestTrue(TEXT("freeze clamp bound is small (a few intervals, not the whole freeze)"), FreezeA <= Interval * 8.0f);
	TestTrue(TEXT("freeze clamp bound >= one interval (a live tick still drains)"), FreezeA >= Interval);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
