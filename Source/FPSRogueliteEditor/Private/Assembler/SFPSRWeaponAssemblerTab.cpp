// Copyright Epic Games, Inc. All Rights Reserved.

#include "Assembler/SFPSRWeaponAssemblerTab.h"

#include "Assembler/SFPSRWeaponAssemblerViewport.h"
#include "Assembler/FPSRWeaponAssemblerViewportClient.h"
#include "Assembler/FPSRWeaponAssemblerHelpers.h"
#include "Weapon/FPSRWeaponDataAsset.h"
#include "Weapon/FPSRWeaponFragment.h"

#include "AdvancedPreviewScene.h"
#include "PreviewScene.h"
#include "PropertyCustomizationHelpers.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/ARFilter.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"

#include "Blueprint/UserWidget.h"
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
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Layout/SSplitter.h"
#include "SEnumCombo.h" // SEnumComboBox — 진화 단계 트리거/스탯 콤보 (EditorWidgets 모듈, 헤더는 SEnumCombo.h)

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
				.Value(0.33f)
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
				.Value(0.34f)
				[
					SNew(SVerticalBox)

					+ SVerticalBox::Slot().AutoHeight().Padding(2.0f)
					[
						SNew(STextBlock).Text(LOCTEXT("EvolutionHeader", "진화 (선택 슬롯)"))
					]

					+ SVerticalBox::Slot().AutoHeight().Padding(2.0f)
					[
						SNew(SHorizontalBox)

						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 4.0f, 0.0f)
						[
							SNew(STextBlock).Text(LOCTEXT("EvolutionFragmentPrompt", "진화 카드:"))
						]

						+ SHorizontalBox::Slot().FillWidth(1.0f)
						[
							SNew(SObjectPropertyEntryBox)
							.AllowedClass(UFPSRWeaponFragment::StaticClass())
							.ObjectPath(this, &SFPSRWeaponAssemblerTab::GetEvolutionFragmentPath)
							.OnObjectChanged(this, &SFPSRWeaponAssemblerTab::OnEvolutionFragmentChanged)
							.AllowClear(true)
							.IsEnabled(this, &SFPSRWeaponAssemblerTab::IsRemovePartEnabled)
						]
					]

					+ SVerticalBox::Slot().AutoHeight().Padding(2.0f, 4.0f, 2.0f, 2.0f)
					[
						SNew(STextBlock).Text(LOCTEXT("StageListHeader", "진화 단계 (아래일수록 우선 · 조건 충족 시 가장 아래 단계 적용):"))
					]

					+ SVerticalBox::Slot().FillHeight(1.0f)
					[
						SAssignNew(StageListView, SListView<TSharedPtr<FStageRow>>)
						.ListItemsSource(&StageRows)
						.OnGenerateRow(this, &SFPSRWeaponAssemblerTab::OnGenerateStageRow)
						.OnSelectionChanged(this, &SFPSRWeaponAssemblerTab::OnStageSelectionChanged)
						.SelectionMode(ESelectionMode::Single)
					]

					+ SVerticalBox::Slot().AutoHeight().Padding(2.0f, 0.0f, 2.0f, 2.0f)
					[
						SNew(SHorizontalBox)

						+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0.0f, 0.0f, 1.0f, 0.0f)
						[
							SNew(SButton)
							.HAlign(HAlign_Center)
							.Text(LOCTEXT("StageMoveUpButton", "▲ 위로"))
							.ToolTipText(LOCTEXT("StageMoveUpButtonTooltip", "선택한 단계를 목록에서 한 칸 위(우선순위 낮음)로 옮깁니다."))
							.IsEnabled(this, &SFPSRWeaponAssemblerTab::IsStageMoveUpEnabled)
							.OnClicked(this, &SFPSRWeaponAssemblerTab::OnStageMoveUpClicked)
						]

						+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(1.0f, 0.0f, 0.0f, 0.0f)
						[
							SNew(SButton)
							.HAlign(HAlign_Center)
							.Text(LOCTEXT("StageMoveDownButton", "▼ 아래로"))
							.ToolTipText(LOCTEXT("StageMoveDownButtonTooltip", "선택한 단계를 목록에서 한 칸 아래(우선순위 높음)로 옮깁니다."))
							.IsEnabled(this, &SFPSRWeaponAssemblerTab::IsStageMoveDownEnabled)
							.OnClicked(this, &SFPSRWeaponAssemblerTab::OnStageMoveDownClicked)
						]
					]

					+ SVerticalBox::Slot().AutoHeight().Padding(2.0f)
					[
						SNew(SVerticalBox)
						.IsEnabled(this, &SFPSRWeaponAssemblerTab::IsStageSelected)

						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 1.0f)
						[
							SNew(STextBlock).Text(LOCTEXT("SelectedStageHeader", "선택 단계:"))
						]

						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 1.0f)
						[
							SNew(SHorizontalBox)

							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 4.0f, 0.0f)
							[
								SNew(STextBlock).Text(LOCTEXT("StageTriggerPrompt", "트리거 종류:"))
							]

							+ SHorizontalBox::Slot().FillWidth(1.0f)
							[
								SNew(SEnumComboBox, StaticEnum<EFPSRPartStageTrigger>())
								.CurrentValue(this, &SFPSRWeaponAssemblerTab::GetSelectedStageTriggerValue)
								.OnEnumSelectionChanged(this, &SFPSRWeaponAssemblerTab::OnSelectedStageTriggerChanged)
							]
						]

						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 1.0f)
						[
							SNew(SHorizontalBox)
							.Visibility(this, &SFPSRWeaponAssemblerTab::GetStackFieldVisibility)

							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 4.0f, 0.0f)
							[
								SNew(STextBlock).Text(LOCTEXT("StageStacksFieldPrompt", "필요 스택:"))
							]

							+ SHorizontalBox::Slot().FillWidth(1.0f)
							[
								SNew(SSpinBox<int32>)
								.MinValue(1)
								.MinSliderValue(1)
								.MaxSliderValue(10)
								.Value_Lambda([this]() { return GetStageMinStacks(GetSelectedStageIndex()); })
								.OnValueChanged_Lambda([this](int32 NewValue) { OnStageMinStacksChanged(NewValue, GetSelectedStageIndex()); })
							]
						]

						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 1.0f)
						[
							SNew(SVerticalBox)
							.Visibility(this, &SFPSRWeaponAssemblerTab::GetStatFieldVisibility)

							+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 1.0f)
							[
								SNew(SHorizontalBox)

								+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 4.0f, 0.0f)
								[
									SNew(STextBlock).Text(LOCTEXT("StageStatAxisPrompt", "스탯 축:"))
								]

								+ SHorizontalBox::Slot().FillWidth(1.0f)
								[
									SNew(SEnumComboBox, StaticEnum<EFPSRWeaponStat>())
									.CurrentValue(this, &SFPSRWeaponAssemblerTab::GetSelectedStageStatAxisValue)
									.OnEnumSelectionChanged(this, &SFPSRWeaponAssemblerTab::OnSelectedStageStatAxisChanged)
								]
							]

							+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 1.0f)
							[
								SNew(SHorizontalBox)

								+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 4.0f, 0.0f)
								[
									SNew(STextBlock).Text(LOCTEXT("StageStatComparePrompt", "비교:"))
								]

								+ SHorizontalBox::Slot().FillWidth(1.0f)
								[
									SNew(SEnumComboBox, StaticEnum<EFPSRStatCompare>())
									.CurrentValue(this, &SFPSRWeaponAssemblerTab::GetSelectedStageStatCompareValue)
									.OnEnumSelectionChanged(this, &SFPSRWeaponAssemblerTab::OnSelectedStageStatCompareChanged)
								]
							]

							+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 1.0f)
							[
								SNew(SHorizontalBox)

								+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 4.0f, 0.0f)
								[
									SNew(STextBlock).Text(LOCTEXT("StageStatValuePrompt", "기준값:"))
								]

								+ SHorizontalBox::Slot().FillWidth(1.0f)
								[
									SNew(SNumericEntryBox<float>)
									.AllowSpin(true)
									.MinSliderValue(0.0f)
									.MaxSliderValue(100.0f)
									.Value(this, &SFPSRWeaponAssemblerTab::GetSelectedStageStatValue)
									.OnValueChanged(this, &SFPSRWeaponAssemblerTab::OnSelectedStageStatValueChanged)
								]
							]
						]

						// --- 스코프(사이트 단계): 트리거 종류와 무관하게 항상(선택 단계 있으면) 표시 -----------------------------
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 1.0f)
						[
							SNew(STextBlock).Text(LOCTEXT("StageScopeHeader", "스코프 (사이트 단계)"))
						]

						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 1.0f)
						[
							SNew(SCheckBox)
							.IsChecked(this, &SFPSRWeaponAssemblerTab::GetSelectedStageScopeOverlay)
							.OnCheckStateChanged(this, &SFPSRWeaponAssemblerTab::OnSelectedStageScopeOverlayChanged)
							[
								SNew(STextBlock).Text(LOCTEXT("StageScopeOverlayLabel", "스코프 오버레이 사용"))
							]
						]

						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 1.0f)
						[
							SNew(SHorizontalBox)

							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 4.0f, 0.0f)
							[
								SNew(STextBlock).Text(LOCTEXT("StageAimFOVPrompt", "조준 배율 FOV(도, 0=무기 기본):"))
							]

							+ SHorizontalBox::Slot().FillWidth(1.0f)
							[
								SNew(SNumericEntryBox<float>)
								.AllowSpin(true)
								.MinSliderValue(0.0f)
								.MaxSliderValue(120.0f)
								.Value(this, &SFPSRWeaponAssemblerTab::GetSelectedStageAimFOV)
								.OnValueChanged(this, &SFPSRWeaponAssemblerTab::OnSelectedStageAimFOVChanged)
							]
						]

						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 1.0f)
						[
							SNew(SVerticalBox)
							.Visibility(this, &SFPSRWeaponAssemblerTab::GetScopeOverlaySubFieldVisibility)

							+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 1.0f)
							[
								SNew(SHorizontalBox)

								+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 4.0f, 0.0f)
								[
									SNew(STextBlock).Text(LOCTEXT("StageScopeWidgetPrompt", "스코프 위젯(WBP):"))
								]

								+ SHorizontalBox::Slot().FillWidth(1.0f)
								[
									SNew(SClassPropertyEntryBox)
									.MetaClass(UUserWidget::StaticClass())
									.AllowAbstract(false)
									.IsBlueprintBaseOnly(true)
									.AllowNone(true)
									.SelectedClass(this, &SFPSRWeaponAssemblerTab::GetSelectedStageScopeWidgetClass)
									.OnSetClass(this, &SFPSRWeaponAssemblerTab::OnSelectedStageScopeWidgetChanged)
								]
							]

							+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 1.0f)
							[
								SNew(SCheckBox)
								.IsChecked(this, &SFPSRWeaponAssemblerTab::GetSelectedStageScopeVignette)
								.OnCheckStateChanged(this, &SFPSRWeaponAssemblerTab::OnSelectedStageScopeVignetteChanged)
								[
									SNew(STextBlock).Text(LOCTEXT("StageScopeVignetteLabel", "스코프 비네트"))
								]
							]

							+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 1.0f)
							[
								SNew(SCheckBox)
								.IsChecked(this, &SFPSRWeaponAssemblerTab::GetSelectedStageHideWeapon)
								.OnCheckStateChanged(this, &SFPSRWeaponAssemblerTab::OnSelectedStageHideWeaponChanged)
								[
									SNew(STextBlock).Text(LOCTEXT("StageHideWeaponLabel", "스코프 시 1P총 숨김"))
								]
							]
						]
					]

					+ SVerticalBox::Slot().AutoHeight().Padding(2.0f)
					[
						SNew(SHorizontalBox)

						+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0.0f, 0.0f, 1.0f, 0.0f)
						[
							SNew(SButton)
							.HAlign(HAlign_Center)
							.Text(LOCTEXT("AddStageButton", "＋ 단계 추가"))
							.ToolTipText(LOCTEXT("AddStageButtonTooltip", "아래 '사용 가능 파츠'에서 고른 메시를 이 슬롯의 진화 단계로 추가합니다. 필요 스택은 자동 증가하며, 각 단계의 스택 수는 목록에서 조정하세요."))
							.IsEnabled(this, &SFPSRWeaponAssemblerTab::IsAddStageEnabled)
							.OnClicked(this, &SFPSRWeaponAssemblerTab::OnAddStageClicked)
						]

						+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(1.0f, 0.0f, 0.0f, 0.0f)
						[
							SNew(SButton)
							.HAlign(HAlign_Center)
							.Text(LOCTEXT("RemoveStageButton", "− 단계 제거"))
							.ToolTipText(LOCTEXT("RemoveStageButtonTooltip", "선택한 진화 단계를 제거합니다."))
							.IsEnabled(this, &SFPSRWeaponAssemblerTab::IsRemoveStageEnabled)
							.OnClicked(this, &SFPSRWeaponAssemblerTab::OnRemoveStageClicked)
						]
					]
				]

				+ SSplitter::Slot()
				.Value(0.33f)
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

	// 파츠 목록이 바뀌면(무기 교체/추가/제거) 진화 패널도 선택 슬롯 기준으로 다시 맞춘다(선택 없으면 자동으로 빈다).
	RefreshStageList();
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
		// 슬롯이 바뀌기 전에 이전 단계 미리보기를 캡처·종료(스테일 인덱스로 다른 슬롯에 쓰는 것 방지).
		Viewport->GetAssemblerClient()->EndStagePreview();
		Viewport->GetAssemblerClient()->SetSelectedPart(Item.IsValid() ? Item->Index : INDEX_NONE);
	}

	// 슬롯이 바뀌면 진화 패널(카드 피커+단계 목록)이 새 슬롯을 따라가도록 다시 구성.
	RefreshStageList();
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

	// 슬롯 제거 전 단계 미리보기 정리(RemoveSelectedPart 내부에도 있지만 탭에서도 명시적으로 — 중복 호출은 no-op).
	Client->EndStagePreview();
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
// Evolution authoring panel (선택 슬롯의 진화 카드 + 진화 단계 목록, W-U1b 저작 UI — 뷰포트 3D 미리보기는 C2)
// ---------------------------------------------------------------------------------------------------------------

