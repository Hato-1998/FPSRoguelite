// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * v3 §20 툴 확장 — 채널 식별자(에디터 툴 전용 개념, 런타임 FPSRGunMotionCurveNames 와는 다른 레이어). 총/왼손/오른손은
 * 고정 FName 상수, 파츠는 그 파츠의 안정 부착소켓 id(FFPSRWeaponPartAttachment::Socket, 예 "SOCKET_Mount_3")를 그대로
 * 채널 id 로 쓴다 — UFPSRGunMotionAuthoringData::PartTracks 의 키와 같은 값이라 별도 매핑 테이블이 필요 없다.
 *
 * §20-3 "채널별 키 배열 맵"의 "채널"을 SFPSRGunMotionTab/SFPSRGunMotionTimeline/FFPSRGunMotionViewportClient 세 곳이
 * 같은 값으로 가리키기 위한 단일 소스 — 리터럴 "Gun"/"LeftHand"/"RightHand" 중복을 막는다.
 */
namespace FPSRGunMotionChannelIds
{
	inline const FName Gun(TEXT("Gun"));
	inline const FName LeftHand(TEXT("LeftHand"));
	inline const FName RightHand(TEXT("RightHand"));
	/** 손 채널의 Blend 스칼라 하위 레인(§20-3 "왼손(+Blend 하위 얇은 레인)") — 채널 키가 아니라 스칼라 키를 다루므로
	 *  ResolveChannelTrack 이 아니라 ResolveScalarTrack 이 받는다(SFPSRGunMotionTab). */
	inline const FName LeftHandBlend(TEXT("LeftHandBlend"));
	inline const FName RightHandBlend(TEXT("RightHandBlend"));
}

/** 채널 id 하나가 어떤 종류인지 — 숫자 키 목록의 열 구성(loc/rot 6열 vs Time/Value 2열)과 뷰포트 기즈모 역산 공간
 *  분기(총=카메라 공간 §9·§12 / 손=총 공간 / 파츠=파츠 로컬, §20-4)가 이걸로 갈린다. */
enum class EFPSRGunMotionChannelKind : uint8
{
	Gun,
	LeftHand,
	RightHand,
	LeftHandBlend,
	RightHandBlend,
	/** Part — ChannelId 자체가 그 파츠의 부착소켓 id(위 헤더 주석 참조). */
	Part,
};

FORCEINLINE EFPSRGunMotionChannelKind FPSRGunMotionClassifyChannel(FName ChannelId)
{
	if (ChannelId == FPSRGunMotionChannelIds::Gun) return EFPSRGunMotionChannelKind::Gun;
	if (ChannelId == FPSRGunMotionChannelIds::LeftHand) return EFPSRGunMotionChannelKind::LeftHand;
	if (ChannelId == FPSRGunMotionChannelIds::RightHand) return EFPSRGunMotionChannelKind::RightHand;
	if (ChannelId == FPSRGunMotionChannelIds::LeftHandBlend) return EFPSRGunMotionChannelKind::LeftHandBlend;
	if (ChannelId == FPSRGunMotionChannelIds::RightHandBlend) return EFPSRGunMotionChannelKind::RightHandBlend;
	return EFPSRGunMotionChannelKind::Part;
}

/** true 면 §20-3 숫자 키 목록이 Time/Value 2열(Blend), false 면 Time+6열(loc/rot 채널). */
FORCEINLINE bool FPSRGunMotionChannelIsScalar(EFPSRGunMotionChannelKind Kind)
{
	return Kind == EFPSRGunMotionChannelKind::LeftHandBlend || Kind == EFPSRGunMotionChannelKind::RightHandBlend;
}
