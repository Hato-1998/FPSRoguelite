// Copyright Epic Games, Inc. All Rights Reserved.

#include "GunMotion/SFPSRGunMotionTimeline.h"

#include "Rendering/DrawElements.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Layout/WidgetPath.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "InputCoreTypes.h"   // EKeys — 마우스 버튼 판정(GetEffectingButton() == EKeys::LeftMouseButton 등)

#define LOCTEXT_NAMESPACE "SFPSRGunMotionTimeline"

namespace
{
	// §16/§20-3 레이아웃 상수 — 룰러(초 주눈금+0.1s 보조눈금+라벨) 위, 채널 레인들 아래(레인마다 키 다이아몬드).
	constexpr float GRulerHeight = 20.0f;
	constexpr float GLaneHeight = 22.0f;
	constexpr float GSubLaneHeight = 12.0f;
	constexpr float GKeyDiamondSize = 9.0f;
	constexpr float GSubLaneKeyDiamondSize = 6.0f;
	constexpr float GMinorTickHeight = 5.0f;
	constexpr float GMajorTickHeight = 10.0f;
	constexpr float GPlayheadHandleHeight = 8.0f;
	constexpr float GPlayheadHandleWidth = 10.0f;
	constexpr float GHitTestPixels = 7.0f;
	constexpr float GDragThresholdPixels = 2.0f;
}

void SFPSRGunMotionTimeline::Construct(const FArguments& InArgs)
{
	SequenceLengthAttr = InArgs._SequenceLength;
	FrameRateAttr = InArgs._FrameRate;
	ScrubPositionAttr = InArgs._ScrubPosition;
	LanesAttr = InArgs._Lanes;
	ActiveChannelIdAttr = InArgs._ActiveChannelId;
	SelectedKeyIndexAttr = InArgs._SelectedKeyIndex;

	OnScrubPositionChangedDelegate = InArgs._OnScrubPositionChanged;
	OnScrubCaptureBeginDelegate = InArgs._OnScrubCaptureBegin;
	OnChannelSelectedDelegate = InArgs._OnChannelSelected;
	OnKeySelectedDelegate = InArgs._OnKeySelected;
	OnKeyTimeCommittedDelegate = InArgs._OnKeyTimeCommitted;
	OnKeyDeleteRequestedDelegate = InArgs._OnKeyDeleteRequested;
	OnKeyAddRequestedDelegate = InArgs._OnKeyAddRequested;
}

float SFPSRGunMotionTimeline::GetLaneHeight(int32 LaneIndex) const
{
	const TArray<FFPSRGunMotionTimelineLane> Lanes = LanesAttr.Get();
	return (Lanes.IsValidIndex(LaneIndex) && Lanes[LaneIndex].bSubLane) ? GSubLaneHeight : GLaneHeight;
}

float SFPSRGunMotionTimeline::GetLaneY(int32 LaneIndex) const
{
	float Y = GRulerHeight;
	for (int32 Index = 0; Index < LaneIndex; ++Index)
	{
		Y += GetLaneHeight(Index);
	}
	return Y;
}

float SFPSRGunMotionTimeline::GetTotalLanesHeight() const
{
	const TArray<FFPSRGunMotionTimelineLane> Lanes = LanesAttr.Get();
	float Total = 0.0f;
	for (int32 Index = 0; Index < Lanes.Num(); ++Index)
	{
		Total += GetLaneHeight(Index);
	}
	return Total;
}

int32 SFPSRGunMotionTimeline::FindLaneIndexAtLocalY(float LocalY) const
{
	const TArray<FFPSRGunMotionTimelineLane> Lanes = LanesAttr.Get();
	if (LocalY < GRulerHeight)
	{
		return INDEX_NONE; // 룰러 영역 — 레인이 아니다(항상 스크럽으로 처리).
	}
	float Y = GRulerHeight;
	for (int32 Index = 0; Index < Lanes.Num(); ++Index)
	{
		const float Height = GetLaneHeight(Index);
		if (LocalY >= Y && LocalY < Y + Height)
		{
			return Index;
		}
		Y += Height;
	}
	return INDEX_NONE;
}

