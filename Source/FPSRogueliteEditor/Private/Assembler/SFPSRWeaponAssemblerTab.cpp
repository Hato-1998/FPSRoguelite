// Copyright Epic Games, Inc. All Rights Reserved.

#include "Assembler/SFPSRWeaponAssemblerTab.h"

#include "Assembler/SFPSRWeaponAssemblerViewport.h"
#include "Assembler/FPSRWeaponAssemblerViewportClient.h"
#include "Assembler/FPSRWeaponAssemblerHelpers.h"
#include "Weapon/FPSRWeaponDataAsset.h"

#include "AdvancedPreviewScene.h"
#include "PreviewScene.h"
#include "PropertyCustomizationHelpers.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/ARFilter.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"

#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"

#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Layout/SSplitter.h"

#define LOCTEXT_NAMESPACE "SFPSRWeaponAssemblerTab"

void SFPSRWeaponAssemblerTab::Construct(const FArguments& InArgs)
{
	// Own preview scene — the whole point of this rewrite is that nothing gets spawned into an editor level anymore.
	PreviewScene = MakeShared<FAdvancedPreviewScene>(FPreviewScene::ConstructionValues());

	ChildSlot
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot().AutoHeight().Padding(4.0f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				SNew(SObjectPropertyEntryBox)
				.AllowedClass(UFPSRWeaponDataAsset::StaticClass())
				.ObjectPath(this, &SFPSRWeaponAssemblerTab::GetWeaponObjectPath)
				.OnObjectChanged(this, &SFPSRWeaponAssemblerTab::OnWeaponAssetChanged)
				.AllowClear(true)
			]

			+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("TranslateMode", "이동"))
				.ToolTipText(LOCTEXT("TranslateModeTooltip", "기즈모를 이동 모드로 전환합니다."))
				.OnClicked(this, &SFPSRWeaponAssemblerTab::OnTranslateModeClicked)
			]

			+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("RotateMode", "회전"))
				.ToolTipText(LOCTEXT("RotateModeTooltip", "기즈모를 회전 모드로 전환합니다."))
				.OnClicked(this, &SFPSRWeaponAssemblerTab::OnRotateModeClicked)
			]

			+ SHorizontalBox::Slot().AutoWidth().Padding(8.0f, 0.0f, 2.0f, 0.0f).VAlign(VAlign_Center)
			[
				SNew(SCheckBox)
				.IsChecked(this, &SFPSRWeaponAssemblerTab::IsMoveAllChecked)
				.OnCheckStateChanged(this, &SFPSRWeaponAssemblerTab::OnMoveAllChanged)
				.ToolTipText(LOCTEXT("MoveAllTooltip", "켜면 선택 파츠가 아니라 모든 파츠가 함께 이동/회전합니다(회전은 전체 파츠 위치의 평균을 기준으로 돕니다)."))
				[
					SNew(STextBlock).Text(LOCTEXT("MoveAll", "전체 이동"))
				]
			]

			+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f, 0.0f).VAlign(VAlign_Center)
			[
				SNew(SCheckBox)
				.IsChecked(this, &SFPSRWeaponAssemblerTab::IsIsolateChecked)
				.OnCheckStateChanged(this, &SFPSRWeaponAssemblerTab::OnIsolateChanged)
				.ToolTipText(LOCTEXT("IsolateTooltip", "켜면 왼쪽에서 선택한 파츠만 보이고 나머지 파츠는 숨겨집니다(바디는 항상 보임)."))
				[
					SNew(STextBlock).Text(LOCTEXT("Isolate", "선택만 보기"))
				]
			]

			+ SHorizontalBox::Slot().AutoWidth().Padding(8.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("Bake", "조립→저장"))
				.ToolTipText(LOCTEXT("BakeTooltip", "현재 파츠 배치를 바디 메시 소켓으로 굽고 무기 DA에 배선한 뒤 저장합니다."))
				.OnClicked(this, &SFPSRWeaponAssemblerTab::OnBakeClicked)
			]
		]

		+ SVerticalBox::Slot().AutoHeight().Padding(4.0f, 0.0f, 4.0f, 4.0f)
		[
			SAssignNew(StatusText, STextBlock)
			.Text(FText::GetEmpty())
		]

		+ SVerticalBox::Slot().FillHeight(1.0f)
		[
			SNew(SSplitter)
			.Orientation(Orient_Horizontal)

			+ SSplitter::Slot()
			.Value(0.22f)
			[
				SNew(SSplitter)
				.Orientation(Orient_Vertical)

				+ SSplitter::Slot()
				.Value(0.5f)
				[
					SNew(SVerticalBox)

					+ SVerticalBox::Slot().AutoHeight().Padding(2.0f)
					[
						SNew(STextBlock).Text(LOCTEXT("CurrentPartsHeader", "현재 파츠"))
					]

					+ SVerticalBox::Slot().FillHeight(1.0f)
					[
						SAssignNew(PartListView, SListView<TSharedPtr<FPartRow>>)
						.ListItemsSource(&PartRows)
						.OnGenerateRow(this, &SFPSRWeaponAssemblerTab::OnGeneratePartRow)
						.OnSelectionChanged(this, &SFPSRWeaponAssemblerTab::OnPartSelectionChanged)
						.SelectionMode(ESelectionMode::Single)
					]
				]

				+ SSplitter::Slot()
				.Value(0.5f)
				[
					SNew(SVerticalBox)

					+ SVerticalBox::Slot().AutoHeight().Padding(2.0f)
					[
						SNew(STextBlock).Text(LOCTEXT("AvailablePartsHeader", "사용 가능 파츠(교체)"))
					]

					+ SVerticalBox::Slot().FillHeight(1.0f)
					[
						SAssignNew(AvailPartListView, SListView<TSharedPtr<FAvailPartRow>>)
						.ListItemsSource(&AvailPartRows)
						.OnGenerateRow(this, &SFPSRWeaponAssemblerTab::OnGenerateAvailRow)
						.OnMouseButtonDoubleClick(this, &SFPSRWeaponAssemblerTab::OnAvailPartActivated)
						.SelectionMode(ESelectionMode::Single)
					]
				]
			]

			+ SSplitter::Slot()
			.Value(0.78f)
			[
				SAssignNew(Viewport, SFPSRWeaponAssemblerViewport, PreviewScene.ToSharedRef())
			]
		]
	];
}

