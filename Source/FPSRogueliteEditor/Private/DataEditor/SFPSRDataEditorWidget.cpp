// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataEditor/SFPSRDataEditorWidget.h"

#include "Validation/FPSRAnchoredValidationService.h"
#include "DataEditor/FPSRDataEditorHelpers.h"

#include "Card/FPSRCardDataAsset.h"
#include "Card/FPSRCardEffect.h"
#include "Card/FPSRCardPoolDataAsset.h"
#include "Weapon/FPSRWeaponDataAsset.h"
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

			const FString Label = FString::Printf(TEXT("Window %d (%d mission%s)"), WindowIndex, Window.MissionPool.Num(), Window.MissionPool.Num() == 1 ? TEXT("") : TEXT("s"));
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
			FSlateDrawElement::MakeText(OutDrawElements, Layer + 3, BossLabelGeometry, FString(TEXT("Boss")), Font, ESlateDrawEffect::None, FLinearColor(1.0f, 0.6f, 0.6f));
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
					.Text(LOCTEXT("HeaderTitle", "FPSR Data Editor"))
					.Font(FAppStyle::GetFontStyle("HeadingExtraSmall"))
				]

				+ SVerticalBox::Slot().AutoHeight().Padding(4.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("SaveAndRescan", "Save Modified + Rescan"))
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
					SNew(STextBlock).Text(LOCTEXT("AnchorsHeader", "Anchors (Card Pool / Run Schedule / Loadout Pool)"))
				]

				+ SVerticalBox::Slot().FillHeight(0.5f).Padding(4.0f)
				[
					SAssignNew(AnchorListView, SListView<TSharedPtr<FAssetData>>)
					.ListItemsSource(&AnchorItems)
					.OnGenerateRow(this, &SFPSRDataEditorWidget::OnGenerateAnchorRow)
					.OnSelectionChanged(this, &SFPSRDataEditorWidget::OnAnchorSelectionChanged)
					.SelectionMode(ESelectionMode::Single)
				]

				+ SVerticalBox::Slot().AutoHeight().Padding(4.0f, 6.0f, 4.0f, 2.0f)
				[
					SNew(STextBlock).Text(LOCTEXT("OrphansHeader", "Orphans (unreachable from any anchor)"))
				]

				+ SVerticalBox::Slot().FillHeight(0.5f).Padding(4.0f)
				[
					SAssignNew(OrphanListView, SListView<TSharedPtr<FAssetData>>)
					.ListItemsSource(&OrphanItems)
					.OnGenerateRow(this, &SFPSRDataEditorWidget::OnGenerateOrphanRow)
					.OnSelectionChanged(this, &SFPSRDataEditorWidget::OnOrphanSelectionChanged)
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
			ScanStatusText->SetText(LOCTEXT("StillScanning", "Asset Registry still scanning..."));
			ScanStatusText->SetVisibility(EVisibility::Visible);
		}
		AnchorItems.Reset();
		OrphanItems.Reset();
		if (AnchorListView.IsValid()) { AnchorListView->RequestListRefresh(); }
		if (OrphanListView.IsValid()) { OrphanListView->RequestListRefresh(); }
		return;
	}

	if (ScanStatusText.IsValid())
	{
		ScanStatusText->SetVisibility(EVisibility::Collapsed);
	}

	AnchorItems.Reset();
	for (const FAssetData& Anchor : FFPSRAnchoredValidationService::FindAnchorAssets())
	{
		AnchorItems.Add(MakeShared<FAssetData>(Anchor));
	}
	if (AnchorListView.IsValid())
	{
		AnchorListView->RequestListRefresh();
	}

	OrphanItems.Reset();
	for (const FAssetData& Orphan : FFPSRAnchoredValidationService::FindOrphans())
	{
		OrphanItems.Add(MakeShared<FAssetData>(Orphan));
	}
	if (OrphanListView.IsValid())
	{
		OrphanListView->RequestListRefresh();
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
	DirtyTrackedPackages.Reset();

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
		return LOCTEXT("UpToDate", "Up to date");
	}
	return FText::Format(LOCTEXT("StaleStatus", "{0} unsaved edit(s) — validation reflects last save"), FText::AsNumber(DirtyTrackedPackages.Num()));
}

// ---------------------------------------------------------------------------------------------------------------
// Anchor / orphan lists (custom widget #1)
// ---------------------------------------------------------------------------------------------------------------

