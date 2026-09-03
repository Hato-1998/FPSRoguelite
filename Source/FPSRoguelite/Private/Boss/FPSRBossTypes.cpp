// Copyright Epic Games, Inc. All Rights Reserved.

#include "Boss/FPSRBossTypes.h"

#if !UE_BUILD_SHIPPING
#include "HAL/IConsoleManager.h"

namespace
{
	static int32 GFPSRBossDebugDraw = 0;
	static FAutoConsoleVariableRef CVarFPSRBossDebugDraw(
		TEXT("FPSR.BossDebugDraw"),
		GFPSRBossDebugDraw,
		TEXT("Draw boss pattern state as debug shapes (0=off, 1=on). Blast markers, beams, the marked player and the ")
		TEXT("current pattern stage. Runs on clients too, so a client's beam can be compared against the server's."),
		ECVF_Cheat);
}
#endif

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

	ETriggerAuthoringIssue ValidateTrigger(const FFPSRBossPatternTrigger& Trigger)
	{
		if (Trigger.Threshold <= 0.0f)
		{
			return ETriggerAuthoringIssue::ThresholdNotPositive;
		}
		if (Trigger.Kind == EFPSRBossTriggerKind::HealthBelow && Trigger.Threshold >= 1.0f)
		{
			return ETriggerAuthoringIssue::HealthThresholdFull;
		}
		return ETriggerAuthoringIssue::None;
	}

	void BuildSelectionOrder(EFPSRBossPatternSelection Policy, int32 NumEligible, int32 Cursor,
		int32 RandomStart, TArray<int32>& OutOrder)
	{
		OutOrder.Reset();
		if (NumEligible <= 0)
		{
			return;
		}
		OutOrder.Reserve(NumEligible);

		// Both policies visit EVERY candidate; they differ only in where the walk starts. That is what keeps a
		// trigger from being wasted on a pattern that happens to be on cooldown.
		const int32 Start = (Policy == EFPSRBossPatternSelection::Random)
			? ((RandomStart % NumEligible) + NumEligible) % NumEligible
			: ((Cursor % NumEligible) + NumEligible) % NumEligible;

		for (int32 Step = 0; Step < NumEligible; ++Step)
		{
			OutOrder.Add((Start + Step) % NumEligible);
		}
	}

	int32 EstimatePeakBlastMarks(int32 MaxPlayers, float FuseSeconds, float IntervalSeconds)
	{
		if (MaxPlayers <= 0 || IntervalSeconds <= KINDA_SMALL_NUMBER)
		{
			return 0;
		}
		// A volley goes out every Interval and each marker lives Fuse seconds, so the number alive at once is one
		// volley per interval that fits inside a fuse — plus the volley just fired, hence the +1.
		const int32 VolleysAlive = FMath::FloorToInt(FMath::Max(0.0f, FuseSeconds) / IntervalSeconds) + 1;
		return MaxPlayers * VolleysAlive;
	}

#if !UE_BUILD_SHIPPING
	bool IsDebugDrawEnabled()
	{
		return GFPSRBossDebugDraw != 0;
	}
#endif
}
