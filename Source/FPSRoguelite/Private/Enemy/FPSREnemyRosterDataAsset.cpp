// Copyright Epic Games, Inc. All Rights Reserved.

#include "Enemy/FPSREnemyRosterDataAsset.h"
#include "Enemy/FPSREnemyBase.h"

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
