// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Input/SCheckBox.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/StaticMesh.h"
#include "UObject/SoftObjectPath.h"

class FAdvancedPreviewScene;
class SEditableTextBox;
class SFPSRWeaponAssemblerViewport;
class STextBlock;
class UFPSRWeaponDataAsset;
struct FAssetData;
struct FReferenceSkeleton;
struct FSlateColor;

/**
 * FPSR Weapon Part Assembler (Tools > FPSR > "무기 파츠 조립기…") — a fully self-contained, fully embedded-viewport
 * tool tab. Replaces the old "spawn a preview actor into the level + gizmo-move it via the level viewport's Details
 * panel + separate 'capture' menu action" workflow: everything now lives in this one dockable tab — a weapon DA
 * picker, an own 3D preview viewport (own FPreviewScene; nothing is ever spawned into an editor level), a parts
 * list, a move/rotate gizmo-mode toggle, and a "조립→저장" button that bakes the current part placement into
 * body-mesh sockets and wires/saves the weapon DA (FPSRWeaponAssemblerHelpers::BakeSockets).
 *
 * All weapon-preview UObject state (body + part components) lives on FFPSRWeaponAssemblerViewportClient, owned by
 * the SFPSRWeaponAssemblerViewport child widget below; this tab only drives that client (SetWeapon/SetSelectedPart/
 * SetWidgetMode) and mirrors its parts list for the SListView on the left.
 */
