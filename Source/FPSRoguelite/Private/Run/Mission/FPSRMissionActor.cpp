// Copyright Epic Games, Inc. All Rights Reserved.

#include "Run/Mission/FPSRMissionActor.h"
#include "Run/Mission/FPSRMissionDataAsset.h"
#include "Net/UnrealNetwork.h"

AFPSRMissionActor::AFPSRMissionActor()
{
	bReplicates = true;
	SetReplicateMovement(false);
	PrimaryActorTick.bCanEverTick = true;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
}

void AFPSRMissionActor::ServerActivate(UFPSRMissionDataAsset* InData)
{
	if (!HasAuthority())
	{
		return;
	}

	MissionData = InData;
	MissionState = EFPSRMissionState::Active;
	ElapsedTime = 0.0f;
	MissionProgress = 0.0f;

	// Mark dirty for replication
	ForceNetUpdate();

	OnMissionActivated();
}

void AFPSRMissionActor::CompleteMission()
{
	if (!HasAuthority() || MissionState != EFPSRMissionState::Active)
	{
		return;
	}

	MissionState = EFPSRMissionState::Completed;
	EndMissionInternal(true);
}

void AFPSRMissionActor::FailMission()
{
	if (!HasAuthority() || MissionState != EFPSRMissionState::Active)
	{
		return;
	}

	MissionState = EFPSRMissionState::Failed;
	EndMissionInternal(false);
}

void AFPSRMissionActor::SetMissionProgress(float NewProgress)
{
	if (!HasAuthority())
	{
		return;
	}

	MissionProgress = FMath::Clamp(NewProgress, 0.0f, 1.0f);
}

void AFPSRMissionActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!HasAuthority() || MissionState != EFPSRMissionState::Active)
	{
		return;
	}

	ElapsedTime += DeltaSeconds;
	OnMissionTickServer(DeltaSeconds);

	// Check time limit
	if (MissionData && MissionData->TimeLimit > 0.0f && ElapsedTime >= MissionData->TimeLimit)
	{
		FailMission();
	}
}

void AFPSRMissionActor::OnRep_MissionState()
{
	// Currently empty; can be expanded for client-side state change reactions
}

void AFPSRMissionActor::EndMissionInternal(bool bSuccess)
{
	OnMissionEnded(bSuccess);
	OnMissionEndedNative.Broadcast(this, bSuccess);
}

void AFPSRMissionActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AFPSRMissionActor, MissionState);
	DOREPLIFETIME(AFPSRMissionActor, MissionProgress);
	DOREPLIFETIME(AFPSRMissionActor, MissionData);
}
