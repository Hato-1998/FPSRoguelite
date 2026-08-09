// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"
#include "Framework/SlateDelegates.h"           // FOnFloatValueChanged — 기존 스크럽 슬라이더가 쓰던 것과 같은 델리게이트 타입 재사용

struct FFPSRGunMotionKey;

/** v3 §20-3: 타임라인 레인 하나 — 총/왼손/왼손Blend/오른손/오른손Blend/파츠×N. 데이터 소유는 여전히 탭
 *  (AssetUserData) — 이 구조체는 그리기/히트테스트에 필요한 최소한(채널 id, 라벨, 키 시각 목록)만 나른다. 스칼라
 *  채널(Blend)이든 loc/rot 채널이든 다이아몬드는 Time 위치만 있으면 그려지므로 여기선 값 자체를 구분하지 않는다
 *  (값 편집은 탭의 숫자 키 목록이 ChannelId 로 채널 종류를 갈라 담당, SFPSRGunMotionTab::BuildChannelKeyRow/
 *  BuildBlendKeyRow). */
struct FFPSRGunMotionTimelineLane
{
	FName ChannelId;
	FText Label;
	TArray<float> KeyTimes;
	/** true 면 Blend 하위 레인처럼 더 얇게 그린다(§20-3 "왼손(+Blend 하위 얇은 레인)"). */
	bool bSubLane = false;
};

/** 레인 클릭 = 활성 채널 전환(§20-3 "레인 클릭 = 활성 전환"). 빈 곳 클릭이라도 그 레인 안이면 발화한다. */
DECLARE_DELEGATE_OneParam(FOnFPSRGunMotionTimelineChannelSelected, FName /*ChannelId*/);

/** 키 클릭 = 선택 + 스크럽 점프(§16, v3 §20-3 에서 채널 id 가 추가됐다). */
DECLARE_DELEGATE_TwoParams(FOnFPSRGunMotionTimelineKeySelected, FName /*ChannelId*/, int32 /*KeyIndex*/);

/** 키 드래그 종료 커밋(§16 "종료 시에만 커밋") — 탭의 §20-3 채널 파라미터화 MutateChannelKey/MutateBlendKey 로 그대로
 *  이어진다(단일 훅). */
DECLARE_DELEGATE_ThreeParams(FOnFPSRGunMotionTimelineKeyTimeCommitted, FName /*ChannelId*/, int32 /*KeyIndex*/, float /*NewTime*/);

/** 키 우클릭 컨텍스트 메뉴의 [키 삭제](§16). */
DECLARE_DELEGATE_TwoParams(FOnFPSRGunMotionTimelineKeyDeleteRequested, FName /*ChannelId*/, int32 /*KeyIndex*/);

/** 빈 곳 더블클릭 = 그 레인·그 시각에 키 추가(§16) — 값 계산(그 채널의 현재 보간값)은 탭이 한다(레인마다 채널
 *  종류가 달라 이 위젯이 값을 만들 이유가 없다, §20-1 "일반화된 EvalChannelKeys/EvalScalarKeys"는 탭/베이커의 몫). */
DECLARE_DELEGATE_TwoParams(FOnFPSRGunMotionTimelineKeyAddRequested, FName /*ChannelId*/, float /*Time*/);

