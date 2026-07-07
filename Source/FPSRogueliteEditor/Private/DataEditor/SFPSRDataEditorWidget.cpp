// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataEditor/SFPSRDataEditorWidget.h"

#include "Validation/FPSRAnchoredValidationService.h"
#include "DataEditor/FPSRDataEditorHelpers.h"

#include "Card/FPSRCardDataAsset.h"
#include "Card/FPSRCardEffect.h"
#include "Card/FPSRCardPoolDataAsset.h"
#include "Weapon/FPSRWeaponDataAsset.h"
#include "Weapon/FPSRWeaponFragment.h"
#include "Weapon/FPSRLoadoutPoolDataAsset.h"
#include "Run/FPSRRunScheduleDataAsset.h"
#include "Run/Mission/FPSRMissionDataAsset.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetRegistry/ARFilter.h"

#include "PropertyEditorModule.h"
#include "IDetailsView.h"
#include "Modules/ModuleManager.h"

#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/SLeafWidget.h"
#include "Rendering/DrawElements.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "SFPSRDataEditorWidget"

// =====================================================================================================================
// SFPSRScheduleTimelineBar — custom widget #3 of the hard-capped three. READ-ONLY: draws mission windows + boss time
// on a horizontal time axis. No interaction, no state beyond what it's given each rebuild.
// =====================================================================================================================
class SFPSRScheduleTimelineBar : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SFPSRScheduleTimelineBar) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TWeakObjectPtr<UFPSRRunScheduleDataAsset> InSchedule)
	{
		Schedule = InSchedule;
	}

	virtual FVector2D ComputeDesiredSize(float) const override
	{
		return FVector2D(400.0f, 140.0f);
	}

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
	{
		const UFPSRRunScheduleDataAsset* ScheduleAsset = Schedule.Get();
		if (!ScheduleAsset)
		{
			return LayerId;
		}

		const FVector2D LocalSize = AllottedGeometry.GetLocalSize();
		if (LocalSize.X <= 0.0f || LocalSize.Y <= 0.0f)
		{
			return LayerId;
		}

		// Time axis range: [0 .. max(BossTime, every window's MaxTime)]. Guard an all-zero/empty schedule so we
		// never divide by zero below.
		float MaxAxisTime = ScheduleAsset->BossTime;
		for (const FFPSRMissionWindow& Window : ScheduleAsset->MissionWindows)
		{
			MaxAxisTime = FMath::Max(MaxAxisTime, Window.MaxTime);
		}
		if (MaxAxisTime <= 0.0f)
		{
			MaxAxisTime = 1.0f; // avoid div-by-zero on a degenerate/empty schedule; nothing meaningful to draw anyway
		}

		const FSlateBrush* WhiteBrush = FAppStyle::GetBrush("WhiteBrush");
		const FSlateFontInfo Font = FAppStyle::GetFontStyle("PropertyWindow.NormalFont");

		auto TimeToX = [&LocalSize, MaxAxisTime](float Time) -> float
		{
			return (Time / MaxAxisTime) * LocalSize.X;
		};

		int32 Layer = LayerId;

		// Baseline axis.
		{
			TArray<FVector2D> AxisPoints;
			AxisPoints.Add(FVector2D(0.0f, LocalSize.Y - 4.0f));
			AxisPoints.Add(FVector2D(LocalSize.X, LocalSize.Y - 4.0f));
			FSlateDrawElement::MakeLines(OutDrawElements, Layer, AllottedGeometry.ToPaintGeometry(), AxisPoints, ESlateDrawEffect::None, FLinearColor(0.4f, 0.4f, 0.4f), true, 1.0f);
		}

		// One horizontal bar per mission window, stacked vertically so overlapping windows are still legible.
		const float RowHeight = 20.0f;
		const float TopPadding = 8.0f;
		for (int32 WindowIndex = 0; WindowIndex < ScheduleAsset->MissionWindows.Num(); ++WindowIndex)
		{
			const FFPSRMissionWindow& Window = ScheduleAsset->MissionWindows[WindowIndex];
			const float StartX = TimeToX(Window.MinTime);
			const float EndX = FMath::Max(TimeToX(Window.MaxTime), StartX + 2.0f);
			const float RowY = TopPadding + WindowIndex * RowHeight;
			if (RowY > LocalSize.Y - RowHeight)
			{
				break; // ran out of vertical room — read-only preview, not a scroll view; later windows are simply omitted
			}

			const FPaintGeometry BarGeometry = AllottedGeometry.ToPaintGeometry(
				FVector2D(EndX - StartX, RowHeight - 4.0f),
				FSlateLayoutTransform(FVector2D(StartX, RowY)));
			FSlateDrawElement::MakeBox(OutDrawElements, Layer, BarGeometry, WhiteBrush, ESlateDrawEffect::None, FLinearColor(0.2f, 0.5f, 0.8f, 0.85f));

			const FString Label = FString::Printf(TEXT("윈도우 %d (미션 %d개)"), WindowIndex, Window.MissionPool.Num());
			const FPaintGeometry LabelGeometry = AllottedGeometry.ToPaintGeometry(
				FVector2D(LocalSize.X, RowHeight),
				FSlateLayoutTransform(FVector2D(StartX + 2.0f, RowY - 1.0f)));
			FSlateDrawElement::MakeText(OutDrawElements, Layer + 1, LabelGeometry, Label, Font, ESlateDrawEffect::None, FLinearColor::White);
		}

		// Boss time: a vertical line + label, drawn on top of the window bars.
		{
			const float BossX = TimeToX(ScheduleAsset->BossTime);
			TArray<FVector2D> BossLine;
			BossLine.Add(FVector2D(BossX, 0.0f));
			BossLine.Add(FVector2D(BossX, LocalSize.Y - 4.0f));
			FSlateDrawElement::MakeLines(OutDrawElements, Layer + 2, AllottedGeometry.ToPaintGeometry(), BossLine, ESlateDrawEffect::None, FLinearColor(0.9f, 0.15f, 0.15f), true, 2.0f);

			const FPaintGeometry BossLabelGeometry = AllottedGeometry.ToPaintGeometry(
				FVector2D(80.0f, 16.0f),
				FSlateLayoutTransform(FVector2D(FMath::Min(BossX + 2.0f, LocalSize.X - 40.0f), LocalSize.Y - 20.0f)));
			FSlateDrawElement::MakeText(OutDrawElements, Layer + 3, BossLabelGeometry, FString(TEXT("보스")), Font, ESlateDrawEffect::None, FLinearColor(1.0f, 0.6f, 0.6f));
		}

		return Layer + 4;
	}

private:
	TWeakObjectPtr<UFPSRRunScheduleDataAsset> Schedule;
};

// =====================================================================================================================
// SFPSRDataEditorWidget
// =====================================================================================================================