FString SFPSRWeaponAssemblerTab::GetEvolutionFragmentPath() const
{
	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport.IsValid() ? Viewport->GetAssemblerClient() : nullptr;
	UFPSRWeaponDataAsset* DA = Client.IsValid() ? Client->GetWeaponDA() : nullptr;
	const int32 Sel = Client.IsValid() ? Client->GetSelectedPart() : INDEX_NONE;
	if (!DA || !DA->WeaponParts1P.IsValidIndex(Sel))
	{
		return FString();
	}
	return DA->WeaponParts1P[Sel].EvolutionFragment.ToSoftObjectPath().ToString();
}

void SFPSRWeaponAssemblerTab::OnEvolutionFragmentChanged(const FAssetData& AssetData)
{
	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport.IsValid() ? Viewport->GetAssemblerClient() : nullptr;
	UFPSRWeaponDataAsset* DA = Client.IsValid() ? Client->GetWeaponDA() : nullptr;
	const int32 Sel = Client.IsValid() ? Client->GetSelectedPart() : INDEX_NONE;
	if (!DA || !DA->WeaponParts1P.IsValidIndex(Sel))
	{
		return;
	}

	// AssetData가 비어 있으면(AllowClear로 지운 경우) GetAsset()이 null이라 EvolutionFragment도 함께 정리된다(진화 없음).
	DA->WeaponParts1P[Sel].EvolutionFragment = Cast<UFPSRWeaponFragment>(AssetData.GetAsset());
	DA->MarkPackageDirty();

	if (StatusText.IsValid())
	{
		StatusText->SetText(LOCTEXT("EvolutionFragmentDone", "진화 카드를 설정했습니다. '조립→저장'을 눌러야 저장됩니다."));
	}
}

