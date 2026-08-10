// Copyright Epic Games, Inc. All Rights Reserved.

#include "GunMotionStudio/FPSRGunMotionStudioBaker.h"

#include "Anim/FPSRGunMotionCurves.h"
#include "Anim/FPSRGunMotionStudioData.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/AnimMontage.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimData/IAnimationDataController.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetRegistry/AssetData.h"

#define LOCTEXT_NAMESPACE "FPSRGunMotionStudioBaker"

void FPSRGunMotionStudioBaker::EvalStudioKeys(const TArray<FFPSRStudioKey>& SortedKeys, float Time, FVector& OutLoc, FQuat& OutRot)
{
	// §2-2/§3-3, ported verbatim from the pre-teardown EvalChannelKeys (git history 4228f37b^) — smoothstep
	// (a²(3−2a)) + Slerp, first/last key outside the range clamps to that key's value.
	if (SortedKeys.Num() == 0)
	{
		OutLoc = FVector::ZeroVector;
		OutRot = FQuat::Identity;
		return;
	}
	if (Time <= SortedKeys[0].Time)
	{
		OutLoc = SortedKeys[0].Loc;
		OutRot = FQuat(SortedKeys[0].Rot);
		return;
	}
	if (Time >= SortedKeys.Last().Time)
	{
		OutLoc = SortedKeys.Last().Loc;
		OutRot = FQuat(SortedKeys.Last().Rot);
		return;
	}

	for (int32 Index = 0; Index < SortedKeys.Num() - 1; ++Index)
	{
		const FFPSRStudioKey& A = SortedKeys[Index];
		const FFPSRStudioKey& B = SortedKeys[Index + 1];
		if (Time >= A.Time && Time <= B.Time)
		{
			const float Span = B.Time - A.Time;
			const float Raw = (Span > UE_KINDA_SMALL_NUMBER) ? (Time - A.Time) / Span : 0.0f;
			const float Alpha = Raw * Raw * (3.0f - 2.0f * Raw); // smoothstep, §3-3
			OutLoc = FMath::Lerp(A.Loc, B.Loc, Alpha);
			OutRot = FQuat::Slerp(FQuat(A.Rot), FQuat(B.Rot), Alpha);
			return;
		}
	}

	// Defensive fallback — the two boundary checks above make this unreachable.
	OutLoc = SortedKeys.Last().Loc;
	OutRot = FQuat(SortedKeys.Last().Rot);
}

UAnimSequence* FPSRGunMotionStudioBaker::CreateWipClip(USkeleton* ArmsSkeleton, float LengthSeconds, FText& OutError)
{
	if (!ArmsSkeleton)
	{
		OutError = LOCTEXT("NoSkeleton", "팔 스켈레톤이 없습니다.");
		return nullptr;
	}

	UAnimSequence* Seq = NewObject<UAnimSequence>(GetTransientPackage(), NAME_None, RF_Transient);
	Seq->SetSkeleton(ArmsSkeleton);
	Seq->AdditiveAnimType = AAT_LocalSpaceBase;
	Seq->RefPoseType = ABPT_RefPose;

	IAnimationDataController& Controller = Seq->GetController();
	{
		IAnimationDataController::FScopedBracket Bracket(Controller, LOCTEXT("CreateWipBracket", "FPSR Gun Motion: Create WIP Clip"));
		Seq->Modify();
		// 🚨 InitializeModel 필수 — UE5.7 데이터 모델(UAnimationSequencerDataModel)은 여기서 내부 MovieScene 을
		// 만든다. 빠뜨리면 "No Movie Scene found for SequencerDataModel" 에러와 함께 길이 0 클립이 되어 슬롯
		// 다이내믹 몽타주 재생이 조용히 실패한다(첫 스모크 실측). UAnimSequenceFactory::FactoryCreateNew 순서
		// (InitializeModel → 프레임 설정 → NotifyPopulated) 답습.
		Controller.InitializeModel();
		Controller.SetFrameRate(FFrameRate(30, 1));
		const int32 NumFrames = FMath::Max(1, FMath::RoundToInt(LengthSeconds * 30.0f));
		Controller.SetNumberOfFrames(FFrameNumber(NumFrames));
		Controller.NotifyPopulated();
	}

	return Seq;
}

