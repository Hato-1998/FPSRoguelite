// Copyright Epic Games, Inc. All Rights Reserved.

#include "Enemy/FPSREnemyAllocator.h"

namespace FPSREnemyAllocator
{
	void Apportion(const TArray<int32>& PlayerCounts, int32 GlobalTarget, int32 GroupBonus, TArray<int32>& OutTargets)
	{
		const int32 N = PlayerCounts.Num();
		OutTargets.Reset();
		OutTargets.SetNumZeroed(N);
		if (N == 0 || GlobalTarget <= 0)
		{
			return;
		}

		// Total weight; empty maps contribute 0 (and get 0 target).
		int64 TotalWeight = 0;
		TArray<int32, TInlineAllocator<8>> Weights;
		Weights.SetNumZeroed(N);
		for (int32 i = 0; i < N; ++i)
		{
			Weights[i] = MapWeight(PlayerCounts[i], GroupBonus);
			TotalWeight += Weights[i];
		}
		if (TotalWeight <= 0)
		{
			return; // no occupied maps
		}

		// Largest-remainder (Hamilton): floor each quota, then hand the leftover units to the largest fractional parts,
		// so the targets sum to EXACTLY GlobalTarget (no independent-round drift that could push the total over cap).
		int32 Assigned = 0;
		TArray<int32, TInlineAllocator<8>> Remainders; // scaled fractional remainder = (GlobalTarget*Weight) % TotalWeight
		Remainders.SetNumZeroed(N);
		for (int32 i = 0; i < N; ++i)
		{
			const int64 Num = static_cast<int64>(GlobalTarget) * Weights[i];
			OutTargets[i] = static_cast<int32>(Num / TotalWeight);
			Remainders[i] = static_cast<int32>(Num % TotalWeight);
			Assigned += OutTargets[i];
		}

		int32 Leftover = GlobalTarget - Assigned;
		// Distribute leftover units one at a time to the maps with the largest remainder (ties -> lower index for
		// determinism). Only maps with weight > 0 are eligible (an empty map stays 0). Leftover < number of maps.
		while (Leftover > 0)
		{
			int32 Best = INDEX_NONE;
			int32 BestRem = -1;
			for (int32 i = 0; i < N; ++i)
			{
				if (Weights[i] > 0 && Remainders[i] > BestRem)
				{
					BestRem = Remainders[i];
					Best = i;
				}
			}
			if (Best == INDEX_NONE)
			{
				break; // shouldn't happen (TotalWeight > 0), guard anyway
			}
			++OutTargets[Best];
			Remainders[Best] = -1; // consumed (each map gets at most one leftover unit)
			--Leftover;
		}
	}
}
