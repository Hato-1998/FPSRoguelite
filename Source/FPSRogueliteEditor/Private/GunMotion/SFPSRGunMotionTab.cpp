// Copyright Epic Games, Inc. All Rights Reserved.

#include "GunMotion/SFPSRGunMotionTab.h"

#include "GunMotion/FPSRGunMotionSettings.h"
#include "GunMotion/FPSRGunMotionBaker.h"
#include "GunMotion/FPSRGunMotionViewportClient.h"
#include "GunMotion/SFPSRGunMotionViewport.h"
#include "GunMotion/SFPSRGunMotionTimeline.h"   // 증보 v2.3 §16 — 기존 SSlider + 키 마커 줄을 대체하는 타임라인 위젯
#include "GunMotion/FPSRGunMotionChannelId.h"   // v3 §20-3: 채널 id/종류
#include "Anim/FPSRGunMotionAuthoringData.h"
#include "Anim/FPSRGunMotionCurveNames.h"   // v3 §20-2 몽타주 굽기 보고 문구가 참조하는 규약 확인용(직접 커브명 리터럴은 안 씀)
#include "Weapon/FPSRWeaponDataAsset.h"   // FFPSRWeaponPartAttachment — §20-3 파츠 레인/부착 드롭다운
#include "Core/FPSRLogChannels.h"
#include "Hero/FPSRCharacter.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimData/IAnimationDataController.h"   // v3 §20-5: [새 액션 클립] — InitializeModel/SetFrameRate/SetNumberOfFrames
#include "Animation/AnimCurveTypes.h"   // FFloatCurve — §14 마지막: [총 고정화]의 LeftHandIKWeight 커브 존재 확인
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"   // §14 라이브 미러 — Montage_* 계열이 받는 UAnimMontage 완전 타입
#include "Camera/CameraComponent.h"   // §11 [PIE 구도 캡처] — FirstPersonCamera::GetCameraView
#include "Camera/CameraTypes.h"       // FMinimalViewInfo
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"      // v3 §20-5: 팔 스켈레톤 → USkeleton
#include "GameFramework/PlayerController.h"

#include "AdvancedPreviewScene.h"   // §7: 이 탭이 단독 소유하는 프리뷰 씬
#include "PreviewScene.h"          // FPreviewScene::ConstructionValues
#include "Editor.h"
#include "FileHelpers.h"   // UEditorLoadingAndSavingUtils::SavePackages (UnrealEd) — 이 프로젝트가 기존에 EditorScriptingUtilities
                            // 플러그인(비활성)을 피해 온 관례를 그대로 따른다(FPSRWeaponAssemblerHelpers.cpp 주석 참조).
                            // 스펙 §4-5 는 UEditorAssetLibrary::SaveLoadedAsset 을 지목하지만 그 플러그인은 이
                            // 프로젝트에서 비활성 상태다 — 대신 이미 검증된 이 경로로 대체했다(보고서에 명시).
#include "Misc/MessageDialog.h"
#include "Misc/PackageName.h"      // v3 §20-5: LongPackageNameToFilename
#include "ScopedTransaction.h"
#include "UObject/Package.h"       // v3 §20-5: CreatePackage
#include "UObject/SavePackage.h"   // v3 §20-5: FSavePackageArgs

#include "PropertyEditorModule.h"
#include "PropertyCustomizationHelpers.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"   // v3 §20-5: FAssetRegistryModule::AssetCreated
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"           // v3 §20-5: [새 액션 클립] 모달 다이얼로그
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"   // §12 자유시점 체크박스
#include "Widgets/Input/SComboBox.h"   // v3 §20-3: 부착 드롭다운
#include "Widgets/Input/SEditableTextBox.h"   // v3 §20-5: 새 액션 클립 다이얼로그 이름 입력
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Layout/SBorder.h"   // §16 숫자 목록 행 선택 강조(BuildKeyRow)
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SExpandableArea.h"   // v3 §20-3: 레거시(v2) 접힘 섹션
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/AppStyle.h"   // §16 행 강조에 쓰는 WhiteBrush

#define LOCTEXT_NAMESPACE "SFPSRGunMotionTab"

namespace
{
	/** PIE 재생(§4-6)이 FirstPersonArms 를, [PIE 구도 캡처](§11)가 FirstPersonArms/FirstPersonCamera 를 찾는 데
	 *  쓴다 — AFPSRCharacter 의 그 멤버들은 protected 라 직접 접근이 안 되므로, GetCamToCompRotation 과 같은 방식
	 *  (설정의 컴포넌트 이름으로 조회)을 재사용한다. 템플릿화한 이유 = §11 에서 USkeletalMeshComponent(팔) 뿐 아니라
	 *  UCameraComponent(카메라)도 같은 방식으로 찾아야 하기 때문. */
	template <typename TComponent>
	TComponent* FindComponentByName(AActor* Actor, FName ComponentName)
	{
		if (!Actor)
		{
			return nullptr;
		}
		TArray<TComponent*> Components;
		Actor->GetComponents<TComponent>(Components);
		for (TComponent* Comp : Components)
		{
			if (Comp && Comp->GetFName() == ComponentName)
			{
				return Comp;
			}
		}
		return nullptr;
	}

	/** 1프레임 길이(초) — §9 "같은 시각(±1프레임)의 키" 판정 허용오차. 데이터 모델을 못 읽으면 30fps 로 폴백한다. */
	float FrameToleranceSeconds(const UAnimSequence* Seq)
	{
		const IAnimationDataModel* Model = Seq ? Seq->GetDataModel() : nullptr;
		const double Fps = Model ? Model->GetFrameRate().AsDecimal() : 0.0;
		return (Fps > 0.0) ? static_cast<float>(1.0 / Fps) : (1.0f / 30.0f);
	}

	/** §14 마지막: [총 고정화] 직후 클립에 CurveName 이름의 플로트 커브가 있는지 — 없으면 경고 대상
	 *  (슬롯 재생은 클립 자신의 커브만 흐르므로, 정식 몽타주에만 실린 IK 해제 커브는 미러에 안 실린다). */
	bool HasFloatCurveNamed(const UAnimSequence* Seq, FName CurveName)
	{
		const IAnimationDataModel* Model = Seq ? Seq->GetDataModel() : nullptr;
		if (!Model || CurveName.IsNone())
		{
			return false;
		}
		for (const FFloatCurve& Curve : Model->GetFloatCurves())
		{
			if (Curve.GetName() == CurveName)
			{
				return true;
			}
		}
		return false;
	}

	/** v3 §20-5 [새 액션 클립] 입력 다이얼로그 — 엔진 SDlgPickPath(Editor/UnrealEd/Public/Dialogs/DlgPickPath.h) 와
	 *  같은 최소 패턴(SWindow 서브클래스 + GEditor->EditorAddModalWindow + UserResponse 필드)을 그대로 축소 적용한
	 *  것 — 이름/길이(초) 두 필드만 받는다. */
	class SFPSRGunMotionNewClipDialog : public SWindow
	{
	public:
		SLATE_BEGIN_ARGS(SFPSRGunMotionNewClipDialog) {}
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			UserResponse = EAppReturnType::Cancel;
			AssetName = TEXT("AM_NewGunMotion_GunMotion");
			LengthSeconds = 1.0f;

			SWindow::Construct(SWindow::FArguments()
				.Title(NSLOCTEXT("SFPSRGunMotionTab", "NewClipDialogTitle", "새 액션 클립"))
				.SizingRule(ESizingRule::Autosized)
				.SupportsMinimize(false)
				.SupportsMaximize(false)
				[
					SNew(SVerticalBox)

					+ SVerticalBox::Slot().AutoHeight().Padding(8.0f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 4.0f, 0.0f)
						[
							SNew(STextBlock).Text(NSLOCTEXT("SFPSRGunMotionTab", "NewClipNameLabel", "에셋 이름"))
						]
						+ SHorizontalBox::Slot().FillWidth(1.0f)
						[
							SNew(SEditableTextBox)
							.Text(FText::FromString(AssetName))
							.OnTextChanged_Lambda([this](const FText& NewText) { AssetName = NewText.ToString(); })
						]
					]

					+ SVerticalBox::Slot().AutoHeight().Padding(8.0f, 0.0f, 8.0f, 8.0f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 4.0f, 0.0f)
						[
							SNew(STextBlock).Text(NSLOCTEXT("SFPSRGunMotionTab", "NewClipLengthLabel", "길이(초)"))
						]
						+ SHorizontalBox::Slot().FillWidth(1.0f)
						[
							SNew(SNumericEntryBox<float>)
							.Value_Lambda([this]() -> TOptional<float> { return LengthSeconds; })
							.OnValueChanged_Lambda([this](float V) { LengthSeconds = V; })
						]
					]

					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(8.0f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
						[
							SNew(SButton)
							.Text(NSLOCTEXT("SFPSRGunMotionTab", "NewClipOk", "생성"))
							.OnClicked(this, &SFPSRGunMotionNewClipDialog::OnButtonClick, EAppReturnType::Ok)
						]
						+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
						[
							SNew(SButton)
							.Text(NSLOCTEXT("SFPSRGunMotionTab", "NewClipCancel", "취소"))
							.OnClicked(this, &SFPSRGunMotionNewClipDialog::OnButtonClick, EAppReturnType::Cancel)
						]
					]
				]);
		}

		EAppReturnType::Type ShowModal()
		{
			GEditor->EditorAddModalWindow(SharedThis(this));
			return UserResponse;
		}

		const FString& GetAssetName() const { return AssetName; }
		float GetLengthSeconds() const { return LengthSeconds; }

	private:
		FReply OnButtonClick(EAppReturnType::Type ButtonID)
		{
			UserResponse = ButtonID;
			RequestDestroyWindow();
			return FReply::Handled();
		}

		EAppReturnType::Type UserResponse = EAppReturnType::Cancel;
		FString AssetName;
		float LengthSeconds = 1.0f;
	};
}

