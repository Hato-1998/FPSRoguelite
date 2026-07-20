// Copyright Epic Games, Inc. All Rights Reserved.

#include "Blockout/SFPSRBlockoutTab.h"
#include "Blockout/FPSRBlockoutSettings.h"
#include "Blockout/FPSRBlockoutValidator.h"
#include "Blockout/FPSRBlockoutPlacementMode.h"
#include "Blockout/FPSRBlockoutSpawn.h"
#include "Validation/FPSRAnchoredValidationService.h"
#include "EditorModeManager.h"

// --- 프리팹 저작 (P2+P3 병합, R1서 Packed Level Actor→경량 Blueprint 하베스트로 교체: 서브레벨 없음) ------------------------
#include "Kismet2/KismetEditorUtilities.h"
#include "Selection.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetRegistry/ARFilter.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "Components/PrimitiveComponent.h"
#include "PhysicsEngine/BodySetup.h"
#include "GameFramework/Actor.h"
#include "CollisionQueryParams.h"
#include "Misc/PackageName.h"
#include "Editor.h"
#include "LevelEditorViewport.h"
#include "ScopedTransaction.h"
#include "Modules/ModuleManager.h"

#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Input/SSegmentedControl.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Views/STableRow.h"
#include "Styling/AppStyle.h"
#include "AssetThumbnail.h"

#define LOCTEXT_NAMESPACE "SFPSRBlockoutTab"

