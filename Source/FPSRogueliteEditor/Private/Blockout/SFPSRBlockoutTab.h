// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/STileView.h"   // brings SListView too
#include "AssetRegistry/AssetData.h"

class STextBlock;
class ITableRow;
class STableViewBase;
class FAssetThumbnail;
class FAssetThumbnailPool;

/** Per-asset collision-suitability status for the palette badge (slice ⑧). Unknown until "상태 검사" loads + inspects.
 *  Pass = has WorldStatic-blocking collision (the flow-field obstacle mask will see a placed piece); Fail = none. */
enum class EBlockoutAssetStatus : uint8
{
	Unknown,
	Pass,
	Fail,
};

/** RIGHT 타일 그리드의 카테고리 필터(슬라이스 P1b) — 배치·스폰·회전·설정 클래스는 건드리지 않는 순수 UX 필터. 기본값 All.
 *  분류는 IsStructureAsset() 휴리스틱 하나로만 판정하며 필터는 비대칭이다: All = 전부 표시, Structure = Structure로
 *  분류된 것만, Dressing = Structure가 "아닌" 전부(장식 + 관례를 벗어나 분류 안 되는 자산 포함) — 어떤 자산도 세 필터
 *  전부에서 숨겨지는 일이 없도록 하는 설계(휴리스틱이 놓친 자산이 Dressing 밑에서라도 항상 보이게). */
enum class EFPSRBlockoutCategoryFilter : uint8
{
	All,
	Structure,
	Dressing,
};

/**
 * FPSR Blockout tool (Tools > FPSR > "블록아웃 툴…") — config-driven modular map palette + blockout guardrails.
 *
 * Two-pane browser: LEFT = a list of the configured palette's folders (categories); clicking a folder fills the RIGHT
 * pane with that folder's assets as a CARD GRID (STileView, large thumbnail + type/collision badge + name). Scans BOTH
 * UStaticMeshes and actor Blueprints. Double-click a card (or "선택 배치") spawns it into the current editor world: a
 * mesh as a WorldStatic "BlockAll" AStaticMeshActor (tool-owned collision + overlap preflight), an actor Blueprint AS-IS
 * from its generated class (no collision auto-mod — Codex P1; if a BP won't block, the designer fixes it, the tool never
 * rewrites vendor BPs). "상태 검사" loads every cached asset on demand and fills the ✓/✗ badges. "레벨 검증" runs the
 * guardrail pass (FFPSRBlockoutValidator) into the FPSRBlockout message log. Config folders: Project Settings > FPSR.
 */