FVector2D SFPSRGunMotionTimeline::ComputeDesiredSize(float) const
{
	return FVector2D(200.0, GRulerHeight + FMath::Max(GetTotalLanesHeight(), GLaneHeight));
}

float SFPSRGunMotionTimeline::LocalXToTime(float LocalX, float GeometryWidth) const
{
	const float SequenceLength = FMath::Max(0.0f, SequenceLengthAttr.Get());
	if (SequenceLength <= UE_KINDA_SMALL_NUMBER || GeometryWidth <= UE_KINDA_SMALL_NUMBER)
	{
		return 0.0f;
	}
	const float Fraction = FMath::Clamp(LocalX / GeometryWidth, 0.0f, 1.0f);
	return Fraction * SequenceLength;
}

float SFPSRGunMotionTimeline::TimeToLocalX(float Time, float GeometryWidth) const
{
	const float SequenceLength = FMath::Max(0.0f, SequenceLengthAttr.Get());
	if (SequenceLength <= UE_KINDA_SMALL_NUMBER)
	{
		return 0.0f;
	}
	return FMath::Clamp(Time / SequenceLength, 0.0f, 1.0f) * GeometryWidth;
}

float SFPSRGunMotionTimeline::SnapTimeToFrame(float Time) const
{
	const float FrameRate = FrameRateAttr.Get();
	if (FrameRate <= UE_KINDA_SMALL_NUMBER)
	{
		return Time;
	}
	const float SequenceLength = FMath::Max(0.0f, SequenceLengthAttr.Get());
	return FMath::Clamp(FMath::RoundToFloat(Time * FrameRate) / FrameRate, 0.0f, SequenceLength);
}

int32 SFPSRGunMotionTimeline::FindKeyIndexAtLocalX(const FFPSRGunMotionTimelineLane& Lane, float LocalX, float GeometryWidth) const
{
	int32 BestIndex = INDEX_NONE;
	float BestDist = GHitTestPixels;
	for (int32 KeyIndex = 0; KeyIndex < Lane.KeyTimes.Num(); ++KeyIndex)
	{
		const float XPos = TimeToLocalX(Lane.KeyTimes[KeyIndex], GeometryWidth);
		const float Dist = FMath::Abs(XPos - LocalX);
		if (Dist <= BestDist)
		{
			BestDist = Dist;
			BestIndex = KeyIndex;
		}
	}
	return BestIndex;
}