void SFPSRBlockoutTab::Construct(const FArguments& InArgs)
{
	// Slice ⑦: pooled thumbnail render targets shared by every card. FAssetThumbnailPool is an FTickableEditorObject
	// (renders itself); cards create their FAssetThumbnail lazily in OnGenerateTile.
	ThumbnailPool = MakeShared<FAssetThumbnailPool>(128);

	// Seed the toolbar grid box from the saved default (the box then drives the placement mode's live snap size).
	if (const UFPSRBlockoutSettings* Settings = GetDefault<UFPSRBlockoutSettings>())
	{
		PlacementGridSize = Settings->PlacementGridSize;
	}

	ChildSlot
	[
		SNew(SVerticalBox)

		// Header
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("Title", "FPSR 블록아웃 툴"))
			.Font(FAppStyle::Get().GetFontStyle("DetailsView.CategoryFontStyle"))
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 0.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("Subtitle", "왼쪽 폴더를 고르면 오른쪽에 그 폴더의 메시/BP가 카드로 표시됩니다. 카드 더블클릭/'선택 배치'로 배치(메시=WorldStatic, BP=as-is), '상태 검사'로 콜리전 배지(✓/✗), '레벨 검증'으로 가드레일 리포트. 팔레트 폴더는 Project Settings > FPSR > FPSR Blockout."))
			.AutoWrapText(true)
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		]

		// Refresh + Place + Inspect + Validate + search + status toolbar
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				.Text(LOCTEXT("Refresh", "새로고침"))
				.ToolTipText(LOCTEXT("RefreshTip", "Project Settings 의 팔레트 폴더 설정을 다시 읽어 자산을 재스캔합니다."))
				.OnClicked(this, &SFPSRBlockoutTab::OnRefreshClicked)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(4.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("Place", "선택 배치"))
				.ToolTipText(LOCTEXT("PlaceTip", "선택한 카드를 현재 레벨의 뷰포트 카메라 앞에 즉시 배치합니다 (메시=WorldStatic, BP=as-is; Ctrl+Z 로 취소)."))
				.IsEnabled(this, &SFPSRBlockoutTab::IsPlaceEnabled)
				.OnClicked(this, &SFPSRBlockoutTab::OnPlaceClicked)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(4.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("PlaceMode", "뷰포트 배치"))
				.ToolTipText(LOCTEXT("PlaceModeTip", "심시티식 배치 모드 진입: 뷰포트에서 커서가 바닥을 따라가며 고스트 프리뷰(격자 스냅·바닥 스냅)가 보이고, 좌클릭으로 배치·연속 배치, ESC 로 종료. 격자 크기는 오른쪽 '격자' 칸에서 바로 조절."))
				.IsEnabled(this, &SFPSRBlockoutTab::IsPlaceEnabled)
				.OnClicked(this, &SFPSRBlockoutTab::OnEnterPlacementModeClicked)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(8.0f, 0.0f, 2.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("GridLabel", "격자(cm)"))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SBox)
				.WidthOverride(70.0f)
				[
					SNew(SNumericEntryBox<float>)
					.Value(this, &SFPSRBlockoutTab::GetGridSizeValue)
					.OnValueChanged(this, &SFPSRBlockoutTab::OnGridSizeChanged)
					.MinValue(0.0f)
					.AllowSpin(true)
					.MinSliderValue(0.0f)
					.MaxSliderValue(1000.0f)
					.ToolTipText(LOCTEXT("GridTip", "뷰포트 배치 모드의 격자 스냅 크기(cm). 0 = 스냅 없음. 모드 진입 중 바꾸면 즉시 반영."))
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(4.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("Inspect", "상태 검사"))
				.ToolTipText(LOCTEXT("InspectTip", "팔레트의 모든 자산을 로드해 WorldStatic 콜리전 유무를 검사하고 카드 배지(✓/✗)를 채웁니다 (온디맨드)."))
				.OnClicked(this, &SFPSRBlockoutTab::OnInspectStatusClicked)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(4.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("Validate", "레벨 검증"))
				.ToolTipText(LOCTEXT("ValidateTip", "현재 레벨의 블록아웃 가드레일(콜리전·지면·스폰Z·볼륨·셀예산·중심)을 검사해 Message Log 에 리포트합니다."))
				.OnClicked(this, &SFPSRBlockoutTab::OnValidateClicked)
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			.Padding(8.0f, 0.0f)
			[
				SNew(SSearchBox)
				.HintText(LOCTEXT("SearchHint", "이름 검색…"))
				.OnTextChanged(this, &SFPSRBlockoutTab::OnSearchTextChanged)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SAssignNew(StatusText, STextBlock)
			]
		]

		// 프리팹 저작 툴바 (P2+P3 병합, R1서 경량 Blueprint 하베스트로 교체) — 선택 액터 → 재사용 가능한 BP_* 프리팹
		// (서브레벨 없음). 기존 툴바 additive, 배치는 기존 팔레트 경로(더블클릭/'선택 배치'/뷰포트 배치) 그대로 재사용한다
		// (RefreshPalette 가 스캔 폴더에 추가하면 끝).
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 0.0f, 8.0f, 8.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("PrefabLabel", "프리팹 이름"))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SBox)
				.WidthOverride(160.0f)
				[
					SNew(SEditableTextBox)
					.HintText(LOCTEXT("PrefabNameHint", "예: Bld_CornerA"))
					.Text(this, &SFPSRBlockoutTab::GetPendingPrefabNameText)
					.OnTextChanged(this, &SFPSRBlockoutTab::OnPendingPrefabNameChanged)
					.ToolTipText(LOCTEXT("PrefabNameTip", "새 프리팹의 이름. Project Settings 의 프리팹 저장 폴더 아래 BP_<이름> 블루프린트로 저장됩니다(서브레벨 없음)."))
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(4.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("CreatePrefab", "선택→프리팹"))
				.ToolTipText(LOCTEXT("CreatePrefabTip", "레벨에서 선택한 액터들을 하나의 재사용 가능한 경량 블루프린트(BP_*, 서브레벨 없음)로 묶습니다. 결과 BP_* 는 팔레트에 카드로 나타나 다시 배치할 수 있습니다 (Ctrl+Z 로 취소)."))
				.OnClicked(this, &SFPSRBlockoutTab::OnCreatePrefabClicked)
			]
		]

		// Two-pane browser: LEFT folder list | RIGHT asset card grid
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.Padding(8.0f)
		[
			SNew(SSplitter)
			.Orientation(Orient_Horizontal)

			+ SSplitter::Slot()
			.Value(0.28f)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				.Padding(2.0f)
				[
					SAssignNew(FolderListView, SListView<TSharedPtr<FBlockoutFolderItem>>)
					.ListItemsSource(&FolderList)
					.OnGenerateRow(this, &SFPSRBlockoutTab::OnGenerateFolderRow)
					.OnSelectionChanged(this, &SFPSRBlockoutTab::OnFolderSelectionChanged)
					.SelectionMode(ESelectionMode::Single)
				]
			]

			+ SSplitter::Slot()
			.Value(0.72f)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				.Padding(2.0f)
				[
					SNew(SVerticalBox)

					// 카테고리 필터 토글(P1b) — 배치/스폰에는 영향 없는 순수 UX 필터. 전체=무필터, 구조=Structure만,
					// 장식=Structure가 "아닌" 전부(비대칭 설계, 헤더의 EFPSRBlockoutCategoryFilter 주석 참고).
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 0.0f, 0.0f, 4.0f)
					[
						SNew(SSegmentedControl<EFPSRBlockoutCategoryFilter>)
						.Value(this, &SFPSRBlockoutTab::GetCategoryFilter)
						.OnValueChanged(this, &SFPSRBlockoutTab::OnCategoryFilterChanged)

						+ SSegmentedControl<EFPSRBlockoutCategoryFilter>::Slot(EFPSRBlockoutCategoryFilter::All)
						.Text(LOCTEXT("CategoryAll", "전체"))

						+ SSegmentedControl<EFPSRBlockoutCategoryFilter>::Slot(EFPSRBlockoutCategoryFilter::Structure)
						.Text(LOCTEXT("CategoryStructure", "구조"))

						+ SSegmentedControl<EFPSRBlockoutCategoryFilter>::Slot(EFPSRBlockoutCategoryFilter::Dressing)
						.Text(LOCTEXT("CategoryDressing", "장식"))
					]

					+ SVerticalBox::Slot()
					.FillHeight(1.0f)
					[
						SAssignNew(AssetTileView, STileView<TSharedPtr<FBlockoutAssetItem>>)
						.ListItemsSource(&CurrentFolderAssets)
						.OnGenerateTile(this, &SFPSRBlockoutTab::OnGenerateTile)
						.OnSelectionChanged(this, &SFPSRBlockoutTab::OnAssetSelectionChanged)
						.OnMouseButtonDoubleClick(this, &SFPSRBlockoutTab::OnTileDoubleClicked)
						.ItemWidth(112.0f)
						.ItemHeight(150.0f)
						.SelectionMode(ESelectionMode::Single)
					]
				]
			]
		]
	];

	RefreshPalette();
}

