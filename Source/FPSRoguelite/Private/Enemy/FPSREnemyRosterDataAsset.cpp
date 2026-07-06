// Copyright Epic Games, Inc. All Rights Reserved.

#include "Enemy/FPSREnemyRosterDataAsset.h"
#include "Enemy/FPSREnemyBase.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

TSubclassOf<AFPSREnemyBase> UFPSREnemyRosterDataAsset::PickEnemyClass(const FFPSREnemySpawnContext& Context) const
{
	// Sum eligible weights (skip inert rules / non-positive weights), then roll a weighted-random pick. Server-side
	// gameplay RNG (FMath::FRand) — spawn composition is a server-authority decision.
	float TotalWeight = 0.0f;
	for (const TObjectPtr<UFPSREnemySpawnRule>& RulePtr : SpawnRules)
	{
		const UFPSREnemySpawnRule* Rule = RulePtr;
		if (Rule == nullptr || Rule->GetEnemyClass() == nullptr)
		{
			continue;
		}
		const float W = Rule->GetWeight(Context);
		if (W > 0.0f)
		{
			TotalWeight += W;
		}
	}

	if (TotalWeight <= 0.0f)
	{
		return nullptr; // no eligible rule — caller falls back to its single configured class
	}

	float Roll = FMath::FRand() * TotalWeight;
	for (const TObjectPtr<UFPSREnemySpawnRule>& RulePtr : SpawnRules)
	{
		const UFPSREnemySpawnRule* Rule = RulePtr;
		if (Rule == nullptr || Rule->GetEnemyClass() == nullptr)
		{
			continue;
		}
		const float W = Rule->GetWeight(Context);
		if (W <= 0.0f)
		{
			continue;
		}
		Roll -= W;
		if (Roll <= 0.0f)
		{
			return Rule->GetEnemyClass();
		}
	}

	// Floating-point slack guard: return the last eligible rule's class.
	for (int32 i = SpawnRules.Num() - 1; i >= 0; --i)
	{
		const UFPSREnemySpawnRule* Rule = SpawnRules[i];
		if (Rule && Rule->GetEnemyClass() && Rule->GetWeight(Context) > 0.0f)
		{
			return Rule->GetEnemyClass();
		}
	}
	return nullptr;
}

#if WITH_EDITOR
#define LOCTEXT_NAMESPACE "FPSREnemyRosterDataAsset"

EDataValidationResult UFPSREnemyRosterDataAsset::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	// A zero-context probe: UFPSREnemySpawnRule_Static's weight doesn't vary with run context, so an empty
	// FFPSREnemySpawnContext is enough to ask "is this rule eligible at all" for the all-inert check below. Future
	// context-scaling rules (level-gated, time-windowed) may report 0 here even when they'd contribute later in the
	// run — that's fine, this is a "roster looks empty right now" warning, not a hard error.
	const FFPSREnemySpawnContext ProbeContext;
	bool bAnyEligible = false;

	for (int32 Index = 0; Index < SpawnRules.Num(); ++Index)
	{
		const UFPSREnemySpawnRule* Rule = SpawnRules[Index];
		if (Rule == nullptr)
		{
			Context.AddError(FText::Format(
				LOCTEXT("NullSpawnRule", "SpawnRules[{0}] is null — remove it or assign a rule."),
				FText::AsNumber(Index)));
			Result = EDataValidationResult::Invalid;
			continue;
		}

		if (const UFPSREnemySpawnRule_Static* StaticRule = Cast<UFPSREnemySpawnRule_Static>(Rule))
		{
			if (StaticRule->EnemyClass == nullptr)
			{
				Context.AddError(FText::Format(
					LOCTEXT("StaticRuleNoEnemyClass", "SpawnRules[{0}] (Static Weight) has no EnemyClass — this rule is permanently inert."),
					FText::AsNumber(Index)));
				Result = EDataValidationResult::Invalid;
				continue;
			}
		}

		if (Rule->GetEnemyClass() != nullptr && Rule->GetWeight(ProbeContext) > 0.0f)
		{
			bAnyEligible = true;
		}
	}

	if (SpawnRules.Num() == 0)
	{
		Context.AddWarning(LOCTEXT("EmptyRoster", "SpawnRules is empty — the swarm falls back to the spawner's single configured EnemyClass (no archetype mix)."));
	}
	else if (!bAnyEligible)
	{
		Context.AddWarning(LOCTEXT("AllRulesInert", "No SpawnRules entry is currently eligible (all zero weight / no EnemyClass) — the swarm falls back to the spawner's single configured EnemyClass."));
	}

	return Result;
}

#undef LOCTEXT_NAMESPACE
#endif // WITH_EDITOR