void SFPSRGunMotionTab::Construct(const FArguments& InArgs)
{
	// §7: 이 탭이 단독 소유하는 프리뷰 씬 — 어셈블러 씬과 공유하지 않는다(저작 대상이 무기 조립이 아니라 클립이라
	// 공유할 이유가 없다. SFPSRWeaponAssemblerFPTab 의 공동소유/약참조 수명주기 지뢰도 여기엔 없다).
	PreviewScene = MakeShared<FAdvancedPreviewScene>(FPreviewScene::ConstructionValues());

	ChildSlot
	[
		SNew(SVerticalBox)

		// --- §7 뷰포트 ---
		+ SVerticalBox::Slot().AutoHeight().Padding(2.0f)
		[
			SNew(SBox).HeightOverride(360.0f)
			[
				SAssignNew(Viewport, SFPSRGunMotionViewport, PreviewScene.ToSharedRef())
			]
		]

		// --- §16 타임라인: 재생/정지 + 룰러/플레이헤드/키 다이아몬드 위젯(기존 스크럽 슬라이더 + §8 키 마커 줄 대체) ---
		+ SVerticalBox::Slot().AutoHeight().Padding(2.0f, 4.0f, 2.0f, 0.0f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				SNew(SButton)
				.Text(this, &SFPSRGunMotionTab::GetPlayPauseLabel)
				.OnClicked(this, &SFPSRGunMotionTab::OnPlayPauseClicked)
			]

			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				SAssignNew(Timeline, SFPSRGunMotionTimeline)
				.SequenceLength(this, &SFPSRGunMotionTab::GetTimelineSequenceLength)
				.FrameRate(this, &SFPSRGunMotionTab::GetTimelineFrameRate)
				.ScrubPosition(this, &SFPSRGunMotionTab::GetScrubPosition)
				.Lanes(this, &SFPSRGunMotionTab::GetTimelineLanes)
				.ActiveChannelId(this, &SFPSRGunMotionTab::GetActiveChannelId)
				.SelectedKeyIndex(this, &SFPSRGunMotionTab::GetSelectedKeyIndex)
				.OnScrubPositionChanged(this, &SFPSRGunMotionTab::OnScrubPositionChanged)
				.OnScrubCaptureBegin(this, &SFPSRGunMotionTab::OnScrubCaptureBegin)
				.OnChannelSelected(this, &SFPSRGunMotionTab::OnTimelineChannelSelected)
				.OnKeySelected(this, &SFPSRGunMotionTab::OnTimelineChannelKeySelected)
				.OnKeyTimeCommitted(this, &SFPSRGunMotionTab::OnTimelineChannelKeyTimeCommitted)
				.OnKeyDeleteRequested(this, &SFPSRGunMotionTab::OnTimelineChannelKeyDeleteRequested)
				.OnKeyAddRequested(this, &SFPSRGunMotionTab::OnTimelineChannelKeyAddRequested)
			]
		]

		// --- §9 기즈모 툴바: 이동/회전 + 현재 시각에 키/삭제 ---
		+ SVerticalBox::Slot().AutoHeight().Padding(2.0f, 0.0f, 2.0f, 4.0f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 2.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("GizmoMoveMode", "이동"))
				.ToolTipText(LOCTEXT("GizmoMoveModeTooltip", "기즈모를 이동 모드로 전환합니다."))
				.OnClicked(this, &SFPSRGunMotionTab::OnTranslateModeClicked)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 8.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("GizmoRotateMode", "회전"))
				.ToolTipText(LOCTEXT("GizmoRotateModeTooltip", "기즈모를 회전 모드로 전환합니다."))
				.OnClicked(this, &SFPSRGunMotionTab::OnRotateModeClicked)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 2.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("KeyAtCurrentTime", "현재 시각에 키"))
				.ToolTipText(LOCTEXT("KeyAtCurrentTimeTooltip", "지금 스크럽 시각에 키가 없으면 새로 추가합니다."))
				.OnClicked(this, &SFPSRGunMotionTab::OnKeyAtCurrentTimeClicked)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 8.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("DeleteKeyAtCurrentTime", "키 삭제"))
				.ToolTipText(LOCTEXT("DeleteKeyAtCurrentTimeTooltip", "지금 스크럽 시각에 가장 가까운 키(±1프레임)를 삭제합니다."))
				.OnClicked(this, &SFPSRGunMotionTab::OnDeleteKeyAtCurrentTimeClicked)
			]

			// --- 증보 v2.1 §11: [PIE 구도 캡처] ---
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 2.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("CaptureCompositionButton", "PIE 구도 캡처"))
				.ToolTipText(LOCTEXT("CaptureCompositionTooltip", "PIE 의 로컬 캐릭터에서 카메라-팔 구도를 실측해 저장하고 뷰포트에 즉시 반영합니다."))
				.OnClicked(this, &SFPSRGunMotionTab::OnCaptureCompositionClicked)
			]

			// --- 증보 v2.1 §12: 자유시점 토글(뷰포트 툴바 체크박스 + 단축키 F) ---
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SCheckBox)
				.IsChecked(this, &SFPSRGunMotionTab::GetFreeLookState)
				.OnCheckStateChanged(this, &SFPSRGunMotionTab::OnFreeLookStateChanged)
				.ToolTipText(LOCTEXT("FreeLookTooltip", "켜면 뷰포트가 표준 에디터 네비게이션(회전/이동/줌)으로 바뀝니다 — 단축키 F. 꺼지면 저장된 구도로 스냅합니다."))
				[
					SNew(STextBlock).Text(LOCTEXT("FreeLookLabel", "자유시점 (F)"))
				]
			]

			// --- 증보 v2.2 §14: [PIE 라이브 링크] 토글 ---
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SCheckBox)
				.IsChecked(this, &SFPSRGunMotionTab::GetPIELiveLinkState)
				.OnCheckStateChanged(this, &SFPSRGunMotionTab::OnPIELiveLinkStateChanged)
				.ToolTipText(LOCTEXT("PIELiveLinkTooltip", "켜면 PIE 의 로컬 1인칭 팔이 이 탭의 저작 포즈(스크럽/재생/재굽기)를 실시간으로 미러링합니다."))
				[
					SNew(STextBlock).Text(LOCTEXT("PIELiveLinkLabel", "PIE 라이브 링크"))
				]
			]
		]

		// --- §11 상태줄: 지금 구도가 캡처값인지 폴백인지 ---
		+ SVerticalBox::Slot().AutoHeight().Padding(2.0f, 0.0f, 2.0f, 4.0f)
		[
			SAssignNew(CompositionStatusText, STextBlock)
			.Text(this, &SFPSRGunMotionTab::GetCompositionSourceText)
		]

		// --- 증보 v2.2 §14 상태줄: PIE 라이브 링크 상태("PIE 링크 활성" / "PIE 없음") ---
		+ SVerticalBox::Slot().AutoHeight().Padding(2.0f, 0.0f, 2.0f, 4.0f)
		[
			SNew(STextBlock)
			.Text(this, &SFPSRGunMotionTab::GetPIELiveLinkStatusText)
		]

		// --- 기존 UI(§4) 그대로 — 숫자 키 목록은 미세조정용으로 유지, 뷰포트 기즈모와 같은 Keys 데이터를 편집한다 ---
		+ SVerticalBox::Slot().FillHeight(1.0f)
		[
			SNew(SScrollBox)

			+ SScrollBox::Slot().Padding(4.0f)
			[
				SNew(SVerticalBox)

				// --- 1. 클립 선택(§4-1) ---
			+ SVerticalBox::Slot().AutoHeight().Padding(2.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 4.0f, 0.0f)
				[
					SNew(STextBlock).Text(LOCTEXT("ClipPrompt", "클립:"))
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f)
				[
					SNew(SObjectPropertyEntryBox)
					.AllowedClass(UAnimSequence::StaticClass())
					.ObjectPath(this, &SFPSRGunMotionTab::GetSequenceObjectPath)
					.OnObjectChanged(this, &SFPSRGunMotionTab::OnSequenceChanged)
					.AllowClear(true)
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(4.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("NewActionClipButton", "새 액션 클립"))
					.ToolTipText(LOCTEXT("NewActionClipTooltip", "본 트랙 0개(델타 0 = Idle 그대로)인 새 애디티브 클립을 만들고 엽니다(§20-5)."))
					.OnClicked(this, &SFPSRGunMotionTab::OnCreateNewActionClipClicked)
				]
			]

			// --- 2. 상태 줄(§4-2) ---
			+ SVerticalBox::Slot().AutoHeight().Padding(2.0f, 4.0f)
			[
				SAssignNew(StatusText, STextBlock)
				.Text(this, &SFPSRGunMotionTab::GetStatusText)
			]

			// --- v3 §20-3: 활성 채널 저작(비애디티브 클립이면 비활성, §4-2 규약 그대로) ---
			+ SVerticalBox::Slot().AutoHeight().Padding(2.0f)
			[
				SNew(SVerticalBox)
				.IsEnabled(this, &SFPSRGunMotionTab::IsClipAdditive)

				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f, 0.0f, 4.0f)
				[
					SNew(STextBlock)
					.Text(this, &SFPSRGunMotionTab::GetActiveChannelHeaderText)
				]

				// --- 부착 드롭다운(§20-3, 손 채널에서만 표시) ---
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
				[
					SNew(SBox)
					.Visibility(this, &SFPSRGunMotionTab::GetAttachRowVisibility)
					[
						BuildAttachComboRow()
					]
				]

				// --- 활성 채널 숫자 키 목록(loc/rot 6열 또는 Blend Time/Value 2열, §20-3) ---
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 6.0f, 0.0f, 2.0f)
				[
					SAssignNew(ChannelKeyListContainer, SVerticalBox)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("AddChannelKeyButton", "+ 채널 키 추가(현재 시각)"))
					.OnClicked(this, &SFPSRGunMotionTab::OnAddChannelKeyClicked)
				]

				// --- [채널 커브 굽기](§20-2: 본 트랙 대체 — 클립+몽타주 양쪽, A17 자동화) ---
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 8.0f, 0.0f, 2.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("BakeCurveChannelsButton", "채널 커브 굽기 (클립 + 몽타주)"))
					.OnClicked(this, &SFPSRGunMotionTab::OnBakeCurveChannelsClicked)
				]
			]

			// --- 레거시(v2): hand_r 본 트랙 베이크 — 접힘 섹션(§20-2 "UI 에서 레거시(v2) 접힘 섹션으로 이동") ---
			+ SVerticalBox::Slot().AutoHeight().Padding(2.0f, 8.0f, 2.0f, 2.0f)
			[
				SNew(SExpandableArea)
				.InitiallyCollapsed(true)
				.AreaTitle(LOCTEXT("LegacySectionHeader", "레거시(v2) — hand_r 본 트랙 베이크"))
				.BodyContent()
				[
				SNew(SVerticalBox)
				.IsEnabled(this, &SFPSRGunMotionTab::IsClipAdditive)

				// --- 3. [총 고정화] ---
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("SanitizeButton", "총 고정화"))
					.OnClicked(this, &SFPSRGunMotionTab::OnSanitizeClicked)
				]

				// --- "풀 오프셋" 입력(§3-3 기본 템플릿이 읽는 값) + [기본 템플릿] ---
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 6.0f, 0.0f, 2.0f)
				[
					SNew(STextBlock).Text(LOCTEXT("FullOffsetHeader", "풀 오프셋 (기본 템플릿용)"))
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
				[
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
					[
						SNew(SBox).WidthOverride(90.0f)
						[
							SNew(SNumericEntryBox<float>)
							.Label()[SNew(STextBlock).Text(LOCTEXT("FullRight", "Right"))]
							.Value(this, &SFPSRGunMotionTab::GetFullOffsetRight)
							.OnValueChanged(this, &SFPSRGunMotionTab::SetFullOffsetRight)
							.OnValueCommitted_Lambda([this](float V, ETextCommit::Type) { SetFullOffsetRight(V); })
						]
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
					[
						SNew(SBox).WidthOverride(90.0f)
						[
							SNew(SNumericEntryBox<float>)
							.Label()[SNew(STextBlock).Text(LOCTEXT("FullUp", "Up"))]
							.Value(this, &SFPSRGunMotionTab::GetFullOffsetUp)
							.OnValueChanged(this, &SFPSRGunMotionTab::SetFullOffsetUp)
							.OnValueCommitted_Lambda([this](float V, ETextCommit::Type) { SetFullOffsetUp(V); })
						]
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
					[
						SNew(SBox).WidthOverride(90.0f)
						[
							SNew(SNumericEntryBox<float>)
							.Label()[SNew(STextBlock).Text(LOCTEXT("FullFwd", "Fwd"))]
							.Value(this, &SFPSRGunMotionTab::GetFullOffsetFwd)
							.OnValueChanged(this, &SFPSRGunMotionTab::SetFullOffsetFwd)
							.OnValueCommitted_Lambda([this](float V, ETextCommit::Type) { SetFullOffsetFwd(V); })
						]
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
					[
						SNew(SBox).WidthOverride(90.0f)
						[
							SNew(SNumericEntryBox<float>)
							.Label()[SNew(STextBlock).Text(LOCTEXT("FullPitch", "Pitch"))]
							.Value(this, &SFPSRGunMotionTab::GetFullOffsetPitch)
							.OnValueChanged(this, &SFPSRGunMotionTab::SetFullOffsetPitch)
							.OnValueCommitted_Lambda([this](float V, ETextCommit::Type) { SetFullOffsetPitch(V); })
						]
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
					[
						SNew(SBox).WidthOverride(90.0f)
						[
							SNew(SNumericEntryBox<float>)
							.Label()[SNew(STextBlock).Text(LOCTEXT("FullYaw", "Yaw"))]
							.Value(this, &SFPSRGunMotionTab::GetFullOffsetYaw)
							.OnValueChanged(this, &SFPSRGunMotionTab::SetFullOffsetYaw)
							.OnValueCommitted_Lambda([this](float V, ETextCommit::Type) { SetFullOffsetYaw(V); })
						]
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
					[
						SNew(SBox).WidthOverride(90.0f)
						[
							SNew(SNumericEntryBox<float>)
							.Label()[SNew(STextBlock).Text(LOCTEXT("FullRoll", "Roll"))]
							.Value(this, &SFPSRGunMotionTab::GetFullOffsetRoll)
							.OnValueChanged(this, &SFPSRGunMotionTab::SetFullOffsetRoll)
							.OnValueCommitted_Lambda([this](float V, ETextCommit::Type) { SetFullOffsetRoll(V); })
						]
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(4.0f, 2.0f)
					[
						SNew(SButton)
						.Text(LOCTEXT("ApplyTemplateButton", "기본 템플릿"))
						.OnClicked(this, &SFPSRGunMotionTab::OnApplyDefaultTemplateClicked)
					]
				]

				// --- 4. 키 목록(§4-4) ---
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 6.0f, 0.0f, 2.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(0.12f)[SNew(STextBlock).Text(LOCTEXT("ColTime", "Time(s)"))]
					+ SHorizontalBox::Slot().FillWidth(0.12f)[SNew(STextBlock).Text(LOCTEXT("ColRight", "Right(cm)"))]
					+ SHorizontalBox::Slot().FillWidth(0.12f)[SNew(STextBlock).Text(LOCTEXT("ColUp", "Up(cm)"))]
					+ SHorizontalBox::Slot().FillWidth(0.12f)[SNew(STextBlock).Text(LOCTEXT("ColFwd", "Fwd(cm)"))]
					+ SHorizontalBox::Slot().FillWidth(0.12f)[SNew(STextBlock).Text(LOCTEXT("ColPitch", "Pitch"))]
					+ SHorizontalBox::Slot().FillWidth(0.12f)[SNew(STextBlock).Text(LOCTEXT("ColYaw", "Yaw"))]
					+ SHorizontalBox::Slot().FillWidth(0.12f)[SNew(STextBlock).Text(LOCTEXT("ColRoll", "Roll"))]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
				[
					SAssignNew(KeyListContainer, SVerticalBox)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("AddKeyButton", "+ 키 추가"))
					.OnClicked(this, &SFPSRGunMotionTab::OnAddKeyClicked)
				]

				// --- 5. [클립에 굽기] ---
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 8.0f, 0.0f, 2.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("BakeButton", "클립에 굽기"))
					.OnClicked(this, &SFPSRGunMotionTab::OnBakeClicked)
				]

				// --- 6. [PIE에서 재생] ---
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("PlayInPIEButton", "PIE에서 재생"))
					.OnClicked(this, &SFPSRGunMotionTab::OnPlayInPIEClicked)
				]
				] // BodyContent
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(2.0f, 6.0f, 2.0f, 2.0f)
			[
				SAssignNew(ActionStatusText, STextBlock)
				.AutoWrapText(true)
			]
		]
		]
	];

	// §7-§9: 뷰포트 클라이언트는 SFPSRGunMotionViewport::Construct 내부(SEditorViewport::Construct →
	// MakeEditorViewportClient)에서 이미 위 ChildSlot 대입 도중 동기적으로 만들어졌다 — 어셈블러 탭이 같은 순서로
	// FPViewport->GetFPClient() 를 곧바로 쓰는 것과 같은 근거(SFPSRWeaponAssemblerFPTab::RebuildViewport 참조).
	if (Viewport.IsValid())
	{
		if (const TSharedPtr<FFPSRGunMotionViewportClient> Client = Viewport->GetGunMotionClient())
		{
			Client->OnChannelGizmoCommit().BindSP(this, &SFPSRGunMotionTab::OnViewportChannelGizmoCommitted);
			Client->OnChannelPicked().BindSP(this, &SFPSRGunMotionTab::OnViewportChannelPicked);
		}
	}

	RebuildKeyRows();
	RebuildAttachOptions();
	RebuildChannelKeyRows();
}

SFPSRGunMotionTab::~SFPSRGunMotionTab()
{
	// §14 수명주기: "탭 닫기 시 미러 몽타주 Montage_Stop(0.1) 정리" — 탭이 닫힌 뒤에도 PIE 팔이 마지막 저작 포즈에
	// 얼어붙어 있으면 안 된다.
	StopMirrorMontage();
}

void SFPSRGunMotionTab::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	if (bPIELiveLinkEnabled)
	{
		UpdatePIEMirror();
	}
	else if (MirrorAnimInstance.IsValid())
	{
		// 토글을 끈 순간까지 연결돼 있었다 — 마지막 포즈에 얼어붙은 채 남기지 않는다(§14 취지, "정리" 확장).
		StopMirrorMontage();
	}
}

// ---------------------------------------------------------------------------------------------------------------
// 클립 선택
// ---------------------------------------------------------------------------------------------------------------

UAnimSequence* SFPSRGunMotionTab::GetSequence() const
{
	return SelectedSequence.Get();
}

FString SFPSRGunMotionTab::GetSequenceObjectPath() const
{
	UAnimSequence* Seq = GetSequence();
	return Seq ? Seq->GetPathName() : FString();
}

void SFPSRGunMotionTab::OnSequenceChanged(const FAssetData& AssetData)
{
	SelectedSequence = Cast<UAnimSequence>(AssetData.GetAsset());
	SelectedKeyIndex = INDEX_NONE;
	RebuildKeyRows();
	RebuildChannelKeyRows();
	RebuildViewportPreview();
}

UFPSRGunMotionAuthoringData* SFPSRGunMotionTab::GetOrCreateAuthoringData(bool bCreateIfMissing)
{
	UAnimSequence* Seq = GetSequence();
	if (!Seq)
	{
		return nullptr;
	}
	UFPSRGunMotionAuthoringData* AuthData = Seq->GetAssetUserData<UFPSRGunMotionAuthoringData>();
	if (!AuthData && bCreateIfMissing)
	{
		AuthData = NewObject<UFPSRGunMotionAuthoringData>(Seq);
		Seq->AddAssetUserData(AuthData);
	}
	return AuthData;
}

