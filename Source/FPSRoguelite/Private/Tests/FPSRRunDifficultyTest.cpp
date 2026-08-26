// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Run/FPSRRunScheduleDataAsset.h"

#if WITH_AUTOMATION_TESTS

// Headless invariant net for the stage-difficulty axis (신설 2026-08-26, ADR 0010 D6 비용 축 "일찍 부수려 할수록
// 비싸다"). Pure/worldless — exercises UFPSRRunScheduleDataAsset's three public static evaluators directly (no
// instance, no world), mirroring FPSRAllocatorUnitTest.cpp's shape (call the static, assert invariants) and
// FPSRDestructibleTest.cpp's CDO-free style. The EvalAliveCountByLevel cases below double as the regression net for
// its 2026-08-26 move out of FPSRRunDirectorSubsystem.cpp's anonymous namespace (pure relocation — see
// FPSRRunScheduleDataAsset.h's comment on the move): same shape, same expected outputs as the function had before
// the move.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSRRunDifficultyTest, "FPSRoguelite.Run.Difficulty",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFPSRRunDifficultyTest::RunTest(const FString& Parameters)
{
	// --- EvalStageAt --------------------------------------------------------------------------------------------

	// (1) Empty anchors -> identity (Bonus 0, both multipliers 1.0) — an unauthored StageDifficulty is a no-op.
	{
		const TArray<FFPSRStageDifficultyAnchor> Empty;
		const FFPSRStageDifficultyAnchor Result = UFPSRRunScheduleDataAsset::EvalStageAt(Empty, 5);
		TestEqual(TEXT("empty anchors: AliveCountBonus 0"), Result.AliveCountBonus, 0);
		TestEqual(TEXT("empty anchors: AliveCountMultiplier 1.0"), Result.AliveCountMultiplier, 1.0f);
		TestEqual(TEXT("empty anchors: InhibitorDurabilityMultiplier 1.0"), Result.InhibitorDurabilityMultiplier, 1.0f);
	}

	// (2) Single anchor -> flat everywhere, regardless of the StageIndex queried.
	{
		TArray<FFPSRStageDifficultyAnchor> Anchors;
		FFPSRStageDifficultyAnchor A;
		A.StageIndex = 3; A.AliveCountBonus = 10; A.AliveCountMultiplier = 1.5f; A.InhibitorDurabilityMultiplier = 2.0f;
		Anchors.Add(A);

		for (int32 Query : {0, 3, 100})
		{
			const FFPSRStageDifficultyAnchor Result = UFPSRRunScheduleDataAsset::EvalStageAt(Anchors, Query);
			TestEqual(TEXT("single anchor: Bonus flat"), Result.AliveCountBonus, 10);
			TestEqual(TEXT("single anchor: AliveCountMultiplier flat"), Result.AliveCountMultiplier, 1.5f);
			TestEqual(TEXT("single anchor: InhibitorDurabilityMultiplier flat"), Result.InhibitorDurabilityMultiplier, 2.0f);
		}
	}

	// (3)-(6): a 2-anchor curve (StageIndex 3 -> +8/x1.15/x1.6, StageIndex 6 -> +16/x1.3/x2.4 — the plan's own
	//     recommended DA_RunSchedule values) exercises below-first / above-last / exact-match / mid-interpolation.
	{
		TArray<FFPSRStageDifficultyAnchor> Anchors;
		FFPSRStageDifficultyAnchor A0;
		A0.StageIndex = 3; A0.AliveCountBonus = 8; A0.AliveCountMultiplier = 1.15f; A0.InhibitorDurabilityMultiplier = 1.6f;
		FFPSRStageDifficultyAnchor A1;
		A1.StageIndex = 6; A1.AliveCountBonus = 16; A1.AliveCountMultiplier = 1.3f; A1.InhibitorDurabilityMultiplier = 2.4f;
		Anchors.Add(A0);
		Anchors.Add(A1);

		// (3) Below the first anchor -> clamps to the first anchor's values.
		const FFPSRStageDifficultyAnchor ResultBelow = UFPSRRunScheduleDataAsset::EvalStageAt(Anchors, 0);
		TestEqual(TEXT("below first anchor: Bonus == first anchor"), ResultBelow.AliveCountBonus, 8);
		TestEqual(TEXT("below first anchor: AliveCountMultiplier == first anchor"), ResultBelow.AliveCountMultiplier, 1.15f);
		TestEqual(TEXT("below first anchor: InhibitorDurabilityMultiplier == first anchor"), ResultBelow.InhibitorDurabilityMultiplier, 1.6f);

		// (4) Above the last anchor -> clamps to the last anchor's values.
		const FFPSRStageDifficultyAnchor ResultAbove = UFPSRRunScheduleDataAsset::EvalStageAt(Anchors, 100);
		TestEqual(TEXT("above last anchor: Bonus == last anchor"), ResultAbove.AliveCountBonus, 16);
		TestEqual(TEXT("above last anchor: AliveCountMultiplier == last anchor"), ResultAbove.AliveCountMultiplier, 1.3f);
		TestEqual(TEXT("above last anchor: InhibitorDurabilityMultiplier == last anchor"), ResultAbove.InhibitorDurabilityMultiplier, 2.4f);

		// (4b) The returned StageIndex is the QUERIED stage on EVERY branch, not the matched anchor's own index —
		//      the two flat clamps are exactly where that used to diverge, so they are what this pins down.
		TestEqual(TEXT("below first anchor: StageIndex is the queried stage"), ResultBelow.StageIndex, 0);
		TestEqual(TEXT("above last anchor: StageIndex is the queried stage"), ResultAbove.StageIndex, 100);
		TestEqual(TEXT("mid interpolation: StageIndex is the queried stage"),
			UFPSRRunScheduleDataAsset::EvalStageAt(Anchors, 4).StageIndex, 4);

		// (5) Exact match on an authored anchor -> returns it exactly (the loop's T=1 edge / the pre-loop clamp).
		const FFPSRStageDifficultyAnchor ResultExact = UFPSRRunScheduleDataAsset::EvalStageAt(Anchors, 6);
		TestEqual(TEXT("exact match: Bonus"), ResultExact.AliveCountBonus, 16);
		TestEqual(TEXT("exact match: AliveCountMultiplier"), ResultExact.AliveCountMultiplier, 1.3f);
		TestEqual(TEXT("exact match: InhibitorDurabilityMultiplier"), ResultExact.InhibitorDurabilityMultiplier, 2.4f);

		// (6) Mid interpolation: StageIndex 4 is 1/3 of the way from 3 to 6 (span 3, offset 1).
		const FFPSRStageDifficultyAnchor ResultMid = UFPSRRunScheduleDataAsset::EvalStageAt(Anchors, 4);
		// Bonus: 8 + (16-8)*(1/3) = 10.666.. -> rounds to 11 (EvalStageAt rounds the interpolated int32 field).
		TestEqual(TEXT("mid interpolation: Bonus rounds"), ResultMid.AliveCountBonus, 11);
		TestTrue(TEXT("mid interpolation: AliveCountMultiplier strictly between anchors"),
			ResultMid.AliveCountMultiplier > 1.15f && ResultMid.AliveCountMultiplier < 1.3f);
		TestTrue(TEXT("mid interpolation: InhibitorDurabilityMultiplier strictly between anchors"),
			ResultMid.InhibitorDurabilityMultiplier > 1.6f && ResultMid.InhibitorDurabilityMultiplier < 2.4f);
	}

	// (7) Two anchors authored at the SAME StageIndex (an authoring mistake the validator flags as an Error — see
	//     FPSRRunScheduleValidator.cpp — but the evaluator itself must never divide by zero/crash on it). Querying
	//     exactly at the shared StageIndex must return a well-defined result (the earlier anchor's values, via the
	//     "at-or-below first anchor" clamp), not NaN/garbage.
	{
		TArray<FFPSRStageDifficultyAnchor> Anchors;
		FFPSRStageDifficultyAnchor A0;
		A0.StageIndex = 5; A0.AliveCountBonus = 1; A0.AliveCountMultiplier = 1.0f; A0.InhibitorDurabilityMultiplier = 1.0f;
		FFPSRStageDifficultyAnchor A1;
		A1.StageIndex = 5; A1.AliveCountBonus = 99; A1.AliveCountMultiplier = 9.0f; A1.InhibitorDurabilityMultiplier = 9.0f;
		Anchors.Add(A0);
		Anchors.Add(A1);

		const FFPSRStageDifficultyAnchor Result = UFPSRRunScheduleDataAsset::EvalStageAt(Anchors, 5);
		TestEqual(TEXT("zero-span anchors: well-defined result, no crash"), Result.AliveCountBonus, 1);
	}

	// (8) Negative StageIndex query -> clamps flat to the first anchor (same "at-or-below first" rule as (3), no
	//     special-case branch for negative numbers).
	{
		TArray<FFPSRStageDifficultyAnchor> Anchors;
		FFPSRStageDifficultyAnchor A0;
		A0.StageIndex = 0; A0.AliveCountBonus = 0; A0.AliveCountMultiplier = 1.0f; A0.InhibitorDurabilityMultiplier = 1.0f;
		FFPSRStageDifficultyAnchor A1;
		A1.StageIndex = 3; A1.AliveCountBonus = 8; A1.AliveCountMultiplier = 1.15f; A1.InhibitorDurabilityMultiplier = 1.6f;
		Anchors.Add(A0);
		Anchors.Add(A1);

		const FFPSRStageDifficultyAnchor Result = UFPSRRunScheduleDataAsset::EvalStageAt(Anchors, -5);
		TestEqual(TEXT("negative StageIndex: clamps to first anchor Bonus"), Result.AliveCountBonus, 0);
		TestEqual(TEXT("negative StageIndex: clamps to first anchor AliveCountMultiplier"), Result.AliveCountMultiplier, 1.0f);
	}

	// --- EvalPartySizeMultiplier ---------------------------------------------------------------------------------

	// (9) Empty array -> 1.0 fallback (documented — validator Warning, not Error).
	{
		const TArray<float> Empty;
		TestEqual(TEXT("empty party-size array -> 1.0"), UFPSRRunScheduleDataAsset::EvalPartySizeMultiplier(Empty, 4), 1.0f);
	}

	// (10) index 0 = 1 player; the plan's 4-entry recommended array covers party sizes 1..4; beyond the array
	//      holds the last entry (no extrapolation); PartySize 0 (defensive — should never actually happen) clamps
	//      the same as PartySize 1.
	{
		const TArray<float> ByPartySize = {1.0f, 1.8f, 2.5f, 3.1f};
		TestEqual(TEXT("party size 0 (defensive) -> index 0"), UFPSRRunScheduleDataAsset::EvalPartySizeMultiplier(ByPartySize, 0), 1.0f);
		TestEqual(TEXT("party size 1 -> index 0"), UFPSRRunScheduleDataAsset::EvalPartySizeMultiplier(ByPartySize, 1), 1.0f);
		TestEqual(TEXT("party size 4 -> index 3"), UFPSRRunScheduleDataAsset::EvalPartySizeMultiplier(ByPartySize, 4), 3.1f);
		TestEqual(TEXT("party size 8 (beyond array) -> holds last entry"), UFPSRRunScheduleDataAsset::EvalPartySizeMultiplier(ByPartySize, 8), 3.1f);
	}

	// --- EvalAliveCountByLevel (2026-08-26 moved from FPSRRunDirectorSubsystem.cpp's anonymous namespace — this is
	//     the byte-identical-behavior regression net the move promised) ---------------------------------------------

	// (11) Empty anchors -> 0.0 (matches the pre-move anonymous function's own early-out).
	{
		const TArray<FFPSRAliveCountAnchor> Empty;
		TestEqual(TEXT("EvalAliveCountByLevel: empty -> 0.0"), UFPSRRunScheduleDataAsset::EvalAliveCountByLevel(Empty, 10), 0.0f);
	}

	// (12) Below first / above last / exact match / mid-interpolation — same shape as EvalStageAt's own cases.
	{
		TArray<FFPSRAliveCountAnchor> Anchors;
		FFPSRAliveCountAnchor A0; A0.Level = 1; A0.Count = 10;
		FFPSRAliveCountAnchor A1; A1.Level = 21; A1.Count = 30;
		Anchors.Add(A0);
		Anchors.Add(A1);

		TestEqual(TEXT("EvalAliveCountByLevel: below first -> first Count"), UFPSRRunScheduleDataAsset::EvalAliveCountByLevel(Anchors, 0), 10.0f);
		TestEqual(TEXT("EvalAliveCountByLevel: above last -> last Count"), UFPSRRunScheduleDataAsset::EvalAliveCountByLevel(Anchors, 999), 30.0f);
		TestEqual(TEXT("EvalAliveCountByLevel: exact match"), UFPSRRunScheduleDataAsset::EvalAliveCountByLevel(Anchors, 21), 30.0f);
		// Level 11 is exactly halfway between Level 1 (Count 10) and Level 21 (Count 30) -> Count 20, exact (T=0.5
		// is exactly representable, so no rounding slack is needed for this equality).
		TestEqual(TEXT("EvalAliveCountByLevel: mid interpolation"), UFPSRRunScheduleDataAsset::EvalAliveCountByLevel(Anchors, 11), 20.0f);
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
