// Copyright Epic Games, Inc. All Rights Reserved.

#include "GunMotion/FPSRGunMotionBaker.h"

#include "GunMotion/FPSRGunMotionSettings.h"
#include "Anim/FPSRGunMotionAuthoringData.h"
#include "Anim/FPSRGunMotionCurveNames.h"
#include "Weapon/FPSRWeaponDataAsset.h"   // FFPSRWeaponPartAttachment::Socket — PartTracks 키와 같은 안정 id

#include "Animation/AnimSequence.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "AnimPose.h"

#include "Engine/Blueprint.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetRegistry/AssetData.h"

#define LOCTEXT_NAMESPACE "FPSRGunMotionBaker"

namespace
{
	// P(§3-2)의 기준 본 — RightChainBones 처럼 설정으로 뺄 대상이 아니라, 이 저작 모델 자체가 "총모션은
	// lowerarm_r 의 컴포넌트 공간 회전을 기준으로 grip 본 델타를 얹는다"고 못박은 상수다(스펙 §3-2 원문 그대로).
	static const FName GLowerArmBoneName(TEXT("lowerarm_r"));
}

bool FPSRGunMotionBaker::ComputeBoneBaseAtFrame0(UAnimSequence* Seq, FName BoneName, FTransform& OutBase, FText& OutError)
{
	if (!Seq)
	{
		OutError = LOCTEXT("NoSequence", "애니메이션 시퀀스가 없습니다.");
		return false;
	}

	// ① RetrieveAdditiveAsFullPose=false → delta(에디티브 저장값 그대로).
	FAnimPoseEvaluationOptions DeltaOptions;
	DeltaOptions.bRetrieveAdditiveAsFullPose = false;
	FAnimPose DeltaPose;
	UAnimPoseExtensions::GetAnimPoseAtTime(Seq, 0.0, DeltaOptions, DeltaPose);
	if (!UAnimPoseExtensions::IsValid(DeltaPose))
	{
		OutError = LOCTEXT("DeltaPoseInvalid", "delta 포즈 평가에 실패했습니다.");
		return false;
	}
	const FTransform DeltaLocal = UAnimPoseExtensions::GetBonePose(DeltaPose, BoneName, EAnimPoseSpaces::Local);

	// ② RetrieveAdditiveAsFullPose=true(기본값) → raw(베이스에 얹힌 풀 포즈).
	FAnimPoseEvaluationOptions RawOptions;
	FAnimPose RawPose;
	UAnimPoseExtensions::GetAnimPoseAtTime(Seq, 0.0, RawOptions, RawPose);
	if (!UAnimPoseExtensions::IsValid(RawPose))
	{
		OutError = LOCTEXT("RawPoseInvalid", "raw 포즈 평가에 실패했습니다.");
		return false;
	}
	const FTransform RawLocal = UAnimPoseExtensions::GetBonePose(RawPose, BoneName, EAnimPoseSpaces::Local);

	// base = delta⁻¹∘raw 역산(FAnimationRuntime::ConvertTransformToAdditive 의 역, 스펙 §3-1).
	FQuat BaseRot = DeltaLocal.GetRotation().Inverse() * RawLocal.GetRotation();
	BaseRot.Normalize();

	const FVector BaseLoc = RawLocal.GetTranslation() - DeltaLocal.GetTranslation();

	const FVector DeltaScale = DeltaLocal.GetScale3D();
	const FVector RawScale = RawLocal.GetScale3D();
	FVector BaseScale;
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		const float Denom = DeltaScale[Axis] + 1.0f;
		BaseScale[Axis] = (FMath::Abs(Denom) < 1e-8f) ? 1.0f : (RawScale[Axis] / Denom);
	}

	OutBase = FTransform(BaseRot, BaseLoc, BaseScale);
	return true;
}

