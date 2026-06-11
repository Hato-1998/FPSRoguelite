// Copyright Epic Games, Inc. All Rights Reserved.

#include "Core/FPSRGameFlowSubsystem.h"
#include "Core/FPSRGameFlowSettings.h"
#include "Core/FPSRLogChannels.h"
#include "Kismet/GameplayStatics.h"

void UFPSRGameFlowSubsystem::StartRun()
{
	ClearLastRunOutcome();

	const UFPSRGameFlowSettings* Settings = GetDefault<UFPSRGameFlowSettings>();
	if (!Settings)
	{
		UE_LOG(LogFPSR, Error, TEXT("[GameFlow] UFPSRGameFlowSettings not found"));
		return;
	}

	FName RunMapName = Settings->GetLevelPackageName(Settings->RunMap);
	if (RunMapName == NAME_None)
	{
		UE_LOG(LogFPSR, Error, TEXT("[GameFlow] RunMap is null or invalid"));
		return;
	}

	UGameplayStatics::OpenLevel(this, RunMapName, true, Settings->RunTravelOptions);
}

void UFPSRGameFlowSubsystem::ReturnToMenu(EFPSRRunOutcome Outcome)
{
	LastRunOutcome = Outcome;

	const UFPSRGameFlowSettings* Settings = GetDefault<UFPSRGameFlowSettings>();
	if (!Settings)
	{
		UE_LOG(LogFPSR, Error, TEXT("[GameFlow] UFPSRGameFlowSettings not found"));
		return;
	}

	FName MenuMapName = Settings->GetLevelPackageName(Settings->MainMenuMap);
	if (MenuMapName == NAME_None)
	{
		UE_LOG(LogFPSR, Error, TEXT("[GameFlow] MainMenuMap is null or invalid"));
		return;
	}

	UGameplayStatics::OpenLevel(this, MenuMapName, true);
}
