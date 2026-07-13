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
#include "Widgets/Input/SEditableTextBox.h"
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

					+ SVerticalBox::Slot().AutoHeight().Padding(2.0f)
					[
						SNew(SHorizontalBox)

						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 4.0f, 0.0f)
						[
							SNew(STextBlock).Text(LOCTEXT("SlotLabelPrompt", "슬롯 이름:"))
						]

						+ SHorizontalBox::Slot().FillWidth(1.0f)
						[
							SNew(SEditableTextBox)
							.Text(this, &SFPSRWeaponAssemblerTab::GetSelectedSlotLabelText)
							.HintText(LOCTEXT("SlotLabelHint", "예: 조준경, 총열, 총구"))
							.IsEnabled(this, &SFPSRWeaponAssemblerTab::IsRemovePartEnabled)
							.OnTextCommitted(this, &SFPSRWeaponAssemblerTab::OnSlotLabelCommitted)
						]
					]

					+ SVerticalBox::Slot().AutoHeight().Padding(2.0f)
					[
						SNew(SButton)
						.HAlign(HAlign_Center)
						.Text(LOCTEXT("RemovePartButton", "− 선택 파츠 제거"))
						.ToolTipText(LOCTEXT("RemovePartButtonTooltip", "위에서 선택한 파츠를 무기에서 제거합니다. '조립→저장' 시 소켓도 함께 정리됩니다."))
						.IsEnabled(this, &SFPSRWeaponAssemblerTab::IsRemovePartEnabled)
						.OnClicked(this, &SFPSRWeaponAssemblerTab::OnRemovePartClicked)
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
						.OnSelectionChanged(this, &SFPSRWeaponAssemblerTab::OnAvailSelectionChanged)
						.OnMouseButtonDoubleClick(this, &SFPSRWeaponAssemblerTab::OnAvailPartActivated)
						.SelectionMode(ESelectionMode::Single)
					]

					+ SVerticalBox::Slot().AutoHeight().Padding(2.0f)
					[
						SNew(SButton)
						.HAlign(HAlign_Center)
						.Text(LOCTEXT("SwapButton", "→ 선택 파츠 교체"))
						.ToolTipText(LOCTEXT("SwapButtonTooltip", "위 '현재 파츠'에서 고른 슬롯을, 여기서 고른 메시로 교체합니다(더블클릭도 동일). '조립→저장'을 눌러야 DA에 저장됩니다."))
						.IsEnabled(this, &SFPSRWeaponAssemblerTab::IsSwapEnabled)
						.OnClicked(this, &SFPSRWeaponAssemblerTab::OnSwapClicked)
					]

					+ SVerticalBox::Slot().AutoHeight().Padding(2.0f, 0.0f, 2.0f, 2.0f)
					[
						SNew(SButton)
						.HAlign(HAlign_Center)
						.Text(LOCTEXT("AddPartButton", "＋ 파츠 추가"))
						.ToolTipText(LOCTEXT("AddPartButtonTooltip", "아래에서 고른 부착물을 무기에 '새 파츠'로 추가합니다(교체가 아니라 추가). 기즈모로 위치를 잡고 '조립→저장'을 누르세요."))
						.IsEnabled(this, &SFPSRWeaponAssemblerTab::IsAddPartEnabled)
						.OnClicked(this, &SFPSRWeaponAssemblerTab::OnAddPartClicked)
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
		TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport->GetAssemblerClient();
		Client->SetWeapon(DA);

		// Drop the preview floor to the freshly-built assembly's underside so the weapon rests on the floor instead of
		// being half-buried at the origin (engine idiom — SStaticMeshEditorViewport uses SetFloorOffset the same way).
		if (PreviewScene.IsValid())
		{
			PreviewScene->SetFloorOffset(FPSRWeaponAssemblerHelpers::ComputeFloorOffsetToRest(Client->GetBodyComp(), Client->GetPartComps()));
		}
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
				Row->Index = i;
				Row->Label = MakePartRowLabel(i);
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
	SelectedAvailPart.Reset();

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
			// The weapon's own part folder (e.g. .../Modular/Weapon_A) holds its structural variants; its sibling
			// "Attachments" folder (.../Modular/Attachments, incl. a Scopes/ subfolder) holds sights, grips, muzzle
			// devices, lasers, etc. Scan both — recursively so Attachments/Scopes is picked up too. The weapon folder
			// itself has no subfolders, so recursing it adds nothing. A missing Attachments path simply returns nothing.
			Filter.PackagePaths.Add(FName(*Folder));
			Filter.PackagePaths.Add(FName(*(FPackageName::GetLongPackagePath(Folder) / TEXT("Attachments"))));
			Filter.bRecursivePaths = true;

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
	PerformSwap(Item);
}

