// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Types/SlateEnums.h"   // ECheckBoxState — §12 자유시점 체크박스
#include "GunMotion/FPSRGunMotionChannelId.h"   // v3 §20-3: 채널 id/종류
#include "GunMotion/SFPSRGunMotionTimeline.h"   // v3 §20-3: FFPSRGunMotionTimelineLane — GetTimelineLanes() 반환 타입

class UAnimSequence;
class UAnimInstance;
class UAnimMontage;
class UFPSRGunMotionAuthoringData;
class UFPSRWeaponDataAsset;
class SVerticalBox;
class STextBlock;
class SExpandableArea;
class FAdvancedPreviewScene;
class SFPSRGunMotionViewport;
struct FAssetData;
struct FFPSRGunMotionKey;
struct FFPSRGunMotionChannelKey;
struct FFPSRGunMotionScalarKey;
struct FFPSRGunMotionChannelTrack;
template <typename OptionType> class SComboBox;

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
	 *  숫자 키 목록 편집과 기즈모 편집이 공유하는 단일 경로(§7 헤더 "같은 Keys 데이터를 두 UI 가 편집한다"). v3
	 *  §20-4: 이제 레거시 Keys 뿐 아니라 AuthoringData 전체(§18 채널 트랙)를 넘겨 뷰포트 직접 평가도 갱신한다. */
	void RebakeViewportPreview();

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
	/** 선택된 키 인덱스 — 숫자 목록 행 강조와 타임라인 다이아몬드 강조가 공유하는 단일 상태(§16 "동기화"). 활성
	 *  채널이 바뀌면 SetActiveChannelId 가 INDEX_NONE 으로 되돌린다(다른 채널의 인덱스를 잘못 가리키지 않도록). */
	int32 GetSelectedKeyIndex() const { return SelectedKeyIndex; }

	// --- 기즈모 툴바(§9, v3 §20-4 에서 "활성 채널 대상"으로 일반화) ---
	FReply OnTranslateModeClicked();
	FReply OnRotateModeClicked();
	/** "현재 시각에 키" — 지금 스크럽 시각에 활성 채널 키가 없으면 그 채널의 현재 평가값으로 새 키를 추가한다
	 *  (§20-3 일반화 — v2 시절엔 무기 채널 고정이었다). */
	FReply OnKeyAtCurrentTimeClicked();
	/** "키 삭제" — 현재 스크럽 시각에 가장 가까운 활성 채널 키를 지운다(±1프레임 이내). 없으면 무동작. */
	FReply OnDeleteKeyAtCurrentTimeClicked();

	// --- v3 §20-3~§20-5: 채널 상태·타임라인 레인·부착 드롭다운·[새 액션 클립]·채널 커브 베이크 ------------------------

	/** 지금 기즈모/숫자 목록/타임라인 하이라이트가 가리키는 채널(§20-3). 소유자는 이 탭 — 뷰포트 클라이언트는
	 *  SetActiveChannelId 가 미는 상태를 그대로 반영만 한다(FFPSRGunMotionViewportClient::SetActiveChannel). */
	FName GetActiveChannelId() const { return SelectedChannelId; }
	/** 활성 채널 전환(레인 클릭·드롭다운·히트 프록시 클릭 공통 경로) — 뷰포트에도 되비추고, 다른 채널의 선택 키
	 *  인덱스를 안 들고 가도록 SelectedKeyIndex 를 리셋한다. */
	void SetActiveChannelId(FName NewChannelId);

	/** §20-3 타임라인 레인 목록(총/왼손(+Blend)/오른손(+Blend)/파츠×N) — 탭이 매 페인트 AuthoringData 에서 다시
	 *  구성한다(위젯은 표시/조작만). */
	TArray<FFPSRGunMotionTimelineLane> GetTimelineLanes() const;
	/** 레인 클릭(빈 곳 포함) = 활성 채널 전환. */
	void OnTimelineChannelSelected(FName ChannelId);
	/** 키 클릭 = 선택 + 그 키 시각으로 스크럽 점프(§16, 채널 id 포함). */
	void OnTimelineChannelKeySelected(FName ChannelId, int32 KeyIndex);
	/** 키 드래그 종료 커밋(§16) — MutateChannelKey/MutateBlendKey 단일 훅으로 이어진다. */
	void OnTimelineChannelKeyTimeCommitted(FName ChannelId, int32 KeyIndex, float NewTime);
	/** 키 우클릭 [키 삭제]. */
	void OnTimelineChannelKeyDeleteRequested(FName ChannelId, int32 KeyIndex);
	/** 빈 곳 더블클릭 = 그 채널·그 시각의 현재 보간값으로 키 추가(포즈 무점프, §16). */
	void OnTimelineChannelKeyAddRequested(FName ChannelId, float Time);

	/** 뷰포트 기즈모 TrackingStopped 커밋(§20-4) — 채널별 MutateChannelKey 로 이어지는 단일 진입점. 같은 시각
	 *  (±1프레임)의 키가 있으면 갱신, 없으면 추가(§9 규약 그대로, 채널 파라미터화). */
	void OnViewportChannelGizmoCommitted(FName ChannelId, float Time, FVector Loc, FRotator Rot);
	/** 뷰포트 히트 프록시 클릭(§20-4) — 활성 채널 전환. */
	void OnViewportChannelPicked(FName ChannelId);

	/** §18 채널 트랙 해석(총/왼손/오른손 고정 필드 3개 + 파츠는 PartTracks 맵) — 기존 MutateKey 단일 훅을 채널
	 *  id 로 파라미터화하는 데 쓰는 조회 헬퍼. bCreatePartIfMissing 이면 파츠 채널을 PartTracks 에 새로 만든다. */
	FFPSRGunMotionChannelTrack* ResolveChannelTrack(UFPSRGunMotionAuthoringData& AuthData, FName ChannelId, bool bCreatePartIfMissing) const;
	const FFPSRGunMotionChannelTrack* ResolveChannelTrackConst(const UFPSRGunMotionAuthoringData& AuthData, FName ChannelId) const;
	/** Blend 스칼라 트랙 해석(왼손/오른손 전용, §18 FPGM_HL_Blend/FPGM_HR_Blend). 그 외 채널은 null. */
	TArray<FFPSRGunMotionScalarKey>* ResolveScalarTrack(UFPSRGunMotionAuthoringData& AuthData, FName ChannelId) const;
	const TArray<FFPSRGunMotionScalarKey>* ResolveScalarTrackConst(const UFPSRGunMotionAuthoringData& AuthData, FName ChannelId) const;

	/** v2 MutateKey 의 §20-3 채널 파라미터화 — loc/rot 채널 키 하나를 트랜잭션 안에서 고친다(같은 꼬리:
	 *  FScopedTransaction + AssetUserData::Modify + Seq::MarkPackageDirty + RebakeViewportPreview). */
	void MutateChannelKey(FName ChannelId, int32 KeyIndex, const FText& TransactionText, TFunctionRef<void(FFPSRGunMotionChannelKey&)> Mutator);
	/** 위의 스칼라(Blend) 버전. */
	void MutateBlendKey(FName ChannelId, int32 KeyIndex, const FText& TransactionText, TFunctionRef<void(FFPSRGunMotionScalarKey&)> Mutator);

	/** Time 에 가장 가까운 그 채널의 키 인덱스(±ToleranceSeconds 이내만, 채널 종류에 따라 channel/scalar 트랙 중
	 *  하나를 본다) — 없으면 INDEX_NONE. §9 "같은 시각(±1프레임)의 키"의 §20-3 채널 파라미터화. */
	int32 FindChannelKeyIndexNearTime(FName ChannelId, float Time, float ToleranceSeconds) const;
	/** 그 채널의 KeyIndex 번째 키의 Time(채널/스칼라 공용) — 타임라인 키 클릭의 스크럽 점프가 쓴다. */
	float GetChannelKeyTime(FName ChannelId, int32 KeyIndex) const;

	/** §20-3 활성 채널의 숫자 키 목록을 다시 그린다(loc/rot 6열 또는 Blend Time/Value 2열, 채널 종류로 분기). */
	void RebuildChannelKeyRows();
	TSharedRef<SWidget> BuildChannelKeyRow(FName ChannelId, int32 KeyIndex);
	TSharedRef<SWidget> BuildBlendKeyRow(FName ChannelId, int32 KeyIndex);
	FReply OnAddChannelKeyClicked();
	FReply OnRemoveChannelKeyClicked(FName ChannelId, int32 KeyIndex);
	/** 활성 채널 숫자 목록 위의 헤더 텍스트("총 채널" / "왼손 채널" 등, §20-3). */
	FText GetActiveChannelHeaderText() const;

	// --- v3 §20-3: 부착 드롭다운(손 채널 전용, FPGM_HL/HR_Blend 가 재기저할 파츠 선택) ---------------------------------

	/** AttachOptions(콤보 옵션 캐시)를 Settings->PreviewWeaponData::WeaponParts 에서 다시 채운다(없음 포함). 매
	 *  페인트 새로 만들면 SComboBox 선택 상태가 깨지므로 클립 선택/파츠 목록이 바뀔 때만 호출한다. */
	void RebuildAttachOptions();
	TSharedRef<SWidget> BuildAttachComboRow();
	/** 활성 채널(왼손/오른손)의 지금 AttachPartSocket 라벨. */
	FText GetAttachComboLabel() const;
	void OnAttachOptionSelected(TSharedPtr<FName> NewSelection, ESelectInfo::Type);
	TSharedRef<SWidget> GenerateAttachOptionWidget(TSharedPtr<FName> Option);
	/** 활성 채널이 왼손/오른손일 때만 보인다(§20 "손 채널에 부착 파츠 선택"). */
	EVisibility GetAttachRowVisibility() const;

	// --- v3 §20-2: 채널 커브 베이크(A17 자동화 — 본 트랙 대체) ---------------------------------------------------------

	FReply OnBakeCurveChannelsClicked();

	// --- v3 §20-5: [새 액션 클립] ------------------------------------------------------------------------------------

	FReply OnCreateNewActionClipClicked();

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

	// --- v3 §20-3~§20-5: 채널 상태 + 부착 드롭다운 ---------------------------------------------------------------

	/** 지금 기즈모/숫자 목록/타임라인이 가리키는 채널(§20-3). 기본값 Gun — 탭을 열면 총 채널부터 저작하는 게
	 *  자연스러운 시작점(뷰포트 클라이언트 기본값과 동일하게 맞춘다). */
	FName SelectedChannelId = FPSRGunMotionChannelIds::Gun;

	/** 활성 채널의 숫자 키 목록 컨테이너(레거시 KeyListContainer 와 별개 — §20-3 채널 목록은 v2 목록과 나란히
	 *  살아있지 않고 위쪽 v3 섹션에 독립적으로 그려진다). */
	TSharedPtr<SVerticalBox> ChannelKeyListContainer;

	/** 부착 드롭다운 옵션 캐시(§20-3) — "없음" + PreviewWeaponData::WeaponParts. SComboBox 는 옵션 배열을
	 *  TSharedPtr 항목으로 들고 있어야 선택 상태가 유지된다(매 페인트 새로 만들면 깨진다). */
	TArray<TSharedPtr<FName>> AttachOptions;
	TSharedPtr<SComboBox<TSharedPtr<FName>>> AttachCombo;
};
