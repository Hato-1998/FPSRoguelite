// Copyright Epic Games, Inc. All Rights Reserved.

#include "Enemy/FPSREnemyHealthComponent.h"
#include "Core/FPSRLogChannels.h"
#include "Net/UnrealNetwork.h"
#include "Net/Core/PushModel/PushModel.h"

UFPSREnemyHealthComponent::UFPSREnemyHealthComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UFPSREnemyHealthComponent::BeginPlay()
{
	Super::BeginPlay();

	if (GetOwner() && GetOwner()->HasAuthority())
	{
		Health = MaxHealth;
		MARK_PROPERTY_DIRTY_FROM_NAME(UFPSREnemyHealthComponent, Health, this);
	}
}

void UFPSREnemyHealthComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	FDoRepLifetimeParams Params;
	Params.bIsPushBased = true;
	DOREPLIFETIME_WITH_PARAMS_FAST(UFPSREnemyHealthComponent, Health, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(UFPSREnemyHealthComponent, bDead, Params);
}

void UFPSREnemyHealthComponent::ApplyDamage(float DamageAmount, AActor* DamageInstigator)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || bDead || DamageAmount <= 0.0f)
	{
		return;
	}

	Health = FMath::Clamp(Health - DamageAmount, 0.0f, MaxHealth);
	MARK_PROPERTY_DIRTY_FROM_NAME(UFPSREnemyHealthComponent, Health, this);

	if (Health <= 0.0f)
	{
		bDead = true;
		MARK_PROPERTY_DIRTY_FROM_NAME(UFPSREnemyHealthComponent, bDead, this);
		OnDeath.Broadcast(GetOwner(), DamageInstigator);
	}
}

void UFPSREnemyHealthComponent::OnRep_Health()
{
	// Client cosmetic hook (health bar / hit flash later).
}
