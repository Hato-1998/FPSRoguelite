// Copyright Epic Games, Inc. All Rights Reserved.

#include "Assembler/SFPSRWeaponAssemblerFPTab.h"

#include "Assembler/FPSRWeaponAssemblerFPViewportClient.h"
#include "Assembler/FPSRWeaponAssemblerSettings.h"
#include "Assembler/SFPSRWeaponAssemblerFPViewport.h"
#include "Assembler/SFPSRWeaponAssemblerTab.h"
#include "FPSRogueliteEditorModule.h"

#include "AdvancedPreviewScene.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SFPSRWeaponAssemblerFPTab"

void SFPSRWeaponAssemblerFPTab::Construct(const FArguments& InArgs)
{
	// 화면비 목록은 설정에서 온다 — C++ 에 박지 않는다(새 종횡비는 ini 한 줄로 추가된다).
	const UFPSRWeaponAssemblerSettings* Settings = GetDefault<UFPSRWeaponAssemblerSettings>();
	if (Settings)
	{
		for (const FFPSRAssemblerAspectPreset& Preset : Settings->AspectPresets)
		{
			TSharedPtr<FAspectOption> Option = MakeShared<FAspectOption>();
			Option->Label = FText::FromString(Preset.Label);
			Option->Aspect = Preset.AspectRatio;
			AspectOptions.Add(Option);
		}
	}
	if (AspectOptions.Num() == 0)
	{
		// 설정이 통째로 비어 있어도 툴이 죽지 않게 — 16:9 하나만 세운다(사유는 상태줄이 아니라 여기 주석으로 충분:
		// 프로젝트 설정에서 목록을 지운 사람만 만나는 상태다).
		TSharedPtr<FAspectOption> Option = MakeShared<FAspectOption>();
		Option->Label = LOCTEXT("DefaultAspect", "16:9");
		Option->Aspect = 16.0f / 9.0f;
		AspectOptions.Add(Option);
	}
	SelectedAspect = AspectOptions[0];

	ChildSlot
	[
		SNew(SVerticalBox)

		// --- 툴바: 화면비 + 구도 다시 읽기 ---------------------------------------------------------------------
		+ SVerticalBox::Slot().AutoHeight().Padding(4.0f, 4.0f, 4.0f, 2.0f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 6.0f, 0.0f)
			[
				SNew(STextBlock).Text(LOCTEXT("AspectPrompt", "화면비:"))
			]

			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 12.0f, 0.0f)
			[
				SNew(SComboBox<TSharedPtr<FAspectOption>>)
				.OptionsSource(&AspectOptions)
				.OnGenerateWidget(this, &SFPSRWeaponAssemblerFPTab::MakeAspectOptionWidget)
				.OnSelectionChanged(this, &SFPSRWeaponAssemblerFPTab::OnAspectSelectionChanged)
				.InitiallySelectedItem(SelectedAspect)
				.ToolTipText(LOCTEXT("AspectTooltip", "게임 화면의 가로:세로 비율입니다. 이 비율로 화면을 고정하고 남는 자리는 검은 띠로 채웁니다.\n\nFHD(1920×1080)와 QHD(2560×1440)는 둘 다 16:9라 구도가 완전히 같습니다 — 구도를 바꾸는 것은 해상도가 아니라 비율입니다.\n비율을 바꾸면 세로로 보이는 범위는 그대로 두고 가로만 넓어집니다(게임과 같은 방식). 목록은 프로젝트 설정 > FPSR > 무기 어셈블러 에서 고칠 수 있습니다."))
				[
					SNew(STextBlock).Text(this, &SFPSRWeaponAssemblerFPTab::GetSelectedAspectLabel)
				]
			]

			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SButton)
				.Text(LOCTEXT("RefreshComposition", "구도 다시 읽기"))
				.ToolTipText(LOCTEXT("RefreshCompositionTooltip", "플레이어 캐릭터 BP에서 카메라와 팔의 위치·FOV를 다시 읽어옵니다. BP의 카메라나 팔 위치를 고친 뒤 누르세요(에디터를 껐다 켤 필요가 없습니다)."))
				.OnClicked(this, &SFPSRWeaponAssemblerFPTab::OnRefreshCompositionClicked)
			]
		]

		// --- 숫자 계기 -----------------------------------------------------------------------------------------
		+ SVerticalBox::Slot().AutoHeight().Padding(4.0f, 0.0f, 4.0f, 2.0f)
		[
			SNew(STextBlock)
			.Text(this, &SFPSRWeaponAssemblerFPTab::GetReadoutText)
			.ToolTipText(LOCTEXT("ReadoutTooltip", "지금 이 화면이 실제로 쓰고 있는 값입니다. '세로'는 화면비를 바꿔도 변하지 않아야 게임과 같은 구도입니다."))
		]

		+ SVerticalBox::Slot().AutoHeight().Padding(4.0f, 0.0f, 4.0f, 4.0f)
		[
			SNew(STextBlock)
			.Text(this, &SFPSRWeaponAssemblerFPTab::GetStatusText)
			.AutoWrapText(true)
			.ColorAndOpacity(FLinearColor(1.0f, 0.55f, 0.1f))
		]

		// --- 뷰포트 자리 ---------------------------------------------------------------------------------------
		+ SVerticalBox::Slot().FillHeight(1.0f)
		[
			SAssignNew(ViewportContainer, SBox)
		]
	];

	RebuildViewport();
}