void SFPSRWeaponAssemblerTab::OnAvailSelectionChanged(TSharedPtr<FAvailPartRow> Item, ESelectInfo::Type SelectInfo)
{
	SelectedAvailPart = Item;
}

void SFPSRWeaponAssemblerTab::PerformSwap(TSharedPtr<FAvailPartRow> AvailItem)
{
	if (!AvailItem.IsValid())
	{
		return;
	}

	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport.IsValid() ? Viewport->GetAssemblerClient() : nullptr;
	if (!Client.IsValid())
	{
		return;
	}

	const int32 Sel = Client->GetSelectedPart();
	if (Sel == INDEX_NONE)
	{
		if (StatusText.IsValid())
		{
			StatusText->SetText(LOCTEXT("SwapNoSelection", "먼저 위 '현재 파츠'에서 교체할 슬롯을 선택하세요."));
		}
		return;
	}

	UStaticMesh* NewMesh = Cast<UStaticMesh>(AvailItem->MeshPath.TryLoad());
	if (!NewMesh)
	{
		if (StatusText.IsValid())
		{
			StatusText->SetText(FText::Format(LOCTEXT("SwapLoadFail", "메시를 불러오지 못했습니다: {0}"), AvailItem->Label));
		}
		return;
	}

	Client->SwapSelectedPartMesh(NewMesh);

	// Update ONLY the affected current-part row label, reusing the same FPartRow shared pointer so the SListView
	// selection (and thus the client's SelectedPart / gizmo target / isolate visibility) is preserved. Calling
	// RefreshPartsList() here would rebuild the rows and drop the selection — the "part vanishes to origin" bug.
	for (const TSharedPtr<FPartRow>& Row : PartRows)
	{
		if (Row.IsValid() && Row->Index == Sel)
		{
			Row->Label = MakePartRowLabel(Sel);
			break;
		}
	}
	if (PartListView.IsValid())
	{
		PartListView->RequestListRefresh();
	}

	if (StatusText.IsValid())
	{
		StatusText->SetText(FText::Format(
			LOCTEXT("SwapDone", "파츠를 {0}(으)로 교체했습니다. '조립→저장'을 눌러야 DA에 저장됩니다."),
			AvailItem->Label));
	}
}

FReply SFPSRWeaponAssemblerTab::OnSwapClicked()
{
	PerformSwap(SelectedAvailPart);
	return FReply::Handled();
}

bool SFPSRWeaponAssemblerTab::IsSwapEnabled() const
{
	const bool bHasCurrent = Viewport.IsValid() && Viewport->GetAssemblerClient().IsValid()
		&& Viewport->GetAssemblerClient()->GetSelectedPart() != INDEX_NONE;
	return bHasCurrent && SelectedAvailPart.IsValid();
}

FText SFPSRWeaponAssemblerTab::MakePartRowLabel(int32 Index) const
{
	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport.IsValid() ? Viewport->GetAssemblerClient() : nullptr;
	if (!Client.IsValid())
	{
		return FText::GetEmpty();
	}

	const TArray<UStaticMeshComponent*>& PartComps = Client->GetPartComps();
	if (!PartComps.IsValidIndex(Index) || !PartComps[Index])
	{
		return FText::GetEmpty();
	}

	// 슬롯 표시명 = 사용자 지정 DisplayLabel 우선, 비었으면 메시 유도명(컴포넌트 이름, 내부 식별용).
	UFPSRWeaponDataAsset* DA = Client->GetWeaponDA();
	FString SlotName;
	if (DA && DA->WeaponParts1P.IsValidIndex(Index) && !DA->WeaponParts1P[Index].DisplayLabel.IsEmpty())
	{
		SlotName = DA->WeaponParts1P[Index].DisplayLabel.ToString();
	}
	else
	{
		SlotName = PartComps[Index]->GetName();
	}
	const UStaticMesh* Mesh = PartComps[Index]->GetStaticMesh();
	const FString MeshName = Mesh ? Mesh->GetName() : TEXT("(없음)");
	return FText::FromString(FString::Printf(TEXT("%s  ·  %s"), *SlotName, *MeshName));
}

// ---------------------------------------------------------------------------------------------------------------
// Slot display label (DisplayLabel) editor
// ---------------------------------------------------------------------------------------------------------------

FText SFPSRWeaponAssemblerTab::GetSelectedSlotLabelText() const
{
	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport.IsValid() ? Viewport->GetAssemblerClient() : nullptr;
	UFPSRWeaponDataAsset* DA = Client.IsValid() ? Client->GetWeaponDA() : nullptr;
	const int32 Sel = Client.IsValid() ? Client->GetSelectedPart() : INDEX_NONE;
	if (!DA || !DA->WeaponParts1P.IsValidIndex(Sel))
	{
		return FText::GetEmpty();
	}
	return DA->WeaponParts1P[Sel].DisplayLabel;
}

