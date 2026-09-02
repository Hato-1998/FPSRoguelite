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

	bool ShouldTriggerFire(const FFPSRBossPatternTrigger& Trigger, float ElapsedSeconds,
		int32 PatternsPerformed, float HealthFraction, int32& OutFireCount)
	{
		OutFireCount = Trigger.FireCount;
		const float Threshold = FMath::Max(KINDA_SMALL_NUMBER, Trigger.Threshold);

		switch (Trigger.Kind)
		{
		case EFPSRBossTriggerKind::Elapsed:
		{
			const int32 Due = FMath::FloorToInt(ElapsedSeconds / Threshold);
			if (Due > Trigger.FireCount && (Trigger.bRepeating || Trigger.FireCount == 0))
			{
				// Repeating latches to Due, not FireCount + 1, so a long gap can never leave a backlog for the boss
				// to burn through as a burst of back-to-back patterns.
				OutFireCount = Trigger.bRepeating ? Due : 1;
				return true;
			}
			return false;
		}

		case EFPSRBossTriggerKind::PatternCount:
		{
			const int32 Due = FMath::FloorToInt(static_cast<float>(PatternsPerformed) / Threshold);
			if (Due > Trigger.FireCount && (Trigger.bRepeating || Trigger.FireCount == 0))
			{
				OutFireCount = Trigger.bRepeating ? Due : 1;
				return true;
			}
			return false;
		}

		case EFPSRBossTriggerKind::HealthBelow:
			// One-shot regardless of bRepeating: health only falls during a boss fight, so a repeating version would
			// mean "fires every tick forever once crossed", which is never what authoring this is meant to express.
			if (Trigger.FireCount == 0 && HealthFraction <= Trigger.Threshold)
			{
				OutFireCount = 1;
				return true;
			}
			return false;
		}

		return false;
	}
}
