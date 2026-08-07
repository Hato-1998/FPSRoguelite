// Copyright Epic Games, Inc. All Rights Reserved.

#include "GunMotion/FPSRGunMotionViewportClient.h"

#include "GunMotion/SFPSRGunMotionViewport.h"
#include "GunMotion/FPSRGunMotionSettings.h"
#include "GunMotion/FPSRGunMotionBaker.h"
#include "Anim/FPSRGunMotionAuthoringData.h"
#include "Assembler/FPSRWeaponAssemblerHelpers.h"   // TickPreviewWorldOnce/DeriveFOVForAspect — 순수 유틸, 프리뷰 씬 프레이밍 전반이 공유
#include "Hero/FPSRCharacter.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/DebugSkelMeshComponent.h"   // 애디티브 기준 포즈를 까는 UAnimPreviewInstance 를 다는 컴포넌트
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Blueprint.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "PreviewScene.h"

namespace
{
	/** §7 카메라 — FPSRWeaponAssemblerHelpers::ReadFirstPersonSetup 과 같은 패턴(CDO 가 아니라 프리뷰 씬에 잠깐
	 *  스폰해서 읽고 파괴)이지만, 설정 소스가 UFPSRGunMotionSettings::TargetCharacterBP(TSoftObjectPtr<UBlueprint>)라
	 *  그 함수(TSoftClassPtr<AFPSRCharacter> 를 읽는다)를 그대로 재사용할 수 없어 이 파일에 자체 구현한다. */
	bool ReadGunMotionCameraSetup(UWorld* World, const UFPSRGunMotionSettings* Settings,
		FTransform& OutCameraRelativeToArms, FMinimalViewInfo& OutView, FString& OutIssue)
	{
		if (!Settings || Settings->TargetCharacterBP.IsNull())
		{
			OutIssue = TEXT("프로젝트 설정 > FPSR > FPSR Gun Motion 에 '대상 캐릭터 BP'가 비어 있습니다.");
			return false;
		}

		UBlueprint* BP = Settings->TargetCharacterBP.LoadSynchronous();
		UClass* GeneratedClass = BP ? BP->GeneratedClass.Get() : nullptr;
		if (!GeneratedClass || !World)
		{
			OutIssue = TEXT("대상 캐릭터 BP 를 불러오지 못했습니다.");
			return false;
		}

		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SpawnParams.ObjectFlags |= RF_Transient;
		AFPSRCharacter* Probe = World->SpawnActor<AFPSRCharacter>(GeneratedClass, FTransform::Identity, SpawnParams);
		if (!Probe)
		{
			OutIssue = TEXT("프리뷰 캐릭터를 프리뷰 씬에 스폰하지 못했습니다.");
			return false;
		}

		const bool bOk = Probe->GetFirstPersonViewSetup(OutCameraRelativeToArms, OutView);
		Probe->Destroy();
		if (!bOk)
		{
			OutIssue = TEXT("대상 캐릭터에 1인칭 카메라 또는 1인칭 팔 컴포넌트가 없어 구도를 읽지 못했습니다.");
		}
		return bOk;
	}

	// 화면비 콤보는 생략하고 16:9 고정으로 시작한다(§7).
	constexpr float GFixedAspect = 16.0f / 9.0f;
}

FFPSRGunMotionViewportClient::FFPSRGunMotionViewportClient(FPreviewScene& InPreviewScene, const TSharedRef<SFPSRGunMotionViewport>& InViewport)
	: FEditorViewportClient(nullptr, &InPreviewScene, StaticCastSharedRef<SEditorViewport>(InViewport))
	, PreviewScene(InPreviewScene)
{
	SetRealtime(true);

	// 종횡비 고정 경로 — FFPSRWeaponAssemblerFPViewportClient 생성자와 동일 근거(그 헤더 클래스 주석 참조):
	// bUseControllingActorViewInfo 가 꺼져 있으면 bConstrainAspectRatio 자체가 읽히지 않는다.
	bUseControllingActorViewInfo = true;
	ControllingActorAspectRatioAxisConstraint = AspectRatio_MaintainYFOV;

	RebuildArmsAndWeapon();
	RefreshCameraComposition();
}

