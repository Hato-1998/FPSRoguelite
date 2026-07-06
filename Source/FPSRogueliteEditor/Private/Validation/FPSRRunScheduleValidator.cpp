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
