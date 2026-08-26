// Copyright Epic Games, Inc. All Rights Reserved.

#include "Validation/FPSRRunScheduleValidator.h"
#include "Run/FPSRRunScheduleDataAsset.h"
#include "Run/Mission/FPSRMissionDataAsset.h"
#include "Misc/DataValidation.h"
#include "Math/NumericLimits.h"

#define LOCTEXT_NAMESPACE "FPSRRunScheduleValidator"

bool UFPSRRunScheduleValidator::CanValidateAsset_Implementation(const FAssetData& InAssetData, UObject* InAsset, FDataValidationContext& InContext) const
{
	return InAsset != nullptr && InAsset->IsA<UFPSRRunScheduleDataAsset>();
}

EDataValidationResult UFPSRRunScheduleValidator::ValidateLoadedAsset_Implementation(const FAssetData& InAssetData, UObject* InAsset, FDataValidationContext& Context)
{
	const UFPSRRunScheduleDataAsset* Schedule = Cast<UFPSRRunScheduleDataAsset>(InAsset);
	if (Schedule == nullptr)
	{
		AssetPasses(InAsset);
		return EDataValidationResult::Valid;
	}

	EDataValidationResult Result = EDataValidationResult::Valid;

	// --- Mission windows: MinTime <= MaxTime, and every pool entry must be a valid (non-null) mission ref. ---
	for (int32 Index = 0; Index < Schedule->MissionWindows.Num(); ++Index)
	{
		const FFPSRMissionWindow& Window = Schedule->MissionWindows[Index];
		if (Window.MinTime > Window.MaxTime)
		{
			Context.AddError(FText::Format(
				LOCTEXT("WindowMinAfterMax", "MissionWindows[{0}]: MinTime ({1}) is greater than MaxTime ({2}) — the window can never roll a valid trigger time."),
				FText::AsNumber(Index), FText::AsNumber(Window.MinTime), FText::AsNumber(Window.MaxTime)));
			Result = EDataValidationResult::Invalid;
		}

		if (Window.MissionPool.Num() == 0)
		{
			Context.AddWarning(FText::Format(
				LOCTEXT("WindowEmptyPool", "MissionWindows[{0}] has an empty MissionPool — this window is a no-op (Game.MD §2-8)."),
				FText::AsNumber(Index)));
		}

		for (int32 MissionIndex = 0; MissionIndex < Window.MissionPool.Num(); ++MissionIndex)
		{
			if (Window.MissionPool[MissionIndex] == nullptr)
			{
				Context.AddError(FText::Format(
					LOCTEXT("WindowNullMission", "MissionWindows[{0}].MissionPool[{1}] is null — if this window rolls that slot, nothing spawns."),
					FText::AsNumber(Index), FText::AsNumber(MissionIndex)));
				Result = EDataValidationResult::Invalid;
			}
		}
	}

	// --- Boss timing: the run has exactly one boss transition, gated by BossTime + BossDefinition + EnemyRoster. ---
	if (Schedule->BossTime <= 0.0f)
	{
		Context.AddError(LOCTEXT("BossTimeNotPositive", "BossTime <= 0 — the Combat -> Boss transition would fire immediately (or never, depending on the director's clamping). Set BossTime > 0."));
		Result = EDataValidationResult::Invalid;
	}

	// WARNING (not error): null BossDefinition / EnemyRoster are DOCUMENTED runtime fallbacks (director spawns the C++
	// AFPSRBossBase placeholder; swarm uses the spawner's single configured EnemyClass). An intentional placeholder /
	// boss-less / single-archetype test schedule is valid, so these must NOT fail the anchored CI commandlet — only
	// flag the likely-forgotten assignment. (Codex merge-gate: don't gate builds on documented fallbacks.)
	if (Schedule->BossDefinition == nullptr)
	{
		Context.AddWarning(LOCTEXT("NoBossDefinition", "BossDefinition is null — the director will fall back to the placeholder AFPSRBossBase. Assign the run's boss unless this is intentionally a boss-less / placeholder schedule."));
	}

	if (Schedule->EnemyRoster == nullptr)
	{
		Context.AddWarning(LOCTEXT("NoEnemyRoster", "EnemyRoster is null — the swarm will fall back to the spawner's single configured EnemyClass (no archetype mix). Assign a roster unless a single-archetype swarm is intended."));
	}

	// --- Alive-count anchors: strictly ascending Level, no duplicate Level keys; a non-positive Count is a soft bug
	//     (that anchor targets an empty swarm) rather than a hard error. ---
	int32 PreviousLevel = TNumericLimits<int32>::Min();
	bool bHasPreviousLevel = false;
	for (int32 Index = 0; Index < Schedule->AliveCountByLevel.Num(); ++Index)
	{
		const FFPSRAliveCountAnchor& Anchor = Schedule->AliveCountByLevel[Index];
		if (bHasPreviousLevel)
		{
			if (Anchor.Level == PreviousLevel)
			{
				Context.AddError(FText::Format(
					LOCTEXT("DuplicateAnchorLevel", "AliveCountByLevel[{0}]: duplicate Level {1} — anchors must be strictly ascending, one entry per level."),
					FText::AsNumber(Index), FText::AsNumber(Anchor.Level)));
				Result = EDataValidationResult::Invalid;
			}
			else if (Anchor.Level < PreviousLevel)
			{
				Context.AddError(FText::Format(
					LOCTEXT("AnchorNotAscending", "AliveCountByLevel[{0}]: Level {1} is out of order (previous anchor was Level {2}) — author anchors in ascending Level order."),
					FText::AsNumber(Index), FText::AsNumber(Anchor.Level), FText::AsNumber(PreviousLevel)));
				Result = EDataValidationResult::Invalid;
			}
		}
		PreviousLevel = Anchor.Level;
		bHasPreviousLevel = true;

		if (Anchor.Count <= 0)
		{
			Context.AddWarning(FText::Format(
				LOCTEXT("AnchorZeroCount", "AliveCountByLevel[{0}] (Level {1}) has Count <= 0 — the swarm targets zero alive enemies at/after this level."),
				FText::AsNumber(Index), FText::AsNumber(Anchor.Level)));
		}
	}

	// --- 난이도 축 (Stage Difficulty, 신설 2026-08-26, ADR 0010 D6 비용 축): StageIndex 중복/역순/음수 규약은
	//     위 AliveCountByLevel 루프와 동일하다 — 같은 판정을 StageDifficulty 에도 적용한다. ---
	// 🔴 첫 앵커가 StageIndex 0 이 아니면서 항등이 아니면, 그 아래 스테이지 전부가 이 값으로 돈다 — flat clamp
	//    규약(EvalStageAt: "첫 앵커 at-or-below 는 첫 앵커 값")의 직접적인 귀결이고, 저작자가 가장 놓치기 쉬운
	//    지점이다. 예: 첫 앵커가 (3, +8, x1.15, x1.6) 이면 스테이지 0 이 이미 x1.6 내구도로 시작하고 스테이지
	//    0->3 의 억제기 파괴 3회가 난이도를 전혀 올리지 않는다("억제기 파괴가 난이도를 올린다"는 이 축의 전제가
	//    앞 세 스테이지에서 죽는다). Error 가 아니라 Warning 인 이유 = 의도적으로 그렇게 저작할 수도 있다
	//    (초반부터 빡센 스케줄). 판정은 저작자에게 남기고 사실만 짚는다.
	if (Schedule->StageDifficulty.Num() > 0)
	{
		const FFPSRStageDifficultyAnchor& First = Schedule->StageDifficulty[0];
		const bool bIsIdentity = (First.AliveCountBonus == 0)
			&& FMath::IsNearlyEqual(First.AliveCountMultiplier, 1.0f)
			&& FMath::IsNearlyEqual(First.InhibitorDurabilityMultiplier, 1.0f);
		if (First.StageIndex > 0 && !bIsIdentity)
		{
			Context.AddWarning(FText::Format(
				LOCTEXT("StageFirstAnchorNotIdentity", "StageDifficulty[0] is at StageIndex {0} with non-identity values (+{1}, x{2} alive, x{3} inhibitor) — every stage from 0 to {4} inherits them (flat clamp below the first anchor), so the run STARTS at this difficulty and the first {0} suppressor breaks raise nothing. Lead with an identity anchor at StageIndex 0 (+0, x1.0, x1.0) unless that head start is intended."),
				FText::AsNumber(First.StageIndex), FText::AsNumber(First.AliveCountBonus),
				FText::AsNumber(First.AliveCountMultiplier), FText::AsNumber(First.InhibitorDurabilityMultiplier),
				FText::AsNumber(First.StageIndex - 1)));
		}
	}

	int32 PreviousStageIndex = TNumericLimits<int32>::Min();
	bool bHasPreviousStageIndex = false;
	for (int32 Index = 0; Index < Schedule->StageDifficulty.Num(); ++Index)
	{
		const FFPSRStageDifficultyAnchor& Anchor = Schedule->StageDifficulty[Index];

		if (Anchor.StageIndex < 0)
		{
			Context.AddError(FText::Format(
				LOCTEXT("StageAnchorNegative", "StageDifficulty[{0}]: StageIndex {1} is negative — stages are numbered from 0."),
				FText::AsNumber(Index), FText::AsNumber(Anchor.StageIndex)));
			Result = EDataValidationResult::Invalid;
		}

		if (bHasPreviousStageIndex)
		{
			if (Anchor.StageIndex == PreviousStageIndex)
			{
				Context.AddError(FText::Format(
					LOCTEXT("StageAnchorDuplicate", "StageDifficulty[{0}]: duplicate StageIndex {1} — anchors must be strictly ascending, one entry per stage."),
					FText::AsNumber(Index), FText::AsNumber(Anchor.StageIndex)));
				Result = EDataValidationResult::Invalid;
			}
			else if (Anchor.StageIndex < PreviousStageIndex)
			{
				Context.AddError(FText::Format(
					LOCTEXT("StageAnchorNotAscending", "StageDifficulty[{0}]: StageIndex {1} is out of order (previous anchor was StageIndex {2}) — author anchors in ascending StageIndex order."),
					FText::AsNumber(Index), FText::AsNumber(Anchor.StageIndex), FText::AsNumber(PreviousStageIndex)));
				Result = EDataValidationResult::Invalid;
			}
		}
		PreviousStageIndex = Anchor.StageIndex;
		bHasPreviousStageIndex = true;

		if (Anchor.AliveCountMultiplier <= 0.0f)
		{
			Context.AddError(FText::Format(
				LOCTEXT("StageAnchorAliveMultNotPositive", "StageDifficulty[{0}] (StageIndex {1}): AliveCountMultiplier <= 0 — the swarm target would be zero or negative from this stage on."),
				FText::AsNumber(Index), FText::AsNumber(Anchor.StageIndex)));
			Result = EDataValidationResult::Invalid;
		}
		if (Anchor.InhibitorDurabilityMultiplier <= 0.0f)
		{
			Context.AddError(FText::Format(
				LOCTEXT("StageAnchorInhibMultNotPositive", "StageDifficulty[{0}] (StageIndex {1}): InhibitorDurabilityMultiplier <= 0 — the suppressor would have zero or negative health (free break) from this stage on."),
				FText::AsNumber(Index), FText::AsNumber(Anchor.StageIndex)));
			Result = EDataValidationResult::Invalid;
		}

		// 🔴 포화 경고는 마지막 앵커만이 아니라 전 앵커에 적용한다 — Bonus/Multiplier 가 단조 증가라는 보장이
		// 없어 중간 앵커가 마지막 앵커보다 더 포화할 수 있다(같은 루프 비용). "최고레벨 Count" = 저작된 마지막
		// AliveCountByLevel 앵커의 Count — 레벨 곡선이 그 이상에서 flat 하게 유지하는 상한이므로, 장기 런에서
		// 이 스테이지 배수가 실제로 만날 수 있는 최악의 입력이다. AliveCountByLevel 이 비어 있으면(레거시 시간
		// 램프) 이 개념 자체가 없으므로 검사하지 않는다.
		if (Schedule->AliveCountByLevel.Num() > 0)
		{
			const int32 HighestLevelCount = Schedule->AliveCountByLevel.Last().Count;
			const float SaturatedTarget = (static_cast<float>(HighestLevelCount) + static_cast<float>(Anchor.AliveCountBonus)) * Anchor.AliveCountMultiplier;
			if (SaturatedTarget > static_cast<float>(Schedule->MaxAliveCount))
			{
				Context.AddWarning(FText::Format(
					LOCTEXT("StageAnchorSaturates", "StageDifficulty[{0}] (StageIndex {1}): (highest-level Count {2} + AliveCountBonus {3}) x AliveCountMultiplier {4} = {5}, which exceeds MaxAliveCount ({6}) — the swarm target clamps at this stage. Note MaxAliveCount is already a dead value above the spawn subsystem's effective ~192 alive cap (GlobalAliveCap 200 - SeedReserve 8) — the real clamp may bite even lower than {6}."),
					FText::AsNumber(Index), FText::AsNumber(Anchor.StageIndex), FText::AsNumber(HighestLevelCount),
					FText::AsNumber(Anchor.AliveCountBonus), FText::AsNumber(Anchor.AliveCountMultiplier),
					FText::AsNumber(SaturatedTarget), FText::AsNumber(Schedule->MaxAliveCount)));
			}

			// 바닥 포화 — 천장 검사의 대칭. 음수 AliveCountBonus 가 저레벨 구간의 목표를 0 으로 깎으면 스웜이
			// 통째로 사라지는데, 위 천장 검사만으로는 통과한다. 같은 파일의 AnchorZeroCount 경고("Count <= 0 —
			// targets an empty swarm")와 정확히 같은 실수를 이 축에서만 놓치지 않도록 짝을 맞춘다. "최저레벨
			// Count" = 첫 AliveCountByLevel 앵커의 Count(그 아래에서 레벨 곡선이 flat 유지하는 하한).
			const int32 LowestLevelCount = Schedule->AliveCountByLevel[0].Count;
			const float FlooredTarget = (static_cast<float>(LowestLevelCount) + static_cast<float>(Anchor.AliveCountBonus)) * Anchor.AliveCountMultiplier;
			if (FlooredTarget <= 0.0f)
			{
				Context.AddWarning(FText::Format(
					LOCTEXT("StageAnchorFloors", "StageDifficulty[{0}] (StageIndex {1}): (lowest-level Count {2} + AliveCountBonus {3}) x AliveCountMultiplier {4} = {5} — the swarm target clamps to ZERO for low-level parties at this stage (no enemies spawn at all)."),
					FText::AsNumber(Index), FText::AsNumber(Anchor.StageIndex), FText::AsNumber(LowestLevelCount),
					FText::AsNumber(Anchor.AliveCountBonus), FText::AsNumber(Anchor.AliveCountMultiplier),
					FText::AsNumber(FlooredTarget)));
			}
		}
	}

	// --- Inhibitor durability fields: a non-positive base or per-size multiplier silently zeroes/negates a
	//     suppressor's effective health (a "free" stage transition, or an unbreakable one at negative health). ---
	if (Schedule->InhibitorBaseDurability <= 0.0f)
	{
		Context.AddError(LOCTEXT("InhibitorBaseDurabilityNotPositive", "InhibitorBaseDurability <= 0 — every suppressor would be authored with zero or negative health."));
		Result = EDataValidationResult::Invalid;
	}

	if (Schedule->InhibitorDurabilityByPartySize.Num() == 0)
	{
		Context.AddWarning(LOCTEXT("InhibitorByPartySizeEmpty", "InhibitorDurabilityByPartySize is empty — falls back to a x1.0 multiplier regardless of party size (documented fallback, EvalPartySizeMultiplier)."));
	}
	for (int32 Index = 0; Index < Schedule->InhibitorDurabilityByPartySize.Num(); ++Index)
	{
		if (Schedule->InhibitorDurabilityByPartySize[Index] <= 0.0f)
		{
			Context.AddError(FText::Format(
				LOCTEXT("InhibitorByPartySizeNotPositive", "InhibitorDurabilityByPartySize[{0}] <= 0 — a party of size {1} would face a suppressor with zero or negative health."),
				FText::AsNumber(Index), FText::AsNumber(Index + 1)));
			Result = EDataValidationResult::Invalid;
		}
	}

	// --- Spawn-rate fields: a non-positive value here silently stalls or empties the swarm director. ---
	if (Schedule->MaxAliveCount <= 0)
	{
		Context.AddError(LOCTEXT("MaxAliveCountNotPositive", "MaxAliveCount <= 0 — the swarm's hard cap would allow no enemies alive at all."));
		Result = EDataValidationResult::Invalid;
	}
	if (Schedule->MaxSpawnPerTick <= 0)
	{
		Context.AddError(LOCTEXT("MaxSpawnPerTickNotPositive", "MaxSpawnPerTick <= 0 — the director would never spawn a batch."));
		Result = EDataValidationResult::Invalid;
	}
	if (Schedule->SpawnIntervalSeconds <= 0.0f)
	{
		Context.AddError(LOCTEXT("SpawnIntervalNotPositive", "SpawnIntervalSeconds <= 0 — the director tick would spin at an invalid (zero/negative) interval."));
		Result = EDataValidationResult::Invalid;
	}

	// --- BossTime landing inside a mission window's range is only a possible overlap (the window rolls a random
	//     time in [MinTime,MaxTime] — it may or may not actually collide with the boss at runtime), so this is a
	//     warning, not an error. ---
	for (int32 Index = 0; Index < Schedule->MissionWindows.Num(); ++Index)
	{
		const FFPSRMissionWindow& Window = Schedule->MissionWindows[Index];
		if (Schedule->BossTime >= Window.MinTime && Schedule->BossTime <= Window.MaxTime)
		{
			Context.AddWarning(FText::Format(
				LOCTEXT("BossTimeInsideWindow", "BossTime ({0}) falls inside MissionWindows[{1}]'s range [{2}, {3}] — the mission may roll a trigger time at or after the boss appears."),
				FText::AsNumber(Schedule->BossTime), FText::AsNumber(Index), FText::AsNumber(Window.MinTime), FText::AsNumber(Window.MaxTime)));
		}
	}

	if (Result == EDataValidationResult::Valid)
	{
		AssetPasses(InAsset);
	}
	return Result;
}

#undef LOCTEXT_NAMESPACE