// ---------------------------------------------------------------------------------------------------------------
// 상태 줄 / 경고
// ---------------------------------------------------------------------------------------------------------------

bool SFPSRGunMotionTab::IsClipAdditive() const
{
	UAnimSequence* Seq = GetSequence();
	return Seq && Seq->IsValidAdditive();
}

FText SFPSRGunMotionTab::GetStatusText() const
{
	UAnimSequence* Seq = GetSequence();
	if (!Seq)
	{
		return LOCTEXT("NoClip", "클립을 선택하세요.");
	}
	if (!Seq->IsValidAdditive())
	{
		return LOCTEXT("StatusNotAdditive", "애디티브 클립이 아닙니다 — 이 툴은 애디티브 클립 전용입니다(모든 기능 비활성).");
	}

	const UFPSRGunMotionAuthoringData* AuthData = Seq->GetAssetUserData<UFPSRGunMotionAuthoringData>();
	const bool bSanitized = AuthData && AuthData->bSanitized;
	const IAnimationDataModel* Model = Seq->GetDataModel();
	const int32 NumFrames = Model ? Model->GetNumberOfFrames() : 0;

	return FText::Format(
		LOCTEXT("StatusFmt", "애디티브: 예 · 총 고정화: {0} · 길이 {1}초 · {2}프레임"),
		bSanitized ? LOCTEXT("SanitizedYes", "완료") : LOCTEXT("SanitizedNo", "안 됨"),
		FText::AsNumber(Seq->GetPlayLength()),
		FText::AsNumber(NumFrames));
}

bool SFPSRGunMotionTab::ConfirmIfOutsideConvention() const
{
	UAnimSequence* Seq = GetSequence();
	if (!Seq)
	{
		return true;
	}
	const bool bInLPAMGFolder = Seq->GetPathName().Contains(TEXT("Anims_LPAMG"));
	const FString AssetName = Seq->GetName();
	const bool bHasConventionSuffix = AssetName.EndsWith(TEXT("_GunLocked")) || AssetName.EndsWith(TEXT("_GunMotion"));
	if (bInLPAMGFolder && bHasConventionSuffix)
	{
		return true;
	}

	const EAppReturnType::Type Choice = FMessageDialog::Open(
		EAppMsgType::YesNo,
		LOCTEXT("OutsideConventionWarning", "이 클립은 Anims_LPAMG 경로 밖이거나 이름이 _GunLocked/_GunMotion 으로 끝나지 않습니다 — 원본 팩 클립을 직접 수정하는 것일 수 있습니다. 계속할까요?"));
	return Choice == EAppReturnType::Yes;
}

void SFPSRGunMotionTab::SetStatus(const FText& InText)
{
	if (ActionStatusText.IsValid())
	{
		ActionStatusText->SetText(InText);
	}
}

// ---------------------------------------------------------------------------------------------------------------
// 액션 버튼
// ---------------------------------------------------------------------------------------------------------------

FReply SFPSRGunMotionTab::OnSanitizeClicked()
{
	UAnimSequence* Seq = GetSequence();
	if (!Seq)
	{
		SetStatus(LOCTEXT("NoClipSelected", "클립을 먼저 선택하세요."));
		return FReply::Handled();
	}
	if (!ConfirmIfOutsideConvention())
	{
		return FReply::Handled();
	}

	const UFPSRGunMotionSettings* Settings = GetDefault<UFPSRGunMotionSettings>();
	FText Error;
	const bool bOk = FPSRGunMotionBaker::SanitizeRightChain(Seq, Settings->RightChainBones, Error);
	if (bOk)
	{
		UE_LOG(LogFPSR, Log, TEXT("[GunMotion] SanitizeRightChain 성공: %s"), *Seq->GetName());

		// §14 마지막: 슬롯 재생(미러)은 클립 자신의 커브만 흐른다 — IK 해제 커브(LeftHandIKWeight)가 정식 몽타주에만
		// 실려 있으면 미러엔 안 실린다. 이관 여부는 툴이 판단하지 않는다(범위 밖) — 없으면 경고만 낸다.
		if (HasFloatCurveNamed(Seq, Settings->LeftHandIKWeightCurveName))
		{
			SetStatus(LOCTEXT("SanitizeOk", "총 고정화 완료."));
		}
		else
		{
			SetStatus(LOCTEXT("SanitizeOkNoIKCurve", "총 고정화 완료. IK 해제 커브가 클립에 없음 — 미러에서 왼손이 그립에 고정됨."));
		}

		// §8: 이전에 복제해 둔 프리뷰 클립은 고정화 이전 상태로 고정된 채라(bSanitized=false 로 복제됨) 재사용할 수
		// 없다 — 원본 클립을 다시 복제해 베이스라인부터 새로 잡는다.
		RebuildViewportPreview();
	}
	else
	{
		UE_LOG(LogFPSR, Warning, TEXT("[GunMotion] SanitizeRightChain 실패: %s — %s"), *Seq->GetName(), *Error.ToString());
		SetStatus(Error);
	}
	return FReply::Handled();
}

FReply SFPSRGunMotionTab::OnBakeClicked()
{
	UAnimSequence* Seq = GetSequence();
	if (!Seq)
	{
		SetStatus(LOCTEXT("NoClipSelected", "클립을 먼저 선택하세요."));
		return FReply::Handled();
	}
	if (!ConfirmIfOutsideConvention())
	{
		return FReply::Handled();
	}

	const UFPSRGunMotionAuthoringData* AuthData = Seq->GetAssetUserData<UFPSRGunMotionAuthoringData>();
	const TArray<FFPSRGunMotionKey> Keys = AuthData ? AuthData->Keys : TArray<FFPSRGunMotionKey>();

	FText Error;
	if (!FPSRGunMotionBaker::BakeGunMotion(Seq, Keys, Error))
	{
		UE_LOG(LogFPSR, Warning, TEXT("[GunMotion] BakeGunMotion 실패: %s — %s"), *Seq->GetName(), *Error.ToString());
		SetStatus(Error);
		return FReply::Handled();
	}

	UE_LOG(LogFPSR, Log, TEXT("[GunMotion] BakeGunMotion 성공: %s"), *Seq->GetName());
	SetStatus(LOCTEXT("BakeOk", "굽기 완료."));

	const EAppReturnType::Type SaveChoice = FMessageDialog::Open(
		EAppMsgType::YesNo,
		LOCTEXT("SavePrompt", "굽기가 완료되었습니다. 지금 저장할까요?"));
	if (SaveChoice == EAppReturnType::Yes)
	{
		TArray<UPackage*> PackagesToSave;
		PackagesToSave.Add(Seq->GetOutermost());
		UEditorLoadingAndSavingUtils::SavePackages(PackagesToSave, /*bOnlyDirty=*/false);
	}
	return FReply::Handled();
}

FReply SFPSRGunMotionTab::OnPlayInPIEClicked()
{
	UAnimSequence* Seq = GetSequence();
	if (!Seq)
	{
		SetStatus(LOCTEXT("NoClipForPreview", "클립을 먼저 선택하세요."));
		return FReply::Handled();
	}

	FWorldContext* PIEWorldContext = GEditor ? GEditor->GetPIEWorldContext() : nullptr;
	UWorld* PIEWorld = PIEWorldContext ? PIEWorldContext->World() : nullptr;
	if (!PIEWorld)
	{
		SetStatus(LOCTEXT("NoPIE", "PIE 가 실행 중이 아닙니다."));
		return FReply::Handled();
	}

	APlayerController* PC = PIEWorld->GetFirstPlayerController();
	AFPSRCharacter* Character = PC ? Cast<AFPSRCharacter>(PC->GetPawn()) : nullptr;
	if (!Character)
	{
		SetStatus(LOCTEXT("NoCharacter", "PIE 에서 로컬 플레이어 캐릭터를 찾지 못했습니다."));
		return FReply::Handled();
	}

	const UFPSRGunMotionSettings* Settings = GetDefault<UFPSRGunMotionSettings>();
	USkeletalMeshComponent* Arms = FindComponentByName<USkeletalMeshComponent>(Character, Settings->ArmsComponentName);
	UAnimInstance* AnimInstance = Arms ? Arms->GetAnimInstance() : nullptr;
	if (!AnimInstance)
	{
		SetStatus(LOCTEXT("NoArmsAnimInstance", "1인칭 팔의 AnimInstance 를 찾지 못했습니다."));
		return FReply::Handled();
	}

	AnimInstance->PlaySlotAnimationAsDynamicMontage(Seq, Settings->PreviewSlotName, 0.1f, 0.1f);
	SetStatus(LOCTEXT("PlayedInPIE", "PIE 에서 재생했습니다."));
	return FReply::Handled();
}

// ---------------------------------------------------------------------------------------------------------------
// 키 목록
// ---------------------------------------------------------------------------------------------------------------

FReply SFPSRGunMotionTab::OnAddKeyClicked()
{
	UAnimSequence* Seq = GetSequence();
	if (!Seq)
	{
		SetStatus(LOCTEXT("NoClipSelected", "클립을 먼저 선택하세요."));
		return FReply::Handled();
	}

	const FScopedTransaction Transaction(LOCTEXT("AddKeyTransaction", "총 모션 키 추가"));
	UFPSRGunMotionAuthoringData* AuthData = GetOrCreateAuthoringData(/*bCreateIfMissing=*/true);
	if (!AuthData)
	{
		SetStatus(LOCTEXT("AuthDataFailed", "저작 데이터를 준비하지 못했습니다."));
		return FReply::Handled();
	}
	AuthData->Modify();

	FFPSRGunMotionKey NewKey;
	NewKey.Time = AuthData->Keys.Num() > 0 ? AuthData->Keys.Last().Time : 0.0f;
	AuthData->Keys.Add(NewKey);

	Seq->MarkPackageDirty();
	RebuildKeyRows();
	RebakeViewportPreview();
	return FReply::Handled();
}

FReply SFPSRGunMotionTab::OnRemoveKeyClicked(int32 KeyIndex)
{
	UAnimSequence* Seq = GetSequence();
	UFPSRGunMotionAuthoringData* AuthData = Seq ? Seq->GetAssetUserData<UFPSRGunMotionAuthoringData>() : nullptr;
	if (!AuthData || !AuthData->Keys.IsValidIndex(KeyIndex))
	{
		return FReply::Handled();
	}

	const FScopedTransaction Transaction(LOCTEXT("RemoveKeyTransaction", "총 모션 키 삭제"));
	AuthData->Modify();
	AuthData->Keys.RemoveAt(KeyIndex);

	// 삭제로 인덱스가 밀리므로, 지금 선택돼 있던 키가 무엇이었든 더 이상 유효하지 않다(§16 타임라인 우클릭 삭제도
	// 이 경로를 공유한다 — 단일 훅).
	SelectedKeyIndex = INDEX_NONE;

	Seq->MarkPackageDirty();
	RebuildKeyRows();
	RebakeViewportPreview();
	return FReply::Handled();
}

FReply SFPSRGunMotionTab::OnApplyDefaultTemplateClicked()
{
	UAnimSequence* Seq = GetSequence();
	if (!Seq)
	{
		SetStatus(LOCTEXT("NoClipSelected", "클립을 먼저 선택하세요."));
		return FReply::Handled();
	}

	const float L = Seq->GetPlayLength();
	const float SecondKeyTime = FMath::Min(0.3f, L * 0.2f);
	const float ThirdKeyTime = FMath::Clamp(L - 0.5f, SecondKeyTime, L);

	const FScopedTransaction Transaction(LOCTEXT("ApplyTemplateTransaction", "총 모션 기본 템플릿 적용"));
	UFPSRGunMotionAuthoringData* AuthData = GetOrCreateAuthoringData(/*bCreateIfMissing=*/true);
	if (!AuthData)
	{
		SetStatus(LOCTEXT("AuthDataFailed", "저작 데이터를 준비하지 못했습니다."));
		return FReply::Handled();
	}
	AuthData->Modify();
	AuthData->Keys.Reset();

	const FVector FullOffset(FullOffsetFwd, FullOffsetRight, FullOffsetUp); // CamOffset: X=앞,Y=오른쪽,Z=위
	const FRotator FullRotation(FullOffsetPitch, FullOffsetYaw, FullOffsetRoll);

	auto AddTemplateKey = [&AuthData](float Time, const FVector& Offset, const FRotator& Rotation)
	{
		FFPSRGunMotionKey Key;
		Key.Time = Time;
		Key.CamOffset = Offset;
		Key.CamRotation = Rotation;
		AuthData->Keys.Add(Key);
	};

	AddTemplateKey(0.0f, FVector::ZeroVector, FRotator::ZeroRotator);
	AddTemplateKey(SecondKeyTime, FullOffset, FullRotation);
	AddTemplateKey(ThirdKeyTime, FullOffset, FullRotation);
	AddTemplateKey(L, FVector::ZeroVector, FRotator::ZeroRotator);

	Seq->MarkPackageDirty();
	RebuildKeyRows();
	RebakeViewportPreview();
	SetStatus(LOCTEXT("TemplateApplied", "기본 템플릿을 적용했습니다."));
	return FReply::Handled();
}

void SFPSRGunMotionTab::RebuildKeyRows()
{
	if (!KeyListContainer.IsValid())
	{
		return;
	}
	KeyListContainer->ClearChildren();

	UAnimSequence* Seq = GetSequence();
	const UFPSRGunMotionAuthoringData* AuthData = Seq ? Seq->GetAssetUserData<UFPSRGunMotionAuthoringData>() : nullptr;
	const int32 NumKeys = AuthData ? AuthData->Keys.Num() : 0;

	for (int32 KeyIndex = 0; KeyIndex < NumKeys; ++KeyIndex)
	{
		KeyListContainer->AddSlot().AutoHeight().Padding(0.0f, 1.0f)
		[
			BuildKeyRow(KeyIndex)
		];
	}
}