bool SFPSRWeaponAssemblerFPTab::IsSceneCurrent() const
{
	const TSharedPtr<SFPSRWeaponAssemblerTab> Tab = AssemblerTab.Pin();
	return Tab.IsValid() && HeldScene.IsValid() && Tab->GetPreviewScene() == HeldScene;
}

void SFPSRWeaponAssemblerFPTab::RebuildViewport()
{
	// 지금 살아 있는 조립기 탭을 다시 찾는다 — 우리가 들고 있던 것이 이미 닫혔을 수 있다.
	AssemblerTab = FFPSRogueliteEditorModule::GetLiveWeaponAssemblerTab();
	const TSharedPtr<SFPSRWeaponAssemblerTab> Tab = AssemblerTab.Pin();
	if (!Tab.IsValid())
	{
		ShowDisconnected(LOCTEXT("NoAssemblerTab", "무기 파츠 조립기 탭이 열려 있지 않습니다. 이 1인칭 뷰는 조립기 탭의 프리뷰를 그대로 보여주는 창이라, 먼저 Tools > FPSR > '무기 파츠 조립기…'를 열어야 합니다."));
		return;
	}

	const TSharedPtr<FAdvancedPreviewScene> Scene = Tab->GetPreviewScene();
	if (!Scene.IsValid())
	{
		ShowDisconnected(LOCTEXT("NoScene", "조립기 탭의 프리뷰 씬을 아직 쓸 수 없습니다. 조립기 탭을 한 번 눌러 활성화한 뒤 [다시 연결]을 누르세요."));
		return;
	}

	// 🚨 여기서 씬을 공동 소유한다 — 이 시점 이후로는 조립기 탭이 닫혀도 아래 뷰포트 클라이언트가 매달리지 않는다.
	HeldScene = Scene;

	ViewportContainer->SetContent(
		SAssignNew(FPViewport, SFPSRWeaponAssemblerFPViewport, HeldScene.ToSharedRef(), AssemblerTab));
	bConnected = true;

	// 콤보에 떠 있는 값을 새 클라이언트에 밀어 넣는다. 안 하면 클라이언트 기본값(16:9)과 콤보 표시가 갈려서,
	// 화면은 16:9 인데 라벨은 21:9 라고 말하는 상태가 된다 — 계기가 거짓말을 하면 계기가 없느니만 못하다.
	if (SelectedAspect.IsValid())
	{
		if (const TSharedPtr<FFPSRWeaponAssemblerFPViewportClient> Client = FPViewport->GetFPClient())
		{
			Client->SetTargetAspect(SelectedAspect->Aspect);
		}
	}
}

void SFPSRWeaponAssemblerFPTab::ShowDisconnected(const FText& Reason)
{
	// 순서가 중요하다: 뷰포트(그리고 그 안의 클라이언트)를 **먼저** 없애고 나서 씬 참조를 놓는다.
	// 클라이언트는 FPreviewScene& 를 들고 있으므로 씬을 먼저 놓으면 파괴 순서에 따라 매달릴 수 있다.
	if (ViewportContainer.IsValid())
	{
		ViewportContainer->SetContent(
			SNew(SVerticalBox)

			+ SVerticalBox::Slot().FillHeight(1.0f).VAlign(VAlign_Center).HAlign(HAlign_Center).Padding(16.0f)
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.0f, 0.0f, 0.0f, 10.0f)
				[
					SNew(STextBlock).Text(Reason).AutoWrapText(true).Justification(ETextJustify::Center)
				]

				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
				[
					SNew(SButton)
					.Text(LOCTEXT("Reconnect", "다시 연결"))
					.ToolTipText(LOCTEXT("ReconnectTooltip", "지금 열려 있는 조립기 탭의 프리뷰에 이 창을 다시 붙입니다."))
					.OnClicked_Lambda([this]() { RebuildViewport(); return FReply::Handled(); })
				]
			]);
	}

	FPViewport.Reset();
	HeldScene.Reset();
	bConnected = false;
}

