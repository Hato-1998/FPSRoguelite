// Copyright Epic Games, Inc. All Rights Reserved.

#include "Run/Mission/FPSRMissionDataAsset.h"

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