// ---------------------------------------------------------------------------------------------------------------
// Weapon DA picker
// ---------------------------------------------------------------------------------------------------------------

FString SFPSRWeaponAssemblerTab::GetWeaponObjectPath() const
{
	if (Viewport.IsValid() && Viewport->GetAssemblerClient().IsValid())
	{
		if (UFPSRWeaponDataAsset* DA = Viewport->GetAssemblerClient()->GetWeaponDA())
		{
			return DA->GetPathName();
		}
	}
	return FString();
}

void SFPSRWeaponAssemblerTab::OnWeaponAssetChanged(const FAssetData& AssetData)
{
	UFPSRWeaponDataAsset* DA = Cast<UFPSRWeaponDataAsset>(AssetData.GetAsset());
	if (Viewport.IsValid() && Viewport->GetAssemblerClient().IsValid())
	{
		Viewport->GetAssemblerClient()->SetWeapon(DA);
	}

	RefreshPartsList();
	RefreshAvailableParts();

	if (StatusText.IsValid())
	{
		StatusText->SetText(FText::GetEmpty());
	}
}

// ---------------------------------------------------------------------------------------------------------------
// Parts list
// ---------------------------------------------------------------------------------------------------------------

void SFPSRWeaponAssemblerTab::RefreshPartsList()
{
	PartRows.Reset();

	if (Viewport.IsValid() && Viewport->GetAssemblerClient().IsValid())
	{
		const TArray<UStaticMeshComponent*>& PartComps = Viewport->GetAssemblerClient()->GetPartComps();
		for (int32 i = 0; i < PartComps.Num(); ++i)
		{
			if (PartComps[i])
			{
				TSharedPtr<FPartRow> Row = MakeShared<FPartRow>();
				Row->Label = FText::FromString(PartComps[i]->GetName());
				Row->Index = i;
				PartRows.Add(Row);
			}
		}
	}

	if (PartListView.IsValid())
	{
		PartListView->RequestListRefresh();
	}
}

