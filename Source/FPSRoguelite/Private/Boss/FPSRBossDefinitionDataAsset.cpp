// Copyright Epic Games, Inc. All Rights Reserved.

#include "Boss/FPSRBossDefinitionDataAsset.h"
#include "Boss/FPSRBossBase.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "FPSRBossDefinition"

#if WITH_EDITOR
FText UFPSRBossDefinitionDataAsset::GetDescription() const
{
	const FText ClassName = BossClass
		? FText::FromString(BossClass->GetName())
		: LOCTEXT("DefaultBoss", "AFPSRBossBase (C++ fallback)");
	const FText SpawnRule = bUseBossSpawnPoint
		? LOCTEXT("AtSpawnPoint", "boss spawn point")
		: LOCTEXT("AtFallback", "fallback location");
	return FText::Format(
		LOCTEXT("BossDescFmt", "Boss: {0} — {1} HP, spawns at {2}."),
		ClassName, FText::AsNumber(FMath::RoundToInt(MaxHealth)), SpawnRule);
}

EDataValidationResult UFPSRBossDefinitionDataAsset::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	// A null BossClass is not invalid (the director falls back to the C++ AFPSRBossBase placeholder), but the
	// designer almost certainly meant to assign a boss BP — surface it as a warning, not an error.
	if (!BossClass)
	{
		Context.AddWarning(LOCTEXT("NoBossClass", "BossDefinition has no BossClass — the director will spawn the C++ AFPSRBossBase placeholder. Assign a boss BP for content."));
	}

	if (MaxHealth <= 0.0f)
	{
		Context.AddError(LOCTEXT("NoMaxHealth", "BossDefinition MaxHealth <= 0 — the boss would spawn already dead. Set MaxHealth > 0."));
		Result = EDataValidationResult::Invalid;
	}

	// Phase thresholds must be strictly descending and strictly inside (0,1). ComputePhase itself is written to
	// survive a mis-ordered array (it takes the deepest match rather than the first), so this is a designer-facing
	// warning about intent rather than a crash guard — but a threshold of exactly 1.0 would put the boss in phase 2
	// before it is ever hit, and one at 0.0 can only trigger on the death frame, so those two are errors.
	for (int32 Index = 0; Index < PhaseHealthThresholds.Num(); ++Index)
	{
		const float Threshold = PhaseHealthThresholds[Index];
		if (Threshold <= 0.0f || Threshold >= 1.0f)
		{
			Context.AddError(FText::Format(
				LOCTEXT("PhaseThresholdRange", "PhaseHealthThresholds[{0}] = {1} — must be strictly between 0 and 1 (1.0 would start the boss in phase 2; 0.0 could only fire on the death frame)."),
				FText::AsNumber(Index), FText::AsNumber(Threshold)));
			Result = EDataValidationResult::Invalid;
		}
		if (Index > 0 && Threshold >= PhaseHealthThresholds[Index - 1])
		{
			Context.AddWarning(FText::Format(
				LOCTEXT("PhaseThresholdOrder", "PhaseHealthThresholds[{0}] is not below the previous entry — the array is meant to descend (e.g. 0.66, 0.33)."),
				FText::AsNumber(Index)));
		}
	}

	return Result;
}
#endif // WITH_EDITOR

#undef LOCTEXT_NAMESPACE
