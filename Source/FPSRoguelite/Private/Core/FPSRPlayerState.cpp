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
}

void AFPSRPlayerState::SetRerollCharges(int32 NewCharges)
{
	if (!HasAuthority())
	{
		return;
	}

	RunRerollCharges = FMath::Max(NewCharges, 0);
	MARK_PROPERTY_DIRTY_FROM_NAME(AFPSRPlayerState, RunRerollCharges, this);
}

void AFPSRPlayerState::OnRep_RunRerollCharges()
{
	// Cosmetic hook — HUD binds in P3-D.
}
