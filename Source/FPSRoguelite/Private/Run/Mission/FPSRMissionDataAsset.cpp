// Copyright Epic Games, Inc. All Rights Reserved.

#include "Run/Mission/FPSRMissionDataAsset.h"
#include "Run/Mission/FPSRMissionActor.h"
#include "Run/Mission/FPSRMissionTuning.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#include "GameplayTagsManager.h"

#define LOCTEXT_NAMESPACE "FPSRMissionDataAsset"

EDataValidationResult UFPSRMissionDataAsset::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	if (!MissionClass)
	{
		Context.AddError(LOCTEXT("NoMissionClass", "Mission has no MissionClass — no actor will be spawned. Set a mission actor class."));
		Result = EDataValidationResult::Invalid;
	}

	// Tuning (§2-8-1 soft migration): if this mission type expects a tuning object (its CDO's
	// GetExpectedTuningClass() is non-null), warn when Tuning is unset (fallback to the actor's legacy fields
	// still works — nothing breaks) and error when Tuning is the wrong subclass for this mission type.
	if (MissionClass)
	{
		if (const AFPSRMissionActor* MissionCDO = MissionClass->GetDefaultObject<AFPSRMissionActor>())
		{
			const TSubclassOf<UFPSRMissionTuning> ExpectedTuningClass = MissionCDO->GetExpectedTuningClass();
			if (ExpectedTuningClass)
			{
				if (!Tuning)
				{
					Context.AddWarning(LOCTEXT("NoTuning", "이 미션 타입은 튜닝 객체가 필요합니다 — Tuning을 설정하세요(미설정 시 미션 액터 기본값 fallback). §2-8-1 마이그레이션"));
				}
				else if (!Tuning->IsA(ExpectedTuningClass))
				{
					Context.AddError(LOCTEXT("TuningTypeMismatch", "Tuning 타입이 미션 클래스와 불일치"));
					Result = EDataValidationResult::Invalid;
				}
			}
		}
	}

	// FGameplayTag::IsValid() only checks TagName != NAME_None — it does NOT confirm the tag is registered in the
	// project's tag dictionary. The tag picker can only ever produce a registered tag, but headless/commandlet
	// content authoring (this project imports GameplayTag-bearing properties via import_text token substitution —
	// see MEMORY note on headless GAS content authoring) can write an arbitrary FName that never resolves to a real
	// tag node, in which case no AFPSRMissionSpawnPoint's MissionTag match would ever succeed. Re-resolve through the
	// manager (ErrorIfNotFound=false to avoid spamming the log) to catch that case specifically.
	if (SpawnPointTag.GetTagName() != NAME_None)
	{
		const FGameplayTag Resolved = UGameplayTagsManager::Get().RequestGameplayTag(SpawnPointTag.GetTagName(), /*ErrorIfNotFound=*/false);
		if (!Resolved.IsValid())
		{
			Context.AddError(FText::Format(
				LOCTEXT("SpawnPointTagNotRegistered", "SpawnPointTag '{0}' is not a registered GameplayTag — no AFPSRMissionSpawnPoint will ever match it. Pick a valid Mission.Spawn.* tag."),
				FText::FromName(SpawnPointTag.GetTagName())));
			Result = EDataValidationResult::Invalid;
		}
	}

	return Result;
}

#undef LOCTEXT_NAMESPACE
#endif // WITH_EDITOR