void SFPSRWeaponAssemblerFPTab::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	// 상태 **전이**일 때만 위젯을 갈아 끼운다 — 매 프레임 SetContent 를 부르면 트리를 계속 헐고 짓는다.
	if (bConnected && !IsSceneCurrent())
	{
		// 조립기 탭이 닫혔거나, 닫았다 다시 열려 **새 씬**이 생겼다. 우리가 쥔 씬은 이제 아무도 안 쓰는 낡은
		// 씬이라 그대로 그리면 빈 화면을 정상인 것처럼 보여주게 된다 — 멈추고 사유를 알린다.
		ShowDisconnected(LOCTEXT("SceneStale", "조립기 탭이 닫혔거나 다시 열려 프리뷰가 새로 만들어졌습니다. 지금 보이던 화면은 더 이상 조립기와 연결돼 있지 않아 렌더를 멈췄습니다. [다시 연결]을 누르세요."));
	}
}

TSharedRef<SWidget> SFPSRWeaponAssemblerFPTab::MakeAspectOptionWidget(TSharedPtr<FAspectOption> Option) const
{
	return SNew(STextBlock).Text(Option.IsValid() ? Option->Label : FText::GetEmpty());
}

void SFPSRWeaponAssemblerFPTab::OnAspectSelectionChanged(TSharedPtr<FAspectOption> Option, ESelectInfo::Type SelectInfo)
{
	if (!Option.IsValid())
	{
		return;
	}
	SelectedAspect = Option;

	if (FPViewport.IsValid())
	{
		if (const TSharedPtr<FFPSRWeaponAssemblerFPViewportClient> Client = FPViewport->GetFPClient())
		{
			Client->SetTargetAspect(Option->Aspect);
		}
	}
}

FText SFPSRWeaponAssemblerFPTab::GetSelectedAspectLabel() const
{
	return SelectedAspect.IsValid() ? SelectedAspect->Label : FText::GetEmpty();
}

FReply SFPSRWeaponAssemblerFPTab::OnRefreshCompositionClicked()
{
	if (FPViewport.IsValid())
	{
		if (const TSharedPtr<FFPSRWeaponAssemblerFPViewportClient> Client = FPViewport->GetFPClient())
		{
			Client->RefreshComposition();
		}
	}
	return FReply::Handled();
}

FText SFPSRWeaponAssemblerFPTab::GetReadoutText() const
{
	if (!FPViewport.IsValid())
	{
		return LOCTEXT("ReadoutDisconnected", "— 연결 안 됨 —");
	}
	const TSharedPtr<FFPSRWeaponAssemblerFPViewportClient> Client = FPViewport->GetFPClient();
	if (!Client.IsValid() || !Client->IsCompositionValid())
	{
		return LOCTEXT("ReadoutNoComposition", "— 구도를 읽지 못했습니다 —");
	}

	const FTransform& Rel = Client->GetCameraRelativeToArms();
	const FVector L = Rel.GetLocation();
	const FRotator R = Rel.GetRotation().Rotator();

	// 한 줄에 다 담는다: 카메라가 팔에 대해 어디에 있는지 / 원본과 적용 FOV / 지금 세로로 몇 도를 보는지.
	// 세로 값이 화면비를 바꿔도 그대로여야 게임과 같은 구도다 — 그게 이 표시의 핵심이다.
	return FText::FromString(FString::Printf(
		TEXT("카메라(팔 기준) 위치 (%.2f, %.2f, %.2f) · 회전 (p %.2f, y %.2f, r %.2f)   |   ")
		TEXT("원본 FOV %.2f° @ %.4f   →   적용 FOV %.2f° @ %.4f   |   세로 %.2f°%s"),
		L.X, L.Y, L.Z, R.Pitch, R.Yaw, R.Roll,
		Client->GetSourceFOV(), Client->GetSourceAspect(),
		Client->GetAppliedFOV(), Client->GetTargetAspect(),
		Client->GetAppliedVerticalFOV(),
		Client->HasArms() ? TEXT("") : TEXT("   |   ⚠ 팔 없음")));
}

FText SFPSRWeaponAssemblerFPTab::GetStatusText() const
{
	if (!FPViewport.IsValid())
	{
		return FText::GetEmpty();
	}
	const TSharedPtr<FFPSRWeaponAssemblerFPViewportClient> Client = FPViewport->GetFPClient();
	if (!Client.IsValid())
	{
		return FText::GetEmpty();
	}

	if (!Client->GetIssue().IsEmpty())
	{
		return FText::FromString(Client->GetIssue());
	}
	if (!Client->HasArms())
	{
		// 구도는 읽혔는데 기준이 될 팔이 없다 — 카메라는 원점 기준으로 서 있고 판정은 의미가 없다.
		return LOCTEXT("NoArms", "조립기 탭에서 '팔 보기'가 꺼져 있습니다. 1인칭 구도는 팔을 기준으로 잡히므로, 켜야 실제 화면과 같아집니다.");
	}
	return FText::GetEmpty();
}

#undef LOCTEXT_NAMESPACE
