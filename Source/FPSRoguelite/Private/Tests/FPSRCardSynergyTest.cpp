// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Card/FPSRCardSubsystem.h"

#if WITH_AUTOMATION_TESTS

// CRIT2 (2026-09-06): headless net for the ONE place build synergy multiplies a mission/unlock-pool draw weight —
// the pure formula UFPSRCardSubsystem::ComputeSynergyMultiplier. No world/actor/subsystem state (repo precedent:
// AFPSRPlayerState::IsTopologyAckSatisfied, FPSRCombat::RollCrit), so a PIE-tuned SynergyBonusPerCard/MaxStacks
// content edit doesn't break this test while a regression in the FORMULA itself still does.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSRCardSynergyTest, "FPSRoguelite.Card.Synergy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFPSRCardSynergyTest::RunTest(const FString& Parameters)
{
	using FSubsystem = UFPSRCardSubsystem;

	// --- (A) 카운트 0 (태그는 있지만 원장에 안 잡힘) -> 1.0 (시너지 없음 = 현행 거동) --------------------------------
	{
		TMap<FName, int32> Counts;
		TestEqual(TEXT("tag count 0 -> multiplier 1.0"),
			FSubsystem::ComputeSynergyMultiplier({ TEXT("crit") }, Counts, 0.5f, 4), 1.0f);
	}

	// --- (B) 카운트 2, 상한(4) 미도달 -> 1 + 0.5*2 = 2.0 -------------------------------------------------------------
	{
		TMap<FName, int32> Counts;
		Counts.Add(TEXT("crit"), 2);
		TestEqual(TEXT("count 2, bonus 0.5 -> 1 + 0.5*2 = 2.0"),
			FSubsystem::ComputeSynergyMultiplier({ TEXT("crit") }, Counts, 0.5f, 4), 2.0f);
	}

	// --- (C) 카운트 10, 상한 4 -> min(10,4)=4로 클램프 -> 1 + 0.5*4 = 3.0 ------------------------------------------
	{
		TMap<FName, int32> Counts;
		Counts.Add(TEXT("crit"), 10);
		TestEqual(TEXT("count 10 clamped to MaxStacks 4 -> 1 + 0.5*4 = 3.0"),
			FSubsystem::ComputeSynergyMultiplier({ TEXT("crit") }, Counts, 0.5f, 4), 3.0f);
	}

	// --- (D) 다중 태그 -> 합산이 아니라 가장 많이 투자한 태그 하나의 카운트만 본다 -------------------------------------
	{
		TMap<FName, int32> Counts;
		Counts.Add(TEXT("crit"), 1);
		Counts.Add(TEXT("aoe"), 3);
		TestEqual(TEXT("multi-tag card uses the BEST tag's count (3), not the sum (1+3)"),
			FSubsystem::ComputeSynergyMultiplier({ TEXT("crit"), TEXT("aoe") }, Counts, 0.5f, 4), 2.5f); // 1 + 0.5*3
	}

	// --- (E) BonusPerCard == 0 -> 카운트가 커도 항상 1.0(시너지 기능 자체가 꺼진 상태) --------------------------------
	{
		TMap<FName, int32> Counts;
		Counts.Add(TEXT("crit"), 4);
		TestEqual(TEXT("BonusPerCard 0 -> always 1.0 regardless of count"),
			FSubsystem::ComputeSynergyMultiplier({ TEXT("crit") }, Counts, 0.0f, 4), 1.0f);
	}

	// --- (F) 빈 태그 배열 -> 1.0 (태그를 안 단 카드는 원장에 뭐가 있든 시너지 축에 안 걸린다) -------------------------
	{
		TMap<FName, int32> Counts;
		Counts.Add(TEXT("crit"), 4);
		TestEqual(TEXT("empty CardTags -> 1.0 (nothing to look up)"),
			FSubsystem::ComputeSynergyMultiplier({}, Counts, 0.5f, 4), 1.0f);
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
