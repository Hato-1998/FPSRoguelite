// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Engine/Blueprint.h"   // TSoftObjectPtr<UBlueprint> — GetCamToCompRotation 이 이 BP 의 CDO 에서 컴포넌트를 찾는다
#include "FPSRGunMotionSettings.generated.h"

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

	/** [PIE에서 재생] 버튼이 PlaySlotAnimationAsDynamicMontage 에 넘기는 몽타주 슬롯 이름. */
	UPROPERTY(EditAnywhere, Config, Category = "Gun Motion")
	FName PreviewSlotName = TEXT("DefaultSlot");

	/** TargetCharacterBP 위의 1인칭 팔 스켈레탈 메시 컴포넌트 이름(GetCamToCompRotation 이 CDO 컴포넌트 배열에서
	 *  이 이름으로 찾는다 — protected UPROPERTY 라 직접 멤버 접근 대신 이름 조회를 쓴다). */
	UPROPERTY(EditAnywhere, Config, Category = "Gun Motion")
	FName ArmsComponentName = TEXT("FirstPersonArms");

	/** TargetCharacterBP 위의 1인칭 카메라 컴포넌트 이름(GetCamToCompRotation, 위와 동일한 이유로 이름 조회). */
	UPROPERTY(EditAnywhere, Config, Category = "Gun Motion")
	FName CameraComponentName = TEXT("FirstPersonCamera");

	UFPSRGunMotionSettings();
};