int32 SFPSRGunMotionTimeline::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const bool bEnabled = ShouldBeEnabled(bParentEnabled);
	const ESlateDrawEffect DrawEffects = bEnabled ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect;

	const float Width = static_cast<float>(AllottedGeometry.GetLocalSize().X);
	const float Height = static_cast<float>(AllottedGeometry.GetLocalSize().Y);
	const float SequenceLength = FMath::Max(0.0f, SequenceLengthAttr.Get());

	// 🚨 스타일 키 무의존: "WhiteBrush"/"Brushes.Recessed" 를 FAppStyle 에서 찾으면 키가 스타일셋에 없을 때
	// 미싱 브러시(흰색)로 조용히 폴백해 타임라인 전체가 흰 판으로 보인다(실사고 2026-08-09). FCoreStyle 의
	// WhiteBrush 는 항상 존재하므로 그것만 쓰고 색은 전부 명시 틴트로 준다.
	const FSlateBrush* WhiteBrush = FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
	const FSlateFontInfo TickFont = FCoreStyle::GetDefaultFontStyle("Regular", 8);
	const TSharedRef<FSlateFontMeasure> FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const FLinearColor RulerColor(0.72f, 0.72f, 0.72f, 1.0f);
	const FLinearColor BackgroundColor(0.015f, 0.015f, 0.018f, 1.0f);
	const FLinearColor LaneSeparatorColor(0.08f, 0.08f, 0.09f, 1.0f);
	const FLinearColor ActiveLaneTint(0.2f, 0.4f, 0.9f, 0.16f);
	const FLinearColor NormalKeyColor(0.65f, 0.75f, 1.0f, 1.0f);
	const FLinearColor SelectedKeyColor(0.95f, 0.58f, 0.10f, 1.0f);

	int32 CurrentLayer = LayerId;

	// 배경.
	FSlateDrawElement::MakeBox(
		OutDrawElements, CurrentLayer,
		AllottedGeometry.ToPaintGeometry(),
		WhiteBrush,
		DrawEffects, BackgroundColor);
	++CurrentLayer;

	// --- v3 §20-3: 레인 배경(활성 레인 하이라이트) + 레인 구분선 ---
	const TArray<FFPSRGunMotionTimelineLane> Lanes = LanesAttr.Get();
	const FName ActiveChannelId = ActiveChannelIdAttr.Get();
	for (int32 LaneIndex = 0; LaneIndex < Lanes.Num(); ++LaneIndex)
	{
		const float LaneY = GetLaneY(LaneIndex);
		const float LaneH = GetLaneHeight(LaneIndex);

		if (Lanes[LaneIndex].ChannelId == ActiveChannelId)
		{
			FSlateDrawElement::MakeBox(
				OutDrawElements, CurrentLayer,
				AllottedGeometry.ToPaintGeometry(FVector2D(Width, LaneH), FSlateLayoutTransform(FVector2D(0.0f, LaneY))),
				WhiteBrush, DrawEffects, ActiveLaneTint);
		}

		TArray<FVector2f> SepPoints;
		SepPoints.Add(FVector2f(0.0f, LaneY + LaneH));
		SepPoints.Add(FVector2f(Width, LaneY + LaneH));
		FSlateDrawElement::MakeLines(
			OutDrawElements, CurrentLayer,
			AllottedGeometry.ToPaintGeometry(),
			SepPoints, DrawEffects, LaneSeparatorColor, true, 1.0f);
	}
	++CurrentLayer;

	// --- §16 시간 룰러: 초 주눈금 + 0.1s 보조눈금 + 숫자 라벨 ---
	if (SequenceLength > UE_KINDA_SMALL_NUMBER && Width > 0.0f)
	{
		const int32 NumTenths = FMath::CeilToInt(SequenceLength / 0.1f);
		for (int32 TenthIndex = 0; TenthIndex <= NumTenths; ++TenthIndex)
		{
			const float TickTime = FMath::Min(TenthIndex * 0.1f, SequenceLength);
			const float XPos = TimeToLocalX(TickTime, Width);
			// 정수 나눗셈으로 "매 10번째 0.1s = 1초"를 판정한다 — 부동소수 누적 오차를 fmod 로 판정하면 흔들릴 수 있다.
			const bool bMajor = (TenthIndex % 10) == 0;
			const float TickHeight = bMajor ? GMajorTickHeight : GMinorTickHeight;

			TArray<FVector2f> TickPoints;
			TickPoints.Add(FVector2f(XPos, GRulerHeight - TickHeight));
			TickPoints.Add(FVector2f(XPos, GRulerHeight));
			FSlateDrawElement::MakeLines(
				OutDrawElements, CurrentLayer,
				AllottedGeometry.ToPaintGeometry(),
				TickPoints, DrawEffects, RulerColor, true, 1.0f);

			if (bMajor)
			{
				const FString Label = FString::Printf(TEXT("%ds"), FMath::RoundToInt(TickTime));
				const FVector2D LabelSize = FontMeasure->Measure(Label, TickFont);
				const FVector2D LabelPos(FMath::Max(0.0f, XPos - static_cast<float>(LabelSize.X) * 0.5f), 0.0f);
				FSlateDrawElement::MakeText(
					OutDrawElements, CurrentLayer,
					AllottedGeometry.ToPaintGeometry(LabelSize, FSlateLayoutTransform(LabelPos)),
					Label, TickFont, DrawEffects, RulerColor);
			}

			if (TickTime >= SequenceLength)
			{
				break;
			}
		}
	}
	++CurrentLayer;

	// --- v3 §20-3: 레인별 키 마커(◆) — 선택 키(활성 레인 안에서만)는 하이라이트 ---
	const int32 SelectedIndex = SelectedKeyIndexAttr.Get();
	for (int32 LaneIndex = 0; LaneIndex < Lanes.Num(); ++LaneIndex)
	{
		const FFPSRGunMotionTimelineLane& Lane = Lanes[LaneIndex];
		const float LaneCenterY = GetLaneY(LaneIndex) + GetLaneHeight(LaneIndex) * 0.5f;
		const bool bLaneActive = (Lane.ChannelId == ActiveChannelId);
		const float DiamondSize = Lane.bSubLane ? GSubLaneKeyDiamondSize : GKeyDiamondSize;

		for (int32 KeyIndex = 0; KeyIndex < Lane.KeyTimes.Num(); ++KeyIndex)
		{
			// 드래그 중인 키는 커밋 전이라도 시각적으로는 손끝을 따라간다(§16 "종료 시에만 커밋" — 데이터는 안 바뀐다).
			const bool bDraggingThis = bDraggingKey && DraggingChannelId == Lane.ChannelId && DraggingKeyIndex == KeyIndex;
			const float KeyTime = bDraggingThis ? DragCurrentTime : Lane.KeyTimes[KeyIndex];
			const float XPos = TimeToLocalX(KeyTime, Width);
			const bool bSelected = bLaneActive && (KeyIndex == SelectedIndex);
			const FLinearColor Tint = bSelected ? SelectedKeyColor : NormalKeyColor;

			const FVector2D DiamondPos(XPos - DiamondSize * 0.5f, LaneCenterY - DiamondSize * 0.5f);
			FSlateDrawElement::MakeRotatedBox(
				OutDrawElements, bSelected ? CurrentLayer + 1 : CurrentLayer,
				AllottedGeometry.ToPaintGeometry(FVector2D(DiamondSize, DiamondSize), FSlateLayoutTransform(DiamondPos)),
				WhiteBrush, DrawEffects, PI * 0.25f,
				TOptional<FVector2f>(), FSlateDrawElement::RelativeToElement, Tint);
		}
	}
	CurrentLayer += 2;

	// --- §16 플레이헤드: 세로선 + 상단 핸들 ---
	const float PlayheadX = TimeToLocalX(ScrubPositionAttr.Get(), Width);
	TArray<FVector2f> PlayheadPoints;
	PlayheadPoints.Add(FVector2f(PlayheadX, 0.0f));
	PlayheadPoints.Add(FVector2f(PlayheadX, Height));
	FSlateDrawElement::MakeLines(
		OutDrawElements, CurrentLayer,
		AllottedGeometry.ToPaintGeometry(),
		PlayheadPoints, DrawEffects, FLinearColor::White, true, 2.0f);

	const FVector2D HandlePos(PlayheadX - GPlayheadHandleWidth * 0.5f, 0.0f);
	FSlateDrawElement::MakeBox(
		OutDrawElements, CurrentLayer + 1,
		AllottedGeometry.ToPaintGeometry(FVector2D(GPlayheadHandleWidth, GPlayheadHandleHeight), FSlateLayoutTransform(HandlePos)),
		WhiteBrush, DrawEffects, FLinearColor::White);

	return CurrentLayer + 1;
}