TSharedRef<ITableRow> SFPSRDataEditorWidget::OnGenerateAnchorRow(TSharedPtr<FAssetData> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(STableRow<TSharedPtr<FAssetData>>, OwnerTable)
		[
			SNew(STextBlock).Text(Item.IsValid() ? FText::FromName(Item->AssetName) : FText::GetEmpty())
		];
}

TSharedRef<ITableRow> SFPSRDataEditorWidget::OnGenerateOrphanRow(TSharedPtr<FAssetData> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(STableRow<TSharedPtr<FAssetData>>, OwnerTable)
		[
			SNew(STextBlock).Text(Item.IsValid() ? FText::FromName(Item->AssetName) : FText::GetEmpty())
			.ColorAndOpacity(FLinearColor(0.9f, 0.7f, 0.2f))
		];
}

void SFPSRDataEditorWidget::OnAnchorSelectionChanged(TSharedPtr<FAssetData> Item, ESelectInfo::Type SelectInfo)
{
	OnAssetSelected(Item, /*bIsOrphan=*/false);
}

void SFPSRDataEditorWidget::OnOrphanSelectionChanged(TSharedPtr<FAssetData> Item, ESelectInfo::Type SelectInfo)
{
	OnAssetSelected(Item, /*bIsOrphan=*/true);
}