bool FPSRGunMotionStudioBaker::BakeGunTrackToHandR(UAnimSequence* Seq, const TArray<FFPSRStudioKey>& SortedGunKeys,
	const FQuat& CamToArmsRotation, const FQuat& LowerArmCompRotation, const FTransform& HandRRefPose,
	FName HandRBoneName, FText& OutError)
{
	if (!Seq)
	{
		OutError = LOCTEXT("NoSequence", "애니메이션 시퀀스가 없습니다.");
		return false;
	}
	IAnimationDataModel* Model = Seq->GetDataModel();
	if (!Model)
	{
		OutError = LOCTEXT("NoDataModel", "애니메이션 데이터 모델을 가져오지 못했습니다.");
		return false;
	}

	// §3-2: M = CamToArmsRotation (ArmsComp world rotation⁻¹ * Cam world rotation), P = LowerArmCompRotation —
	// BOTH captured LIVE from the PIE actor once at studio session entry (FPSRGunMotionStudio), never re-derived
	// here — this function is pure math over whatever was cached.
	const FQuat MInv = CamToArmsRotation.Inverse();
	const FQuat P = LowerArmCompRotation;
	const FQuat PInv = P.Inverse();

	const int32 NumFrames = Model->GetNumberOfFrames();
	const double Fps = Model->GetFrameRate().AsDecimal();
	const int32 NumKeys = NumFrames + 1;

	TArray<FVector> ClipPos; ClipPos.Reserve(NumKeys);
	TArray<FQuat> ClipRot; ClipRot.Reserve(NumKeys);
	TArray<FVector> ClipScale; ClipScale.Reserve(NumKeys);

	for (int32 FrameIndex = 0; FrameIndex <= NumFrames; ++FrameIndex)
	{
		const float Time = (Fps > 0.0) ? static_cast<float>(FrameIndex / Fps) : 0.0f;

		FVector OcamOffset;
		FQuat OcamRot;
		EvalStudioKeys(SortedGunKeys, Time, OcamOffset, OcamRot);

		// Ocomp = M⊗O_t, Oq' = M·O_q·M⁻¹ (§3-2)
		const FVector OcompTranslation = CamToArmsRotation.RotateVector(OcamOffset);
		FQuat OcompRot = CamToArmsRotation * OcamRot * MInv;
		OcompRot.Normalize();

		// d_rot = P⁻¹·Oq'·P, d_t = P⁻¹⊗Ocomp (§3-2)
		FQuat DRot = PInv * OcompRot * P;
		DRot.Normalize();
		const FVector DTranslation = PInv.RotateVector(OcompTranslation);

		// track_rot = d_rot · refpose_rot(hand_r), track_t = refpose_t + d_t (§3-2) — on a fresh 0-track WIP clip
		// (ABPT_RefPose) there is no existing additive delta to subtract first: Base IS the ref pose exactly.
		FQuat ClipRotValue = DRot * HandRRefPose.GetRotation();
		ClipRotValue.Normalize();

		ClipRot.Add(ClipRotValue);
		ClipPos.Add(HandRRefPose.GetLocation() + DTranslation);
		ClipScale.Add(HandRRefPose.GetScale3D());
	}

	IAnimationDataController& Controller = Seq->GetController();
	IAnimationDataController::FScopedBracket Bracket(Controller, LOCTEXT("BakeGunBracket", "FPSR Gun Motion: Bake Gun Track"));
	Seq->Modify();
	if (!Model->IsValidBoneTrackName(HandRBoneName))
	{
		Controller.AddBoneCurve(HandRBoneName);
	}
	if (!Controller.SetBoneTrackKeys(HandRBoneName, ClipPos, ClipRot, ClipScale))
	{
		OutError = FText::Format(LOCTEXT("SetKeysFailed", "본 '{0}' 트랙 쓰기에 실패했습니다."), FText::FromName(HandRBoneName));
		return false;
	}

	Seq->MarkPackageDirty();
	return true;
}

