// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Engine/Blueprint.h"   // TSoftObjectPtr<UBlueprint> — GetCamToCompRotation 이 이 BP 의 CDO 에서 컴포넌트를 찾는다
#include "FPSRGunMotionSettings.generated.h"

class UStaticMesh;

/**
 * 총 모션 저작 툴 설정(editor-only). Config = Editor + DefaultConfig — 값이 Config/DefaultEditor.ini 에 실려
 * 체크인되고, 재빌드 없이 Project Settings > FPSR 에서 바꿀 수 있다(기존 UFPSRWeaponAssemblerSettings 패턴 답습,
 * GunMotionTool_Spec.md §1).
 */
UCLASS(Config = Editor, DefaultConfig, meta = (DisplayName = "FPSR Gun Motion"))
class UFPSRGunMotionSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** Project Settings 에서 "FPSR" 섹션 아래 다른 FPSR 에디터 툴들과 나란히 묶인다. */
	virtual FName GetCategoryName() const override { return FName(TEXT("FPSR")); }

	/** 저작 대상 1인칭 캐릭터 BP. FPSRGunMotionBaker::GetCamToCompRotation 이 이 BP 의 CDO 에서
	 *  ArmsComponentName/CameraComponentName 컴포넌트를 찾아 어태치 체인을 걸어 올라간다(스폰 없이 CDO 만으로
	 *  계산 — 이 툴의 순수 로직이 커맨드릿에서도 재사용 가능해야 하기 때문에 프리뷰 월드에 기대지 않는다). */
	UPROPERTY(EditAnywhere, Config, Category = "Gun Motion")
	TSoftObjectPtr<UBlueprint> TargetCharacterBP;

	/** [총 고정화](SanitizeRightChain)가 "총 고정" 상태로 중립화하는 오른팔 체인 뼈 목록. */
	UPROPERTY(EditAnywhere, Config, Category = "Gun Motion")
	TArray<FName> RightChainBones;

	/** 저작 키가 실제로 구워지는 대상 뼈(그립 소스). 기본 hand_r — ik_hand_gun 이 매 프레임 이 뼈를 따라가므로
	 *  이 뼈의 델타만 저작하면 총+양손 IK 가 자동으로 따라온다(GunMotionTool_Spec.md §0). */
	UPROPERTY(EditAnywhere, Config, Category = "Gun Motion")
	FName GripSourceBone = TEXT("hand_r");

	/** [PIE에서 재생] 버튼, §14 PIE 라이브 링크 미러 몽타주가 공통으로 PlaySlotAnimationAsDynamicMontage 에
	 *  넘기는 몽타주 슬롯 이름. */
	UPROPERTY(EditAnywhere, Config, Category = "Gun Motion")
	FName PreviewSlotName = TEXT("DefaultSlot");

	/** 증보 v2.2 §14 마지막: [총 고정화] 실행 시 클립에 이 이름의 플로트 커브가 없으면 상태줄에 경고("IK 해제
	 *  커브가 클립에 없음 — 미러에서 왼손이 그립에 고정됨")를 낸다. 🚨 런타임 UFPSRFirstPersonArmsAnimInstance::
	 *  LeftHandIKWeightCurve 와 이름이 일치해야 한다 — 어긋나면 이 경고가 무의미해진다. */
	UPROPERTY(EditAnywhere, Config, Category = "Gun Motion")
	FName LeftHandIKWeightCurveName = TEXT("LeftHandIKWeight");

	/** TargetCharacterBP 위의 1인칭 팔 스켈레탈 메시 컴포넌트 이름(GetCamToCompRotation 이 CDO 컴포넌트 배열에서
	 *  이 이름으로 찾는다 — protected UPROPERTY 라 직접 멤버 접근 대신 이름 조회를 쓴다). */
	UPROPERTY(EditAnywhere, Config, Category = "Gun Motion")
	FName ArmsComponentName = TEXT("FirstPersonArms");

	/** TargetCharacterBP 위의 1인칭 카메라 컴포넌트 이름(GetCamToCompRotation, 위와 동일한 이유로 이름 조회). */
	UPROPERTY(EditAnywhere, Config, Category = "Gun Motion")
	FName CameraComponentName = TEXT("FirstPersonCamera");

	/** 비주얼 저작 프리뷰(증보 v2, §7)가 팔 소켓에 붙이는 무기 스태틱 메시. 게임의 gun-anchor 경로(ik_hand_gun 뼈
	 *  앵커)와는 부착 메커니즘이 다르지만 등가다 — ABP 의 CopyBone(ik_hand_gun := hand_r)이 항등인 조건에서 총은
	 *  hand_r 을 강체로 따라가므로, 프리뷰에서 hand_r 소켓(PreviewWeaponAttachSocket)에 부착하는 것이 인게임 결과와
	 *  같은 상대 배치를 만든다. */
	UPROPERTY(EditAnywhere, Config, Category = "Gun Motion Preview")
	TSoftObjectPtr<UStaticMesh> PreviewWeaponMesh;

	/** 위 무기 메시를 붙일 팔 메시의 소켓 이름(§7). */
	UPROPERTY(EditAnywhere, Config, Category = "Gun Motion Preview")
	FName PreviewWeaponAttachSocket = TEXT("SOCKET_Weapon");

	// --- 증보 v2.1 §11: PIE 구도 캡처 -----------------------------------------------------------------------------
	// 프로브 스폰(BeginPlay 없음)의 구도는 CDO 저작 배치라 런타임 보정(시선 높이/스탠스 카메라 등)이 빠진다 —
	// [PIE 구도 캡처] 버튼이 실행 중인 PIE 캐릭터에서 실측해 여기 저장한다(§11 배경 설명).

	/** [PIE 구도 캡처]가 저장한 카메라↔팔 상대 트랜스폼 — PIE 의 FirstPersonCamera/FirstPersonArms 실제 월드
	 *  트랜스폼으로 `CamWorld.GetRelativeTransform(ArmsWorld)` 를 계산한 값(§11). */
	UPROPERTY(EditAnywhere, Config, Category = "Gun Motion Composition Capture")
	FTransform CapturedCameraRelativeToArms = FTransform::Identity;

	/** [PIE 구도 캡처]가 저장한 FirstPersonCamera::GetCameraView 의 가로 FOV(도, §11). */
	UPROPERTY(EditAnywhere, Config, Category = "Gun Motion Composition Capture")
	float CapturedFOV = 0.0f;

	/** 위 두 값이 실제로 캡처됐는가. false 면 RefreshCameraComposition 이 기존 프로브 경로(CDO 스폰, §7)로
	 *  폴백한다(§11 "최초 사용 전 폴백"). */
	UPROPERTY(EditAnywhere, Config, Category = "Gun Motion Composition Capture")
	bool bHasCapturedComposition = false;

	UFPSRGunMotionSettings();
};