TSharedRef<SWidget> SFPSRGunMotionTab::BuildKeyRow(int32 KeyIndex)
{
	auto MakeNumBox = [this](TAttribute<TOptional<float>> Getter, TFunction<void(float)> Setter) -> TSharedRef<SWidget>
	{
		return SNew(SBox).WidthOverride(80.0f)
		[
			SNew(SNumericEntryBox<float>)
			.Value(Getter)
			.OnValueChanged_Lambda([Setter](float V) { Setter(V); })
			.OnValueCommitted_Lambda([Setter](float V, ETextCommit::Type) { Setter(V); })
		];
	};

	// §16 "숫자 목록 행 선택과 동기화" — 타임라인에서 선택된 키와 같은 행을 옅게 강조한다. 선택 아님(투명)일 땐
	// 시각적으로 아무 것도 안 그려지므로 배경 브러시는 WhiteBrush 하나로 충분하다(틴트 알파로 온/오프).
	TSharedRef<SHorizontalBox> RowContent = SNew(SHorizontalBox)

		+ SHorizontalBox::Slot().FillWidth(0.12f).Padding(2.0f)
		[
			MakeNumBox(
				TAttribute<TOptional<float>>::Create(TAttribute<TOptional<float>>::FGetter::CreateLambda([this, KeyIndex]() -> TOptional<float> { return GetKeyTime(KeyIndex); })),
				[this, KeyIndex](float V) { SetKeyTime(KeyIndex, V); })
		]
		+ SHorizontalBox::Slot().FillWidth(0.12f).Padding(2.0f)
		[
			MakeNumBox(
				TAttribute<TOptional<float>>::Create(TAttribute<TOptional<float>>::FGetter::CreateLambda([this, KeyIndex]() -> TOptional<float> { return GetKeyRight(KeyIndex); })),
				[this, KeyIndex](float V) { SetKeyRight(KeyIndex, V); })
		]
		+ SHorizontalBox::Slot().FillWidth(0.12f).Padding(2.0f)
		[
			MakeNumBox(
				TAttribute<TOptional<float>>::Create(TAttribute<TOptional<float>>::FGetter::CreateLambda([this, KeyIndex]() -> TOptional<float> { return GetKeyUp(KeyIndex); })),
				[this, KeyIndex](float V) { SetKeyUp(KeyIndex, V); })
		]
		+ SHorizontalBox::Slot().FillWidth(0.12f).Padding(2.0f)
		[
			MakeNumBox(
				TAttribute<TOptional<float>>::Create(TAttribute<TOptional<float>>::FGetter::CreateLambda([this, KeyIndex]() -> TOptional<float> { return GetKeyFwd(KeyIndex); })),
				[this, KeyIndex](float V) { SetKeyFwd(KeyIndex, V); })
		]
		+ SHorizontalBox::Slot().FillWidth(0.12f).Padding(2.0f)
		[
			MakeNumBox(
				TAttribute<TOptional<float>>::Create(TAttribute<TOptional<float>>::FGetter::CreateLambda([this, KeyIndex]() -> TOptional<float> { return GetKeyPitch(KeyIndex); })),
				[this, KeyIndex](float V) { SetKeyPitch(KeyIndex, V); })
		]
		+ SHorizontalBox::Slot().FillWidth(0.12f).Padding(2.0f)
		[
			MakeNumBox(
				TAttribute<TOptional<float>>::Create(TAttribute<TOptional<float>>::FGetter::CreateLambda([this, KeyIndex]() -> TOptional<float> { return GetKeyYaw(KeyIndex); })),
				[this, KeyIndex](float V) { SetKeyYaw(KeyIndex, V); })
		]
		+ SHorizontalBox::Slot().FillWidth(0.12f).Padding(2.0f)
		[
			MakeNumBox(
				TAttribute<TOptional<float>>::Create(TAttribute<TOptional<float>>::FGetter::CreateLambda([this, KeyIndex]() -> TOptional<float> { return GetKeyRoll(KeyIndex); })),
				[this, KeyIndex](float V) { SetKeyRoll(KeyIndex, V); })
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(4.0f, 0.0f, 0.0f, 0.0f)
		[
			SNew(SButton)
			.Text(LOCTEXT("RemoveKeyButton", "삭제"))
			.OnClicked(FOnClicked::CreateSP(this, &SFPSRGunMotionTab::OnRemoveKeyClicked, KeyIndex))
		];

	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
		.Padding(FMargin(0.0f))
		.BorderBackgroundColor_Lambda([this, KeyIndex]() -> FSlateColor
		{
			return (KeyIndex == SelectedKeyIndex)
				? FSlateColor(FLinearColor(0.2f, 0.4f, 0.9f, 0.35f))
				: FSlateColor(FLinearColor::Transparent);
		})
		[
			RowContent
		];
}

void SFPSRGunMotionTab::MutateKey(int32 KeyIndex, const FText& TransactionText, TFunctionRef<void(FFPSRGunMotionKey&)> Mutator)
{
	UAnimSequence* Seq = GetSequence();
	UFPSRGunMotionAuthoringData* AuthData = Seq ? Seq->GetAssetUserData<UFPSRGunMotionAuthoringData>() : nullptr;
	if (!AuthData || !AuthData->Keys.IsValidIndex(KeyIndex))
	{
		return;
	}

	const FScopedTransaction Transaction(TransactionText);
	AuthData->Modify();
	Mutator(AuthData->Keys[KeyIndex]);
	Seq->MarkPackageDirty();

	// §7 헤더: "같은 Keys 데이터를 두 UI 가 편집한다" — 숫자 입력으로 고친 값도 뷰포트가 즉시 반영해야 한다(§8).
	RebakeViewportPreview();
}

float SFPSRGunMotionTab::GetKeyTime(int32 KeyIndex) const
{
	UAnimSequence* Seq = GetSequence();
	const UFPSRGunMotionAuthoringData* AuthData = Seq ? Seq->GetAssetUserData<UFPSRGunMotionAuthoringData>() : nullptr;
	return (AuthData && AuthData->Keys.IsValidIndex(KeyIndex)) ? AuthData->Keys[KeyIndex].Time : 0.0f;
}
void SFPSRGunMotionTab::SetKeyTime(int32 KeyIndex, float NewValue)
{
	MutateKey(KeyIndex, LOCTEXT("EditKeyTimeTransaction", "총 모션 키 시간 편집"), [NewValue](FFPSRGunMotionKey& Key) { Key.Time = NewValue; });
}

float SFPSRGunMotionTab::GetKeyRight(int32 KeyIndex) const
{
	UAnimSequence* Seq = GetSequence();
	const UFPSRGunMotionAuthoringData* AuthData = Seq ? Seq->GetAssetUserData<UFPSRGunMotionAuthoringData>() : nullptr;
	return (AuthData && AuthData->Keys.IsValidIndex(KeyIndex)) ? AuthData->Keys[KeyIndex].CamOffset.Y : 0.0f;
}
void SFPSRGunMotionTab::SetKeyRight(int32 KeyIndex, float NewValue)
{
	MutateKey(KeyIndex, LOCTEXT("EditKeyRightTransaction", "총 모션 키 Right 편집"), [NewValue](FFPSRGunMotionKey& Key) { Key.CamOffset.Y = NewValue; });
}

float SFPSRGunMotionTab::GetKeyUp(int32 KeyIndex) const
{
	UAnimSequence* Seq = GetSequence();
	const UFPSRGunMotionAuthoringData* AuthData = Seq ? Seq->GetAssetUserData<UFPSRGunMotionAuthoringData>() : nullptr;
	return (AuthData && AuthData->Keys.IsValidIndex(KeyIndex)) ? AuthData->Keys[KeyIndex].CamOffset.Z : 0.0f;
}
void SFPSRGunMotionTab::SetKeyUp(int32 KeyIndex, float NewValue)
{
	MutateKey(KeyIndex, LOCTEXT("EditKeyUpTransaction", "총 모션 키 Up 편집"), [NewValue](FFPSRGunMotionKey& Key) { Key.CamOffset.Z = NewValue; });
}

float SFPSRGunMotionTab::GetKeyFwd(int32 KeyIndex) const
{
	UAnimSequence* Seq = GetSequence();
	const UFPSRGunMotionAuthoringData* AuthData = Seq ? Seq->GetAssetUserData<UFPSRGunMotionAuthoringData>() : nullptr;
	return (AuthData && AuthData->Keys.IsValidIndex(KeyIndex)) ? AuthData->Keys[KeyIndex].CamOffset.X : 0.0f;
}
void SFPSRGunMotionTab::SetKeyFwd(int32 KeyIndex, float NewValue)
{
	MutateKey(KeyIndex, LOCTEXT("EditKeyFwdTransaction", "총 모션 키 Fwd 편집"), [NewValue](FFPSRGunMotionKey& Key) { Key.CamOffset.X = NewValue; });
}

float SFPSRGunMotionTab::GetKeyPitch(int32 KeyIndex) const
{
	UAnimSequence* Seq = GetSequence();
	const UFPSRGunMotionAuthoringData* AuthData = Seq ? Seq->GetAssetUserData<UFPSRGunMotionAuthoringData>() : nullptr;
	return (AuthData && AuthData->Keys.IsValidIndex(KeyIndex)) ? AuthData->Keys[KeyIndex].CamRotation.Pitch : 0.0f;
}
void SFPSRGunMotionTab::SetKeyPitch(int32 KeyIndex, float NewValue)
{
	MutateKey(KeyIndex, LOCTEXT("EditKeyPitchTransaction", "총 모션 키 Pitch 편집"), [NewValue](FFPSRGunMotionKey& Key) { Key.CamRotation.Pitch = NewValue; });
}

float SFPSRGunMotionTab::GetKeyYaw(int32 KeyIndex) const
{
	UAnimSequence* Seq = GetSequence();
	const UFPSRGunMotionAuthoringData* AuthData = Seq ? Seq->GetAssetUserData<UFPSRGunMotionAuthoringData>() : nullptr;
	return (AuthData && AuthData->Keys.IsValidIndex(KeyIndex)) ? AuthData->Keys[KeyIndex].CamRotation.Yaw : 0.0f;
}
void SFPSRGunMotionTab::SetKeyYaw(int32 KeyIndex, float NewValue)
{
	MutateKey(KeyIndex, LOCTEXT("EditKeyYawTransaction", "총 모션 키 Yaw 편집"), [NewValue](FFPSRGunMotionKey& Key) { Key.CamRotation.Yaw = NewValue; });
}

float SFPSRGunMotionTab::GetKeyRoll(int32 KeyIndex) const
{
	UAnimSequence* Seq = GetSequence();
	const UFPSRGunMotionAuthoringData* AuthData = Seq ? Seq->GetAssetUserData<UFPSRGunMotionAuthoringData>() : nullptr;
	return (AuthData && AuthData->Keys.IsValidIndex(KeyIndex)) ? AuthData->Keys[KeyIndex].CamRotation.Roll : 0.0f;
}
void SFPSRGunMotionTab::SetKeyRoll(int32 KeyIndex, float NewValue)
{
	MutateKey(KeyIndex, LOCTEXT("EditKeyRollTransaction", "총 모션 키 Roll 편집"), [NewValue](FFPSRGunMotionKey& Key) { Key.CamRotation.Roll = NewValue; });
}

// ---------------------------------------------------------------------------------------------------------------
// 증보 v2 §7-§9: 뷰포트 프리뷰 / 타임라인 / 기즈모
// ---------------------------------------------------------------------------------------------------------------

void SFPSRGunMotionTab::RebuildViewportPreview()
{
	if (!Viewport.IsValid())
	{
		return;
	}
	const TSharedPtr<FFPSRGunMotionViewportClient> Client = Viewport->GetGunMotionClient();
	if (!Client.IsValid())
	{
		return;
	}

	UAnimSequence* Seq = GetSequence();
	const UFPSRGunMotionAuthoringData* AuthData = Seq ? Seq->GetAssetUserData<UFPSRGunMotionAuthoringData>() : nullptr;

	// v3 §20-4: AuthoringData 전체를 넘긴다 — 레거시 Keys(hand_r 본 트랙) 뿐 아니라 §18 채널 트랙도 여기서부터
	// 직접 평가로 적용해야 해서다(SetSourceClip 이 내부에서 RebakePreview(Data) 를 호출한다).
	Client->SetSourceClip(Seq, AuthData);
	Client->SetActiveChannel(SelectedChannelId);

	// §16: 타임라인 위젯은 SequenceLength/Lanes 를 매 페인트 어트리뷰트로 다시 읽으므로(GetTimelineSequenceLength/
	// GetTimelineLanes), 기존 스크럽 슬라이더처럼 여기서 범위를 수동 동기화할 필요가 없다.

	if (!Client->GetIssue().IsEmpty())
	{
		SetStatus(FText::FromString(Client->GetIssue()));
	}
}

void SFPSRGunMotionTab::RebakeViewportPreview()
{
	if (!Viewport.IsValid())
	{
		return;
	}
	const TSharedPtr<FFPSRGunMotionViewportClient> Client = Viewport->GetGunMotionClient();
	if (!Client.IsValid() || !Client->HasPreviewClip())
	{
		return;
	}

	UAnimSequence* Seq = GetSequence();
	const UFPSRGunMotionAuthoringData* AuthData = Seq ? Seq->GetAssetUserData<UFPSRGunMotionAuthoringData>() : nullptr;

	Client->RebakePreview(AuthData);

	// §14: "재굽기 직후 → 미러 몽타주를 정지 후 재시작(블렌드 0.05)" — 재생 중인 몽타주가 갱신된 압축 데이터를
	// 확실히 다시 읽게 하는 가장 단순한 보장. 미러가 연결돼 있지 않으면(§14 수명주기) 무동작.
	ResyncMirrorAfterRebake();
}

float SFPSRGunMotionTab::GetScrubPosition() const
{
	if (!Viewport.IsValid())
	{
		return 0.0f;
	}
	const TSharedPtr<FFPSRGunMotionViewportClient> Client = Viewport->GetGunMotionClient();
	return Client.IsValid() ? Client->GetPreviewPosition() : 0.0f;
}

void SFPSRGunMotionTab::OnScrubPositionChanged(float NewValue)
{
	if (!Viewport.IsValid())
	{
		return;
	}
	if (const TSharedPtr<FFPSRGunMotionViewportClient> Client = Viewport->GetGunMotionClient())
	{
		Client->SetPreviewPosition(NewValue);
	}
}

void SFPSRGunMotionTab::OnScrubCaptureBegin()
{
	// §8: "스크럽 중에는 PreviewInstance 를 해당 시각에 고정(SetPosition, 재생 정지)" — 드래그 시작 시 재생을 멈춘다.
	if (!Viewport.IsValid())
	{
		return;
	}
	if (const TSharedPtr<FFPSRGunMotionViewportClient> Client = Viewport->GetGunMotionClient())
	{
		Client->SetPreviewPlaying(false);
	}
}

FReply SFPSRGunMotionTab::OnPlayPauseClicked()
{
	if (Viewport.IsValid())
	{
		if (const TSharedPtr<FFPSRGunMotionViewportClient> Client = Viewport->GetGunMotionClient())
		{
			Client->SetPreviewPlaying(!Client->IsPreviewPlaying());
		}
	}
	return FReply::Handled();
}

FText SFPSRGunMotionTab::GetPlayPauseLabel() const
{
	const TSharedPtr<FFPSRGunMotionViewportClient> Client = Viewport.IsValid() ? Viewport->GetGunMotionClient() : nullptr;
	const bool bPlaying = Client.IsValid() && Client->IsPreviewPlaying();
	return bPlaying ? LOCTEXT("PauseLabel", "정지") : LOCTEXT("PlayLabel", "재생");
}

// ---------------------------------------------------------------------------------------------------------------
// 증보 v2.3 §16: 타임라인 위젯(SFPSRGunMotionTimeline) 어트리뷰트/델리게이트
// ---------------------------------------------------------------------------------------------------------------

float SFPSRGunMotionTab::GetTimelineSequenceLength() const
{
	const TSharedPtr<FFPSRGunMotionViewportClient> Client = Viewport.IsValid() ? Viewport->GetGunMotionClient() : nullptr;
	return Client.IsValid() ? Client->GetPreviewLength() : 0.0f;
}

float SFPSRGunMotionTab::GetTimelineFrameRate() const
{
	const UAnimSequence* Seq = GetSequence();
	const IAnimationDataModel* Model = Seq ? Seq->GetDataModel() : nullptr;
	const double Fps = Model ? Model->GetFrameRate().AsDecimal() : 0.0;
	return (Fps > 0.0) ? static_cast<float>(Fps) : 30.0f;
}

FReply SFPSRGunMotionTab::OnTranslateModeClicked()
{
	if (Viewport.IsValid())
	{
		if (const TSharedPtr<FFPSRGunMotionViewportClient> Client = Viewport->GetGunMotionClient())
		{
			Client->SetWidgetMode(UE::Widget::WM_Translate);
		}
	}
	return FReply::Handled();
}

FReply SFPSRGunMotionTab::OnRotateModeClicked()
{
	if (Viewport.IsValid())
	{
		if (const TSharedPtr<FFPSRGunMotionViewportClient> Client = Viewport->GetGunMotionClient())
		{
			Client->SetWidgetMode(UE::Widget::WM_Rotate);
		}
	}
	return FReply::Handled();
}

FReply SFPSRGunMotionTab::OnKeyAtCurrentTimeClicked()
{
	// v3 §20-4: "현재 시각에 키" 는 이제 활성 채널에 적용된다(§20-3 일반화) — v2 시절엔 무기 채널 고정이었다.
	return OnAddChannelKeyClicked();
}

FReply SFPSRGunMotionTab::OnDeleteKeyAtCurrentTimeClicked()
{
	UAnimSequence* Seq = GetSequence();
	if (!Seq || !Viewport.IsValid())
	{
		return FReply::Handled();
	}
	const TSharedPtr<FFPSRGunMotionViewportClient> Client = Viewport->GetGunMotionClient();
	const float Time = Client.IsValid() ? Client->GetPreviewPosition() : 0.0f;

	const int32 ExistingIndex = FindChannelKeyIndexNearTime(SelectedChannelId, Time, FrameToleranceSeconds(Seq));
	OnRemoveChannelKeyClicked(SelectedChannelId, ExistingIndex);
	return FReply::Handled();
}

// ---------------------------------------------------------------------------------------------------------------
// v3 §20-3: 채널 상태 + 타임라인 레인
// ---------------------------------------------------------------------------------------------------------------

void SFPSRGunMotionTab::SetActiveChannelId(FName NewChannelId)
{
	if (SelectedChannelId == NewChannelId)
	{
		return;
	}
	SelectedChannelId = NewChannelId;
	SelectedKeyIndex = INDEX_NONE;

	if (Viewport.IsValid())
	{
		if (const TSharedPtr<FFPSRGunMotionViewportClient> Client = Viewport->GetGunMotionClient())
		{
			Client->SetActiveChannel(NewChannelId);
		}
	}

	RebuildChannelKeyRows();
}

TArray<FFPSRGunMotionTimelineLane> SFPSRGunMotionTab::GetTimelineLanes() const
{
	TArray<FFPSRGunMotionTimelineLane> Lanes;

	UAnimSequence* Seq = GetSequence();
	const UFPSRGunMotionAuthoringData* AuthData = Seq ? Seq->GetAssetUserData<UFPSRGunMotionAuthoringData>() : nullptr;

	auto CollectTimes = [](const TArray<FFPSRGunMotionChannelKey>& Keys) -> TArray<float>
	{
		TArray<float> Times;
		Times.Reserve(Keys.Num());
		for (const FFPSRGunMotionChannelKey& Key : Keys) { Times.Add(Key.Time); }
		return Times;
	};
	auto CollectScalarTimes = [](const TArray<FFPSRGunMotionScalarKey>& Keys) -> TArray<float>
	{
		TArray<float> Times;
		Times.Reserve(Keys.Num());
		for (const FFPSRGunMotionScalarKey& Key : Keys) { Times.Add(Key.Time); }
		return Times;
	};

	FFPSRGunMotionTimelineLane GunLane;
	GunLane.ChannelId = FPSRGunMotionChannelIds::Gun;
	GunLane.Label = LOCTEXT("LaneGun", "총");
	GunLane.KeyTimes = AuthData ? CollectTimes(AuthData->GunTrack.Keys) : TArray<float>();
	Lanes.Add(GunLane);

	FFPSRGunMotionTimelineLane LeftHandLane;
	LeftHandLane.ChannelId = FPSRGunMotionChannelIds::LeftHand;
	LeftHandLane.Label = LOCTEXT("LaneLeftHand", "왼손");
	LeftHandLane.KeyTimes = AuthData ? CollectTimes(AuthData->LeftHandTrack.Keys) : TArray<float>();
	Lanes.Add(LeftHandLane);

	FFPSRGunMotionTimelineLane LeftHandBlendLane;
	LeftHandBlendLane.ChannelId = FPSRGunMotionChannelIds::LeftHandBlend;
	LeftHandBlendLane.Label = LOCTEXT("LaneLeftHandBlend", "왼손 Blend");
	LeftHandBlendLane.bSubLane = true;
	LeftHandBlendLane.KeyTimes = AuthData ? CollectScalarTimes(AuthData->LeftHandBlendKeys) : TArray<float>();
	Lanes.Add(LeftHandBlendLane);

	FFPSRGunMotionTimelineLane RightHandLane;
	RightHandLane.ChannelId = FPSRGunMotionChannelIds::RightHand;
	RightHandLane.Label = LOCTEXT("LaneRightHand", "오른손");
	RightHandLane.KeyTimes = AuthData ? CollectTimes(AuthData->RightHandTrack.Keys) : TArray<float>();
	Lanes.Add(RightHandLane);

	FFPSRGunMotionTimelineLane RightHandBlendLane;
	RightHandBlendLane.ChannelId = FPSRGunMotionChannelIds::RightHandBlend;
	RightHandBlendLane.Label = LOCTEXT("LaneRightHandBlend", "오른손 Blend");
	RightHandBlendLane.bSubLane = true;
	RightHandBlendLane.KeyTimes = AuthData ? CollectScalarTimes(AuthData->RightHandBlendKeys) : TArray<float>();
	Lanes.Add(RightHandBlendLane);

	// 파츠 레인 — 뷰포트가 이미 PreviewWeaponData 에서 구성해 둔 채널 id 목록을 그대로 쓴다(§20-3, 뷰포트/타임라인이
	// 같은 파츠 순서를 보도록 하는 단일 소스).
	if (Viewport.IsValid())
	{
		if (const TSharedPtr<FFPSRGunMotionViewportClient> Client = Viewport->GetGunMotionClient())
		{
			for (const FName& PartChannelId : Client->GetPartChannelIds())
			{
				FFPSRGunMotionTimelineLane PartLane;
				PartLane.ChannelId = PartChannelId;
				PartLane.Label = FText::FromName(PartChannelId);
				if (AuthData)
				{
					if (const FFPSRGunMotionChannelTrack* Track = AuthData->PartTracks.Find(PartChannelId))
					{
						PartLane.KeyTimes = CollectTimes(Track->Keys);
					}
				}
				Lanes.Add(PartLane);
			}
		}
	}

	return Lanes;
}

void SFPSRGunMotionTab::OnTimelineChannelSelected(FName ChannelId)
{
	SetActiveChannelId(ChannelId);
}

void SFPSRGunMotionTab::OnTimelineChannelKeySelected(FName ChannelId, int32 KeyIndex)
{
	SetActiveChannelId(ChannelId);
	SelectedKeyIndex = KeyIndex;
	// §16: "키 클릭 = 선택 + 스크럽을 그 키 시각으로 점프" — 기존 스크럽 캡처/이동 경로를 그대로 재사용한다.
	OnScrubCaptureBegin();
	OnScrubPositionChanged(GetChannelKeyTime(ChannelId, KeyIndex));
}

void SFPSRGunMotionTab::OnTimelineChannelKeyTimeCommitted(FName ChannelId, int32 KeyIndex, float NewTime)
{
	if (FPSRGunMotionChannelIsScalar(FPSRGunMotionClassifyChannel(ChannelId)))
	{
		MutateBlendKey(ChannelId, KeyIndex, LOCTEXT("EditChannelKeyTimeTransaction", "총 모션 채널 키 시간 편집"),
			[NewTime](FFPSRGunMotionScalarKey& Key) { Key.Time = NewTime; });
	}
	else
	{
		MutateChannelKey(ChannelId, KeyIndex, LOCTEXT("EditChannelKeyTimeTransaction", "총 모션 채널 키 시간 편집"),
			[NewTime](FFPSRGunMotionChannelKey& Key) { Key.Time = NewTime; });
	}
}

void SFPSRGunMotionTab::OnTimelineChannelKeyDeleteRequested(FName ChannelId, int32 KeyIndex)
{
	OnRemoveChannelKeyClicked(ChannelId, KeyIndex);
}

void SFPSRGunMotionTab::OnTimelineChannelKeyAddRequested(FName ChannelId, float Time)
{
	UAnimSequence* Seq = GetSequence();
	if (!Seq)
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("AddChannelKeyAtTimeTransaction", "총 모션 채널 키 추가"));
	UFPSRGunMotionAuthoringData* AuthData = GetOrCreateAuthoringData(/*bCreateIfMissing=*/true);
	if (!AuthData)
	{
		SetStatus(LOCTEXT("AuthDataFailed", "저작 데이터를 준비하지 못했습니다."));
		return;
	}
	AuthData->Modify();

	if (FPSRGunMotionChannelIsScalar(FPSRGunMotionClassifyChannel(ChannelId)))
	{
		if (TArray<FFPSRGunMotionScalarKey>* Track = ResolveScalarTrack(*AuthData, ChannelId))
		{
			TArray<FFPSRGunMotionScalarKey> Sorted = *Track;
			Sorted.Sort([](const FFPSRGunMotionScalarKey& A, const FFPSRGunMotionScalarKey& B) { return A.Time < B.Time; });
			FFPSRGunMotionScalarKey NewKey;
			NewKey.Time = Time;
			NewKey.Value = FPSRGunMotionBaker::EvalScalarKeys(Sorted, Time);
			Track->Add(NewKey);
		}
	}
	else if (FFPSRGunMotionChannelTrack* Track = ResolveChannelTrack(*AuthData, ChannelId, /*bCreatePartIfMissing=*/true))
	{
		TArray<FFPSRGunMotionChannelKey> Sorted = Track->Keys;
		Sorted.Sort([](const FFPSRGunMotionChannelKey& A, const FFPSRGunMotionChannelKey& B) { return A.Time < B.Time; });
		FVector Loc;
		FQuat RotQ;
		FPSRGunMotionBaker::EvalChannelKeys(Sorted, Time, Loc, RotQ);
		FFPSRGunMotionChannelKey NewKey;
		NewKey.Time = Time;
		NewKey.Loc = Loc;
		NewKey.Rot = RotQ.Rotator();
		Track->Keys.Add(NewKey);
	}

	Seq->MarkPackageDirty();
	SetActiveChannelId(ChannelId);   // 레인 클릭·더블클릭 모두 그 레인을 활성화한다(§20-3).
	RebuildChannelKeyRows();
	RebakeViewportPreview();
}