void FFPSRGunMotionViewportClient::RebuildArmsAndWeapon()
{
	const UFPSRGunMotionSettings* Settings = GetDefault<UFPSRGunMotionSettings>();
	if (!Settings || Settings->TargetCharacterBP.IsNull())
	{
		Issue = TEXT("프로젝트 설정 > FPSR > FPSR Gun Motion 에 '대상 캐릭터 BP'가 비어 있습니다.");
		return;
	}

	UBlueprint* BP = Settings->TargetCharacterBP.LoadSynchronous();
	UClass* GeneratedClass = BP ? BP->GeneratedClass.Get() : nullptr;
	AActor* CDO = GeneratedClass ? GeneratedClass->GetDefaultObject<AActor>() : nullptr;
	if (!CDO)
	{
		Issue = TEXT("대상 캐릭터 BP 의 CDO 를 가져오지 못했습니다.");
		return;
	}

	USkeletalMeshComponent* ArmsSource = nullptr;
	TArray<USkeletalMeshComponent*> Comps;
	CDO->GetComponents<USkeletalMeshComponent>(Comps);
	for (USkeletalMeshComponent* Comp : Comps)
	{
		if (Comp && Comp->GetFName() == Settings->ArmsComponentName)
		{
			ArmsSource = Comp;
			break;
		}
	}
	USkeletalMesh* ArmsMesh = ArmsSource ? ArmsSource->GetSkeletalMeshAsset() : nullptr;
	if (!ArmsMesh)
	{
		Issue = FString::Printf(TEXT("대상 캐릭터 BP 에서 팔 컴포넌트 '%s' 또는 그 스켈레탈 메시를 찾지 못했습니다."),
			*Settings->ArmsComponentName.ToString());
		return;
	}

	if (WeaponComp)
	{
		PreviewScene.RemoveComponent(WeaponComp);
		WeaponComp = nullptr;
	}
	if (ArmsComp)
	{
		PreviewScene.RemoveComponent(ArmsComp);
		ArmsComp = nullptr;
	}

	// UDebugSkelMeshComponent — 🚨 일반 SkeletalMeshComponent 금지(§7): 애디티브 프리뷰는 UAnimPreviewInstance
	// (bCanProcessAdditiveAnimations) 라야 기준 포즈 위에 올바로 얹힌다.
	ArmsComp = NewObject<UDebugSkelMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);
	ArmsComp->SetSkeletalMeshAsset(ArmsMesh);
	ArmsComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PreviewScene.AddComponent(ArmsComp, FTransform::Identity);

	UStaticMesh* WeaponMesh = Settings->PreviewWeaponMesh.IsNull() ? nullptr : Settings->PreviewWeaponMesh.LoadSynchronous();
	WeaponComp = NewObject<UStaticMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);
	WeaponComp->SetStaticMesh(WeaponMesh);
	WeaponComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PreviewScene.AddComponent(WeaponComp, FTransform::Identity);

	if (ArmsComp->DoesSocketExist(Settings->PreviewWeaponAttachSocket))
	{
		// 게임의 gun-anchor 경로(ik_hand_gun)와 부착 메커니즘은 다르지만 등가다(§7 헤더 주석) — 상대 트랜스폼을
		// 명시적으로 0 으로 둬서 무기가 소켓을 그대로(오프셋 없이) 따라가게 한다.
		WeaponComp->AttachToComponent(ArmsComp, FAttachmentTransformRules::SnapToTargetNotIncludingScale, Settings->PreviewWeaponAttachSocket);
		WeaponComp->SetRelativeTransform(FTransform::Identity);
	}
	else
	{
		Issue = FString::Printf(TEXT("팔 메시에 소켓 '%s' 이 없어 무기를 붙이지 못했습니다."),
			*Settings->PreviewWeaponAttachSocket.ToString());
	}

	ArmsComp->RefreshBoneTransforms();
	Invalidate();
}

void FFPSRGunMotionViewportClient::RefreshCameraComposition()
{
	bHasComposition = false;
	const UFPSRGunMotionSettings* Settings = GetDefault<UFPSRGunMotionSettings>();
	bHasComposition = ReadGunMotionCameraSetup(GetWorld(), Settings, CameraRelativeToArms, CameraView, Issue);
}

void FFPSRGunMotionViewportClient::SetSourceClip(UAnimSequence* SourceClip, const TArray<FFPSRGunMotionKey>& InitialKeys)
{
	Issue.Reset();
	PreviewClip = nullptr;
	bHasGunBase = false;

	if (!ArmsComp)
	{
		RebuildArmsAndWeapon();
	}
	if (!ArmsComp)
	{
		Invalidate();
		return;
	}

	if (!SourceClip)
	{
		ArmsComp->EnablePreview(true, nullptr);
		Invalidate();
		return;
	}

	// transient 패키지에 복제 — AssetUserData(UFPSRGunMotionAuthoringData)는 Instanced UPROPERTY 라 Keys/bSanitized
	// 까지 그대로 복제된다(엔진 소스 확인: AnimationAsset.h `Instanced` 지정 AssetUserData 배열). 원본과 독립적이라
	// 여기서 굽는 것은 원본 클립을 절대 건드리지 않는다(§8 "프리뷰 클립은 커밋과 무관한 스크래치").
	PreviewClip = DuplicateObject<UAnimSequence>(SourceClip, GetTransientPackage());

	ArmsComp->EnablePreview(true, PreviewClip);
	ArmsComp->Play(true);
	ArmsComp->RefreshBoneTransforms();

	RecomputeGunBase();
	RebakePreview(InitialKeys);
}

