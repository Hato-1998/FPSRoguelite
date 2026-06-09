// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/FPSRRunHUDWidget.h"
#include "Core/FPSRGameState.h"
#include "Engine/World.h"

void UFPSRRunHUDWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (AFPSRGameState* GS = GetWorld() ? GetWorld()->GetGameState<AFPSRGameState>() : nullptr)
	{
		GS->OnRunStateChanged.AddDynamic(this, &UFPSRRunHUDWidget::HandleRunStateChanged);
	}

	OnRunStateUpdated();
}

void UFPSRRunHUDWidget::NativeDestruct()
{
	if (AFPSRGameState* GS = GetWorld() ? GetWorld()->GetGameState<AFPSRGameState>() : nullptr)
	{
		GS->OnRunStateChanged.RemoveDynamic(this, &UFPSRRunHUDWidget::HandleRunStateChanged);
	}

	Super::NativeDestruct();
}

void UFPSRRunHUDWidget::HandleRunStateChanged()
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