void SFPSRWeaponAssemblerTab::RefreshStageList()
{
	StageRows.Reset();
	SelectedStageRow.Reset();

	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport.IsValid() ? Viewport->GetAssemblerClient() : nullptr;
	UFPSRWeaponDataAsset* DA = Client.IsValid() ? Client->GetWeaponDA() : nullptr;
	const int32 Sel = Client.IsValid() ? Client->GetSelectedPart() : INDEX_NONE;
	if (DA && DA->WeaponParts1P.IsValidIndex(Sel))
	{
		const TArray<FFPSRWeaponPartStage>& Stages = DA->WeaponParts1P[Sel].Stages;
		for (int32 s = 0; s < Stages.Num(); ++s)
		{
			TSharedPtr<FStageRow> Row = MakeShared<FStageRow>();
			Row->StageIndex = s;
			StageRows.Add(Row);
		}
	}

	if (StageListView.IsValid())
	{
		StageListView->RequestListRefresh();
	}
}

TSharedRef<ITableRow> SFPSRWeaponAssemblerTab::OnGenerateStageRow(TSharedPtr<FStageRow> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	const int32 StageIndex = Item.IsValid() ? Item->StageIndex : INDEX_NONE;

	return SNew(STableRow<TSharedPtr<FStageRow>>, OwnerTable)
		[
			SNew(STextBlock)
			.Text_Lambda([this, StageIndex]() { return MakeStageRowSummary(StageIndex); })
		];
}

void SFPSRWeaponAssemblerTab::OnStageSelectionChanged(TSharedPtr<FStageRow> Item, ESelectInfo::Type SelectInfo)
{
	SelectedStageRow = Item;

	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport.IsValid() ? Viewport->GetAssemblerClient() : nullptr;
	const int32 Sel = Client.IsValid() ? Client->GetSelectedPart() : INDEX_NONE;
	if (Client.IsValid() && Item.IsValid() && Sel != INDEX_NONE)
	{
		// 뷰포트에서 이 단계를 미리본다: 슬롯 컴포넌트 메시를 stage 메시로 바꾸고 base 기준 stage.Offset만큼 배치,
		// 기즈모를 그 슬롯에 맞춘다(BeginStagePreview가 이전 미리보기가 있으면 알아서 먼저 캡처·종료한다).
		Client->BeginStagePreview(Sel, Item->StageIndex);
	}
	else if (Client.IsValid())
	{
		// 선택 해제(빈 클릭 등) — 미리보기 중이었다면 캡처·복원.
		Client->EndStagePreview();
	}
}

