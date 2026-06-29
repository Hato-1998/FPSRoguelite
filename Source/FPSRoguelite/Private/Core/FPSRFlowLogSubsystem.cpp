// Copyright Epic Games, Inc. All Rights Reserved.

#include "Core/FPSRFlowLogSubsystem.h"
#include "Core/FPSRFlowLog.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "UObject/UObjectGlobals.h"   // FCoreUObjectDelegates

void UFPSRFlowLogSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	FPSRFlowLog::Event(TEXT("BOOT"), FString::Printf(TEXT("GameInstance start — log file: %s"), *FPSRFlowLog::GetLogFilePath()));

	// Automatic capture: every map load + every network/travel failure (the latter is why a failed Steam join
	// silently returns to the main menu — the engine browses to GameDefaultMap on a client connection failure).
	PostLoadMapHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &UFPSRFlowLogSubsystem::HandlePostLoadMap);

	if (GEngine)
	{
		NetworkFailureHandle = GEngine->OnNetworkFailure().AddUObject(this, &UFPSRFlowLogSubsystem::HandleNetworkFailure);
		TravelFailureHandle = GEngine->OnTravelFailure().AddUObject(this, &UFPSRFlowLogSubsystem::HandleTravelFailure);
	}
}

void UFPSRFlowLogSubsystem::Deinitialize()
{
	FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadMapHandle);
	if (GEngine)
	{
		GEngine->OnNetworkFailure().Remove(NetworkFailureHandle);
		GEngine->OnTravelFailure().Remove(TravelFailureHandle);
	}

	FPSRFlowLog::Event(TEXT("BOOT"), TEXT("GameInstance shutdown"));

	Super::Deinitialize();
}

void UFPSRFlowLogSubsystem::HandlePostLoadMap(UWorld* LoadedWorld)
{
	if (!LoadedWorld)
	{
		return;
	}
	FPSRFlowLog::Event(LoadedWorld, TEXT("MAP"),
		FString::Printf(TEXT("Loaded map '%s'"), *LoadedWorld->GetMapName()));
}

void UFPSRFlowLogSubsystem::HandleNetworkFailure(UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& ErrorString)
{
	FPSRFlowLog::Event(World, TEXT("NETFAIL"),
		FString::Printf(TEXT("%s — %s"), ENetworkFailure::ToString(FailureType), *ErrorString));
}

void UFPSRFlowLogSubsystem::HandleTravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& ErrorString)
{
	FPSRFlowLog::Event(World, TEXT("TRAVELFAIL"),
		FString::Printf(TEXT("%s — %s"), ETravelFailure::ToString(FailureType), *ErrorString));
}