void SFPSRDataEditorWidget::Construct(const FArguments& InArgs)
{
	FPropertyEditorModule& PropertyEditorModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FDetailsViewArgs DetailsViewArgs;
	DetailsViewArgs.bUpdatesFromSelection = false;
	DetailsViewArgs.bLockable = false;
	DetailsViewArgs.bAllowSearch = true;
	DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::ObjectsUseNameArea;
	DetailsViewArgs.bHideSelectionTip = true;
	DetailsView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);
	// The details view is the primary editing surface (all membership-array/property edits happen here). It marks the
	// edited object's package dirty but does not register it with this tool's Save+Rescan tracking — hook its
	// finished-changing event so a details-panel edit is actually saved and reflected in the stale status.
	DetailsView->OnFinishedChangingProperties().AddSP(this, &SFPSRDataEditorWidget::OnDetailsPropertyChanged);

	ChildSlot
	[
		SNew(SSplitter)
		.Orientation(Orient_Horizontal)

		+ SSplitter::Slot()
		.Value(0.28f)
		[
			SNew(SBox)
			.WidthOverride(320.0f)
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot().AutoHeight().Padding(4.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("HeaderTitle", "FPSR 데이터 에디터"))
					.Font(FAppStyle::GetFontStyle("HeadingExtraSmall"))
				]

				+ SVerticalBox::Slot().AutoHeight().Padding(4.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("SaveAndRescan", "변경 저장 + 재검사"))
					.OnClicked(this, &SFPSRDataEditorWidget::OnSaveAndRescanClicked)
				]

				+ SVerticalBox::Slot().AutoHeight().Padding(4.0f, 0.0f, 4.0f, 4.0f)
				[
					SAssignNew(StaleStatusText, STextBlock)
					.Text(this, &SFPSRDataEditorWidget::GetStaleStatusText)
				]

				+ SVerticalBox::Slot().AutoHeight().Padding(4.0f, 0.0f, 4.0f, 4.0f)
				[
					SAssignNew(ScanStatusText, STextBlock)
					.Visibility(EVisibility::Collapsed)
					.ColorAndOpacity(FLinearColor(0.9f, 0.7f, 0.2f))
				]

				+ SVerticalBox::Slot().AutoHeight().Padding(4.0f, 6.0f, 4.0f, 2.0f)
				[
					SNew(STextBlock).Text(LOCTEXT("AssetTreeHeader", "데이터 에셋"))
				]

				+ SVerticalBox::Slot().FillHeight(1.0f).Padding(4.0f)
				[
					SAssignNew(AssetTreeView, STreeView<TSharedPtr<FFPSRDataEditorTreeItem>>)
					.TreeItemsSource(&TreeRoots)
					.OnGenerateRow(this, &SFPSRDataEditorWidget::OnGenerateTreeRow)
					.OnGetChildren(this, &SFPSRDataEditorWidget::OnGetTreeChildren)
					.OnSelectionChanged(this, &SFPSRDataEditorWidget::OnTreeSelectionChanged)
					.SelectionMode(ESelectionMode::Single)
				]
			]
		]

		+ SSplitter::Slot()
		.Value(0.72f)
		[
			SNew(SScrollBox)

			+ SScrollBox::Slot()
			[
				SNew(SBox)
				.MinDesiredHeight(250.0f)
				[
					DetailsView.ToSharedRef()
				]
			]

			+ SScrollBox::Slot()
			.Padding(4.0f)
			[
				SAssignNew(MagnitudeGridContainer, SVerticalBox)
			]

			+ SScrollBox::Slot()
			.Padding(4.0f)
			[
				SAssignNew(ScheduleTimelineContainer, SVerticalBox)
			]

			+ SScrollBox::Slot()
			.Padding(4.0f)
			[
				SAssignNew(GuidedAddContainer, SVerticalBox)
			]
		]
	];

	RefreshLists();
}

// ---------------------------------------------------------------------------------------------------------------
// Data refresh
// ---------------------------------------------------------------------------------------------------------------

void SFPSRDataEditorWidget::RefreshLists()
{
	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	if (AssetRegistry.IsLoadingAssets())
	{
		if (ScanStatusText.IsValid())
		{
			ScanStatusText->SetText(LOCTEXT("StillScanning", "에셋 레지스트리 스캔 중…"));
			ScanStatusText->SetVisibility(EVisibility::Visible);
		}
		TreeRoots.Reset();
		if (AssetTreeView.IsValid()) { AssetTreeView->RequestTreeRefresh(); }
		return;
	}

	if (ScanStatusText.IsValid())
	{
		ScanStatusText->SetVisibility(EVisibility::Collapsed);
	}

	// Orphan package set: cross-referenced against every category below so an unreachable leaf is flagged inline
	// instead of living in a separate list (this is the whole point of TASK B — reachable weapons/cards/missions
	// used to not appear ANYWHERE; now every asset of every in-scope type shows up under its category, orphan or not).
	TSet<FName> OrphanPackages;
	for (const FAssetData& Orphan : FFPSRAnchoredValidationService::FindOrphans())
	{
		OrphanPackages.Add(Orphan.PackageName);
	}

	// Ordered category table (label, class). Order here IS the on-screen order — weapons/cards first (most-edited),
	// anchors (pool/schedule/loadout) after, fragments last (P1 has no guided-add for them, see RebuildGuidedAddForOrphan).
	struct FCategoryEntry
	{
		FText Label;
		UClass* Class = nullptr;
	};
	const FCategoryEntry Categories[] = {
		{ LOCTEXT("Category_Weapon", "무기"), UFPSRWeaponDataAsset::StaticClass() },
		{ LOCTEXT("Category_Card", "카드"), UFPSRCardDataAsset::StaticClass() },
		{ LOCTEXT("Category_CardPool", "카드 풀"), UFPSRCardPoolDataAsset::StaticClass() },
		{ LOCTEXT("Category_Mission", "미션"), UFPSRMissionDataAsset::StaticClass() },
		{ LOCTEXT("Category_RunSchedule", "런 스케줄"), UFPSRRunScheduleDataAsset::StaticClass() },
		{ LOCTEXT("Category_LoadoutPool", "로드아웃 풀"), UFPSRLoadoutPoolDataAsset::StaticClass() },
		{ LOCTEXT("Category_Fragment", "프래그먼트"), UFPSRWeaponFragment::StaticClass() },
	};

	TreeRoots.Reset();
	for (const FCategoryEntry& CategoryEntry : Categories)
	{
		FARFilter Filter;
		Filter.bRecursiveClasses = false;
		Filter.ClassPaths.Add(CategoryEntry.Class->GetClassPathName());
		TArray<FAssetData> FoundAssets;
		AssetRegistry.GetAssets(Filter, FoundAssets);

		TSharedPtr<FFPSRDataEditorTreeItem> CategoryItem = MakeShared<FFPSRDataEditorTreeItem>();
		CategoryItem->bIsCategory = true;

		for (const FAssetData& Found : FoundAssets)
		{
			if (FFPSRAnchoredValidationService::IsExcludedPath(Found.PackagePath))
			{
				continue; // designer scratch space (/Game/Developers, /Game/Test, *_Scratch) — never shown
			}
			TSharedPtr<FFPSRDataEditorTreeItem> Leaf = MakeShared<FFPSRDataEditorTreeItem>();
			Leaf->Label = FText::FromName(Found.AssetName);
			Leaf->Asset = Found;
			Leaf->bIsOrphan = OrphanPackages.Contains(Found.PackageName);
			CategoryItem->Children.Add(Leaf);
		}
		CategoryItem->Children.Sort([](const TSharedPtr<FFPSRDataEditorTreeItem>& A, const TSharedPtr<FFPSRDataEditorTreeItem>& B)
		{
			return A->Asset.AssetName.LexicalLess(B->Asset.AssetName);
		});

		// Category node is added ALWAYS, even at 0 children — a designer should see every asset TYPE that exists in
		// the schema, not just the ones with content today (mirrors the "closed table for a closed enum" spirit: the
		// category list is fixed by the schema, so it's shown in full regardless of current content).
		CategoryItem->Label = FText::Format(LOCTEXT("CategoryHeaderFmt", "{0} ({1})"), CategoryEntry.Label, FText::AsNumber(CategoryItem->Children.Num()));
		TreeRoots.Add(CategoryItem);
	}

	if (AssetTreeView.IsValid())
	{
		AssetTreeView->RequestTreeRefresh();
		for (const TSharedPtr<FFPSRDataEditorTreeItem>& Root : TreeRoots)
		{
			AssetTreeView->SetItemExpansion(Root, true); // categories start expanded — this IS the whole point of the browser
		}
	}
}