bool FPSRGunMotionBaker::SanitizeRightChain(UAnimSequence* Seq, const TArray<FName>& Bones, FText& OutError)
{
	if (!Seq)
	{
		OutError = LOCTEXT("NoSequence", "애니메이션 시퀀스가 없습니다.");
		return false;
	}
	if (!Seq->IsValidAdditive())
	{
		OutError = LOCTEXT("NotAdditive", "애디티브 클립이 아닙니다 — 총 고정화는 애디티브 클립에서만 동작합니다.");
		return false;
	}

	IAnimationDataModel* Model = Seq->GetDataModel();
	if (!Model)
	{
		OutError = LOCTEXT("NoDataModel", "애니메이션 데이터 모델을 가져오지 못했습니다.");
		return false;
	}

	const int32 NumFrames = Model->GetNumberOfFrames();
	const int32 NumKeys = NumFrames + 1;

	IAnimationDataController& Controller = Seq->GetController();
	IAnimationDataController::FScopedBracket Bracket(Controller, LOCTEXT("SanitizeBracket", "FPSR Gun Motion: Sanitize Right Chain"));

	Seq->Modify();

	for (const FName& Bone : Bones)
	{
		FTransform Base;
		if (!ComputeBoneBaseAtFrame0(Seq, Bone, Base, OutError))
		{
			return false;
		}

		if (!Model->IsValidBoneTrackName(Bone))
		{
			Controller.AddBoneCurve(Bone);
		}

		TArray<FVector> PosKeys; PosKeys.Init(Base.GetLocation(), NumKeys);
		TArray<FQuat> RotKeys; RotKeys.Init(Base.GetRotation(), NumKeys);
		TArray<FVector> ScaleKeys; ScaleKeys.Init(Base.GetScale3D(), NumKeys);

		if (!Controller.SetBoneTrackKeys(Bone, PosKeys, RotKeys, ScaleKeys))
		{
			OutError = FText::Format(LOCTEXT("SetKeysFailed", "본 '{0}' 트랙 쓰기에 실패했습니다."), FText::FromName(Bone));
			return false;
		}
	}

	UFPSRGunMotionAuthoringData* AuthData = Seq->GetAssetUserData<UFPSRGunMotionAuthoringData>();
	if (!AuthData)
	{
		AuthData = NewObject<UFPSRGunMotionAuthoringData>(Seq);
		Seq->AddAssetUserData(AuthData);
	}
	AuthData->Modify();
	AuthData->bSanitized = true;

	Seq->MarkPackageDirty();
	return true;
}

void FPSRGunMotionBaker::EvalKeys(const TArray<FFPSRGunMotionKey>& SortedKeys, float Time, FVector& OutCamOffset, FQuat& OutCamRotation)
{
	if (SortedKeys.Num() == 0)
	{
		OutCamOffset = FVector::ZeroVector;
		OutCamRotation = FQuat::Identity;
		return;
	}
	if (Time <= SortedKeys[0].Time)
	{
		OutCamOffset = SortedKeys[0].CamOffset;
		OutCamRotation = FQuat(SortedKeys[0].CamRotation);
		return;
	}
	if (Time >= SortedKeys.Last().Time)
	{
		OutCamOffset = SortedKeys.Last().CamOffset;
		OutCamRotation = FQuat(SortedKeys.Last().CamRotation);
		return;
	}

	for (int32 Index = 0; Index < SortedKeys.Num() - 1; ++Index)
	{
		const FFPSRGunMotionKey& A = SortedKeys[Index];
		const FFPSRGunMotionKey& B = SortedKeys[Index + 1];
		if (Time >= A.Time && Time <= B.Time)
		{
			const float Span = B.Time - A.Time;
			const float Raw = (Span > UE_KINDA_SMALL_NUMBER) ? (Time - A.Time) / Span : 0.0f;
			const float Alpha = Raw * Raw * (3.0f - 2.0f * Raw); // smoothstep, 스펙 §3-3
			OutCamOffset = FMath::Lerp(A.CamOffset, B.CamOffset, Alpha);
			OutCamRotation = FQuat::Slerp(FQuat(A.CamRotation), FQuat(B.CamRotation), Alpha);
			return;
		}
	}

	// 위 두 경계 검사(첫 키 이전 / 마지막 키 이후)로 인해 도달하지 않는다 — 방어적 폴백.
	OutCamOffset = SortedKeys.Last().CamOffset;
	OutCamRotation = FQuat(SortedKeys.Last().CamRotation);
}