void FPSRGunMotionStudioBaker::SampleAttachBlendCurve(const TArray<FFPSRStudioAttachSpan>& Spans, int32 NumFrames,
	double Fps, float BlendTimeSeconds, TArray<FRichCurveKey>& OutKeys)
{
	OutKeys.Reset();
	if (Spans.Num() == 0)
	{
		return; // §7 "부재=0" — no spans means the caller deletes the curve (empty array is the signal).
	}

	const float SafeBlend = FMath::Max(BlendTimeSeconds, UE_KINDA_SMALL_NUMBER);

	for (int32 FrameIndex = 0; FrameIndex <= NumFrames; ++FrameIndex)
	{
		const float Time = (Fps > 0.0) ? static_cast<float>(FrameIndex / Fps) : 0.0f;
		float Blend = 0.0f;

		for (const FFPSRStudioAttachSpan& Span : Spans)
		{
			if (Time >= Span.Start && Time <= Span.End)
			{
				// Inside the span: ramp up from Start, ramp down toward End, full 1 in the middle stretch.
				const float InRamp = FMath::Clamp((Time - Span.Start) / SafeBlend, 0.0f, 1.0f);
				const float OutRamp = FMath::Clamp((Span.End - Time) / SafeBlend, 0.0f, 1.0f);
				Blend = FMath::Max(Blend, FMath::Min(InRamp, OutRamp));
			}
		}

		FRichCurveKey Key(Time, Blend);
		Key.InterpMode = RCIM_Linear;
		OutKeys.Add(Key);
	}
}

FName FPSRGunMotionStudioBaker::ReduceAttachSpansToRepresentativeSocket(const TArray<FFPSRStudioAttachSpan>& Spans)
{
	if (Spans.Num() == 0)
	{
		return NAME_None;
	}

	// §2-3: one representative socket per clip — pick the socket covering the most total TIME across its spans;
	// ties break to whichever socket appears FIRST (deterministic, matches author's authoring order).
	TMap<FName, float> TotalTimeBySocket;
	TArray<FName> FirstSeenOrder;
	for (const FFPSRStudioAttachSpan& Span : Spans)
	{
		if (Span.PartSocket.IsNone())
		{
			continue;
		}
		if (!TotalTimeBySocket.Contains(Span.PartSocket))
		{
			FirstSeenOrder.Add(Span.PartSocket);
		}
		TotalTimeBySocket.FindOrAdd(Span.PartSocket) += FMath::Max(0.0f, Span.End - Span.Start);
	}

	FName Best = NAME_None;
	float BestTime = -1.0f;
	for (const FName& Socket : FirstSeenOrder)
	{
		const float Time = TotalTimeBySocket.FindChecked(Socket);
		if (Time > BestTime)
		{
			BestTime = Time;
			Best = Socket;
		}
	}
	return Best;
}