FReply SFPSRDataEditorWidget::OnSaveAndRescanClicked()
{
	TArray<UPackage*> Packages;
	for (const TWeakObjectPtr<UPackage>& WeakPackage : DirtyTrackedPackages)
	{
		if (UPackage* Package = WeakPackage.Get())
		{
			Packages.Add(Package);
		}
	}
	FFPSRDataEditorHelpers::SavePackages(Packages);
	// Keep any package that FAILED to save (still dirty) tracked so the stale status stays honest instead of falsely
	// reading "Up to date" while an asset remains unsaved (read-only / source-controlled / disk error).
	TSet<TWeakObjectPtr<UPackage>> StillDirty;
	for (const TWeakObjectPtr<UPackage>& WeakPackage : DirtyTrackedPackages)
	{
		if (UPackage* Package = WeakPackage.Get())
		{
			if (Package->IsDirty())
			{
				StillDirty.Add(Package);
			}
		}
	}
	DirtyTrackedPackages = MoveTemp(StillDirty);

	RefreshLists();
	return FReply::Handled();
}

void SFPSRDataEditorWidget::TrackDirtyPackage(UPackage* Package)
{
	if (Package)
	{
		DirtyTrackedPackages.Add(Package);
	}
}

FText SFPSRDataEditorWidget::GetStaleStatusText() const
{
	if (DirtyTrackedPackages.Num() == 0)
	{
		return LOCTEXT("UpToDate", "최신 상태");
	}
	return FText::Format(LOCTEXT("StaleStatus", "저장 안 된 편집 {0}건 — 검증은 마지막 저장 기준"), FText::AsNumber(DirtyTrackedPackages.Num()));
}

// ---------------------------------------------------------------------------------------------------------------
// Categorized asset tree (custom widget #1)
// ---------------------------------------------------------------------------------------------------------------

TSharedRef<ITableRow> SFPSRDataEditorWidget::OnGenerateTreeRow(TSharedPtr<FFPSRDataEditorTreeItem> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	if (!Item.IsValid())
	{
		return SNew(STableRow<TSharedPtr<FFPSRDataEditorTreeItem>>, OwnerTable)[SNullWidget::NullWidget];
	}

	if (Item->bIsCategory)
	{
		// Category header row: bold, no orphan coloring — this is a grouping node, not a selectable asset.
		return SNew(STableRow<TSharedPtr<FFPSRDataEditorTreeItem>>, OwnerTable)
			[
				SNew(STextBlock)
				.Text(Item->Label)
				.Font(FAppStyle::GetFontStyle("PropertyWindow.BoldFont"))
			];
	}

	// Leaf asset row: plain label, or "{name}  (미배선)" in orange if this asset is unreachable from any anchor
	// (same visual signal the old OrphanListView gave, just inline in the tree instead of a separate list).
	const FText DisplayText = Item->bIsOrphan
		? FText::Format(LOCTEXT("OrphanLeafFmt", "{0}  (미배선)"), Item->Label)
		: Item->Label;
	return SNew(STableRow<TSharedPtr<FFPSRDataEditorTreeItem>>, OwnerTable)
		[
			SNew(STextBlock)
			.Text(DisplayText)
			.ColorAndOpacity(Item->bIsOrphan ? FLinearColor(0.9f, 0.7f, 0.2f) : FSlateColor::UseForeground())
		];
}

void SFPSRDataEditorWidget::OnGetTreeChildren(TSharedPtr<FFPSRDataEditorTreeItem> Item, TArray<TSharedPtr<FFPSRDataEditorTreeItem>>& OutChildren)
{
	if (Item.IsValid())
	{
		OutChildren = Item->Children;
	}
}

void SFPSRDataEditorWidget::OnTreeSelectionChanged(TSharedPtr<FFPSRDataEditorTreeItem> Item, ESelectInfo::Type SelectInfo)
{
	// Category nodes (and a null/deselect event) clear the right side exactly like an invalid asset selection always
	// has — OnAssetSelected already handles Item.IsValid()==false as "SetObject(nullptr) + RebuildAuxPanels clears
	// the aux panels", so route both cases through the SAME selection path the old anchor/orphan handlers used.
	if (Item.IsValid() && !Item->bIsCategory && Item->Asset.IsValid())
	{
		OnAssetSelected(MakeShared<FAssetData>(Item->Asset), Item->bIsOrphan);
	}
	else
	{
		OnAssetSelected(TSharedPtr<FAssetData>(), /*bIsOrphan=*/false);
	}
}

void SFPSRDataEditorWidget::OnAssetSelected(const TSharedPtr<FAssetData>& Item, bool bIsOrphan)
{
	UObject* Asset = Item.IsValid() ? Item->GetAsset() : nullptr;
	if (DetailsView.IsValid())
	{
		DetailsView->SetObject(Asset);
	}
	SelectedAsset = Asset;
	bSelectedIsOrphan = bIsOrphan;
	RebuildAuxPanels();
}

void SFPSRDataEditorWidget::RebuildAuxPanels()
{
	// Rebuild the selection-driven side panels (magnitude grid / timeline / guided-add) from the CURRENT object shape.
	// Called on selection change AND after a Details-view edit — the latter matters because editing the asset's Effects
	// (remove/reorder) would otherwise leave grid rows keyed on a stale EffectIndex that writes to the wrong effect.
	ClearMagnitudeGrid();
	ClearScheduleTimeline();
	ClearGuidedAdd();

	UObject* Asset = SelectedAsset.Get();
	if (!Asset)
	{
		return;
	}
	if (UFPSRCardPoolDataAsset* Pool = Cast<UFPSRCardPoolDataAsset>(Asset))
	{
		RebuildMagnitudeGridFromPool(Pool);
	}
	else if (UFPSRCardDataAsset* Card = Cast<UFPSRCardDataAsset>(Asset))
	{
		RebuildMagnitudeGridFromCard(Card);
	}
	else if (UFPSRRunScheduleDataAsset* Schedule = Cast<UFPSRRunScheduleDataAsset>(Asset))
	{
		RebuildScheduleTimeline(Schedule);
	}
	if (bSelectedIsOrphan)
	{
		RebuildGuidedAddForOrphan(Asset);
	}
}

// ---------------------------------------------------------------------------------------------------------------
// Card magnitude grid (custom widget #2)
// ---------------------------------------------------------------------------------------------------------------