FReply SFPSRGunMotionTimeline::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	const FVector2D Local = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	const float LocalX = static_cast<float>(Local.X);
	const float LocalY = static_cast<float>(Local.Y);
	const float Width = static_cast<float>(MyGeometry.GetLocalSize().X);

	const TArray<FFPSRGunMotionTimelineLane> Lanes = LanesAttr.Get();
	const int32 LaneIndex = FindLaneIndexAtLocalY(LocalY);
	const bool bInLane = Lanes.IsValidIndex(LaneIndex);
	const int32 HitKeyIndex = bInLane ? FindKeyIndexAtLocalX(Lanes[LaneIndex], LocalX, Width) : INDEX_NONE;

	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		DragDistanceAccum = 0.0f;

		if (bInLane)
		{
			// §20-3: "레인 클릭 = 활성 전환" — 키를 맞추지 못한 빈 곳 클릭이라도 그 레인은 활성이 된다.
			OnChannelSelectedDelegate.ExecuteIfBound(Lanes[LaneIndex].ChannelId);
		}

		if (HitKeyIndex != INDEX_NONE)
		{
			// §16: "키 클릭 = 선택 + 스크럽을 그 키 시각으로 점프".
			bDraggingKey = true;
			DraggingChannelId = Lanes[LaneIndex].ChannelId;
			DraggingKeyIndex = HitKeyIndex;
			DragCurrentTime = Lanes[LaneIndex].KeyTimes[HitKeyIndex];
			OnKeySelectedDelegate.ExecuteIfBound(DraggingChannelId, HitKeyIndex);
		}
		else
		{
			// §16: "빈 곳 클릭/드래그 = 스크럽(기존 슬라이더와 동일 경로)" — 룰러 영역도 여기로 떨어진다.
			bScrubbing = true;
			OnScrubCaptureBeginDelegate.ExecuteIfBound();
			OnScrubPositionChangedDelegate.ExecuteIfBound(LocalXToTime(LocalX, Width));
		}
		return FReply::Handled().CaptureMouse(SharedThis(this)).PreventThrottling();
	}
	else if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		// §16: "키 우클릭 = 컨텍스트 메뉴 [키 삭제]" — 빈 곳 우클릭은 무동작.
		if (HitKeyIndex != INDEX_NONE)
		{
			OpenKeyContextMenu(Lanes[LaneIndex].ChannelId, HitKeyIndex, MouseEvent);
			return FReply::Handled();
		}
	}

	return FReply::Unhandled();
}