class SFPSRWeaponAssemblerTab : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SFPSRWeaponAssemblerTab) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	/** One row of the left-panel parts list: the representative (variant-stripped) part name shown to the designer,
	 *  index-aligned to the viewport client's PartComps / the weapon DA's WeaponParts. */
	struct FPartRow
	{
		FText Label;
		int32 Index = INDEX_NONE;
	};

	/** One row of the "사용 가능 파츠(교체)" catalog list below the current-parts list: a StaticMesh asset found in the
	 *  current weapon's part folder (see RefreshAvailableParts). Double-clicking a row swaps the currently-selected
	 *  current-part (left-top list) to this mesh. */
	struct FAvailPartRow
	{
		FText Label;
		FSoftObjectPath MeshPath;
	};

	// --- Weapon DA picker (SObjectPropertyEntryBox) --------------------------------------------------------------

	/** ObjectPath attribute: the currently-loaded weapon DA's full path, or empty if none selected. */
	FString GetWeaponObjectPath() const;

	/** OnObjectChanged: rebuilds the viewport's body+parts from the newly-picked DA (or clears the preview on
	 *  "None") and refreshes both the current-parts list and the available-parts catalog to match. */
	void OnWeaponAssetChanged(const FAssetData& AssetData);

	// --- Parts list (SListView<FPartRow>) -------------------------------------------------------------------------

	/** Rebuilds PartRows from the viewport client's current PartComps and refreshes the list view. Called after
	 *  every weapon change. */
	void RefreshPartsList();
	TSharedRef<class ITableRow> OnGeneratePartRow(TSharedPtr<FPartRow> Item, const TSharedRef<class STableViewBase>& OwnerTable);
	void OnPartSelectionChanged(TSharedPtr<FPartRow> Item, ESelectInfo::Type SelectInfo);

	// --- Available parts catalog (SListView<FAvailPartRow>) --------------------------------------------------------

	/** Rebuilds AvailPartRows: takes the folder of the current weapon DA's first non-null WeaponParts[].Part and
	 *  asset-registry-scans that folder (non-recursive) for UStaticMesh assets. No weapon / no non-null part =>
	 *  empty list. Called from OnWeaponAssetChanged so the catalog always matches the loaded weapon. */
	void RefreshAvailableParts();
	TSharedRef<class ITableRow> OnGenerateAvailRow(TSharedPtr<FAvailPartRow> Item, const TSharedRef<class STableViewBase>& OwnerTable);

	/** Double-click handler for a catalog row: swaps the currently-selected current-part (left-top list) to this
	 *  row's mesh via the viewport client's SwapSelectedPartMesh, then reports the result in StatusText. If no
	 *  current-part is selected, reports a "먼저 선택하세요" status instead and performs no swap. */
	void OnAvailPartActivated(TSharedPtr<FAvailPartRow> Item);

	/** Single-click selection handler for the catalog list: remembers the picked row in SelectedAvailPart so the
	 *  explicit "교체" button (OnSwapClicked / IsSwapEnabled) knows what to swap to. */
	void OnAvailSelectionChanged(TSharedPtr<FAvailPartRow> Item, ESelectInfo::Type SelectInfo);

	/** Shared swap path used by both the catalog double-click and the explicit "교체" button. Validates a current
	 *  part is selected and the mesh loads, swaps it (preview + in-memory DA .Part), then updates ONLY the affected
	 *  current-part row label IN PLACE (reusing the same FPartRow shared pointer) so the SListView selection — and
	 *  therefore the client's SelectedPart / gizmo target / isolate visibility — is preserved. Rebuilding the whole
	 *  list would drop the selection: gizmo jumps to origin and, with "선택만 보기" on, the swapped part is hidden. */
	void PerformSwap(TSharedPtr<FAvailPartRow> AvailItem);

	/** "→ 선택 파츠 교체" 버튼: 위 '현재 파츠' 슬롯을 아래에서 고른 메시로 교체(PerformSwap 위임). */
	FReply OnSwapClicked();

	/** 교체 버튼 활성 조건: 현재 파츠가 선택돼 있고, 사용 가능 파츠도 하나 골라져 있을 때만 true. */
	bool IsSwapEnabled() const;

	/** '<슬롯명>  ·  <현재 메시명>' 형태의 현재-파츠 행 라벨. 교체 시 이 라벨만 제자리 갱신해 선택을 잃지 않는다. */
	FText MakePartRowLabel(int32 Index) const;

	// --- Slot display label (DisplayLabel) editor ----------------------------------------------------------------

	/** 선택 슬롯의 표시 라벨(DisplayLabel) — 편집 박스 Text 어트리뷰트. 선택 없으면 빈 텍스트. */
	FText GetSelectedSlotLabelText() const;
	/** 편집 박스 커밋: 선택 슬롯의 DisplayLabel을 갱신하고 해당 행 라벨을 제자리 갱신(선택 유지). DA 저장은 '조립→저장'. */
	void OnSlotLabelCommitted(const FText& InText, ETextCommit::Type CommitType);

	/** "＋ 파츠 추가" 버튼: 아래 '사용 가능 파츠'에서 고른 부착물을 무기에 새 파츠로 추가(client AddPart) → 목록 갱신·
	 *  새 파츠 선택·바닥 재적합. DA 저장은 '조립→저장'이 담당. */
	FReply OnAddPartClicked();
	/** 추가 버튼 활성 조건: 무기 DA가 있고 사용 가능 파츠가 하나 골라져 있을 때만. */
	bool IsAddPartEnabled() const;

	/** "− 선택 파츠 제거" 버튼: 위 '현재 파츠'에서 고른 파츠를 무기에서 제거(client RemoveSelectedPart) → 목록·바닥 갱신.
	 *  재베이크 시 소켓도 정리된다. */
	FReply OnRemovePartClicked();
	/** 제거 버튼 활성 조건: 현재 파츠가 선택돼 있을 때만. */
	bool IsRemovePartEnabled() const;

	// --- Evolution authoring panel (선택 슬롯의 진화 카드 + 진화 단계 목록, W-U1b 저작 UI) --------------------------------

	/** 진화 단계 리스트뷰의 한 행: 선택 슬롯(DA->WeaponParts[Sel]) Stages 배열의 인덱스만 들고 있는 얇은 미러. */
	struct FStageRow
	{
		int32 StageIndex = INDEX_NONE;
	};

	/** 진화 카드 피커 ObjectPath — 선택 슬롯의 EvolutionFragment 경로(없으면 빈 문자열). */
	FString GetEvolutionFragmentPath() const;
	/** 진화 카드 변경 — 선택 슬롯의 EvolutionFragment 갱신 + MarkPackageDirty. */
	void OnEvolutionFragmentChanged(const FAssetData& AssetData);

	/** 선택 슬롯의 Stages로 StageRows 재구성 + 리스트 갱신. 슬롯 미선택이면 비운다. OnPartSelectionChanged/무기 변경 시 호출. */
	void RefreshStageList();
	TSharedRef<class ITableRow> OnGenerateStageRow(TSharedPtr<FStageRow> Item, const TSharedRef<class STableViewBase>& OwnerTable);
	void OnStageSelectionChanged(TSharedPtr<FStageRow> Item, ESelectInfo::Type SelectInfo);

	/** "＋ 단계 추가": 사용 가능 파츠에서 고른 메시를 선택 슬롯의 새 진화 단계로 추가. */
	FReply OnAddStageClicked();
	bool IsAddStageEnabled() const;
	/** "− 단계 제거": 선택된 진화 단계를 제거. */
	FReply OnRemoveStageClicked();
	bool IsRemoveStageEnabled() const;

	/** 단계 행 스핀박스 값/커밋(선택 슬롯 Stages[StageIndex] 기준). "선택 단계" 소폼의 스택 스핀박스도 이걸 재사용(인덱스는
	 *  GetSelectedStageIndex()로 넘긴다). */
	int32 GetStageMinStacks(int32 StageIndex) const;
	void OnStageMinStacksChanged(int32 NewValue, int32 StageIndex);

	// --- "선택 단계" 소폼 (트리거 종류 + 스택/스탯 필드 + 순서 이동, 우선순위가 명시적으로 보이도록) ------------------------------

	/** 진화 단계 목록에서 현재 선택된 단계의 인덱스(SelectedStageRow->StageIndex, 없으면 INDEX_NONE). */
	int32 GetSelectedStageIndex() const;

	/** 단계 행 요약 텍스트: "N. [트리거요약] 메시명" (N=StageIndex+1). 트리거요약 = 스택 조건("스택 ≥{N}") 또는
	 *  스탯 조건("{축} {비교} {기준값}") — 우선순위(목록 아래일수록 우선)가 한눈에 보이도록. */
	FText MakeStageRowSummary(int32 StageIndex) const;

	/** 선택 단계의 트리거 종류 콤보(SEnumComboBox) 값/변경. */
	int32 GetSelectedStageTriggerValue() const;
	void OnSelectedStageTriggerChanged(int32 NewValue, ESelectInfo::Type SelectInfo);

	/** 선택 단계의 스탯 임계 필드(축/비교/기준값) getter/setter. */
	int32 GetSelectedStageStatAxisValue() const;
	void OnSelectedStageStatAxisChanged(int32 NewValue, ESelectInfo::Type SelectInfo);
	int32 GetSelectedStageStatCompareValue() const;
	void OnSelectedStageStatCompareChanged(int32 NewValue, ESelectInfo::Type SelectInfo);
	TOptional<float> GetSelectedStageStatValue() const;
	void OnSelectedStageStatValueChanged(float NewValue);

	/** "선택 단계" 소폼 표시/활성 제어: 스택 필드=Trigger==FragmentStacks, 스탯 필드=Trigger==StatThreshold,
	 *  소폼 전체=선택 단계가 유효할 때만. */
	EVisibility GetStackFieldVisibility() const;
	EVisibility GetStatFieldVisibility() const;
	bool IsStageSelected() const;

	/** "▲ 위로"/"▼ 아래로": 선택 단계를 Stages 배열에서 이웃과 스왑(우선순위 재배치), 선택 유지. */
	FReply OnStageMoveUpClicked();
	bool IsStageMoveUpEnabled() const;
	FReply OnStageMoveDownClicked();
	bool IsStageMoveDownEnabled() const;

	// --- "선택 단계" 소폼 하단 "스코프(사이트 단계)" 섹션 (선택 단계 Scope 필드, GetSelectedStageIndex() 대상) ------------------------

	/** 선택 단계 Scope.bScopeOverlay getter/setter. */
	ECheckBoxState GetSelectedStageScopeOverlay() const;
	void OnSelectedStageScopeOverlayChanged(ECheckBoxState NewState);

	/** 선택 단계 Scope.AimFieldOfView getter/setter(모든 사이트에 적용, 항상 표시). */
	TOptional<float> GetSelectedStageAimFOV() const;
	void OnSelectedStageAimFOVChanged(float NewValue);

	/** 선택 단계 Scope.ScopeOverlayWidgetClass(소프트 위젯 클래스) getter/setter. */
	const UClass* GetSelectedStageScopeWidgetClass() const;
	void OnSelectedStageScopeWidgetChanged(const UClass* NewClass);

	/** 선택 단계 Scope.bScopeVignette getter/setter. */
	ECheckBoxState GetSelectedStageScopeVignette() const;
	void OnSelectedStageScopeVignetteChanged(ECheckBoxState NewState);

	/** 선택 단계 Scope.bHideWeaponWhileScoped getter/setter. */
	ECheckBoxState GetSelectedStageHideWeapon() const;
	void OnSelectedStageHideWeaponChanged(ECheckBoxState NewState);

	/** 스코프 위젯(WBP)/비네트/총 숨김 서브필드 표시 조건: 선택 단계가 유효하고 Scope.bScopeOverlay일 때만 Visible. */
	EVisibility GetScopeOverlaySubFieldVisibility() const;

	// --- Toolbar --------------------------------------------------------------------------------------------------

	/** "조립→저장": bakes the current part placement into the body mesh's sockets + wires/saves the weapon DA via
	 *  FPSRWeaponAssemblerHelpers::BakeSockets, then reports the result in StatusText. */
	FReply OnBakeClicked();
	FReply OnTranslateModeClicked();
	FReply OnRotateModeClicked();

	/** "전체 이동"/"선택만 보기" 툴바 체크박스 — 뷰포트 클라이언트의 SetMoveAll/SetIsolate로 위임하고, 체크 상태는
	 *  클라이언트의 IsMoveAll/IsIsolate를 그대로 되비춘다(체크박스 자체는 상태를 갖지 않음). */
	void OnMoveAllChanged(ECheckBoxState NewState);
	void OnIsolateChanged(ECheckBoxState NewState);
	ECheckBoxState IsMoveAllChecked() const;
	ECheckBoxState IsIsolateChecked() const;

	// --- 1인칭 팔 패널 (뷰포트 오른쪽 사이드 패널) ---------------------------------------------------------------------
	// SetShowArms/RefreshArmsFromSettings/GetArmsStatusMessage, 재생(SetPreviewPlaying 등), 그립(HasGripFrame/
	// GetGripTransform/SetGripTransform/GetGripBone/SetGripBone), 소켓(GetResolvedAttachSocketName/
	// IsUsingSharedDefaultSocket/SetWeaponAttachSocketName)은 전부 클라이언트(FFPSRWeaponAssemblerViewportClient)가
	// 소유한다 — 이 탭은 그 값을 그리고 위임만 한다. BakeWeaponSocket(FBakeHandResult 반환)도 마찬가지로 헬퍼가 소유.

	/** "팔 보기" 체크박스 — 1인칭 팔을 세우고 무기를 손에 얹는다. 클라이언트가 설정의 팔 메시가 비면 켜지길 거부하므로,
	 *  체크 상태는 요청값이 아니라 **클라이언트의 실제 상태**(IsShowArms)를 되비춘다. 그때 StatusText로 이유를 알린다. */
	void OnShowArmsChanged(ECheckBoxState NewState);
	ECheckBoxState IsShowArmsChecked() const;

	/** 뼈 피커(SBoneSelectionWidget) 활성 조건 — 팔이 서 있어야 물어볼 스켈레톤이 있다. HasGripFrame()보다 일부러 약한
	 *  조건이다: 아직 뼈가 안 잡혀 HasGripFrame()이 false여도 뼈 피커 자체는 계속 눌러 고칠 수 있어야 한다 — 안
	 *  그러면 "뼈가 없어 비활성 → 비활성이라 뼈를 못 고름"으로 잠긴다. */
	bool IsShowArmsEnabled() const;

	/** "1인칭 뷰" 체크박스 — 카메라를 플레이어 눈 위치/FOV 에 고정하고 조준 축 기준선을 그린다. 자유 시점으로는
	 *  그립을 판정할 수 없다는 지적에서 나온 기능. 팔 보기와 같은 규약으로, 체크 상태는 요청값이 아니라 클라이언트의
	 *  실제 상태(IsFirstPersonView)를 되비추고 못 켠 사유는 StatusText 로 알린다. */
	void OnFirstPersonViewChanged(ECheckBoxState NewState);
	ECheckBoxState IsFirstPersonViewChecked() const;
	bool IsFirstPersonViewEnabled() const;

	// --- 팔 메시/애니 피커(SObjectPropertyEntryBox, C2) — 값은 UFPSRWeaponAssemblerSettings에 직접 읽고 쓴다(진실원천
	//     하나, 에셋 경로를 C++에 박지 않는다 — ADR 0006 I4). -------------------------------------------------------

	FString GetArmsMeshObjectPath() const;
	/** 팔 메시 변경 — 설정에 대입 후 SaveConfig, 클라이언트에 RefreshArmsFromSettings로 반영. */
	void OnArmsMeshAssetChanged(const FAssetData& AssetData);
	FString GetArmsPoseObjectPath() const;
	/** 팔 애니(포즈) 변경 — 위와 동일 경로. */
	void OnArmsPoseAssetChanged(const FAssetData& AssetData);
	/** 애니 피커 필터 — 팔 메시의 스켈레톤과 호환되는 애니만 보이게(USkeleton::ShouldFilterAsset를 그대로 물린다 —
	 *  엔진이 바로 이 용도로 제공하는 래퍼). 팔 메시가 없으면 걸러낼 기준이 없으므로 필터하지 않는다(전부 통과). */
	bool OnShouldFilterArmsPoseAsset(const FAssetData& AssetData) const;

	// --- 재생 컨트롤 (C6) ------------------------------------------------------------------------------------------

	FReply OnPreviewPlayPauseClicked();
	FText GetPreviewPlayPauseLabel() const;
	/** 시간 슬라이더 값 — SSlider의 Value는 0..1 정규화 규약이라 Position/Length로 직접 정규화한다(길이 0=애니 없음이면 0). */
	float GetPreviewSliderFraction() const;
	void OnPreviewSliderFractionChanged(float NewFraction);
	/** "0.42 / 1.83 s" 표시 텍스트. */
	FText GetPreviewTimeLabel() const;
	ECheckBoxState IsPreviewLoopingChecked() const;
	void OnPreviewLoopingChanged(ECheckBoxState NewState);
	/** 재생버튼/슬라이더/루프 공통 활성 조건 — GetPreviewLength()가 0(애니 없음)이면 전부 비활성. */
	bool IsPreviewControlsEnabled() const;

	// --- 그립(= 구워질 값, 뼈 상대) 위치/회전 숫자 필드 (C4) --------------------------------------------------------

	/** 그립 소폼(위치/회전) + "손 위치 저장" 버튼 공통 활성 조건 — 클라이언트의 HasGripFrame()을 그대로 되비춘다(팔이
	 *  서 있고 부착 기준이 잡혀 있어야 굽는 값이 의미 있다). */
	bool HasGripFrame() const;

	// SVectorInputBox/SRotatorInputBox는 컴포넌트별 float 어트리뷰트(X/Y/Z, Roll/Pitch/Yaw)라 6개로 나뉜다. FVector/
	// FRotator는 LWC(double 저장)라 위젯의 float 경계에서 명시적으로 좁히고/넓힌다(.cpp 참고).
	TOptional<float> GetGripLocationX() const;
	TOptional<float> GetGripLocationY() const;
	TOptional<float> GetGripLocationZ() const;
	void OnGripLocationXChanged(float NewValue);
	void OnGripLocationYChanged(float NewValue);
	void OnGripLocationZChanged(float NewValue);
	/** 🪤 Rotator()로 뽑아 한 축만 고친 뒤 Quaternion()으로 되넣는 왕복은, 짐벌 부근(Pitch ±90° 근접)에서 건드리지
	 *  않은 다른 두 축이 튀어 보일 수 있는 알려진 한계다(엔진 Details 패널은 이를 피하려 FRotator를 별도 캐시한다).
	 *  그립 회전은 손으로 쥐는 각도라 짐벌 부근에 놓일 일이 거의 없어 실사용 리스크가 낮고, 이 툴 범위에서 캐시(무효화
	 *  시점 관리 필요)를 추가하는 비용이 이득보다 크다고 판단해 단순 왕복으로 남긴다. */
	TOptional<float> GetGripRotationRoll() const;
	TOptional<float> GetGripRotationPitch() const;
	TOptional<float> GetGripRotationYaw() const;
	void OnGripRotationRollChanged(float NewValue);
	void OnGripRotationPitchChanged(float NewValue);
	void OnGripRotationYawChanged(float NewValue);

	// --- 뼈 피커 (SBoneSelectionWidget, C5 — IEditableSkeleton 불필요, 델리게이트 3개뿐) -------------------------------

	const FReferenceSkeleton& GetArmsReferenceSkeletonForBonePicker() const;
	FName GetGripBoneForBonePicker(bool& bMultipleValues) const;
	void OnGripBonePicked(FName NewBone);

	// --- 손 소켓 이름 + 공용/전용 배지 (C3, 🚨 공용 기본 소켓 사고 방지가 이 패널에서 가장 중요) --------------------------

	/** 무기가 바뀌거나 "이 무기 전용 소켓 만들기"가 성공한 뒤 입력 박스를 현재 결정된 소켓 이름으로 재동기화한다. 라이브
	 *  Attribute로 .Text를 바인딩하지 않는 이유: 그러면 박스가 포커스를 잃는 순간(=버튼을 누르려 클릭하는 순간) 아직
	 *  DA에 쓰이지 않은 입력값이 결정된 이름으로 되돌아가 버려, 버튼이 읽어야 할 "입력값"이 사라진다. */
	void RefreshGripSocketNameBox();
	FText GetGripSocketBadgeText() const;
	FSlateColor GetGripSocketBadgeColor() const;
	/** 공용 기본일 때만 경고 문구, 전용이면 빈 텍스트(줄 자체는 GetGripSocketWarningVisibility로 접는다). */
	FText GetGripSocketWarningText() const;
	EVisibility GetGripSocketWarningVisibility() const;

	/** "이 무기 전용 소켓 만들기": 입력 박스의 현재 텍스트를 DA->WeaponAttachSocket에 쓴다(SetWeaponAttachSocketName).
	 *  순수 데이터 선언이라 팔이 서 있지 않아도 눌러도 된다(HasGripFrame 게이트 없음). */
	FReply OnMakeDedicatedSocketClicked();

	/** "손 위치로 리셋 (0,0,0)" — 그립 위치/회전을 항등으로 되돌려 무기를 기준 뼈에 정확히 겹친다. 뼈를 바꾼 직후
	 *  무기가 옛 기준의 (쓸모없는) 자리에 남아 있을 때의 출발점. 프리뷰만 바꾸고 저장은 하지 않는다. */
	FReply OnResetGripClicked();
	/** 활성 조건: 무기 DA가 있고, 현재 공용 기본을 쓰는 중이고(이미 전용이면 할 일이 없다), 입력 텍스트가 비어있지
	 *  않을 때만. */
	bool IsMakeDedicatedSocketEnabled() const;

	/** "손 위치 저장": '전체 이동'으로 잡은 무기 위치를 **팔 메시**의 무기 부착 소켓(GetResolvedAttachSocketName)에,
	 *  뼈 피커가 가리키는 뼈(GetGripBone) 기준으로 굽는다(FPSRWeaponAssemblerHelpers::BakeWeaponSocket). 공용 기본
	 *  소켓을 고치는 중이면(IsUsingSharedDefaultSocket) 확인 대화상자로 한 번 더 묻는다 — 전용 소켓이 없는 무기
	 *  전부가 함께 움직이기 때문(계약 C3). 결과(FBakeHandResult)의 실제 저장 패키지를 StatusText에 밝힌다. */
	FReply OnBakeHandClicked();

	// --- State ------------------------------------------------------------------------------------------------

	/** This tool's own preview scene — shared by the viewport widget below and never anything spawned into an
	 *  editor level. Constructed once in Construct() and kept alive for the tab's lifetime. */
	TSharedPtr<FAdvancedPreviewScene> PreviewScene;
	TSharedPtr<SFPSRWeaponAssemblerViewport> Viewport;

	TArray<TSharedPtr<FPartRow>> PartRows;
	TSharedPtr<SListView<TSharedPtr<FPartRow>>> PartListView;

	TArray<TSharedPtr<FAvailPartRow>> AvailPartRows;
	TSharedPtr<SListView<TSharedPtr<FAvailPartRow>>> AvailPartListView;

	/** The catalog row currently single-click-selected (drives the "교체" button + IsSwapEnabled). Reset on weapon change. */
	TSharedPtr<FAvailPartRow> SelectedAvailPart;

	/** 선택 슬롯(DA->WeaponParts[Sel].Stages)의 미러 — RefreshStageList가 재구성. */
	TArray<TSharedPtr<FStageRow>> StageRows;
	TSharedPtr<SListView<TSharedPtr<FStageRow>>> StageListView;
	/** 진화 단계 리스트에서 현재 선택된 행("− 단계 제거" 활성 조건). */
	TSharedPtr<FStageRow> SelectedStageRow;

	/** "손 소켓" 패널의 소켓 이름 입력 박스 — 위젯이 직접 소유(GetText()로 직접 읽는다; 이유는 RefreshGripSocketNameBox
	 *  주석 참고). */
	TSharedPtr<SEditableTextBox> GripSocketNameBox;

	TSharedPtr<STextBlock> StatusText;
};