void SFPSRDataEditorWidget::ClearMagnitudeGrid()
{
	MagnitudeGridItems.Reset();
	if (MagnitudeGridListView.IsValid())
	{
		MagnitudeGridListView->RequestListRefresh();
	}
	if (MagnitudeGridContainer.IsValid())
	{
		MagnitudeGridContainer->ClearChildren();
	}
	MagnitudeGridListView.Reset();
}

void SFPSRDataEditorWidget::RebuildMagnitudeGridFromCard(UFPSRCardDataAsset* Card)
{
	if (!Card || !MagnitudeGridContainer.IsValid())
	{
		return;
	}
	MagnitudeGridContainer->ClearChildren();
	MagnitudeGridItems.Reset();

	// Only rows for magnitude-bearing effects (an effect with zero RarityTiers has nothing to edit here — it still
	// shows in the IDetailsView above for its own non-magnitude fields, e.g. UCardEffect_GrantWeapon::WeaponToGrant).
	for (int32 EffectIndex = 0; EffectIndex < Card->Effects.Num(); ++EffectIndex)
	{
		if (Card->Effects[EffectIndex] && Card->Effects[EffectIndex]->RarityTiers.Num() > 0)
		{
			TSharedPtr<FMagnitudeGridRow> Row = MakeShared<FMagnitudeGridRow>();
			Row->Card = Card;
			Row->EffectIndex = EffectIndex;
			MagnitudeGridItems.Add(Row);
		}
	}

	if (MagnitudeGridItems.Num() == 0)
	{
		return; // nothing magnitude-bearing on this card — skip the section entirely
	}

	MagnitudeGridContainer->AddSlot().AutoHeight()
	[
		SNew(SExpandableArea)
		.AreaTitle(LOCTEXT("MagnitudeGridTitle", "카드 매그니튜드 그리드"))
		.InitiallyCollapsed(false)
		.BodyContent()
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				BuildMagnitudeGridHeaderRow()
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SAssignNew(MagnitudeGridListView, SListView<TSharedPtr<FMagnitudeGridRow>>)
				.ListItemsSource(&MagnitudeGridItems)
				.OnGenerateRow(this, &SFPSRDataEditorWidget::OnGenerateMagnitudeGridRow)
				.SelectionMode(ESelectionMode::None)
			]
		]
	];
}

void SFPSRDataEditorWidget::RebuildMagnitudeGridFromPool(UFPSRCardPoolDataAsset* Pool)
{
	if (!Pool || !MagnitudeGridContainer.IsValid())
	{
		return;
	}
	MagnitudeGridContainer->ClearChildren();
	MagnitudeGridItems.Reset();

	auto AddCardsFrom = [this](const TArray<TObjectPtr<UFPSRCardDataAsset>>& Cards)
	{
		for (const TObjectPtr<UFPSRCardDataAsset>& Card : Cards)
		{
			if (!Card)
			{
				continue;
			}
			for (int32 EffectIndex = 0; EffectIndex < Card->Effects.Num(); ++EffectIndex)
			{
				if (Card->Effects[EffectIndex] && Card->Effects[EffectIndex]->RarityTiers.Num() > 0)
				{
					TSharedPtr<FMagnitudeGridRow> Row = MakeShared<FMagnitudeGridRow>();
					Row->Card = Card;
					Row->EffectIndex = EffectIndex;
					MagnitudeGridItems.Add(Row);
				}
			}
		}
	};
	AddCardsFrom(Pool->Cards);
	AddCardsFrom(Pool->WeaponUnlockCards);

	if (MagnitudeGridItems.Num() == 0)
	{
		return;
	}

	MagnitudeGridContainer->AddSlot().AutoHeight()
	[
		SNew(SExpandableArea)
		.AreaTitle(LOCTEXT("MagnitudeGridTitlePool", "카드 매그니튜드 그리드 (이 풀의 전체 카드)"))
		.InitiallyCollapsed(true) // a pool can be large — collapsed by default, unlike a single-card selection
		.BodyContent()
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				BuildMagnitudeGridHeaderRow()
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SAssignNew(MagnitudeGridListView, SListView<TSharedPtr<FMagnitudeGridRow>>)
				.ListItemsSource(&MagnitudeGridItems)
				.OnGenerateRow(this, &SFPSRDataEditorWidget::OnGenerateMagnitudeGridRow)
				.SelectionMode(ESelectionMode::None)
			]
		]
	];
}

TSharedRef<SWidget> SFPSRDataEditorWidget::BuildMagnitudeGridHeaderRow() const
{
	// Column labels matching OnGenerateMagnitudeGridRow's layout exactly (same FillWidth proportions) so the header
	// lines up with the data rows below it. Rarity names (Common/Rare/Epic/Legendary) stay in English — standard
	// game terms, not UI chrome — everything else is Korean per the localization pass.
	const FSlateFontInfo BoldFont = FAppStyle::GetFontStyle("PropertyWindow.BoldFont");

	return SNew(SHorizontalBox)

		+ SHorizontalBox::Slot().FillWidth(0.14f).VAlign(VAlign_Center).Padding(2.0f)
		[
			SNew(STextBlock).Text(LOCTEXT("GridHeader_CardId", "카드ID")).Font(BoldFont)
		]
		+ SHorizontalBox::Slot().FillWidth(0.18f).VAlign(VAlign_Center).Padding(2.0f)
		[
			SNew(STextBlock).Text(LOCTEXT("GridHeader_Name", "이름")).Font(BoldFont)
		]
		+ SHorizontalBox::Slot().FillWidth(0.06f).VAlign(VAlign_Center).Padding(2.0f)
		[
			SNew(STextBlock).Text(LOCTEXT("GridHeader_EffectIndex", "효과#")).Font(BoldFont)
		]
		+ SHorizontalBox::Slot().FillWidth(0.24f).VAlign(VAlign_Center).Padding(2.0f)
		[
			SNew(STextBlock).Text(LOCTEXT("GridHeader_Effect", "효과")).Font(BoldFont)
		]
		+ SHorizontalBox::Slot().FillWidth(0.095f).VAlign(VAlign_Center).Padding(2.0f)
		[
			SNew(STextBlock).Text(LOCTEXT("GridHeader_Common", "Common")).Font(BoldFont)
		]
		+ SHorizontalBox::Slot().FillWidth(0.095f).VAlign(VAlign_Center).Padding(2.0f)
		[
			SNew(STextBlock).Text(LOCTEXT("GridHeader_Rare", "Rare")).Font(BoldFont)
		]
		+ SHorizontalBox::Slot().FillWidth(0.095f).VAlign(VAlign_Center).Padding(2.0f)
		[
			SNew(STextBlock).Text(LOCTEXT("GridHeader_Epic", "Epic")).Font(BoldFont)
		]
		+ SHorizontalBox::Slot().FillWidth(0.095f).VAlign(VAlign_Center).Padding(2.0f)
		[
			SNew(STextBlock).Text(LOCTEXT("GridHeader_Legendary", "Legendary")).Font(BoldFont)
		];
}