/**
 * 총 모션 타임라인 위젯(GunMotionTool_Spec.md 증보 v2.3 §16, v3 §20-3 멀티채널 레인 확장) — 시간 룰러(초 주눈금 +
 * 0.1s 보조눈금 + 라벨) + 플레이헤드(세로선 + 상단 핸들) + 채널별 레인(각 레인에 그 채널의 키 다이아몬드)을 커스텀
 * OnPaint 로 직접 그린다.
 *
 * §16 구현 방식 선택(변경 없음): 엔진 Persona 계열 SAnimTimeline 은 Editor/Persona/**Private**(비공개, FAnimModel 등
 * 무거운 종속) 라 이 모듈에서 재사용 불가. Editor/KismetWidgets/Public 의 SScrubWidget 은 공개 API 지만
 * "DraggableBars"(TArray<float>) 모델이라 개별 키 선택 하이라이트 · 우클릭 컨텍스트 메뉴 · 더블클릭 추가 · 다중
 * 레인을 지원하지 않는다(모델 불일치) — 그래서 SLeafWidget 커스텀 OnPaint 로 직접 그린다(UnrealEd 의
 * SColorGradientEditor 가 정확히 같은 패턴의 선례).
 *
 * 데이터 소유권은 여전히 탭(AssetUserData) 이 갖는다 — 이 위젯은 뷰(Lanes 를 매 페인트 다시 읽는다)와 조작
 * (클릭/드래그/더블클릭/우클릭/레인전환)만 담당하고, 실제 변경은 전부 델리게이트로 위임한다. 스크럽 상태 소유도
 * 기존 위치 그대로(탭이 뷰포트 클라이언트를 통해 갖는다).
 */