class SFPSRBlockoutTab : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SFPSRBlockoutTab) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	/** One row of the LEFT folder list: a configured-palette folder (package path) + its filtered asset count. */
	struct FBlockoutFolderItem
	{
		FName PackagePath;
		FText Label;   // "{folder}  ({N})"
		int32 Count = 0;
	};

	/** One card of the RIGHT tile grid: a placeable asset (UStaticMesh or actor Blueprint) in the selected folder. */
	struct FBlockoutAssetItem
	{
		FAssetData Asset;
		FText Label;
		/** Lazy asset thumbnail — created on first tile-generate (slice ⑦), shares the tab's pooled render targets. */
		TSharedPtr<FAssetThumbnail> Thumbnail;
	};

	/** Re-reads UFPSRBlockoutSettings::PaletteFolders, recursively asset-registry-scans each for UStaticMesh + actor
	 *  Blueprints (non-actor BPs filtered out), caches the results, then rebuilds the folder list. Guards on IsLoading. */
	void RefreshPalette();

	/** Rebuilds FolderList from CachedAssets (folders that contain a name-filter match), refreshes the left list, and
	 *  re-selects the previously selected folder (or the first) so the right pane stays populated. */
	void RebuildFolderList();

	/** Fills CurrentFolderAssets from CachedAssets in SelectedFolderPath (name-filtered), refreshes the right tile grid,
	 *  and clears the tile selection. */
	void PopulateCurrentFolder();

	FReply OnRefreshClicked();
	void OnSearchTextChanged(const FText& NewText);

	// --- Category filter toggle (P1b, 순수 UX 필터: 전체/구조/장식) ------------------------------------------------------
	/** SSegmentedControl 의 .Value 바인딩 — 현재 선택된 필터를 반환. */
	EFPSRBlockoutCategoryFilter GetCategoryFilter() const;
	/** SSegmentedControl 의 .OnValueChanged — 필터를 갱신하고 오른쪽 타일 소스를 다시 빌드(PopulateCurrentFolder). */
	void OnCategoryFilterChanged(EFPSRBlockoutCategoryFilter NewFilter);

	// --- Left folder list -------------------------------------------------------------------------------------------
	TSharedRef<ITableRow> OnGenerateFolderRow(TSharedPtr<FBlockoutFolderItem> Item, const TSharedRef<STableViewBase>& OwnerTable);
	void OnFolderSelectionChanged(TSharedPtr<FBlockoutFolderItem> Item, ESelectInfo::Type SelectInfo);

	// --- Right asset card grid --------------------------------------------------------------------------------------
	TSharedRef<ITableRow> OnGenerateTile(TSharedPtr<FBlockoutAssetItem> Item, const TSharedRef<STableViewBase>& OwnerTable);
	/** R3a: a USER selection (mouse click / key press — NOT the programmatic ESelectInfo::Direct re-selection that
	 *  RefreshPalette/RebuildFolderList issue while restoring the previous pick) auto-arms placement mode via
	 *  ArmPlacementForSelectedAsset(), so picking a card is enough to start snapping without an extra button click. */
	void OnAssetSelectionChanged(TSharedPtr<FBlockoutAssetItem> Item, ESelectInfo::Type SelectInfo);
	void OnTileDoubleClicked(TSharedPtr<FBlockoutAssetItem> Item);

	// --- Placement / actions ----------------------------------------------------------------------------------------
	FReply OnPlaceClicked();
	/** "뷰포트 배치" button — activates the city-builder placement UEdMode and hands it the selected asset (ghost +
	 *  cursor-to-floor + grid snap + left-click to place). Enabled when a card is selected (IsPlaceEnabled). Kept
	 *  alongside the R3a auto-arm-on-select behavior as an explicit, harmless re-entry point (also routes through
	 *  ArmPlacementForSelectedAsset — one code path). */
	FReply OnEnterPlacementModeClicked();
	/** R3a shared helper (OnEnterPlacementModeClicked + the auto-arm branch of OnAssetSelectionChanged): activates the
	 *  placement UEdMode if it isn't already active, then pushes SelectedAsset + PlacementGridSize into it. If the mode
	 *  is ALREADY active, only the asset/grid size are pushed (no re-activation — avoids re-running Enter() and
	 *  resetting the designer's in-progress rotation while they're just switching which piece they're placing).
	 *  No-op if no valid card is selected. */
	void ArmPlacementForSelectedAsset();
	/** Toolbar grid-size numeric box getter/handler — pushes the live snap size to the active placement mode. */
	TOptional<float> GetGridSizeValue() const;
	void OnGridSizeChanged(float NewValue);
	bool IsPlaceEnabled() const;
	FReply OnValidateClicked();
	FReply OnInspectStatusClicked();

	// --- 프리팹 저작 (P2+P3 병합, R1서 경량 Blueprint 하베스트로 교체: 선택 액터 → 재사용 가능한 BP_* 프리팹, 서브레벨 없음) -----
	/** SEditableTextBox 의 .Text 바인딩 — PendingPrefabName 을 FText 로 반환. */
	FText GetPendingPrefabNameText() const;
	/** SEditableTextBox 의 .OnTextChanged — PendingPrefabName 갱신. */
	void OnPendingPrefabNameChanged(const FText& NewText);
	FReply OnCreatePrefabClicked();
	/** 현재 레벨 선택 액터들을 FKismetEditorUtilities::HarvestBlueprintFromActors 로 컴포넌트만 흡수한 경량 BP_* 프리팹
	 *  (서브레벨 없음, bReplaceActors=true 로 선택 액터를 즉시 새 BP 인스턴스로 치환) 하나로 묶는다. 실패(월드 없음/선택
	 *  없음/이름 없음/하베스트 실패) 시 StatusText 에 사유를 표시하고 조용히 리턴 — 크래시 없음. 성공 시 RefreshPalette() 로
	 *  팔레트에 새 BP_* 카드가 즉시 보이게 한다(생성된 BP 는 액터 BP 라 IsActorBlueprint 필터를 통과, 기존 배치 경로 그대로
	 *  재사용). Undo 가능(FScopedTransaction). */
	void CreatePrefabFromSelection();

	/** Places AssetData into the current editor world: a UStaticMesh spawns as a WorldStatic "BlockAll" AStaticMeshActor
	 *  (tool-owned collision + mesh-only overlap preflight); an actor Blueprint spawns AS-IS from its generated class
	 *  (no auto-collision). Undo transaction, asset-name label, "Blockout" outliner folder, selected. */
	void PlaceAsset(const FAssetData& AssetData);
	FVector ComputeSpawnLocation() const;

	static bool IsBlueprintAsset(const FAssetData& AssetData);
	static bool IsActorBlueprint(const FAssetData& AssetData);
	static EBlockoutAssetStatus InspectAssetStatus(const FAssetData& AssetData);
	/** 카테고리 필터(P1b) 분류 휴리스틱 — 패키지 경로에 "/Buildings"·"/Base" 포함 또는 자산명이 "SM_Bld_"로 시작하면
	 *  Structure(벽/바닥/건물)로 판정. Dressing(소품/환경, 경로 "/Props"·"/Environment" 또는 이름 "SM_Prop_"·"SM_Env_")은
	 *  별도 판정 함수를 두지 않는다 — 필터가 비대칭 설계(EFPSRBlockoutCategoryFilter 주석 참고)라 "Structure가 아니면
	 *  Dressing 뷰에 표시"로 충분하기 때문. 실제 태그가 아닌 경로/이름 관례 기반이므로 어디까지나 휴리스틱. */
	static bool IsStructureAsset(const FAssetData& AssetData);

	// --- State ------------------------------------------------------------------------------------------------------
	/** Full scan result (all placeable assets across all configured folders), cached so filtering is a pure rebuild. */
	TArray<FAssetData> CachedAssets;

	TArray<TSharedPtr<FBlockoutFolderItem>> FolderList;
	TSharedPtr<SListView<TSharedPtr<FBlockoutFolderItem>>> FolderListView;

	TArray<TSharedPtr<FBlockoutAssetItem>> CurrentFolderAssets;
	TSharedPtr<STileView<TSharedPtr<FBlockoutAssetItem>>> AssetTileView;

	/** The folder currently selected in the left list (NAME_None if none). */
	FName SelectedFolderPath;
	/** The card currently selected in the right grid (drives "선택 배치"). Reset on folder change / rebuild. */
	TSharedPtr<FBlockoutAssetItem> SelectedAsset;

	FString CurrentFilter;
	/** 오른쪽 타일 그리드 카테고리 필터(P1b) — LEFT 폴더 목록/카운트에는 적용하지 않고 PopulateCurrentFolder 에서만 사용. */
	EFPSRBlockoutCategoryFilter CurrentCategoryFilter = EFPSRBlockoutCategoryFilter::All;
	/** Collision-status cache keyed by asset path — survives filter/folder changes; filled by OnInspectStatusClicked. */
	TMap<FSoftObjectPath, EBlockoutAssetStatus> StatusByAsset;
	/** Live grid-snap size (cm) for the viewport placement mode; edited via the toolbar box, seeded from settings. */
	float PlacementGridSize = 100.0f;
	/** Designer-entered name for "선택→프리팹" (drives BP_<Name> asset naming, no sub-level). */
	FString PendingPrefabName;
	/** Shared pool backing the lazy card thumbnails (slice ⑦); an FTickableEditorObject, so it renders itself. */
	TSharedPtr<FAssetThumbnailPool> ThumbnailPool;
	TSharedPtr<STextBlock> StatusText;
};
