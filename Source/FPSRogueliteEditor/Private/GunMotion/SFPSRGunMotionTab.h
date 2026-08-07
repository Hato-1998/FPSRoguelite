// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class UAnimSequence;
class UFPSRGunMotionAuthoringData;
class SVerticalBox;
class SHorizontalBox;
class STextBlock;
class SSlider;
class FAdvancedPreviewScene;
class SFPSRGunMotionViewport;
struct FAssetData;
struct FFPSRGunMotionKey;

/**
 * 총 모션 저작 툴 탭(GunMotionTool_Spec.md §4) — 클립 선택 → [총 고정화] → 카메라 공간 키 저작 → [클립에 굽기] →
 * [PIE에서 재생] 워크플로 하나로 묶는다. 실제 수학은 전부 FPSRGunMotionBaker(순수 로직)에 있고, 이 위젯은 그
 * 호출과 저작 키(AssetUserData) 편집 UI 만 담당한다.
 *
 * 클립은 TWeakObjectPtr 로만 들고 있는다(§5: 탭 위젯이 클립 강참조를 들지 말 것) — 탭을 열어 둔 채 클립을
 * 삭제/리로드해도 위젯이 그 참조 하나 때문에 GC 를 막지 않는다.
 */
class SFPSRGunMotionTab : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SFPSRGunMotionTab) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	// --- 클립 선택 ---
	FString GetSequenceObjectPath() const;
	void OnSequenceChanged(const FAssetData& AssetData);
	UAnimSequence* GetSequence() const;
	UFPSRGunMotionAuthoringData* GetOrCreateAuthoringData(bool bCreateIfMissing);

	// --- 상태 줄(§4-2) ---
	FText GetStatusText() const;
	bool IsClipAdditive() const;

	/** §5: 클립 경로가 Anims_LPAMG 밖이거나 _GunLocked/_GunMotion 접미가 없으면 계속/취소 경고. true = 계속 진행
	 *  (클립이 없으면 무조건 true — 버튼 자체 핸들러가 별도로 "클립 없음"을 처리한다). */
	bool ConfirmIfOutsideConvention() const;

	void SetStatus(const FText& InText);

	// --- 액션 버튼(§4-3·5·6) ---
	FReply OnSanitizeClicked();
	FReply OnBakeClicked();
	FReply OnPlayInPIEClicked();

	// --- 키 목록(§4-4) ---
	void RebuildKeyRows();
	TSharedRef<SWidget> BuildKeyRow(int32 KeyIndex);
	FReply OnAddKeyClicked();
	FReply OnRemoveKeyClicked(int32 KeyIndex);
	FReply OnApplyDefaultTemplateClicked();

	/** 키 하나의 필드를 트랜잭션 안에서 고친다 — 6개 성분(Right/Up/Fwd/Pitch/Yaw/Roll)+Time 세터가 공유하는
	 *  단일 경로(FScopedTransaction + AssetUserData::Modify + Seq::MarkPackageDirty). */
	void MutateKey(int32 KeyIndex, const FText& TransactionText, TFunctionRef<void(FFPSRGunMotionKey&)> Mutator);

	float GetKeyTime(int32 KeyIndex) const;
	void SetKeyTime(int32 KeyIndex, float NewValue);
	float GetKeyRight(int32 KeyIndex) const;
	void SetKeyRight(int32 KeyIndex, float NewValue);
	float GetKeyUp(int32 KeyIndex) const;
	void SetKeyUp(int32 KeyIndex, float NewValue);
	float GetKeyFwd(int32 KeyIndex) const;
	void SetKeyFwd(int32 KeyIndex, float NewValue);
	float GetKeyPitch(int32 KeyIndex) const;
	void SetKeyPitch(int32 KeyIndex, float NewValue);
	float GetKeyYaw(int32 KeyIndex) const;
	void SetKeyYaw(int32 KeyIndex, float NewValue);
	float GetKeyRoll(int32 KeyIndex) const;
	void SetKeyRoll(int32 KeyIndex, float NewValue);

	// --- "풀 오프셋" 입력값(§3-3 기본 템플릿이 읽는 값) ---
	TOptional<float> GetFullOffsetRight() const { return FullOffsetRight; }
	void SetFullOffsetRight(float V) { FullOffsetRight = V; }
	TOptional<float> GetFullOffsetUp() const { return FullOffsetUp; }
	void SetFullOffsetUp(float V) { FullOffsetUp = V; }
	TOptional<float> GetFullOffsetFwd() const { return FullOffsetFwd; }
	void SetFullOffsetFwd(float V) { FullOffsetFwd = V; }
	TOptional<float> GetFullOffsetPitch() const { return FullOffsetPitch; }
	void SetFullOffsetPitch(float V) { FullOffsetPitch = V; }
	TOptional<float> GetFullOffsetYaw() const { return FullOffsetYaw; }
	void SetFullOffsetYaw(float V) { FullOffsetYaw = V; }
	TOptional<float> GetFullOffsetRoll() const { return FullOffsetRoll; }
	void SetFullOffsetRoll(float V) { FullOffsetRoll = V; }

	// --- 증보 v2 §7-§9: 뷰포트 + 타임라인 + 기즈모 ---------------------------------------------------------------

	/** 클립 선택이 바뀌거나(§8) [총 고정화]가 새로 성공했을 때(예전 프리뷰 클립은 고정화 이전 상태로 고정된 채라
	 *  못 쓴다) 호출 — 뷰포트에 원본 클립을 다시 복제해 얹고 베이스라인부터 다시 잡는다. */
	void RebuildViewportPreview();

	/** 키가 바뀔 때마다(§8 "항상 구운 결과를 본다") 호출 — 현재 AssetUserData 의 Keys 로 프리뷰 클립을 다시 굽는다.
	 *  숫자 키 목록 편집과 기즈모 편집이 공유하는 단일 경로(§7 헤더 "같은 Keys 데이터를 두 UI 가 편집한다"). */
	void RebakeViewportPreview();

	/** 기즈모 TrackingStopped 커밋(§9) — 같은 시각(±1프레임)의 키가 있으면 갱신, 없으면 추가한 뒤 재굽기. */
	void OnGizmoKeyCommitted(float Time, FVector CamOffset, FRotator CamRotation);

	/** Time 에 가장 가까운 키 인덱스(±ToleranceSeconds 이내만) — 없으면 INDEX_NONE. §9 "같은 시각(±1프레임)의 키". */
	int32 FindKeyIndexNearTime(float Time, float ToleranceSeconds) const;

	/** 타임라인 아래 키 시각 마커 줄을 다시 그린다(§8). */
	void RebuildTimelineMarkers();

	// --- 타임라인 스크럽/재생 ---
	float GetScrubPosition() const;
	void OnScrubPositionChanged(float NewValue);
	void OnScrubCaptureBegin();
	FReply OnPlayPauseClicked();
	FText GetPlayPauseLabel() const;

	// --- 기즈모 툴바(§9) ---
	FReply OnTranslateModeClicked();
	FReply OnRotateModeClicked();
	/** "현재 시각에 키" — 지금 스크럽 시각에 키가 없으면 지금 무기 오프셋(기즈모로 만진 값이 있다면 그 값, 없으면
	 *  이전 값 유지)으로 새 키를 추가한다. 값 자체는 TrackingStopped 경로와 동일하게 GunBase/GunNow 역산으로 구한다. */
	FReply OnKeyAtCurrentTimeClicked();
	/** "키 삭제" — 현재 스크럽 시각에 가장 가까운 키를 지운다(±1프레임 이내). 없으면 무동작. */
	FReply OnDeleteKeyAtCurrentTimeClicked();

	TWeakObjectPtr<UAnimSequence> SelectedSequence;

	TSharedPtr<STextBlock> StatusText;
	TSharedPtr<STextBlock> ActionStatusText;
	TSharedPtr<SVerticalBox> KeyListContainer;

	// 기본값(스펙 §3-3): Right+3 / Up+6cm, Pitch12 / Roll10 도.
	float FullOffsetRight = 3.0f;
	float FullOffsetUp = 6.0f;
	float FullOffsetFwd = 0.0f;
	float FullOffsetPitch = 12.0f;
	float FullOffsetYaw = 0.0f;
	float FullOffsetRoll = 10.0f;

	// --- 증보 v2 §7-§9 ---------------------------------------------------------------------------------------------

	/** 이 탭이 단독 소유하는 프리뷰 씬(§7 — 어셈블러 씬 공유 금지). */
	TSharedPtr<FAdvancedPreviewScene> PreviewScene;
	TSharedPtr<SFPSRGunMotionViewport> Viewport;

	TSharedPtr<SSlider> ScrubSlider;
	/** 타임라인 아래 키 시각 마커 줄(§8). RebuildTimelineMarkers 가 채운다. */
	TSharedPtr<SHorizontalBox> KeyMarkerContainer;
};
