// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/FPSRRunHUDWidget.h"
#include "Core/FPSRGameState.h"
#include "Core/FPSRPlayerState.h"
#include "Engine/World.h"
#include "TimerManager.h"

void UFPSRRunHUDWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (AFPSRGameState* GS = GetWorld() ? GetWorld()->GetGameState<AFPSRGameState>() : nullptr)
	{
		GS->OnRunStateChanged.AddDynamic(this, &UFPSRRunHUDWidget::HandleRunStateChanged);
	}

	EnsurePlayerStateBound();

	// If the owning PlayerState hasn't replicated yet, retry on a timer — OnRunStateChanged alone can't be
	// relied on (the run may already be paused, in which case no run-state events fire to drive the rebind).
	if (!bPlayerStateBound)
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(PlayerStateBindRetryHandle, this,
				&UFPSRRunHUDWidget::RetryPlayerStateBind, 0.25f, true);
		}
	}

	OnRunStateUpdated();
}

void UFPSRRunHUDWidget::EnsurePlayerStateBound()
{
	if (bPlayerStateBound)
	{
		return;
	}

	if (AFPSRPlayerState* PS = GetOwningPlayerState<AFPSRPlayerState>())
	{
		PS->OnCardPicksChanged.AddDynamic(this, &UFPSRRunHUDWidget::HandleCardPicksChanged);
		bPlayerStateBound = true;
	}
}

void UFPSRRunHUDWidget::RetryPlayerStateBind()
{
	EnsurePlayerStateBound();
	if (bPlayerStateBound)
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(PlayerStateBindRetryHandle);
		}
		OnRunStateUpdated(); // pick data is now available — refresh the display
	}
}

void UFPSRRunHUDWidget::NativeDestruct()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PlayerStateBindRetryHandle);
	}

	if (AFPSRGameState* GS = GetWorld() ? GetWorld()->GetGameState<AFPSRGameState>() : nullptr)
	{
		GS->OnRunStateChanged.RemoveDynamic(this, &UFPSRRunHUDWidget::HandleRunStateChanged);
	}

	if (AFPSRPlayerState* PS = GetOwningPlayerState<AFPSRPlayerState>())
	{
		PS->OnCardPicksChanged.RemoveDynamic(this, &UFPSRRunHUDWidget::HandleCardPicksChanged);
	}
	bPlayerStateBound = false; // re-add must rebind (NativeConstruct skips when already bound)

	Super::NativeDestruct();
}

void UFPSRRunHUDWidget::HandleRunStateChanged()
{
	EnsurePlayerStateBound();
	OnRunStateUpdated();
}

void UFPSRRunHUDWidget::HandleCardPicksChanged()
{
	OnRunStateUpdated();
}

float UFPSRRunHUDWidget::GetRunClockSeconds() const
{
	const AFPSRGameState* GS = GetWorld() ? GetWorld()->GetGameState<AFPSRGameState>() : nullptr;
	return GS ? GS->GetRunClockSeconds() : 0.0f;
}

FText UFPSRRunHUDWidget::GetRunClockText() const
{
	const int32 Total = FMath::Max(0, FMath::FloorToInt(GetRunClockSeconds()));
	const int32 Mins = Total / 60;
	const int32 Secs = Total % 60;
	return FText::FromString(FString::Printf(TEXT("%02d:%02d"), Mins, Secs));
}

ERunPhase UFPSRRunHUDWidget::GetRunPhase() const
{
	const AFPSRGameState* GS = GetWorld() ? GetWorld()->GetGameState<AFPSRGameState>() : nullptr;
	return GS ? GS->GetRunPhase() : ERunPhase::Combat;
}

bool UFPSRRunHUDWidget::IsRunPaused() const
{
	const AFPSRGameState* GS = GetWorld() ? GetWorld()->GetGameState<AFPSRGameState>() : nullptr;
	return GS ? GS->IsRunPaused() : false;
}

int32 UFPSRRunHUDWidget::GetPartyLevel() const
{
	const AFPSRGameState* GS = GetWorld() ? GetWorld()->GetGameState<AFPSRGameState>() : nullptr;
	return GS ? GS->GetPartyLevel() : 0;
}

int32 UFPSRRunHUDWidget::GetSharedXP() const
{
	const AFPSRGameState* GS = GetWorld() ? GetWorld()->GetGameState<AFPSRGameState>() : nullptr;
	return GS ? GS->GetSharedXP() : 0;
}

int32 UFPSRRunHUDWidget::GetRequiredXPForNextLevel() const
{
	const AFPSRGameState* GS = GetWorld() ? GetWorld()->GetGameState<AFPSRGameState>() : nullptr;
	return GS ? GS->GetRequiredXPForNextLevel() : 0;
}

float UFPSRRunHUDWidget::GetXPProgress01() const
{
	const int32 Req = GetRequiredXPForNextLevel();
	return Req > 0 ? FMath::Clamp(static_cast<float>(GetSharedXP()) / static_cast<float>(Req), 0.0f, 1.0f) : 0.0f;
}

int32 UFPSRRunHUDWidget::GetLocalCardPicksPending() const
{
	AFPSRPlayerState* PS = GetOwningPlayerState<AFPSRPlayerState>();
	return PS ? PS->GetCardPicksPending() : 0;
}

int32 UFPSRRunHUDWidget::GetLocalMissionRewardPicksPending() const
{
	AFPSRPlayerState* PS = GetOwningPlayerState<AFPSRPlayerState>();
	return PS ? PS->GetMissionRewardPicksPending() : 0;
}
