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
class SFPSRWeaponAssemblerViewport;
class STextBlock;
class UFPSRWeaponDataAsset;
struct FAssetData;

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
	 *  index-aligned to the viewport client's PartComps / the weapon DA's WeaponParts1P. */
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

	/** Rebuilds AvailPartRows: takes the folder of the current weapon DA's first non-null WeaponParts1P[].Part and
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

	/** 진화 단계 리스트뷰의 한 행: 선택 슬롯(DA->WeaponParts1P[Sel]) Stages 배열의 인덱스만 들고 있는 얇은 미러. */
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

	/** 선택 단계 Scope.ScopeReticle(소프트 텍스처) getter/setter. */
	FString GetSelectedStageReticlePath() const;
	void OnSelectedStageReticleChanged(const FAssetData& AssetData);

	/** 선택 단계 Scope.bScopeVignette getter/setter. */
	ECheckBoxState GetSelectedStageScopeVignette() const;
	void OnSelectedStageScopeVignetteChanged(ECheckBoxState NewState);

	/** 선택 단계 Scope.bHideWeaponWhileScoped getter/setter. */
	ECheckBoxState GetSelectedStageHideWeapon() const;
	void OnSelectedStageHideWeaponChanged(ECheckBoxState NewState);

	/** 리티클/비네트/총 숨김 서브필드 표시 조건: 선택 단계가 유효하고 Scope.bScopeOverlay일 때만 Visible. */
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

	/** 선택 슬롯(DA->WeaponParts1P[Sel].Stages)의 미러 — RefreshStageList가 재구성. */
	TArray<TSharedPtr<FStageRow>> StageRows;
	TSharedPtr<SListView<TSharedPtr<FStageRow>>> StageListView;
	/** 진화 단계 리스트에서 현재 선택된 행("− 단계 제거" 활성 조건). */
	TSharedPtr<FStageRow> SelectedStageRow;

	TSharedPtr<STextBlock> StatusText;
};
