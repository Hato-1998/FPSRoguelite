// Copyright Epic Games, Inc. All Rights Reserved.

#include "Assembler/SFPSRWeaponAssemblerTab.h"

#include "Assembler/SFPSRWeaponAssemblerViewport.h"
#include "Assembler/FPSRWeaponAssemblerViewportClient.h"
#include "Assembler/FPSRWeaponAssemblerHelpers.h"
#include "Assembler/FPSRWeaponAssemblerSettings.h"
#include "FPSRogueliteEditorModule.h"   // 1인칭 뷰 탭 식별자 — "1인칭 뷰 열기" 버튼
#include "Framework/Docking/TabManager.h"
#include "Weapon/FPSRWeaponDataAsset.h"
#include "Weapon/FPSRWeaponFragment.h"

#include "AdvancedPreviewScene.h"
#include "PreviewScene.h"
#include "PropertyCustomizationHelpers.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/ARFilter.h"
#include "AssetRegistry/IAssetRegistry.h"

#include "Animation/AnimationAsset.h"
#include "Animation/Skeleton.h"
#include "Blueprint/UserWidget.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/MessageDialog.h" // FMessageDialog — C3 공용 기본 소켓 덮어쓰기 확인 대화상자
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "ReferenceSkeleton.h"
#include "Styling/SlateColor.h"