TSharedRef<ITableRow> SFPSRDataEditorWidget::OnGenerateMagnitudeGridRow(TSharedPtr<FMagnitudeGridRow> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	if (!Item.IsValid() || !Item->Card.IsValid() || !Item->Card->Effects.IsValidIndex(Item->EffectIndex))
	{
		return SNew(STableRow<TSharedPtr<FMagnitudeGridRow>>, OwnerTable)[SNullWidget::NullWidget];
	}

	UFPSRCardDataAsset* Card = Item->Card.Get();
	const UFPSRCardEffect* Effect = Card->Effects[Item->EffectIndex];
	const FText CardIdText = Card->CardId.IsNone() ? LOCTEXT("NoCardId", "(CardId 없음)") : FText::FromName(Card->CardId);
	const FText EffectLabel = Effect->GetEditorGridLabel();

	return SNew(STableRow<TSharedPtr<FMagnitudeGridRow>>, OwnerTable)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot().FillWidth(0.14f).VAlign(VAlign_Center).Padding(2.0f)
			[
				SNew(STextBlock).Text(CardIdText)
			]
			+ SHorizontalBox::Slot().FillWidth(0.18f).VAlign(VAlign_Center).Padding(2.0f)
			[
				SNew(STextBlock).Text(Card->DisplayName)
			]
			+ SHorizontalBox::Slot().FillWidth(0.06f).VAlign(VAlign_Center).Padding(2.0f)
			[
				SNew(STextBlock).Text(FText::AsNumber(Item->EffectIndex))
			]
			+ SHorizontalBox::Slot().FillWidth(0.24f).VAlign(VAlign_Center).Padding(2.0f)
			[
				SNew(STextBlock).Text(EffectLabel)
			]
			+ SHorizontalBox::Slot().FillWidth(0.095f).VAlign(VAlign_Center).Padding(2.0f)
			[
				BuildMagnitudeCell(Item->Card, Item->EffectIndex, ECardRarity::Common)
			]
			+ SHorizontalBox::Slot().FillWidth(0.095f).VAlign(VAlign_Center).Padding(2.0f)
			[
				BuildMagnitudeCell(Item->Card, Item->EffectIndex, ECardRarity::Rare)
			]
			+ SHorizontalBox::Slot().FillWidth(0.095f).VAlign(VAlign_Center).Padding(2.0f)
			[
				BuildMagnitudeCell(Item->Card, Item->EffectIndex, ECardRarity::Epic)
			]
			+ SHorizontalBox::Slot().FillWidth(0.095f).VAlign(VAlign_Center).Padding(2.0f)
			[
				BuildMagnitudeCell(Item->Card, Item->EffectIndex, ECardRarity::Legendary)
			]
		];
}

TSharedRef<SWidget> SFPSRDataEditorWidget::BuildMagnitudeCell(TWeakObjectPtr<UFPSRCardDataAsset> Card, int32 EffectIndex, ECardRarity Rarity)
{
	const UFPSRCardDataAsset* CardPtr = Card.Get();
	if (!CardPtr || !CardPtr->Effects.IsValidIndex(EffectIndex) || !CardPtr->Effects[EffectIndex])
	{
		return SNullWidget::NullWidget;
	}
	const UFPSRCardEffect* Effect = CardPtr->Effects[EffectIndex];
	const FFPSRCardRarityTier* Tier = Effect->RarityTiers.FindByPredicate(
		[Rarity](const FFPSRCardRarityTier& T) { return T.Rarity == Rarity; });

	// Disabled/blank when this effect declares no tier for Rarity — SetEffectRarityMagnitude only edits an
	// EXISTING tier (P1 scope), so there's nothing meaningful to commit into for a rarity this effect doesn't offer.
	const bool bEnabled = Tier != nullptr;

	// Value_Lambda RE-READS the tier each frame (not a one-time TOptional captured at build) so the cell reflects the
	// committed value / any external change instead of snapping back to the build-time number after a commit or refresh.
	return SNew(SNumericEntryBox<float>)
		.IsEnabled(bEnabled)
		.Value_Lambda([Card, EffectIndex, Rarity]() -> TOptional<float>
		{
			const UFPSRCardDataAsset* C = Card.Get();
			if (!C || !C->Effects.IsValidIndex(EffectIndex) || !C->Effects[EffectIndex])
			{
				return TOptional<float>();
			}
			const FFPSRCardRarityTier* T = C->Effects[EffectIndex]->RarityTiers.FindByPredicate(
				[Rarity](const FFPSRCardRarityTier& X) { return X.Rarity == Rarity; });
			return T ? TOptional<float>(T->Magnitude) : TOptional<float>();
		})
		.UndeterminedString(LOCTEXT("NoTier", "--"))
		.OnValueCommitted(this, &SFPSRDataEditorWidget::OnMagnitudeCommitted, Card, EffectIndex, Rarity);
}

void SFPSRDataEditorWidget::OnMagnitudeCommitted(float NewValue, ETextCommit::Type CommitType, TWeakObjectPtr<UFPSRCardDataAsset> Card, int32 EffectIndex, ECardRarity Rarity)
{
	UFPSRCardDataAsset* CardPtr = Card.Get();
	if (!CardPtr)
	{
		return;
	}
	if (FFPSRDataEditorHelpers::SetCardEffectMagnitude(CardPtr, EffectIndex, Rarity, NewValue))
	{
		TrackDirtyPackage(CardPtr->GetPackage());
	}
}

// ---------------------------------------------------------------------------------------------------------------
// Mission schedule timeline (custom widget #3)
// ---------------------------------------------------------------------------------------------------------------

void SFPSRDataEditorWidget::ClearScheduleTimeline()
{
	SelectedSchedule.Reset();
	if (ScheduleTimelineContainer.IsValid())
	{
		ScheduleTimelineContainer->ClearChildren();
	}
}

void SFPSRDataEditorWidget::RebuildScheduleTimeline(UFPSRRunScheduleDataAsset* Schedule)
{
	if (!Schedule || !ScheduleTimelineContainer.IsValid())
	{
		return;
	}
	ScheduleTimelineContainer->ClearChildren();
	SelectedSchedule = Schedule;

	ScheduleTimelineContainer->AddSlot().AutoHeight()
	[
		SNew(SExpandableArea)
		.AreaTitle(LOCTEXT("ScheduleTimelineTitle", "미션 스케줄 타임라인 (읽기 전용)"))
		.InitiallyCollapsed(false)
		.BodyContent()
		[
			SNew(SBox)
			.HeightOverride(140.0f)
			[
				SNew(SFPSRScheduleTimelineBar, SelectedSchedule)
			]
		]
	];
}

// ---------------------------------------------------------------------------------------------------------------
// Guided-add affordance (orphan wiring)
// ---------------------------------------------------------------------------------------------------------------

void SFPSRDataEditorWidget::ClearGuidedAdd()
{
	GuidedAddOrphan.Reset();
	GuidedAddRouteOptions.Reset();
	GuidedAddSelectedRoute.Reset();
	GuidedAddTargetOptions.Reset();
	GuidedAddSelectedTarget.Reset();
	GuidedAddWindowIndexOptions.Reset();
	GuidedAddSelectedWindowIndex.Reset();
	GuidedAddTargetCombo.Reset();
	GuidedAddWindowCombo.Reset();
	GuidedAddStatusText.Reset();
	if (GuidedAddContainer.IsValid())
	{
		GuidedAddContainer->ClearChildren();
	}
}

