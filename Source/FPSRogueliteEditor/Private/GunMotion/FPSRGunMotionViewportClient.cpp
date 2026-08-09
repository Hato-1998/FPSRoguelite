// Copyright Epic Games, Inc. All Rights Reserved.

#include "GunMotion/FPSRGunMotionViewportClient.h"

#include "GunMotion/SFPSRGunMotionViewport.h"
#include "GunMotion/FPSRGunMotionSettings.h"
#include "GunMotion/FPSRGunMotionBaker.h"
#include "Anim/FPSRGunMotionAuthoringData.h"
#include "Weapon/FPSRWeaponDataAsset.h"   // FFPSRWeaponPartAttachment/WeaponParts, LeftHandSocket/RightHandSocket — §20-4 파츠+그립 소스
#include "Assembler/FPSRWeaponAssemblerHelpers.h"   // TickPreviewWorldOnce/DeriveFOVForAspect — 순수 유틸, 프리뷰 씬 프레이밍 전반이 공유
#include "Hero/FPSRCharacter.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/DebugSkelMeshComponent.h"   // 애디티브 기준 포즈를 까는 UAnimPreviewInstance 를 다는 컴포넌트
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SphereComponent.h"   // §20-4 손 IK 타깃 프록시
#include "Engine/Blueprint.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"      // HActor — §20-4 히트 프록시 클릭(어셈블러의 "HActor 계열 패턴")
#include "InputCoreTypes.h"   // EKeys — §12 F 키(FPSRBlockoutPlacementMode.cpp 의 InputKey 오버라이드와 같은 include)
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

	// v3 §20-4: 파츠/손 프록시는 WeaponComp 가 있어야 붙는다 — 팔/무기를 다시 세운 직후 재구성한다.
	RebuildPartsAndHandProxies();

	Invalidate();
}

