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
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/SLeafWidget.h"
#include "Rendering/DrawElements.h"
#include "Styling/AppStyle.h"
#include "InputCoreTypes.h"

#define LOCTEXT_NAMESPACE "SFPSRDataEditorWidget"

// =====================================================================================================================
// SFPSRScheduleTimelineBar — custom widget #3 of the hard-capped three. Draws mission windows + boss time on a
// horizontal time axis and lets a designer DRAG a window's edges (resize MinTime/MaxTime) or body (move, preserving
// width) to retime it. Commit is on mouse-up only (one transaction per drag, not per pixel); OnPaint previews the
// in-flight drag from Pending{Min,Max} before the write lands.
// =====================================================================================================================
class SFPSRScheduleTimelineBar : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SFPSRScheduleTimelineBar) {}
		/** Fired once, on mouse-up, after a drag has committed a [MinTime,MaxTime] write via
		 *  FFPSRDataEditorHelpers::SetScheduleWindowTime — lets the owner track the package as dirty. */
		SLATE_EVENT(FSimpleDelegate, OnEdited)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TWeakObjectPtr<UFPSRRunScheduleDataAsset> InSchedule)
	{
		Schedule = InSchedule;
		OnEdited = InArgs._OnEdited;
	}

	virtual FVector2D ComputeDesiredSize(float) const override
	{
		return FVector2D(400.0f, 140.0f);
	}

	virtual bool SupportsKeyboardFocus() const override { return false; }

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

		const float MaxAxisTime = ComputeMaxAxisTime(ScheduleAsset);

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

		// One horizontal bar per mission window, stacked vertically so overlapping windows are still legible. The
		// window currently being dragged is drawn from the PENDING (uncommitted) time range so the drag previews
		// live; every other window reads its committed MinTime/MaxTime straight off the asset.
		const float RowHeight = 20.0f;
		const float TopPadding = 8.0f;
		for (int32 WindowIndex = 0; WindowIndex < ScheduleAsset->MissionWindows.Num(); ++WindowIndex)
		{
			const FFPSRMissionWindow& Window = ScheduleAsset->MissionWindows[WindowIndex];
			const bool bIsDragPreview = (WindowIndex == DragWindowIndex && DragMode != EDragMode::None);
			const float MinTime = bIsDragPreview ? PendingMin : Window.MinTime;
			const float MaxTime = bIsDragPreview ? PendingMax : Window.MaxTime;
			const float StartX = TimeToX(MinTime);
			const float EndX = FMath::Max(TimeToX(MaxTime), StartX + 2.0f);
			const float RowY = TopPadding + WindowIndex * RowHeight;
			if (RowY > LocalSize.Y - RowHeight)
			{
				break; // ran out of vertical room — not a scroll view; later windows are simply omitted from the preview
			}

			const FLinearColor BarColor = bIsDragPreview ? FLinearColor(0.95f, 0.75f, 0.2f, 0.9f) : FLinearColor(0.2f, 0.5f, 0.8f, 0.85f);
			const FPaintGeometry BarGeometry = AllottedGeometry.ToPaintGeometry(
				FVector2D(EndX - StartX, RowHeight - 4.0f),
				FSlateLayoutTransform(FVector2D(StartX, RowY)));
			FSlateDrawElement::MakeBox(OutDrawElements, Layer, BarGeometry, WhiteBrush, ESlateDrawEffect::None, BarColor);

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

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		const UFPSRRunScheduleDataAsset* ScheduleAsset = Schedule.Get();
		if (!ScheduleAsset || !MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton))
		{
			return FReply::Unhandled();
		}

		const FVector2D LocalSize = MyGeometry.GetLocalSize();
		if (LocalSize.X <= 0.0f || LocalSize.Y <= 0.0f)
		{
			return FReply::Unhandled();
		}

		const FVector2D LocalPos = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
		const float MaxAxisTime = ComputeMaxAxisTime(ScheduleAsset);
		const float MouseX = LocalPos.X;

		int32 HitWindowIndex = INDEX_NONE;
		EDragMode HitMode = EDragMode::None;
		HitTest(ScheduleAsset, LocalSize, MaxAxisTime, LocalPos, HitWindowIndex, HitMode);

		if (HitMode == EDragMode::None || !ScheduleAsset->MissionWindows.IsValidIndex(HitWindowIndex))
		{
			return FReply::Unhandled();
		}

		const FFPSRMissionWindow& Window = ScheduleAsset->MissionWindows[HitWindowIndex];
		DragWindowIndex = HitWindowIndex;
		DragMode = HitMode;
		PendingMin = Window.MinTime;
		PendingMax = Window.MaxTime;
		DragStartMin = Window.MinTime;
		DragStartMax = Window.MaxTime;
		DragStartTime = (MouseX / LocalSize.X) * MaxAxisTime;

		return FReply::Handled().CaptureMouse(SharedThis(this));
	}

	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (!HasMouseCapture() || DragMode == EDragMode::None)
		{
			return FReply::Unhandled();
		}

		const UFPSRRunScheduleDataAsset* ScheduleAsset = Schedule.Get();
		const FVector2D LocalSize = MyGeometry.GetLocalSize();
		if (!ScheduleAsset || LocalSize.X <= 0.0f)
		{
			return FReply::Handled();
		}

		const float MaxAxisTime = ComputeMaxAxisTime(ScheduleAsset);
		const FVector2D LocalPos = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
		const float ClampedX = FMath::Clamp(LocalPos.X, 0.0f, LocalSize.X);
		const float MouseTime = (ClampedX / LocalSize.X) * MaxAxisTime;

		switch (DragMode)
		{
		case EDragMode::Min:
			PendingMin = FMath::Clamp(MouseTime, 0.0f, PendingMax);
			break;
		case EDragMode::Max:
			PendingMax = FMath::Max(MouseTime, PendingMin);
			break;
		case EDragMode::Move:
		{
			const float Delta = MouseTime - DragStartTime;
			const float Width = DragStartMax - DragStartMin;
			PendingMin = FMath::Max(0.0f, DragStartMin + Delta);
			PendingMax = PendingMin + Width;
			break;
		}
		default:
			break;
		}

		return FReply::Handled();
	}

	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (DragMode != EDragMode::None)
		{
			if (UFPSRRunScheduleDataAsset* ScheduleAsset = Schedule.Get())
			{
				if (FFPSRDataEditorHelpers::SetScheduleWindowTime(ScheduleAsset, DragWindowIndex, PendingMin, PendingMax))
				{
					OnEdited.ExecuteIfBound();
				}
			}
		}

		DragMode = EDragMode::None;
		DragWindowIndex = INDEX_NONE;

		return FReply::Handled().ReleaseMouseCapture();
	}

	virtual FCursorReply OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const override
	{
		const UFPSRRunScheduleDataAsset* ScheduleAsset = Schedule.Get();
		const FVector2D LocalSize = MyGeometry.GetLocalSize();
		if (!ScheduleAsset || LocalSize.X <= 0.0f || LocalSize.Y <= 0.0f)
		{
			return FCursorReply::Cursor(EMouseCursor::Default);
		}

		const float MaxAxisTime = ComputeMaxAxisTime(ScheduleAsset);
		const FVector2D LocalPos = MyGeometry.AbsoluteToLocal(CursorEvent.GetScreenSpacePosition());

		int32 HitWindowIndex = INDEX_NONE;
		EDragMode HitMode = EDragMode::None;
		HitTest(ScheduleAsset, LocalSize, MaxAxisTime, LocalPos, HitWindowIndex, HitMode);

		if (HitMode == EDragMode::Min || HitMode == EDragMode::Max)
		{
			return FCursorReply::Cursor(EMouseCursor::ResizeLeftRight);
		}
		return FCursorReply::Cursor(EMouseCursor::Default);
	}

