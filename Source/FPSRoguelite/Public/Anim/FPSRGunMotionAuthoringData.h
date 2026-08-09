// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/AssetUserData.h"
#include "FPSRGunMotionAuthoringData.generated.h"

/**
 * 총 모션 저작 키 한 개(GunMotionTool_Spec.md §1). 카메라 공간에서 손이 어디로 움직여야 하는지를 기술한다 —
 * 클립 좌표계가 아니라 카메라 좌표계인 이유는 저작자가 "화면에서 총이 어떻게 움직이는가"로 판단하기 때문이고,
 * 실제 hand_r 트랙 굽기(FPSRGunMotionBaker::BakeGunMotion)가 이걸 컴포넌트 공간으로 변환한다.
 */
USTRUCT()
struct FFPSRGunMotionKey
{
	GENERATED_BODY()

	/** 클립 재생 시각(초). */
	UPROPERTY(EditAnywhere)
	float Time = 0.0f;

	/** 카메라 공간 이동(cm). X=앞, Y=오른쪽, Z=위. */
	UPROPERTY(EditAnywhere)
	FVector CamOffset = FVector::ZeroVector;

	/** 카메라 공간 회전(도). Pitch/Yaw/Roll. */
	UPROPERTY(EditAnywhere)
	FRotator CamRotation = FRotator::ZeroRotator;
};

/**
 * v3 §20-1: 채널 하나의 저작 키(위치+회전). 공간은 채널이 정한다(§18 — 총=카메라 공간, 손=총 공간, 파츠=파츠 로컬).
 * FFPSRGunMotionKey(v2, CamOffset/CamRotation)와 이름을 다르게 둔 이유는 v3 채널이 카메라 공간 전용이 아니라서다.
 */
USTRUCT()
struct FFPSRGunMotionChannelKey
{
	GENERATED_BODY()

	/** 클립 재생 시각(초). */
	UPROPERTY(EditAnywhere)
	float Time = 0.0f;

	/** 채널 공간 이동(cm). */
	UPROPERTY(EditAnywhere)
	FVector Loc = FVector::ZeroVector;

	/** 채널 공간 회전(도). */
	UPROPERTY(EditAnywhere)
	FRotator Rot = FRotator::ZeroRotator;
};

/** v3 §20-1: 스칼라 채널(FPGM_HL_Blend/FPGM_HR_Blend) 키 하나 — 0..1 클램프는 소비 측(런타임 GetCurveValue 리더)의 몫. */
USTRUCT()
struct FFPSRGunMotionScalarKey
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere)
	float Time = 0.0f;

	UPROPERTY(EditAnywhere)
	float Value = 0.0f;
};

/** v3 §20-1: 채널 하나의 독립 타이밍 키 배열 — 총/왼손/오른손/파츠마다 이 트랙을 하나씩 들고, 서로 다른 시각에 키를
 *  찍을 수 있다(§20 "채널마다 독립 타이밍"). */
USTRUCT()
struct FFPSRGunMotionChannelTrack
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere)
	TArray<FFPSRGunMotionChannelKey> Keys;
};

/**
 * 총 모션 클립에 부착되는 저작 키 영속 데이터(GunMotionTool_Spec.md §1). 사이드카 파일이 아니라 클립 에셋 자신에
 * AssetUserData 로 붙는다 — 클립을 옮기거나 복제해도 저작 키가 따라오게 하기 위함. 런타임 소비는 없다(에디터 툴
 * 전용, FPSRGunMotionBaker 가 Keys 를 읽어 hand_r 트랙을 굽고 나면 그 결과(트랙 자체)만 게임이 재생한다).
 */
UCLASS()
class FPSROGUELITE_API UFPSRGunMotionAuthoringData : public UAssetUserData
{
	GENERATED_BODY()

public:
	/** v2 레거시 — hand_r 본 트랙 베이크(FPSRGunMotionBaker::BakeGunMotion)가 읽는 저작 키. v3 채널 저작에서는
	 *  호출되지 않지만 기존 클립(장전 1호 등) 호환을 위해 보존한다(GunMotionTool_Spec.md §20-2 마이그레이션 노트).
	 *  시간 순서는 굽기 시점에 정렬되므로 여기서는 강제하지 않는다. */
	UPROPERTY(EditAnywhere)
	TArray<FFPSRGunMotionKey> Keys;

	/** SanitizeRightChain 이 우측 체인을 중립화 완료했는지 — BakeGunMotion 의 선행 조건 게이트. */
	UPROPERTY(VisibleAnywhere)
	bool bSanitized = false;

	/** v3 §18: 이 클립이 재생되는 동안 왼손 IK 타깃이 FPGM_HL_Blend=1 에서 재기저할 부착 파츠의 안정 id
	 *  (FFPSRWeaponPartAttachment::Socket, 예: "SOCKET_Mount_3"). 클립당 손별 1개(장전=왼손↔탄창이면 충분 —
	 *  한 클립에서 여러 파츠를 순차 부착하는 것은 후속 확장, §18). None = 이 클립은 왼손 재기저를 쓰지 않음
	 *  (Blend 커브가 있어도 그립 기준 오프셋으로만 남는다 — AFPSRFirstPersonArmsAnimInstance 가 이 값을
	 *  런타임에 조회해 AFPSRCharacter::GetWeaponPartFrameInGunSpace 에 넘긴다). */
	UPROPERTY(EditAnywhere)
	FName LeftHandAttachPartSocket = NAME_None;

	/** 오른손 미러(v3 §18) — FPGM_HR_Blend=1 에서 재기저할 부착 파츠의 안정 id. */
	UPROPERTY(EditAnywhere)
	FName RightHandAttachPartSocket = NAME_None;

	// --- v3 §20-1: 채널별 키 배열(P2 툴이 편집·베이크하는 소스) ---------------------------------------------------

	/** 총 전체 loc/rot 오프셋(카메라 공간, §18 FPGM_Gun_*). */
	UPROPERTY(EditAnywhere)
	FFPSRGunMotionChannelTrack GunTrack;

	/** 왼손 IK 타깃 오프셋(총 공간, §18 FPGM_HL_*). */
	UPROPERTY(EditAnywhere)
	FFPSRGunMotionChannelTrack LeftHandTrack;

	/** 오른손 IK 타깃 오프셋(총 공간, §18 FPGM_HR_*). */
	UPROPERTY(EditAnywhere)
	FFPSRGunMotionChannelTrack RightHandTrack;

	/** 왼손 그립⊕부착파츠 블렌드(§18 FPGM_HL_Blend, 0..1). */
	UPROPERTY(EditAnywhere)
	TArray<FFPSRGunMotionScalarKey> LeftHandBlendKeys;

	/** 오른손 미러. */
	UPROPERTY(EditAnywhere)
	TArray<FFPSRGunMotionScalarKey> RightHandBlendKeys;

	/** 파츠별 추가 상대 트랜스폼(파츠 로컬 공간, §18 FPGM_P_<소켓>_*) — 키 = 안정 부착소켓 id
	 *  (FFPSRWeaponPartAttachment::Socket, 예 "SOCKET_Mount_3"). */
	UPROPERTY(EditAnywhere)
	TMap<FName, FFPSRGunMotionChannelTrack> PartTracks;
};
