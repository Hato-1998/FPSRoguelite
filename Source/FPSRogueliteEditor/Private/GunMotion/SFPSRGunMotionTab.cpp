// Copyright Epic Games, Inc. All Rights Reserved.

#include "GunMotion/SFPSRGunMotionTab.h"

#include "GunMotion/FPSRGunMotionSettings.h"
#include "GunMotion/FPSRGunMotionBaker.h"
#include "GunMotion/FPSRGunMotionViewportClient.h"
#include "GunMotion/SFPSRGunMotionViewport.h"
#include "Anim/FPSRGunMotionAuthoringData.h"
#include "Core/FPSRLogChannels.h"
#include "Hero/FPSRCharacter.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimCurveTypes.h"   // FFloatCurve — §14 마지막: [총 고정화]의 LeftHandIKWeight 커브 존재 확인
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"   // §14 라이브 미러 — Montage_* 계열이 받는 UAnimMontage 완전 타입
#include "Camera/CameraComponent.h"   // §11 [PIE 구도 캡처] — FirstPersonCamera::GetCameraView
#include "Camera/CameraTypes.h"       // FMinimalViewInfo
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/PlayerController.h"

#include "AdvancedPreviewScene.h"   // §7: 이 탭이 단독 소유하는 프리뷰 씬
#include "PreviewScene.h"          // FPreviewScene::ConstructionValues
#include "Editor.h"
#include "FileHelpers.h"   // UEditorLoadingAndSavingUtils::SavePackages (UnrealEd) — 이 프로젝트가 기존에 EditorScriptingUtilities
                            // 플러그인(비활성)을 피해 온 관례를 그대로 따른다(FPSRWeaponAssemblerHelpers.cpp 주석 참조).
                            // 스펙 §4-5 는 UEditorAssetLibrary::SaveLoadedAsset 을 지목하지만 그 플러그인은 이
                            // 프로젝트에서 비활성 상태다 — 대신 이미 검증된 이 경로로 대체했다(보고서에 명시).
#include "Misc/MessageDialog.h"
#include "ScopedTransaction.h"

#include "PropertyEditorModule.h"
#include "PropertyCustomizationHelpers.h"
#include "AssetRegistry/AssetData.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"   // §12 자유시점 체크박스
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Text/STextBlock.h"

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

		// --- §8 타임라인: 재생/정지 + 스크럽 슬라이더 ---
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
				SAssignNew(ScrubSlider, SSlider)
				.MinValue(0.0f)
				.MaxValue(1.0f)
				.Value(this, &SFPSRGunMotionTab::GetScrubPosition)
				.OnMouseCaptureBegin(this, &SFPSRGunMotionTab::OnScrubCaptureBegin)
				.OnValueChanged(this, &SFPSRGunMotionTab::OnScrubPositionChanged)
			]
		]

		// --- §8 키 시각 마커 줄 ---
		+ SVerticalBox::Slot().AutoHeight().Padding(2.0f, 0.0f, 2.0f, 4.0f)
		[
			SAssignNew(KeyMarkerContainer, SHorizontalBox)
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
			]

			// --- 2. 상태 줄(§4-2) ---
			+ SVerticalBox::Slot().AutoHeight().Padding(2.0f, 4.0f)
			[
				SAssignNew(StatusText, STextBlock)
				.Text(this, &SFPSRGunMotionTab::GetStatusText)
			]

			// --- 나머지 전부: 비애디티브 클립이면 통째로 비활성(§4-2 "비애디티브 클립이면 전 기능 비활성") ---
			+ SVerticalBox::Slot().AutoHeight().Padding(2.0f)
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
			Client->OnGizmoCommit().BindSP(this, &SFPSRGunMotionTab::OnGizmoKeyCommitted);
		}
	}

	RebuildKeyRows();
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
	RebuildKeyRows();
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

	return SNew(SHorizontalBox)

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
	const TArray<FFPSRGunMotionKey> Keys = AuthData ? AuthData->Keys : TArray<FFPSRGunMotionKey>();

	Client->SetSourceClip(Seq, Keys);

	if (ScrubSlider.IsValid())
	{
		const float Length = Client->GetPreviewLength();
		ScrubSlider->SetMinAndMaxValues(0.0f, FMath::Max(UE_KINDA_SMALL_NUMBER, Length));
	}

	RebuildTimelineMarkers();

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
	const TArray<FFPSRGunMotionKey> Keys = AuthData ? AuthData->Keys : TArray<FFPSRGunMotionKey>();

	Client->RebakePreview(Keys);
	RebuildTimelineMarkers();

	// §14: "재굽기 직후 → 미러 몽타주를 정지 후 재시작(블렌드 0.05)" — 재생 중인 몽타주가 갱신된 압축 데이터를
	// 확실히 다시 읽게 하는 가장 단순한 보장. 미러가 연결돼 있지 않으면(§14 수명주기) 무동작.
	ResyncMirrorAfterRebake();
}

int32 SFPSRGunMotionTab::FindKeyIndexNearTime(float Time, float ToleranceSeconds) const
{
	UAnimSequence* Seq = GetSequence();
	const UFPSRGunMotionAuthoringData* AuthData = Seq ? Seq->GetAssetUserData<UFPSRGunMotionAuthoringData>() : nullptr;
	if (!AuthData)
	{
		return INDEX_NONE;
	}

	int32 BestIndex = INDEX_NONE;
	float BestDelta = ToleranceSeconds;
	for (int32 Index = 0; Index < AuthData->Keys.Num(); ++Index)
	{
		const float Delta = FMath::Abs(AuthData->Keys[Index].Time - Time);
		if (Delta <= BestDelta)
		{
			BestDelta = Delta;
			BestIndex = Index;
		}
	}
	return BestIndex;
}