FReply SFPSRWeaponAssemblerTab::OnAddStageClicked()
{
	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport.IsValid() ? Viewport->GetAssemblerClient() : nullptr;
	UFPSRWeaponDataAsset* DA = Client.IsValid() ? Client->GetWeaponDA() : nullptr;
	const int32 Sel = Client.IsValid() ? Client->GetSelectedPart() : INDEX_NONE;
	if (!DA || !DA->WeaponParts1P.IsValidIndex(Sel))
	{
		if (StatusText.IsValid())
		{
			StatusText->SetText(LOCTEXT("AddStageNoSelection", "먼저 위 '현재 파츠'에서 슬롯을 선택하세요."));
		}
		return FReply::Handled();
	}

	// Stages 배열을 건드리기 전에 이전 단계 미리보기를 캡처·종료(스테일 인덱스 방지).
	Client->EndStagePreview();

	FFPSRWeaponPartAttachment& Slot = DA->WeaponParts1P[Sel];

	// 사용 가능 파츠에서 고른 메시를 새 단계로(없으면 null-safe로 빈 단계). 필요 스택은 목록 끝에 자동 이어붙인다.
	FFPSRWeaponPartStage NewStage;
	NewStage.Mesh = SelectedAvailPart.IsValid() ? Cast<UStaticMesh>(SelectedAvailPart->MeshPath.TryLoad()) : nullptr;
	NewStage.MinStacks = Slot.Stages.Num() + 1;
	Slot.Stages.Add(NewStage);
	DA->MarkPackageDirty();

	RefreshStageList();
	if (StageListView.IsValid() && StageRows.Num() > 0)
	{
		StageListView->SetSelection(StageRows.Last());
	}

	if (StatusText.IsValid())
	{
		StatusText->SetText(FText::Format(
			LOCTEXT("AddStageDone", "진화 단계 추가(스택 {0}). 스택 수는 목록에서 조정. C2에서 위치 배치 예정. '조립→저장'을 눌러야 저장됩니다."),
			FText::AsNumber(NewStage.MinStacks)));
	}
	return FReply::Handled();
}

bool SFPSRWeaponAssemblerTab::IsAddStageEnabled() const
{
	return Viewport.IsValid() && Viewport->GetAssemblerClient().IsValid()
		&& Viewport->GetAssemblerClient()->GetSelectedPart() != INDEX_NONE;
}

FReply SFPSRWeaponAssemblerTab::OnRemoveStageClicked()
{
	if (!SelectedStageRow.IsValid())
	{
		return FReply::Handled();
	}

	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport.IsValid() ? Viewport->GetAssemblerClient() : nullptr;
	UFPSRWeaponDataAsset* DA = Client.IsValid() ? Client->GetWeaponDA() : nullptr;
	const int32 Sel = Client.IsValid() ? Client->GetSelectedPart() : INDEX_NONE;
	if (!DA || !DA->WeaponParts1P.IsValidIndex(Sel))
	{
		return FReply::Handled();
	}

	// Stages 배열을 건드리기 전에 이전 단계 미리보기를 캡처·종료(스테일 인덱스 방지).
	Client->EndStagePreview();

	TArray<FFPSRWeaponPartStage>& Stages = DA->WeaponParts1P[Sel].Stages;
	if (Stages.IsValidIndex(SelectedStageRow->StageIndex))
	{
		Stages.RemoveAt(SelectedStageRow->StageIndex);
		DA->MarkPackageDirty();
	}

	RefreshStageList();

	if (StatusText.IsValid())
	{
		StatusText->SetText(LOCTEXT("RemoveStageDone", "선택한 진화 단계를 제거했습니다. '조립→저장'을 눌러야 저장됩니다."));
	}
	return FReply::Handled();
}

bool SFPSRWeaponAssemblerTab::IsRemoveStageEnabled() const
{
	return SelectedStageRow.IsValid();
}

int32 SFPSRWeaponAssemblerTab::GetStageMinStacks(int32 StageIndex) const
{
	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport.IsValid() ? Viewport->GetAssemblerClient() : nullptr;
	UFPSRWeaponDataAsset* DA = Client.IsValid() ? Client->GetWeaponDA() : nullptr;
	const int32 Sel = Client.IsValid() ? Client->GetSelectedPart() : INDEX_NONE;
	if (DA && DA->WeaponParts1P.IsValidIndex(Sel) && DA->WeaponParts1P[Sel].Stages.IsValidIndex(StageIndex))
	{
		return DA->WeaponParts1P[Sel].Stages[StageIndex].MinStacks;
	}
	return 1;
}

void SFPSRWeaponAssemblerTab::OnStageMinStacksChanged(int32 NewValue, int32 StageIndex)
{
	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport.IsValid() ? Viewport->GetAssemblerClient() : nullptr;
	UFPSRWeaponDataAsset* DA = Client.IsValid() ? Client->GetWeaponDA() : nullptr;
	const int32 Sel = Client.IsValid() ? Client->GetSelectedPart() : INDEX_NONE;
	if (DA && DA->WeaponParts1P.IsValidIndex(Sel) && DA->WeaponParts1P[Sel].Stages.IsValidIndex(StageIndex))
	{
		DA->WeaponParts1P[Sel].Stages[StageIndex].MinStacks = FMath::Max(1, NewValue);
		DA->MarkPackageDirty();
		// 스핀박스가 값을 소유(Value_Lambda가 즉시 되읽음) — 리스트 리프레시 불요.
	}
}

// ---------------------------------------------------------------------------------------------------------------
// "선택 단계" 소폼 (트리거 종류 + 스택/스탯 필드 + 순서 이동)
// ---------------------------------------------------------------------------------------------------------------