void FFPSRGunMotionViewportClient::RecomputeGunBase()
{
	bHasGunBase = false;
	if (!PreviewClip || !WeaponComp || !ArmsComp)
	{
		return;
	}

	// §9 베이스라인: 오프셋 0 키 하나로 임시 굽고 그 상태의 무기 월드 트랜스폼을 캐시한다. 우측 체인이 상수라
	// (총 고정화) 시각 무관 상수이므로 여기서 한 번만 계산하면 된다.
	TArray<FFPSRGunMotionKey> ZeroKeys;
	ZeroKeys.AddDefaulted();   // Time=0, CamOffset=Zero, CamRotation=Zero

	FText Error;
	if (!FPSRGunMotionBaker::BakeGunMotion(PreviewClip, ZeroKeys, Error))
	{
		Issue = Error.ToString();
		return;
	}

	ArmsComp->SetPosition(0.0f);
	ArmsComp->RefreshBoneTransforms();
	GunBase = WeaponComp->GetComponentTransform();
	bHasGunBase = true;
}

void FFPSRGunMotionViewportClient::RebakePreview(const TArray<FFPSRGunMotionKey>& Keys)
{
	if (!PreviewClip)
	{
		return;
	}

	FText Error;
	if (!FPSRGunMotionBaker::BakeGunMotion(PreviewClip, Keys, Error))
	{
		Issue = Error.ToString();
		Invalidate();
		return;
	}
	Issue.Reset();

	// §9 마지막 문단: 재굽기 → 컴포넌트 임시 델타 해제(구운 포즈가 대신한다) — 기즈모 드래그로 얹힌 상대 트랜스폼을
	// 버리고 소켓(=구운 hand_r 포즈)이 다시 그대로 무기를 몬다. RebakePreview 는 드래그 도중에는 절대 불리지 않는다
	// (TrackingStopped 이후에만) — 그래서 여기서 무조건 리셋해도 안전하다.
	if (WeaponComp)
	{
		WeaponComp->SetRelativeTransform(FTransform::Identity);
	}
	if (ArmsComp)
	{
		ArmsComp->RefreshBoneTransforms();
	}
	Invalidate();
}

// --- 타임라인(§8) ---------------------------------------------------------------------------------------------

void FFPSRGunMotionViewportClient::SetPreviewPlaying(bool bPlay)
{
	if (!ArmsComp)
	{
		return;
	}
	if (UAnimSingleNodeInstance* Instance = ArmsComp->GetSingleNodeInstance())
	{
		Instance->SetPlaying(bPlay);
	}
	Invalidate();
}

bool FFPSRGunMotionViewportClient::IsPreviewPlaying() const
{
	UAnimSingleNodeInstance* Instance = ArmsComp ? ArmsComp->GetSingleNodeInstance() : nullptr;
	return Instance && Instance->IsPlaying();
}

void FFPSRGunMotionViewportClient::SetPreviewPosition(float Seconds)
{
	if (!ArmsComp)
	{
		return;
	}
	ArmsComp->SetPosition(Seconds);
	ArmsComp->RefreshBoneTransforms();
	Invalidate();
}

float FFPSRGunMotionViewportClient::GetPreviewPosition() const
{
	return ArmsComp ? ArmsComp->GetPosition() : 0.0f;
}

float FFPSRGunMotionViewportClient::GetPreviewLength() const
{
	UAnimSingleNodeInstance* Instance = ArmsComp ? ArmsComp->GetSingleNodeInstance() : nullptr;
	return Instance ? Instance->GetLength() : 0.0f;
}

// --- 기즈모(§9) ------------------------------------------------------------------------------------------------

void FFPSRGunMotionViewportClient::SetWidgetMode(UE::Widget::EWidgetMode InMode)
{
	WidgetMode = InMode;
	Invalidate();
}

FVector FFPSRGunMotionViewportClient::GetWidgetLocation() const
{
	return WeaponComp ? WeaponComp->GetComponentLocation() : FVector::ZeroVector;
}