#include "BoneSelectionWidget.h" // SBoneSelectionWidget — 그립 뼈 피커 (Persona 모듈, C5)
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Input/SRotatorInputBox.h"
#include "Widgets/Input/SVectorInputBox.h"
#include "Widgets/Layout/SSeparator.h"
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
			// "팔 보기"/"손 위치 저장"은 오른쪽 "1인칭 팔" 사이드 패널로 옮겼다(툴바가 이미 꽉 차 있음 + 관련 컨트롤을
			// 한 곳에 모으는 게 낫다) — 아래 SSplitter의 세 번째(오른쪽) 슬롯 참고.
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
			.Value(0.58f)
			[
				SAssignNew(Viewport, SFPSRWeaponAssemblerViewport, PreviewScene.ToSharedRef())
			]

			// --- 1인칭 팔 패널 (뷰포트 오른쪽, 계약 C1) — 그립을 "손에 든 상태"로 판정하는 데 필요한 컨트롤을 한 곳에 모은다. ---
			+ SSplitter::Slot()
			.Value(0.20f)
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot().AutoHeight().Padding(2.0f)
				[
					SNew(STextBlock).Text(LOCTEXT("ArmsPanelHeader", "1인칭 팔"))
				]

				+ SVerticalBox::Slot().AutoHeight().Padding(2.0f)
				[
					SNew(SCheckBox)
					.IsChecked(this, &SFPSRWeaponAssemblerTab::IsShowArmsChecked)
					.OnCheckStateChanged(this, &SFPSRWeaponAssemblerTab::OnShowArmsChanged)
					.ToolTipText(LOCTEXT("ShowArmsTooltip", "1인칭 팔을 세우고 무기를 손에 얹습니다 — 그립을 '손에 든 상태'로 볼 수 있습니다. 팔 메시·애니는 바로 아래에서 지정합니다(비어 있으면 켜지지 않습니다)."))
					[
						SNew(STextBlock).Text(LOCTEXT("ShowArms", "팔 보기"))
					]
				]

				+ SVerticalBox::Slot().AutoHeight().Padding(2.0f)
				[
					SNew(SButton)
					.HAlign(HAlign_Center)
					.Text(LOCTEXT("OpenFirstPersonView", "1인칭 뷰 열기"))
					.ToolTipText(LOCTEXT("OpenFirstPersonViewTooltip", "플레이어 눈 시점으로, 게임과 같은 화면비로 보는 별도 창을 엽니다. 이 뷰포트에서 그립을 만지면 그 창에 바로 반영됩니다.\n\n창을 따로 띄우는 이유: 여기 붙어 있는 뷰포트는 가로세로 비율이 레이아웃에 따라 정해져서, 시야각이 같아도 세로로 보이는 범위가 게임과 달라집니다.\n\n먼저 위의 '팔 보기'를 켜세요 — 1인칭 구도는 팔을 기준으로 잡힙니다."))
					.OnClicked(this, &SFPSRWeaponAssemblerTab::OnOpenFirstPersonViewClicked)
				]

				+ SVerticalBox::Slot().AutoHeight().Padding(2.0f)
				[
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 4.0f, 0.0f)
					[
						SNew(STextBlock).Text(LOCTEXT("ArmsMeshPrompt", "팔 메시:"))
					]

					+ SHorizontalBox::Slot().FillWidth(1.0f)
					[
						SNew(SObjectPropertyEntryBox)
						.AllowedClass(USkeletalMesh::StaticClass())
						.ObjectPath(this, &SFPSRWeaponAssemblerTab::GetArmsMeshObjectPath)
						.OnObjectChanged(this, &SFPSRWeaponAssemblerTab::OnArmsMeshAssetChanged)
						.AllowClear(true)
					]
				]

				+ SVerticalBox::Slot().AutoHeight().Padding(2.0f)
				[
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 4.0f, 0.0f)
					[
						SNew(STextBlock).Text(LOCTEXT("ArmsPosePrompt", "애니:"))
					]

					+ SHorizontalBox::Slot().FillWidth(1.0f)
					[
						SNew(SObjectPropertyEntryBox)
						.AllowedClass(UAnimationAsset::StaticClass())
						.ObjectPath(this, &SFPSRWeaponAssemblerTab::GetArmsPoseObjectPath)
						.OnObjectChanged(this, &SFPSRWeaponAssemblerTab::OnArmsPoseAssetChanged)
						.OnShouldFilterAsset(this, &SFPSRWeaponAssemblerTab::OnShouldFilterArmsPoseAsset)
						.AllowClear(true)
					]
				]

				// --- 재생 컨트롤 (C6) — 애니 없으면(GetPreviewLength()==0) 전부 비활성. ---
				+ SVerticalBox::Slot().AutoHeight().Padding(2.0f, 4.0f, 2.0f, 0.0f)
				[
					SNew(SHorizontalBox)
					.IsEnabled(this, &SFPSRWeaponAssemblerTab::IsPreviewControlsEnabled)

					+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 2.0f, 0.0f)
					[
						SNew(SButton)
						.Text(this, &SFPSRWeaponAssemblerTab::GetPreviewPlayPauseLabel)
						.ToolTipText(LOCTEXT("PreviewPlayPauseTooltip", "팔 애니를 재생/일시정지합니다."))
						.OnClicked(this, &SFPSRWeaponAssemblerTab::OnPreviewPlayPauseClicked)
					]

					+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(2.0f, 0.0f)
					[
						SNew(SSlider)
						.Value(this, &SFPSRWeaponAssemblerTab::GetPreviewSliderFraction)
						.OnValueChanged(this, &SFPSRWeaponAssemblerTab::OnPreviewSliderFractionChanged)
						.ToolTipText(LOCTEXT("PreviewSliderTooltip", "애니 재생 위치를 스크럽합니다."))
					]
				]

				+ SVerticalBox::Slot().AutoHeight().Padding(2.0f, 0.0f, 2.0f, 2.0f)
				[
					SNew(SHorizontalBox)
					.IsEnabled(this, &SFPSRWeaponAssemblerTab::IsPreviewControlsEnabled)

					+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
					[
						SNew(STextBlock).Text(this, &SFPSRWeaponAssemblerTab::GetPreviewTimeLabel)
					]

					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(SCheckBox)
						.IsChecked(this, &SFPSRWeaponAssemblerTab::IsPreviewLoopingChecked)
						.OnCheckStateChanged(this, &SFPSRWeaponAssemblerTab::OnPreviewLoopingChanged)
						.ToolTipText(LOCTEXT("PreviewLoopTooltip", "켜면 애니가 끝에서 되감아 반복 재생됩니다."))
						[
							SNew(STextBlock).Text(LOCTEXT("PreviewLoop", "루프"))
						]
					]
				]

				+ SVerticalBox::Slot().AutoHeight().Padding(2.0f, 6.0f, 2.0f, 2.0f)
				[
					SNew(SSeparator)
				]

				// --- 손 소켓 (C3~C5) — 🚨 공용 기본 소켓 사고 방지: 배지/경고문구/확인창/전용화 버튼. ---
				+ SVerticalBox::Slot().AutoHeight().Padding(2.0f)
				[
					SNew(STextBlock).Text(LOCTEXT("GripSocketHeader", "손 소켓"))
				]

				+ SVerticalBox::Slot().AutoHeight().Padding(2.0f)
				[
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 4.0f, 0.0f)
					[
						SNew(STextBlock).Text(LOCTEXT("GripSocketNamePrompt", "소켓 이름:"))
					]

					+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0.0f, 0.0f, 4.0f, 0.0f)
					[
						SAssignNew(GripSocketNameBox, SEditableTextBox)
						.HintText(LOCTEXT("GripSocketNameHint", "예: SOCKET_Weapon_Rifle"))
						.ToolTipText(LOCTEXT("GripSocketNameTooltip", "이 무기의 손 부착 소켓 이름입니다. '공용 기본'일 때 여기서 이름을 바꾸고 아래 '이 무기 전용 소켓 만들기'를 누르면 이 무기만의 소켓이 됩니다."))
					]

					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(this, &SFPSRWeaponAssemblerTab::GetGripSocketBadgeText)
						.ColorAndOpacity(this, &SFPSRWeaponAssemblerTab::GetGripSocketBadgeColor)
					]
				]

				+ SVerticalBox::Slot().AutoHeight().Padding(2.0f, 0.0f, 2.0f, 2.0f)
				[
					SNew(STextBlock)
					.Text(this, &SFPSRWeaponAssemblerTab::GetGripSocketWarningText)
					.ColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.55f, 0.05f)))
					.AutoWrapText(true)
					.Visibility(this, &SFPSRWeaponAssemblerTab::GetGripSocketWarningVisibility)
				]

				+ SVerticalBox::Slot().AutoHeight().Padding(2.0f)
				[
					SNew(SHorizontalBox)
					.IsEnabled(this, &SFPSRWeaponAssemblerTab::IsShowArmsEnabled)

					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 4.0f, 0.0f)
					[
						SNew(STextBlock).Text(LOCTEXT("GripBonePrompt", "붙는 뼈:"))
					]

					+ SHorizontalBox::Slot().FillWidth(1.0f)
					[
						SNew(SBoneSelectionWidget)
						.OnGetReferenceSkeleton(this, &SFPSRWeaponAssemblerTab::GetArmsReferenceSkeletonForBonePicker)
						.OnGetSelectedBone(this, &SFPSRWeaponAssemblerTab::GetGripBoneForBonePicker)
						.OnBoneSelectionChanged(this, &SFPSRWeaponAssemblerTab::OnGripBonePicked)
					]
				]

				+ SVerticalBox::Slot().AutoHeight().Padding(2.0f)
				[
					SNew(SVerticalBox)
					.IsEnabled(this, &SFPSRWeaponAssemblerTab::HasGripFrame)

					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock).Text(LOCTEXT("GripLocationPrompt", "위치(뼈 상대, cm):"))
					]

					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 1.0f, 0.0f, 3.0f)
					[
						SNew(SVectorInputBox)
						.AllowSpin(true)
						.SpinDelta(0.5f)
						.X(this, &SFPSRWeaponAssemblerTab::GetGripLocationX)
						.Y(this, &SFPSRWeaponAssemblerTab::GetGripLocationY)
						.Z(this, &SFPSRWeaponAssemblerTab::GetGripLocationZ)
						.OnXChanged(this, &SFPSRWeaponAssemblerTab::OnGripLocationXChanged)
						.OnYChanged(this, &SFPSRWeaponAssemblerTab::OnGripLocationYChanged)
						.OnZChanged(this, &SFPSRWeaponAssemblerTab::OnGripLocationZChanged)
					]

					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock).Text(LOCTEXT("GripRotationPrompt", "회전(뼈 상대, 도):"))
					]

					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 1.0f)
					[
						SNew(SRotatorInputBox)
						.AllowSpin(true)
						.SpinDelta(1.0f)
						.Roll(this, &SFPSRWeaponAssemblerTab::GetGripRotationRoll)
						.Pitch(this, &SFPSRWeaponAssemblerTab::GetGripRotationPitch)
						.Yaw(this, &SFPSRWeaponAssemblerTab::GetGripRotationYaw)
						.OnRollChanged(this, &SFPSRWeaponAssemblerTab::OnGripRotationRollChanged)
						.OnPitchChanged(this, &SFPSRWeaponAssemblerTab::OnGripRotationPitchChanged)
						.OnYawChanged(this, &SFPSRWeaponAssemblerTab::OnGripRotationYawChanged)
					]
				]

				// 뼈를 바꾸면 SetGripBone이 무기의 **월드 위치를 유지**한다(잘 잡아둔 그립을 뼈만 갈아끼울 때 튀지
				// 말라고). 그런데 이전 기준 뼈가 애니에서 안 움직이는 뼈였다면 그 '유지된 위치'는 쓸모없는 값이라,
				// 손에서 멀리 떨어진 채로 남는다. 그때 숫자 6칸을 손으로 0으로 만드는 대신 한 번에 뼈 위치로 보낸다.
				+ SVerticalBox::Slot().AutoHeight().Padding(2.0f, 4.0f, 2.0f, 2.0f)
				[
					SNew(SButton)
					.HAlign(HAlign_Center)
					.Text(LOCTEXT("ResetGripButton", "손 위치로 리셋 (0,0,0)"))
					.ToolTipText(LOCTEXT("ResetGripTooltip", "위치·회전을 모두 0으로 되돌려 무기를 기준 뼈에 정확히 겹칩니다. 뼈를 막 바꿔서 무기가 엉뚱한 데 있을 때 여기서 시작하세요. 저장하지는 않습니다 — '손 위치 저장'은 따로 눌러야 합니다."))
					.IsEnabled(this, &SFPSRWeaponAssemblerTab::HasGripFrame)
					.OnClicked(this, &SFPSRWeaponAssemblerTab::OnResetGripClicked)
				]

				+ SVerticalBox::Slot().AutoHeight().Padding(2.0f, 0.0f, 2.0f, 2.0f)
				[
					SNew(SButton)
					.HAlign(HAlign_Center)
					.Text(LOCTEXT("MakeDedicatedSocketButton", "이 무기 전용 소켓 만들기"))
					.ToolTipText(LOCTEXT("MakeDedicatedSocketTooltip", "위 입력값을 이 무기만의 소켓 이름으로 지정합니다(DA에 인메모리로 기록 — 실제 소켓은 '손 위치 저장'을 눌러야 구워집니다). 공용 기본과 다른 이름으로 바꾸세요 — 같은 이름이면 여전히 공용 소켓을 고치는 것입니다."))
					.IsEnabled(this, &SFPSRWeaponAssemblerTab::IsMakeDedicatedSocketEnabled)
					.OnClicked(this, &SFPSRWeaponAssemblerTab::OnMakeDedicatedSocketClicked)
				]

				+ SVerticalBox::Slot().AutoHeight().Padding(2.0f, 0.0f, 2.0f, 2.0f)
				[
					SNew(SButton)
					.HAlign(HAlign_Center)
					.Text(LOCTEXT("BakeHand", "손 위치 저장"))
					.IsEnabled(this, &SFPSRWeaponAssemblerTab::HasGripFrame)
					.ToolTipText(LOCTEXT("BakeHandTooltip", "'전체 이동'으로 잡은 무기 위치를 팔 메시의 무기 부착 소켓에 굽고 저장합니다. ⚠ 저장 대상이 무기가 아니라 '팔 메시'입니다(3인칭 바디 소켓은 건드리지 않습니다)."))
					.OnClicked(this, &SFPSRWeaponAssemblerTab::OnBakeHandClicked)
				]
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
			PreviewScene->SetFloorOffset(FPSRWeaponAssemblerHelpers::ComputeFloorOffsetToRest(Client->GetBodyComp(), Client->GetPartComps(), Client->GetArmsComp()));
		}
	}

	RefreshPartsList();
	RefreshAvailableParts();
	// 손 부착 소켓 이름은 무기(DA)가 결정한다(전용 값 or 캐릭터 기본) — 무기가 바뀔 때마다 입력 박스를 재동기화.
	RefreshGripSocketNameBox();

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
		for (const FFPSRWeaponPartAttachment& Part : DA->WeaponParts)
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
			// attachments folder (.../Modular/Attachments, incl. a Scopes/ subfolder) holds sights, grips, muzzle
			// devices, lasers, etc. Scan both — recursively so Attachments/Scopes is picked up too. The weapon folder
			// itself has no subfolders, so recursing it adds nothing. A missing attachments path simply returns nothing.
			// The sibling folder name is a config value (no C++ hardcode) so a content reorg needs no rebuild.
			const FString AttachmentsFolderName = GetDefault<UFPSRWeaponAssemblerSettings>()->AttachmentsFolderName;
			Filter.PackagePaths.Add(FName(*Folder));
			Filter.PackagePaths.Add(FName(*(FPackageName::GetLongPackagePath(Folder) / AttachmentsFolderName)));
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
	if (DA && DA->WeaponParts.IsValidIndex(Index) && !DA->WeaponParts[Index].DisplayLabel.IsEmpty())
	{
		SlotName = DA->WeaponParts[Index].DisplayLabel.ToString();
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
	if (!DA || !DA->WeaponParts.IsValidIndex(Sel))
	{
		return FText::GetEmpty();
	}
	return DA->WeaponParts[Sel].DisplayLabel;
}