void FPSRGunMotionBaker::EvalChannelKeys(const TArray<FFPSRGunMotionChannelKey>& SortedKeys, float Time, FVector& OutLoc, FQuat& OutRot)
{
	// §20-1: EvalKeys(§3-3)를 채널 무관 공용 함수로 일반화한 것 — 로직은 축자 동일, 필드 이름만 Loc/Rot.
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
		const FFPSRGunMotionChannelKey& A = SortedKeys[Index];
		const FFPSRGunMotionChannelKey& B = SortedKeys[Index + 1];
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

	OutLoc = SortedKeys.Last().Loc;
	OutRot = FQuat(SortedKeys.Last().Rot);
}

float FPSRGunMotionBaker::EvalScalarKeys(const TArray<FFPSRGunMotionScalarKey>& SortedKeys, float Time)
{
	if (SortedKeys.Num() == 0)
	{
		return 0.0f;
	}
	if (Time <= SortedKeys[0].Time)
	{
		return SortedKeys[0].Value;
	}
	if (Time >= SortedKeys.Last().Time)
	{
		return SortedKeys.Last().Value;
	}

	for (int32 Index = 0; Index < SortedKeys.Num() - 1; ++Index)
	{
		const FFPSRGunMotionScalarKey& A = SortedKeys[Index];
		const FFPSRGunMotionScalarKey& B = SortedKeys[Index + 1];
		if (Time >= A.Time && Time <= B.Time)
		{
			const float Span = B.Time - A.Time;
			const float Raw = (Span > UE_KINDA_SMALL_NUMBER) ? (Time - A.Time) / Span : 0.0f;
			const float Alpha = Raw * Raw * (3.0f - 2.0f * Raw); // smoothstep, §3-3
			return FMath::Lerp(A.Value, B.Value, Alpha);
		}
	}
	return SortedKeys.Last().Value;
}

bool FPSRGunMotionBaker::GetCamToCompRotation(FQuat& OutM, FText& OutError)
{
	const UFPSRGunMotionSettings* Settings = GetDefault<UFPSRGunMotionSettings>();
	if (!Settings || Settings->TargetCharacterBP.IsNull())
	{
		OutError = LOCTEXT("NoTargetCharacterBP", "프로젝트 설정 > FPSR > FPSR Gun Motion 에 '대상 캐릭터 BP'가 비어 있습니다.");
		return false;
	}

	UBlueprint* BP = Settings->TargetCharacterBP.LoadSynchronous();
	UClass* GeneratedClass = BP ? BP->GeneratedClass.Get() : nullptr;
	AActor* CDO = GeneratedClass ? GeneratedClass->GetDefaultObject<AActor>() : nullptr;
	if (!CDO)
	{
		OutError = LOCTEXT("NoCDO", "대상 캐릭터 BP 의 CDO 를 가져오지 못했습니다.");
		return false;
	}

	TArray<USceneComponent*> Components;
	CDO->GetComponents<USceneComponent>(Components);

	USceneComponent* ArmsComp = nullptr;
	USceneComponent* CamComp = nullptr;
	for (USceneComponent* Comp : Components)
	{
		if (!Comp)
		{
			continue;
		}
		if (Comp->GetFName() == Settings->ArmsComponentName)
		{
			ArmsComp = Comp;
		}
		if (Comp->GetFName() == Settings->CameraComponentName)
		{
			CamComp = Comp;
		}
	}
	if (!ArmsComp || !CamComp)
	{
		OutError = FText::Format(
			LOCTEXT("ComponentsNotFound", "대상 캐릭터 BP 에서 컴포넌트 '{0}' / '{1}' 를 찾지 못했습니다."),
			FText::FromName(Settings->ArmsComponentName), FText::FromName(Settings->CameraComponentName));
		return false;
	}

	// 컴포넌트 자신에서 AttachParent 를 따라 루트까지 걸어 올라가며 GetRelativeTransform() (부모 기준 로컬
	// 트랜스폼)을 오른쪽으로 계속 곱한다 — USceneComponent::CalcNewComponentToWorld_GeneralCase 의
	// "NewRelativeTransform * ParentToWorld" 재귀 규칙을 "World" 대신 "액터 루트"로 바꿔 그대로 적용한 것.
	// CDO 는 등록되지 않아 ComponentToWorld 캐시가 의미 없으므로 GetComponentTransform() 대신 이 방식을 쓴다.
	auto ComputeRootRelativeRotation = [](USceneComponent* Comp) -> FQuat
	{
		FTransform Accum = FTransform::Identity;
		TSet<USceneComponent*> Visited;
		USceneComponent* Cur = Comp;
		while (Cur && !Visited.Contains(Cur))
		{
			Visited.Add(Cur);
			Accum = Accum * Cur->GetRelativeTransform();
			Cur = Cur->GetAttachParent();
		}
		FQuat Result = Accum.GetRotation();
		Result.Normalize();
		return Result;
	};

	const FQuat ArmsRot = ComputeRootRelativeRotation(ArmsComp);
	const FQuat CamRot = ComputeRootRelativeRotation(CamComp);

	FQuat M = ArmsRot.Inverse() * CamRot;
	M.Normalize();
	OutM = M;
	return true;
}