private:
	enum class EDragMode : uint8 { None, Min, Max, Move };

	/** Time axis range: [0 .. max(BossTime, every window's MaxTime)]. Guards an all-zero/empty schedule so callers
	 *  never divide by zero. Shared by OnPaint and every mouse handler so hit-testing and drawing always agree. */
	static float ComputeMaxAxisTime(const UFPSRRunScheduleDataAsset* ScheduleAsset)
	{
		float MaxAxisTime = ScheduleAsset->BossTime;
		for (const FFPSRMissionWindow& Window : ScheduleAsset->MissionWindows)
		{
			MaxAxisTime = FMath::Max(MaxAxisTime, Window.MaxTime);
		}
		if (MaxAxisTime <= 0.0f)
		{
			MaxAxisTime = 1.0f;
		}
		return MaxAxisTime;
	}

	/** Shared hit-test used by both OnMouseButtonDown (to start a drag) and OnCursorQuery (to preview the cursor).
	 *  Mirrors OnPaint's row layout exactly (RowHeight/TopPadding) so the hit area always matches what's drawn.
	 *  Edge grab tolerance is +/-6px; anywhere else between the edges (but within the row) is a Move; outside every
	 *  row's Y range is None. */
	static void HitTest(const UFPSRRunScheduleDataAsset* ScheduleAsset, const FVector2D& LocalSize, float MaxAxisTime, const FVector2D& LocalPos, int32& OutWindowIndex, EDragMode& OutMode)
	{
		OutWindowIndex = INDEX_NONE;
		OutMode = EDragMode::None;

		const float RowHeight = 20.0f;
		const float TopPadding = 8.0f;
		const float EdgeTolerance = 6.0f;

		auto TimeToX = [&LocalSize, MaxAxisTime](float Time) -> float
		{
			return (Time / MaxAxisTime) * LocalSize.X;
		};

		for (int32 WindowIndex = 0; WindowIndex < ScheduleAsset->MissionWindows.Num(); ++WindowIndex)
		{
			const FFPSRMissionWindow& Window = ScheduleAsset->MissionWindows[WindowIndex];
			const float RowY = TopPadding + WindowIndex * RowHeight;
			if (RowY > LocalSize.Y - RowHeight)
			{
				break; // matches OnPaint's early-out — rows beyond the visible area aren't interactive either
			}
			if (LocalPos.Y < RowY || LocalPos.Y > RowY + RowHeight)
			{
				continue;
			}

			const float StartX = TimeToX(Window.MinTime);
			const float EndX = FMath::Max(TimeToX(Window.MaxTime), StartX + 2.0f);

			// Edge grab: when the mouse is within tolerance of BOTH edges (a narrow window where StartX/EndX are
			// closer than 2*EdgeTolerance), pick the CLOSEST edge — otherwise the left-edge check would always win
			// and the right edge (Max) could never be resized on a narrow/exact window (Codex P2).
			const float DistToStart = FMath::Abs(LocalPos.X - StartX);
			const float DistToEnd = FMath::Abs(LocalPos.X - EndX);
			const bool bNearStart = DistToStart <= EdgeTolerance;
			const bool bNearEnd = DistToEnd <= EdgeTolerance;
			if (bNearStart || bNearEnd)
			{
				OutWindowIndex = WindowIndex;
				OutMode = (bNearStart && (!bNearEnd || DistToStart <= DistToEnd)) ? EDragMode::Min : EDragMode::Max;
				return;
			}
			if (LocalPos.X > StartX && LocalPos.X < EndX)
			{
				OutWindowIndex = WindowIndex;
				OutMode = EDragMode::Move;
				return;
			}
		}
	}

	TWeakObjectPtr<UFPSRRunScheduleDataAsset> Schedule;
	FSimpleDelegate OnEdited;

	int32 DragWindowIndex = INDEX_NONE;
	EDragMode DragMode = EDragMode::None;
	float PendingMin = 0.0f;
	float PendingMax = 0.0f;
	float DragStartTime = 0.0f;
	float DragStartMin = 0.0f;
	float DragStartMax = 0.0f;
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

	// Only rows for magnitude-bearing effects (GetEditorMagnitudeUnit() != None) — a fresh effect with zero
	// RarityTiers still surfaces as a row (P2: the offer-rarity toggle row can create its first tier), while an
	// effect whose runtime never reads magnitude (grant/passive/behavior) is left to the IDetailsView above for its
	// own non-magnitude fields (e.g. UCardEffect_GrantWeapon::WeaponToGrant).
	for (int32 EffectIndex = 0; EffectIndex < Card->Effects.Num(); ++EffectIndex)
	{
		if (Card->Effects[EffectIndex] && Card->Effects[EffectIndex]->GetEditorMagnitudeUnit() != EFPSREditorMagnitudeUnit::None)
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
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 4.0f)
			[
				BuildOfferRarityToggleRow(Card)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 4.0f)
			[
				BuildBulkOpToolbar()
			]
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
				if (Card->Effects[EffectIndex] && Card->Effects[EffectIndex]->GetEditorMagnitudeUnit() != EFPSREditorMagnitudeUnit::None)
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
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 4.0f)
			[
				BuildBulkOpToolbar()
			]
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