bool SFPSRBlockoutTab::IsBlueprintAsset(const FAssetData& AssetData)
{
	return AssetData.AssetClassPath == UBlueprint::StaticClass()->GetClassPathName();
}

bool SFPSRBlockoutTab::IsActorBlueprint(const FAssetData& AssetData)
{
	// Resolve the Blueprint's nearest NATIVE ancestor from the asset tag without loading (engine PlacementMode
	// pattern). For an actor BP that ancestor is always an AActor subclass; for a widget/anim/etc BP it is not.
	FString NativeParentPath;
	if (AssetData.GetTagValue(FBlueprintTags::NativeParentClassPath, NativeParentPath) && !NativeParentPath.IsEmpty())
	{
		const UClass* NativeParent = UClass::TryFindTypeSlow<UClass>(FPackageName::ExportTextPathToObjectPath(NativeParentPath));
		return NativeParent && NativeParent->IsChildOf(AActor::StaticClass());
	}
	return false;
}

bool SFPSRBlockoutTab::IsStructureAsset(const FAssetData& AssetData)
{
	// 카테고리 필터(P1b) 분류 휴리스틱 — 실제 태그/메타데이터가 아니라 폴더 경로·명명 관례에 의존한다. 관례를 벗어난
	// 자산(위 두 조건 모두 미해당)은 여기서 false 를 반환하고 "장식" 필터 아래로 새어나간다(비대칭 설계, 헤더 주석 참고).
	const FString PackagePathStr = AssetData.PackagePath.ToString();
	if (PackagePathStr.Contains(TEXT("/Buildings")) || PackagePathStr.Contains(TEXT("/Base")))
	{
		return true;
	}
	return AssetData.AssetName.ToString().StartsWith(TEXT("SM_Bld_"));
}

void SFPSRBlockoutTab::RefreshPalette()
{
	CachedAssets.Reset();

	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	if (AssetRegistry.IsLoadingAssets())
	{
		if (StatusText.IsValid())
		{
			StatusText->SetText(LOCTEXT("StillScanning", "에셋 레지스트리 스캔 중…"));
		}
		RebuildFolderList();
		return;
	}

	const UFPSRBlockoutSettings* Settings = GetDefault<UFPSRBlockoutSettings>();

	FARFilter Filter;
	Filter.bRecursivePaths = true;
	Filter.bRecursiveClasses = false;
	Filter.ClassPaths.Add(UStaticMesh::StaticClass()->GetClassPathName());
	Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
	for (const FDirectoryPath& Folder : Settings->PaletteFolders)
	{
		if (!Folder.Path.IsEmpty())
		{
			Filter.PackagePaths.Add(FName(*Folder.Path));
		}
	}
	// 프리팹 저장 폴더도 스캔 대상에 추가 — 새로 만든 BP_* 는 ClassPaths(UBlueprint) 필터는 통과하지만, PackagePaths
	// 필터가 PaletteFolders 로만 좁혀져 있어 그 폴더가 PaletteFolders 에 수동 등록되지 않으면 스캔 자체에서 빠진다.
	if (!Settings->PrefabSaveFolder.Path.IsEmpty())
	{
		Filter.PackagePaths.AddUnique(FName(*Settings->PrefabSaveFolder.Path));
	}

	if (Filter.PackagePaths.Num() > 0)
	{
		TArray<FAssetData> Found;
		AssetRegistry.GetAssets(Filter, Found);

		// Dedupe by object path (configured folders can overlap) + drop scratch/developer paths + drop non-actor
		// Blueprints (widget/anim/data BPs are not placeable map pieces).
		TSet<FSoftObjectPath> Seen;
		for (const FAssetData& Asset : Found)
		{
			if (FFPSRAnchoredValidationService::IsExcludedPath(Asset.PackagePath))
			{
				continue;
			}
			if (IsBlueprintAsset(Asset) && !IsActorBlueprint(Asset))
			{
				continue;
			}
			bool bAlready = false;
			Seen.Add(Asset.GetSoftObjectPath(), &bAlready);
			if (!bAlready)
			{
				CachedAssets.Add(Asset);
			}
		}
	}

	RebuildFolderList();
}

