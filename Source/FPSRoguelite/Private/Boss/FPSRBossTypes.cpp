// Copyright Epic Games, Inc. All Rights Reserved.

#include "Boss/FPSRBossTypes.h"

namespace FPSRBoss
{
	int32 ComputePhase(float HealthFraction, TConstArrayView<float> Thresholds)
	{
		// Walk the descending thresholds and take the deepest one we are at or below. Written as a scan rather than a
		// binary search on purpose: this runs once per damage event on ONE actor, and a scan stays correct even if a
		// designer authors the array slightly out of order (IsDataValid warns about that separately) — a binary
		// search would silently return a wrong phase for the same input.
		int32 Phase = 1;
		for (int32 Index = 0; Index < Thresholds.Num(); ++Index)
		{
			if (HealthFraction <= Thresholds[Index])
			{
				Phase = FMath::Max(Phase, Index + 2);
			}
		}
		return Phase;
	}
}