FReply SFPSRGunMotionTimeline::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!HasMouseCapture())
	{
		return FReply::Unhandled();
	}

	const float LocalX = static_cast<float>(MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()).X);
	const float Width = static_cast<float>(MyGeometry.GetLocalSize().X);

	if (bDraggingKey)
	{
		DragDistanceAccum += FMath::Abs(MouseEvent.GetCursorDelta().X);
		// §16: "키 드래그 = 프레임 스냅 Time 변경" — 드래그 중엔 시각 미리보기만 갱신, 커밋은 마우스업에서.
		DragCurrentTime = SnapTimeToFrame(LocalXToTime(LocalX, Width));
		return FReply::Handled();
	}
	if (bScrubbing)
	{
		OnScrubPositionChangedDelegate.ExecuteIfBound(LocalXToTime(LocalX, Width));
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

FReply SFPSRGunMotionTimeline::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton || !HasMouseCapture())
	{
		return FReply::Unhandled();
	}

	if (bDraggingKey)
	{
		// §16: "드래그 종료 시에만 커밋" — 실제로 움직인 경우만 커밋해 단순 클릭이 무동작 트랜잭션을 만들지 않게 한다.
		if (DragDistanceAccum > GDragThresholdPixels && DraggingKeyIndex != INDEX_NONE)
		{
			OnKeyTimeCommittedDelegate.ExecuteIfBound(DraggingChannelId, DraggingKeyIndex, DragCurrentTime);
		}
	}

	bDraggingKey = false;
	DraggingChannelId = NAME_None;
	DraggingKeyIndex = INDEX_NONE;
	bScrubbing = false;
	DragDistanceAccum = 0.0f;

	return FReply::Handled().ReleaseMouseCapture();
}

