// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UAnimSequence;
class UAnimSequenceBase;
class UFPSRGunMotionAuthoringData;
struct FFPSRGunMotionKey;
struct FFPSRGunMotionChannelKey;
struct FFPSRGunMotionScalarKey;
struct FRichCurveKey;

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

	/** §3-3: Time 오름차순으로 정렬된 Keys 를 스무스스텝 보간해 Time 시각의 카메라 공간 오프셋/회전을 구한다.
	 *  증보 v2.3 §16: 타임라인의 "빈 곳 더블클릭 = 현재 보간값으로 키 추가"가 이 함수를 그대로 재사용한다
	 *  (원래 private 였던 것을 이번 증보에서 public 으로 올렸다 — BakeGunMotion 과 같은 보간 결과를 UI 도 봐야
	 *  더블클릭 추가가 포즈를 튀게 하지 않는다). */
	static void EvalKeys(const TArray<FFPSRGunMotionKey>& SortedKeys, float Time, FVector& OutCamOffset, FQuat& OutCamRotation);

	/** v3 §20-1: §3-3 보간을 채널 무관 공용 함수로 일반화한 것 — FFPSRGunMotionChannelTrack(총/손/파츠 공용) 이
	 *  쓴다. SortedKeys 는 호출자가 미리 Time 오름차순 정렬해 넘긴다(EvalKeys 와 같은 계약). */
	static void EvalChannelKeys(const TArray<FFPSRGunMotionChannelKey>& SortedKeys, float Time, FVector& OutLoc, FQuat& OutRot);

	/** v3 §20-1: 스칼라 채널(FPGM_HL_Blend/FPGM_HR_Blend) 버전 — 같은 smoothstep 규칙, 성분이 float 하나뿐. */
	static float EvalScalarKeys(const TArray<FFPSRGunMotionScalarKey>& SortedKeys, float Time);

	/** v3 §20-2: §18 커브 채널을 클립 + (지원 가능한) 참조 몽타주 양쪽에 굽는다(A17 자동화 — Docs/Troubleshooting.md
	 *  A17 "커브는 클립+몽타주 양쪽"). 본 트랙은 건드리지 않는다(BakeGunMotion/hand_r 경로와 독립). 키 0개인 채널의
	 *  커브는 삭제한다(§18 "부재=0 규약" — 죽은 커브 잔존 금지). OutMontageReport 가 있으면 몽타주별 굽기/스킵
	 *  사유를 사람이 읽을 문자열로 채운다(§20-2 "기록/스킵한 몽타주 목록을 토스트·로그로 보고"). */
	static bool BakeCurveChannels(UAnimSequence* Seq, const UFPSRGunMotionAuthoringData& Data, FText& OutError, TArray<FString>* OutMontageReport = nullptr);

	/** v3 §20-4: 위의 클립측 절반만 — 몽타주 스캔(A17) 없이 Seq 에만 §18 커브를 굽는다. transient 프리뷰 클립용
	 *  (프리뷰 클립에 커브가 없으면 PIE 라이브 미러의 GetCurveValue 가 전부 0 이라 채널 판정이 불가능하다).
	 *  실제 에셋 커밋은 반드시 BakeCurveChannels(A17 양쪽 규약)를 쓸 것. */
	static bool BakeCurveChannelsClipOnly(UAnimSequence* Seq, const UFPSRGunMotionAuthoringData& Data, FText& OutError);

private:
	/** BakeCurveChannels 의 클립측 계산 — Seq 의 프레임레이트/프레임수로 각 §18 커브 이름 → 샘플 키 배열을 만든다.
	 *  키 0개인 채널은 빈 배열(호출자가 삭제로 해석). */
	static void BuildCurveSampleMap(const UFPSRGunMotionAuthoringData& Data, int32 NumFrames, double Fps,
		TMap<FName, TArray<FRichCurveKey>>& OutCurveSamples);

	/** BakeCurveChannels 의 실제 쓰기 — Target(클립 또는 몽타주, 둘 다 UAnimSequenceBase)의 컨트롤러로 커브를
	 *  하나의 bracket 안에서 쓴다. 빈 키 배열 = 그 커브 삭제(있었다면). */
	static bool WriteCurveSamplesToSequenceBase(UAnimSequenceBase* Target, const TMap<FName, TArray<FRichCurveKey>>& CurveSamples, const FText& TransactionText, FText& OutError);


	/** §3-1: 본 하나의 시각 t=0 base 트랜스폼 역산(base = delta⁻¹∘raw). Seq 는 애디티브(AAT_LOCAL_SPACE_BASE)
	 *  여야 한다 — 아니면 delta/raw 개념 자체가 성립하지 않는다. */
	static bool ComputeBoneBaseAtFrame0(UAnimSequence* Seq, FName BoneName, FTransform& OutBase, FText& OutError);
};