TSharedRef<SWidget> SFPSRDataEditorWidget::BuildOfferRarityToggleRow(UFPSRCardDataAsset* Card)
{
	// One checkbox per rarity — checked state is re-queried every frame from Card->OfferRarities (IsChecked_Lambda),
	// never cached, so a create/delete elsewhere (e.g. IDetailsView editing RarityTiers directly) stays reflected.
	TWeakObjectPtr<UFPSRCardDataAsset> WeakCard = Card;

	auto MakeToggle = [this, WeakCard](ECardRarity Rarity, FText Label)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2.0f, 0.0f)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([WeakCard, Rarity]() -> ECheckBoxState
				{
					const UFPSRCardDataAsset* C = WeakCard.Get();
					return (C && C->OfferRarities.Contains(Rarity)) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([this, WeakCard, Rarity](ECheckBoxState NewState)
				{
					UFPSRCardDataAsset* C = WeakCard.Get();
					if (!C)
					{
						return;
					}
					const bool bChanged = (NewState == ECheckBoxState::Checked)
						? FFPSRDataEditorHelpers::CreateCardOfferRarity(C, Rarity)
						: FFPSRDataEditorHelpers::DeleteCardOfferRarity(C, Rarity);
					if (bChanged)
					{
						TrackDirtyPackage(C->GetPackage());
					}
					// Rebuild regardless of success — this re-syncs every checkbox to the ACTUAL OfferRarities (a
					// refused delete, e.g. the card's last rarity, snaps the checkbox back to checked).
					RebuildMagnitudeGridFromCard(C);
				})
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2.0f, 0.0f, 10.0f, 0.0f)
			[
				SNew(STextBlock).Text(Label)
			];
	};

	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2.0f)
		[
			SNew(STextBlock).Text(LOCTEXT("OfferRarityToggleLabel", "오퍼 등급:"))
		]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			MakeToggle(ECardRarity::Common, LOCTEXT("Rarity_Common", "Common"))
		]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			MakeToggle(ECardRarity::Rare, LOCTEXT("Rarity_Rare", "Rare"))
		]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			MakeToggle(ECardRarity::Epic, LOCTEXT("Rarity_Epic", "Epic"))
		]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			MakeToggle(ECardRarity::Legendary, LOCTEXT("Rarity_Legendary", "Legendary"))
		];
}