void SFPSRBlockoutTab::RebuildFolderList()
{
	const FName PrevSelectedFolder = SelectedFolderPath;

	FolderList.Reset();

	// Group the name-filtered assets by folder (a folder appears only if it has a match).
	TMap<FName, int32> CountByPath;
	int32 MeshCount = 0;
	int32 BlueprintCount = 0;
	for (const FAssetData& Asset : CachedAssets)
	{
		if (!CurrentFilter.IsEmpty() && !Asset.AssetName.ToString().Contains(CurrentFilter))
		{
			continue;
		}
		CountByPath.FindOrAdd(Asset.PackagePath)++;
		if (IsBlueprintAsset(Asset)) { ++BlueprintCount; } else { ++MeshCount; }
	}

	for (const TPair<FName, int32>& Pair : CountByPath)
	{
		TSharedPtr<FBlockoutFolderItem> FolderItem = MakeShared<FBlockoutFolderItem>();
		FolderItem->PackagePath = Pair.Key;
		FolderItem->Count = Pair.Value;
		FolderItem->Label = FText::Format(LOCTEXT("FolderRowFmt", "{0}  ({1})"), FText::FromName(Pair.Key), FText::AsNumber(Pair.Value));
		FolderList.Add(FolderItem);
	}
	FolderList.Sort([](const TSharedPtr<FBlockoutFolderItem>& A, const TSharedPtr<FBlockoutFolderItem>& B)
	{
		return A->PackagePath.LexicalLess(B->PackagePath);
	});

	if (FolderListView.IsValid())
	{
		FolderListView->RequestListRefresh();
	}

	// Re-select the previously selected folder if it survived the filter, else the first folder.
	TSharedPtr<FBlockoutFolderItem> ToSelect;
	for (const TSharedPtr<FBlockoutFolderItem>& FolderItem : FolderList)
	{
		if (FolderItem->PackagePath == PrevSelectedFolder)
		{
			ToSelect = FolderItem;
			break;
		}
	}
	if (!ToSelect.IsValid() && FolderList.Num() > 0)
	{
		ToSelect = FolderList[0];
	}

	if (ToSelect.IsValid())
	{
		SelectedFolderPath = ToSelect->PackagePath;
		if (FolderListView.IsValid())
		{
			FolderListView->SetSelection(ToSelect, ESelectInfo::Direct);
		}
		PopulateCurrentFolder();
	}
	else
	{
		SelectedFolderPath = NAME_None;
		CurrentFolderAssets.Reset();
		SelectedAsset.Reset();
		if (AssetTileView.IsValid())
		{
			AssetTileView->RequestListRefresh();
		}
	}

	if (StatusText.IsValid())
	{
		const UFPSRBlockoutSettings* Settings = GetDefault<UFPSRBlockoutSettings>();
		int32 FolderCount = 0;
		for (const FDirectoryPath& Folder : Settings->PaletteFolders)
		{
			if (!Folder.Path.IsEmpty()) { ++FolderCount; }
		}
		if (FolderCount == 0)
		{
			StatusText->SetText(LOCTEXT("NoFolders", "설정된 팔레트 폴더가 없습니다 — Project Settings > FPSR > FPSR Blockout"));
		}
		else
		{
			StatusText->SetText(FText::Format(LOCTEXT("StatusFmt", "메시 {0} · BP {1} · 폴더 {2}개"),
				FText::AsNumber(MeshCount), FText::AsNumber(BlueprintCount), FText::AsNumber(FolderList.Num())));
		}
	}
}

void SFPSRBlockoutTab::PopulateCurrentFolder()
{
	CurrentFolderAssets.Reset();
	SelectedAsset.Reset();

	for (const FAssetData& Asset : CachedAssets)
	{
		if (Asset.PackagePath != SelectedFolderPath)
		{
			continue;
		}
		if (!CurrentFilter.IsEmpty() && !Asset.AssetName.ToString().Contains(CurrentFilter))
		{
			continue;
		}
		// 카테고리 필터(P1b) — LEFT 폴더 목록/카운트는 그대로 두고 여기(오른쪽 타일 소스)에만 적용. 비대칭 설계:
		// Structure 는 Structure로 분류된 것만, Dressing 은 Structure가 "아닌" 전부(관례를 벗어난 자산도 여기서 보임).
		if (CurrentCategoryFilter != EFPSRBlockoutCategoryFilter::All)
		{
			const bool bIsStructure = IsStructureAsset(Asset);
			const bool bWantStructure = (CurrentCategoryFilter == EFPSRBlockoutCategoryFilter::Structure);
			if (bIsStructure != bWantStructure)
			{
				continue;
			}
		}
		TSharedPtr<FBlockoutAssetItem> AssetItem = MakeShared<FBlockoutAssetItem>();
		AssetItem->Asset = Asset;
		AssetItem->Label = FText::FromName(Asset.AssetName);
		CurrentFolderAssets.Add(AssetItem);
	}
	CurrentFolderAssets.Sort([](const TSharedPtr<FBlockoutAssetItem>& A, const TSharedPtr<FBlockoutAssetItem>& B)
	{
		return A->Asset.AssetName.LexicalLess(B->Asset.AssetName);
	});

	if (AssetTileView.IsValid())
	{
		AssetTileView->RequestListRefresh();
	}
}

FReply SFPSRBlockoutTab::OnRefreshClicked()
{
	RefreshPalette();
	return FReply::Handled();
}

void SFPSRBlockoutTab::OnSearchTextChanged(const FText& NewText)
{
	CurrentFilter = NewText.ToString();
	RebuildFolderList();
}

EFPSRBlockoutCategoryFilter SFPSRBlockoutTab::GetCategoryFilter() const
{
	return CurrentCategoryFilter;
}

void SFPSRBlockoutTab::OnCategoryFilterChanged(EFPSRBlockoutCategoryFilter NewFilter)
{
	CurrentCategoryFilter = NewFilter;
	// LEFT 폴더 목록은 이름검색만 반영(RebuildFolderList 미호출) — 오른쪽 타일 소스만 다시 빌드.
	PopulateCurrentFolder();
}