void SFPSRDataEditorWidget::OnAssetSelected(const TSharedPtr<FAssetData>& Item, bool bIsOrphan)
{
	ClearMagnitudeGrid();
	ClearScheduleTimeline();
	ClearGuidedAdd();

	if (!Item.IsValid())
	{
		if (DetailsView.IsValid())
		{
			DetailsView->SetObject(nullptr);
		}
		return;
	}

	UObject* Asset = Item->GetAsset();
	if (DetailsView.IsValid())
	{
		DetailsView->SetObject(Asset);
	}
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

	if (bIsOrphan)
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
		.AreaTitle(LOCTEXT("MagnitudeGridTitle", "Card Magnitude Grid"))
		.InitiallyCollapsed(false)
		.BodyContent()
		[
			SAssignNew(MagnitudeGridListView, SListView<TSharedPtr<FMagnitudeGridRow>>)
			.ListItemsSource(&MagnitudeGridItems)
			.OnGenerateRow(this, &SFPSRDataEditorWidget::OnGenerateMagnitudeGridRow)
			.SelectionMode(ESelectionMode::None)
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
		.AreaTitle(LOCTEXT("MagnitudeGridTitlePool", "Card Magnitude Grid (all cards in this pool)"))
		.InitiallyCollapsed(true) // a pool can be large — collapsed by default, unlike a single-card selection
		.BodyContent()
		[
			SAssignNew(MagnitudeGridListView, SListView<TSharedPtr<FMagnitudeGridRow>>)
			.ListItemsSource(&MagnitudeGridItems)
			.OnGenerateRow(this, &SFPSRDataEditorWidget::OnGenerateMagnitudeGridRow)
			.SelectionMode(ESelectionMode::None)
		]
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
	const FText CardIdText = Card->CardId.IsNone() ? LOCTEXT("NoCardId", "(no CardId)") : FText::FromName(Card->CardId);
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
	const TOptional<float> Value = Tier ? TOptional<float>(Tier->Magnitude) : TOptional<float>();
	const bool bEnabled = Tier != nullptr;

	return SNew(SNumericEntryBox<float>)
		.IsEnabled(bEnabled)
		.Value(Value)
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
		.AreaTitle(LOCTEXT("ScheduleTimelineTitle", "Mission Schedule Timeline (read-only)"))
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

		// Target-anchor combo: card pools (for pool routes) or weapons (for weapon routes) — populated from the
		// SAME anchor discovery the left panel uses, re-run here rather than reusing AnchorItems so a target class
		// filter (weapon vs. pool) applies. Weapons aren't anchors themselves, so query them directly.
		GuidedAddTargetOptions.Reset();
		FARFilter Filter;
		Filter.bRecursiveClasses = false;
		Filter.ClassPaths.Add(UFPSRCardPoolDataAsset::StaticClass()->GetClassPathName());
		Filter.ClassPaths.Add(UFPSRWeaponDataAsset::StaticClass()->GetClassPathName());
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
				SNew(STextBlock).Text(LOCTEXT("GuidedAddCardTitle", "Wire orphan card into a route:"))
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
				})
				[
					SNew(STextBlock).Text_Lambda([this]()
					{
						return GuidedAddSelectedRoute.IsValid() ? FFPSRDataEditorHelpers::GetRouteDisplayText(*GuidedAddSelectedRoute) : LOCTEXT("NoEligibleRoute", "(no eligible route)");
					})
				]
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
						return GuidedAddSelectedTarget.IsValid() ? FText::FromName(GuidedAddSelectedTarget->AssetName) : LOCTEXT("NoTarget", "(no target)");
					})
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("GuidedAddButton", "Add"))
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

		GuidedAddWindowIndexOptions.Reset();
		if (const UFPSRRunScheduleDataAsset* Schedule = GuidedAddSelectedTarget.IsValid() ? Cast<UFPSRRunScheduleDataAsset>(GuidedAddSelectedTarget->GetAsset()) : nullptr)
		{
			for (int32 WindowIndex = 0; WindowIndex < Schedule->MissionWindows.Num(); ++WindowIndex)
			{
				GuidedAddWindowIndexOptions.Add(MakeShared<int32>(WindowIndex));
			}
		}
		GuidedAddSelectedWindowIndex = GuidedAddWindowIndexOptions.Num() > 0 ? GuidedAddWindowIndexOptions[0] : nullptr;

		GuidedAddContainer->AddSlot().AutoHeight().Padding(4.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock).Text(LOCTEXT("GuidedAddMissionTitle", "Wire orphan mission into a schedule window:"))
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
					// Defer the rebuild to the next tick rather than tearing down GuidedAddContainer's children
					// (including this very combo box) synchronously from inside its own OnSelectionChanged —
					// RegisterActiveTimer is the standard Slate-safe way to "rebuild my own UI from a child callback".
					RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateLambda(
						[this](double, float) -> EActiveTimerReturnType
						{
							if (UFPSRMissionDataAsset* PinnedMission = Cast<UFPSRMissionDataAsset>(GuidedAddOrphan.Get()))
							{
								RebuildGuidedAddForOrphan(PinnedMission); // re-derive the window-index list for the newly picked schedule
							}
							return EActiveTimerReturnType::Stop;
						}));
				})
				[
					SNew(STextBlock).Text_Lambda([this]()
					{
						return GuidedAddSelectedTarget.IsValid() ? FText::FromName(GuidedAddSelectedTarget->AssetName) : LOCTEXT("NoTarget", "(no target)");
					})
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
			[
				SNew(SComboBox<TSharedPtr<int32>>)
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
						return GuidedAddSelectedWindowIndex.IsValid() ? FText::AsNumber(*GuidedAddSelectedWindowIndex) : LOCTEXT("NoWindow", "(no window)");
					})
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("GuidedAddButton", "Add"))
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
				SNew(STextBlock).Text(LOCTEXT("GuidedAddWeaponTitle", "Wire orphan weapon into a loadout pool:"))
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
						return GuidedAddSelectedTarget.IsValid() ? FText::FromName(GuidedAddSelectedTarget->AssetName) : LOCTEXT("NoTarget", "(no target)");
					})
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("GuidedAddButton", "Add"))
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
	if (UFPSRCardPoolDataAsset* Pool = Cast<UFPSRCardPoolDataAsset>(TargetAsset))
	{
		const bool bUnlockArray = (Route == EFPSRCardRoute::MissionClearNewWeapon);
		bAdded = FFPSRDataEditorHelpers::AddCardToPool(Pool, Card, bUnlockArray);
		TargetPackage = Pool->GetPackage();
	}
	else if (UFPSRWeaponDataAsset* Weapon = Cast<UFPSRWeaponDataAsset>(TargetAsset))
	{
		const bool bUnlockableFeatures = (Route == EFPSRCardRoute::MissionClearWeaponFeature);
		bAdded = FFPSRDataEditorHelpers::AddCardToWeapon(Weapon, Card, bUnlockableFeatures);
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
			GuidedAddStatusText->SetText(bAdded ? FText::Format(LOCTEXT("AddedWithWarning", "Added (with warning): {0}"), Reason) : Reason);
			GuidedAddStatusText->SetColorAndOpacity(FLinearColor(0.9f, 0.7f, 0.2f));
		}
		else
		{
			GuidedAddStatusText->SetText(bAdded ? LOCTEXT("AddedOk", "Added.") : LOCTEXT("AddedNoop", "Already present — no change."));
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
		GuidedAddStatusText->SetText(bAdded ? LOCTEXT("AddedOk", "Added.") : LOCTEXT("AddedNoop", "Already present — no change."));
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
		GuidedAddStatusText->SetText(bAdded ? LOCTEXT("AddedOk", "Added.") : LOCTEXT("AddedNoop", "Already present — no change."));
	}
	if (bAdded)
	{
		RefreshLists();
	}
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