int32 SFPSRWeaponAssemblerTab::GetSelectedStageIndex() const
{
	return SelectedStageRow.IsValid() ? SelectedStageRow->StageIndex : INDEX_NONE;
}

FText SFPSRWeaponAssemblerTab::MakeStageRowSummary(int32 StageIndex) const
{
	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport.IsValid() ? Viewport->GetAssemblerClient() : nullptr;
	UFPSRWeaponDataAsset* DA = Client.IsValid() ? Client->GetWeaponDA() : nullptr;
	const int32 Sel = Client.IsValid() ? Client->GetSelectedPart() : INDEX_NONE;
	if (!DA || !DA->WeaponParts1P.IsValidIndex(Sel) || !DA->WeaponParts1P[Sel].Stages.IsValidIndex(StageIndex))
	{
		return FText::GetEmpty();
	}

	const FFPSRWeaponPartStage& Stage = DA->WeaponParts1P[Sel].Stages[StageIndex];

	FText TriggerSummary;
	if (Stage.Trigger == EFPSRPartStageTrigger::FragmentStacks)
	{
		TriggerSummary = FText::Format(LOCTEXT("StageTriggerStacksSummary", "스택 ≥{0}"), FText::AsNumber(Stage.MinStacks));
	}
	else
	{
		const FText AxisName = StaticEnum<EFPSRWeaponStat>()->GetDisplayNameTextByValue((int64)Stage.StatAxis);
		const FText CompareSymbol = StaticEnum<EFPSRStatCompare>()->GetDisplayNameTextByValue((int64)Stage.StatCompare);
		TriggerSummary = FText::Format(LOCTEXT("StageTriggerStatSummary", "{0} {1} {2}"), AxisName, CompareSymbol, FText::AsNumber(Stage.StatValue));
	}

	const FText MeshLabel = Stage.Mesh.IsNull()
		? LOCTEXT("StageMeshNone", "(메시 없음)")
		: FText::FromString(Stage.Mesh.GetAssetName());

	return FText::Format(LOCTEXT("StageRowSummary", "{0}. [{1}] {2}"), FText::AsNumber(StageIndex + 1), TriggerSummary, MeshLabel);
}

int32 SFPSRWeaponAssemblerTab::GetSelectedStageTriggerValue() const
{
	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport.IsValid() ? Viewport->GetAssemblerClient() : nullptr;
	UFPSRWeaponDataAsset* DA = Client.IsValid() ? Client->GetWeaponDA() : nullptr;
	const int32 Sel = Client.IsValid() ? Client->GetSelectedPart() : INDEX_NONE;
	const int32 StageIndex = GetSelectedStageIndex();
	if (DA && DA->WeaponParts1P.IsValidIndex(Sel) && DA->WeaponParts1P[Sel].Stages.IsValidIndex(StageIndex))
	{
		return (int32)DA->WeaponParts1P[Sel].Stages[StageIndex].Trigger;
	}
	return (int32)EFPSRPartStageTrigger::FragmentStacks;
}

void SFPSRWeaponAssemblerTab::OnSelectedStageTriggerChanged(int32 NewValue, ESelectInfo::Type SelectInfo)
{
	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport.IsValid() ? Viewport->GetAssemblerClient() : nullptr;
	UFPSRWeaponDataAsset* DA = Client.IsValid() ? Client->GetWeaponDA() : nullptr;
	const int32 Sel = Client.IsValid() ? Client->GetSelectedPart() : INDEX_NONE;
	const int32 StageIndex = GetSelectedStageIndex();
	if (DA && DA->WeaponParts1P.IsValidIndex(Sel) && DA->WeaponParts1P[Sel].Stages.IsValidIndex(StageIndex))
	{
		DA->WeaponParts1P[Sel].Stages[StageIndex].Trigger = (EFPSRPartStageTrigger)NewValue;
		DA->MarkPackageDirty();
		if (StageListView.IsValid())
		{
			StageListView->RequestListRefresh();
		}
	}
}

int32 SFPSRWeaponAssemblerTab::GetSelectedStageStatAxisValue() const
{
	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport.IsValid() ? Viewport->GetAssemblerClient() : nullptr;
	UFPSRWeaponDataAsset* DA = Client.IsValid() ? Client->GetWeaponDA() : nullptr;
	const int32 Sel = Client.IsValid() ? Client->GetSelectedPart() : INDEX_NONE;
	const int32 StageIndex = GetSelectedStageIndex();
	if (DA && DA->WeaponParts1P.IsValidIndex(Sel) && DA->WeaponParts1P[Sel].Stages.IsValidIndex(StageIndex))
	{
		return (int32)DA->WeaponParts1P[Sel].Stages[StageIndex].StatAxis;
	}
	return (int32)EFPSRWeaponStat::FireRate;
}

void SFPSRWeaponAssemblerTab::OnSelectedStageStatAxisChanged(int32 NewValue, ESelectInfo::Type SelectInfo)
{
	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport.IsValid() ? Viewport->GetAssemblerClient() : nullptr;
	UFPSRWeaponDataAsset* DA = Client.IsValid() ? Client->GetWeaponDA() : nullptr;
	const int32 Sel = Client.IsValid() ? Client->GetSelectedPart() : INDEX_NONE;
	const int32 StageIndex = GetSelectedStageIndex();
	if (DA && DA->WeaponParts1P.IsValidIndex(Sel) && DA->WeaponParts1P[Sel].Stages.IsValidIndex(StageIndex))
	{
		DA->WeaponParts1P[Sel].Stages[StageIndex].StatAxis = (EFPSRWeaponStat)NewValue;
		DA->MarkPackageDirty();
		if (StageListView.IsValid())
		{
			StageListView->RequestListRefresh();
		}
	}
}

int32 SFPSRWeaponAssemblerTab::GetSelectedStageStatCompareValue() const
{
	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport.IsValid() ? Viewport->GetAssemblerClient() : nullptr;
	UFPSRWeaponDataAsset* DA = Client.IsValid() ? Client->GetWeaponDA() : nullptr;
	const int32 Sel = Client.IsValid() ? Client->GetSelectedPart() : INDEX_NONE;
	const int32 StageIndex = GetSelectedStageIndex();
	if (DA && DA->WeaponParts1P.IsValidIndex(Sel) && DA->WeaponParts1P[Sel].Stages.IsValidIndex(StageIndex))
	{
		return (int32)DA->WeaponParts1P[Sel].Stages[StageIndex].StatCompare;
	}
	return (int32)EFPSRStatCompare::GreaterOrEqual;
}

