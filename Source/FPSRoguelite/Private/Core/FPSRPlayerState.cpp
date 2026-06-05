// Copyright Epic Games, Inc. All Rights Reserved.

#include "Core/FPSRPlayerState.h"
#include "AbilitySystem/FPSRAbilitySystemComponent.h"
#include "AbilitySystem/Attributes/FPSRHealthSet.h"
#include "AbilitySystem/Attributes/FPSRCombatSet.h"
#include "Net/UnrealNetwork.h"
#include "Net/Core/PushModel/PushModel.h"

AFPSRPlayerState::AFPSRPlayerState()
{
	AbilitySystemComponent = CreateDefaultSubobject<UFPSRAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	AbilitySystemComponent->SetIsReplicated(true);
	AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);

	HealthSet = CreateDefaultSubobject<UFPSRHealthSet>(TEXT("HealthSet"));
	CombatSet = CreateDefaultSubobject<UFPSRCombatSet>(TEXT("CombatSet"));

	// PlayerState updates frequently so GAS state stays responsive for clients.
	SetNetUpdateFrequency(100.0f);
}

UAbilitySystemComponent* AFPSRPlayerState::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

void AFPSRPlayerState::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
	{
		ResetRerollCharges();
	}
}

void AFPSRPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	FDoRepLifetimeParams Params;
	Params.bIsPushBased = true;
	DOREPLIFETIME_WITH_PARAMS_FAST(AFPSRPlayerState, RunRerollCharges, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(AFPSRPlayerState, CardPicksPending, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(AFPSRPlayerState, MissionRewardPicksPending, Params);
}

bool AFPSRPlayerState::ConsumeRerollCharge()
{
	if (!HasAuthority())
	{
		return false;
	}

	if (RunRerollCharges > 0)
	{
		--RunRerollCharges;
		MARK_PROPERTY_DIRTY_FROM_NAME(AFPSRPlayerState, RunRerollCharges, this);
		OnRerollChargesChanged.Broadcast();
		return true;
	}

	return false;
}

void AFPSRPlayerState::ResetRerollCharges()
{
	if (!HasAuthority())
	{
		return;
	}

	RunRerollCharges = DefaultRerollCharges;
	MARK_PROPERTY_DIRTY_FROM_NAME(AFPSRPlayerState, RunRerollCharges, this);
	OnRerollChargesChanged.Broadcast();
}

void AFPSRPlayerState::SetRerollCharges(int32 NewCharges)
{
	if (!HasAuthority())
	{
		return;
	}

	RunRerollCharges = FMath::Max(NewCharges, 0);
	MARK_PROPERTY_DIRTY_FROM_NAME(AFPSRPlayerState, RunRerollCharges, this);
	OnRerollChargesChanged.Broadcast();
}

void AFPSRPlayerState::OnRep_RunRerollCharges()
{
	OnRerollChargesChanged.Broadcast();
}

void AFPSRPlayerState::AddCardPick()
{
	if (!HasAuthority())
	{
		return;
	}

	++CardPicksPending;
	MARK_PROPERTY_DIRTY_FROM_NAME(AFPSRPlayerState, CardPicksPending, this);
	OnCardPicksChanged.Broadcast();
}

bool AFPSRPlayerState::ConsumeCardPick()
{
	if (!HasAuthority() || CardPicksPending <= 0)
	{
		return false;
	}

	--CardPicksPending;
	MARK_PROPERTY_DIRTY_FROM_NAME(AFPSRPlayerState, CardPicksPending, this);
	OnCardPicksChanged.Broadcast();
	return true;
}

void AFPSRPlayerState::AddMissionRewardPick()
{
	if (!HasAuthority())
	{
		return;
	}

	++MissionRewardPicksPending;
	MARK_PROPERTY_DIRTY_FROM_NAME(AFPSRPlayerState, MissionRewardPicksPending, this);
	OnCardPicksChanged.Broadcast();
}

bool AFPSRPlayerState::ConsumeMissionRewardPick()
{
	if (!HasAuthority() || MissionRewardPicksPending <= 0)
	{
		return false;
	}

	--MissionRewardPicksPending;
	MARK_PROPERTY_DIRTY_FROM_NAME(AFPSRPlayerState, MissionRewardPicksPending, this);
	OnCardPicksChanged.Broadcast();
	return true;
}

void AFPSRPlayerState::OnRep_CardPicksPending()
{
	OnCardPicksChanged.Broadcast();
}
