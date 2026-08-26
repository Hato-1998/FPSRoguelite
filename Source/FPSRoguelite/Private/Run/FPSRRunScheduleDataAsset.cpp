// Copyright Epic Games, Inc. All Rights Reserved.

#include "Run/FPSRRunScheduleDataAsset.h"
#include "Math/UnrealMathUtility.h"

FFPSRStageDifficultyAnchor UFPSRRunScheduleDataAsset::EvalStageAt(TConstArrayView<FFPSRStageDifficultyAnchor> Anchors, int32 StageIndex)
{
	const int32 Num = Anchors.Num();
	if (Num == 0)
	{
		return FFPSRStageDifficultyAnchor(); // identity — an unauthored StageDifficulty array is a complete no-op
	}
	// The returned StageIndex is ALWAYS the QUERIED stage, in every branch — including the two flat clamps below,
	// which would otherwise hand back the anchor's own index and make this field mean different things depending on
	// which branch produced the result (merge-gate P3). Nothing reads it today; that is exactly why it has to be
	// pinned down now rather than after a first consumer builds on one branch's meaning.
	if (StageIndex <= Anchors[0].StageIndex)
	{
		FFPSRStageDifficultyAnchor Clamped = Anchors[0];
		Clamped.StageIndex = StageIndex;
		return Clamped;
	}
	if (StageIndex >= Anchors[Num - 1].StageIndex)
	{
		FFPSRStageDifficultyAnchor Clamped = Anchors[Num - 1];
		Clamped.StageIndex = StageIndex;
		return Clamped;
	}
	for (int32 i = 1; i < Num; ++i)
	{
		const FFPSRStageDifficultyAnchor& A = Anchors[i - 1];
		const FFPSRStageDifficultyAnchor& B = Anchors[i];
		if (StageIndex <= B.StageIndex)
		{
			const float Span = static_cast<float>(B.StageIndex - A.StageIndex);
			const float T = (Span > 0.0f) ? static_cast<float>(StageIndex - A.StageIndex) / Span : 0.0f;
			FFPSRStageDifficultyAnchor Result;
			Result.StageIndex = StageIndex;
			Result.AliveCountBonus = FMath::RoundToInt(FMath::Lerp(static_cast<float>(A.AliveCountBonus), static_cast<float>(B.AliveCountBonus), T));
			Result.AliveCountMultiplier = FMath::Lerp(A.AliveCountMultiplier, B.AliveCountMultiplier, T);
			Result.InhibitorDurabilityMultiplier = FMath::Lerp(A.InhibitorDurabilityMultiplier, B.InhibitorDurabilityMultiplier, T);
			return Result;
		}
	}
	// Unreachable (StageIndex >= last is handled above) — belt-and-suspenders, mirrors EvalAliveCountByLevel. Still
	// stamps the queried StageIndex so the "always the queried stage" contract above holds on every path.
	FFPSRStageDifficultyAnchor Fallback = Anchors[Num - 1];
	Fallback.StageIndex = StageIndex;
	return Fallback;
}

float UFPSRRunScheduleDataAsset::EvalPartySizeMultiplier(TConstArrayView<float> ByPartySize, int32 PartySize)
{
	const int32 Num = ByPartySize.Num();
	if (Num == 0)
	{
		return 1.0f; // documented fallback — an unauthored array applies no party-size scaling
	}
	const int32 Index = FMath::Clamp(PartySize - 1, 0, Num - 1); // index 0 = 1 player; beyond the array holds the last entry
	return ByPartySize[Index];
}

float UFPSRRunScheduleDataAsset::EvalAliveCountByLevel(TConstArrayView<FFPSRAliveCountAnchor> Anchors, int32 Level)
{
	// Pure relocation from FPSRRunDirectorSubsystem.cpp's anonymous namespace (2026-08-26) — logic below is
	// byte-identical to the original inline function; only the parameter type changed (const TArray<>& -> the new
	// evaluator contract's TConstArrayView<>, which TArray implicitly converts to at every existing call site).
	const int32 Num = Anchors.Num();
	if (Num == 0)
	{
		return 0.0f;
	}
	if (Level <= Anchors[0].Level)
	{
		return static_cast<float>(Anchors[0].Count);
	}
	if (Level >= Anchors[Num - 1].Level)
	{
		return static_cast<float>(Anchors[Num - 1].Count);
	}
	for (int32 i = 1; i < Num; ++i)
	{
		const FFPSRAliveCountAnchor& A = Anchors[i - 1];
		const FFPSRAliveCountAnchor& B = Anchors[i];
		if (Level <= B.Level)
		{
			const float Span = static_cast<float>(B.Level - A.Level);
			const float T = (Span > 0.0f) ? static_cast<float>(Level - A.Level) / Span : 0.0f;
			return FMath::Lerp(static_cast<float>(A.Count), static_cast<float>(B.Count), T);
		}
	}
	return static_cast<float>(Anchors[Num - 1].Count);
}
