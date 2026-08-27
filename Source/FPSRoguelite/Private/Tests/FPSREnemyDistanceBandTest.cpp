// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Enemy/FPSREnemyTuning.h"

#if WITH_AUTOMATION_TESTS

// LOD1 (Docs/Specs/LOD1_EnemyDistanceBand.md §12-4): headless proof for FPSREnemyTuning's pure, worldless helpers —
// no UObject/world, exercises exactly what the cosmetic LOD pass and the server batch pass both rely on.
// FPSREnemyPursuitTest.cpp / FPSREnemyDormantPoolTest.cpp's convention (IMPLEMENT_SIMPLE_AUTOMATION_TEST, no
// NewObject, hand-authored inputs).

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSREnemyDistanceBandTest, "FPSRoguelite.Enemy.DistanceBand",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFPSREnemyDistanceBandTest::RunTest(const FString& Parameters)
{
	using namespace FPSREnemyTuning;

	// ---- (1) ClassifyBand boundary values — each of the 3 radii, exactly below / at / above (9 cases, §12-4-①) ----
	{
		const float Epsilon = 100.0f; // safely distinguishable at these magnitudes (S2Sq ~= 3.6e7) in float32

		// S0 boundary (1500^2)
		TestTrue(TEXT("(1) S0 - epsilon -> S0"), ClassifyBand(SignificanceS0RadiusSq - Epsilon) == EFPSRDistanceBand::S0);
		TestTrue(TEXT("(1) S0 exactly -> S0 (<= is inclusive)"), ClassifyBand(SignificanceS0RadiusSq) == EFPSRDistanceBand::S0);
		TestTrue(TEXT("(1) S0 + epsilon -> S1"), ClassifyBand(SignificanceS0RadiusSq + Epsilon) == EFPSRDistanceBand::S1);

		// S1 boundary (3500^2)
		TestTrue(TEXT("(1) S1 - epsilon -> S1"), ClassifyBand(SignificanceS1RadiusSq - Epsilon) == EFPSRDistanceBand::S1);
		TestTrue(TEXT("(1) S1 exactly -> S1 (<= is inclusive)"), ClassifyBand(SignificanceS1RadiusSq) == EFPSRDistanceBand::S1);
		TestTrue(TEXT("(1) S1 + epsilon -> S2"), ClassifyBand(SignificanceS1RadiusSq + Epsilon) == EFPSRDistanceBand::S2);

		// S2 boundary (6000^2)
		TestTrue(TEXT("(1) S2 - epsilon -> S2"), ClassifyBand(SignificanceS2RadiusSq - Epsilon) == EFPSRDistanceBand::S2);
		TestTrue(TEXT("(1) S2 exactly -> S2 (<= is inclusive)"), ClassifyBand(SignificanceS2RadiusSq) == EFPSRDistanceBand::S2);
		TestTrue(TEXT("(1) S2 + epsilon -> S3"), ClassifyBand(SignificanceS2RadiusSq + Epsilon) == EFPSRDistanceBand::S3);
	}

	// ---- (2) ApplyRadiusHysteresis converges to exactly one toggle per boundary crossing, even while the sampled
	//          distance oscillates INSIDE the [On, Off] band where it must NOT flip (§12-4-②) ----
	{
		const float OnSq = FMath::Square(1000.0f);
		const float OffSq = FMath::Square(1200.0f);

		struct FSample
		{
			float DistSq;
			bool bExpectedOn;
			const TCHAR* Desc;
		};
		const FSample Samples[] = {
			{ FMath::Square(2000.0f), false, TEXT("(2) far outside -> stays off") },
			{ FMath::Square(1100.0f), false, TEXT("(2) inside the band while off -> stays off (On threshold not yet crossed)") },
			{ OnSq,                   true,  TEXT("(2) exactly at OnSq -> turns on") },
			{ FMath::Square(900.0f),  true,  TEXT("(2) well inside -> stays on") },
			{ FMath::Square(1100.0f), true,  TEXT("(2) back into the band while on -> stays on (Off threshold not yet crossed)") },
			{ OffSq,                  true,  TEXT("(2) exactly at OffSq -> still on (<=)") },
			{ FMath::Square(1201.0f), false, TEXT("(2) just past OffSq -> turns off") },
			{ FMath::Square(1100.0f), false, TEXT("(2) back into the band while off -> stays off") },
		};

		bool bOn = false;
		int32 ToggleCount = 0;
		for (const FSample& S : Samples)
		{
			const bool bNext = ApplyRadiusHysteresis(bOn, S.DistSq, OnSq, OffSq);
			TestEqual(S.Desc, bNext, S.bExpectedOn);
			if (bNext != bOn)
			{
				++ToggleCount;
			}
			bOn = bNext;
		}
		TestEqual(TEXT("(2) exactly 2 toggles across the whole walk (one on-crossing, one off-crossing) despite ")
			TEXT("repeated in-band oscillation"), ToggleCount, 2);
	}

	// ---- (3) Radius value lock — the three significance constants match the historical literals (§12-4-③) ----
	{
		TestEqual(TEXT("(3) SignificanceS0RadiusSq == 1500^2"), SignificanceS0RadiusSq, 1500.0f * 1500.0f);
		TestEqual(TEXT("(3) SignificanceS1RadiusSq == 3500^2"), SignificanceS1RadiusSq, 3500.0f * 3500.0f);
		TestEqual(TEXT("(3) SignificanceS2RadiusSq == 6000^2"), SignificanceS2RadiusSq, 6000.0f * 6000.0f);
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