bool FFPSRGunMotionViewportClient::InputWidgetDelta(FViewport* InViewport, EAxisList::Type CurrentAxis, FVector& Drag, FRotator& Rot, FVector& Scale)
{
	if (CurrentAxis == EAxisList::None || !WeaponComp)
	{
		return FEditorViewportClient::InputWidgetDelta(InViewport, CurrentAxis, Drag, Rot, Scale);
	}

	// 드래그 중: 무기 컴포넌트에 델타를 즉시 반영(§9 시각 피드백). 저장은 TrackingStopped 가 담당.
	WeaponComp->AddWorldOffset(Drag);
	WeaponComp->AddWorldRotation(Rot);
	Invalidate();
	return true;
}

void FFPSRGunMotionViewportClient::TrackingStarted(const FInputEventState& InInputState, bool bIsDraggingWidget, bool bNudge)
{
	Invalidate();
}

void FFPSRGunMotionViewportClient::TrackingStopped()
{
	if (WeaponComp && bHasGunBase)
	{
		// §9 역산식(축자):
		//   CamRot   = 프리뷰 카메라의 월드 회전
		//   O_cam_t  = CamRot⁻¹ ⊗ (GunNow.T − GunBase.T)
		//   O_cam_q  = CamRot⁻¹ * (GunNow.R * GunBase.R⁻¹) * CamRot (정규화)
		const FQuat CamRot = GetViewRotation().Quaternion();
		const FTransform GunNow = WeaponComp->GetComponentTransform();

		const FVector OCamT = CamRot.Inverse().RotateVector(GunNow.GetLocation() - GunBase.GetLocation());
		FQuat OCamQ = CamRot.Inverse() * (GunNow.GetRotation() * GunBase.GetRotation().Inverse()) * CamRot;
		OCamQ.Normalize();

		GizmoCommitDelegate.ExecuteIfBound(GetPreviewPosition(), OCamT, OCamQ.Rotator());
	}
	Invalidate();
}

void FFPSRGunMotionViewportClient::Tick(float DeltaSeconds)
{
	FEditorViewportClient::Tick(DeltaSeconds);

	// 이 탭 전용 씬이라 다른 탭과 공유되지 않지만, TickPreviewWorldOnce 는 월드 포인터를 키로 삼는 범용 문지기라
	// 그대로 재사용해도 안전하다(FPSRWeaponAssemblerHelpers 헤더 주석 참조).
	FPSRWeaponAssemblerHelpers::TickPreviewWorldOnce(PreviewScene.GetWorld(), DeltaSeconds);

	ApplyCameraComposition();
}

void FFPSRGunMotionViewportClient::ApplyCameraComposition()
{
	if (!bHasComposition)
	{
		return;
	}

	// 투영: 종횡비 고정(16:9) + 그 종횡비에 맞게 다시 구한 가로 FOV — FFPSRWeaponAssemblerFPViewportClient::
	// ApplyComposition 과 같은 근거(엔진이 그리는 동안 ControllingActorViewInfo 를 뷰포트 상태로 되쓰므로 매 프레임
	// 다시 넣어야 한다, EditorViewportClient.cpp:1102-1106).
	ControllingActorViewInfo = CameraView;
	ControllingActorViewInfo.AspectRatio = GFixedAspect;
	ControllingActorViewInfo.bConstrainAspectRatio = true;
	const float AppliedFOV = FPSRWeaponAssemblerHelpers::DeriveFOVForAspect(CameraView, GFixedAspect);
	ControllingActorViewInfo.FOV = AppliedFOV;
	ViewFOV = AppliedFOV;

	// 카메라: 팔의 살아 있는 월드에 캐시한 상대값을 곱한다 — 팔 배치가 바뀌어도 구도가 따라간다.
	const FTransform ArmsWorld = ArmsComp ? ArmsComp->GetComponentTransform() : FTransform::Identity;
	const FTransform CameraWorld = CameraRelativeToArms * ArmsWorld;

	// 🚨 여기가 잠금이다 — ControllingActorViewInfo.Location/Rotation 에 넣어도 소용없다(엔진이 읽는 것은 뷰포트
	// 자기 트랜스폼). FFPSRWeaponAssemblerFPViewportClient 헤더 주석과 같은 근거.
	SetViewLocation(CameraWorld.GetLocation());
	SetViewRotation(CameraWorld.GetRotation().Rotator());
}

void FFPSRGunMotionViewportClient::AddReferencedObjects(FReferenceCollector& Collector)
{
	FEditorViewportClient::AddReferencedObjects(Collector);
	Collector.AddReferencedObject(PreviewClip);
}