void FPSRGunMotionStudioBaker::BuildCurveSampleMap(const UFPSRGunMotionStudioData* StudioData, int32 NumFrames,
	double Fps, TMap<FName, TArray<FRichCurveKey>>& OutCurveSamples)
{
	OutCurveSamples.Reset();
	if (!StudioData)
	{
		return;
	}

	// Channel (loc/rot) track -> 6 axis curves. 0 keys = all 6 registered EMPTY (caller interprets empty = delete,
	// §7 "부재=0").
	auto SampleChannelTrack = [&OutCurveSamples, NumFrames, Fps](const FFPSRStudioTrack& Track,
		FName TX, FName TY, FName TZ, FName RP, FName RY, FName RR)
	{
		TArray<FRichCurveKey>& TXKeys = OutCurveSamples.FindOrAdd(TX);
		TArray<FRichCurveKey>& TYKeys = OutCurveSamples.FindOrAdd(TY);
		TArray<FRichCurveKey>& TZKeys = OutCurveSamples.FindOrAdd(TZ);
		TArray<FRichCurveKey>& RPKeys = OutCurveSamples.FindOrAdd(RP);
		TArray<FRichCurveKey>& RYKeys = OutCurveSamples.FindOrAdd(RY);
		TArray<FRichCurveKey>& RRKeys = OutCurveSamples.FindOrAdd(RR);

		if (Track.Keys.Num() == 0)
		{
			return;
		}

		TArray<FFPSRStudioKey> Sorted = Track.Keys;
		Sorted.Sort([](const FFPSRStudioKey& A, const FFPSRStudioKey& B) { return A.Time < B.Time; });

		for (int32 FrameIndex = 0; FrameIndex <= NumFrames; ++FrameIndex)
		{
			const float Time = (Fps > 0.0) ? static_cast<float>(FrameIndex / Fps) : 0.0f;
			FVector Loc;
			FQuat RotQuat;
			EvalStudioKeys(Sorted, Time, Loc, RotQuat);
			const FRotator Rot = RotQuat.Rotator();

			auto MakeKey = [Time](float Value) { FRichCurveKey Key(Time, Value); Key.InterpMode = RCIM_Linear; return Key; };
			TXKeys.Add(MakeKey(Loc.X));
			TYKeys.Add(MakeKey(Loc.Y));
			TZKeys.Add(MakeKey(Loc.Z));
			RPKeys.Add(MakeKey(Rot.Pitch));
			RYKeys.Add(MakeKey(Rot.Yaw));
			RRKeys.Add(MakeKey(Rot.Roll));
		}
	};

	SampleChannelTrack(StudioData->LeftHandKeys,
		FPSRGunMotionCurves::HL_TX, FPSRGunMotionCurves::HL_TY, FPSRGunMotionCurves::HL_TZ,
		FPSRGunMotionCurves::HL_RP, FPSRGunMotionCurves::HL_RY, FPSRGunMotionCurves::HL_RR);
	SampleChannelTrack(StudioData->RightHandKeys,
		FPSRGunMotionCurves::HR_TX, FPSRGunMotionCurves::HR_TY, FPSRGunMotionCurves::HR_TZ,
		FPSRGunMotionCurves::HR_RP, FPSRGunMotionCurves::HR_RY, FPSRGunMotionCurves::HR_RR);

	// Blend ramps from the attach spans (§2-2, ±0.1s default).
	constexpr float DefaultBlendTimeSeconds = 0.1f;
	TArray<FRichCurveKey>& HLBlend = OutCurveSamples.FindOrAdd(FPSRGunMotionCurves::HL_Blend);
	SampleAttachBlendCurve(StudioData->LeftHandAttachSpans, NumFrames, Fps, DefaultBlendTimeSeconds, HLBlend);
	TArray<FRichCurveKey>& HRBlend = OutCurveSamples.FindOrAdd(FPSRGunMotionCurves::HR_Blend);
	SampleAttachBlendCurve(StudioData->RightHandAttachSpans, NumFrames, Fps, DefaultBlendTimeSeconds, HRBlend);

	// Part channels — key = stable attach-socket id (FFPSRWeaponPartAttachment::Socket), name via MakePartCurveNames
	// (§7 "커브명 리터럴 금지").
	for (const TPair<FName, FFPSRStudioTrack>& PartEntry : StudioData->PartTracks)
	{
		if (PartEntry.Key.IsNone())
		{
			continue;
		}
		const FPSRGunMotionCurves::FPartCurveNames Names = FPSRGunMotionCurves::MakePartCurveNames(PartEntry.Key);
		SampleChannelTrack(PartEntry.Value, Names.TX, Names.TY, Names.TZ, Names.RP, Names.RY, Names.RR);
	}
}

bool FPSRGunMotionStudioBaker::WriteCurveSamplesToSequenceBase(UAnimSequenceBase* Target,
	const TMap<FName, TArray<FRichCurveKey>>& CurveSamples, const FText& TransactionText, FText& OutError)
{
	if (!Target)
	{
		OutError = LOCTEXT("NoTarget", "커브를 쓸 대상(클립/몽타주)이 없습니다.");
		return false;
	}
	IAnimationDataModel* Model = Target->GetDataModel();
	if (!Model)
	{
		OutError = LOCTEXT("NoDataModel", "애니메이션 데이터 모델을 가져오지 못했습니다.");
		return false;
	}

	IAnimationDataController& Controller = Target->GetController();
	IAnimationDataController::FScopedBracket Bracket(Controller, TransactionText);
	Target->Modify();

	for (const TPair<FName, TArray<FRichCurveKey>>& Entry : CurveSamples)
	{
		const FAnimationCurveIdentifier CurveId(Entry.Key, ERawCurveTrackTypes::RCT_Float);
		if (Entry.Value.Num() == 0)
		{
			// §7 "부재=0" — no dead curves left lying around.
			if (Model->FindFloatCurve(CurveId))
			{
				Controller.RemoveCurve(CurveId);
			}
			continue;
		}

		if (!Model->FindFloatCurve(CurveId))
		{
			Controller.AddCurve(CurveId, AACF_DefaultCurve);
		}
		Controller.SetCurveKeys(CurveId, Entry.Value);
	}

	Target->MarkPackageDirty();
	return true;
}

