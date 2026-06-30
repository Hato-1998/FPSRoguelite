// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Subsystems/GameInstanceSubsystem.h"
#include "Engine/EngineBaseTypes.h"            // ETravelFailure
#include "Net/Core/Connection/NetEnums.h"      // ENetworkFailure
#include "FPSRFlowLogSubsystem.generated.h"

class UNetDriver;

/**
 * Owns the flow-log lifecycle for the whole GameInstance. It does the *automatic* branch-point capture so the
 * rest of the code only has to log its own intentful events (FPSRFlowLog::Event):
 *   - GameInstance start / shutdown
 *   - Every map load (covers menu → lobby → run → menu travels)
 *   - Network failures and travel failures — the smoking gun when a Steam join silently bounces to the menu.
 */
UCLASS()
class FPSROGUELITE_API UFPSRFlowLogSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

private:
	void HandlePostLoadMap(UWorld* LoadedWorld);
	void HandleNetworkFailure(UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& ErrorString);
	void HandleTravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& ErrorString);

	FDelegateHandle PostLoadMapHandle;
	FDelegateHandle NetworkFailureHandle;
	FDelegateHandle TravelFailureHandle;
};
