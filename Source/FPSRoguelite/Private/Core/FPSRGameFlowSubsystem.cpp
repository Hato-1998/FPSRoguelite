// Copyright Epic Games, Inc. All Rights Reserved.

#include "Core/FPSRGameFlowSubsystem.h"
#include "Core/FPSRGameFlowSettings.h"
#include "Core/FPSRLogChannels.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	/** Both entry points here travel the whole party via OpenLevel, which only the authority may decide. The header
	 *  already declares them authority-only but both are BlueprintCallable, so a BP button on a client could reach
	 *  them — and OpenLevel on a client sets that client's own TravelURL, quietly dropping it out of the party into
	 *  a solo map while everyone else plays on. Same whitelist idiom as the debug travel commands in FPSRGameMode. */
	bool FPSRFlowHasTravelAuthority(const UWorld* World, const TCHAR* Context)
	{
		if (World && (World->IsNetMode(NM_ListenServer) || World->IsNetMode(NM_Standalone) || World->IsNetMode(NM_DedicatedServer)))
		{
			return true;
		}
		UE_LOG(LogFPSR, Warning, TEXT("[GameFlow] %s ignored — not the travel authority (client)."), Context);
		return false;
	}
}

void UFPSRGameFlowSubsystem::StartRun()
{
	if (!FPSRFlowHasTravelAuthority(GetWorld(), TEXT("StartRun")))
	{
		return;
	}

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
	if (!FPSRFlowHasTravelAuthority(GetWorld(), TEXT("ReturnToMenu")))
	{
		return;
	}

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