void FFPSRGunMotionViewportClient::RebuildPartsAndHandProxies()
{
	// 재구성 전에 기존 것부터 정리한다(무기 DA 를 바꿔 다시 열 수도 있으므로 RebuildArmsAndWeapon 이 WeaponComp 를
	// 지우고 다시 만드는 것과 같은 이유 — 낡은 파츠가 새 WeaponComp 없이 씬에 남으면 안 된다).
	for (UStaticMeshComponent* PartComp : PartComps)
	{
		if (PartComp)
		{
			PreviewScene.RemoveComponent(PartComp);
		}
	}
	PartComps.Reset();
	PartSocketIds.Reset();
	PartSocketRelative.Reset();
	PartAuthoredOffset.Reset();
	if (LeftHandProxy)
	{
		PreviewScene.RemoveComponent(LeftHandProxy);
		LeftHandProxy = nullptr;
	}
	if (RightHandProxy)
	{
		PreviewScene.RemoveComponent(RightHandProxy);
		RightHandProxy = nullptr;
	}
	LeftHandGripSocket = NAME_None;
	RightHandGripSocket = NAME_None;

	if (!WeaponComp)
	{
		return;
	}

	const UFPSRGunMotionSettings* Settings = GetDefault<UFPSRGunMotionSettings>();
	const UFPSRWeaponDataAsset* WeaponData = (Settings && !Settings->PreviewWeaponData.IsNull()) ? Settings->PreviewWeaponData.LoadSynchronous() : nullptr;
	if (!WeaponData)
	{
		// 파츠 레인/그립 소켓 없이도 §7 총모션 저작 자체는 동작해야 한다 — 조용히 비워 둔다(경고는 Issue 에 이미
		// 다른 사유가 없을 때만 추가해 기존 사유를 덮지 않는다).
		if (Issue.IsEmpty())
		{
			Issue = TEXT("설정의 'PreviewWeaponData'가 비어 있어 파츠 레인/손 그립 소켓을 만들 수 없습니다(총 채널 저작은 계속됩니다).");
		}
		return;
	}

	// --- 파츠(§20-4 "무기 메시의 Socket+Offset 부착, DA 값 그대로") — 어셈블러 뷰포트 클라이언트의 Init 합성과 같은
	// 관례(Socket 이 있으면 Offset*SocketRel, 없으면 Offset 그대로), 단 WeaponComp 는 스태틱 메시라 소켓이 없으면
	// (Synty 모듈러 바디는 SKEL_LPAMG_<W> 전용 소켓을 안 갖는다) 무기 원점 폴백 + Issue 한 줄로 그친다(저작값은
	// 오프셋이라 조작엔 지장 없음, §20-4 원문).
	for (int32 PartIndex = 0; PartIndex < WeaponData->WeaponParts.Num(); ++PartIndex)
	{
		const FFPSRWeaponPartAttachment& PartDef = WeaponData->WeaponParts[PartIndex];
		if (PartDef.Part.IsNull() || PartDef.Socket.IsNone())
		{
			continue; // §20-3 "Part=null·Socket=None 제외".
		}

		UStaticMesh* PartMesh = PartDef.Part.LoadSynchronous();
		UStaticMeshComponent* PartComp = NewObject<UStaticMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);
		PartComp->SetStaticMesh(PartMesh);
		PartComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		PreviewScene.AddComponent(PartComp, FTransform::Identity);
		PartComp->AttachToComponent(WeaponComp, FAttachmentTransformRules::KeepRelativeTransform);

		FTransform SocketRel = FTransform::Identity;
		if (WeaponComp->DoesSocketExist(PartDef.Socket))
		{
			SocketRel = WeaponComp->GetSocketTransform(PartDef.Socket, RTS_Component);
		}
		else
		{
			Issue = FString::Printf(TEXT("무기 몸통 메시에 파츠 소켓 '%s' 이 없어 원점(+오프셋)에 붙였습니다(저작값은 오프셋이라 조작엔 지장 없음)."), *PartDef.Socket.ToString());
		}
		PartComp->SetRelativeTransform(PartDef.Offset * SocketRel);

		PartComps.Add(PartComp);
		PartSocketIds.Add(PartDef.Socket);
		PartSocketRelative.Add(SocketRel);
		PartAuthoredOffset.Add(PartDef.Offset);
	}

	// --- 손 IK 타깃 프록시(§20-4 "손 IK 타깃 프록시 = 구체 컴포넌트 2개") — 그립 기준점은 캐릭터 그립 캐시와 같은
	// 소스(AFPSRCharacter::ComputeGripInGunFrame 이 읽는 Weapon->LeftHandSocket/RightHandSocket).
	LeftHandGripSocket = WeaponData->LeftHandSocket;
	RightHandGripSocket = WeaponData->RightHandSocket;

	auto MakeHandProxy = [this](FName GripSocket) -> USphereComponent*
	{
		USphereComponent* Proxy = NewObject<USphereComponent>(GetTransientPackage(), NAME_None, RF_Transient);
		Proxy->SetSphereRadius(1.5f);
		Proxy->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Proxy->ShapeColor = FColor::Yellow;
		PreviewScene.AddComponent(Proxy, FTransform::Identity);
		Proxy->AttachToComponent(WeaponComp, FAttachmentTransformRules::KeepRelativeTransform);

		FTransform Base = FTransform::Identity;
		if (!GripSocket.IsNone() && WeaponComp->DoesSocketExist(GripSocket))
		{
			Base = WeaponComp->GetSocketTransform(GripSocket, RTS_Component);
		}
		else if (!GripSocket.IsNone())
		{
			Issue = FString::Printf(TEXT("무기 몸통 메시에 그립 소켓 '%s' 이 없어 무기 원점에 프록시를 놓았습니다(저작값은 오프셋이라 조작엔 지장 없음)."), *GripSocket.ToString());
		}
		Proxy->SetRelativeTransform(Base);
		return Proxy;
	};

	LeftHandProxy = MakeHandProxy(LeftHandGripSocket);
	RightHandProxy = MakeHandProxy(RightHandGripSocket);
}

void FFPSRGunMotionViewportClient::RefreshCameraComposition()
{
	bHasComposition = false;
	const UFPSRGunMotionSettings* Settings = GetDefault<UFPSRGunMotionSettings>();

	// §11 분기: bHasCapturedComposition 이면 [PIE 구도 캡처]가 저장한 실측값을 쓴다(캡처가 실행 중인 PIE 캐릭터에서
	// 읽은 값이라 프로브 스폰의 CDO 저작 배치와 달리 런타임 보정이 반영돼 있다). 아니면 기존 프로브 경로로 폴백
	// (§11 "최초 사용 전 폴백").
	if (Settings && Settings->bHasCapturedComposition)
	{
		CameraRelativeToArms = Settings->CapturedCameraRelativeToArms;

		CameraView = FMinimalViewInfo();
		CameraView.FOV = Settings->CapturedFOV;
		// ApplyCameraComposition 은 DeriveFOVForAspect(CameraView, GFixedAspect) 를 부른다 — 그 함수는 Source==Target
		// 종횡비일 때 축 제약과 무관하게 항등이 된다(대수적으로 SourceFOV 를 그대로 반환). 캡처값은 이미 실제 PIE
		// 화면의 가로 FOV 이므로(원본 종횡비를 저장하지 않는다, §11 스펙 그대로 float 하나만) 재유도할 근거 자체가
		// 없다 — AspectRatio 를 GFixedAspect 와 맞춰서 그 재유도를 항등으로 만들고 CapturedFOV 를 그대로 통과시킨다.
		CameraView.AspectRatio = GFixedAspect;

		Issue.Reset();
		bHasComposition = true;
		return;
	}

	bHasComposition = ReadGunMotionCameraSetup(GetWorld(), Settings, CameraRelativeToArms, CameraView, Issue);
}

