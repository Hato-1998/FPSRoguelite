// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "AssetRegistry/AssetData.h"
#include "Card/FPSRCardTypes.h"

class IDetailsView;
class UFPSRCardDataAsset;
class UFPSRCardPoolDataAsset;
class UFPSRWeaponDataAsset;
class UFPSRLoadoutPoolDataAsset;
class UFPSRRunScheduleDataAsset;
class UFPSRMissionDataAsset;
class UPackage;
template <typename OptionType> class SComboBox;

/**
 * FPSR Data Editor (P1 designer tool) — the whole tab body. Left: anchor/orphan discovery (reuses
 * FFPSRAnchoredValidationService, P0). Right: an engine IDetailsView for ALL property editing (per the over-design
 * gate, this tool hand-rolls exactly THREE custom widgets — this list panel's selection plumbing doesn't count as
 * one of the three; the three are: (1) this anchor/orphan list panel, (2) the card magnitude grid below, and
 * (3) SFPSRScheduleTimelineBar) — plus the card magnitude grid and the read-only mission schedule timeline.
 *
 * Everything that is just "edit a selected asset's properties" (including membership arrays like Pool->Cards) is
 * left to IDetailsView; this widget only adds the THREE things IDetailsView cannot do: cross-asset orphan/anchor
 * discovery, a per-rarity magnitude grid across a card's Instanced effects, and a read-only schedule timeline.
 */