// ---------------------------------------------------------------------------------------------------------------
// v3 §20-4: 뷰포트 채널 훅(기즈모 커밋 + 히트 프록시 클릭)
// ---------------------------------------------------------------------------------------------------------------

void SFPSRGunMotionTab::OnViewportChannelGizmoCommitted(FName ChannelId, float Time, FVector Loc, FRotator Rot)
{
	UAnimSequence* Seq = GetSequence();
	if (!Seq)
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("GizmoChannelKeyCommitTransaction", "총 모션 키(기즈모)"));
	UFPSRGunMotionAuthoringData* AuthData = GetOrCreateAuthoringData(/*bCreateIfMissing=*/true);
	if (!AuthData)
	{
		SetStatus(LOCTEXT("AuthDataFailed", "저작 데이터를 준비하지 못했습니다."));
		return;
	}
	AuthData->Modify();

	if (FFPSRGunMotionChannelTrack* Track = ResolveChannelTrack(*AuthData, ChannelId, /*bCreatePartIfMissing=*/true))
	{
		// §9: "같은 시각(±1프레임)의 키가 있으면 갱신, 없으면 추가" — 채널 파라미터화(§20-4).
		const int32 ExistingIndex = FindChannelKeyIndexNearTime(ChannelId, Time, FrameToleranceSeconds(Seq));
		if (Track->Keys.IsValidIndex(ExistingIndex))
		{
			Track->Keys[ExistingIndex].Loc = Loc;
			Track->Keys[ExistingIndex].Rot = Rot;
		}
		else
		{
			FFPSRGunMotionChannelKey NewKey;
			NewKey.Time = Time;
			NewKey.Loc = Loc;
			NewKey.Rot = Rot;
			Track->Keys.Add(NewKey);
		}
	}

	Seq->MarkPackageDirty();
	RebuildChannelKeyRows();
	RebakeViewportPreview();
}

void SFPSRGunMotionTab::OnViewportChannelPicked(FName ChannelId)
{
	SetActiveChannelId(ChannelId);
}

// ---------------------------------------------------------------------------------------------------------------
// v3 §20-1/§20-3: 채널 트랙 해석 + 단일 커밋 훅(채널 id 파라미터화)
// ---------------------------------------------------------------------------------------------------------------

FFPSRGunMotionChannelTrack* SFPSRGunMotionTab::ResolveChannelTrack(UFPSRGunMotionAuthoringData& AuthData, FName ChannelId, bool bCreatePartIfMissing) const
{
	switch (FPSRGunMotionClassifyChannel(ChannelId))
	{
	case EFPSRGunMotionChannelKind::Gun:
		return &AuthData.GunTrack;
	case EFPSRGunMotionChannelKind::LeftHand:
		return &AuthData.LeftHandTrack;
	case EFPSRGunMotionChannelKind::RightHand:
		return &AuthData.RightHandTrack;
	default:
		break;
	}

	// Part — PartTracks 는 맵이라 없으면 새로 만들 수도 있다(§20-1 "채널마다 독립 타이밍" 맵).
	if (!bCreatePartIfMissing)
	{
		return AuthData.PartTracks.Find(ChannelId);
	}
	return &AuthData.PartTracks.FindOrAdd(ChannelId);
}

