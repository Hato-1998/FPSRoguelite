// Copyright Epic Games, Inc. All Rights Reserved.

#include "GunMotion/FPSRGunMotionBaker.h"

#include "GunMotion/FPSRGunMotionSettings.h"
#include "Anim/FPSRGunMotionAuthoringData.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "AnimPose.h"

#include "Engine/Blueprint.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"

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

#undef LOCTEXT_NAMESPACE