FReply SFPSRGunMotionTimeline::OnMouseButtonDoubleClick(const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}

	const FVector2D Local = InMyGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
	const float LocalX = static_cast<float>(Local.X);
	const float LocalY = static_cast<float>(Local.Y);
	const float Width = static_cast<float>(InMyGeometry.GetLocalSize().X);

	const TArray<FFPSRGunMotionTimelineLane> Lanes = LanesAttr.Get();
	const int32 LaneIndex = FindLaneIndexAtLocalY(LocalY);
	if (!Lanes.IsValidIndex(LaneIndex))
	{
		return FReply::Unhandled();
	}

	// §16: "빈 곳 더블클릭" 만 규정한다 — 기존 키 위 더블클릭은 무동작(중복 키 방지).
	if (FindKeyIndexAtLocalX(Lanes[LaneIndex], LocalX, Width) != INDEX_NONE)
	{
		return FReply::Unhandled();
	}

	const float Time = LocalXToTime(LocalX, Width);
	OnChannelSelectedDelegate.ExecuteIfBound(Lanes[LaneIndex].ChannelId);
	OnKeyAddRequestedDelegate.ExecuteIfBound(Lanes[LaneIndex].ChannelId, Time);
	return FReply::Handled();
}

FCursorReply SFPSRGunMotionTimeline::OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const
{
	const FVector2D Local = MyGeometry.AbsoluteToLocal(CursorEvent.GetScreenSpacePosition());
	const float LocalX = static_cast<float>(Local.X);
	const float LocalY = static_cast<float>(Local.Y);
	const float Width = static_cast<float>(MyGeometry.GetLocalSize().X);

	const TArray<FFPSRGunMotionTimelineLane> Lanes = LanesAttr.Get();
	const int32 LaneIndex = FindLaneIndexAtLocalY(LocalY);
	const bool bOverKey = Lanes.IsValidIndex(LaneIndex) && FindKeyIndexAtLocalX(Lanes[LaneIndex], LocalX, Width) != INDEX_NONE;

	if (bDraggingKey || bOverKey)
	{
		return FCursorReply::Cursor(EMouseCursor::ResizeLeftRight);
	}
	return FCursorReply::Cursor(EMouseCursor::Default);
}

void SFPSRGunMotionTimeline::OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent)
{
	// 드래그 도중 캡처를 잃으면(알트탭 등) 커밋하지 않고 그냥 버린다 — 마우스업 없이 데이터를 바꾸지 않는다(§16).
	bDraggingKey = false;
	DraggingChannelId = NAME_None;
	DraggingKeyIndex = INDEX_NONE;
	bScrubbing = false;
	DragDistanceAccum = 0.0f;
}

void SFPSRGunMotionTimeline::OpenKeyContextMenu(FName ChannelId, int32 KeyIndex, const FPointerEvent& MouseEvent)
{
	FMenuBuilder MenuBuilder(/*bShouldCloseWindowAfterMenuSelection=*/true, nullptr);
	MenuBuilder.AddMenuEntry(
		LOCTEXT("DeleteKeyMenuEntry", "키 삭제"),
		FText(),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this, ChannelId, KeyIndex]()
		{
			OnKeyDeleteRequestedDelegate.ExecuteIfBound(ChannelId, KeyIndex);
		})));

	const FWidgetPath WidgetPath = MouseEvent.GetEventPath() != nullptr ? *MouseEvent.GetEventPath() : FWidgetPath();
	FSlateApplication::Get().PushMenu(AsShared(), WidgetPath, MenuBuilder.MakeWidget(), MouseEvent.GetScreenSpacePosition(), FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu));
}

#undef LOCTEXT_NAMESPACE