void SFPSRGunMotionTab::OnGizmoKeyCommitted(float Time, FVector CamOffset, FRotator CamRotation)
{
	UAnimSequence* Seq = GetSequence();
	if (!Seq)
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("GizmoKeyCommitTransaction", "총 모션 키(기즈모)"));
	UFPSRGunMotionAuthoringData* AuthData = GetOrCreateAuthoringData(/*bCreateIfMissing=*/true);
	if (!AuthData)
	{
		SetStatus(LOCTEXT("AuthDataFailed", "저작 데이터를 준비하지 못했습니다."));
		return;
	}
	AuthData->Modify();

	// §9: "같은 시각(±1프레임)의 키가 있으면 갱신, 없으면 추가".
	const int32 ExistingIndex = FindKeyIndexNearTime(Time, FrameToleranceSeconds(Seq));
	if (AuthData->Keys.IsValidIndex(ExistingIndex))
	{
		AuthData->Keys[ExistingIndex].CamOffset = CamOffset;
		AuthData->Keys[ExistingIndex].CamRotation = CamRotation;
	}
	else
	{
		FFPSRGunMotionKey NewKey;
		NewKey.Time = Time;
		NewKey.CamOffset = CamOffset;
		NewKey.CamRotation = CamRotation;
		AuthData->Keys.Add(NewKey);
	}

	Seq->MarkPackageDirty();
	RebuildKeyRows();
	RebakeViewportPreview();
}

void SFPSRGunMotionTab::RebuildTimelineMarkers()
{
	if (!KeyMarkerContainer.IsValid())
	{
		return;
	}
	KeyMarkerContainer->ClearChildren();

	UAnimSequence* Seq = GetSequence();
	const UFPSRGunMotionAuthoringData* AuthData = Seq ? Seq->GetAssetUserData<UFPSRGunMotionAuthoringData>() : nullptr;
	const float Length = Seq ? Seq->GetPlayLength() : 0.0f;
	if (!AuthData || Length <= UE_KINDA_SMALL_NUMBER)
	{
		return;
	}

	// 시간 순으로 정렬한 사본으로 마커를 왼쪽부터 배치한다 — 저작 순서가 아니라 시각 순서로 보여야 타임라인처럼 읽힌다.
	TArray<FFPSRGunMotionKey> SortedKeys = AuthData->Keys;
	SortedKeys.Sort([](const FFPSRGunMotionKey& A, const FFPSRGunMotionKey& B) { return A.Time < B.Time; });

	float PrevTime = 0.0f;
	for (const FFPSRGunMotionKey& Key : SortedKeys)
	{
		const float GapFraction = FMath::Clamp((Key.Time - PrevTime) / Length, 0.0f, 1.0f);
		if (GapFraction > 0.0f)
		{
			KeyMarkerContainer->AddSlot().FillWidth(GapFraction)[SNew(SSpacer)];
		}
		KeyMarkerContainer->AddSlot().AutoWidth()
		[
			SNew(STextBlock).Text(LOCTEXT("KeyMarkerGlyph", "|")).ToolTipText(FText::AsNumber(Key.Time))
		];
		PrevTime = Key.Time;
	}
	const float TailFraction = FMath::Clamp((Length - PrevTime) / Length, 0.0f, 1.0f);
	if (TailFraction > 0.0f)
	{
		KeyMarkerContainer->AddSlot().FillWidth(TailFraction)[SNew(SSpacer)];
	}
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
	UAnimSequence* Seq = GetSequence();
	if (!Seq || !Viewport.IsValid())
	{
		SetStatus(LOCTEXT("NoClipSelected", "클립을 먼저 선택하세요."));
		return FReply::Handled();
	}
	const TSharedPtr<FFPSRGunMotionViewportClient> Client = Viewport->GetGunMotionClient();
	const float Time = Client.IsValid() ? Client->GetPreviewPosition() : 0.0f;

	const FScopedTransaction Transaction(LOCTEXT("KeyAtTimeTransaction", "총 모션 키 추가(현재 시각)"));
	UFPSRGunMotionAuthoringData* AuthData = GetOrCreateAuthoringData(/*bCreateIfMissing=*/true);
	if (!AuthData)
	{
		SetStatus(LOCTEXT("AuthDataFailed", "저작 데이터를 준비하지 못했습니다."));
		return FReply::Handled();
	}
	AuthData->Modify();

	const int32 ExistingIndex = FindKeyIndexNearTime(Time, FrameToleranceSeconds(Seq));
	if (!AuthData->Keys.IsValidIndex(ExistingIndex))
	{
		FFPSRGunMotionKey NewKey;
		NewKey.Time = Time;
		AuthData->Keys.Add(NewKey);
	}

	Seq->MarkPackageDirty();
	RebuildKeyRows();
	RebakeViewportPreview();
	return FReply::Handled();
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

	UFPSRGunMotionAuthoringData* AuthData = Seq->GetAssetUserData<UFPSRGunMotionAuthoringData>();
	const int32 ExistingIndex = AuthData ? FindKeyIndexNearTime(Time, FrameToleranceSeconds(Seq)) : INDEX_NONE;
	if (!AuthData || !AuthData->Keys.IsValidIndex(ExistingIndex))
	{
		return FReply::Handled();
	}

	const FScopedTransaction Transaction(LOCTEXT("DeleteKeyAtTimeTransaction", "총 모션 키 삭제(현재 시각)"));
	AuthData->Modify();
	AuthData->Keys.RemoveAt(ExistingIndex);

	Seq->MarkPackageDirty();
	RebuildKeyRows();
	RebakeViewportPreview();
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