TSharedRef<ITableRow> SFPSRBlockoutTab::OnGenerateFolderRow(TSharedPtr<FBlockoutFolderItem> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	if (!Item.IsValid())
	{
		return SNew(STableRow<TSharedPtr<FBlockoutFolderItem>>, OwnerTable)[SNullWidget::NullWidget];
	}
	return SNew(STableRow<TSharedPtr<FBlockoutFolderItem>>, OwnerTable)
		[
			SNew(STextBlock)
			.Text(Item->Label)
			.ToolTipText(FText::FromName(Item->PackagePath))
		];
}

void SFPSRBlockoutTab::OnFolderSelectionChanged(TSharedPtr<FBlockoutFolderItem> Item, ESelectInfo::Type SelectInfo)
{
	if (Item.IsValid())
	{
		SelectedFolderPath = Item->PackagePath;
		PopulateCurrentFolder();
	}
}

TSharedRef<ITableRow> SFPSRBlockoutTab::OnGenerateTile(TSharedPtr<FBlockoutAssetItem> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	if (!Item.IsValid())
	{
		return SNew(STableRow<TSharedPtr<FBlockoutAssetItem>>, OwnerTable)[SNullWidget::NullWidget];
	}

	// Lazy large thumbnail (created on first tile-generate — only for realized cards; the pool caps render targets).
	TSharedRef<SWidget> ThumbWidget = SNullWidget::NullWidget;
	if (ThumbnailPool.IsValid())
	{
		if (!Item->Thumbnail.IsValid())
		{
			Item->Thumbnail = MakeShared<FAssetThumbnail>(Item->Asset, 96, 96, ThumbnailPool);
		}
		ThumbWidget = Item->Thumbnail->MakeThumbnailWidget();
	}

	const bool bIsBP = IsBlueprintAsset(Item->Asset);
	const FText TypeText = bIsBP ? LOCTEXT("BadgeBP", "BP") : LOCTEXT("BadgeMesh", "메시");
	const FLinearColor TypeColor = bIsBP ? FLinearColor(0.5f, 0.7f, 1.0f) : FLinearColor(0.72f, 0.72f, 0.72f);

	const EBlockoutAssetStatus Status = StatusByAsset.FindRef(Item->Asset.GetSoftObjectPath());
	FText StatusGlyph;
	FLinearColor StatusColor;
	FText StatusTip;
	switch (Status)
	{
	case EBlockoutAssetStatus::Pass:
		StatusGlyph = FText::FromString(TEXT("✓"));
		StatusColor = FLinearColor(0.35f, 0.85f, 0.35f);
		StatusTip = LOCTEXT("StatusPass", "WorldStatic 콜리전 있음 — 플로우필드가 장애물로 인식");
		break;
	case EBlockoutAssetStatus::Fail:
		StatusGlyph = FText::FromString(TEXT("✗"));
		StatusColor = FLinearColor(0.9f, 0.35f, 0.35f);
		StatusTip = LOCTEXT("StatusFail", "WorldStatic 콜리전 없음 — 배치해도 스웜을 막지 못함");
		break;
	default:
		StatusGlyph = FText::FromString(TEXT("?"));
		StatusColor = FLinearColor(0.5f, 0.5f, 0.5f);
		StatusTip = LOCTEXT("StatusUnknown", "미검사 — '상태 검사'를 눌러 콜리전 유무를 확인");
		break;
	}

	return SNew(STableRow<TSharedPtr<FBlockoutAssetItem>>, OwnerTable)
		.Padding(3.0f)
		.ToolTipText(Item->Label)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.Padding(3.0f)
			[
				SNew(SVerticalBox)

				// Thumbnail
				+ SVerticalBox::Slot()
				.AutoHeight()
				.HAlign(HAlign_Center)
				[
					SNew(SBox)
					.WidthOverride(96.0f)
					.HeightOverride(96.0f)
					[
						ThumbWidget
					]
				]

				// Type + status badge row
				+ SVerticalBox::Slot()
				.AutoHeight()
				.HAlign(HAlign_Center)
				.Padding(0.0f, 2.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(TypeText)
						.ColorAndOpacity(TypeColor)
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(4.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(STextBlock)
						.Text(StatusGlyph)
						.ColorAndOpacity(StatusColor)
						.ToolTipText(StatusTip)
					]
				]

				// Name
				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				.HAlign(HAlign_Center)
				[
					SNew(STextBlock)
					.Text(Item->Label)
					.AutoWrapText(true)
					.Justification(ETextJustify::Center)
				]
			]
		];
}

void SFPSRBlockoutTab::OnAssetSelectionChanged(TSharedPtr<FBlockoutAssetItem> Item, ESelectInfo::Type SelectInfo)
{
	SelectedAsset = Item;
}

void SFPSRBlockoutTab::OnTileDoubleClicked(TSharedPtr<FBlockoutAssetItem> Item)
{
	if (Item.IsValid() && Item->Asset.IsValid())
	{
		PlaceAsset(Item->Asset);
	}
}

FReply SFPSRBlockoutTab::OnPlaceClicked()
{
	if (SelectedAsset.IsValid() && SelectedAsset->Asset.IsValid())
	{
		PlaceAsset(SelectedAsset->Asset);
	}
	return FReply::Handled();
}