class SFPSRGunMotionTimeline : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SFPSRGunMotionTimeline)
		: _SequenceLength(0.0f)
		, _FrameRate(30.0f)
		, _ScrubPosition(0.0f)
		, _SelectedKeyIndex(INDEX_NONE)
		{}

		/** 클립 길이(초) — 룰러/키 X 좌표가 이 값 기준으로 위젯 폭 전체에 매핑된다. */
		SLATE_ATTRIBUTE(float, SequenceLength)
		/** 클립 프레임레이트 — 키 드래그의 프레임 스냅 기준(§16 "프레임레이트로 스냅"). */
		SLATE_ATTRIBUTE(float, FrameRate)
		/** 현재 스크럽 시각(플레이헤드 위치). */
		SLATE_ATTRIBUTE(float, ScrubPosition)
		/** v3 §20-3: 그려야 할 레인 목록(총/손(+Blend)/파츠×N, 탭이 매 페인트 다시 구성). */
		SLATE_ATTRIBUTE(TArray<FFPSRGunMotionTimelineLane>, Lanes)
		/** v3 §20-3: 지금 활성 채널 — 그 레인을 하이라이트한다(레인 클릭·뷰포트 히트프록시 클릭·부착 드롭다운과
		 *  동기화되는 단일 상태, 소유자는 탭). */
		SLATE_ATTRIBUTE(FName, ActiveChannelId)
		/** 활성 채널 안에서 선택된 키 인덱스(숫자 목록과의 동기화 기준 — §16 "숫자 목록 행 선택과 동기화"). */
		SLATE_ATTRIBUTE(int32, SelectedKeyIndex)

		/** 빈 곳 클릭/드래그 = 스크럽(§16) — 기존 SSlider 슬롯이 쓰던 것과 같은 델리게이트 타입이라 탭의 기존
		 *  OnScrubPositionChanged 핸들러를 그대로 재사용한다. */
		SLATE_EVENT(FOnFloatValueChanged, OnScrubPositionChanged)
		/** 스크럽 드래그 시작(§8 "스크럽 중에는 PreviewInstance 를 해당 시각에 고정" — 기존 재생 정지 훅 재사용). */
		SLATE_EVENT(FSimpleDelegate, OnScrubCaptureBegin)

		/** v3 §20-3: 레인 클릭(빈 곳 포함) = 활성 채널 전환. */
		SLATE_EVENT(FOnFPSRGunMotionTimelineChannelSelected, OnChannelSelected)
		/** 키 클릭 = 선택 + 점프(§16, 채널 id 포함). */
		SLATE_EVENT(FOnFPSRGunMotionTimelineKeySelected, OnKeySelected)
		/** 키 드래그 종료 커밋(§16 "종료 시에만 커밋"). */
		SLATE_EVENT(FOnFPSRGunMotionTimelineKeyTimeCommitted, OnKeyTimeCommitted)
		/** 키 우클릭 컨텍스트 메뉴의 [키 삭제](§16). */
		SLATE_EVENT(FOnFPSRGunMotionTimelineKeyDeleteRequested, OnKeyDeleteRequested)
		/** 빈 곳 더블클릭 = 키 추가(§16, 값 계산은 탭이 한다). */
		SLATE_EVENT(FOnFPSRGunMotionTimelineKeyAddRequested, OnKeyAddRequested)

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	// SWidget(SLeafWidget) interface
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float) const override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonDoubleClick(const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FCursorReply OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const override;
	virtual void OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override;

private:
	/** 로컬 X 좌표 → 클립 시각(초), [0, SequenceLength] 로 클램프. SequenceLength<=0 이면 0. */
	float LocalXToTime(float LocalX, float GeometryWidth) const;
	/** 클립 시각(초) → 로컬 X 좌표. */
	float TimeToLocalX(float Time, float GeometryWidth) const;
	/** Time 을 §16 "프레임레이트로 스냅"한다 — FrameRate<=0 이면 스냅 없이 그대로. */
	float SnapTimeToFrame(float Time) const;

	/** v3 §20-3: 레인 하나의 Y 시작 오프셋(룰러 아래부터 누적) — bSubLane 은 더 얇게. */
	float GetLaneY(int32 LaneIndex) const;
	float GetLaneHeight(int32 LaneIndex) const;
	/** 전체 레인 높이 합(룰러 제외) — ComputeDesiredSize 가 쓴다. */
	float GetTotalLanesHeight() const;
	/** 로컬 Y 좌표가 속한 레인 인덱스 — 없으면 INDEX_NONE(룰러 영역이나 위젯 밖). */
	int32 FindLaneIndexAtLocalY(float LocalY) const;
	/** 레인 안에서 MouseX 근방(±HitTestPixels)의 가장 가까운 키 인덱스(그 레인의 KeyTimes 배열 인덱스). 없으면
	 *  INDEX_NONE. */
	int32 FindKeyIndexAtLocalX(const FFPSRGunMotionTimelineLane& Lane, float LocalX, float GeometryWidth) const;
	/** 우클릭 컨텍스트 메뉴 — [키 삭제] 하나(§16). */
	void OpenKeyContextMenu(FName ChannelId, int32 KeyIndex, const FPointerEvent& MouseEvent);

	// --- 마우스 조작 상태 ---
	bool bScrubbing = false;
	bool bDraggingKey = false;
	FName DraggingChannelId = NAME_None;
	int32 DraggingKeyIndex = INDEX_NONE;
	float DragCurrentTime = 0.0f;     // 커밋 전 시각적 미리보기용(드래그 중에만 유효) — §16 "종료 시에만 커밋"
	float DragDistanceAccum = 0.0f;   // 단순 클릭과 드래그를 구분(무동작 커밋을 만들지 않기 위해)

	TAttribute<float> SequenceLengthAttr;
	TAttribute<float> FrameRateAttr;
	TAttribute<float> ScrubPositionAttr;
	TAttribute<TArray<FFPSRGunMotionTimelineLane>> LanesAttr;
	TAttribute<FName> ActiveChannelIdAttr;
	TAttribute<int32> SelectedKeyIndexAttr;

	FOnFloatValueChanged OnScrubPositionChangedDelegate;
	FSimpleDelegate OnScrubCaptureBeginDelegate;
	FOnFPSRGunMotionTimelineChannelSelected OnChannelSelectedDelegate;
	FOnFPSRGunMotionTimelineKeySelected OnKeySelectedDelegate;
	FOnFPSRGunMotionTimelineKeyTimeCommitted OnKeyTimeCommittedDelegate;
	FOnFPSRGunMotionTimelineKeyDeleteRequested OnKeyDeleteRequestedDelegate;
	FOnFPSRGunMotionTimelineKeyAddRequested OnKeyAddRequestedDelegate;
};
