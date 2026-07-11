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

	TSharedPtr<STextBlock> StatusText;
};