FReply SFPSRBlockoutTab::OnEnterPlacementModeClicked()
{
	if (SelectedAsset.IsValid() && SelectedAsset->Asset.IsValid())
	{
		// The level editor (and its mode manager) exists whenever this tab is interactable, so call directly (the
		// GLevelEditorModeToolsIsValid() guard is deprecated / unnecessary here).
		GLevelEditorModeTools().ActivateMode(UFPSRBlockoutPlacementMode::EM_BlockoutPlacement);
		if (UFPSRBlockoutPlacementMode* Mode = Cast<UFPSRBlockoutPlacementMode>(
				GLevelEditorModeTools().GetActiveScriptableMode(UFPSRBlockoutPlacementMode::EM_BlockoutPlacement)))
		{
			Mode->SetAssetToPlace(SelectedAsset->Asset);
			Mode->SetGridSize(PlacementGridSize);
		}
		if (StatusText.IsValid())
		{
			StatusText->SetText(LOCTEXT("EnterPlaceMode", "뷰포트 배치 모드: 커서로 바닥 지정 · 좌클릭 배치 · ESC 종료."));
		}
	}
	return FReply::Handled();
}

TOptional<float> SFPSRBlockoutTab::GetGridSizeValue() const
{
	return PlacementGridSize;
}

void SFPSRBlockoutTab::OnGridSizeChanged(float NewValue)
{
	PlacementGridSize = FMath::Max(0.0f, NewValue);
	// If the placement mode is active right now, push the new snap size to it immediately.
	if (UFPSRBlockoutPlacementMode* Mode = Cast<UFPSRBlockoutPlacementMode>(
			GLevelEditorModeTools().GetActiveScriptableMode(UFPSRBlockoutPlacementMode::EM_BlockoutPlacement)))
	{
		Mode->SetGridSize(PlacementGridSize);
	}
}

bool SFPSRBlockoutTab::IsPlaceEnabled() const
{
	return SelectedAsset.IsValid() && SelectedAsset->Asset.IsValid();
}

FReply SFPSRBlockoutTab::OnValidateClicked()
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	const int32 Findings = FFPSRBlockoutValidator::ValidateLevel(World);
	if (StatusText.IsValid())
	{
		StatusText->SetText(FText::Format(LOCTEXT("ValidateDone", "검증 완료 — 발견 {0}건 (Message Log 참조)."), FText::AsNumber(Findings)));
	}
	return FReply::Handled();
}

EBlockoutAssetStatus SFPSRBlockoutTab::InspectAssetStatus(const FAssetData& AssetData)
{
	auto IsBlockingWorldStatic = [](const UActorComponent* Comp) -> bool
	{
		if (const UPrimitiveComponent* Prim = Cast<const UPrimitiveComponent>(Comp))
		{
			return Prim->GetCollisionEnabled() != ECollisionEnabled::NoCollision && Prim->GetCollisionObjectType() == ECC_WorldStatic;
		}
		return false;
	};

	if (IsBlueprintAsset(AssetData))
	{
		const UBlueprint* BP = Cast<UBlueprint>(AssetData.GetAsset());
		if (!BP || !BP->GeneratedClass)
		{
			return EBlockoutAssetStatus::Unknown;
		}
		UClass* GenClass = BP->GeneratedClass;

		// Native default subobjects (components added in the C++ constructor live on the CDO).
		if (const AActor* CDO = GenClass->GetDefaultObject<AActor>())
		{
			for (const UActorComponent* Comp : CDO->GetComponents())
			{
				if (IsBlockingWorldStatic(Comp))
				{
					return EBlockoutAssetStatus::Pass;
				}
			}
		}

		// BP-added components live as SimpleConstructionScript templates (this class + inherited BP classes).
		for (UClass* C = GenClass; C; C = C->GetSuperClass())
		{
			if (const UBlueprintGeneratedClass* BPGC = Cast<UBlueprintGeneratedClass>(C))
			{
				if (BPGC->SimpleConstructionScript)
				{
					for (const USCS_Node* Node : BPGC->SimpleConstructionScript->GetAllNodes())
					{
						if (Node && IsBlockingWorldStatic(Node->ComponentTemplate))
						{
							return EBlockoutAssetStatus::Pass;
						}
					}
				}
			}
		}
		return EBlockoutAssetStatus::Fail;
	}

	// Static mesh: Pass if the asset carries collision the placed BlockAll profile can actually use.
	const UStaticMesh* Mesh = Cast<UStaticMesh>(AssetData.GetAsset());
	if (!Mesh)
	{
		return EBlockoutAssetStatus::Unknown;
	}
	UBodySetup* BodySetup = Mesh->GetBodySetup();
	if (!BodySetup)
	{
		return EBlockoutAssetStatus::Fail;
	}
	// Read the raw UPROPERTY member (not GetCollisionTraceFlag(), whose symbol lives in the unlinked PhysicsCore module).
	if (BodySetup->CollisionTraceFlag == CTF_UseComplexAsSimple)
	{
		return EBlockoutAssetStatus::Pass; // the render mesh itself is the collision
	}
	if (BodySetup->AggGeom.GetElementCount() > 0)
	{
		return EBlockoutAssetStatus::Pass; // authored simple collision primitives
	}
	return EBlockoutAssetStatus::Fail;
}