void SFPSRDataEditorWidget::RebuildGuidedAddForOrphan(UObject* Orphan)
{
	if (!Orphan || !GuidedAddContainer.IsValid())
	{
		return;
	}
	GuidedAddContainer->ClearChildren();
	GuidedAddOrphan = Orphan;

	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

	if (UFPSRCardDataAsset* Card = Cast<UFPSRCardDataAsset>(Orphan))
	{
		// Route combo: the card's own eligible routes (OCP — no effect-type switch upstream of this list).
		GuidedAddRouteOptions.Reset();
		for (const EFPSRCardRoute Route : FFPSRDataEditorHelpers::GetCardEligibleRoutes(Card))
		{
			GuidedAddRouteOptions.Add(MakeShared<EFPSRCardRoute>(Route));
		}
		// Default to MissionClearWeaponFeature when eligible (H2 recommendation for WeaponBehavior cards).
		GuidedAddSelectedRoute = GuidedAddRouteOptions.Num() > 0 ? GuidedAddRouteOptions[0] : nullptr;
		for (const TSharedPtr<EFPSRCardRoute>& Option : GuidedAddRouteOptions)
		{
			if (Option.IsValid() && *Option == EFPSRCardRoute::MissionClearWeaponFeature)
			{
				GuidedAddSelectedRoute = Option;
				break;
			}
		}

		// Target-anchor combo is filtered by the SELECTED route (pool routes -> card pools, weapon routes -> weapons)
		// so a card can only be wired into a route-consistent target (else a pool route + weapon target would silently
		// write into the wrong membership array). Populate for the default route now — the combo handle is still null
		// at this point, so RefreshCardTargetOptions just fills GuidedAddTargetOptions / GuidedAddSelectedTarget.
		RefreshCardTargetOptions();

		GuidedAddContainer->AddSlot().AutoHeight().Padding(4.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock).Text(LOCTEXT("GuidedAddCardTitle", "고아 카드를 라우트에 배선:"))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
			[
				SNew(SComboBox<TSharedPtr<EFPSRCardRoute>>)
				.OptionsSource(&GuidedAddRouteOptions)
				.InitiallySelectedItem(GuidedAddSelectedRoute)
				.OnGenerateWidget_Lambda([](TSharedPtr<EFPSRCardRoute> Option)
				{
					return SNew(STextBlock).Text(Option.IsValid() ? FFPSRDataEditorHelpers::GetRouteDisplayText(*Option) : FText::GetEmpty());
				})
				.OnSelectionChanged_Lambda([this](TSharedPtr<EFPSRCardRoute> NewSelection, ESelectInfo::Type)
				{
					GuidedAddSelectedRoute = NewSelection;
					RefreshCardTargetOptions(); // refilter targets to the newly picked route (pool vs. weapon)
				})
				[
					SNew(STextBlock).Text_Lambda([this]()
					{
						return GuidedAddSelectedRoute.IsValid() ? FFPSRDataEditorHelpers::GetRouteDisplayText(*GuidedAddSelectedRoute) : LOCTEXT("NoEligibleRoute", "(적격 라우트 없음)");
					})
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
			[
				SAssignNew(GuidedAddTargetCombo, SComboBox<TSharedPtr<FAssetData>>)
				.OptionsSource(&GuidedAddTargetOptions)
				.InitiallySelectedItem(GuidedAddSelectedTarget)
				.OnGenerateWidget_Lambda([](TSharedPtr<FAssetData> Option)
				{
					return SNew(STextBlock).Text(Option.IsValid() ? FText::FromName(Option->AssetName) : FText::GetEmpty());
				})
				.OnSelectionChanged_Lambda([this](TSharedPtr<FAssetData> NewSelection, ESelectInfo::Type)
				{
					GuidedAddSelectedTarget = NewSelection;
				})
				[
					SNew(STextBlock).Text_Lambda([this]()
					{
						return GuidedAddSelectedTarget.IsValid() ? FText::FromName(GuidedAddSelectedTarget->AssetName) : LOCTEXT("NoTarget", "(대상 없음)");
					})
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("GuidedAddButton", "추가"))
				.OnClicked(this, &SFPSRDataEditorWidget::OnGuidedAddCardClicked)
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SAssignNew(GuidedAddStatusText, STextBlock)
			]
		];
	}
	else if (UFPSRMissionDataAsset* Mission = Cast<UFPSRMissionDataAsset>(Orphan))
	{
		GuidedAddTargetOptions.Reset();
		FARFilter Filter;
		Filter.bRecursiveClasses = false;
		Filter.ClassPaths.Add(UFPSRRunScheduleDataAsset::StaticClass()->GetClassPathName());
		TArray<FAssetData> Candidates;
		AssetRegistry.GetAssets(Filter, Candidates);
		for (const FAssetData& Candidate : Candidates)
		{
			if (!FFPSRAnchoredValidationService::IsExcludedPath(Candidate.PackagePath))
			{
				GuidedAddTargetOptions.Add(MakeShared<FAssetData>(Candidate));
			}
		}
		GuidedAddSelectedTarget = GuidedAddTargetOptions.Num() > 0 ? GuidedAddTargetOptions[0] : nullptr;

		RefreshMissionWindowOptions();

		GuidedAddContainer->AddSlot().AutoHeight().Padding(4.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock).Text(LOCTEXT("GuidedAddMissionTitle", "고아 미션을 스케줄 윈도우에 배선:"))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
			[
				SNew(SComboBox<TSharedPtr<FAssetData>>)
				.OptionsSource(&GuidedAddTargetOptions)
				.InitiallySelectedItem(GuidedAddSelectedTarget)
				.OnGenerateWidget_Lambda([](TSharedPtr<FAssetData> Option)
				{
					return SNew(STextBlock).Text(Option.IsValid() ? FText::FromName(Option->AssetName) : FText::GetEmpty());
				})
				.OnSelectionChanged_Lambda([this](TSharedPtr<FAssetData> NewSelection, ESelectInfo::Type)
				{
					GuidedAddSelectedTarget = NewSelection;
					RefreshMissionWindowOptions(); // refresh window list for the picked schedule (selection preserved, no teardown)
				})
				[
					SNew(STextBlock).Text_Lambda([this]()
					{
						return GuidedAddSelectedTarget.IsValid() ? FText::FromName(GuidedAddSelectedTarget->AssetName) : LOCTEXT("NoTarget", "(대상 없음)");
					})
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
			[
				SAssignNew(GuidedAddWindowCombo, SComboBox<TSharedPtr<int32>>)
				.OptionsSource(&GuidedAddWindowIndexOptions)
				.InitiallySelectedItem(GuidedAddSelectedWindowIndex)
				.OnGenerateWidget_Lambda([](TSharedPtr<int32> Option)
				{
					return SNew(STextBlock).Text(Option.IsValid() ? FText::AsNumber(*Option) : FText::GetEmpty());
				})
				.OnSelectionChanged_Lambda([this](TSharedPtr<int32> NewSelection, ESelectInfo::Type)
				{
					GuidedAddSelectedWindowIndex = NewSelection;
				})
				[
					SNew(STextBlock).Text_Lambda([this]()
					{
						return GuidedAddSelectedWindowIndex.IsValid() ? FText::AsNumber(*GuidedAddSelectedWindowIndex) : LOCTEXT("NoWindow", "(윈도우 없음)");
					})
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("GuidedAddButton", "추가"))
				.OnClicked(this, &SFPSRDataEditorWidget::OnGuidedAddMissionClicked)
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SAssignNew(GuidedAddStatusText, STextBlock)
			]
		];
	}
	else if (UFPSRWeaponDataAsset* Weapon = Cast<UFPSRWeaponDataAsset>(Orphan))
	{
		GuidedAddTargetOptions.Reset();
		FARFilter Filter;
		Filter.bRecursiveClasses = false;
		Filter.ClassPaths.Add(UFPSRLoadoutPoolDataAsset::StaticClass()->GetClassPathName());
		TArray<FAssetData> Candidates;
		AssetRegistry.GetAssets(Filter, Candidates);
		for (const FAssetData& Candidate : Candidates)
		{
			if (!FFPSRAnchoredValidationService::IsExcludedPath(Candidate.PackagePath))
			{
				GuidedAddTargetOptions.Add(MakeShared<FAssetData>(Candidate));
			}
		}
		GuidedAddSelectedTarget = GuidedAddTargetOptions.Num() > 0 ? GuidedAddTargetOptions[0] : nullptr;

		GuidedAddContainer->AddSlot().AutoHeight().Padding(4.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock).Text(LOCTEXT("GuidedAddWeaponTitle", "고아 무기를 로드아웃 풀에 배선:"))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
			[
				SNew(SComboBox<TSharedPtr<FAssetData>>)
				.OptionsSource(&GuidedAddTargetOptions)
				.InitiallySelectedItem(GuidedAddSelectedTarget)
				.OnGenerateWidget_Lambda([](TSharedPtr<FAssetData> Option)
				{
					return SNew(STextBlock).Text(Option.IsValid() ? FText::FromName(Option->AssetName) : FText::GetEmpty());
				})
				.OnSelectionChanged_Lambda([this](TSharedPtr<FAssetData> NewSelection, ESelectInfo::Type)
				{
					GuidedAddSelectedTarget = NewSelection;
				})
				[
					SNew(STextBlock).Text_Lambda([this]()
					{
						return GuidedAddSelectedTarget.IsValid() ? FText::FromName(GuidedAddSelectedTarget->AssetName) : LOCTEXT("NoTarget", "(대상 없음)");
					})
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("GuidedAddButton", "추가"))
				.OnClicked(this, &SFPSRDataEditorWidget::OnGuidedAddWeaponClicked)
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SAssignNew(GuidedAddStatusText, STextBlock)
			]
		];
	}
	// Other orphan classes (e.g. UFPSRWeaponFragment) have no guided-add affordance in P1 — the details view above
	// still lets a designer inspect/edit it, and IsDataValid/orphan discovery keep reporting it until it's referenced.
}

bool SFPSRDataEditorWidget::RouteExpectsPool(EFPSRCardRoute Route)
{
	return Route == EFPSRCardRoute::LevelUpGlobal || Route == EFPSRCardRoute::MissionClearNewWeapon;
}

void SFPSRDataEditorWidget::RefreshCardTargetOptions()
{
	// Filter the card guided-add target list by the selected route so a card can only be wired into a route-consistent
	// target (pool routes -> card pools, weapon routes -> weapons). At initial build the combo handle is still null, so
	// this just fills the options; on a route change the combo is valid, so refresh + reselect it.
	GuidedAddTargetOptions.Reset();
	if (GuidedAddSelectedRoute.IsValid())
	{
		const bool bPoolRoute = RouteExpectsPool(*GuidedAddSelectedRoute);
		IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		FARFilter Filter;
		Filter.bRecursiveClasses = false;
		Filter.ClassPaths.Add((bPoolRoute
			? UFPSRCardPoolDataAsset::StaticClass()
			: UFPSRWeaponDataAsset::StaticClass())->GetClassPathName());
		TArray<FAssetData> Candidates;
		AssetRegistry.GetAssets(Filter, Candidates);

		// Weapon-route targets must be REACHABLE weapons: wiring a card into an ORPHAN weapon wouldn't make the card
		// reachable (the weapon itself is unreachable from any anchor), so the "repair" would silently leave the card
		// orphaned and still report "Added.". Pool routes target card pools, which ARE anchors (reachable by
		// definition), so they need no such filter.
		TSet<FName> ReachablePackages;
		if (!bPoolRoute)
		{
			for (const FAssetData& Reachable : FFPSRAnchoredValidationService::GatherAssetsToValidate())
			{
				ReachablePackages.Add(Reachable.PackageName);
			}
		}
		for (const FAssetData& Candidate : Candidates)
		{
			if (FFPSRAnchoredValidationService::IsExcludedPath(Candidate.PackagePath))
			{
				continue;
			}
			if (!bPoolRoute && !ReachablePackages.Contains(Candidate.PackageName))
			{
				continue; // orphan weapon — wiring a card into it wouldn't un-orphan the card
			}
			GuidedAddTargetOptions.Add(MakeShared<FAssetData>(Candidate));
		}
	}
	GuidedAddSelectedTarget = GuidedAddTargetOptions.Num() > 0 ? GuidedAddTargetOptions[0] : nullptr;
	if (GuidedAddTargetCombo.IsValid())
	{
		GuidedAddTargetCombo->RefreshOptions();
		GuidedAddTargetCombo->SetSelectedItem(GuidedAddSelectedTarget);
	}
}

void SFPSRDataEditorWidget::RefreshMissionWindowOptions()
{
	// Rebuild the window-index list for the currently selected schedule WITHOUT tearing down the guided-add UI (a full
	// RebuildGuidedAddForOrphan resets the schedule selection to the first candidate — the bug this avoids).
	GuidedAddWindowIndexOptions.Reset();
	if (const UFPSRRunScheduleDataAsset* Schedule = GuidedAddSelectedTarget.IsValid() ? Cast<UFPSRRunScheduleDataAsset>(GuidedAddSelectedTarget->GetAsset()) : nullptr)
	{
		for (int32 WindowIndex = 0; WindowIndex < Schedule->MissionWindows.Num(); ++WindowIndex)
		{
			GuidedAddWindowIndexOptions.Add(MakeShared<int32>(WindowIndex));
		}
	}
	GuidedAddSelectedWindowIndex = GuidedAddWindowIndexOptions.Num() > 0 ? GuidedAddWindowIndexOptions[0] : nullptr;
	if (GuidedAddWindowCombo.IsValid())
	{
		GuidedAddWindowCombo->RefreshOptions();
		GuidedAddWindowCombo->SetSelectedItem(GuidedAddSelectedWindowIndex);
	}
}

void SFPSRDataEditorWidget::OnDetailsPropertyChanged(const FPropertyChangedEvent& Event)
{
	// A details-panel edit marks the object's package dirty but does not register it with this tool's Save+Rescan
	// tracking, so without this hook "Save Modified + Rescan" would save nothing while the stale status read "Up to
	// date". Track every object the details view currently shows.
	if (!DetailsView.IsValid())
	{
		return;
	}
	for (const TWeakObjectPtr<UObject>& WeakObject : DetailsView->GetSelectedObjects())
	{
		if (UObject* Object = WeakObject.Get())
		{
			TrackDirtyPackage(Object->GetPackage());
		}
	}

	// The edit may have reshaped the asset (removed/reordered a card effect, changed mission windows), so rebuild the
	// aux panels — otherwise the magnitude grid keeps stale EffectIndex rows that would commit to the wrong effect.
	RebuildAuxPanels();
}

FReply SFPSRDataEditorWidget::OnGuidedAddCardClicked()
{
	UFPSRCardDataAsset* Card = Cast<UFPSRCardDataAsset>(GuidedAddOrphan.Get());
	if (!Card || !GuidedAddSelectedRoute.IsValid() || !GuidedAddSelectedTarget.IsValid())
	{
		return FReply::Handled();
	}

	const EFPSRCardRoute Route = *GuidedAddSelectedRoute;
	FText Reason;
	const EFPSRWiringVerdict Verdict = FFPSRDataEditorHelpers::CheckCardRoute(Card, Route, Reason);
	if (Verdict == EFPSRWiringVerdict::Blocked)
	{
		if (GuidedAddStatusText.IsValid())
		{
			GuidedAddStatusText->SetText(Reason);
			GuidedAddStatusText->SetColorAndOpacity(FLinearColor(0.9f, 0.2f, 0.2f));
		}
		return FReply::Handled();
	}

	UObject* TargetAsset = GuidedAddSelectedTarget->GetAsset();
	bool bAdded = false;
	UPackage* TargetPackage = nullptr;
	// Decide the destination array by the ROUTE (not by whatever the target happens to cast to), and require the
	// selected target to be the route's matching kind — otherwise a pool route + weapon target (or vice-versa) would
	// silently wire the card into the wrong membership array despite the route preflight passing.
	if (RouteExpectsPool(Route))
	{
		UFPSRCardPoolDataAsset* Pool = Cast<UFPSRCardPoolDataAsset>(TargetAsset);
		if (!Pool)
		{
			if (GuidedAddStatusText.IsValid())
			{
				GuidedAddStatusText->SetText(LOCTEXT("RouteNeedsPool", "이 라우트의 대상은 카드 풀입니다 — 대상으로 카드 풀을 선택하세요."));
				GuidedAddStatusText->SetColorAndOpacity(FLinearColor(0.9f, 0.2f, 0.2f));
			}
			return FReply::Handled();
		}
		bAdded = FFPSRDataEditorHelpers::AddCardToPool(Pool, Card, /*bUnlockArray=*/Route == EFPSRCardRoute::MissionClearNewWeapon);
		TargetPackage = Pool->GetPackage();
	}
	else
	{
		UFPSRWeaponDataAsset* Weapon = Cast<UFPSRWeaponDataAsset>(TargetAsset);
		if (!Weapon)
		{
			if (GuidedAddStatusText.IsValid())
			{
				GuidedAddStatusText->SetText(LOCTEXT("RouteNeedsWeapon", "이 라우트의 대상은 무기입니다 — 대상으로 무기를 선택하세요."));
				GuidedAddStatusText->SetColorAndOpacity(FLinearColor(0.9f, 0.2f, 0.2f));
			}
			return FReply::Handled();
		}
		// A card in a weapon's LEVEL-UP pool (WeaponCards) must be Group=Weapon, or GatherCandidatePool won't set the
		// draw's TargetWeapon (FPSRCardSubsystem.cpp:603) and the offer would apply to the equipped weapon instead of
		// the source weapon. Set it so the wiring is correct-by-construction (the card's own package is tracked too).
		if (Route == EFPSRCardRoute::LevelUpWeapon && Card->Group != ECardGroup::Weapon)
		{
			if (FFPSRDataEditorHelpers::SetCardGroup(Card, ECardGroup::Weapon))
			{
				TrackDirtyPackage(Card->GetPackage());
			}
		}
		bAdded = FFPSRDataEditorHelpers::AddCardToWeapon(Weapon, Card, /*bUnlockableFeatures=*/Route == EFPSRCardRoute::MissionClearWeaponFeature);
		TargetPackage = Weapon->GetPackage();
	}

	if (bAdded && TargetPackage)
	{
		TrackDirtyPackage(TargetPackage);
	}
	if (GuidedAddStatusText.IsValid())
	{
		if (Verdict == EFPSRWiringVerdict::Warn)
		{
			GuidedAddStatusText->SetText(bAdded ? FText::Format(LOCTEXT("AddedWithWarning", "추가됨(경고): {0}"), Reason) : Reason);
			GuidedAddStatusText->SetColorAndOpacity(FLinearColor(0.9f, 0.7f, 0.2f));
		}
		else
		{
			GuidedAddStatusText->SetText(bAdded ? LOCTEXT("AddedOk", "추가됨.") : LOCTEXT("AddedNoop", "이미 있음 — 변경 없음"));
			GuidedAddStatusText->SetColorAndOpacity(FLinearColor::White);
		}
	}
	if (bAdded)
	{
		RefreshLists();
	}
	return FReply::Handled();
}

FReply SFPSRDataEditorWidget::OnGuidedAddMissionClicked()
{
	UFPSRMissionDataAsset* Mission = Cast<UFPSRMissionDataAsset>(GuidedAddOrphan.Get());
	UFPSRRunScheduleDataAsset* Schedule = GuidedAddSelectedTarget.IsValid() ? Cast<UFPSRRunScheduleDataAsset>(GuidedAddSelectedTarget->GetAsset()) : nullptr;
	if (!Mission || !Schedule || !GuidedAddSelectedWindowIndex.IsValid())
	{
		return FReply::Handled();
	}

	const bool bAdded = FFPSRDataEditorHelpers::AddMissionToScheduleWindow(Schedule, *GuidedAddSelectedWindowIndex, Mission);
	if (bAdded)
	{
		TrackDirtyPackage(Schedule->GetPackage());
	}
	if (GuidedAddStatusText.IsValid())
	{
		GuidedAddStatusText->SetText(bAdded ? LOCTEXT("AddedOk", "추가됨.") : LOCTEXT("AddedNoop", "이미 있음 — 변경 없음"));
	}
	if (bAdded)
	{
		RefreshLists();
	}
	return FReply::Handled();
}

FReply SFPSRDataEditorWidget::OnGuidedAddWeaponClicked()
{
	UFPSRWeaponDataAsset* Weapon = Cast<UFPSRWeaponDataAsset>(GuidedAddOrphan.Get());
	UFPSRLoadoutPoolDataAsset* Loadout = GuidedAddSelectedTarget.IsValid() ? Cast<UFPSRLoadoutPoolDataAsset>(GuidedAddSelectedTarget->GetAsset()) : nullptr;
	if (!Weapon || !Loadout)
	{
		return FReply::Handled();
	}

	const bool bAdded = FFPSRDataEditorHelpers::AddWeaponToLoadout(Loadout, Weapon);
	if (bAdded)
	{
		TrackDirtyPackage(Loadout->GetPackage());
	}
	if (GuidedAddStatusText.IsValid())
	{
		GuidedAddStatusText->SetText(bAdded ? LOCTEXT("AddedOk", "추가됨.") : LOCTEXT("AddedNoop", "이미 있음 — 변경 없음"));
	}
	if (bAdded)
	{
		RefreshLists();
	}
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
