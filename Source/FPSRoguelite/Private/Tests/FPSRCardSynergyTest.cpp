// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Card/FPSRCardSubsystem.h"
#include "Card/FPSRCardDataAsset.h"

#if WITH_AUTOMATION_TESTS

// CRIT2 (2026-09-06, P2-2 머지 게이트로 확장): headless net for the pure/static card-draw formulas a content edit
// must not silently break:
//  - UFPSRCardSubsystem::ComputeSynergyMultiplier — the ONE place build synergy multiplies a mission/unlock-pool
//    draw weight (a PIE-tuned SynergyBonusPerCard/MaxStacks edit doesn't break this test; a regression in the
//    formula itself still does).
//  - UFPSRCardSubsystem::WeightedSampleWithoutReplacement — the §12-6 그룹 비중 보존 2단 추출: Weights ==
//    BaselineWeights 이면 종래 균등 추출과 등가여야 하고, 그룹의 몫은 그룹 내부 시너지 유무와 무관하게 보존돼야 한다.
// Both are public static with no world/actor/subsystem state (repo precedent: AFPSRPlayerState::IsTopologyAckSatisfied,
// FPSRCombat::RollCrit).

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

	// --- (G) §12-6 그룹 비중 보존 2단 추출의 통계적 등가성 (P2-2 머지 게이트) ---------------------------------------
	// WeightedSampleWithoutReplacement 는 public static 이라(§6 G1 P2-7) 월드 없이 여기서 바로 부를 수 있다.
	{
		using FDraw = FFPSRCardDraw;
		constexpr int32 NumA = 6;                    // 그룹 0 (예: 새 무기 후보)
		constexpr int32 NumB = 5;                    // 그룹 1 (예: 기능 카드 후보)
		constexpr int32 NumCandidates = NumA + NumB; // 11
		constexpr int32 DrawCount = 3;
		constexpr int32 NumTrials = 10000;

		// 카드 포인터 자체가 후보 식별자 — WeightedSampleWithoutReplacement 는 Card 를 역참조하지 않고 FFPSRCardDraw
		// struct 만 이동시키므로(§6 헬퍼 계약) 진짜 카드 저작 없이 빈 UFPSRCardDataAsset 11개로 충분하다
		// (FPSRDataEditorRoundTripTest.cpp 의 NewObject<UFPSRCardDataAsset>() 전례).
		TArray<UFPSRCardDataAsset*> CardObjects;
		CardObjects.Reserve(NumCandidates);
		for (int32 i = 0; i < NumCandidates; ++i)
		{
			CardObjects.Add(NewObject<UFPSRCardDataAsset>());
		}

		// 매 시행마다 새로 채운다 — WeightedSampleWithoutReplacement 는 선택/제외된 원소를 In-Out 배열에서 지우므로
		// (§6 헬퍼 계약) 같은 배열을 재사용하면 두 번째 시행부터 후보가 고갈된다.
		auto BuildCandidates = [&](TArray<FDraw>& OutCandidates, TArray<float>& OutWeights, TArray<float>& OutBaseline, TArray<int32>& OutGroupIds)
		{
			OutCandidates.Reset(NumCandidates);
			OutWeights.Reset(NumCandidates);
			OutBaseline.Reset(NumCandidates);
			OutGroupIds.Reset(NumCandidates);
			for (int32 i = 0; i < NumCandidates; ++i)
			{
				FDraw Entry;
				Entry.Card = CardObjects[i];
				OutCandidates.Add(Entry);
				OutWeights.Add(1.0f);
				OutBaseline.Add(1.0f); // BaselineWeights = 시너지 제외 원 가중치(§6) — 그룹 몫의 입력, 시너지와 무관
				OutGroupIds.Add(i < NumA ? 0 : 1);
			}
		};
		auto NoExclusion = [](const FDraw&, const FDraw&) -> bool { return false; }; // 배제 술어 없음(자기 제거만, §6)

		// ⓐ 균등 회귀: 전부 Weight=1(Weights == BaselineWeights, 즉 시너지 없음)일 때 §6 헤더 주석의 "모든 시너지가
		// 1 이고 모든 Card->Weight 가 같을 때 종래 균등 추출과 분포가 같다"는 주장을 검증한다 — 단순 무작위
		// 비복원추출의 후보별 포함확률은 정확히 Count/N, 이항 분산 근사 σ=sqrt(p(1-p)/M)(지시된 공식).
		TArray<int32> InclusionCount;
		InclusionCount.SetNumZeroed(NumCandidates);
		for (int32 Trial = 0; Trial < NumTrials; ++Trial)
		{
			TArray<FDraw> Candidates;
			TArray<float> Weights;
			TArray<float> Baseline;
			TArray<int32> GroupIds;
			BuildCandidates(Candidates, Weights, Baseline, GroupIds);

			TArray<FDraw> Picked;
			FSubsystem::WeightedSampleWithoutReplacement(Candidates, Weights, Baseline, GroupIds, DrawCount, NoExclusion, Picked);
			for (const FDraw& Pick : Picked)
			{
				const int32 Idx = CardObjects.IndexOfByKey(Pick.Card.Get());
				if (InclusionCount.IsValidIndex(Idx))
				{
					++InclusionCount[Idx];
				}
			}
		}

		const double ExpectedP = static_cast<double>(DrawCount) / static_cast<double>(NumCandidates); // 3/11
		const double UniformSigma = FMath::Sqrt(ExpectedP * (1.0 - ExpectedP) / static_cast<double>(NumTrials));
		// 결정성: 11개 후보 전부를 개별 단언하므로(다중비교) CI 에서 흔들리지 않도록 ±4σ 로 넉넉히 잡는다(지시사항).
		const double UniformTolerance = 4.0 * UniformSigma;
		for (int32 i = 0; i < NumCandidates; ++i)
		{
			const double Observed = static_cast<double>(InclusionCount[i]) / static_cast<double>(NumTrials);
			TestEqual(FString::Printf(TEXT("uniform draw: candidate %d inclusion freq converges to Count/N (tolerance ±4σ=%.4f)"), i, UniformTolerance),
				Observed, ExpectedP, UniformTolerance);
		}

		// ⓑ 그룹 몫 보존(사용자 결정 C): B 그룹 일부에만 시너지(Weights)를 걸어도 1단계는 BaselineWeights 로만
		// 그룹을 고르므로, A 그룹 6개의 매 추출당 기대 픽 수는 시너지 유무와 무관하게 항상 같다 — 그룹 레이블
		// 시퀀스가 정확히 초기하분포 Hypergeometric(N=11,K=6,n=3)을 따르고, 그 분포는 그룹 내부에서 어떤 개별
		// 원소가 뽑히는지에 의존하지 않기 때문이다. 시너지 없음/있음 두 시행을 각각 몬테카를로로 돌려 A 그룹 총
		// 포함 수의 평균을 직접 비교한다.
		auto RunTotalAPicked = [&](bool bApplySynergy) -> int32
		{
			int32 TotalAPicked = 0;
			for (int32 Trial = 0; Trial < NumTrials; ++Trial)
			{
				TArray<FDraw> Candidates;
				TArray<float> Weights;
				TArray<float> Baseline;
				TArray<int32> GroupIds;
				BuildCandidates(Candidates, Weights, Baseline, GroupIds);
				if (bApplySynergy)
				{
					// B 그룹의 처음 2장에만 3배 시너지 — Weights(그룹 내부 재분배)만 바뀌고 BaselineWeights(그룹
					// 몫의 입력)는 그대로다(§6 계약).
					Weights[NumA + 0] = 3.0f;
					Weights[NumA + 1] = 3.0f;
				}
				TArray<FDraw> Picked;
				FSubsystem::WeightedSampleWithoutReplacement(Candidates, Weights, Baseline, GroupIds, DrawCount, NoExclusion, Picked);
				for (const FDraw& Pick : Picked)
				{
					const int32 Idx = CardObjects.IndexOfByKey(Pick.Card.Get());
					if (Idx >= 0 && Idx < NumA)
					{
						++TotalAPicked;
					}
				}
			}
			return TotalAPicked;
		};

		const double MeanA_NoSynergy = static_cast<double>(RunTotalAPicked(false)) / static_cast<double>(NumTrials);
		const double MeanA_WithSynergy = static_cast<double>(RunTotalAPicked(true)) / static_cast<double>(NumTrials);

		// A 그룹 픽 수는 정확히 Hypergeometric(모집단=11, 성공(A)=6, 추출=3) —
		// Var = DrawsPerTrial * (GroupASize/PopulationSize) * ((PopulationSize-GroupASize)/PopulationSize) *
		//       ((PopulationSize-DrawsPerTrial)/(PopulationSize-1))  (유한모집단 보정 포함).
		// 독립인 두 평균(각 M 회)의 차의 표준편차 = sqrt(2*Var/M). 결정성: ±4σ(지시사항).
		const double PopulationSize = static_cast<double>(NumCandidates);
		const double GroupASize = static_cast<double>(NumA);
		const double DrawsPerTrial = static_cast<double>(DrawCount);
		const double VarPerDraw = DrawsPerTrial * (GroupASize / PopulationSize) * ((PopulationSize - GroupASize) / PopulationSize)
			* ((PopulationSize - DrawsPerTrial) / (PopulationSize - 1.0));
		const double GroupShareSigma = FMath::Sqrt(2.0 * VarPerDraw / static_cast<double>(NumTrials));
		const double GroupShareTolerance = 4.0 * GroupShareSigma;

		TestEqual(FString::Printf(TEXT("group share preserved: mean A picks/draw with vs without B-group synergy (tolerance ±4σ=%.4f)"), GroupShareTolerance),
			MeanA_WithSynergy, MeanA_NoSynergy, GroupShareTolerance);
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