class SFPSRDataEditorWidget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SFPSRDataEditorWidget) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	// --- Data refresh -------------------------------------------------------------------------------------------

	/** Re-runs FFPSRAnchoredValidationService::FindAnchorAssets/FindOrphans and refreshes both list views. Guards
	 *  on IAssetRegistry::IsLoadingAssets() first (mirrors FPSRogueliteEditorModule.cpp's menu-entry guard). */
	void RefreshLists();

	/** "Save Modified + Rescan": saves DirtyTrackedPackages via FFPSRDataEditorHelpers::SavePackages, clears the
	 *  tracked set, then calls RefreshLists() so orphan/anchor discovery reflects the just-saved state. */
	FReply OnSaveAndRescanClicked();

	/** Marks Package as having unsaved Data Editor edits (shown by the stale-status text) and eligible for the
	 *  next Save + Rescan. No-ops on null. Does not itself dirty the package (the mutating helper call already did). */
	void TrackDirtyPackage(UPackage* Package);

	/** Text for the stale-status line ("N unsaved edit(s) — validation reflects last save" / "Up to date"). */
	FText GetStaleStatusText() const;

	// --- Anchor / orphan lists (custom widget #1: the list panel + its selection-driven side panels) -------------

	TSharedRef<class ITableRow> OnGenerateAnchorRow(TSharedPtr<FAssetData> Item, const TSharedRef<class STableViewBase>& OwnerTable);
	TSharedRef<class ITableRow> OnGenerateOrphanRow(TSharedPtr<FAssetData> Item, const TSharedRef<class STableViewBase>& OwnerTable);
	void OnAnchorSelectionChanged(TSharedPtr<FAssetData> Item, ESelectInfo::Type SelectInfo);
	void OnOrphanSelectionChanged(TSharedPtr<FAssetData> Item, ESelectInfo::Type SelectInfo);

	/** Common selection handling: loads Item's asset, sets it on the details view, and rebuilds whichever
	 *  auxiliary panel (magnitude grid / timeline / guided-add) applies to its class. bIsOrphan drives whether the
	 *  guided-add affordance is shown (only orphans get a "wire this in" prompt). */
	void OnAssetSelected(const TSharedPtr<FAssetData>& Item, bool bIsOrphan);

	// --- Card magnitude grid (custom widget #2) -----------------------------------------------------------------

	/** One row of the magnitude grid: a single (card, effect) pair with per-rarity numeric cells. */
	struct FMagnitudeGridRow
	{
		TWeakObjectPtr<UFPSRCardDataAsset> Card;
		int32 EffectIndex = INDEX_NONE;
	};

	void RebuildMagnitudeGridFromPool(UFPSRCardPoolDataAsset* Pool);
	void RebuildMagnitudeGridFromCard(UFPSRCardDataAsset* Card);
	void ClearMagnitudeGrid();
	TSharedRef<class ITableRow> OnGenerateMagnitudeGridRow(TSharedPtr<FMagnitudeGridRow> Item, const TSharedRef<class STableViewBase>& OwnerTable);

	/** Builds one rarity's SNumericEntryBox<float> cell for a magnitude grid row (disabled/blank if the effect has
	 *  no tier for that rarity — see UFPSRCardDataAsset::SetEffectRarityMagnitude's "edit existing tier only" contract). */
	TSharedRef<SWidget> BuildMagnitudeCell(TWeakObjectPtr<UFPSRCardDataAsset> Card, int32 EffectIndex, ECardRarity Rarity);
	void OnMagnitudeCommitted(float NewValue, ETextCommit::Type CommitType, TWeakObjectPtr<UFPSRCardDataAsset> Card, int32 EffectIndex, ECardRarity Rarity);

	// --- Mission schedule timeline (custom widget #3, defined in SFPSRScheduleTimelineBar below) ------------------

	void RebuildScheduleTimeline(UFPSRRunScheduleDataAsset* Schedule);
	void ClearScheduleTimeline();

	// --- Guided-add affordance (reuses engine SComboBox/SButton — not a counted custom widget) --------------------

	void RebuildGuidedAddForOrphan(UObject* Orphan);
	void ClearGuidedAdd();

	FReply OnGuidedAddCardClicked();
	FReply OnGuidedAddMissionClicked();
	FReply OnGuidedAddWeaponClicked();

	// --- State ------------------------------------------------------------------------------------------------

	TSharedPtr<IDetailsView> DetailsView;

	TArray<TSharedPtr<FAssetData>> AnchorItems;
	TArray<TSharedPtr<FAssetData>> OrphanItems;
	TSharedPtr<SListView<TSharedPtr<FAssetData>>> AnchorListView;
	TSharedPtr<SListView<TSharedPtr<FAssetData>>> OrphanListView;

	TSharedPtr<STextBlock> ScanStatusText;
	TSharedPtr<STextBlock> StaleStatusText;

	/** Packages touched by this tool's mutating helpers since the last Save + Rescan. Weak so a package unloaded
	 *  out from under the tool (unlikely in-session, but defensive) doesn't crash the save pass. */
	TSet<TWeakObjectPtr<UPackage>> DirtyTrackedPackages;

	// Magnitude grid state
	TSharedPtr<SVerticalBox> MagnitudeGridContainer;
	TArray<TSharedPtr<FMagnitudeGridRow>> MagnitudeGridItems;
	TSharedPtr<SListView<TSharedPtr<FMagnitudeGridRow>>> MagnitudeGridListView;

	// Schedule timeline state
	TSharedPtr<SVerticalBox> ScheduleTimelineContainer;
	TWeakObjectPtr<UFPSRRunScheduleDataAsset> SelectedSchedule;

	// Guided-add state
	TSharedPtr<SVerticalBox> GuidedAddContainer;
	TWeakObjectPtr<UObject> GuidedAddOrphan;
	TArray<TSharedPtr<EFPSRCardRoute>> GuidedAddRouteOptions;
	TSharedPtr<EFPSRCardRoute> GuidedAddSelectedRoute;
	TArray<TSharedPtr<FAssetData>> GuidedAddTargetOptions;
	TSharedPtr<FAssetData> GuidedAddSelectedTarget;
	TArray<TSharedPtr<int32>> GuidedAddWindowIndexOptions;
	TSharedPtr<int32> GuidedAddSelectedWindowIndex;
	TSharedPtr<STextBlock> GuidedAddStatusText;
};
