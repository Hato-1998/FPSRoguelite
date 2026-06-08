// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/FPSRXPBarWidget.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Core/FPSRGameState.h"
#include "Core/FPSRPlayerState.h"
#include "Engine/World.h"
#include "Input/UIActionBindingHandle.h"
#include "CommonInputModeTypes.h"

void UFPSRXPBarWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (AFPSRGameState* GS = GetWorld() ? GetWorld()->GetGameState<AFPSRGameState>() : nullptr)
	{
		GS->OnRunStateChanged.AddDynamic(this, &UFPSRXPBarWidget::HandleRunStateChanged);
	}

	EnsurePlayerStateBound();
	UpdateDisplay();
}

void UFPSRXPBarWidget::EnsurePlayerStateBound()
{
	if (bPlayerStateBound)
	{
		return;
	}

	if (AFPSRPlayerState* PS = GetOwningPlayerState<AFPSRPlayerState>())
	{
		PS->OnCardPicksChanged.AddDynamic(this, &UFPSRXPBarWidget::HandleCardPicksChanged);
		bPlayerStateBound = true;
	}
}

void UFPSRXPBarWidget::NativeDestruct()
{
	if (AFPSRGameState* GS = GetWorld() ? GetWorld()->GetGameState<AFPSRGameState>() : nullptr)
	{
		GS->OnRunStateChanged.RemoveDynamic(this, &UFPSRXPBarWidget::HandleRunStateChanged);
	}

	if (AFPSRPlayerState* PS = GetOwningPlayerState<AFPSRPlayerState>())
	{
		PS->OnCardPicksChanged.RemoveDynamic(this, &UFPSRXPBarWidget::HandleCardPicksChanged);
	}
	bPlayerStateBound = false; // re-add must rebind (NativeConstruct skips when already bound)

	Super::NativeDestruct();
}

TOptional<FUIInputConfig> UFPSRXPBarWidget::GetDesiredInputConfig() const
{
	return FUIInputConfig(ECommonInputMode::Game, EMouseCaptureMode::CapturePermanently);
}

void UFPSRXPBarWidget::HandleRunStateChanged()
{
	// PlayerState may have arrived after construct (remote clients) — catch up the pick binding.
	EnsurePlayerStateBound();
	UpdateDisplay();
}

void UFPSRXPBarWidget::HandleCardPicksChanged()
{
	UpdateDisplay();
}

void UFPSRXPBarWidget::UpdateDisplay()
{
	if (AFPSRGameState* GS = GetWorld() ? GetWorld()->GetGameState<AFPSRGameState>() : nullptr)
	{
		const int32 Level = GS->GetPartyLevel();
		const int32 XP = GS->GetSharedXP();
		const int32 Required = GS->GetRequiredXPForNextLevel();

		if (XPBar)
		{
			XPBar->SetPercent(Required > 0 ? static_cast<float>(XP) / static_cast<float>(Required) : 0.0f);
		}
		if (LevelText)
		{
			LevelText->SetText(FText::AsNumber(Level));
		}
	}

	int32 Picks = 0;
	if (AFPSRPlayerState* PS = GetOwningPlayerState<AFPSRPlayerState>())
	{
		Picks = PS->GetCardPicksPending();
	}
	if (StackText)
	{
		StackText->SetText(FText::AsNumber(Picks));
	}
}
