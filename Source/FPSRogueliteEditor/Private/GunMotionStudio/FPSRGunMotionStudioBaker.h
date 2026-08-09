// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UAnimSequence;
class UAnimSequenceBase;
class UAnimMontage;
class USkeleton;
struct FFPSRStudioKey;
struct FFPSRStudioTrack;
struct FFPSRStudioAttachSpan;
class UFPSRGunMotionStudioData;

/**
 * Gun-motion studio baker (GunMotionTool_Spec.md §3) — pure logic, NO Slate/ImGui dependency (§1 file layout note),
 * so it can be unit-exercised (and reasoned about) independently of the ImGui session/UI. Ported line-for-line where
 * the spec calls for it from the pre-teardown baker (git history 4228f37b^ FPSRGunMotionBaker.cpp) — same smoothstep
 * interpolation (§2-2/§3-3), same curve-write-to-clip-and-montage recipe (§3-3/§3-4, A17) — adapted to the NEW data
 * model (FFPSRStudioKey/FFPSRStudioTrack/FFPSRStudioAttachSpan, UFPSRGunMotionStudioData) and the NEW §3-2 live-M/P
 * bone-track bake (no CDO approximation — the session caches M/P from the LIVE PIE actor once at entry and passes
 * them in here every re-bake).
 */
class FPSRGunMotionStudioBaker
{
public:
	/** §3-1: create a brand-new transient WIP clip on ArmsSkeleton — 0 bone tracks, 30fps, AAT_LocalSpaceBase +
	 *  ABPT_RefPose (untracked bones = structural delta-identity, §7 A16 — never "trackless means broken"), length =
	 *  LengthSeconds. Transient (GetTransientPackage()) — never saved directly; §3-1's *real* asset is only created at
	 *  Save time via AnimationEditorUtils::CreateAnimationAssets (content-browser name/path dialog, called by the
	 *  session, not this class — this only builds the WIP scratch clip the session plays/scrubs while authoring). */
	static UAnimSequence* CreateWipClip(USkeleton* ArmsSkeleton, float LengthSeconds, FText& OutError);

	/** §3-3/§2-2 smoothstep(a²(3-2a)) + Slerp keyframe evaluation, shared by every channel type (gun/hand/part keys
	 *  all use FFPSRStudioKey — unlike the pre-teardown generation's separate Cam/Channel/Scalar key structs, the new
	 *  data model unified them). First/last key outside the range clamps to that key's value (§2-2). */
	static void EvalStudioKeys(const TArray<FFPSRStudioKey>& SortedKeys, float Time, FVector& OutLoc, FQuat& OutRot);

	/** §3-2: bake the GunKeys track (camera-space authoring) into Seq's hand_r bone track via the live-cached M/P
	 *  transforms (captured once at studio session entry from the PIE actor — see FPSRGunMotionStudio). Overwrites
	 *  the WHOLE hand_r track for every frame of Seq's current length (WIP re-bake semantics, §5 "키 변경 → WIP
	 *  재베이크"). CamToArmsRotation = M (ArmsComp world rotation⁻¹ * Cam world rotation). LowerArmCompRotation = P
	 *  (lowerarm_r component-space rotation at session entry, Idle pose). HandRRefPose = the hand_r bone's LOCAL
	 *  ref-pose transform on ArmsSkeleton (Base in the spec's math — §3-2 track_rot = d_rot·refpose_rot, so unlike
	 *  the sanitize-based Base of the pre-teardown generation, on a brand-new 0-track clip Base IS simply ref pose:
	 *  no existing additive delta to subtract, ABPT_RefPose means "no track = ref pose" by construction). */
	static bool BakeGunTrackToHandR(UAnimSequence* Seq, const TArray<FFPSRStudioKey>& SortedGunKeys,
		const FQuat& CamToArmsRotation, const FQuat& LowerArmCompRotation, const FTransform& HandRRefPose,
		FName HandRBoneName, FText& OutError);

	/** §3-3/§3-4/A17: sample LeftHandKeys/RightHandKeys/PartTracks + attach spans (ramped into the Blend curves,
	 *  ±BlendTimeSeconds around each span boundary) into FPGM_* curves (FPSRGunMotionCurves.h names — never a
	 *  literal) and write them to Seq AND to any UAnimMontage that references Seq as a single-segment, PlayRate=1,
	 *  start=0 slot track (the A17 automation — the montage's OWN curve is what the runtime GetCurveValue path reads
	 *  when Seq plays inside a montage). Also collapses LeftHandAttachSpans/RightHandAttachSpans down to ONE
	 *  representative socket per hand (§2-3 LeftHandAttachPartSocket/RightHandAttachPartSocket) and writes it into
	 *  StudioData for the session/save step to persist. Returns per-montage skip/write reasons in OutMontageReport
	 *  (may be null). */
	static bool BakeCurveChannels(UAnimSequence* Seq, UFPSRGunMotionStudioData* StudioData, FText& OutError,
		TArray<FString>* OutMontageReport = nullptr);

private:
	/** Shared write-to-asset recipe (clip or montage) — ported verbatim in structure from the pre-teardown
	 *  WriteCurveSamplesToSequenceBase: empty sample array = remove the curve if present (§7 "부재=0", no dead
	 *  curves); non-empty = add-if-missing then SetCurveKeys, inside one IAnimationDataController bracket. */
	static bool WriteCurveSamplesToSequenceBase(UAnimSequenceBase* Target,
		const TMap<FName, TArray<FRichCurveKey>>& CurveSamples, const FText& TransactionText, FText& OutError);

	/** Build the full FPGM_* curve sample map (every axis of every channel, sampled to Seq's frame grid) from
	 *  StudioData — the single source both the clip-write and the montage-write draw from, so a montage's curve is
	 *  never a fraction-off resample of the clip's (A17 "값이 갈라지면 두 경로가 다르게 논다" — same bug class the
	 *  pre-teardown generation guarded against, ported here unchanged). */
	static void BuildCurveSampleMap(const UFPSRGunMotionStudioData* StudioData, int32 NumFrames, double Fps,
		TMap<FName, TArray<FRichCurveKey>>& OutCurveSamples);

	/** One hand's Blend curve ramp from its attach spans (§2-2 "베이크가 Blend 커브 램프(스팬 경계 ±BlendTime, 기본
	 *  0.1초)로 변환") — 0 outside every span, ramps 0→1 over BlendTimeSeconds entering a span and 1→0 leaving it,
	 *  linear (matches every OTHER curve write in this baker using RCIM_Linear sampled keys, §3-3). */
	static void SampleAttachBlendCurve(const TArray<FFPSRStudioAttachSpan>& Spans, int32 NumFrames, double Fps,
		float BlendTimeSeconds, TArray<FRichCurveKey>& OutKeys);

	/** §2-3 reduction: a clip attaches at most one part per hand — pick the span whose socket appears in the most
	 *  spans (author intent: "sequential multi-part attachment within a single clip is a later extension", so ties
	 *  break to the FIRST span's socket, deterministic). NAME_None if Spans is empty. */
	static FName ReduceAttachSpansToRepresentativeSocket(const TArray<FFPSRStudioAttachSpan>& Spans);
};
