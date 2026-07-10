// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

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

	// --- Weapon DA picker (SObjectPropertyEntryBox) --------------------------------------------------------------

	/** ObjectPath attribute: the currently-loaded weapon DA's full path, or empty if none selected. */
	FString GetWeaponObjectPath() const;

	/** OnObjectChanged: rebuilds the viewport's body+parts from the newly-picked DA (or clears the preview on
	 *  "None") and refreshes the parts list to match. */
	void OnWeaponAssetChanged(const FAssetData& AssetData);

	// --- Parts list (SListView<FPartRow>) -------------------------------------------------------------------------

	/** Rebuilds PartRows from the viewport client's current PartComps and refreshes the list view. Called after
	 *  every weapon change. */
	void RefreshPartsList();
	TSharedRef<class ITableRow> OnGeneratePartRow(TSharedPtr<FPartRow> Item, const TSharedRef<class STableViewBase>& OwnerTable);
	void OnPartSelectionChanged(TSharedPtr<FPartRow> Item, ESelectInfo::Type SelectInfo);

	// --- Toolbar --------------------------------------------------------------------------------------------------

	/** "조립→저장": bakes the current part placement into the body mesh's sockets + wires/saves the weapon DA via
	 *  FPSRWeaponAssemblerHelpers::BakeSockets, then reports the result in StatusText. */
	FReply OnBakeClicked();
	FReply OnTranslateModeClicked();
	FReply OnRotateModeClicked();

	// --- State ------------------------------------------------------------------------------------------------

	/** This tool's own preview scene — shared by the viewport widget below and never anything spawned into an
	 *  editor level. Constructed once in Construct() and kept alive for the tab's lifetime. */
	TSharedPtr<FAdvancedPreviewScene> PreviewScene;
	TSharedPtr<SFPSRWeaponAssemblerViewport> Viewport;

	TArray<TSharedPtr<FPartRow>> PartRows;
	TSharedPtr<SListView<TSharedPtr<FPartRow>>> PartListView;

	TSharedPtr<STextBlock> StatusText;
};