void SFPSRWeaponAssemblerTab::OnSelectedStageStatCompareChanged(int32 NewValue, ESelectInfo::Type SelectInfo)
{
	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport.IsValid() ? Viewport->GetAssemblerClient() : nullptr;
	UFPSRWeaponDataAsset* DA = Client.IsValid() ? Client->GetWeaponDA() : nullptr;
	const int32 Sel = Client.IsValid() ? Client->GetSelectedPart() : INDEX_NONE;
	const int32 StageIndex = GetSelectedStageIndex();
	if (DA && DA->WeaponParts1P.IsValidIndex(Sel) && DA->WeaponParts1P[Sel].Stages.IsValidIndex(StageIndex))
	{
		DA->WeaponParts1P[Sel].Stages[StageIndex].StatCompare = (EFPSRStatCompare)NewValue;
		DA->MarkPackageDirty();
		if (StageListView.IsValid())
		{
			StageListView->RequestListRefresh();
		}
	}
}

TOptional<float> SFPSRWeaponAssemblerTab::GetSelectedStageStatValue() const
{
	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport.IsValid() ? Viewport->GetAssemblerClient() : nullptr;
	UFPSRWeaponDataAsset* DA = Client.IsValid() ? Client->GetWeaponDA() : nullptr;
	const int32 Sel = Client.IsValid() ? Client->GetSelectedPart() : INDEX_NONE;
	const int32 StageIndex = GetSelectedStageIndex();
	if (DA && DA->WeaponParts1P.IsValidIndex(Sel) && DA->WeaponParts1P[Sel].Stages.IsValidIndex(StageIndex))
	{
		return DA->WeaponParts1P[Sel].Stages[StageIndex].StatValue;
	}
	return 0.0f;
}

void SFPSRWeaponAssemblerTab::OnSelectedStageStatValueChanged(float NewValue)
{
	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport.IsValid() ? Viewport->GetAssemblerClient() : nullptr;
	UFPSRWeaponDataAsset* DA = Client.IsValid() ? Client->GetWeaponDA() : nullptr;
	const int32 Sel = Client.IsValid() ? Client->GetSelectedPart() : INDEX_NONE;
	const int32 StageIndex = GetSelectedStageIndex();
	if (DA && DA->WeaponParts1P.IsValidIndex(Sel) && DA->WeaponParts1P[Sel].Stages.IsValidIndex(StageIndex))
	{
		DA->WeaponParts1P[Sel].Stages[StageIndex].StatValue = NewValue;
		DA->MarkPackageDirty();
		if (StageListView.IsValid())
		{
			StageListView->RequestListRefresh();
		}
	}
}