TSharedRef<ITableRow> SFPSRWeaponAssemblerTab::OnGeneratePartRow(TSharedPtr<FPartRow> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(STableRow<TSharedPtr<FPartRow>>, OwnerTable)
		[
			SNew(STextBlock)
			.Text(Item.IsValid() ? Item->Label : FText::GetEmpty())
		];
}

void SFPSRWeaponAssemblerTab::OnPartSelectionChanged(TSharedPtr<FPartRow> Item, ESelectInfo::Type SelectInfo)
{
	if (Viewport.IsValid() && Viewport->GetAssemblerClient().IsValid())
	{
		Viewport->GetAssemblerClient()->SetSelectedPart(Item.IsValid() ? Item->Index : INDEX_NONE);
	}
}

// ---------------------------------------------------------------------------------------------------------------
// Available parts catalog
// ---------------------------------------------------------------------------------------------------------------

void SFPSRWeaponAssemblerTab::RefreshAvailableParts()
{
	AvailPartRows.Reset();

	UFPSRWeaponDataAsset* DA = (Viewport.IsValid() && Viewport->GetAssemblerClient().IsValid())
		? Viewport->GetAssemblerClient()->GetWeaponDA()
		: nullptr;

	if (DA)
	{
		// Every modular variant of this weapon's parts lives alongside the wired ones in the same content folder
		// (e.g. all of /Game/.../Weapon_A) — so the first non-null part's folder locates the whole catalog.
		FString Folder;
		for (const FFPSRWeaponPartAttachment& Part : DA->WeaponParts1P)
		{
			if (!Part.Part.IsNull())
			{
				Folder = FPackageName::GetLongPackagePath(Part.Part.GetLongPackageName());
				break;
			}
		}

		if (!Folder.IsEmpty())
		{
			IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

			FARFilter Filter;
			Filter.ClassPaths.Add(UStaticMesh::StaticClass()->GetClassPathName());
			Filter.PackagePaths.Add(FName(*Folder));
			Filter.bRecursivePaths = false;

			TArray<FAssetData> FoundAssets;
			AssetRegistry.GetAssets(Filter, FoundAssets);

			for (const FAssetData& Found : FoundAssets)
			{
				TSharedPtr<FAvailPartRow> Row = MakeShared<FAvailPartRow>();
				Row->Label = FText::FromName(Found.AssetName);
				Row->MeshPath = Found.GetSoftObjectPath();
				AvailPartRows.Add(Row);
			}

			AvailPartRows.Sort([](const TSharedPtr<FAvailPartRow>& A, const TSharedPtr<FAvailPartRow>& B)
			{
				return A->Label.CompareTo(B->Label) < 0;
			});
		}
	}

	if (AvailPartListView.IsValid())
	{
		AvailPartListView->RequestListRefresh();
	}
}

TSharedRef<ITableRow> SFPSRWeaponAssemblerTab::OnGenerateAvailRow(TSharedPtr<FAvailPartRow> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(STableRow<TSharedPtr<FAvailPartRow>>, OwnerTable)
		[
			SNew(STextBlock)
			.Text(Item.IsValid() ? Item->Label : FText::GetEmpty())
		];
}

