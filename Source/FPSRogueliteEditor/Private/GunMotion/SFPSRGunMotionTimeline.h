// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"
#include "Framework/SlateDelegates.h"           // FOnFloatValueChanged — 기존 스크럽 슬라이더가 쓰던 것과 같은 델리게이트 타입 재사용
#include "GunMotion/FPSRGunMotionViewportClient.h"   // FOnFPSRGunMotionGizmoCommit — §16 더블클릭 키 추가가 §9 기즈모 커밋과 같은 모양(Time,CamOffset,CamRotation)이라 재사용

struct FFPSRGunMotionKey;

/** 키 클릭 = 선택 + 스크럽 점프(GunMotionTool_Spec.md 증보 v2.3 §16). */
DECLARE_DELEGATE_OneParam(FOnFPSRGunMotionKeySelected, int32 /*KeyIndex*/);

/** 키 드래그 종료 커밋(§16 "종료 시에만 커밋") — 탭의 기존 SetKeyTime(단일 훅 MutateKey → RebakeViewportPreview)과
 *  시그니처가 같아 그대로 바인딩된다. */
DECLARE_DELEGATE_TwoParams(FOnFPSRGunMotionKeyTimeCommitted, int32 /*KeyIndex*/, float /*NewTime*/);

/** 키 우클릭 컨텍스트 메뉴의 [키 삭제](§16). */
DECLARE_DELEGATE_OneParam(FOnFPSRGunMotionKeyDeleteRequested, int32 /*KeyIndex*/);

/**
 * 총 모션 타임라인 위젯(GunMotionTool_Spec.md 증보 v2.3 §16) — 기존 스크럽 슬라이더(SSlider) + §8 "키 시각 마커
 * 줄"을 대체하는 전용 위젯. 시간 룰러(초 주눈금 + 0.1s 보조눈금 + 라벨) + 플레이헤드(세로선 + 상단 핸들) + 키
 * 다이아몬드를 커스텀 OnPaint 로 직접 그린다.
 *
 * §16 구현 방식 선택: 엔진 Persona 계열 SAnimTimeline 은 Editor/Persona/**Private**(비공개, FAnimModel 등 무거운
 * 종속) 라 이 모듈에서 재사용 불가. Editor/KismetWidgets/Public 의 SScrubWidget 은 공개 API 지만
 * "DraggableBars"(TArray<float>) 모델이라 개별 키 선택 하이라이트 · 우클릭 컨텍스트 메뉴 · 더블클릭 추가를
 * 지원하지 않는다(모델 불일치) — 그래서 SLeafWidget 커스텀 OnPaint 로 직접 그린다(UnrealEd 의
 * SColorGradientEditor 가 정확히 같은 패턴의 선례: SLeafWidget + OnPaint(MakeBox/MakeRotatedBox) +
 * OnMouseButtonDown/Move/Up/DoubleClick + FMenuBuilder 우클릭 메뉴).
 *
 * 데이터 소유권은 여전히 탭(AssetUserData) 이 갖는다 — 이 위젯은 뷰(Keys 를 매 페인트 다시 읽는다)와 조작
 * (클릭/드래그/더블클릭/우클릭)만 담당하고, 실제 변경은 전부 델리게이트로 위임한다. 스크럽 상태 소유도 기존
 * 위치 그대로(탭이 뷰포트 클라이언트를 통해 갖는다) — 이 위젯은 값을 읽고 변경을 "요청"만 한다(§16 "스크럽
 * 상태 소유는 기존 위치 유지").
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
		/** 지금 그려야 할 저작 키 목록(시간 순서 무관 — 위젯이 조작마다 필요한 곳에서 정렬해 쓴다). */
		SLATE_ATTRIBUTE(TArray<FFPSRGunMotionKey>, Keys)
		/** 선택된 키 인덱스(숫자 목록과의 동기화 기준 — §16 "숫자 목록 행 선택과 동기화"). */
		SLATE_ATTRIBUTE(int32, SelectedKeyIndex)

		/** 빈 곳 클릭/드래그 = 스크럽(§16) — 기존 SSlider 슬롯이 쓰던 것과 같은 델리게이트 타입이라 탭의 기존
		 *  OnScrubPositionChanged 핸들러를 그대로 재사용한다. */
		SLATE_EVENT(FOnFloatValueChanged, OnScrubPositionChanged)
		/** 스크럽 드래그 시작(§8 "스크럽 중에는 PreviewInstance 를 해당 시각에 고정" — 기존 재생 정지 훅 재사용). */
		SLATE_EVENT(FSimpleDelegate, OnScrubCaptureBegin)

		/** 키 클릭 = 선택 + 점프(§16). */
		SLATE_EVENT(FOnFPSRGunMotionKeySelected, OnKeySelected)
		/** 키 드래그 종료 커밋(§16 "종료 시에만 커밋"). */
		SLATE_EVENT(FOnFPSRGunMotionKeyTimeCommitted, OnKeyTimeCommitted)
		/** 키 우클릭 컨텍스트 메뉴의 [키 삭제](§16). */
		SLATE_EVENT(FOnFPSRGunMotionKeyDeleteRequested, OnKeyDeleteRequested)
		/** 빈 곳 더블클릭 = 키 추가(§16) — §9 기즈모 커밋과 같은 모양(Time,CamOffset,CamRotation)이라 탭의 기존
		 *  OnGizmoKeyCommitted(같은 단일 훅: 시간 근접 시 갱신/아니면 추가 → RebakeViewportPreview)를 그대로
		 *  재사용한다. */
		SLATE_EVENT(FOnFPSRGunMotionGizmoCommit, OnKeyAddRequested)

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
	/** MouseX 근방(±HitTestPixels)에서 가장 가까운 키의 원본 Keys 배열 인덱스(시간순 정렬이 아니라 탭의
	 *  AssetUserData::Keys 인덱스와 일치해야 삭제/드래그 커밋이 맞는 키를 가리킨다). 없으면 INDEX_NONE. */
	int32 FindKeyIndexAtLocalX(float LocalX, float GeometryWidth) const;
	/** 우클릭 컨텍스트 메뉴 — [키 삭제] 하나(§16). */
	void OpenKeyContextMenu(int32 KeyIndex, const FPointerEvent& MouseEvent);

	// --- 마우스 조작 상태 ---
	bool bScrubbing = false;
	bool bDraggingKey = false;
	int32 DraggingKeyIndex = INDEX_NONE;
	float DragCurrentTime = 0.0f;     // 커밋 전 시각적 미리보기용(드래그 중에만 유효) — §16 "종료 시에만 커밋"
	float DragDistanceAccum = 0.0f;   // 단순 클릭과 드래그를 구분(무동작 커밋을 만들지 않기 위해)

	TAttribute<float> SequenceLengthAttr;
	TAttribute<float> FrameRateAttr;
	TAttribute<float> ScrubPositionAttr;
	TAttribute<TArray<FFPSRGunMotionKey>> KeysAttr;
	TAttribute<int32> SelectedKeyIndexAttr;

	FOnFloatValueChanged OnScrubPositionChangedDelegate;
	FSimpleDelegate OnScrubCaptureBeginDelegate;
	FOnFPSRGunMotionKeySelected OnKeySelectedDelegate;
	FOnFPSRGunMotionKeyTimeCommitted OnKeyTimeCommittedDelegate;
	FOnFPSRGunMotionKeyDeleteRequested OnKeyDeleteRequestedDelegate;
	FOnFPSRGunMotionGizmoCommit OnKeyAddRequestedDelegate;
};