bool FPSRGunMotionBaker::BakeGunMotion(UAnimSequence* Seq, const TArray<FFPSRGunMotionKey>& Keys, FText& OutError)
{
	if (!Seq)
	{
		OutError = LOCTEXT("NoSequence", "애니메이션 시퀀스가 없습니다.");
		return false;
	}

	const UFPSRGunMotionAuthoringData* AuthData = Seq->GetAssetUserData<UFPSRGunMotionAuthoringData>();
	if (!AuthData || !AuthData->bSanitized)
	{
		OutError = LOCTEXT("NotSanitized", "먼저 [총 고정화]를 실행해야 합니다.");
		return false;
	}

	IAnimationDataModel* Model = Seq->GetDataModel();
	if (!Model)
	{
		OutError = LOCTEXT("NoDataModel", "애니메이션 데이터 모델을 가져오지 못했습니다.");
		return false;
	}

	const UFPSRGunMotionSettings* Settings = GetDefault<UFPSRGunMotionSettings>();
	const FName GripBone = Settings ? Settings->GripSourceBone : FName(TEXT("hand_r"));

	FQuat M;
	if (!GetCamToCompRotation(M, OutError))
	{
		return false;
	}
	const FQuat MInv = M.Inverse();

	// P = Sanitize 된 클립 raw full pose(t=0) 에서 lowerarm_r 의 컴포넌트(World) 공간 회전(§3-2).
	FAnimPoseEvaluationOptions RawFullOptions; // 기본값 bRetrieveAdditiveAsFullPose=true 그대로 사용.
	FAnimPose RawFullPoseAtZero;
	UAnimPoseExtensions::GetAnimPoseAtTime(Seq, 0.0, RawFullOptions, RawFullPoseAtZero);
	if (!UAnimPoseExtensions::IsValid(RawFullPoseAtZero))
	{
		OutError = LOCTEXT("RawFullPoseInvalid", "raw full pose 평가에 실패했습니다.");
		return false;
	}
	const FTransform LowerArmWorld = UAnimPoseExtensions::GetBonePose(RawFullPoseAtZero, GLowerArmBoneName, EAnimPoseSpaces::World);
	const FQuat P = LowerArmWorld.GetRotation();
	const FQuat PInv = P.Inverse();

	FTransform Base;
	if (!ComputeBoneBaseAtFrame0(Seq, GripBone, Base, OutError))
	{
		return false;
	}

	TArray<FFPSRGunMotionKey> SortedKeys = Keys;
	SortedKeys.Sort([](const FFPSRGunMotionKey& A, const FFPSRGunMotionKey& B) { return A.Time < B.Time; });

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
		EvalKeys(SortedKeys, Time, OcamOffset, OcamRot);

		const FVector OcompTranslation = M.RotateVector(OcamOffset);
		FQuat OcompRot = M * OcamRot * MInv;
		OcompRot.Normalize();

		FQuat DRot = PInv * OcompRot * P;
		DRot.Normalize();
		const FVector DTranslation = PInv.RotateVector(OcompTranslation);

		FQuat ClipRotValue = DRot * Base.GetRotation();
		ClipRotValue.Normalize();

		ClipRot.Add(ClipRotValue);
		ClipPos.Add(Base.GetLocation() + DTranslation);
		ClipScale.Add(Base.GetScale3D());
	}

	IAnimationDataController& Controller = Seq->GetController();
	IAnimationDataController::FScopedBracket Bracket(Controller, LOCTEXT("BakeBracket", "FPSR Gun Motion: Bake"));

	Seq->Modify();
	if (!Model->IsValidBoneTrackName(GripBone))
	{
		Controller.AddBoneCurve(GripBone);
	}
	if (!Controller.SetBoneTrackKeys(GripBone, ClipPos, ClipRot, ClipScale))
	{
		OutError = FText::Format(LOCTEXT("SetKeysFailed", "본 '{0}' 트랙 쓰기에 실패했습니다."), FText::FromName(GripBone));
		return false;
	}

	Seq->MarkPackageDirty();
	return true;
}