void SFPSRWeaponAssemblerTab::OnAvailPartActivated(TSharedPtr<FAvailPartRow> Item)
{
	if (!Item.IsValid())
	{
		return;
	}

	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport.IsValid() ? Viewport->GetAssemblerClient() : nullptr;
	if (!Client.IsValid() || Client->GetSelectedPart() == INDEX_NONE)
	{
		if (StatusText.IsValid())
		{
			StatusText->SetText(LOCTEXT("SwapNoSelection", "먼저 좌측에서 교체할 파츠를 선택하세요."));
		}
		return;
	}

	Client->SwapSelectedPartMesh(Cast<UStaticMesh>(Item->MeshPath.TryLoad()));
	RefreshPartsList();

	if (StatusText.IsValid())
	{
		StatusText->SetText(LOCTEXT("SwapDone", "파츠 교체됨"));
	}
}

// ---------------------------------------------------------------------------------------------------------------
// Toolbar
// ---------------------------------------------------------------------------------------------------------------

FReply SFPSRWeaponAssemblerTab::OnBakeClicked()
{
	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport.IsValid() ? Viewport->GetAssemblerClient() : nullptr;
	UFPSRWeaponDataAsset* DA = Client.IsValid() ? Client->GetWeaponDA() : nullptr;
	if (!DA)
	{
		if (StatusText.IsValid())
		{
			StatusText->SetText(LOCTEXT("BakeNoWeapon", "먼저 무기 DA를 선택하세요."));
		}
		return FReply::Handled();
	}

	USkeletalMeshComponent* BodyComp = Client->GetBodyComp();
	USkeletalMesh* BodyMesh = BodyComp ? BodyComp->GetSkeletalMeshAsset() : nullptr;
	if (!BodyMesh)
	{
		if (StatusText.IsValid())
		{
			StatusText->SetText(LOCTEXT("BakeNoBodyMesh", "이 무기 DA에 1인칭 스켈레탈 메시(WeaponMesh1P)가 없어 소켓을 구울 수 없습니다."));
		}
		return FReply::Handled();
	}

	const int32 N = FPSRWeaponAssemblerHelpers::BakeSockets(DA, BodyComp, Client->GetPartComps());
	if (StatusText.IsValid())
	{
		StatusText->SetText(FText::Format(LOCTEXT("BakeDone", "소켓 {0}개 생성/갱신 + DA 배선·저장 완료."), FText::AsNumber(N)));
	}
	return FReply::Handled();
}

FReply SFPSRWeaponAssemblerTab::OnTranslateModeClicked()
{
	if (Viewport.IsValid() && Viewport->GetAssemblerClient().IsValid())
	{
		Viewport->GetAssemblerClient()->SetWidgetMode(UE::Widget::WM_Translate);
	}
	return FReply::Handled();
}

FReply SFPSRWeaponAssemblerTab::OnRotateModeClicked()
{
	if (Viewport.IsValid() && Viewport->GetAssemblerClient().IsValid())
	{
		Viewport->GetAssemblerClient()->SetWidgetMode(UE::Widget::WM_Rotate);
	}
	return FReply::Handled();
}

void SFPSRWeaponAssemblerTab::OnMoveAllChanged(ECheckBoxState NewState)
{
	if (Viewport.IsValid() && Viewport->GetAssemblerClient().IsValid())
	{
		Viewport->GetAssemblerClient()->SetMoveAll(NewState == ECheckBoxState::Checked);
	}
}

void SFPSRWeaponAssemblerTab::OnIsolateChanged(ECheckBoxState NewState)
{
	if (Viewport.IsValid() && Viewport->GetAssemblerClient().IsValid())
	{
		Viewport->GetAssemblerClient()->SetIsolate(NewState == ECheckBoxState::Checked);
	}
}

ECheckBoxState SFPSRWeaponAssemblerTab::IsMoveAllChecked() const
{
	const bool bChecked = Viewport.IsValid() && Viewport->GetAssemblerClient().IsValid() && Viewport->GetAssemblerClient()->IsMoveAll();
	return bChecked ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

ECheckBoxState SFPSRWeaponAssemblerTab::IsIsolateChecked() const
{
	const bool bChecked = Viewport.IsValid() && Viewport->GetAssemblerClient().IsValid() && Viewport->GetAssemblerClient()->IsIsolate();
	return bChecked ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

#undef LOCTEXT_NAMESPACE