const FFPSRGunMotionChannelTrack* SFPSRGunMotionTab::ResolveChannelTrackConst(const UFPSRGunMotionAuthoringData& AuthData, FName ChannelId) const
{
	switch (FPSRGunMotionClassifyChannel(ChannelId))
	{
	case EFPSRGunMotionChannelKind::Gun:
		return &AuthData.GunTrack;
	case EFPSRGunMotionChannelKind::LeftHand:
		return &AuthData.LeftHandTrack;
	case EFPSRGunMotionChannelKind::RightHand:
		return &AuthData.RightHandTrack;
	default:
		return AuthData.PartTracks.Find(ChannelId);
	}
}

TArray<FFPSRGunMotionScalarKey>* SFPSRGunMotionTab::ResolveScalarTrack(UFPSRGunMotionAuthoringData& AuthData, FName ChannelId) const
{
	if (ChannelId == FPSRGunMotionChannelIds::LeftHandBlend)
	{
		return &AuthData.LeftHandBlendKeys;
	}
	if (ChannelId == FPSRGunMotionChannelIds::RightHandBlend)
	{
		return &AuthData.RightHandBlendKeys;
	}
	return nullptr;
}

const TArray<FFPSRGunMotionScalarKey>* SFPSRGunMotionTab::ResolveScalarTrackConst(const UFPSRGunMotionAuthoringData& AuthData, FName ChannelId) const
{
	if (ChannelId == FPSRGunMotionChannelIds::LeftHandBlend)
	{
		return &AuthData.LeftHandBlendKeys;
	}
	if (ChannelId == FPSRGunMotionChannelIds::RightHandBlend)
	{
		return &AuthData.RightHandBlendKeys;
	}
	return nullptr;
}

void SFPSRGunMotionTab::MutateChannelKey(FName ChannelId, int32 KeyIndex, const FText& TransactionText, TFunctionRef<void(FFPSRGunMotionChannelKey&)> Mutator)
{
	UAnimSequence* Seq = GetSequence();
	UFPSRGunMotionAuthoringData* AuthData = Seq ? Seq->GetAssetUserData<UFPSRGunMotionAuthoringData>() : nullptr;
	if (!AuthData)
	{
		return;
	}
	FFPSRGunMotionChannelTrack* Track = ResolveChannelTrack(*AuthData, ChannelId, /*bCreatePartIfMissing=*/false);
	if (!Track || !Track->Keys.IsValidIndex(KeyIndex))
	{
		return;
	}

	const FScopedTransaction Transaction(TransactionText);
	AuthData->Modify();
	Mutator(Track->Keys[KeyIndex]);
	Seq->MarkPackageDirty();

	// §7 헤더 취지의 §20-3 확장: 숫자 입력·타임라인 드래그·기즈모가 전부 이 하나의 꼬리를 공유한다.
	RebakeViewportPreview();
}

void SFPSRGunMotionTab::MutateBlendKey(FName ChannelId, int32 KeyIndex, const FText& TransactionText, TFunctionRef<void(FFPSRGunMotionScalarKey&)> Mutator)
{
	UAnimSequence* Seq = GetSequence();
	UFPSRGunMotionAuthoringData* AuthData = Seq ? Seq->GetAssetUserData<UFPSRGunMotionAuthoringData>() : nullptr;
	if (!AuthData)
	{
		return;
	}
	TArray<FFPSRGunMotionScalarKey>* Track = ResolveScalarTrack(*AuthData, ChannelId);
	if (!Track || !Track->IsValidIndex(KeyIndex))
	{
		return;
	}

	const FScopedTransaction Transaction(TransactionText);
	AuthData->Modify();
	Mutator((*Track)[KeyIndex]);
	Seq->MarkPackageDirty();
	RebakeViewportPreview();
}

int32 SFPSRGunMotionTab::FindChannelKeyIndexNearTime(FName ChannelId, float Time, float ToleranceSeconds) const
{
	UAnimSequence* Seq = GetSequence();
	const UFPSRGunMotionAuthoringData* AuthData = Seq ? Seq->GetAssetUserData<UFPSRGunMotionAuthoringData>() : nullptr;
	if (!AuthData)
	{
		return INDEX_NONE;
	}

	int32 BestIndex = INDEX_NONE;
	float BestDelta = ToleranceSeconds;

	if (FPSRGunMotionChannelIsScalar(FPSRGunMotionClassifyChannel(ChannelId)))
	{
		const TArray<FFPSRGunMotionScalarKey>* Track = ResolveScalarTrackConst(*AuthData, ChannelId);
		if (!Track)
		{
			return INDEX_NONE;
		}
		for (int32 Index = 0; Index < Track->Num(); ++Index)
		{
			const float Delta = FMath::Abs((*Track)[Index].Time - Time);
			if (Delta <= BestDelta)
			{
				BestDelta = Delta;
				BestIndex = Index;
			}
		}
	}
	else
	{
		const FFPSRGunMotionChannelTrack* Track = ResolveChannelTrackConst(*AuthData, ChannelId);
		if (!Track)
		{
			return INDEX_NONE;
		}
		for (int32 Index = 0; Index < Track->Keys.Num(); ++Index)
		{
			const float Delta = FMath::Abs(Track->Keys[Index].Time - Time);
			if (Delta <= BestDelta)
			{
				BestDelta = Delta;
				BestIndex = Index;
			}
		}
	}
	return BestIndex;
}

float SFPSRGunMotionTab::GetChannelKeyTime(FName ChannelId, int32 KeyIndex) const
{
	UAnimSequence* Seq = GetSequence();
	const UFPSRGunMotionAuthoringData* AuthData = Seq ? Seq->GetAssetUserData<UFPSRGunMotionAuthoringData>() : nullptr;
	if (!AuthData)
	{
		return 0.0f;
	}
	if (FPSRGunMotionChannelIsScalar(FPSRGunMotionClassifyChannel(ChannelId)))
	{
		const TArray<FFPSRGunMotionScalarKey>* Track = ResolveScalarTrackConst(*AuthData, ChannelId);
		return (Track && Track->IsValidIndex(KeyIndex)) ? (*Track)[KeyIndex].Time : 0.0f;
	}
	const FFPSRGunMotionChannelTrack* Track = ResolveChannelTrackConst(*AuthData, ChannelId);
	return (Track && Track->Keys.IsValidIndex(KeyIndex)) ? Track->Keys[KeyIndex].Time : 0.0f;
}

// ---------------------------------------------------------------------------------------------------------------
// v3 §20-3: 활성 채널 숫자 키 목록
// ---------------------------------------------------------------------------------------------------------------

void SFPSRGunMotionTab::RebuildChannelKeyRows()
{
	if (!ChannelKeyListContainer.IsValid())
	{
		return;
	}
	ChannelKeyListContainer->ClearChildren();

	UAnimSequence* Seq = GetSequence();
	const UFPSRGunMotionAuthoringData* AuthData = Seq ? Seq->GetAssetUserData<UFPSRGunMotionAuthoringData>() : nullptr;
	if (!AuthData)
	{
		return;
	}

	if (FPSRGunMotionChannelIsScalar(FPSRGunMotionClassifyChannel(SelectedChannelId)))
	{
		const TArray<FFPSRGunMotionScalarKey>* Track = ResolveScalarTrackConst(*AuthData, SelectedChannelId);
		const int32 NumKeys = Track ? Track->Num() : 0;
		for (int32 KeyIndex = 0; KeyIndex < NumKeys; ++KeyIndex)
		{
			ChannelKeyListContainer->AddSlot().AutoHeight().Padding(0.0f, 1.0f)
			[
				BuildBlendKeyRow(SelectedChannelId, KeyIndex)
			];
		}
	}
	else
	{
		const FFPSRGunMotionChannelTrack* Track = ResolveChannelTrackConst(*AuthData, SelectedChannelId);
		const int32 NumKeys = Track ? Track->Keys.Num() : 0;
		for (int32 KeyIndex = 0; KeyIndex < NumKeys; ++KeyIndex)
		{
			ChannelKeyListContainer->AddSlot().AutoHeight().Padding(0.0f, 1.0f)
			[
				BuildChannelKeyRow(SelectedChannelId, KeyIndex)
			];
		}
	}
}

TSharedRef<SWidget> SFPSRGunMotionTab::BuildChannelKeyRow(FName ChannelId, int32 KeyIndex)
{
	auto MakeNumBox = [this](TAttribute<TOptional<float>> Getter, TFunction<void(float)> Setter) -> TSharedRef<SWidget>
	{
		return SNew(SBox).WidthOverride(72.0f)
		[
			SNew(SNumericEntryBox<float>)
			.Value(Getter)
			.OnValueChanged_Lambda([Setter](float V) { Setter(V); })
			.OnValueCommitted_Lambda([Setter](float V, ETextCommit::Type) { Setter(V); })
		];
	};

	// Field: 0=Time 1=TX 2=TY 3=TZ 4=RP(Pitch) 5=RY(Yaw) 6=RR(Roll) — §18 커브 접미와 같은 순서.
	auto GetField = [this, ChannelId, KeyIndex](int32 Field) -> TOptional<float>
	{
		UAnimSequence* Seq = GetSequence();
		const UFPSRGunMotionAuthoringData* AuthData = Seq ? Seq->GetAssetUserData<UFPSRGunMotionAuthoringData>() : nullptr;
		const FFPSRGunMotionChannelTrack* Track = AuthData ? ResolveChannelTrackConst(*AuthData, ChannelId) : nullptr;
		if (!Track || !Track->Keys.IsValidIndex(KeyIndex))
		{
			return 0.0f;
		}
		const FFPSRGunMotionChannelKey& Key = Track->Keys[KeyIndex];
		switch (Field)
		{
		case 0: return Key.Time;
		case 1: return Key.Loc.X;
		case 2: return Key.Loc.Y;
		case 3: return Key.Loc.Z;
		case 4: return Key.Rot.Pitch;
		case 5: return Key.Rot.Yaw;
		default: return Key.Rot.Roll;
		}
	};
	auto SetField = [this, ChannelId, KeyIndex](int32 Field, float V)
	{
		MutateChannelKey(ChannelId, KeyIndex, LOCTEXT("EditChannelKeyTransaction", "총 모션 채널 키 편집"),
			[Field, V](FFPSRGunMotionChannelKey& Key)
			{
				switch (Field)
				{
				case 0: Key.Time = V; break;
				case 1: Key.Loc.X = V; break;
				case 2: Key.Loc.Y = V; break;
				case 3: Key.Loc.Z = V; break;
				case 4: Key.Rot.Pitch = V; break;
				case 5: Key.Rot.Yaw = V; break;
				default: Key.Rot.Roll = V; break;
				}
			});
	};

	TSharedRef<SHorizontalBox> RowContent = SNew(SHorizontalBox);
	for (int32 Field = 0; Field < 7; ++Field)
	{
		RowContent->AddSlot().FillWidth(0.13f).Padding(2.0f)
		[
			MakeNumBox(
				TAttribute<TOptional<float>>::Create(TAttribute<TOptional<float>>::FGetter::CreateLambda([GetField, Field]() { return GetField(Field); })),
				[SetField, Field](float V) { SetField(Field, V); })
		];
	}
	RowContent->AddSlot().AutoWidth().Padding(4.0f, 0.0f, 0.0f, 0.0f)
	[
		SNew(SButton)
		.Text(LOCTEXT("RemoveChannelKeyButton", "삭제"))
		.OnClicked(FOnClicked::CreateSP(this, &SFPSRGunMotionTab::OnRemoveChannelKeyClicked, ChannelId, KeyIndex))
	];

	return RowContent;
}