bool FPSRGunMotionStudioBaker::BakeCurveChannels(UAnimSequence* Seq, UFPSRGunMotionStudioData* StudioData,
	FText& OutError, TArray<FString>* OutMontageReport)
{
	if (!Seq)
	{
		OutError = LOCTEXT("NoSequence", "애니메이션 시퀀스가 없습니다.");
		return false;
	}
	if (!StudioData)
	{
		OutError = LOCTEXT("NoStudioData", "저작 데이터(UFPSRGunMotionStudioData)가 없습니다.");
		return false;
	}
	IAnimationDataModel* Model = Seq->GetDataModel();
	if (!Model)
	{
		OutError = LOCTEXT("NoDataModel", "애니메이션 데이터 모델을 가져오지 못했습니다.");
		return false;
	}

	// §2-3 reduction, written back onto StudioData for the session to persist (once per bake — cheap, deterministic).
	StudioData->LeftHandAttachPartSocket = ReduceAttachSpansToRepresentativeSocket(StudioData->LeftHandAttachSpans);
	StudioData->RightHandAttachPartSocket = ReduceAttachSpansToRepresentativeSocket(StudioData->RightHandAttachSpans);

	const int32 NumFrames = Model->GetNumberOfFrames();
	const double Fps = Model->GetFrameRate().AsDecimal();

	TMap<FName, TArray<FRichCurveKey>> CurveSamples;
	BuildCurveSampleMap(StudioData, NumFrames, Fps, CurveSamples);

	if (!WriteCurveSamplesToSequenceBase(Seq, CurveSamples, LOCTEXT("BakeCurveChannelsBracket", "FPSR Gun Motion: Bake Curve Channels"), OutError))
	{
		return false;
	}

	// A17 automation (Docs/Troubleshooting.md A17): scan referencer montages for a single-segment, PlayRate=1,
	// start=0 reference to Seq and write the SAME sample map into that montage's own curve (the runtime path reads
	// the montage's curve, not the segment clip's, once the clip plays inside a montage).
	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	TArray<FName> ReferencerPackages;
	AssetRegistry.GetReferencers(Seq->GetOutermost()->GetFName(), ReferencerPackages);

	for (const FName& PackageName : ReferencerPackages)
	{
		TArray<FAssetData> AssetsInPackage;
		AssetRegistry.GetAssetsByPackageName(PackageName, AssetsInPackage);
		for (const FAssetData& AssetData : AssetsInPackage)
		{
			if (!AssetData.IsInstanceOf(UAnimMontage::StaticClass()))
			{
				continue;
			}
			UAnimMontage* Montage = Cast<UAnimMontage>(AssetData.GetAsset());
			if (!Montage)
			{
				continue;
			}

			const FAnimSegment* MatchingSegment = nullptr;
			int32 MatchCount = 0;
			for (const FSlotAnimationTrack& SlotTrack : Montage->SlotAnimTracks)
			{
				for (const FAnimSegment& Segment : SlotTrack.AnimTrack.AnimSegments)
				{
					if (Segment.GetAnimReference() == Seq)
					{
						MatchingSegment = &Segment;
						++MatchCount;
					}
				}
			}

			if (MatchCount == 0)
			{
				continue; // Referenced, but not as a slot segment (e.g. a notify) — no place to hang a curve.
			}
			if (MatchCount > 1)
			{
				if (OutMontageReport)
				{
					OutMontageReport->Add(FString::Printf(TEXT("스킵: %s — 이 클립을 여러 세그먼트에서 참조(다중 세그먼트 미지원)"), *Montage->GetName()));
				}
				continue;
			}
			if (!FMath::IsNearlyEqual(MatchingSegment->AnimPlayRate, 1.0f) || !FMath::IsNearlyZero(MatchingSegment->StartPos) || !FMath::IsNearlyZero(MatchingSegment->AnimStartTime))
			{
				if (OutMontageReport)
				{
					OutMontageReport->Add(FString::Printf(TEXT("스킵: %s — PlayRate/시작 오프셋이 항등이 아님(시간 매핑 불일치)"), *Montage->GetName()));
				}
				continue;
			}

			FText MontageError;
			if (WriteCurveSamplesToSequenceBase(Montage, CurveSamples, LOCTEXT("BakeCurveChannelsMontageBracket", "FPSR Gun Motion: Bake Curve Channels (Montage)"), MontageError))
			{
				if (OutMontageReport)
				{
					OutMontageReport->Add(FString::Printf(TEXT("기록: %s"), *Montage->GetName()));
				}
			}
			else if (OutMontageReport)
			{
				OutMontageReport->Add(FString::Printf(TEXT("스킵: %s — %s"), *Montage->GetName(), *MontageError.ToString()));
			}
		}
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
