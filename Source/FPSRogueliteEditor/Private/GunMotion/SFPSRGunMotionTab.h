// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Types/SlateEnums.h"   // ECheckBoxState — §12 자유시점 체크박스

class UAnimSequence;
class UAnimInstance;
class UAnimMontage;
class UFPSRGunMotionAuthoringData;
class SVerticalBox;
class STextBlock;
class FAdvancedPreviewScene;
class SFPSRGunMotionViewport;
class SFPSRGunMotionTimeline;
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
	~SFPSRGunMotionTab();

	/** 매 틱(SWidget) — §14 PIE 라이브 링크 유효성 재검사 + 미러 몽타주 재생/스크럽 동기화. SFPSRWeaponAssemblerFPTab::
	 *  Tick 과 같은 근거(연결 상태를 매 프레임 재확인하는 이 프로젝트의 기존 관용구). */
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

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

	// --- 타임라인 스크럽/재생 ---
	float GetScrubPosition() const;
	void OnScrubPositionChanged(float NewValue);
	void OnScrubCaptureBegin();
	FReply OnPlayPauseClicked();
	FText GetPlayPauseLabel() const;

	// --- 증보 v2.3 §16: 타임라인 위젯(SFPSRGunMotionTimeline) ------------------------------------------------------

	/** 타임라인의 SequenceLength 어트리뷰트 — 기존 스크럽 슬라이더가 SetMinAndMaxValues 로 한 번 동기화하던 것을
	 *  매 페인트 읽는 어트리뷰트로 바꿨다(위젯이 룰러/플레이헤드를 그때그때 다시 그리므로 별도 동기화 호출이
	 *  필요 없다). */
	float GetTimelineSequenceLength() const;
	/** 타임라인의 FrameRate 어트리뷰트 — 키 드래그의 프레임 스냅 기준(§16). */
	float GetTimelineFrameRate() const;
	/** 타임라인이 매 페인트 그릴 저작 키 목록(AssetUserData 사본 — 위젯은 표시/조작만, 소유는 여전히 탭). */
	TArray<FFPSRGunMotionKey> GetTimelineKeys() const;
	/** 선택된 키 인덱스 — 숫자 목록 행 강조와 타임라인 다이아몬드 강조가 공유하는 단일 상태(§16 "동기화"). */
	int32 GetSelectedKeyIndex() const { return SelectedKeyIndex; }
	/** 타임라인 키 클릭(§16) — 선택 상태를 갱신하고 기존 스크럽 경로(일시정지 후 이동)로 그 키 시각으로 점프한다. */
	void OnTimelineKeySelected(int32 KeyIndex);

	// --- 기즈모 툴바(§9) ---
	FReply OnTranslateModeClicked();
	FReply OnRotateModeClicked();
	/** "현재 시각에 키" — 지금 스크럽 시각에 키가 없으면 지금 무기 오프셋(기즈모로 만진 값이 있다면 그 값, 없으면
	 *  이전 값 유지)으로 새 키를 추가한다. 값 자체는 TrackingStopped 경로와 동일하게 GunBase/GunNow 역산으로 구한다. */
	FReply OnKeyAtCurrentTimeClicked();
	/** "키 삭제" — 현재 스크럽 시각에 가장 가까운 키를 지운다(±1프레임 이내). 없으면 무동작. */
	FReply OnDeleteKeyAtCurrentTimeClicked();

	// --- 증보 v2.1 §11-§12: PIE 구도 캡처 + 자유시점 --------------------------------------------------------------

	/** [PIE 구도 캡처] — PIE 의 로컬 캐릭터에서 FirstPersonCamera/FirstPersonArms 의 실제 월드 트랜스폼과
	 *  GetCameraView 의 FOV 를 읽어 설정에 저장하고(TryUpdateDefaultConfigFile), 뷰포트 구도를 즉시 재적용한다(§11). */
	FReply OnCaptureCompositionClicked();

	/** 자유시점 체크박스(§12) — 뷰포트 클라이언트의 bFreeLook 을 그대로 비춘다. */
	ECheckBoxState GetFreeLookState() const;
	void OnFreeLookStateChanged(ECheckBoxState NewState);

	/** 상태줄 — 지금 구도가 캡처값인지 폴백(BP 프로브)인지(§11 "상태줄에 어느 소스인지 표시"). */
	FText GetCompositionSourceText() const;

	// --- 증보 v2.2 §14: PIE 라이브 링크 -----------------------------------------------------------------------------

	/** [PIE 라이브 링크] 체크박스(기본 ON, §14). */
	ECheckBoxState GetPIELiveLinkState() const;
	void OnPIELiveLinkStateChanged(ECheckBoxState NewState);

	/** 상태줄 — "PIE 링크 활성" / "PIE 없음"(§14 축자) — 토글이 꺼져 있으면 별도 문구. */
	FText GetPIELiveLinkStatusText() const;

	/** 매 틱(Tick 이 호출) — PIE 로컬 AFPSRCharacter 의 FirstPersonArms AnimInstance 를 재조회해 미러 연결을
	 *  갱신하고(끊기면 조용히 해제, 재개되면 자동 재연결), 연결돼 있으면 재생 상태·스크럽 위치를 동기화한다. */
	void UpdatePIEMirror();

	/** 미러 몽타주를 지금 프리뷰 클립으로 (재)시작한다 — 최초 연결/재연결/재굽기-재시작이 공유하는 단일 경로.
	 *  InTimeToStartMontageAt 에 현재 스크럽 위치를 넘겨 시작 즉시 그 위치에서 출발하게 하고(§14 "스크럽 위치로
	 *  복원"), 탭이 재생 중이 아니면 시작 직후 바로 Montage_Pause 한다. */
	void StartOrRestartMirrorMontage(UAnimInstance& AnimInstance, UAnimSequence& PreviewClip, float BlendTime);

	/** 재굽기(RebakeViewportPreview) 직후 호출(§14) — 미러가 연결돼 있으면 몽타주를 정지 후(블렌드 0.05) 같은
	 *  블렌드로 재시작해 갱신된 압축 데이터를 확실히 다시 읽게 한다. 미러가 연결돼 있지 않으면 무동작(다음 Tick 의
	 *  UpdatePIEMirror 가 알아서 연결한다). */
	void ResyncMirrorAfterRebake();

	/** 미러 몽타주 정리(§14 "탭/클립 닫기 시 Montage_Stop(0.1)") — 소멸자·PIE 라이브 링크 OFF 전환·PIE/클립 소실
	 *  공통 경로. */
	void StopMirrorMontage();

	/** [PIE 라이브 링크] 토글 상태(기본 ON). */
	bool bPIELiveLinkEnabled = true;

	/** 미러가 지금 물고 있는 PIE AnimInstance/몽타주/프리뷰클립. 전부 PIE 월드(또는 트랜지언트 패키지) 소유라
	 *  TWeakObjectPtr(§14 수명주기 — 매 틱 유효성 검사, PIE 종료/리스폰 시 조용히 해제). */
	TWeakObjectPtr<UAnimInstance> MirrorAnimInstance;
	TWeakObjectPtr<UAnimMontage> MirrorMontage;
	TWeakObjectPtr<UAnimSequence> MirrorPlayingClip;

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

	/** 증보 v2.3 §16: 타임라인 위젯(룰러+플레이헤드+키 다이아몬드) — 기존 스크럽 슬라이더(SSlider) + §8 키 시각
	 *  마커 줄을 대체한다. */
	TSharedPtr<SFPSRGunMotionTimeline> Timeline;

	/** §16 "숫자 목록 행 선택과 동기화" — 타임라인에서 클릭/드래그로 선택된 키 인덱스(없으면 INDEX_NONE). */
	int32 SelectedKeyIndex = INDEX_NONE;

	/** §11: 지금 구도가 캡처값인지 폴백인지 표시하는 상태줄(GetCompositionSourceText). */
	TSharedPtr<STextBlock> CompositionStatusText;
};
