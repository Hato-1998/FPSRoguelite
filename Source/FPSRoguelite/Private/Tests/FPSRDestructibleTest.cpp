// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Destructible/FPSRDestructible.h"
#include "Arena/FPSRArenaDestructible.h"

#if WITH_AUTOMATION_TESTS

// Golden net for the door -> destructible generalization (ADR 0010 D7, "파괴 가능 프롭 = 문의 일반화"). Pure/CDO
// checks only — no world, no SpawnActor (mirrors FPSRDirectorSensorTest.cpp's CDO-as-classifier-input style):
//   ComputeDamageStage — the pure threshold-count helper extracted from the old inline AFPSRDoor::HandleHealthChanged.
//   Inheritance        — AFPSRArenaDestructible's CDO IS-A AFPSRDestructible (the generalization actually happened
//                         as real inheritance, not just a parallel class with the same shape). This used to assert it
//                         through AFPSRDoor, the class the base was extracted FROM; the door was retired 2026-08-24
//                         (the suppressor replaces it) so the arena prop is now the subclass that proves the same thing.
//   Empty-by-default   — AFPSRDestructible's own CDO carries no Rewards (a plain destructible = pure obstacle,
//                         no reward, until an author opts in per placed instance).
//   Unbroken-by-default — AFPSRDestructible's own CDO starts IsBroken() == false (F2): ServerReset/ClearBrokenState
//                          only make sense against that resting state. ClearBrokenState itself is NOT tested here —
//                          it is protected (no CDO-level access from outside the class) and its effect
//                          (SetActorEnableCollision/SetActorHiddenInGame) needs a world to observe meaningfully.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSRDestructibleTest, "FPSRoguelite.Destructible.Base",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFPSRDestructibleTest::RunTest(const FString& Parameters)
{
	// --- (1) ComputeDamageStage — descending thresholds {0.75, 0.5, 0.25, 0.05}. -----------------------
	{
		const TArray<float> Thresholds = {0.75f, 0.5f, 0.25f, 0.05f};

		TestEqual(TEXT("Pct 1.0 -> stage 0"), AFPSRDestructible::ComputeDamageStage(Thresholds, 1.0f), 0);
		TestEqual(TEXT("Pct 0.75 -> stage 1 (at threshold)"), AFPSRDestructible::ComputeDamageStage(Thresholds, 0.75f), 1);
		TestEqual(TEXT("Pct 0.751 -> stage 0 (just above threshold)"), AFPSRDestructible::ComputeDamageStage(Thresholds, 0.751f), 0);
		TestEqual(TEXT("Pct 0.5 -> stage 2"), AFPSRDestructible::ComputeDamageStage(Thresholds, 0.5f), 2);
		TestEqual(TEXT("Pct 0.26 -> stage 2"), AFPSRDestructible::ComputeDamageStage(Thresholds, 0.26f), 2);
		TestEqual(TEXT("Pct 0.25 -> stage 3"), AFPSRDestructible::ComputeDamageStage(Thresholds, 0.25f), 3);
		TestEqual(TEXT("Pct 0.04 -> stage 4"), AFPSRDestructible::ComputeDamageStage(Thresholds, 0.04f), 4);
		TestEqual(TEXT("Pct 0.0 -> stage 4"), AFPSRDestructible::ComputeDamageStage(Thresholds, 0.0f), 4);
	}

	// --- (2) Empty threshold array -> always stage 0, regardless of Pct. --------------------------------
	{
		const TArray<float> Empty;
		TestEqual(TEXT("empty thresholds, Pct 1.0 -> stage 0"), AFPSRDestructible::ComputeDamageStage(Empty, 1.0f), 0);
		TestEqual(TEXT("empty thresholds, Pct 0.0 -> stage 0"), AFPSRDestructible::ComputeDamageStage(Empty, 0.0f), 0);
	}

	// --- (3) AFPSRArenaDestructible's CDO IS-A AFPSRDestructible — the generalization is real inheritance, not
	//         merely a parallel class that happens to look the same. ---------------------------------------
	{
		const AFPSRArenaDestructible* ArenaPropCDO = GetDefault<AFPSRArenaDestructible>();
		TestTrue(TEXT("AFPSRArenaDestructible CDO IsA AFPSRDestructible"), ArenaPropCDO->IsA<AFPSRDestructible>());
	}

	// --- (4) AFPSRDestructible's own CDO carries no Rewards by default (pure obstacle, opt-in per instance). ---
	{
		const AFPSRDestructible* DestructibleCDO = GetDefault<AFPSRDestructible>();
		TestEqual(TEXT("AFPSRDestructible CDO Rewards is empty by default"), DestructibleCDO->GetRewards().Num(), 0);
	}

	// --- (5) A fresh AFPSRDestructible CDO starts unbroken (F2). AFPSRArenaActor::SetArenaActive's ServerReset
	//         pass only means anything if "not broken" is this class's own resting state to begin with. ---
	{
		const AFPSRDestructible* DestructibleCDO = GetDefault<AFPSRDestructible>();
		TestFalse(TEXT("AFPSRDestructible CDO IsBroken() == false"), DestructibleCDO->IsBroken());
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
