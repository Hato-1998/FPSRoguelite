// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Enemy/FPSREnemyAllocator.h"

#if WITH_AUTOMATION_TESTS

// Headless invariant net for the map-aware budget apportionment (Codex consult 2026-07-06). Pure math, no world.
// Asserts the guarantees the adversarial review flagged as load-bearing: the per-map targets sum to EXACTLY the global
// target (no rounding drift that could push the host budget over cap), empty maps get 0, and a 2+ front out-weights solos.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSRAllocatorUnitTest, "FPSRoguelite.Allocator.Unit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFPSRAllocatorUnitTest::RunTest(const FString& Parameters)
{
	const int32 Bonus = 1;

	auto Sum = [](const TArray<int32>& A) { int32 S = 0; for (int32 V : A) { S += V; } return S; };
	auto RunCase = [&](const TArray<int32>& Counts, int32 Target, TArray<int32>& Out)
	{
		FPSREnemyAllocator::Apportion(Counts, Target, Bonus, Out);
		// Universal invariants: sized to input, each >= 0, empty map -> 0.
		TestEqual(TEXT("Out sized to input"), Out.Num(), Counts.Num());
		for (int32 i = 0; i < Out.Num(); ++i)
		{
			TestTrue(TEXT("target >= 0"), Out[i] >= 0);
			if (Counts[i] == 0) { TestEqual(TEXT("empty map target is 0"), Out[i], 0); }
		}
	};

	// (1) Single occupied map gets the whole target.
	{
		TArray<int32> Out; RunCase({ 4 }, 192, Out);
		TestEqual(TEXT("single map == target"), Out[0], 192);
	}

	// (2) 2/2 even split sums to target, both equal.
	{
		TArray<int32> Out; RunCase({ 2, 2 }, 100, Out);
		TestEqual(TEXT("2/2 sums to target"), Sum(Out), 100);
		TestEqual(TEXT("2/2 equal"), Out[0], Out[1]);
	}

	// (3) 3/1: aggregate of the 3p map exceeds the solo, and per-capita is NOT inverted (2+ front denser, not starved).
	{
		TArray<int32> Out; RunCase({ 3, 1 }, 100, Out);
		TestEqual(TEXT("3/1 sums to target"), Sum(Out), 100);
		TestTrue(TEXT("3p aggregate > solo aggregate"), Out[0] > Out[1]);
		TestTrue(TEXT("3p per-capita >= solo per-capita (no inversion)"),
			(static_cast<float>(Out[0]) / 3.0f) >= (static_cast<float>(Out[1]) / 1.0f));
	}

	// (4) Full spread 1/1/1/1 sums to target (Hamilton distributes the remainder exactly).
	{
		TArray<int32> Out; RunCase({ 1, 1, 1, 1 }, 100, Out);
		TestEqual(TEXT("1/1/1/1 sums to target"), Sum(Out), 100);
	}

	// (5) 4/0: the empty map gets 0, all budget to the occupied map.
	{
		TArray<int32> Out; RunCase({ 4, 0 }, 100, Out);
		TestEqual(TEXT("4/0 sums to target"), Sum(Out), 100);
		TestEqual(TEXT("4/0 empty map 0"), Out[1], 0);
		TestEqual(TEXT("4/0 occupied gets all"), Out[0], 100);
	}

	// (6) Rounding: 3 equal maps, target not divisible -> largest-remainder still sums exactly.
	{
		TArray<int32> Out; RunCase({ 1, 1, 1 }, 100, Out);
		TestEqual(TEXT("odd split sums exactly to target"), Sum(Out), 100);
		// Each is floor(100/3)=33 with one map getting the +1 leftover.
		for (int32 V : Out) { TestTrue(TEXT("each near even share"), V == 33 || V == 34); }
	}

	// (7) Zero target -> all zero.
	{
		TArray<int32> Out; RunCase({ 2, 3 }, 0, Out);
		TestEqual(TEXT("zero target -> zero sum"), Sum(Out), 0);
	}

	// (8) No occupied maps -> all zero (no divide-by-zero).
	{
		TArray<int32> Out; RunCase({ 0, 0 }, 100, Out);
		TestEqual(TEXT("no occupants -> zero sum"), Sum(Out), 0);
	}

	// (9) NEVER over target: a batch of odd splits must never exceed the requested total (host-cap safety).
	{
		const int32 Splits[][4] = { {2,2,0,0}, {3,1,0,0}, {1,1,1,1}, {4,3,2,1}, {2,0,0,0} };
		for (const auto& S : Splits)
		{
			TArray<int32> Out;
			FPSREnemyAllocator::Apportion({ S[0], S[1], S[2], S[3] }, 137, Bonus, Out);
			TestTrue(TEXT("sum never exceeds target"), Sum(Out) <= 137);
			// And exactly hits it whenever at least one map is occupied.
			if (S[0] + S[1] + S[2] + S[3] > 0) { TestEqual(TEXT("sum hits target when occupied"), Sum(Out), 137); }
		}
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