FReply SFPSRBlockoutTab::OnInspectStatusClicked()
{
	int32 PassCount = 0;
	int32 FailCount = 0;
	for (const FAssetData& Asset : CachedAssets)
	{
		const EBlockoutAssetStatus S = InspectAssetStatus(Asset);
		StatusByAsset.Add(Asset.GetSoftObjectPath(), S);
		if (S == EBlockoutAssetStatus::Pass) { ++PassCount; }
		else if (S == EBlockoutAssetStatus::Fail) { ++FailCount; }
	}

	if (AssetTileView.IsValid())
	{
		AssetTileView->RebuildList();
	}
	if (StatusText.IsValid())
	{
		StatusText->SetText(FText::Format(LOCTEXT("InspectDone", "상태 검사: 통과 {0} · 실패 {1} (총 {2})"),
			FText::AsNumber(PassCount), FText::AsNumber(FailCount), FText::AsNumber(CachedAssets.Num())));
	}
	return FReply::Handled();
}

FText SFPSRBlockoutTab::GetPendingPrefabNameText() const
{
	return FText::FromString(PendingPrefabName);
}

void SFPSRBlockoutTab::OnPendingPrefabNameChanged(const FText& NewText)
{
	PendingPrefabName = NewText.ToString();
}

FReply SFPSRBlockoutTab::OnCreatePrefabClicked()
{
	CreatePrefabFromSelection();
	return FReply::Handled();
}

void SFPSRBlockoutTab::CreatePrefabFromSelection()
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		if (StatusText.IsValid())
		{
			StatusText->SetText(LOCTEXT("PrefabNoWorld", "편집 가능한 에디터 월드가 없습니다 (PIE 중이면 종료 후 시도)."));
		}
		return;
	}

	TArray<AActor*> ActorsToMove;
	if (USelection* Selection = GEditor->GetSelectedActors())
	{
		Selection->GetSelectedObjects<AActor>(ActorsToMove);
	}
	if (ActorsToMove.Num() == 0)
	{
		if (StatusText.IsValid())
		{
			StatusText->SetText(LOCTEXT("PrefabNoSelection", "선택된 액터가 없습니다."));
		}
		return;
	}

	if (PendingPrefabName.IsEmpty())
	{
		if (StatusText.IsValid())
		{
			StatusText->SetText(LOCTEXT("PrefabNoName", "프리팹 이름을 입력하세요."));
		}
		return;
	}

	const UFPSRBlockoutSettings* Settings = GetDefault<UFPSRBlockoutSettings>();
	const FString BaseFolder = Settings->PrefabSaveFolder.Path.IsEmpty() ? TEXT("/Game/CityPrefabs") : Settings->PrefabSaveFolder.Path;
	const FString Path = BaseFolder / (TEXT("BP_") + PendingPrefabName);

	// R1: 사용자 결정 — 서브레벨(.umap) 없는 경량 프리팹. HarvestBlueprintFromActors 가 선택 액터들의 컴포넌트를 통짜
	// Blueprint 하나로 흡수한다(Packed Level Actor 방식 대비 서브레벨 0개). bReplaceActors=true 로 선택 액터를 즉시
	// 새 BP 인스턴스로 치환, bOpenBlueprint=false 로 BP 에디터가 모달로 뜨지 않게(비대화식 배치 워크플로 유지).
	FKismetEditorUtilities::FHarvestBlueprintFromActorsParams Params;
	Params.bReplaceActors = true;
	Params.bOpenBlueprint = false;

	const FScopedTransaction Transaction(LOCTEXT("CreatePrefabTx", "블록아웃 프리팹 생성"));
	UBlueprint* BP = FKismetEditorUtilities::HarvestBlueprintFromActors(Path, ActorsToMove, Params);
	if (!BP)
	{
		if (StatusText.IsValid())
		{
			StatusText->SetText(LOCTEXT("PrefabCreateFail", "프리팹 생성 실패."));
		}
		return;
	}

	// 새 BP_* 가 팔레트 스캔 폴더(PrefabSaveFolder)에 즉시 나타나도록 재스캔.
	RefreshPalette();

	if (StatusText.IsValid())
	{
		StatusText->SetText(FText::Format(LOCTEXT("PrefabCreated", "프리팹 생성: {0}"), FText::FromString(PendingPrefabName)));
	}
}

