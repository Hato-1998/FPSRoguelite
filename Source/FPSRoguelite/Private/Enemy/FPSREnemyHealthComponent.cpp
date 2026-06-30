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
		MARK_PROPERTY_DIRTY_FROM_NAME(UFPSREnemyHealthComponent, MaxHealth, this);
	}
}

void UFPSREnemyHealthComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	FDoRepLifetimeParams Params;
	Params.bIsPushBased = true;
	DOREPLIFETIME_WITH_PARAMS_FAST(UFPSREnemyHealthComponent, Health, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(UFPSREnemyHealthComponent, MaxHealth, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(UFPSREnemyHealthComponent, bDead, Params);
}

void UFPSREnemyHealthComponent::ApplyDamage(float DamageAmount, AActor* DamageInstigator, FGameplayTag DamageType)
{
	(void)DamageType; // U18a seam (D3 elemental)
	if (!GetOwner() || !GetOwner()->HasAuthority() || bDead || DamageAmount <= 0.0f)
	{
		return;
	}

	Health = FMath::Clamp(Health - DamageAmount, 0.0f, MaxHealth);
	MARK_PROPERTY_DIRTY_FROM_NAME(UFPSREnemyHealthComponent, Health, this);

	// Server-side health-change notification (before death) — drives cosmetic damage stages (e.g. door crack/break
	// thresholds). Fired on the lethal hit too (NewHealth == 0), so the final stage runs ahead of OnDeath.
	OnHealthChanged.Broadcast(Health, MaxHealth);

	if (Health <= 0.0f)
	{
		bDead = true;
		MARK_PROPERTY_DIRTY_FROM_NAME(UFPSREnemyHealthComponent, bDead, this);
		OnDeath.Broadcast(GetOwner(), DamageInstigator);
	}
}

void UFPSREnemyHealthComponent::ResetForReuse()
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	Health = MaxHealth;
	MARK_PROPERTY_DIRTY_FROM_NAME(UFPSREnemyHealthComponent, Health, this);

	bDead = false;
	MARK_PROPERTY_DIRTY_FROM_NAME(UFPSREnemyHealthComponent, bDead, this);

	// Repaint the bound health bar to full on the LISTEN-SERVER HOST (A1). The host has no OnRep, so without this it
	// would keep the last ~0% paint from the prior life until the next hit. Clients already get this for free: the
	// reused actor's Health replicates 0 -> MaxHealth and OnRep_Health fires the same broadcast, so this is purely
	// host/client symmetry (the bar hides at full health; full-health delta is the same one clients already handle).
	OnHealthChanged.Broadcast(Health, MaxHealth);
}

void UFPSREnemyHealthComponent::InitializeMaxHealth(float NewMaxHealth)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || NewMaxHealth <= 0.0f)
	{
		return;
	}

	// MaxHealth replicates (B12) so a runtime-set value (boss/door) reaches clients for a correct health-bar percent.
	MaxHealth = NewMaxHealth;
	Health = NewMaxHealth;
	MARK_PROPERTY_DIRTY_FROM_NAME(UFPSREnemyHealthComponent, Health, this);
	MARK_PROPERTY_DIRTY_FROM_NAME(UFPSREnemyHealthComponent, MaxHealth, this);

	bDead = false;
	MARK_PROPERTY_DIRTY_FROM_NAME(UFPSREnemyHealthComponent, bDead, this);
}

void UFPSREnemyHealthComponent::OnRep_Health()
{
	// Client-side mirror of the server's OnHealthChanged broadcast (B12). Fires when Health OR MaxHealth replicates
	// (both share this RepNotify), so a client health bar / hit flash bound to OnHealthChanged repaints with the
	// correct NewHealth/MaxHealth percent. The server fires the same delegate from the authoritative ApplyDamage.
	OnHealthChanged.Broadcast(Health, MaxHealth);
}