TSharedRef<SWidget> SFPSRDataEditorWidget::BuildBulkOpToolbar()
{
	// Op combo options (Multiply / Add) — rebuilt each call so the toolbar can be re-instantiated per grid rebuild
	// without stale TSharedPtr<EFPSRBulkMagnitudeOp> selections pointing at a torn-down options array.
	BulkOpOptions.Reset();
	BulkOpOptions.Add(MakeShared<EFPSRBulkMagnitudeOp>(EFPSRBulkMagnitudeOp::Multiply));
	BulkOpOptions.Add(MakeShared<EFPSRBulkMagnitudeOp>(EFPSRBulkMagnitudeOp::Add));
	// Preserve the previously chosen OP VALUE (not TSharedPtr identity, which is rebuilt fresh every call) across
	// grid rebuilds, so applying a bulk op doesn't silently reset the combo back to Multiply.
	const EFPSRBulkMagnitudeOp PreviousOp = BulkSelectedOp.IsValid() ? *BulkSelectedOp : EFPSRBulkMagnitudeOp::Multiply;
	BulkSelectedOp = (PreviousOp == EFPSRBulkMagnitudeOp::Add) ? BulkOpOptions[1] : BulkOpOptions[0];

	auto GetOpLabel = [](EFPSRBulkMagnitudeOp Op) -> FText
	{
		return Op == EFPSRBulkMagnitudeOp::Multiply
			? LOCTEXT("BulkOp_Multiply", "× (곱하기)")
			: LOCTEXT("BulkOp_Add", "+ (더하기)");
	};

	// Rarity-filter combo: index 0 = nullptr sentinel = "전체" (all four rarities); the rest = one option per
	// ECardRarity. Rebuilt each call for the same reason as BulkOpOptions above. Preserve the previously chosen
	// VALUE (nullptr = all, or a specific rarity) by remapping it into the freshly-built options array.
	const TOptional<ECardRarity> PreviousRarityFilter = BulkRarityFilterSelection.IsValid() ? TOptional<ECardRarity>(*BulkRarityFilterSelection) : TOptional<ECardRarity>();
	BulkRarityFilterOptions.Reset();
	BulkRarityFilterOptions.Add(nullptr);
	BulkRarityFilterOptions.Add(MakeShared<ECardRarity>(ECardRarity::Common));
	BulkRarityFilterOptions.Add(MakeShared<ECardRarity>(ECardRarity::Rare));
	BulkRarityFilterOptions.Add(MakeShared<ECardRarity>(ECardRarity::Epic));
	BulkRarityFilterOptions.Add(MakeShared<ECardRarity>(ECardRarity::Legendary));
	BulkRarityFilterSelection = nullptr; // default = 전체 (all)
	if (PreviousRarityFilter.IsSet())
	{
		for (const TSharedPtr<ECardRarity>& Option : BulkRarityFilterOptions)
		{
			if (Option.IsValid() && *Option == PreviousRarityFilter.GetValue())
			{
				BulkRarityFilterSelection = Option;
				break;
			}
		}
	}

	auto GetRarityFilterLabel = [](const TSharedPtr<ECardRarity>& Opt) -> FText
	{
		if (!Opt.IsValid())
		{
			return LOCTEXT("BulkRarityFilter_All", "전체");
		}
		switch (*Opt)
		{
		case ECardRarity::Common: return LOCTEXT("Rarity_Common", "Common");
		case ECardRarity::Rare: return LOCTEXT("Rarity_Rare", "Rare");
		case ECardRarity::Epic: return LOCTEXT("Rarity_Epic", "Epic");
		case ECardRarity::Legendary: return LOCTEXT("Rarity_Legendary", "Legendary");
		default: return FText::GetEmpty();
		}
	};

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2.0f)
			[
				SNew(STextBlock).Text(LOCTEXT("BulkOpToolbarLabel", "일괄 연산:"))
			]

			+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
			[
				SNew(SBox).WidthOverride(120.0f)
				[
					SNew(SComboBox<TSharedPtr<EFPSRBulkMagnitudeOp>>)
					.OptionsSource(&BulkOpOptions)
					.InitiallySelectedItem(BulkSelectedOp)
					.OnGenerateWidget_Lambda([GetOpLabel](TSharedPtr<EFPSRBulkMagnitudeOp> Option)
					{
						return SNew(STextBlock).Text(Option.IsValid() ? GetOpLabel(*Option) : FText::GetEmpty());
					})
					.OnSelectionChanged_Lambda([this](TSharedPtr<EFPSRBulkMagnitudeOp> NewSelection, ESelectInfo::Type)
					{
						BulkSelectedOp = NewSelection;
					})
					[
						SNew(STextBlock).Text_Lambda([this, GetOpLabel]()
						{
							return BulkSelectedOp.IsValid() ? GetOpLabel(*BulkSelectedOp) : FText::GetEmpty();
						})
					]
				]
			]

			+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
			[
				SNew(SBox).WidthOverride(100.0f)
				[
					SNew(SNumericEntryBox<float>)
					.Value_Lambda([this]() -> TOptional<float> { return BulkOperand; })
					.OnValueChanged_Lambda([this](float NewValue) { BulkOperand = NewValue; })
					.OnValueCommitted_Lambda([this](float NewValue, ETextCommit::Type) { BulkOperand = NewValue; })
					.UndeterminedString(LOCTEXT("BulkOperandPlaceholder", "값"))
				]
			]

			+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
			[
				SNew(SBox).WidthOverride(100.0f)
				[
					SNew(SComboBox<TSharedPtr<ECardRarity>>)
					.OptionsSource(&BulkRarityFilterOptions)
					.InitiallySelectedItem(BulkRarityFilterSelection)
					.OnGenerateWidget_Lambda([GetRarityFilterLabel](TSharedPtr<ECardRarity> Option)
					{
						return SNew(STextBlock).Text(GetRarityFilterLabel(Option));
					})
					.OnSelectionChanged_Lambda([this](TSharedPtr<ECardRarity> NewSelection, ESelectInfo::Type)
					{
						BulkRarityFilterSelection = NewSelection;
					})
					[
						SNew(STextBlock).Text_Lambda([this, GetRarityFilterLabel]()
						{
							return GetRarityFilterLabel(BulkRarityFilterSelection);
						})
					]
				]
			]

			+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("BulkApplyButton", "적용"))
				.OnClicked(this, &SFPSRDataEditorWidget::OnApplyBulkClicked)
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(2.0f)
		[
			SAssignNew(BulkStatusText, STextBlock)
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

FReply SFPSRDataEditorWidget::OnApplyBulkClicked()
{
	if (!BulkStatusText.IsValid())
	{
		return FReply::Handled();
	}
	if (!BulkSelectedOp.IsValid() || !BulkOperand.IsSet())
	{
		BulkStatusText->SetText(LOCTEXT("BulkApply_NoOperand", "연산과 값을 먼저 지정하세요."));
		return FReply::Handled();
	}

	// Build the cell list from the CURRENT grid rows × {selected rarity, or all four if "전체"} — only cells whose
	// effect actually has a tier for that rarity are included (BulkApplyMagnitude also re-checks this, but filtering
	// here keeps the cell count meaningful for a mixed grid where rows cover different rarity sets).
	TArray<ECardRarity> Rarities;
	if (BulkRarityFilterSelection.IsValid())
	{
		Rarities.Add(*BulkRarityFilterSelection);
	}
	else
	{
		Rarities = { ECardRarity::Common, ECardRarity::Rare, ECardRarity::Epic, ECardRarity::Legendary };
	}

	TArray<FFPSRMagnitudeCellRef> Cells;
	TSet<TWeakObjectPtr<UFPSRCardDataAsset>> AffectedCards;
	for (const TSharedPtr<FMagnitudeGridRow>& Row : MagnitudeGridItems)
	{
		if (!Row.IsValid() || !Row->Card.IsValid() || !Row->Card->Effects.IsValidIndex(Row->EffectIndex) || !Row->Card->Effects[Row->EffectIndex])
		{
			continue;
		}
		const UFPSRCardEffect* Effect = Row->Card->Effects[Row->EffectIndex];
		for (const ECardRarity Rarity : Rarities)
		{
			const bool bHasTier = Effect->RarityTiers.ContainsByPredicate(
				[Rarity](const FFPSRCardRarityTier& T) { return T.Rarity == Rarity; });
			if (!bHasTier)
			{
				continue;
			}
			FFPSRMagnitudeCellRef Cell;
			Cell.Card = Row->Card;
			Cell.EffectIndex = Row->EffectIndex;
			Cell.Rarity = Rarity;
			Cells.Add(Cell);
			AffectedCards.Add(Row->Card);
		}
	}

	FText Status;
	const int32 NumChanged = FFPSRDataEditorHelpers::BulkApplyMagnitude(Cells, *BulkSelectedOp, BulkOperand.GetValue(), Status);

	if (NumChanged > 0)
	{
		for (const TWeakObjectPtr<UFPSRCardDataAsset>& WeakCard : AffectedCards)
		{
			if (UFPSRCardDataAsset* CardPtr = WeakCard.Get())
			{
				TrackDirtyPackage(CardPtr->GetPackage());
			}
		}
		// Rebuild whichever grid is currently shown (single card vs. whole pool) so the cells reflect the new values.
		// This tears down and re-creates BulkStatusText (BuildBulkOpToolbar is called again), so the status must be
		// applied AFTER the rebuild, not before — otherwise it'd be set on the widget about to be destroyed.
		if (UFPSRCardDataAsset* SelectedCard = Cast<UFPSRCardDataAsset>(SelectedAsset.Get()))
		{
			RebuildMagnitudeGridFromCard(SelectedCard);
		}
		else if (UFPSRCardPoolDataAsset* SelectedPool = Cast<UFPSRCardPoolDataAsset>(SelectedAsset.Get()))
		{
			RebuildMagnitudeGridFromPool(SelectedPool);
		}
	}

	if (BulkStatusText.IsValid())
	{
		BulkStatusText->SetText(Status);
	}

	return FReply::Handled();
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
		.AreaTitle(LOCTEXT("ScheduleTimelineTitle", "미션 스케줄 타임라인 (가장자리 드래그로 편집)"))
		.InitiallyCollapsed(false)
		.BodyContent()
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 2.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("ScheduleTimelineHint", "윈도우 가장자리를 드래그해 시간 조정"))
				.Font(FAppStyle::GetFontStyle("PropertyWindow.NormalFont"))
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SBox)
				.HeightOverride(140.0f)
				[
					SNew(SFPSRScheduleTimelineBar, SelectedSchedule)
					.OnEdited(FSimpleDelegate::CreateSP(this, &SFPSRDataEditorWidget::OnScheduleWindowEdited))
				]
			]
		]
	];
}

void SFPSRDataEditorWidget::OnScheduleWindowEdited()
{
	// The timeline bar reads SelectedSchedule's live MinTime/MaxTime every OnPaint, so no rebuild is needed here —
	// only dirty-tracking so "Save Modified + Rescan" picks up the drag-committed write.
	if (UFPSRRunScheduleDataAsset* Schedule = SelectedSchedule.Get())
	{
		TrackDirtyPackage(Schedule->GetPackage());
	}
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
		GuidedAddStatusText->SetText(bAdded ? LOCTEXT("AddedOk", "추가됨.") : LOCTEXT("AddedNoop", "이미 있음 — 변경 없음"));
		GuidedAddStatusText->SetColorAndOpacity(FLinearColor::White);
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