// ---------------------------------------------------------------------------------------------------------------
// v3 §20-2: 커브 채널 베이크(본 트랙 대체) — 클립 + (지원 가능한) 참조 몽타주 양쪽에 굽는다(A17 자동화).
// ---------------------------------------------------------------------------------------------------------------

void FPSRGunMotionBaker::BuildCurveSampleMap(const UFPSRGunMotionAuthoringData& Data, int32 NumFrames, double Fps, TMap<FName, TArray<FRichCurveKey>>& OutCurveSamples)
{
	OutCurveSamples.Reset();

	// 채널(loc/rot) 트랙 하나 → 6개 축 커브. 키 0개 = 6개 커브 전부 빈 배열로 등록(호출자가 삭제로 해석, §18 "부재=0").
	auto SampleChannelTrack = [&OutCurveSamples, NumFrames, Fps](const FFPSRGunMotionChannelTrack& Track,
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

		TArray<FFPSRGunMotionChannelKey> Sorted = Track.Keys;
		Sorted.Sort([](const FFPSRGunMotionChannelKey& A, const FFPSRGunMotionChannelKey& B) { return A.Time < B.Time; });

		for (int32 FrameIndex = 0; FrameIndex <= NumFrames; ++FrameIndex)
		{
			const float Time = (Fps > 0.0) ? static_cast<float>(FrameIndex / Fps) : 0.0f;
			FVector Loc;
			FQuat RotQuat;
			EvalChannelKeys(Sorted, Time, Loc, RotQuat);
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

	// 스칼라 채널(Blend) 하나 → 커브 하나.
	auto SampleScalarTrack = [&OutCurveSamples, NumFrames, Fps](const TArray<FFPSRGunMotionScalarKey>& Keys, FName CurveName)
	{
		TArray<FRichCurveKey>& Out = OutCurveSamples.FindOrAdd(CurveName);
		if (Keys.Num() == 0)
		{
			return;
		}
		TArray<FFPSRGunMotionScalarKey> Sorted = Keys;
		Sorted.Sort([](const FFPSRGunMotionScalarKey& A, const FFPSRGunMotionScalarKey& B) { return A.Time < B.Time; });
		for (int32 FrameIndex = 0; FrameIndex <= NumFrames; ++FrameIndex)
		{
			const float Time = (Fps > 0.0) ? static_cast<float>(FrameIndex / Fps) : 0.0f;
			FRichCurveKey Key(Time, EvalScalarKeys(Sorted, Time));
			Key.InterpMode = RCIM_Linear;
			Out.Add(Key);
		}
	};

	SampleChannelTrack(Data.GunTrack,
		FPSRGunMotionCurveNames::Gun_TX, FPSRGunMotionCurveNames::Gun_TY, FPSRGunMotionCurveNames::Gun_TZ,
		FPSRGunMotionCurveNames::Gun_RP, FPSRGunMotionCurveNames::Gun_RY, FPSRGunMotionCurveNames::Gun_RR);
	SampleChannelTrack(Data.LeftHandTrack,
		FPSRGunMotionCurveNames::HL_TX, FPSRGunMotionCurveNames::HL_TY, FPSRGunMotionCurveNames::HL_TZ,
		FPSRGunMotionCurveNames::HL_RP, FPSRGunMotionCurveNames::HL_RY, FPSRGunMotionCurveNames::HL_RR);
	SampleChannelTrack(Data.RightHandTrack,
		FPSRGunMotionCurveNames::HR_TX, FPSRGunMotionCurveNames::HR_TY, FPSRGunMotionCurveNames::HR_TZ,
		FPSRGunMotionCurveNames::HR_RP, FPSRGunMotionCurveNames::HR_RY, FPSRGunMotionCurveNames::HR_RR);
	SampleScalarTrack(Data.LeftHandBlendKeys, FPSRGunMotionCurveNames::HL_Blend);
	SampleScalarTrack(Data.RightHandBlendKeys, FPSRGunMotionCurveNames::HR_Blend);

	// 파츠 채널 — 키 = 안정 부착소켓 id(FFPSRWeaponPartAttachment::Socket), 이름은 MakePartCurveNames 로 조립한다
	// (§20-2 "커브명은 전부 FPSRGunMotionCurveNames 상수 + MakePartCurveNames — 리터럴 중복 금지").
	for (const TPair<FName, FFPSRGunMotionChannelTrack>& PartEntry : Data.PartTracks)
	{
		if (PartEntry.Key.IsNone())
		{
			continue; // 안정 id 없는 항목은 커브 이름을 지을 수 없다.
		}
		const FPSRGunMotionCurveNames::FPartCurveNames Names = FPSRGunMotionCurveNames::MakePartCurveNames(PartEntry.Key);
		SampleChannelTrack(PartEntry.Value, Names.TX, Names.TY, Names.TZ, Names.RP, Names.RY, Names.RR);
	}
}

bool FPSRGunMotionBaker::WriteCurveSamplesToSequenceBase(UAnimSequenceBase* Target, const TMap<FName, TArray<FRichCurveKey>>& CurveSamples, const FText& TransactionText, FText& OutError)
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
			// §18 "부재=0 규약" — 죽은 커브 잔존 금지(있으면 지운다, 없으면 무동작).
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

bool FPSRGunMotionBaker::BakeCurveChannelsClipOnly(UAnimSequence* Seq, const UFPSRGunMotionAuthoringData& Data, FText& OutError)
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

	TMap<FName, TArray<FRichCurveKey>> CurveSamples;
	BuildCurveSampleMap(Data, Model->GetNumberOfFrames(), Model->GetFrameRate().AsDecimal(), CurveSamples);

	return WriteCurveSamplesToSequenceBase(Seq, CurveSamples, LOCTEXT("BakeCurveChannelsBracket", "FPSR Gun Motion: Bake Curve Channels"), OutError);
}

bool FPSRGunMotionBaker::BakeCurveChannels(UAnimSequence* Seq, const UFPSRGunMotionAuthoringData& Data, FText& OutError, TArray<FString>* OutMontageReport)
{
	if (!BakeCurveChannelsClipOnly(Seq, Data, OutError))
	{
		return false;
	}

	// 몽타주 스캔에서 클립과 같은 샘플을 다시 만든다 — BuildCurveSampleMap 은 결정적이라(같은 Data·프레임 격자)
	// 클립에 쓴 것과 자릿수까지 같은 키가 몽타주에 실린다(A17 "값이 갈라지면 두 경로가 다르게 논다" 방지).
	IAnimationDataModel* Model = Seq->GetDataModel();
	TMap<FName, TArray<FRichCurveKey>> CurveSamples;
	BuildCurveSampleMap(Data, Model->GetNumberOfFrames(), Model->GetFrameRate().AsDecimal(), CurveSamples);

	// A17 자동화(Docs/Troubleshooting.md A17): 이 클립을 세그먼트로 무는 UAnimMontage 를 찾아 같은 커브를 몽타주에도
	// 기록한다 — 정식 재생 경로는 세그먼트 클립의 커브를 GetCurveValue 까지 전달하지 않으므로, 몽타주 자신의
	// 커브가 없으면 정식 재생에서 §18 채널이 조용히 무시된다.
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

			// 이 클립을 참조하는 세그먼트를 슬롯 트랙 전체에서 찾는다(§20-2 "지원 = 세그먼트 1개·PlayRate 1·시작 0").
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
				continue; // 이 몽타주의 이 클립 참조는 세그먼트가 아니다(노티파이 등) — 커브를 실을 자리가 없다.
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