void SFPSRBlockoutTab::PlaceAsset(const FAssetData& AssetData)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		if (StatusText.IsValid())
		{
			StatusText->SetText(LOCTEXT("NoWorld", "편집 가능한 에디터 월드가 없습니다 (PIE 중이면 종료 후 시도)."));
		}
		return;
	}

	const FVector SpawnLocation = ComputeSpawnLocation();

	// --- Actor Blueprint: spawn AS-IS from its generated class (no collision auto-mod — Codex P1). No mesh-bounds
	//     overlap preflight (BP bounds aren't known pre-spawn). --------------------------------------------------------
	if (IsBlueprintAsset(AssetData))
	{
		UBlueprint* BP = Cast<UBlueprint>(AssetData.GetAsset());
		if (!BP || !BP->GeneratedClass || !BP->GeneratedClass->IsChildOf(AActor::StaticClass()))
		{
			if (StatusText.IsValid())
			{
				StatusText->SetText(FText::Format(LOCTEXT("BPLoadFail", "액터 BP 로드 실패: {0}"), FText::FromName(AssetData.AssetName)));
			}
			return;
		}

		const FScopedTransaction Transaction(LOCTEXT("PlaceBPTx", "블록아웃 BP 배치"));
		AActor* NewActor = FFPSRBlockoutSpawn::SpawnPiece(World, AssetData, FTransform(FRotator::ZeroRotator, SpawnLocation), /*bTransientGhost=*/false);
		if (!NewActor)
		{
			if (StatusText.IsValid())
			{
				StatusText->SetText(LOCTEXT("SpawnFail", "액터 스폰 실패."));
			}
			return;
		}

		GEditor->SelectNone(/*bNoteSelectionChange=*/false, /*bDeselectBSPSurfs=*/true);
		GEditor->SelectActor(NewActor, /*bInSelected=*/true, /*bNotify=*/true);

		if (StatusText.IsValid())
		{
			StatusText->SetText(FText::Format(LOCTEXT("PlacedBP", "배치(BP·as-is): {0}"), FText::FromString(NewActor->GetActorLabel())));
		}
		return;
	}

	// --- Static mesh: WorldStatic "BlockAll" AStaticMeshActor with mesh-bounds overlap preflight ---------------------
	UStaticMesh* Mesh = Cast<UStaticMesh>(AssetData.GetAsset());
	if (!Mesh)
	{
		if (StatusText.IsValid())
		{
			StatusText->SetText(FText::Format(LOCTEXT("LoadFail", "메시 로드 실패: {0}"), FText::FromName(AssetData.AssetName)));
		}
		return;
	}

	// Overlap guard (preflight, not a viewport mode): don't drop a piece where it would overlap an ALREADY-placed
	// blockout piece (a "Blockout"-folder StaticMeshActor). Box test against ECC_WorldStatic at the spawn pose using the
	// mesh's own bounds (shrunk 5% so flush-edge placement is allowed). Floor / vendor geometry are ignored — only
	// tool-placed pieces count, and only at drop time (moving a placed piece into overlap is free).
	{
		const FBox LocalBox = Mesh->GetBoundingBox();
		const FVector BoxCenterWorld = SpawnLocation + LocalBox.GetCenter();
		const FVector BoxExtent = LocalBox.GetExtent() * 0.95f;
		TArray<FOverlapResult> Overlaps;
		FCollisionQueryParams OverlapParams(SCENE_QUERY_STAT(FPSRBlockoutPlaceOverlap), /*bTraceComplex=*/false);
		if (World->OverlapMultiByChannel(Overlaps, BoxCenterWorld, FQuat::Identity, ECC_WorldStatic, FCollisionShape::MakeBox(BoxExtent), OverlapParams))
		{
			for (const FOverlapResult& Overlap : Overlaps)
			{
				AActor* OtherActor = Overlap.GetActor();
				if (OtherActor && OtherActor->IsA<AStaticMeshActor>() && OtherActor->GetFolderPath().ToString().StartsWith(TEXT("Blockout")))
				{
					if (StatusText.IsValid())
					{
						StatusText->SetText(FText::Format(LOCTEXT("Overlap", "겹침: {0}이(가) 이미 배치된 '{1}'과(와) 겹쳐 배치를 취소했습니다."),
							FText::FromName(AssetData.AssetName), FText::FromString(OtherActor->GetActorLabel())));
					}
					return;
				}
			}
		}
	}

	const FScopedTransaction Transaction(LOCTEXT("PlaceMeshTx", "블록아웃 메시 배치"));
	AActor* NewActor = FFPSRBlockoutSpawn::SpawnPiece(World, AssetData, FTransform(FRotator::ZeroRotator, SpawnLocation), /*bTransientGhost=*/false);
	if (!NewActor)
	{
		if (StatusText.IsValid())
		{
			StatusText->SetText(LOCTEXT("SpawnFail", "액터 스폰 실패."));
		}
		return;
	}

	GEditor->SelectNone(/*bNoteSelectionChange=*/false, /*bDeselectBSPSurfs=*/true);
	GEditor->SelectActor(NewActor, /*bInSelected=*/true, /*bNotify=*/true);

	if (StatusText.IsValid())
	{
		StatusText->SetText(FText::Format(LOCTEXT("Placed", "배치(WorldStatic): {0}  @ ({1}, {2}, {3})"),
			FText::FromString(NewActor->GetActorLabel()),
			FText::AsNumber(FMath::RoundToInt(SpawnLocation.X)),
			FText::AsNumber(FMath::RoundToInt(SpawnLocation.Y)),
			FText::AsNumber(FMath::RoundToInt(SpawnLocation.Z))));
	}
}

FVector SFPSRBlockoutTab::ComputeSpawnLocation() const
{
	if (GCurrentLevelEditingViewportClient)
	{
		const FVector ViewLoc = GCurrentLevelEditingViewportClient->GetViewLocation();
		const FRotator ViewRot = GCurrentLevelEditingViewportClient->GetViewRotation();
		return ViewLoc + ViewRot.Vector() * 500.0f;
	}
	return FVector::ZeroVector;
}

#undef LOCTEXT_NAMESPACE