void FFPSRGunMotionViewportClient::SetSourceClip(UAnimSequence* SourceClip, const UFPSRGunMotionAuthoringData* InitialData)
{
	Issue.Reset();
	PreviewClip = nullptr;
	bHasGunBase = false;
	bHasCachedCamToComp = false; // 설정(대상 BP/컴포넌트명) 변경은 클립 재선택으로 반영된다.
	LastAppliedAuthoringData.Reset();

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

	// v3 §20-4: WeaponComp 는 이전 클립의 마지막 ApplyChannelEvaluationAtTime 이 남긴 Gun 채널 오프셋을 들고 있을 수
	// 있다(RebuildArmsAndWeapon 을 다시 안 타는 클립 전환 경로) — RecomputeGunBase 가 요구하는 "오프셋 0" 불변식을
	// 명시적으로 복원한 뒤 계산한다(§9 GunBase 는 순수 0 상태에서 재야 한다).
	if (WeaponComp)
	{
		WeaponComp->SetRelativeTransform(FTransform::Identity);
	}
	RecomputeGunBase();
	RebakePreview(InitialData);
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

void FFPSRGunMotionViewportClient::RebakePreview(const UFPSRGunMotionAuthoringData* Data)
{
	if (!PreviewClip)
	{
		return;
	}

	LastAppliedAuthoringData = Data;
	const TArray<FFPSRGunMotionKey>& LegacyKeys = Data ? Data->Keys : TArray<FFPSRGunMotionKey>();

	FText Error;
	if (FPSRGunMotionBaker::BakeGunMotion(PreviewClip, LegacyKeys, Error))
	{
		Issue.Reset();
	}
	else
	{
		// v3 §20-4: 레거시 hand_r 굽기가 막혀도(전형적으로 bSanitized=false) §18 채널 저작/프리뷰는 계속 동작해야
		// 한다 — [새 액션 클립]으로 만든 v3 클립은 애초에 hand_r 트랙이 없으므로(§20-5) 이 실패가 정상 경로다.
		Issue = Error.ToString();
	}

	// v3 §20-4 "transient 프리뷰 클립 재굽기가 커브를 실으므로 미러가 총/손/파츠 전부 진짜로 보여준다" — §18 커브를
	// 프리뷰 클립에도 굽는다. 이게 없으면 PIE 라이브 미러(판정 경로)의 GetCurveValue 가 전부 0 이라 채널이 무동작이다
	// (검증에서 잡은 결함). 몽타주 스캔(A17)은 실제 에셋 [채널 커브 굽기] 전용 — transient 클립은 클립 쓰기만 한다.
	if (Data)
	{
		FText CurveError;
		if (!FPSRGunMotionBaker::BakeCurveChannelsClipOnly(PreviewClip, *Data, CurveError))
		{
			Issue = CurveError.ToString();
		}
	}

	// §9 마지막 문단: 재굽기 → 컴포넌트 임시 델타 해제(구운 포즈가 대신한다) — 기즈모 드래그로 얹힌 상대 트랜스폼을
	// 버리고 소켓(=구운 hand_r 포즈)이 다시 그대로 무기를 몬다. RebakePreview 는 드래그 도중에는 절대 불리지 않는다
	// (TrackingStopped 이후에만) — 그래서 여기서 무조건 리셋해도 안전하다. v3 §20-4: 리셋 후 그 자리에서 곧바로
	// 지금 시각의 §18 채널 값을 직접 적용한다(레거시 v2 클립은 채널이 비어 있으니 결과가 Identity 그대로다 — 회귀 0).
	if (ArmsComp)
	{
		ArmsComp->RefreshBoneTransforms();
	}
	ApplyChannelEvaluationAtTime(Data, GetPreviewPosition());
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
	// v3 §20-4 "뷰포트 포즈 반영 = 평가값 직접 적용" — 스크럽마다 이 시각의 §18 채널 값을 다시 적용한다.
	ApplyChannelEvaluationAtTime(LastAppliedAuthoringData.Get(), Seconds);
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
	const UPrimitiveComponent* Target = GetActiveTargetComponent();
	return Target ? Target->GetComponentLocation() : FVector::ZeroVector;
}

bool FFPSRGunMotionViewportClient::InputWidgetDelta(FViewport* InViewport, EAxisList::Type CurrentAxis, FVector& Drag, FRotator& Rot, FVector& Scale)
{
	UPrimitiveComponent* Target = GetActiveTargetComponent();
	if (CurrentAxis == EAxisList::None || !Target)
	{
		return FEditorViewportClient::InputWidgetDelta(InViewport, CurrentAxis, Drag, Rot, Scale);
	}

	// 드래그 중: 활성 채널 대상 컴포넌트에 델타를 즉시 반영(§9/§20-4 시각 피드백 — 총/손/파츠 공용). 저장은
	// TrackingStopped 가 담당.
	Target->AddWorldOffset(Drag);
	Target->AddWorldRotation(Rot);
	Invalidate();
	return true;
}

void FFPSRGunMotionViewportClient::TrackingStarted(const FInputEventState& InInputState, bool bIsDraggingWidget, bool bNudge)
{
	// v3 §20-4: 손/파츠 채널 역산의 기준 스냅샷(§9 GunBase 를 채널 4종으로 일반화한 것 — Gun 채널은 여전히 GunBase
	// 를 쓰므로 이 스냅샷을 참조하지 않는다, CommitHandOrPartChannel 참조).
	// bIsDraggingWidget 스냅샷: 카메라 궤도/빈 클릭도 같은 트래킹 쌍을 타므로, 이 가드 없이는 TrackingStopped 가
	// 조작 없는 트래킹마다 활성 채널에 무점프 키를 하나씩 쌓는다(bWidgetDragActive 멤버 주석 참조).
	bWidgetDragActive = bIsDraggingWidget;
	if (UPrimitiveComponent* Target = GetActiveTargetComponent())
	{
		DragBaseWorld = Target->GetComponentTransform();
	}
	Invalidate();
}

void FFPSRGunMotionViewportClient::TrackingStopped()
{
	// 위젯 드래그가 아니었으면(카메라 궤도·빈 클릭) 커밋 없음 — TrackingStarted 의 가드 주석 참조.
	const bool bWasWidgetDrag = bWidgetDragActive;
	bWidgetDragActive = false;

	UPrimitiveComponent* Target = GetActiveTargetComponent();
	if (!bWasWidgetDrag || !Target)
	{
		Invalidate();
		return;
	}

	if (FPSRGunMotionClassifyChannel(ActiveChannelId) == EFPSRGunMotionChannelKind::Gun)
	{
		if (WeaponComp && bHasGunBase)
		{
			// §9 역산식(축자, 변경 없음), §12: CamRot 은 자유시점 여부와 무관하게 항상 "잠금 구도"의 카메라 회전이다
			// — 자유시점 뷰 회전(GetViewRotation())을 쓰면 같은 드래그가 뷰마다 다른 키를 낳는다(§12 스펙 원문).
			//   CamRot   = 잠금 구도 카메라의 월드 회전
			//   O_cam_t  = CamRot⁻¹ ⊗ (GunNow.T − GunBase.T)
			//   O_cam_q  = CamRot⁻¹ * (GunNow.R * GunBase.R⁻¹) * CamRot (정규화)
			const FQuat CamRot = GetLockedCompositionCameraRotation();
			const FTransform GunNow = WeaponComp->GetComponentTransform();

			const FVector OCamT = CamRot.Inverse().RotateVector(GunNow.GetLocation() - GunBase.GetLocation());
			FQuat OCamQ = CamRot.Inverse() * (GunNow.GetRotation() * GunBase.GetRotation().Inverse()) * CamRot;
			OCamQ.Normalize();

			ChannelGizmoCommitDelegate.ExecuteIfBound(FPSRGunMotionChannelIds::Gun, GetPreviewPosition(), OCamT, OCamQ.Rotator());
		}
	}
	else
	{
		// v3 §20-4: 손="총 공간" / 파츠="파츠 로컬" — CommitHandOrPartChannel 이 채널 종류에 따라 부모 회전을
		// 스스로 고른다(손=WeaponComp 의 지금 월드 회전, 파츠=그 파츠 자신의 베이스 월드 회전).
		CommitHandOrPartChannel(ActiveChannelId, Target);
	}
	Invalidate();
}

void FFPSRGunMotionViewportClient::Tick(float DeltaSeconds)
{
	FEditorViewportClient::Tick(DeltaSeconds);

	// 이 탭 전용 씬이라 다른 탭과 공유되지 않지만, TickPreviewWorldOnce 는 월드 포인터를 키로 삼는 범용 문지기라
	// 그대로 재사용해도 안전하다(FPSRWeaponAssemblerHelpers 헤더 주석 참조).
	FPSRWeaponAssemblerHelpers::TickPreviewWorldOnce(PreviewScene.GetWorld(), DeltaSeconds);

	// v3 §20-4: 재생 중에는 매 틱 스크럽 위치가 흐르므로, 총/손/파츠 프리뷰도 매 틱 다시 평가해야 한다(스크럽
	// 정지 상태에서는 SetPreviewPosition 이 이미 같은 일을 하지만, [재생] 중에는 그 훅을 안 거친다).
	// 🚨 단 트래킹(기즈모 드래그 포함) 중에는 건너뛴다 — SetRelativeTransform 재적용이 InputWidgetDelta 가 얹은
	// 드래그 델타를 매 틱 되돌려 기즈모가 조작 불능이 되고, TrackingStopped 의 역산도 무점프 no-op 이 된다.
	if (!bIsTracking)
	{
		ApplyChannelEvaluationAtTime(LastAppliedAuthoringData.Get(), GetPreviewPosition());
	}

	// §12: 자유시점 ON 이면 구도 재적용을 건너뛴다 — 그래야 표준 에디터 뷰포트 네비게이션(회전/이동/줌)이 매 프레임
	// 덮어써지지 않는다. bFreeLook 을 끄는 쪽(SetFreeLook)이 스냅을 직접 처리하므로 여기선 그냥 스킵만 한다.
	if (!bFreeLook)
	{
		ApplyCameraComposition();
	}
}

FQuat FFPSRGunMotionViewportClient::GetLockedCompositionCameraRotation() const
{
	if (!bHasComposition)
	{
		return GetViewRotation().Quaternion();
	}
	// ApplyCameraComposition 이 잠금 모드에서 카메라 월드를 만드는 것과 같은 합성 — bFreeLook/GetViewRotation() 과
	// 무관하게 항상 다시 계산한다(§12).
	const FTransform ArmsWorld = ArmsComp ? ArmsComp->GetComponentTransform() : FTransform::Identity;
	const FTransform CameraWorld = CameraRelativeToArms * ArmsWorld;
	return CameraWorld.GetRotation();
}

void FFPSRGunMotionViewportClient::SetFreeLook(bool bEnable)
{
	if (bFreeLook == bEnable)
	{
		return;
	}
	bFreeLook = bEnable;
	if (!bFreeLook)
	{
		// §12: "OFF 복귀는 구도 값으로 스냅" — 다음 Tick 을 기다리지 않고 즉시 되돌린다.
		ApplyCameraComposition();
	}
	Invalidate();
}

bool FFPSRGunMotionViewportClient::InputKey(const FInputKeyEventArgs& EventArgs)
{
	if (EventArgs.Key == EKeys::F && EventArgs.Event == IE_Pressed)
	{
		SetFreeLook(!bFreeLook);
		return true;
	}
	return FEditorViewportClient::InputKey(EventArgs);
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

// ---------------------------------------------------------------------------------------------------------------
// v3 §20-4: 채널 선택 + 손/파츠 프리뷰(대상 클릭 선택·직접 평가 적용·채널별 기즈모 역산)
// ---------------------------------------------------------------------------------------------------------------

void FFPSRGunMotionViewportClient::SetActiveChannel(FName ChannelId)
{
	if (ActiveChannelId == ChannelId)
	{
		return;
	}
	ActiveChannelId = ChannelId;
	Invalidate();
}

FName FFPSRGunMotionViewportClient::ResolveChannelForComponent(const UPrimitiveComponent* Component) const
{
	if (!Component)
	{
		return NAME_None;
	}
	if (Component == WeaponComp)
	{
		return FPSRGunMotionChannelIds::Gun;
	}
	if (Component == LeftHandProxy)
	{
		return FPSRGunMotionChannelIds::LeftHand;
	}
	if (Component == RightHandProxy)
	{
		return FPSRGunMotionChannelIds::RightHand;
	}
	for (int32 PartIndex = 0; PartIndex < PartComps.Num(); ++PartIndex)
	{
		if (PartComps[PartIndex] == Component)
		{
			return PartSocketIds[PartIndex];
		}
	}
	return NAME_None;
}

UPrimitiveComponent* FFPSRGunMotionViewportClient::GetActiveTargetComponent() const
{
	switch (FPSRGunMotionClassifyChannel(ActiveChannelId))
	{
	case EFPSRGunMotionChannelKind::Gun:
		return WeaponComp;
	case EFPSRGunMotionChannelKind::LeftHand:
		return LeftHandProxy;
	case EFPSRGunMotionChannelKind::RightHand:
		return RightHandProxy;
	default:
		{
			const int32 PartIndex = PartSocketIds.IndexOfByKey(ActiveChannelId);
			return PartComps.IsValidIndex(PartIndex) ? PartComps[PartIndex] : nullptr;
		}
	}
}

void FFPSRGunMotionViewportClient::ProcessClick(FSceneView& View, HHitProxy* HitProxy, FKey Key, EInputEvent Event, uint32 HitX, uint32 HitY)
{
	// §20-4 "히트 프록시(어셈블러 뷰포트 클라이언트의 HActor 계열 패턴)로 총몸/파츠/손 프록시 클릭" — 엔진 기본
	// UPrimitiveComponent::CreateHitProxies 가 등록된 모든 프리뷰 컴포넌트에 HActor(Owner=null, PrimComponent=this)
	// 를 자동으로 붙여 준다(이 프리뷰 씬 컴포넌트들은 소유 액터가 없다 — Actor 가 null 이어도 PrimComponent 는
	// 유효하다). ModeTools 를 거치지 않고(§9 클래스 주석과 같은 이유) 여기서 직접 판정한다.
	if (Key == EKeys::LeftMouseButton && Event == IE_Released && HitProxy && HitProxy->IsA(HActor::StaticGetType()))
	{
		const HActor* ActorHit = static_cast<HActor*>(HitProxy);
		const FName ClickedChannel = ResolveChannelForComponent(ActorHit->PrimComponent);
		if (!ClickedChannel.IsNone())
		{
			ChannelPickedDelegate.ExecuteIfBound(ClickedChannel);
			return;
		}
	}
	FEditorViewportClient::ProcessClick(View, HitProxy, Key, Event, HitX, HitY);
}

void FFPSRGunMotionViewportClient::CommitHandOrPartChannel(FName ChannelId, UPrimitiveComponent* TargetComp)
{
	if (!TargetComp || !WeaponComp)
	{
		return;
	}

	const EFPSRGunMotionChannelKind Kind = FPSRGunMotionClassifyChannel(ChannelId);

	FQuat ParentRot = FQuat::Identity;
	if (Kind == EFPSRGunMotionChannelKind::LeftHand || Kind == EFPSRGunMotionChannelKind::RightHand)
	{
		// §18: 손 채널은 "총 공간" — WeaponComp 의 지금(이미 걸린 Gun 채널 오프셋 포함) 월드 회전이 부모다.
		ParentRot = WeaponComp->GetComponentTransform().GetRotation();
	}
	else
	{
		// Part — §18 "파츠 로컬"의 정확한 기준 = **소켓 프레임**이다(런타임 ApplyWeaponPartCurves 가 이동 델타를
		// 저작 오프셋 이동에 그대로 더하는 그 프레임 — DA 저작 오프셋의 회전까지 합성한 프레임을 쓰면 커브 델타가
		// 오프셋 회전만큼 틀어진다, 검증에서 잡은 결함).
		const int32 PartIndex = PartSocketIds.IndexOfByKey(ChannelId);
		if (!PartSocketRelative.IsValidIndex(PartIndex))
		{
			return;
		}
		ParentRot = (PartSocketRelative[PartIndex] * WeaponComp->GetComponentTransform()).GetRotation();
	}

	const FTransform Now = TargetComp->GetComponentTransform();
	const FVector DeltaLoc = ParentRot.Inverse().RotateVector(Now.GetLocation() - DragBaseWorld.GetLocation());
	FQuat DeltaRot = ParentRot.Inverse() * (Now.GetRotation() * DragBaseWorld.GetRotation().Inverse()) * ParentRot;
	DeltaRot.Normalize();

	// 증분 커밋(헤더 주석 참조) — 이 채널이 지금 시각에 이미 평가 중이던 값에 델타를 더한다.
	const float Time = GetPreviewPosition();
	FVector ExistingLoc = FVector::ZeroVector;
	FQuat ExistingRot = FQuat::Identity;
	if (const UFPSRGunMotionAuthoringData* Data = LastAppliedAuthoringData.Get())
	{
		TArray<FFPSRGunMotionChannelKey> Sorted;
		if (Kind == EFPSRGunMotionChannelKind::LeftHand)
		{
			Sorted = Data->LeftHandTrack.Keys;
		}
		else if (Kind == EFPSRGunMotionChannelKind::RightHand)
		{
			Sorted = Data->RightHandTrack.Keys;
		}
		else if (const FFPSRGunMotionChannelTrack* PartTrack = Data->PartTracks.Find(ChannelId))
		{
			Sorted = PartTrack->Keys;
		}
		Sorted.Sort([](const FFPSRGunMotionChannelKey& A, const FFPSRGunMotionChannelKey& B) { return A.Time < B.Time; });
		FPSRGunMotionBaker::EvalChannelKeys(Sorted, Time, ExistingLoc, ExistingRot);
	}

	const FVector NewLoc = ExistingLoc + DeltaLoc;
	FQuat NewRot = DeltaRot * ExistingRot;
	NewRot.Normalize();

	ChannelGizmoCommitDelegate.ExecuteIfBound(ChannelId, Time, NewLoc, NewRot.Rotator());
}

void FFPSRGunMotionViewportClient::ApplyChannelEvaluationAtTime(const UFPSRGunMotionAuthoringData* Data, float Time)
{
	auto SortedEval = [Time](const TArray<FFPSRGunMotionChannelKey>& Keys, FVector& OutLoc, FQuat& OutRot)
	{
		TArray<FFPSRGunMotionChannelKey> Sorted = Keys;
		Sorted.Sort([](const FFPSRGunMotionChannelKey& A, const FFPSRGunMotionChannelKey& B) { return A.Time < B.Time; });
		FPSRGunMotionBaker::EvalChannelKeys(Sorted, Time, OutLoc, OutRot);
	};

	// --- 총 채널(카메라 공간, §18 FPGM_Gun_*) — M = GetCamToCompRotation()(§3-2/§19-1 과 같은 CDO 기준 회전; 이
	// 프리뷰는 팔/카메라가 애니메이트되지 않으므로 CDO 상수 M 이 런타임 라이브 M 과 등가다). ---
	if (WeaponComp)
	{
		FVector CamLoc = FVector::ZeroVector;
		FQuat CamRotQ = FQuat::Identity;
		if (Data)
		{
			SortedEval(Data->GunTrack.Keys, CamLoc, CamRotQ);
		}

		if (!bHasCachedCamToComp)
		{
			FText Unused;
			CachedCamToComp = FQuat::Identity;
			FPSRGunMotionBaker::GetCamToCompRotation(CachedCamToComp, Unused);
			// 실패해도 M=Identity 로 계속(§7 Issue 가 이미 카메라 미설정을 보고). 매 틱 CDO 워크를 반복하지 않도록
			// 성공 여부와 무관하게 이번 클립 세션 동안은 재시도하지 않는다(SetSourceClip 에서 무효화).
			bHasCachedCamToComp = true;
		}
		const FQuat& M = CachedCamToComp;

		const FVector CompLoc = M.RotateVector(CamLoc);
		FQuat CompRot = M * CamRotQ * M.Inverse();
		CompRot.Normalize();

		WeaponComp->SetRelativeTransform(FTransform(CompRot, CompLoc));
	}

	// --- 파츠 채널(파츠 로컬, §18 FPGM_P_<소켓>_*) — Base(Socket+Offset) 위에 오프셋을 가산 합성
	// (ChannelOffset * Base, 어셈블러 Init 합성과 같은 관례). 손 채널 블렌드가 이 결과(갱신된 PartComp 상대
	// 트랜스폼)를 그대로 읽으므로 손보다 먼저 계산해야 한다. ---
	for (int32 PartIndex = 0; PartIndex < PartComps.Num(); ++PartIndex)
	{
		UStaticMeshComponent* PartComp = PartComps[PartIndex];
		if (!PartComp || !PartSocketRelative.IsValidIndex(PartIndex) || !PartAuthoredOffset.IsValidIndex(PartIndex))
		{
			continue;
		}
		FVector Loc = FVector::ZeroVector;
		FQuat RotQ = FQuat::Identity;
		if (Data)
		{
			if (const FFPSRGunMotionChannelTrack* Track = Data->PartTracks.Find(PartSocketIds[PartIndex]))
			{
				SortedEval(Track->Keys, Loc, RotQ);
			}
		}
		// 런타임 ApplyWeaponPartCurves 와 자릿수까지 같은 합성(§19-3 "이동=소켓 프레임 가산, 회전=저작 오프셋에
		// 좌곱") — FTransform 후곱(Offset * Base)은 이동이 저작 오프셋 회전으로 돌아가고 회전 곱 순서도 반대라
		// 프리뷰가 PIE 와 어긋난다(검증에서 잡은 결함).
		const FTransform& Authored = PartAuthoredOffset[PartIndex];
		FTransform Local = Authored;
		Local.SetLocation(Authored.GetLocation() + Loc);
		Local.SetRotation((RotQ * Authored.GetRotation()).GetNormalized());
		PartComp->SetRelativeTransform(Local * PartSocketRelative[PartIndex]);
	}

	// --- 손 채널(총 공간, §18 FPGM_HL_*/FPGM_HR_*) — 그립⊕부착파츠 블렌드 위에 오프셋을 가산 합성. ---
	auto ApplyHand = [this, Data, Time, &SortedEval](USphereComponent* Proxy, FName GripSocket,
		const FFPSRGunMotionChannelTrack* Track, const TArray<FFPSRGunMotionScalarKey>* BlendKeys, FName AttachPartSocket)
	{
		if (!Proxy || !WeaponComp)
		{
			return;
		}

		FTransform GripLocal = FTransform::Identity;
		if (!GripSocket.IsNone() && WeaponComp->DoesSocketExist(GripSocket))
		{
			GripLocal = WeaponComp->GetSocketTransform(GripSocket, RTS_Component);
		}

		FTransform BaseLocal = GripLocal;
		if (!AttachPartSocket.IsNone())
		{
			const int32 PartIndex = PartSocketIds.IndexOfByKey(AttachPartSocket);
			if (PartComps.IsValidIndex(PartIndex) && PartComps[PartIndex])
			{
				float Blend = 0.0f;
				if (BlendKeys && BlendKeys->Num() > 0)
				{
					// EvalScalarKeys 는 정렬된 배열을 전제한다(저장 배열은 시간 순서를 강제하지 않는다 — AUD 규약).
					TArray<FFPSRGunMotionScalarKey> SortedBlend = *BlendKeys;
					SortedBlend.Sort([](const FFPSRGunMotionScalarKey& A, const FFPSRGunMotionScalarKey& B) { return A.Time < B.Time; });
					Blend = FPSRGunMotionBaker::EvalScalarKeys(SortedBlend, Time);
				}
				if (Blend > UE_KINDA_SMALL_NUMBER)
				{
					// PartComp 는 이번 프레임 파츠 오프셋까지 이미 반영된 상태(위 루프가 먼저 돈다) — WeaponComp
					// 기준 상대 트랜스폼으로 정확히 blend 대상이 된다(런타임의 "라이브 파츠 프레임" 재기저와 등가).
					const FTransform PartLocal = PartComps[PartIndex]->GetComponentTransform().GetRelativeTransform(WeaponComp->GetComponentTransform());
					BaseLocal.SetLocation(FMath::Lerp(GripLocal.GetLocation(), PartLocal.GetLocation(), Blend));
					BaseLocal.SetRotation(FQuat::Slerp(GripLocal.GetRotation(), PartLocal.GetRotation(), Blend));
				}
			}
		}

		FVector Loc = FVector::ZeroVector;
		FQuat RotQ = FQuat::Identity;
		if (Track)
		{
			SortedEval(Track->Keys, Loc, RotQ);
		}
		// 런타임 ComputeHandIKTarget 과 같은 합성(§18/§19-1 "이동=총 공간 가산, 회전=좌곱") — 총 공간 = WeaponComp
		// 상대 공간이므로 오프셋 이동은 그립 회전에 물리지 않고 그대로 더해진다. (블렌드 순서 차이는 등가:
		// lerp(G+O, P+O) = lerp(G,P)+O, slerp(O*G, O*P) = O*slerp(G,P).)
		FTransform Target = BaseLocal;
		Target.SetLocation(BaseLocal.GetLocation() + Loc);
		Target.SetRotation((RotQ * BaseLocal.GetRotation()).GetNormalized());
		Proxy->SetRelativeTransform(Target);
	};

	ApplyHand(LeftHandProxy, LeftHandGripSocket, Data ? &Data->LeftHandTrack : nullptr, Data ? &Data->LeftHandBlendKeys : nullptr, Data ? Data->LeftHandAttachPartSocket : NAME_None);
	ApplyHand(RightHandProxy, RightHandGripSocket, Data ? &Data->RightHandTrack : nullptr, Data ? &Data->RightHandBlendKeys : nullptr, Data ? Data->RightHandAttachPartSocket : NAME_None);
}
