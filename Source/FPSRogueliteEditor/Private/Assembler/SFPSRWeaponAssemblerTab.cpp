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

#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"

#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
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
				SAssignNew(PartListView, SListView<TSharedPtr<FPartRow>>)
				.ListItemsSource(&PartRows)
				.OnGenerateRow(this, &SFPSRWeaponAssemblerTab::OnGeneratePartRow)
				.OnSelectionChanged(this, &SFPSRWeaponAssemblerTab::OnPartSelectionChanged)
				.SelectionMode(ESelectionMode::Single)
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

	const int32 N = FPSRWeaponAssemblerHelpers::BakeSockets(DA, BodyMesh, Client->GetPartComps());
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

#undef LOCTEXT_NAMESPACE