TSharedRef<SWidget> SFPSRGunMotionTab::BuildBlendKeyRow(FName ChannelId, int32 KeyIndex)
{
	auto GetTime = [this, ChannelId, KeyIndex]() -> TOptional<float>
	{
		return GetChannelKeyTime(ChannelId, KeyIndex);
	};
	auto SetTime = [this, ChannelId, KeyIndex](float V)
	{
		MutateBlendKey(ChannelId, KeyIndex, LOCTEXT("EditBlendKeyTimeTransaction", "총 모션 Blend 키 시간 편집"),
			[V](FFPSRGunMotionScalarKey& Key) { Key.Time = V; });
	};
	auto GetValue = [this, ChannelId, KeyIndex]() -> TOptional<float>
	{
		UAnimSequence* Seq = GetSequence();
		const UFPSRGunMotionAuthoringData* AuthData = Seq ? Seq->GetAssetUserData<UFPSRGunMotionAuthoringData>() : nullptr;
		const TArray<FFPSRGunMotionScalarKey>* Track = AuthData ? ResolveScalarTrackConst(*AuthData, ChannelId) : nullptr;
		return (Track && Track->IsValidIndex(KeyIndex)) ? (*Track)[KeyIndex].Value : 0.0f;
	};
	auto SetValue = [this, ChannelId, KeyIndex](float V)
	{
		MutateBlendKey(ChannelId, KeyIndex, LOCTEXT("EditBlendKeyValueTransaction", "총 모션 Blend 키 값 편집"),
			[V](FFPSRGunMotionScalarKey& Key) { Key.Value = FMath::Clamp(V, 0.0f, 1.0f); });
	};

	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(0.4f).Padding(2.0f)
		[
			SNew(SBox).WidthOverride(80.0f)
			[
				SNew(SNumericEntryBox<float>)
				.Value_Lambda(GetTime)
				.OnValueChanged_Lambda(SetTime)
				.OnValueCommitted_Lambda([SetTime](float V, ETextCommit::Type) { SetTime(V); })
			]
		]
		+ SHorizontalBox::Slot().FillWidth(0.4f).Padding(2.0f)
		[
			SNew(SBox).WidthOverride(80.0f)
			[
				SNew(SNumericEntryBox<float>)
				.MinValue(0.0f)
				.MaxValue(1.0f)
				.Value_Lambda(GetValue)
				.OnValueChanged_Lambda(SetValue)
				.OnValueCommitted_Lambda([SetValue](float V, ETextCommit::Type) { SetValue(V); })
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(4.0f, 0.0f, 0.0f, 0.0f)
		[
			SNew(SButton)
			.Text(LOCTEXT("RemoveBlendKeyButton", "삭제"))
			.OnClicked(FOnClicked::CreateSP(this, &SFPSRGunMotionTab::OnRemoveChannelKeyClicked, ChannelId, KeyIndex))
		];
}

FReply SFPSRGunMotionTab::OnAddChannelKeyClicked()
{
	UAnimSequence* Seq = GetSequence();
	if (!Seq || !Viewport.IsValid())
	{
		SetStatus(LOCTEXT("NoClipSelected", "클립을 먼저 선택하세요."));
		return FReply::Handled();
	}
	const TSharedPtr<FFPSRGunMotionViewportClient> Client = Viewport->GetGunMotionClient();
	const float Time = Client.IsValid() ? Client->GetPreviewPosition() : 0.0f;

	// §20-4 원문 규약("지금 스크럽 시각에 키가 없으면 새로 추가") — 이미 있으면 무동작(중복 키 방지).
	if (FindChannelKeyIndexNearTime(SelectedChannelId, Time, FrameToleranceSeconds(Seq)) != INDEX_NONE)
	{
		return FReply::Handled();
	}

	OnTimelineChannelKeyAddRequested(SelectedChannelId, Time);
	return FReply::Handled();
}

FReply SFPSRGunMotionTab::OnRemoveChannelKeyClicked(FName ChannelId, int32 KeyIndex)
{
	UAnimSequence* Seq = GetSequence();
	UFPSRGunMotionAuthoringData* AuthData = Seq ? Seq->GetAssetUserData<UFPSRGunMotionAuthoringData>() : nullptr;
	if (!AuthData || KeyIndex == INDEX_NONE)
	{
		return FReply::Handled();
	}

	if (FPSRGunMotionChannelIsScalar(FPSRGunMotionClassifyChannel(ChannelId)))
	{
		TArray<FFPSRGunMotionScalarKey>* Track = ResolveScalarTrack(*AuthData, ChannelId);
		if (!Track || !Track->IsValidIndex(KeyIndex))
		{
			return FReply::Handled();
		}
		const FScopedTransaction Transaction(LOCTEXT("RemoveBlendKeyTransaction", "총 모션 Blend 키 삭제"));
		AuthData->Modify();
		Track->RemoveAt(KeyIndex);
	}
	else
	{
		FFPSRGunMotionChannelTrack* Track = ResolveChannelTrack(*AuthData, ChannelId, /*bCreatePartIfMissing=*/false);
		if (!Track || !Track->Keys.IsValidIndex(KeyIndex))
		{
			return FReply::Handled();
		}
		const FScopedTransaction Transaction(LOCTEXT("RemoveChannelKeyTransaction", "총 모션 채널 키 삭제"));
		AuthData->Modify();
		Track->Keys.RemoveAt(KeyIndex);
	}

	SelectedKeyIndex = INDEX_NONE;
	Seq->MarkPackageDirty();
	RebuildChannelKeyRows();
	RebakeViewportPreview();
	return FReply::Handled();
}

FText SFPSRGunMotionTab::GetActiveChannelHeaderText() const
{
	FText ChannelLabel;
	switch (FPSRGunMotionClassifyChannel(SelectedChannelId))
	{
	case EFPSRGunMotionChannelKind::Gun:
		ChannelLabel = LOCTEXT("ChannelHeaderGun", "총");
		break;
	case EFPSRGunMotionChannelKind::LeftHand:
		ChannelLabel = LOCTEXT("ChannelHeaderLeftHand", "왼손");
		break;
	case EFPSRGunMotionChannelKind::RightHand:
		ChannelLabel = LOCTEXT("ChannelHeaderRightHand", "오른손");
		break;
	case EFPSRGunMotionChannelKind::LeftHandBlend:
		ChannelLabel = LOCTEXT("ChannelHeaderLeftHandBlend", "왼손 Blend");
		break;
	case EFPSRGunMotionChannelKind::RightHandBlend:
		ChannelLabel = LOCTEXT("ChannelHeaderRightHandBlend", "오른손 Blend");
		break;
	default:
		ChannelLabel = FText::FromName(SelectedChannelId); // 파츠 — 안정 소켓 id 그대로 표시.
		break;
	}
	return FText::Format(LOCTEXT("ChannelHeaderFmt", "활성 채널: {0}"), ChannelLabel);
}

// ---------------------------------------------------------------------------------------------------------------
// v3 §20-3: 부착 드롭다운(손 채널 전용)
// ---------------------------------------------------------------------------------------------------------------

void SFPSRGunMotionTab::RebuildAttachOptions()
{
	AttachOptions.Reset();
	AttachOptions.Add(MakeShared<FName>(NAME_None));   // "없음"

	const UFPSRGunMotionSettings* Settings = GetDefault<UFPSRGunMotionSettings>();
	const UFPSRWeaponDataAsset* WeaponData = (Settings && !Settings->PreviewWeaponData.IsNull()) ? Settings->PreviewWeaponData.LoadSynchronous() : nullptr;
	if (WeaponData)
	{
		for (const FFPSRWeaponPartAttachment& Part : WeaponData->WeaponParts)
		{
			if (!Part.Part.IsNull() && !Part.Socket.IsNone())
			{
				AttachOptions.Add(MakeShared<FName>(Part.Socket));
			}
		}
	}

	if (AttachCombo.IsValid())
	{
		AttachCombo->RefreshOptions();
	}
}

TSharedRef<SWidget> SFPSRGunMotionTab::BuildAttachComboRow()
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 4.0f, 0.0f)
		[
			SNew(STextBlock).Text(LOCTEXT("AttachPartLabel", "부착 파츠:"))
		]
		+ SHorizontalBox::Slot().FillWidth(1.0f)
		[
			SAssignNew(AttachCombo, SComboBox<TSharedPtr<FName>>)
			.OptionsSource(&AttachOptions)
			.OnGenerateWidget(this, &SFPSRGunMotionTab::GenerateAttachOptionWidget)
			.OnSelectionChanged(this, &SFPSRGunMotionTab::OnAttachOptionSelected)
			[
				SNew(STextBlock).Text(this, &SFPSRGunMotionTab::GetAttachComboLabel)
			]
		];
}

FText SFPSRGunMotionTab::GetAttachComboLabel() const
{
	UAnimSequence* Seq = GetSequence();
	const UFPSRGunMotionAuthoringData* AuthData = Seq ? Seq->GetAssetUserData<UFPSRGunMotionAuthoringData>() : nullptr;
	if (!AuthData)
	{
		return LOCTEXT("AttachNone", "없음");
	}
	FName Current = NAME_None;
	const EFPSRGunMotionChannelKind Kind = FPSRGunMotionClassifyChannel(SelectedChannelId);
	if (Kind == EFPSRGunMotionChannelKind::LeftHand)
	{
		Current = AuthData->LeftHandAttachPartSocket;
	}
	else if (Kind == EFPSRGunMotionChannelKind::RightHand)
	{
		Current = AuthData->RightHandAttachPartSocket;
	}
	return Current.IsNone() ? LOCTEXT("AttachNone", "없음") : FText::FromName(Current);
}

void SFPSRGunMotionTab::OnAttachOptionSelected(TSharedPtr<FName> NewSelection, ESelectInfo::Type)
{
	if (!NewSelection.IsValid())
	{
		return;
	}
	UAnimSequence* Seq = GetSequence();
	if (!Seq)
	{
		return;
	}
	const EFPSRGunMotionChannelKind Kind = FPSRGunMotionClassifyChannel(SelectedChannelId);
	if (Kind != EFPSRGunMotionChannelKind::LeftHand && Kind != EFPSRGunMotionChannelKind::RightHand)
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("SetAttachPartTransaction", "총 모션 부착 파츠 지정"));
	UFPSRGunMotionAuthoringData* AuthData = GetOrCreateAuthoringData(/*bCreateIfMissing=*/true);
	if (!AuthData)
	{
		SetStatus(LOCTEXT("AuthDataFailed", "저작 데이터를 준비하지 못했습니다."));
		return;
	}
	AuthData->Modify();
	if (Kind == EFPSRGunMotionChannelKind::LeftHand)
	{
		AuthData->LeftHandAttachPartSocket = *NewSelection;
	}
	else
	{
		AuthData->RightHandAttachPartSocket = *NewSelection;
	}
	Seq->MarkPackageDirty();
	RebakeViewportPreview();
}

TSharedRef<SWidget> SFPSRGunMotionTab::GenerateAttachOptionWidget(TSharedPtr<FName> Option)
{
	const FName Value = Option.IsValid() ? *Option : NAME_None;
	return SNew(STextBlock).Text(Value.IsNone() ? LOCTEXT("AttachNone", "없음") : FText::FromName(Value));
}

EVisibility SFPSRGunMotionTab::GetAttachRowVisibility() const
{
	const EFPSRGunMotionChannelKind Kind = FPSRGunMotionClassifyChannel(SelectedChannelId);
	return (Kind == EFPSRGunMotionChannelKind::LeftHand || Kind == EFPSRGunMotionChannelKind::RightHand) ? EVisibility::Visible : EVisibility::Collapsed;
}

// ---------------------------------------------------------------------------------------------------------------
// v3 §20-2: 채널 커브 베이크(A17 자동화 — 본 트랙 대체)
// ---------------------------------------------------------------------------------------------------------------

FReply SFPSRGunMotionTab::OnBakeCurveChannelsClicked()
{
	UAnimSequence* Seq = GetSequence();
	if (!Seq)
	{
		SetStatus(LOCTEXT("NoClipSelected", "클립을 먼저 선택하세요."));
		return FReply::Handled();
	}
	if (!ConfirmIfOutsideConvention())
	{
		return FReply::Handled();
	}

	const UFPSRGunMotionAuthoringData* AuthData = Seq->GetAssetUserData<UFPSRGunMotionAuthoringData>();
	if (!AuthData)
	{
		SetStatus(LOCTEXT("NoAuthoringData", "저작 데이터가 없습니다 — 채널 키를 먼저 저작하세요."));
		return FReply::Handled();
	}

	TArray<FString> MontageReport;
	FText Error;
	if (!FPSRGunMotionBaker::BakeCurveChannels(Seq, *AuthData, Error, &MontageReport))
	{
		UE_LOG(LogFPSR, Warning, TEXT("[GunMotion] BakeCurveChannels 실패: %s — %s"), *Seq->GetName(), *Error.ToString());
		SetStatus(Error);
		return FReply::Handled();
	}

	const FString ReportJoined = MontageReport.Num() > 0 ? FString::Join(MontageReport, TEXT(" / ")) : TEXT("참조 몽타주 없음");
	UE_LOG(LogFPSR, Log, TEXT("[GunMotion] BakeCurveChannels 성공: %s — %s"), *Seq->GetName(), *ReportJoined);
	SetStatus(FText::Format(LOCTEXT("BakeCurveChannelsOk", "채널 커브 굽기 완료. 몽타주: {0}"), FText::FromString(ReportJoined)));

	const EAppReturnType::Type SaveChoice = FMessageDialog::Open(
		EAppMsgType::YesNo,
		LOCTEXT("SavePrompt", "굽기가 완료되었습니다. 지금 저장할까요?"));
	if (SaveChoice == EAppReturnType::Yes)
	{
		// BakeCurveChannels 는 굽기/스킵 사유를 문자열로만 보고한다(몽타주 UObject 포인터는 안 돌려준다) — 참조
		// 몽타주 패키지는 여기서 명시적으로 저장하지 않지만 MarkPackageDirty 상태로 남아 에디터가 "저장되지 않은
		// 변경"으로 계속 추적하므로 데이터 유실은 없다(사용자가 나중에 일괄 저장 가능). 이 절충은 보고서에 명시.
		TArray<UPackage*> PackagesToSave;
		PackagesToSave.Add(Seq->GetOutermost());
		UEditorLoadingAndSavingUtils::SavePackages(PackagesToSave, /*bOnlyDirty=*/false);
	}
	return FReply::Handled();
}

// ---------------------------------------------------------------------------------------------------------------
// v3 §20-5: [새 액션 클립]
// ---------------------------------------------------------------------------------------------------------------

FReply SFPSRGunMotionTab::OnCreateNewActionClipClicked()
{
	const UFPSRGunMotionSettings* Settings = GetDefault<UFPSRGunMotionSettings>();
	if (!Settings || Settings->TargetCharacterBP.IsNull())
	{
		SetStatus(LOCTEXT("NoTargetCharacterBP", "프로젝트 설정 > FPSR > FPSR Gun Motion 에 '대상 캐릭터 BP'가 비어 있습니다."));
		return FReply::Handled();
	}

	UBlueprint* BP = Settings->TargetCharacterBP.LoadSynchronous();
	UClass* GeneratedClass = BP ? BP->GeneratedClass.Get() : nullptr;
	AActor* CDO = GeneratedClass ? GeneratedClass->GetDefaultObject<AActor>() : nullptr;
	USkeletalMeshComponent* ArmsSource = CDO ? FindComponentByName<USkeletalMeshComponent>(CDO, Settings->ArmsComponentName) : nullptr;
	USkeletalMesh* ArmsMesh = ArmsSource ? ArmsSource->GetSkeletalMeshAsset() : nullptr;
	USkeleton* ArmsSkeleton = ArmsMesh ? ArmsMesh->GetSkeleton() : nullptr;
	if (!ArmsSkeleton)
	{
		SetStatus(LOCTEXT("NoArmsSkeleton", "대상 캐릭터 BP 에서 팔 스켈레톤을 찾지 못했습니다."));
		return FReply::Handled();
	}

	TSharedRef<SFPSRGunMotionNewClipDialog> Dialog = SNew(SFPSRGunMotionNewClipDialog);
	if (Dialog->ShowModal() != EAppReturnType::Ok)
	{
		return FReply::Handled();
	}

	FString AssetName = Dialog->GetAssetName();
	if (AssetName.IsEmpty())
	{
		SetStatus(LOCTEXT("NewClipNoName", "에셋 이름을 입력하세요."));
		return FReply::Handled();
	}
	// §20-5/§5: "저장 경로 = Anims_LPAMG 하위 + _GunMotion 접미 강제(§5 경고 규약 통과 위치)".
	if (!AssetName.EndsWith(TEXT("_GunLocked")) && !AssetName.EndsWith(TEXT("_GunMotion")))
	{
		AssetName += TEXT("_GunMotion");
	}

	const float LengthSeconds = FMath::Max(Dialog->GetLengthSeconds(), 1.0f / 30.0f);
	const FString FolderPath = Settings->NewActionClipFolder.Path.IsEmpty() ? TEXT("/Game/Character/FPArms/Anims_LPAMG") : Settings->NewActionClipFolder.Path;
	const FString PackageName = FolderPath / AssetName;

	// 같은 이름 에셋이 이미 있으면 조용히 덮지 않는다 — CreatePackage+NewObject 는 기존 오브젝트와 충돌한다.
	if (FPackageName::DoesPackageExist(PackageName))
	{
		SetStatus(FText::Format(LOCTEXT("NewClipAlreadyExists", "같은 이름의 에셋이 이미 있습니다: {0} — 다른 이름을 쓰세요."), FText::FromString(PackageName)));
		return FReply::Handled();
	}

	UPackage* Package = CreatePackage(*PackageName);
	if (!Package)
	{
		SetStatus(LOCTEXT("NewClipPackageFailed", "패키지 생성에 실패했습니다."));
		return FReply::Handled();
	}

	UAnimSequence* NewSeq = NewObject<UAnimSequence>(Package, FName(*AssetName), RF_Public | RF_Standalone);
	NewSeq->SetSkeleton(ArmsSkeleton);

	// §20-5: 본 트랙 0개, 30fps, 애디티브(AAT_LocalSpaceBase) + RefPoseType=ABPT_RefPose — 트랙이 없으므로
	// raw=refpose=base 가 구조적으로 성립해 델타가 항등 0 이다(A16 함정 "트랙 삭제 ≠ 델타 0"과 무관 — 여기는
	// 아예 처음부터 트랙이 없는 신규 클립).
	IAnimationDataController& Controller = NewSeq->GetController();
	Controller.InitializeModel();
	Controller.SetFrameRate(FFrameRate(30, 1));
	Controller.SetNumberOfFrames(FFrameNumber(FMath::RoundToInt(LengthSeconds * 30.0f)));
	NewSeq->AdditiveAnimType = AAT_LocalSpaceBase;
	NewSeq->RefPoseType = ABPT_RefPose;
	Controller.NotifyPopulated();

	// §20-5: "생성 직후 탭이 그 클립을 열고 AUD 부착, bSanitized=true 로 시작(본 트랙이 없어 고정화 불요)".
	UFPSRGunMotionAuthoringData* AuthData = NewObject<UFPSRGunMotionAuthoringData>(NewSeq);
	AuthData->bSanitized = true;
	NewSeq->AddAssetUserData(AuthData);

	NewSeq->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(NewSeq);

	const FString PackageFileName = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	const bool bSaved = UPackage::SavePackage(Package, NewSeq, *PackageFileName, SaveArgs);

	SelectedSequence = NewSeq;
	SelectedKeyIndex = INDEX_NONE;
	SetActiveChannelId(FPSRGunMotionChannelIds::Gun);
	RebuildKeyRows();
	RebuildChannelKeyRows();
	RebuildViewportPreview();

	SetStatus(bSaved
		? FText::Format(LOCTEXT("NewClipCreated", "새 액션 클립 생성 완료: {0}"), FText::FromString(AssetName))
		: FText::Format(LOCTEXT("NewClipCreatedNotSaved", "새 액션 클립을 만들었지만 저장에 실패했습니다: {0}"), FText::FromString(AssetName)));
	return FReply::Handled();
}

