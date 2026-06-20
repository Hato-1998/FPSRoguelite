// Copyright Epic Games, Inc. All Rights Reserved.

#include "Boss/FPSRBossDefinitionDataAsset.h"
#include "Boss/FPSRBossBase.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "FPSRBossDefinition"

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

#if WITH_EDITOR
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

	return Result;
}
#endif // WITH_EDITOR

#undef LOCTEXT_NAMESPACE