void SFPSRWeaponAssemblerTab::OnSlotLabelCommitted(const FText& InText, ETextCommit::Type CommitType)
{
	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport.IsValid() ? Viewport->GetAssemblerClient() : nullptr;
	UFPSRWeaponDataAsset* DA = Client.IsValid() ? Client->GetWeaponDA() : nullptr;
	const int32 Sel = Client.IsValid() ? Client->GetSelectedPart() : INDEX_NONE;
	if (!DA || !DA->WeaponParts.IsValidIndex(Sel))
	{
		return;
	}

	DA->WeaponParts[Sel].DisplayLabel = InText;
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
		PreviewScene->SetFloorOffset(FPSRWeaponAssemblerHelpers::ComputeFloorOffsetToRest(Client->GetBodyComp(), Client->GetPartComps(), Client->GetArmsComp()));
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
		PreviewScene->SetFloorOffset(FPSRWeaponAssemblerHelpers::ComputeFloorOffsetToRest(Client->GetBodyComp(), Client->GetPartComps(), Client->GetArmsComp()));
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
	if (!DA || !DA->WeaponParts.IsValidIndex(Sel))
	{
		return FString();
	}
	return DA->WeaponParts[Sel].EvolutionFragment.ToSoftObjectPath().ToString();
}

void SFPSRWeaponAssemblerTab::OnEvolutionFragmentChanged(const FAssetData& AssetData)
{
	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport.IsValid() ? Viewport->GetAssemblerClient() : nullptr;
	UFPSRWeaponDataAsset* DA = Client.IsValid() ? Client->GetWeaponDA() : nullptr;
	const int32 Sel = Client.IsValid() ? Client->GetSelectedPart() : INDEX_NONE;
	if (!DA || !DA->WeaponParts.IsValidIndex(Sel))
	{
		return;
	}

	// AssetData가 비어 있으면(AllowClear로 지운 경우) GetAsset()이 null이라 EvolutionFragment도 함께 정리된다(진화 없음).
	DA->WeaponParts[Sel].EvolutionFragment = Cast<UFPSRWeaponFragment>(AssetData.GetAsset());
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
	if (DA && DA->WeaponParts.IsValidIndex(Sel))
	{
		const TArray<FFPSRWeaponPartStage>& Stages = DA->WeaponParts[Sel].Stages;
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
	if (!DA || !DA->WeaponParts.IsValidIndex(Sel))
	{
		if (StatusText.IsValid())
		{
			StatusText->SetText(LOCTEXT("AddStageNoSelection", "먼저 위 '현재 파츠'에서 슬롯을 선택하세요."));
		}
		return FReply::Handled();
	}

	// Stages 배열을 건드리기 전에 이전 단계 미리보기를 캡처·종료(스테일 인덱스 방지).
	Client->EndStagePreview();

	FFPSRWeaponPartAttachment& Slot = DA->WeaponParts[Sel];

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
	if (!DA || !DA->WeaponParts.IsValidIndex(Sel))
	{
		return FReply::Handled();
	}

	// Stages 배열을 건드리기 전에 이전 단계 미리보기를 캡처·종료(스테일 인덱스 방지).
	Client->EndStagePreview();

	TArray<FFPSRWeaponPartStage>& Stages = DA->WeaponParts[Sel].Stages;
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
	if (DA && DA->WeaponParts.IsValidIndex(Sel) && DA->WeaponParts[Sel].Stages.IsValidIndex(StageIndex))
	{
		return DA->WeaponParts[Sel].Stages[StageIndex].MinStacks;
	}
	return 1;
}

void SFPSRWeaponAssemblerTab::OnStageMinStacksChanged(int32 NewValue, int32 StageIndex)
{
	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport.IsValid() ? Viewport->GetAssemblerClient() : nullptr;
	UFPSRWeaponDataAsset* DA = Client.IsValid() ? Client->GetWeaponDA() : nullptr;
	const int32 Sel = Client.IsValid() ? Client->GetSelectedPart() : INDEX_NONE;
	if (DA && DA->WeaponParts.IsValidIndex(Sel) && DA->WeaponParts[Sel].Stages.IsValidIndex(StageIndex))
	{
		DA->WeaponParts[Sel].Stages[StageIndex].MinStacks = FMath::Max(1, NewValue);
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
	if (!DA || !DA->WeaponParts.IsValidIndex(Sel) || !DA->WeaponParts[Sel].Stages.IsValidIndex(StageIndex))
	{
		return FText::GetEmpty();
	}

	const FFPSRWeaponPartStage& Stage = DA->WeaponParts[Sel].Stages[StageIndex];

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
	if (DA && DA->WeaponParts.IsValidIndex(Sel) && DA->WeaponParts[Sel].Stages.IsValidIndex(StageIndex))
	{
		return (int32)DA->WeaponParts[Sel].Stages[StageIndex].Trigger;
	}
	return (int32)EFPSRPartStageTrigger::FragmentStacks;
}

void SFPSRWeaponAssemblerTab::OnSelectedStageTriggerChanged(int32 NewValue, ESelectInfo::Type SelectInfo)
{
	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport.IsValid() ? Viewport->GetAssemblerClient() : nullptr;
	UFPSRWeaponDataAsset* DA = Client.IsValid() ? Client->GetWeaponDA() : nullptr;
	const int32 Sel = Client.IsValid() ? Client->GetSelectedPart() : INDEX_NONE;
	const int32 StageIndex = GetSelectedStageIndex();
	if (DA && DA->WeaponParts.IsValidIndex(Sel) && DA->WeaponParts[Sel].Stages.IsValidIndex(StageIndex))
	{
		DA->WeaponParts[Sel].Stages[StageIndex].Trigger = (EFPSRPartStageTrigger)NewValue;
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
	if (DA && DA->WeaponParts.IsValidIndex(Sel) && DA->WeaponParts[Sel].Stages.IsValidIndex(StageIndex))
	{
		return (int32)DA->WeaponParts[Sel].Stages[StageIndex].StatAxis;
	}
	return (int32)EFPSRWeaponStat::FireRate;
}

void SFPSRWeaponAssemblerTab::OnSelectedStageStatAxisChanged(int32 NewValue, ESelectInfo::Type SelectInfo)
{
	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport.IsValid() ? Viewport->GetAssemblerClient() : nullptr;
	UFPSRWeaponDataAsset* DA = Client.IsValid() ? Client->GetWeaponDA() : nullptr;
	const int32 Sel = Client.IsValid() ? Client->GetSelectedPart() : INDEX_NONE;
	const int32 StageIndex = GetSelectedStageIndex();
	if (DA && DA->WeaponParts.IsValidIndex(Sel) && DA->WeaponParts[Sel].Stages.IsValidIndex(StageIndex))
	{
		DA->WeaponParts[Sel].Stages[StageIndex].StatAxis = (EFPSRWeaponStat)NewValue;
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
	if (DA && DA->WeaponParts.IsValidIndex(Sel) && DA->WeaponParts[Sel].Stages.IsValidIndex(StageIndex))
	{
		return (int32)DA->WeaponParts[Sel].Stages[StageIndex].StatCompare;
	}
	return (int32)EFPSRStatCompare::GreaterOrEqual;
}

void SFPSRWeaponAssemblerTab::OnSelectedStageStatCompareChanged(int32 NewValue, ESelectInfo::Type SelectInfo)
{
	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport.IsValid() ? Viewport->GetAssemblerClient() : nullptr;
	UFPSRWeaponDataAsset* DA = Client.IsValid() ? Client->GetWeaponDA() : nullptr;
	const int32 Sel = Client.IsValid() ? Client->GetSelectedPart() : INDEX_NONE;
	const int32 StageIndex = GetSelectedStageIndex();
	if (DA && DA->WeaponParts.IsValidIndex(Sel) && DA->WeaponParts[Sel].Stages.IsValidIndex(StageIndex))
	{
		DA->WeaponParts[Sel].Stages[StageIndex].StatCompare = (EFPSRStatCompare)NewValue;
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
	if (DA && DA->WeaponParts.IsValidIndex(Sel) && DA->WeaponParts[Sel].Stages.IsValidIndex(StageIndex))
	{
		return DA->WeaponParts[Sel].Stages[StageIndex].StatValue;
	}
	return 0.0f;
}

void SFPSRWeaponAssemblerTab::OnSelectedStageStatValueChanged(float NewValue)
{
	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport.IsValid() ? Viewport->GetAssemblerClient() : nullptr;
	UFPSRWeaponDataAsset* DA = Client.IsValid() ? Client->GetWeaponDA() : nullptr;
	const int32 Sel = Client.IsValid() ? Client->GetSelectedPart() : INDEX_NONE;
	const int32 StageIndex = GetSelectedStageIndex();
	if (DA && DA->WeaponParts.IsValidIndex(Sel) && DA->WeaponParts[Sel].Stages.IsValidIndex(StageIndex))
	{
		DA->WeaponParts[Sel].Stages[StageIndex].StatValue = NewValue;
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
	if (DA && DA->WeaponParts.IsValidIndex(Sel) && DA->WeaponParts[Sel].Stages.IsValidIndex(StageIndex))
	{
		return DA->WeaponParts[Sel].Stages[StageIndex].Scope.bScopeOverlay ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	}
	return ECheckBoxState::Unchecked;
}

void SFPSRWeaponAssemblerTab::OnSelectedStageScopeOverlayChanged(ECheckBoxState NewState)
{
	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport.IsValid() ? Viewport->GetAssemblerClient() : nullptr;
	UFPSRWeaponDataAsset* DA = Client.IsValid() ? Client->GetWeaponDA() : nullptr;
	const int32 Sel = Client.IsValid() ? Client->GetSelectedPart() : INDEX_NONE;
	const int32 StageIndex = GetSelectedStageIndex();
	if (DA && DA->WeaponParts.IsValidIndex(Sel) && DA->WeaponParts[Sel].Stages.IsValidIndex(StageIndex))
	{
		DA->WeaponParts[Sel].Stages[StageIndex].Scope.bScopeOverlay = (NewState == ECheckBoxState::Checked);
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
	if (DA && DA->WeaponParts.IsValidIndex(Sel) && DA->WeaponParts[Sel].Stages.IsValidIndex(StageIndex))
	{
		return DA->WeaponParts[Sel].Stages[StageIndex].Scope.AimFieldOfView;
	}
	return 0.0f;
}

void SFPSRWeaponAssemblerTab::OnSelectedStageAimFOVChanged(float NewValue)
{
	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport.IsValid() ? Viewport->GetAssemblerClient() : nullptr;
	UFPSRWeaponDataAsset* DA = Client.IsValid() ? Client->GetWeaponDA() : nullptr;
	const int32 Sel = Client.IsValid() ? Client->GetSelectedPart() : INDEX_NONE;
	const int32 StageIndex = GetSelectedStageIndex();
	if (DA && DA->WeaponParts.IsValidIndex(Sel) && DA->WeaponParts[Sel].Stages.IsValidIndex(StageIndex))
	{
		DA->WeaponParts[Sel].Stages[StageIndex].Scope.AimFieldOfView = NewValue;
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
	if (DA && DA->WeaponParts.IsValidIndex(Sel) && DA->WeaponParts[Sel].Stages.IsValidIndex(StageIndex))
	{
		// LoadSynchronous()는 내부적으로 Get()을 먼저 시도하므로 이미 로드돼 있으면 추가 로드 없이 반환된다.
		return DA->WeaponParts[Sel].Stages[StageIndex].Scope.ScopeOverlayWidgetClass.LoadSynchronous();
	}
	return nullptr;
}

void SFPSRWeaponAssemblerTab::OnSelectedStageScopeWidgetChanged(const UClass* NewClass)
{
	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport.IsValid() ? Viewport->GetAssemblerClient() : nullptr;
	UFPSRWeaponDataAsset* DA = Client.IsValid() ? Client->GetWeaponDA() : nullptr;
	const int32 Sel = Client.IsValid() ? Client->GetSelectedPart() : INDEX_NONE;
	const int32 StageIndex = GetSelectedStageIndex();
	if (DA && DA->WeaponParts.IsValidIndex(Sel) && DA->WeaponParts[Sel].Stages.IsValidIndex(StageIndex))
	{
		// NewClass가 nullptr이면(AllowNone으로 지운 경우) ScopeOverlayWidgetClass도 함께 정리된다.
		DA->WeaponParts[Sel].Stages[StageIndex].Scope.ScopeOverlayWidgetClass = NewClass;
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
	if (DA && DA->WeaponParts.IsValidIndex(Sel) && DA->WeaponParts[Sel].Stages.IsValidIndex(StageIndex))
	{
		return DA->WeaponParts[Sel].Stages[StageIndex].Scope.bScopeVignette ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	}
	return ECheckBoxState::Unchecked;
}

void SFPSRWeaponAssemblerTab::OnSelectedStageScopeVignetteChanged(ECheckBoxState NewState)
{
	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport.IsValid() ? Viewport->GetAssemblerClient() : nullptr;
	UFPSRWeaponDataAsset* DA = Client.IsValid() ? Client->GetWeaponDA() : nullptr;
	const int32 Sel = Client.IsValid() ? Client->GetSelectedPart() : INDEX_NONE;
	const int32 StageIndex = GetSelectedStageIndex();
	if (DA && DA->WeaponParts.IsValidIndex(Sel) && DA->WeaponParts[Sel].Stages.IsValidIndex(StageIndex))
	{
		DA->WeaponParts[Sel].Stages[StageIndex].Scope.bScopeVignette = (NewState == ECheckBoxState::Checked);
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
	if (DA && DA->WeaponParts.IsValidIndex(Sel) && DA->WeaponParts[Sel].Stages.IsValidIndex(StageIndex))
	{
		return DA->WeaponParts[Sel].Stages[StageIndex].Scope.bHideWeaponWhileScoped ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	}
	return ECheckBoxState::Unchecked;
}

void SFPSRWeaponAssemblerTab::OnSelectedStageHideWeaponChanged(ECheckBoxState NewState)
{
	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport.IsValid() ? Viewport->GetAssemblerClient() : nullptr;
	UFPSRWeaponDataAsset* DA = Client.IsValid() ? Client->GetWeaponDA() : nullptr;
	const int32 Sel = Client.IsValid() ? Client->GetSelectedPart() : INDEX_NONE;
	const int32 StageIndex = GetSelectedStageIndex();
	if (DA && DA->WeaponParts.IsValidIndex(Sel) && DA->WeaponParts[Sel].Stages.IsValidIndex(StageIndex))
	{
		DA->WeaponParts[Sel].Stages[StageIndex].Scope.bHideWeaponWhileScoped = (NewState == ECheckBoxState::Checked);
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
	if (DA && DA->WeaponParts.IsValidIndex(Sel) && DA->WeaponParts[Sel].Stages.IsValidIndex(StageIndex))
	{
		return DA->WeaponParts[Sel].Stages[StageIndex].Scope.bScopeOverlay ? EVisibility::Visible : EVisibility::Collapsed;
	}
	return EVisibility::Collapsed;
}

FReply SFPSRWeaponAssemblerTab::OnStageMoveUpClicked()
{
	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport.IsValid() ? Viewport->GetAssemblerClient() : nullptr;
	UFPSRWeaponDataAsset* DA = Client.IsValid() ? Client->GetWeaponDA() : nullptr;
	const int32 Sel = Client.IsValid() ? Client->GetSelectedPart() : INDEX_NONE;
	const int32 StageIndex = GetSelectedStageIndex();
	if (!DA || !DA->WeaponParts.IsValidIndex(Sel))
	{
		return FReply::Handled();
	}

	TArray<FFPSRWeaponPartStage>& Stages = DA->WeaponParts[Sel].Stages;
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
	if (!DA || !DA->WeaponParts.IsValidIndex(Sel))
	{
		return FReply::Handled();
	}

	TArray<FFPSRWeaponPartStage>& Stages = DA->WeaponParts[Sel].Stages;
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
	if (!DA || !DA->WeaponParts.IsValidIndex(Sel))
	{
		return false;
	}
	return StageIndex != INDEX_NONE && StageIndex < DA->WeaponParts[Sel].Stages.Num() - 1;
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
			StatusText->SetText(LOCTEXT("BakeNoBodyMesh", "이 무기 DA에 1인칭 스켈레탈 메시(WeaponMesh)가 없어 소켓을 구울 수 없습니다."));
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

TSharedPtr<FFPSRWeaponAssemblerViewportClient> SFPSRWeaponAssemblerTab::GetAssemblerViewportClient() const
{
	return Viewport.IsValid() ? Viewport->GetAssemblerClient() : nullptr;
}

FReply SFPSRWeaponAssemblerTab::OnOpenFirstPersonViewClicked()
{
	// 이미 열려 있으면 그 탭으로 포커스가 간다(TryInvokeTab 규약). 1인칭 탭은 열리면서 지금 살아 있는 조립기
	// 탭 — 즉 이 위젯 — 의 프리뷰 씬을 스스로 찾아 붙는다(FFPSRogueliteEditorModule::GetLiveWeaponAssemblerTab).
	FGlobalTabmanager::Get()->TryInvokeTab(FFPSRogueliteEditorModule::GetWeaponAssemblerFPTabName());

	// 팔이 꺼져 있으면 1인칭 창이 기준 없이 뜬다 — 여기서 미리 알려 주는 편이 저쪽에서 빈 화면을 보는 것보다 낫다.
	const TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = GetAssemblerViewportClient();
	if (StatusText.IsValid() && (!Client.IsValid() || !Client->IsShowArms()))
	{
		StatusText->SetText(LOCTEXT("FPViewNeedsArms", "1인칭 뷰를 열었습니다. 위의 '팔 보기'를 켜야 실제 화면과 같은 구도가 됩니다 — 구도는 팔을 기준으로 잡힙니다."));
	}
	return FReply::Handled();
}

void SFPSRWeaponAssemblerTab::OnShowArmsChanged(ECheckBoxState NewState)
{
	if (!Viewport.IsValid() || !Viewport->GetAssemblerClient().IsValid())
	{
		return;
	}
	const bool bWanted = NewState == ECheckBoxState::Checked;
	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport->GetAssemblerClient();
	Client->SetShowArms(bWanted);

	// 클라이언트가 거부할 수 있다(설정에 팔 메시가 없을 때). 체크박스는 IsShowArmsChecked 로 실제 상태를 되비추므로
	// 저절로 원복되지만, **왜** 안 켜졌는지는 여기서 알려 줘야 사용자가 설정을 찾아갈 수 있다.
	if (bWanted && !Client->IsShowArms() && StatusText.IsValid())
	{
		StatusText->SetText(LOCTEXT("ShowArmsNoMesh",
			"팔 프리뷰용 메시가 지정되지 않았습니다 — 바로 아래 '팔 메시'에서 지정하거나 프로젝트 설정 > FPSR > FPSR Weapon Assembler 의 'Preview Arms Mesh' 를 지정하세요."));
		return;
	}

	// 팔은 섰지만 뭔가 정상이 아닐 수 있다 — 예: 애니가 팔 스켈레톤과 안 맞음(배경 P4). 클라이언트가 남긴 사유를 그대로.
	const FString ArmsStatus = Client->IsShowArms() ? Client->GetArmsStatusMessage() : FString();
	if (!ArmsStatus.IsEmpty() && StatusText.IsValid())
	{
		StatusText->SetText(FText::FromString(ArmsStatus));
	}
}

ECheckBoxState SFPSRWeaponAssemblerTab::IsShowArmsChecked() const
{
	const bool bChecked = Viewport.IsValid() && Viewport->GetAssemblerClient().IsValid() && Viewport->GetAssemblerClient()->IsShowArms();
	return bChecked ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

bool SFPSRWeaponAssemblerTab::IsShowArmsEnabled() const
{
	return Viewport.IsValid() && Viewport->GetAssemblerClient().IsValid() && Viewport->GetAssemblerClient()->IsShowArms();
}

// ---------------------------------------------------------------------------------------------------------------
// 팔 메시/애니 피커 (C2) — UFPSRWeaponAssemblerSettings가 유일한 진실원천. 여기서 읽고 쓰고 SaveConfig 후 클라이언트에
// RefreshArmsFromSettings로 반영한다(새 저장소를 만들지 않는다).
// ---------------------------------------------------------------------------------------------------------------

FString SFPSRWeaponAssemblerTab::GetArmsMeshObjectPath() const
{
	return GetDefault<UFPSRWeaponAssemblerSettings>()->PreviewArmsMesh.ToString();
}

void SFPSRWeaponAssemblerTab::OnArmsMeshAssetChanged(const FAssetData& AssetData)
{
	UFPSRWeaponAssemblerSettings* Settings = GetMutableDefault<UFPSRWeaponAssemblerSettings>();
	Settings->PreviewArmsMesh = Cast<USkeletalMesh>(AssetData.GetAsset());
	const bool bSaved = Settings->TryUpdateDefaultConfigFile();

	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport.IsValid() ? Viewport->GetAssemblerClient() : nullptr;
	if (Client.IsValid())
	{
		Client->RefreshArmsFromSettings();
	}
	if (StatusText.IsValid())
	{
		// 메시가 바뀌면 지금 물린 애니가 더 이상 안 맞을 수 있다 — 클라이언트가 사유를 남겼으면 그걸 우선 보여준다.
		const FString ArmsStatus = Client.IsValid() ? Client->GetArmsStatusMessage() : FString();
		StatusText->SetText(!ArmsStatus.IsEmpty()
			? FText::FromString(ArmsStatus)
			: (bSaved
				? LOCTEXT("ArmsMeshChanged", "팔 메시 설정을 변경했습니다(Config/DefaultEditor.ini 에 저장됨).")
				: LOCTEXT("ArmsMeshChangedNotSaved", "팔 메시를 바꿨지만 Config/DefaultEditor.ini 에 저장하지 못했습니다 — 이번 세션에만 적용됩니다(파일 읽기 전용?).")));
	}
}

FString SFPSRWeaponAssemblerTab::GetArmsPoseObjectPath() const
{
	return GetDefault<UFPSRWeaponAssemblerSettings>()->PreviewArmsPose.ToString();
}

void SFPSRWeaponAssemblerTab::OnArmsPoseAssetChanged(const FAssetData& AssetData)
{
	UFPSRWeaponAssemblerSettings* Settings = GetMutableDefault<UFPSRWeaponAssemblerSettings>();
	Settings->PreviewArmsPose = Cast<UAnimationAsset>(AssetData.GetAsset());
	// 🚨 SaveConfig() 가 아니다 — 이 클래스는 UCLASS(Config=Editor, **DefaultConfig**) 라 값이 살아야 할 곳은
	// 체크인되는 Config/DefaultEditor.ini 이고, 거기 쓰는 API 는 TryUpdateDefaultConfigFile() 다.
	// SaveConfig() 는 CDO 메모리만 바꾸고 그 파일엔 아무것도 안 남겨서, 화면엔 바뀐 채로 보이다가 에디터를 껐다 켜면
	// 조용히 옛 값으로 돌아갔다(실제로 이번 세션에 겪음: 툴에선 Reload 가 돌고 있는데 ini 엔 Idle 이 그대로였다).
	// 반환값을 실제로 확인해서 보고한다 — "바꿨다"가 아니라 "저장됐다"가 핵심(BakeWeaponSocket 과 같은 규율).
	const bool bSaved = Settings->TryUpdateDefaultConfigFile();

	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport.IsValid() ? Viewport->GetAssemblerClient() : nullptr;
	if (Client.IsValid())
	{
		Client->RefreshArmsFromSettings();
	}
	if (StatusText.IsValid())
	{
		// 필터를 통과했어도(호환 스켈레톤) 클라이언트가 재생 자체에서 걸러낼 사유를 남겼을 수 있다 — 그게 우선.
		const FString ArmsStatus = Client.IsValid() ? Client->GetArmsStatusMessage() : FString();
		StatusText->SetText(!ArmsStatus.IsEmpty()
			? FText::FromString(ArmsStatus)
			: (bSaved
				? LOCTEXT("ArmsPoseChanged", "팔 애니 설정을 변경했습니다(Config/DefaultEditor.ini 에 저장됨).")
				: LOCTEXT("ArmsPoseChangedNotSaved", "팔 애니를 바꿨지만 Config/DefaultEditor.ini 에 저장하지 못했습니다 — 이번 세션에만 적용됩니다(파일 읽기 전용?).")));
	}
}

bool SFPSRWeaponAssemblerTab::OnShouldFilterArmsPoseAsset(const FAssetData& AssetData) const
{
	// "현재 설정된" 팔 메시 기준(계약 C2) — 라이브 ArmsComp가 아니라 설정값을 본다: 팔 보기가 꺼져 있어도(ArmsComp==
	// null이어도) 애니 피커는 항상 설정된 팔과 호환되는 것만 보여줘야 한다.
	const USkeletalMesh* ArmsMesh = GetDefault<UFPSRWeaponAssemblerSettings>()->PreviewArmsMesh.LoadSynchronous();
	if (!ArmsMesh)
	{
		return false; // 팔 메시가 없으면 걸러낼 기준이 없다 — 전부 통과.
	}
	const USkeleton* Skeleton = ArmsMesh->GetSkeleton();
	return Skeleton ? Skeleton->ShouldFilterAsset(AssetData) : false;
}

// ---------------------------------------------------------------------------------------------------------------
// 재생 컨트롤 (C6)
// ---------------------------------------------------------------------------------------------------------------

FReply SFPSRWeaponAssemblerTab::OnPreviewPlayPauseClicked()
{
	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport.IsValid() ? Viewport->GetAssemblerClient() : nullptr;
	if (Client.IsValid())
	{
		Client->SetPreviewPlaying(!Client->IsPreviewPlaying());
	}
	return FReply::Handled();
}

FText SFPSRWeaponAssemblerTab::GetPreviewPlayPauseLabel() const
{
	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport.IsValid() ? Viewport->GetAssemblerClient() : nullptr;
	const bool bPlaying = Client.IsValid() && Client->IsPreviewPlaying();
	return bPlaying ? LOCTEXT("PreviewPause", "⏸") : LOCTEXT("PreviewPlay", "▶");
}

float SFPSRWeaponAssemblerTab::GetPreviewSliderFraction() const
{
	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport.IsValid() ? Viewport->GetAssemblerClient() : nullptr;
	if (!Client.IsValid())
	{
		return 0.0f;
	}
	const float Length = Client->GetPreviewLength();
	return Length > 0.0f ? (Client->GetPreviewPosition() / Length) : 0.0f;
}

void SFPSRWeaponAssemblerTab::OnPreviewSliderFractionChanged(float NewFraction)
{
	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport.IsValid() ? Viewport->GetAssemblerClient() : nullptr;
	if (Client.IsValid())
	{
		Client->SetPreviewPosition(FMath::Clamp(NewFraction, 0.0f, 1.0f) * Client->GetPreviewLength());
	}
}

FText SFPSRWeaponAssemblerTab::GetPreviewTimeLabel() const
{
	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport.IsValid() ? Viewport->GetAssemblerClient() : nullptr;
	const float Position = Client.IsValid() ? Client->GetPreviewPosition() : 0.0f;
	const float Length = Client.IsValid() ? Client->GetPreviewLength() : 0.0f;
	return FText::FromString(FString::Printf(TEXT("%.2f / %.2f s"), Position, Length));
}

ECheckBoxState SFPSRWeaponAssemblerTab::IsPreviewLoopingChecked() const
{
	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport.IsValid() ? Viewport->GetAssemblerClient() : nullptr;
	const bool bLooping = Client.IsValid() && Client->IsPreviewLooping();
	return bLooping ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SFPSRWeaponAssemblerTab::OnPreviewLoopingChanged(ECheckBoxState NewState)
{
	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport.IsValid() ? Viewport->GetAssemblerClient() : nullptr;
	if (Client.IsValid())
	{
		Client->SetPreviewLooping(NewState == ECheckBoxState::Checked);
	}
}

bool SFPSRWeaponAssemblerTab::IsPreviewControlsEnabled() const
{
	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport.IsValid() ? Viewport->GetAssemblerClient() : nullptr;
	return Client.IsValid() && Client->GetPreviewLength() > 0.0f;
}

// ---------------------------------------------------------------------------------------------------------------
// 그립(= 구워질 값, 뼈 상대) 위치/회전 숫자 필드 (C4)
// ---------------------------------------------------------------------------------------------------------------

bool SFPSRWeaponAssemblerTab::HasGripFrame() const
{
	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport.IsValid() ? Viewport->GetAssemblerClient() : nullptr;
	return Client.IsValid() && Client->HasGripFrame();
}

// FVector/FRotator는 LWC(double) 저장이지만 SVectorInputBox/SRotatorInputBox는 float 어트리뷰트라 경계에서 명시적으로
// 좁힌다(읽기)/넓힌다(쓰기, float->double은 암시적으로 안전해 캐스트 불필요).

TOptional<float> SFPSRWeaponAssemblerTab::GetGripLocationX() const
{
	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport.IsValid() ? Viewport->GetAssemblerClient() : nullptr;
	return Client.IsValid() ? static_cast<float>(Client->GetGripTransform().GetLocation().X) : 0.0f;
}

TOptional<float> SFPSRWeaponAssemblerTab::GetGripLocationY() const
{
	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport.IsValid() ? Viewport->GetAssemblerClient() : nullptr;
	return Client.IsValid() ? static_cast<float>(Client->GetGripTransform().GetLocation().Y) : 0.0f;
}

TOptional<float> SFPSRWeaponAssemblerTab::GetGripLocationZ() const
{
	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport.IsValid() ? Viewport->GetAssemblerClient() : nullptr;
	return Client.IsValid() ? static_cast<float>(Client->GetGripTransform().GetLocation().Z) : 0.0f;
}

void SFPSRWeaponAssemblerTab::OnGripLocationXChanged(float NewValue)
{
	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport.IsValid() ? Viewport->GetAssemblerClient() : nullptr;
	if (!Client.IsValid())
	{
		return;
	}
	FTransform GripXf = Client->GetGripTransform();
	FVector Loc = GripXf.GetLocation();
	Loc.X = NewValue;
	GripXf.SetLocation(Loc);
	Client->SetGripTransform(GripXf);
}

void SFPSRWeaponAssemblerTab::OnGripLocationYChanged(float NewValue)
{
	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport.IsValid() ? Viewport->GetAssemblerClient() : nullptr;
	if (!Client.IsValid())
	{
		return;
	}
	FTransform GripXf = Client->GetGripTransform();
	FVector Loc = GripXf.GetLocation();
	Loc.Y = NewValue;
	GripXf.SetLocation(Loc);
	Client->SetGripTransform(GripXf);
}

void SFPSRWeaponAssemblerTab::OnGripLocationZChanged(float NewValue)
{
	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport.IsValid() ? Viewport->GetAssemblerClient() : nullptr;
	if (!Client.IsValid())
	{
		return;
	}
	FTransform GripXf = Client->GetGripTransform();
	FVector Loc = GripXf.GetLocation();
	Loc.Z = NewValue;
	GripXf.SetLocation(Loc);
	Client->SetGripTransform(GripXf);
}

TOptional<float> SFPSRWeaponAssemblerTab::GetGripRotationRoll() const
{
	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport.IsValid() ? Viewport->GetAssemblerClient() : nullptr;
	return Client.IsValid() ? static_cast<float>(Client->GetGripTransform().Rotator().Roll) : 0.0f;
}

TOptional<float> SFPSRWeaponAssemblerTab::GetGripRotationPitch() const
{
	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport.IsValid() ? Viewport->GetAssemblerClient() : nullptr;
	return Client.IsValid() ? static_cast<float>(Client->GetGripTransform().Rotator().Pitch) : 0.0f;
}

TOptional<float> SFPSRWeaponAssemblerTab::GetGripRotationYaw() const
{
	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport.IsValid() ? Viewport->GetAssemblerClient() : nullptr;
	return Client.IsValid() ? static_cast<float>(Client->GetGripTransform().Rotator().Yaw) : 0.0f;
}

void SFPSRWeaponAssemblerTab::OnGripRotationRollChanged(float NewValue)
{
	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport.IsValid() ? Viewport->GetAssemblerClient() : nullptr;
	if (!Client.IsValid())
	{
		return;
	}
	FTransform GripXf = Client->GetGripTransform();
	FRotator Rot = GripXf.Rotator();
	Rot.Roll = NewValue;
	GripXf.SetRotation(Rot.Quaternion());
	Client->SetGripTransform(GripXf);
}

void SFPSRWeaponAssemblerTab::OnGripRotationPitchChanged(float NewValue)
{
	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport.IsValid() ? Viewport->GetAssemblerClient() : nullptr;
	if (!Client.IsValid())
	{
		return;
	}
	FTransform GripXf = Client->GetGripTransform();
	FRotator Rot = GripXf.Rotator();
	Rot.Pitch = NewValue;
	GripXf.SetRotation(Rot.Quaternion());
	Client->SetGripTransform(GripXf);
}

void SFPSRWeaponAssemblerTab::OnGripRotationYawChanged(float NewValue)
{
	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport.IsValid() ? Viewport->GetAssemblerClient() : nullptr;
	if (!Client.IsValid())
	{
		return;
	}
	FTransform GripXf = Client->GetGripTransform();
	FRotator Rot = GripXf.Rotator();
	Rot.Yaw = NewValue;
	GripXf.SetRotation(Rot.Quaternion());
	Client->SetGripTransform(GripXf);
}

// ---------------------------------------------------------------------------------------------------------------
// 뼈 피커 (SBoneSelectionWidget, C5)
// ---------------------------------------------------------------------------------------------------------------

const FReferenceSkeleton& SFPSRWeaponAssemblerTab::GetArmsReferenceSkeletonForBonePicker() const
{
	// SBoneSelectionWidget의 OnGetReferenceSkeleton은 참조를 요구한다 — 팔이 없을 때 돌려줄 안정된(수명이 함수 밖까지
	// 사는) 빈 스켈레톤이 필요해 static local로 둔다.
	static const FReferenceSkeleton EmptyRefSkeleton;

	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport.IsValid() ? Viewport->GetAssemblerClient() : nullptr;
	const USkeletalMeshComponent* ArmsComp = Client.IsValid() ? Client->GetArmsComp() : nullptr;
	const USkeletalMesh* ArmsMesh = ArmsComp ? ArmsComp->GetSkeletalMeshAsset() : nullptr;
	return ArmsMesh ? ArmsMesh->GetRefSkeleton() : EmptyRefSkeleton;
}

FName SFPSRWeaponAssemblerTab::GetGripBoneForBonePicker(bool& bMultipleValues) const
{
	bMultipleValues = false;
	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport.IsValid() ? Viewport->GetAssemblerClient() : nullptr;
	return Client.IsValid() ? Client->GetGripBone() : NAME_None;
}

void SFPSRWeaponAssemblerTab::OnGripBonePicked(FName NewBone)
{
	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport.IsValid() ? Viewport->GetAssemblerClient() : nullptr;
	if (Client.IsValid())
	{
		Client->SetGripBone(NewBone);
	}
}

// ---------------------------------------------------------------------------------------------------------------
// 손 소켓 이름 + 공용/전용 배지 (C3, 🚨 공용 기본 소켓 사고 방지)
// ---------------------------------------------------------------------------------------------------------------

void SFPSRWeaponAssemblerTab::RefreshGripSocketNameBox()
{
	if (!GripSocketNameBox.IsValid())
	{
		return;
	}
	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport.IsValid() ? Viewport->GetAssemblerClient() : nullptr;
	const FName Resolved = Client.IsValid() ? Client->GetResolvedAttachSocketName() : NAME_None;
	GripSocketNameBox->SetText(FText::FromName(Resolved));
}

FText SFPSRWeaponAssemblerTab::GetGripSocketBadgeText() const
{
	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport.IsValid() ? Viewport->GetAssemblerClient() : nullptr;
	if (!Client.IsValid())
	{
		return FText::GetEmpty();
	}
	return Client->IsUsingSharedDefaultSocket()
		? LOCTEXT("SharedDefaultSocketBadge", "[공용 기본]")
		: LOCTEXT("DedicatedSocketBadge", "[이 무기 전용]");
}

FSlateColor SFPSRWeaponAssemblerTab::GetGripSocketBadgeColor() const
{
	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport.IsValid() ? Viewport->GetAssemblerClient() : nullptr;
	const bool bShared = Client.IsValid() && Client->IsUsingSharedDefaultSocket();
	// 경고색(주황) — 공용 기본을 고치면 전용 소켓이 없는 무기 전부가 함께 움직인다는 신호(계약 C3).
	return bShared ? FSlateColor(FLinearColor(1.0f, 0.55f, 0.05f)) : FSlateColor::UseForeground();
}

FText SFPSRWeaponAssemblerTab::GetGripSocketWarningText() const
{
	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport.IsValid() ? Viewport->GetAssemblerClient() : nullptr;
	if (Client.IsValid() && Client->IsUsingSharedDefaultSocket())
	{
		return LOCTEXT("SharedDefaultSocketWarning",
			"이 소켓은 전용 소켓이 없는 무기 전부가 함께 씁니다 — 여기서 손 위치를 저장하면 Knife·Unarmed 등도 함께 움직입니다.");
	}
	return FText::GetEmpty();
}

EVisibility SFPSRWeaponAssemblerTab::GetGripSocketWarningVisibility() const
{
	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport.IsValid() ? Viewport->GetAssemblerClient() : nullptr;
	return (Client.IsValid() && Client->IsUsingSharedDefaultSocket()) ? EVisibility::Visible : EVisibility::Collapsed;
}

FReply SFPSRWeaponAssemblerTab::OnResetGripClicked()
{
	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport.IsValid() ? Viewport->GetAssemblerClient() : nullptr;
	if (!Client.IsValid() || !Client->HasGripFrame())
	{
		return FReply::Handled();
	}

	// 항등 = "무기 원점을 기준 뼈에 정확히 겹친다". 위치/회전 칸은 뼈 상대값이라 이게 곧 그 칸들을 전부 0으로 만드는
	// 것과 같다. 저장은 하지 않는다 — 여기서부터 기즈모로 잡고 '손 위치 저장'을 눌러야 소켓에 구워진다.
	Client->SetGripTransform(FTransform::Identity);

	if (StatusText.IsValid())
	{
		StatusText->SetText(FText::Format(
			LOCTEXT("ResetGripDone", "그립을 기준 뼈 '{0}' 위치로 되돌렸습니다(위치·회전 0). 여기서 잡은 뒤 '손 위치 저장'을 누르세요."),
			FText::FromName(Client->GetGripBone())));
	}
	return FReply::Handled();
}

FReply SFPSRWeaponAssemblerTab::OnMakeDedicatedSocketClicked()
{
	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport.IsValid() ? Viewport->GetAssemblerClient() : nullptr;
	if (!Client.IsValid() || !GripSocketNameBox.IsValid() || GripSocketNameBox->GetText().IsEmptyOrWhitespace())
	{
		return FReply::Handled();
	}

	const FName NewSocketName(*GripSocketNameBox->GetText().ToString());
	Client->SetWeaponAttachSocketName(NewSocketName);
	RefreshGripSocketNameBox();

	if (StatusText.IsValid())
	{
		StatusText->SetText(FText::Format(
			LOCTEXT("MakeDedicatedSocketDone", "'{0}' 을(를) 이 무기 전용 소켓으로 지정했습니다(인메모리). '손 위치 저장'을 눌러야 실제로 구워집니다."),
			FText::FromName(NewSocketName)));
	}
	return FReply::Handled();
}

bool SFPSRWeaponAssemblerTab::IsMakeDedicatedSocketEnabled() const
{
	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport.IsValid() ? Viewport->GetAssemblerClient() : nullptr;
	if (!Client.IsValid() || !Client->GetWeaponDA() || !Client->IsUsingSharedDefaultSocket())
	{
		return false;
	}
	return GripSocketNameBox.IsValid() && !GripSocketNameBox->GetText().IsEmptyOrWhitespace();
}

FReply SFPSRWeaponAssemblerTab::OnBakeHandClicked()
{
	if (!Viewport.IsValid() || !Viewport->GetAssemblerClient().IsValid())
	{
		return FReply::Handled();
	}
	TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Viewport->GetAssemblerClient();

	// 🚨 C3: 전용 소켓이 없으면 이건 캐릭터의 공용 기본 소켓(SOCKET_Weapon)을 고치는 것이다 — 전용 소켓이 없는 무기
	// 전부(Knife·Unarmed 등)가 함께 움직인다. 되돌리기 번거로운 실수라 한 번 더 확인한다. 전용 소켓이면 묻지 않는다.
	if (Client->IsUsingSharedDefaultSocket())
	{
		const EAppReturnType::Type Choice = FMessageDialog::Open(EAppMsgType::YesNo, LOCTEXT("BakeHandSharedSocketWarning",
			"지금 저장하면 전용 소켓이 없는 무기 전부가 함께 쓰는 '공용 기본' 소켓이 바뀝니다(Knife·Unarmed 등도 함께 움직입니다).\n\n계속하시겠습니까?"));
		if (Choice != EAppReturnType::Yes)
		{
			if (StatusText.IsValid())
			{
				StatusText->SetText(LOCTEXT("BakeHandCancelled", "손 위치 저장을 취소했습니다(공용 기본 소켓)."));
			}
			return FReply::Handled();
		}
	}

	// 🚨 굽기 전 그립을 떠 둔다 — **베이크는 무기를 움직여선 안 된다**는 불변식을 아래에서 직접 강제하기 위해서다.
	// 없으면 저장을 누를 때마다 값이 델타만큼 누적된다(사용자 발견): 바디는 지금 굽는 그 소켓의 자식이라, 소켓 값이
	// 바뀌는 순간 부모가 움직인 셈이 되어 바디도 같은 만큼 끌려간다. 그 끌려간 위치를 다음 베이크가 다시 구우므로
	// 누를 때마다 델타가 더해진다(실측: 소켓 -17 에서 -12 로 구웠더니 그립이 -7 = 2×(-12) − (-17) 로 튀었다).
	const FTransform GripBeforeBake = Client->GetGripTransform();

	const FPSRWeaponAssemblerHelpers::FBakeHandResult Result = FPSRWeaponAssemblerHelpers::BakeWeaponSocket(
		Client->GetArmsComp(), Client->GetBodyComp(), Client->GetResolvedAttachSocketName(), Client->GetGripBone());

	if (Result.bOk)
	{
		// 구운 값 그대로 그립을 다시 세운다. 부착 대상이 소켓이든 뼈든 상관없이 성립한다 — SetGripTransform 은 뼈
		// 기준으로 월드를 잡으므로, 소켓에 붙어 있으면 소켓-상대가 항등이 되고(끌림 상쇄) 뼈에 붙어 있으면 원래
		// 자리를 그대로 유지한다. 그래서 재베이크가 멱등이 되고, 그립 숫자도 방금 구운 소켓 값과 일치한다.
		Client->SetGripTransform(GripBeforeBake);
	}

	if (StatusText.IsValid())
	{
		if (Result.bOk)
		{
			// "소켓을 만들었다"가 아니라 "무엇이 저장됐다"가 핵심(계약 C7, 불변식 I-E) — 실제로 저장에 성공한 패키지를
			// 그대로 나열한다. FString::Join이 이 엔진 버전엔 없어(확인함) 직접 이어붙인다.
			FString SavedList;
			for (const FString& Pkg : Result.SavedPackages)
			{
				if (!SavedList.IsEmpty())
				{
					SavedList += TEXT(", ");
				}
				SavedList += Pkg;
			}
			if (SavedList.IsEmpty())
			{
				SavedList = TEXT("(저장된 패키지 없음)");
			}

			// 어느 애셋에 저장됐는지는 Result.Message 가 이미 정확히 말한다(팔 메시일 수도, 팔 스켈레톤일 수도 있다 —
			// 이 팩의 SOCKET_Weapon 은 스켈레톤에 있다). 여기서 "팔 메시"라고 못박으면 그 문장과 정면으로 어긋나므로,
			// 이 줄은 "무기 DA 가 아니라 팔 쪽"이라는 사실과 3인칭 미변경만 덧붙인다.
			StatusText->SetText(FText::Format(
				LOCTEXT("BakeHandDone", "{0} 저장된 패키지: {1}. ⚠ 저장 대상은 무기 DA 가 아니라 '팔 쪽' 애셋입니다(3인칭 바디 소켓은 건드리지 않습니다)."),
				FText::FromString(Result.Message), FText::FromString(SavedList)));
		}
		else
		{
			StatusText->SetText(FText::Format(LOCTEXT("BakeHandFail", "손 위치 저장 실패 — {0}"), FText::FromString(Result.Message)));
		}
	}
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