// ---------------------------------------------------------------------------------------------------------------
// 증보 v2.1 §11-§12: PIE 구도 캡처 + 자유시점
// ---------------------------------------------------------------------------------------------------------------

FReply SFPSRGunMotionTab::OnCaptureCompositionClicked()
{
	// §11: PIE 월드가 없으면 사유 토스트(기존 [PIE에서 재생] 버튼과 같은 처리).
	FWorldContext* PIEWorldContext = GEditor ? GEditor->GetPIEWorldContext() : nullptr;
	UWorld* PIEWorld = PIEWorldContext ? PIEWorldContext->World() : nullptr;
	if (!PIEWorld)
	{
		SetStatus(LOCTEXT("NoPIEForCapture", "PIE 가 실행 중이 아닙니다."));
		return FReply::Handled();
	}

	APlayerController* PC = PIEWorld->GetFirstPlayerController();
	AFPSRCharacter* Character = PC ? Cast<AFPSRCharacter>(PC->GetPawn()) : nullptr;
	if (!Character)
	{
		SetStatus(LOCTEXT("NoCharacterForCapture", "PIE 에서 로컬 플레이어 캐릭터를 찾지 못했습니다."));
		return FReply::Handled();
	}

	const UFPSRGunMotionSettings* Settings = GetDefault<UFPSRGunMotionSettings>();
	USkeletalMeshComponent* Arms = FindComponentByName<USkeletalMeshComponent>(Character, Settings->ArmsComponentName);
	UCameraComponent* Camera = FindComponentByName<UCameraComponent>(Character, Settings->CameraComponentName);
	if (!Arms || !Camera)
	{
		SetStatus(LOCTEXT("NoArmsOrCameraForCapture", "PIE 캐릭터에서 팔/카메라 컴포넌트를 찾지 못했습니다."));
		return FReply::Handled();
	}

	// §11: "실제 월드 트랜스폼" — 프로브 스폰(CDO 저작 배치)이 아니라 실행 중인 PIE 캐릭터라 런타임 보정
	// (시선 높이/스탠스 카메라 등)이 이미 반영돼 있다.
	const FTransform ArmsWorld = Arms->GetComponentTransform();
	const FTransform CameraWorld = Camera->GetComponentTransform();
	const FTransform CamRelArms = CameraWorld.GetRelativeTransform(ArmsWorld);

	FMinimalViewInfo View;
	Camera->GetCameraView(0.0f, View);

	UFPSRGunMotionSettings* MutableSettings = GetMutableDefault<UFPSRGunMotionSettings>();
	MutableSettings->CapturedCameraRelativeToArms = CamRelArms;
	MutableSettings->CapturedFOV = View.FOV;
	MutableSettings->bHasCapturedComposition = true;

	// 🚨 SaveConfig() 금지 — CDO 메모리만 바뀌고 체크인되는 ini 엔 안 남는다(프로젝트 실사고). 반환값을 확인한다.
	const bool bSaved = MutableSettings->TryUpdateDefaultConfigFile();
	SetStatus(bSaved
		? LOCTEXT("CaptureSaved", "PIE 구도를 캡처해 설정 파일에 저장했습니다.")
		: LOCTEXT("CaptureSaveFailed", "구도를 캡처했지만 설정 파일 저장에 실패했습니다(읽기 전용일 수 있습니다)."));

	// §11: "뷰포트 즉시 재적용".
	if (Viewport.IsValid())
	{
		if (const TSharedPtr<FFPSRGunMotionViewportClient> Client = Viewport->GetGunMotionClient())
		{
			Client->RefreshCameraComposition();
		}
	}
	return FReply::Handled();
}

ECheckBoxState SFPSRGunMotionTab::GetFreeLookState() const
{
	const TSharedPtr<FFPSRGunMotionViewportClient> Client = Viewport.IsValid() ? Viewport->GetGunMotionClient() : nullptr;
	return (Client.IsValid() && Client->IsFreeLook()) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SFPSRGunMotionTab::OnFreeLookStateChanged(ECheckBoxState NewState)
{
	if (Viewport.IsValid())
	{
		if (const TSharedPtr<FFPSRGunMotionViewportClient> Client = Viewport->GetGunMotionClient())
		{
			Client->SetFreeLook(NewState == ECheckBoxState::Checked);
		}
	}
}

FText SFPSRGunMotionTab::GetCompositionSourceText() const
{
	const UFPSRGunMotionSettings* Settings = GetDefault<UFPSRGunMotionSettings>();
	if (Settings && Settings->bHasCapturedComposition)
	{
		return LOCTEXT("CompositionSourceCaptured", "구도: 캡처 구도(PIE 실측)");
	}
	return LOCTEXT("CompositionSourceFallback", "구도: 기본(BP) 구도 — PIE 캡처 권장");
}

// ---------------------------------------------------------------------------------------------------------------
// 증보 v2.2 §14: PIE 라이브 링크
// ---------------------------------------------------------------------------------------------------------------

ECheckBoxState SFPSRGunMotionTab::GetPIELiveLinkState() const
{
	return bPIELiveLinkEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SFPSRGunMotionTab::OnPIELiveLinkStateChanged(ECheckBoxState NewState)
{
	bPIELiveLinkEnabled = (NewState == ECheckBoxState::Checked);
	if (!bPIELiveLinkEnabled)
	{
		// 사용자 승인 해석 ①: OFF 전환은 "탭/클립 닫기"와 같은 취지 — 마지막 저작 포즈에 얼어붙은 채로 남기지 않는다.
		StopMirrorMontage();
	}
}

FText SFPSRGunMotionTab::GetPIELiveLinkStatusText() const
{
	if (!bPIELiveLinkEnabled)
	{
		return LOCTEXT("PIELiveLinkOff", "PIE 라이브 링크: 꺼짐");
	}
	return MirrorAnimInstance.IsValid()
		? LOCTEXT("PIELiveLinkActive", "PIE 링크 활성")
		: LOCTEXT("PIELiveLinkInactive", "PIE 없음");
}

void SFPSRGunMotionTab::UpdatePIEMirror()
{
	const TSharedPtr<FFPSRGunMotionViewportClient> Client = Viewport.IsValid() ? Viewport->GetGunMotionClient() : nullptr;
	UAnimSequence* PreviewClip = (Client.IsValid() && Client->HasPreviewClip()) ? Client->GetPreviewClip() : nullptr;

	// §14 조건: "PIE 월드 존재 + 프리뷰 클립 유효" — 둘 중 하나라도 없으면 조용히 해제.
	FWorldContext* PIEWorldContext = GEditor ? GEditor->GetPIEWorldContext() : nullptr;
	UWorld* PIEWorld = PIEWorldContext ? PIEWorldContext->World() : nullptr;
	if (!PIEWorld || !PreviewClip)
	{
		if (MirrorAnimInstance.IsValid())
		{
			StopMirrorMontage();
		}
		return;
	}

	APlayerController* PC = PIEWorld->GetFirstPlayerController();
	AFPSRCharacter* Character = PC ? Cast<AFPSRCharacter>(PC->GetPawn()) : nullptr;
	const UFPSRGunMotionSettings* Settings = GetDefault<UFPSRGunMotionSettings>();
	USkeletalMeshComponent* Arms = Character ? FindComponentByName<USkeletalMeshComponent>(Character, Settings->ArmsComponentName) : nullptr;
	UAnimInstance* AnimInstance = Arms ? Arms->GetAnimInstance() : nullptr;
	if (!AnimInstance)
	{
		// PIE 는 살아 있지만 폰/팔을 아직(또는 더 이상) 못 찾음 — 리스폰 도중 등. 조용히 해제, 다음 틱에 재시도.
		if (MirrorAnimInstance.IsValid())
		{
			StopMirrorMontage();
		}
		return;
	}

	// §14 수명주기: 재조회한 AnimInstance/클립이 지금 물고 있는 것과 다르거나, 몽타주가 더 이상 살아있지 않으면
	// (PIE 재시작·리스폰·클립 재선택) (재)연결한다 — 최초 연결도 이 분기를 탄다(MirrorMontage 가 아직 비어 있으므로).
	const bool bNeedsReconnect =
		(MirrorAnimInstance.Get() != AnimInstance) ||
		(MirrorPlayingClip.Get() != PreviewClip) ||
		!MirrorMontage.IsValid() ||
		!AnimInstance->Montage_IsActive(MirrorMontage.Get());
	if (bNeedsReconnect)
	{
		StartOrRestartMirrorMontage(*AnimInstance, *PreviewClip, /*BlendTime=*/0.1f);
		return;
	}

	// 이미 연결됨 — 재생 상태/스크럽 위치 동기화.
	UAnimMontage* Montage = MirrorMontage.Get();
	const bool bTabPlaying = Client->IsPreviewPlaying();
	const float TabPosition = Client->GetPreviewPosition();
	const float MirrorPosition = AnimInstance->Montage_GetPosition(Montage);

	if (bTabPlaying)
	{
		// §14: "탭 [재생] 중 → 미러 몽타주도 재생, 매 틱 위치 오차 0.1s 이상이면 재동기화".
		if (!AnimInstance->Montage_IsPlaying(Montage))
		{
			AnimInstance->Montage_Resume(Montage);
		}
		if (FMath::Abs(MirrorPosition - TabPosition) >= 0.1f)
		{
			AnimInstance->Montage_SetPosition(Montage, TabPosition);
		}
	}
	else
	{
		// §14: "탭 스크럽 변경 → Montage_SetPosition + Montage_Pause (포즈 고정 동기화)".
		if (!FMath::IsNearlyEqual(MirrorPosition, TabPosition, 0.001f))
		{
			AnimInstance->Montage_SetPosition(Montage, TabPosition);
		}
		if (AnimInstance->Montage_IsPlaying(Montage))
		{
			AnimInstance->Montage_Pause(Montage);
		}
	}
}

void SFPSRGunMotionTab::StartOrRestartMirrorMontage(UAnimInstance& AnimInstance, UAnimSequence& PreviewClip, float BlendTime)
{
	const UFPSRGunMotionSettings* Settings = GetDefault<UFPSRGunMotionSettings>();
	const TSharedPtr<FFPSRGunMotionViewportClient> Client = Viewport.IsValid() ? Viewport->GetGunMotionClient() : nullptr;
	const float StartPosition = Client.IsValid() ? Client->GetPreviewPosition() : 0.0f;
	const bool bShouldPlay = Client.IsValid() && Client->IsPreviewPlaying();

	// InTimeToStartMontageAt 에 현재 스크럽 위치를 넘겨 시작과 동시에 그 위치에서 출발하게 한다(§14 "스크럽 위치로
	// 복원" — 재굽기 재시작·최초 연결·재연결이 전부 이 경로 하나를 공유한다).
	UAnimMontage* Montage = AnimInstance.PlaySlotAnimationAsDynamicMontage(
		&PreviewClip, Settings->PreviewSlotName, BlendTime, BlendTime,
		/*InPlayRate=*/1.0f, /*LoopCount=*/1, /*BlendOutTriggerTime=*/-1.0f, /*InTimeToStartMontageAt=*/StartPosition);

	MirrorAnimInstance = &AnimInstance;
	MirrorMontage = Montage;
	MirrorPlayingClip = &PreviewClip;

	if (Montage && !bShouldPlay)
	{
		AnimInstance.Montage_Pause(Montage);
	}
}

void SFPSRGunMotionTab::ResyncMirrorAfterRebake()
{
	if (!bPIELiveLinkEnabled || !MirrorAnimInstance.IsValid())
	{
		// 미러가 연결돼 있지 않으면 무동작 — 다음 Tick 의 UpdatePIEMirror 가 알아서 (재)연결한다.
		return;
	}

	UAnimInstance* AnimInstance = MirrorAnimInstance.Get();
	const TSharedPtr<FFPSRGunMotionViewportClient> Client = Viewport.IsValid() ? Viewport->GetGunMotionClient() : nullptr;
	UAnimSequence* PreviewClip = (Client.IsValid() && Client->HasPreviewClip()) ? Client->GetPreviewClip() : nullptr;
	if (!PreviewClip)
	{
		return;
	}

	// 사용자 승인 해석 ②: "정지 후 재시작(블렌드 0.05)"을 정지/재시작 양쪽 블렌드에 대칭 적용.
	if (UAnimMontage* OldMontage = MirrorMontage.Get())
	{
		AnimInstance->Montage_Stop(0.05f, OldMontage);
	}
	StartOrRestartMirrorMontage(*AnimInstance, *PreviewClip, /*BlendTime=*/0.05f);
}

void SFPSRGunMotionTab::StopMirrorMontage()
{
	if (UAnimInstance* AnimInstance = MirrorAnimInstance.Get())
	{
		if (UAnimMontage* Montage = MirrorMontage.Get())
		{
			AnimInstance->Montage_Stop(0.1f, Montage);
		}
	}
	MirrorAnimInstance.Reset();
	MirrorMontage.Reset();
	MirrorPlayingClip.Reset();
}

#undef LOCTEXT_NAMESPACE
