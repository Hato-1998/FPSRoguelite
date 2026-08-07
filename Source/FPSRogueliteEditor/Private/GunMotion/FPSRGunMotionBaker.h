// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UAnimSequence;
struct FFPSRGunMotionKey;

/**
 * 총 모션 저작 수학(GunMotionTool_Spec.md §2-§3) — 순수 로직, Slate 의존 없음(커맨드릿에서도 재사용 가능하도록
 * SFPSRGunMotionTab 과 분리했다). 커밋 30a97953 파이썬 레시피의 C++ 축자 이식.
 */
class FPSRGunMotionBaker
{
public:
	/** 우측 체인을 각 본의 base 포즈 상수로 덮어써 "총 고정" 상태로 만든다(A16 레시피). 하나의 컨트롤러 bracket
	 *  안에서 전체 본을 처리하고, 완료 시 AssetUserData::bSanitized=true 로 표시한다. */
	static bool SanitizeRightChain(UAnimSequence* Seq, const TArray<FName>& Bones, FText& OutError);

	/** 저작 키(Keys) → 그립 소스 본(Settings::GripSourceBone) 트랙 굽기. Seq 가 SanitizeRightChain 을 거치지
	 *  않았으면(AssetUserData::bSanitized == false) 실패한다. */
	static bool BakeGunMotion(UAnimSequence* Seq, const TArray<FFPSRGunMotionKey>& Keys, FText& OutError);

	/** 카메라→팔 컴포넌트 회전 M(§2·§3-2) — UFPSRGunMotionSettings::TargetCharacterBP 의 CDO 에서
	 *  ArmsComponentName/CameraComponentName 컴포넌트를 찾아 어태치 체인을 걸어 올라가며 계산한다. */
	static bool GetCamToCompRotation(FQuat& OutM, FText& OutError);

private:
	/** §3-1: 본 하나의 시각 t=0 base 트랜스폼 역산(base = delta⁻¹∘raw). Seq 는 애디티브(AAT_LOCAL_SPACE_BASE)
	 *  여야 한다 — 아니면 delta/raw 개념 자체가 성립하지 않는다. */
	static bool ComputeBoneBaseAtFrame0(UAnimSequence* Seq, FName BoneName, FTransform& OutBase, FText& OutError);

	/** §3-3: Time 오름차순으로 정렬된 Keys 를 스무스스텝 보간해 Time 시각의 카메라 공간 오프셋/회전을 구한다. */
	static void EvalKeys(const TArray<FFPSRGunMotionKey>& SortedKeys, float Time, FVector& OutCamOffset, FQuat& OutCamRotation);
};