EVisibility SFPSRWeaponAssemblerTab::GetStackFieldVisibility() const
{
	return GetSelectedStageTriggerValue() == (int32)EFPSRPartStageTrigger::FragmentStacks ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility SFPSRWeaponAssemblerTab::GetStatFieldVisibility() const
{
	return GetSelectedStageTriggerValue() == (int32)EFPSRPartStageTrigger::StatThreshold ? EVisibility::Visible : EVisibility::Collapsed;
}

bool SFPSRWeaponAssemblerTab::IsStageSelected() const
{
	return GetSelectedStageIndex() != INDEX_NONE;
}

// ---------------------------------------------------------------------------------------------------------------
// "선택 단계" 소폼 하단 "스코프(사이트 단계)" 섹션 (선택 단계 Scope 필드)
// ---------------------------------------------------------------------------------------------------------------

ECheckBoxState SFPSRWeaponAssemblerTab::GetSelectedStageScopeOverlay() const
{
	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport.IsValid() ? Viewport->GetAssemblerClient() : nullptr;
	UFPSRWeaponDataAsset* DA = Client.IsValid() ? Client->GetWeaponDA() : nullptr;
	const int32 Sel = Client.IsValid() ? Client->GetSelectedPart() : INDEX_NONE;
	const int32 StageIndex = GetSelectedStageIndex();
	if (DA && DA->WeaponParts1P.IsValidIndex(Sel) && DA->WeaponParts1P[Sel].Stages.IsValidIndex(StageIndex))
	{
		return DA->WeaponParts1P[Sel].Stages[StageIndex].Scope.bScopeOverlay ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	}
	return ECheckBoxState::Unchecked;
}

void SFPSRWeaponAssemblerTab::OnSelectedStageScopeOverlayChanged(ECheckBoxState NewState)
{
	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport.IsValid() ? Viewport->GetAssemblerClient() : nullptr;
	UFPSRWeaponDataAsset* DA = Client.IsValid() ? Client->GetWeaponDA() : nullptr;
	const int32 Sel = Client.IsValid() ? Client->GetSelectedPart() : INDEX_NONE;
	const int32 StageIndex = GetSelectedStageIndex();
	if (DA && DA->WeaponParts1P.IsValidIndex(Sel) && DA->WeaponParts1P[Sel].Stages.IsValidIndex(StageIndex))
	{
		DA->WeaponParts1P[Sel].Stages[StageIndex].Scope.bScopeOverlay = (NewState == ECheckBoxState::Checked);
		DA->MarkPackageDirty();
		if (StageListView.IsValid())
		{
			StageListView->RequestListRefresh();
		}
	}
}

TOptional<float> SFPSRWeaponAssemblerTab::GetSelectedStageAimFOV() const
{
	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport.IsValid() ? Viewport->GetAssemblerClient() : nullptr;
	UFPSRWeaponDataAsset* DA = Client.IsValid() ? Client->GetWeaponDA() : nullptr;
	const int32 Sel = Client.IsValid() ? Client->GetSelectedPart() : INDEX_NONE;
	const int32 StageIndex = GetSelectedStageIndex();
	if (DA && DA->WeaponParts1P.IsValidIndex(Sel) && DA->WeaponParts1P[Sel].Stages.IsValidIndex(StageIndex))
	{
		return DA->WeaponParts1P[Sel].Stages[StageIndex].Scope.AimFieldOfView;
	}
	return 0.0f;
}

void SFPSRWeaponAssemblerTab::OnSelectedStageAimFOVChanged(float NewValue)
{
	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport.IsValid() ? Viewport->GetAssemblerClient() : nullptr;
	UFPSRWeaponDataAsset* DA = Client.IsValid() ? Client->GetWeaponDA() : nullptr;
	const int32 Sel = Client.IsValid() ? Client->GetSelectedPart() : INDEX_NONE;
	const int32 StageIndex = GetSelectedStageIndex();
	if (DA && DA->WeaponParts1P.IsValidIndex(Sel) && DA->WeaponParts1P[Sel].Stages.IsValidIndex(StageIndex))
	{
		DA->WeaponParts1P[Sel].Stages[StageIndex].Scope.AimFieldOfView = NewValue;
		DA->MarkPackageDirty();
		if (StageListView.IsValid())
		{
			StageListView->RequestListRefresh();
		}
	}
}

const UClass* SFPSRWeaponAssemblerTab::GetSelectedStageScopeWidgetClass() const
{
	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport.IsValid() ? Viewport->GetAssemblerClient() : nullptr;
	UFPSRWeaponDataAsset* DA = Client.IsValid() ? Client->GetWeaponDA() : nullptr;
	const int32 Sel = Client.IsValid() ? Client->GetSelectedPart() : INDEX_NONE;
	const int32 StageIndex = GetSelectedStageIndex();
	if (DA && DA->WeaponParts1P.IsValidIndex(Sel) && DA->WeaponParts1P[Sel].Stages.IsValidIndex(StageIndex))
	{
		// LoadSynchronous()는 내부적으로 Get()을 먼저 시도하므로 이미 로드돼 있으면 추가 로드 없이 반환된다.
		return DA->WeaponParts1P[Sel].Stages[StageIndex].Scope.ScopeOverlayWidgetClass.LoadSynchronous();
	}
	return nullptr;
}

void SFPSRWeaponAssemblerTab::OnSelectedStageScopeWidgetChanged(const UClass* NewClass)
{
	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport.IsValid() ? Viewport->GetAssemblerClient() : nullptr;
	UFPSRWeaponDataAsset* DA = Client.IsValid() ? Client->GetWeaponDA() : nullptr;
	const int32 Sel = Client.IsValid() ? Client->GetSelectedPart() : INDEX_NONE;
	const int32 StageIndex = GetSelectedStageIndex();
	if (DA && DA->WeaponParts1P.IsValidIndex(Sel) && DA->WeaponParts1P[Sel].Stages.IsValidIndex(StageIndex))
	{
		// NewClass가 nullptr이면(AllowNone으로 지운 경우) ScopeOverlayWidgetClass도 함께 정리된다.
		DA->WeaponParts1P[Sel].Stages[StageIndex].Scope.ScopeOverlayWidgetClass = NewClass;
		DA->MarkPackageDirty();
		if (StageListView.IsValid())
		{
			StageListView->RequestListRefresh();
		}
	}
}

ECheckBoxState SFPSRWeaponAssemblerTab::GetSelectedStageScopeVignette() const
{
	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport.IsValid() ? Viewport->GetAssemblerClient() : nullptr;
	UFPSRWeaponDataAsset* DA = Client.IsValid() ? Client->GetWeaponDA() : nullptr;
	const int32 Sel = Client.IsValid() ? Client->GetSelectedPart() : INDEX_NONE;
	const int32 StageIndex = GetSelectedStageIndex();
	if (DA && DA->WeaponParts1P.IsValidIndex(Sel) && DA->WeaponParts1P[Sel].Stages.IsValidIndex(StageIndex))
	{
		return DA->WeaponParts1P[Sel].Stages[StageIndex].Scope.bScopeVignette ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	}
	return ECheckBoxState::Unchecked;
}

void SFPSRWeaponAssemblerTab::OnSelectedStageScopeVignetteChanged(ECheckBoxState NewState)
{
	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport.IsValid() ? Viewport->GetAssemblerClient() : nullptr;
	UFPSRWeaponDataAsset* DA = Client.IsValid() ? Client->GetWeaponDA() : nullptr;
	const int32 Sel = Client.IsValid() ? Client->GetSelectedPart() : INDEX_NONE;
	const int32 StageIndex = GetSelectedStageIndex();
	if (DA && DA->WeaponParts1P.IsValidIndex(Sel) && DA->WeaponParts1P[Sel].Stages.IsValidIndex(StageIndex))
	{
		DA->WeaponParts1P[Sel].Stages[StageIndex].Scope.bScopeVignette = (NewState == ECheckBoxState::Checked);
		DA->MarkPackageDirty();
		if (StageListView.IsValid())
		{
			StageListView->RequestListRefresh();
		}
	}
}

ECheckBoxState SFPSRWeaponAssemblerTab::GetSelectedStageHideWeapon() const
{
	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport.IsValid() ? Viewport->GetAssemblerClient() : nullptr;
	UFPSRWeaponDataAsset* DA = Client.IsValid() ? Client->GetWeaponDA() : nullptr;
	const int32 Sel = Client.IsValid() ? Client->GetSelectedPart() : INDEX_NONE;
	const int32 StageIndex = GetSelectedStageIndex();
	if (DA && DA->WeaponParts1P.IsValidIndex(Sel) && DA->WeaponParts1P[Sel].Stages.IsValidIndex(StageIndex))
	{
		return DA->WeaponParts1P[Sel].Stages[StageIndex].Scope.bHideWeaponWhileScoped ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	}
	return ECheckBoxState::Unchecked;
}

void SFPSRWeaponAssemblerTab::OnSelectedStageHideWeaponChanged(ECheckBoxState NewState)
{
	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport.IsValid() ? Viewport->GetAssemblerClient() : nullptr;
	UFPSRWeaponDataAsset* DA = Client.IsValid() ? Client->GetWeaponDA() : nullptr;
	const int32 Sel = Client.IsValid() ? Client->GetSelectedPart() : INDEX_NONE;
	const int32 StageIndex = GetSelectedStageIndex();
	if (DA && DA->WeaponParts1P.IsValidIndex(Sel) && DA->WeaponParts1P[Sel].Stages.IsValidIndex(StageIndex))
	{
		DA->WeaponParts1P[Sel].Stages[StageIndex].Scope.bHideWeaponWhileScoped = (NewState == ECheckBoxState::Checked);
		DA->MarkPackageDirty();
		if (StageListView.IsValid())
		{
			StageListView->RequestListRefresh();
		}
	}
}

EVisibility SFPSRWeaponAssemblerTab::GetScopeOverlaySubFieldVisibility() const
{
	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport.IsValid() ? Viewport->GetAssemblerClient() : nullptr;
	UFPSRWeaponDataAsset* DA = Client.IsValid() ? Client->GetWeaponDA() : nullptr;
	const int32 Sel = Client.IsValid() ? Client->GetSelectedPart() : INDEX_NONE;
	const int32 StageIndex = GetSelectedStageIndex();
	if (DA && DA->WeaponParts1P.IsValidIndex(Sel) && DA->WeaponParts1P[Sel].Stages.IsValidIndex(StageIndex))
	{
		return DA->WeaponParts1P[Sel].Stages[StageIndex].Scope.bScopeOverlay ? EVisibility::Visible : EVisibility::Collapsed;
	}
	return EVisibility::Collapsed;
}

FReply SFPSRWeaponAssemblerTab::OnStageMoveUpClicked()
{
	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport.IsValid() ? Viewport->GetAssemblerClient() : nullptr;
	UFPSRWeaponDataAsset* DA = Client.IsValid() ? Client->GetWeaponDA() : nullptr;
	const int32 Sel = Client.IsValid() ? Client->GetSelectedPart() : INDEX_NONE;
	const int32 StageIndex = GetSelectedStageIndex();
	if (!DA || !DA->WeaponParts1P.IsValidIndex(Sel))
	{
		return FReply::Handled();
	}

	TArray<FFPSRWeaponPartStage>& Stages = DA->WeaponParts1P[Sel].Stages;
	if (!Stages.IsValidIndex(StageIndex) || StageIndex <= 0)
	{
		return FReply::Handled();
	}

	// Stages 배열을 건드리기 전에 이전 단계 미리보기를 캡처·종료(스왑 후 스테일 인덱스로 엉뚱한 단계에 쓰는 것 방지).
	if (Client.IsValid())
	{
		Client->EndStagePreview();
	}

	Stages.Swap(StageIndex, StageIndex - 1);
	DA->MarkPackageDirty();

	RefreshStageList();
	if (StageListView.IsValid() && StageRows.IsValidIndex(StageIndex - 1))
	{
		StageListView->SetSelection(StageRows[StageIndex - 1]);
	}

	if (StatusText.IsValid())
	{
		StatusText->SetText(LOCTEXT("StageMoveDone", "단계 순서를 변경했습니다. '조립→저장'을 눌러야 저장됩니다."));
	}
	return FReply::Handled();
}

bool SFPSRWeaponAssemblerTab::IsStageMoveUpEnabled() const
{
	return GetSelectedStageIndex() > 0;
}

FReply SFPSRWeaponAssemblerTab::OnStageMoveDownClicked()
{
	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport.IsValid() ? Viewport->GetAssemblerClient() : nullptr;
	UFPSRWeaponDataAsset* DA = Client.IsValid() ? Client->GetWeaponDA() : nullptr;
	const int32 Sel = Client.IsValid() ? Client->GetSelectedPart() : INDEX_NONE;
	const int32 StageIndex = GetSelectedStageIndex();
	if (!DA || !DA->WeaponParts1P.IsValidIndex(Sel))
	{
		return FReply::Handled();
	}

	TArray<FFPSRWeaponPartStage>& Stages = DA->WeaponParts1P[Sel].Stages;
	if (!Stages.IsValidIndex(StageIndex) || StageIndex >= Stages.Num() - 1)
	{
		return FReply::Handled();
	}

	// Stages 배열을 건드리기 전에 이전 단계 미리보기를 캡처·종료(스왑 후 스테일 인덱스로 엉뚱한 단계에 쓰는 것 방지).
	if (Client.IsValid())
	{
		Client->EndStagePreview();
	}

	Stages.Swap(StageIndex, StageIndex + 1);
	DA->MarkPackageDirty();

	RefreshStageList();
	if (StageListView.IsValid() && StageRows.IsValidIndex(StageIndex + 1))
	{
		StageListView->SetSelection(StageRows[StageIndex + 1]);
	}

	if (StatusText.IsValid())
	{
		StatusText->SetText(LOCTEXT("StageMoveDone", "단계 순서를 변경했습니다. '조립→저장'을 눌러야 저장됩니다."));
	}
	return FReply::Handled();
}

bool SFPSRWeaponAssemblerTab::IsStageMoveDownEnabled() const
{
	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport.IsValid() ? Viewport->GetAssemblerClient() : nullptr;
	UFPSRWeaponDataAsset* DA = Client.IsValid() ? Client->GetWeaponDA() : nullptr;
	const int32 Sel = Client.IsValid() ? Client->GetSelectedPart() : INDEX_NONE;
	const int32 StageIndex = GetSelectedStageIndex();
	if (!DA || !DA->WeaponParts1P.IsValidIndex(Sel))
	{
		return false;
	}
	return StageIndex != INDEX_NONE && StageIndex < DA->WeaponParts1P[Sel].Stages.Num() - 1;
}

// ---------------------------------------------------------------------------------------------------------------
// Toolbar
// ---------------------------------------------------------------------------------------------------------------

FReply SFPSRWeaponAssemblerTab::OnBakeClicked()
{
	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport.IsValid() ? Viewport->GetAssemblerClient() : nullptr;

	// 단계 메시가 base로 구워지는 것 방지 — base 복원 후 굽는다(캡처된 오프셋은 Stages[].Offset에 남아 있으니 손실 없음).
	if (Client.IsValid())
	{
		Client->EndStagePreview();
	}

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