void SFPSRWeaponAssemblerTab::OnSlotLabelCommitted(const FText& InText, ETextCommit::Type CommitType)
{
	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport.IsValid() ? Viewport->GetAssemblerClient() : nullptr;
	UFPSRWeaponDataAsset* DA = Client.IsValid() ? Client->GetWeaponDA() : nullptr;
	const int32 Sel = Client.IsValid() ? Client->GetSelectedPart() : INDEX_NONE;
	if (!DA || !DA->WeaponParts1P.IsValidIndex(Sel))
	{
		return;
	}

	DA->WeaponParts1P[Sel].DisplayLabel = InText;
	DA->MarkPackageDirty();

	// PerformSwap과 동일 패턴: 해당 행 라벨만 제자리 갱신(전체 리빌드 금지 — 선택 유실 방지).
	for (const TSharedPtr<FPartRow>& Row : PartRows)
	{
		if (Row.IsValid() && Row->Index == Sel)
		{
			Row->Label = MakePartRowLabel(Sel);
			break;
		}
	}
	if (PartListView.IsValid())
	{
		PartListView->RequestListRefresh();
	}

	if (StatusText.IsValid())
	{
		StatusText->SetText(FText::Format(
			LOCTEXT("SlotLabelDone", "슬롯 이름을 '{0}'(으)로 변경했습니다. '조립→저장'을 눌러야 저장됩니다."),
			InText));
	}
}

FReply SFPSRWeaponAssemblerTab::OnAddPartClicked()
{
	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport.IsValid() ? Viewport->GetAssemblerClient() : nullptr;
	if (!Client.IsValid() || !SelectedAvailPart.IsValid())
	{
		return FReply::Handled();
	}
	if (!Client->GetWeaponDA())
	{
		if (StatusText.IsValid())
		{
			StatusText->SetText(LOCTEXT("AddNoWeapon", "먼저 무기 DA를 선택하세요."));
		}
		return FReply::Handled();
	}

	UStaticMesh* NewMesh = Cast<UStaticMesh>(SelectedAvailPart->MeshPath.TryLoad());
	if (!NewMesh)
	{
		if (StatusText.IsValid())
		{
			StatusText->SetText(FText::Format(LOCTEXT("AddLoadFail", "메시를 불러오지 못했습니다: {0}"), SelectedAvailPart->Label));
		}
		return FReply::Handled();
	}

	Client->AddPart(NewMesh);
	RefreshPartsList();
	// Select the newly-added (last) part so the gizmo targets it immediately.
	if (PartListView.IsValid() && PartRows.Num() > 0)
	{
		PartListView->SetSelection(PartRows.Last());
	}
	// The assembly bounds changed — refit the preview floor.
	if (PreviewScene.IsValid())
	{
		PreviewScene->SetFloorOffset(FPSRWeaponAssemblerHelpers::ComputeFloorOffsetToRest(Client->GetBodyComp(), Client->GetPartComps()));
	}
	if (StatusText.IsValid())
	{
		StatusText->SetText(FText::Format(LOCTEXT("AddDone", "파츠 추가: {0}. 기즈모로 위치를 잡고 '조립→저장'을 누르세요."), SelectedAvailPart->Label));
	}
	return FReply::Handled();
}

bool SFPSRWeaponAssemblerTab::IsAddPartEnabled() const
{
	const bool bHasWeapon = Viewport.IsValid() && Viewport->GetAssemblerClient().IsValid()
		&& Viewport->GetAssemblerClient()->GetWeaponDA() != nullptr;
	return bHasWeapon && SelectedAvailPart.IsValid();
}

FReply SFPSRWeaponAssemblerTab::OnRemovePartClicked()
{
	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport.IsValid() ? Viewport->GetAssemblerClient() : nullptr;
	if (!Client.IsValid() || Client->GetSelectedPart() == INDEX_NONE)
	{
		return FReply::Handled();
	}

	Client->RemoveSelectedPart();
	RefreshPartsList();
	if (PreviewScene.IsValid())
	{
		PreviewScene->SetFloorOffset(FPSRWeaponAssemblerHelpers::ComputeFloorOffsetToRest(Client->GetBodyComp(), Client->GetPartComps()));
	}
	if (StatusText.IsValid())
	{
		StatusText->SetText(LOCTEXT("RemoveDone", "선택 파츠를 제거했습니다. '조립→저장'을 누르면 소켓도 정리됩니다."));
	}
	return FReply::Handled();
}

bool SFPSRWeaponAssemblerTab::IsRemovePartEnabled() const
{
	return Viewport.IsValid() && Viewport->GetAssemblerClient().IsValid()
		&& Viewport->GetAssemblerClient()->GetSelectedPart() != INDEX_NONE;
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
