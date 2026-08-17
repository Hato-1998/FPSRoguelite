// Copyright Epic Games, Inc. All Rights Reserved.

#include "Destructible/FPSRDestructible.h"
#include "Destructible/FPSRDestructibleReward.h"
#include "Enemy/FPSREnemyHealthComponent.h"

#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "Net/Core/PushModel/PushModel.h"

AFPSRDestructible::AFPSRDestructible()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true; // bBroken/DamageStage replicate so late joiners / clients see the correct broken state

	// Neutral scene root — subclasses attach their own mesh(es) under this (Game.MD §2: no hardcoded asset paths,
	// so this base never assigns a mesh itself). AFPSRDoor attaches DoorMesh/FrameMesh as siblings of each other
	// under this same root.
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	HealthComponent = CreateDefaultSubobject<UFPSREnemyHealthComponent>(TEXT("HealthComponent"));
	HealthComponent->SetCountsAsKill(false); // destructible, but NOT an enemy (no kill credit / on-kill / lifesteal)
}

void AFPSRDestructible::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority())
	{
		if (HealthComponent)
		{
			// Size HP to the designer's durability (overrides the component default), then listen for health changes
			// (damage stages) and death (break).
			HealthComponent->InitializeMaxHealth(Durability);
			HealthComponent->OnHealthChanged.AddDynamic(this, &AFPSRDestructible::HandleHealthChanged);
			HealthComponent->OnDeath.AddDynamic(this, &AFPSRDestructible::HandleBroken);
		}
	}
	else if (bBroken)
	{
		// Late-joining client: already broken when it became net-relevant — apply the open state now (OnRep won't
		// fire for the initial replicated value). Virtual, so a late-joining client sees the correct subclass state.
		ApplyBrokenState();
	}
}

void AFPSRDestructible::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	FDoRepLifetimeParams Params;
	Params.bIsPushBased = true;
	DOREPLIFETIME_WITH_PARAMS_FAST(AFPSRDestructible, bBroken, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(AFPSRDestructible, DamageStage, Params);
}

int32 AFPSRDestructible::ComputeDamageStage(TConstArrayView<float> Thresholds, float HealthPct)
{
	// Count how many thresholds the current percent is at-or-below (thresholds are descending, so this is the
	// stage count). Order-independent for the count; index semantics assume descending (see header).
	int32 NewStage = 0;
	for (const float Threshold : Thresholds)
	{
		if (HealthPct <= Threshold)
		{
			++NewStage;
		}
	}
	return NewStage;
}

void AFPSRDestructible::HandleHealthChanged(float NewHealth, float MaxHealth)
{
	// Server-only (OnHealthChanged broadcasts on authority): advance the damage stage and fire the BP presentation
	// for any thresholds the hit just crossed. Clients fire the same stages via OnRep_DamageStage.
	if (!HasAuthority() || bBroken || MaxHealth <= 0.0f)
	{
		return;
	}

	const float Pct = NewHealth / MaxHealth;
	const int32 NewStage = ComputeDamageStage(DamageStageThresholds, Pct);

	if (NewStage > DamageStage)
	{
		FireDamageStages(DamageStage, NewStage, Pct); // server-local presentation
		DamageStage = static_cast<uint8>(NewStage);
		MARK_PROPERTY_DIRTY_FROM_NAME(AFPSRDestructible, DamageStage, this);
	}
}

void AFPSRDestructible::OnRep_DamageStage(uint8 OldStage)
{
	if (DamageStage > OldStage)
	{
		FireDamageStages(OldStage, DamageStage, -1.0f); // client: no exact health, report per-stage threshold
	}
}

void AFPSRDestructible::FireDamageStages(int32 FromStage, int32 ToStage, float CurrentPct)
{
	for (int32 Stage = FromStage; Stage < ToStage; ++Stage)
	{
		const float Threshold = DamageStageThresholds.IsValidIndex(Stage) ? DamageStageThresholds[Stage] : 0.0f;
		const float ReportPct = (CurrentPct >= 0.0f) ? CurrentPct : Threshold;
		FireDamageStagePresentation(Stage, ReportPct, Threshold);
	}
}

void AFPSRDestructible::HandleBroken(AActor* DeadActor, AActor* Killer)
{
	if (!HasAuthority() || bBroken)
	{
		return;
	}

	bBroken = true;
	MARK_PROPERTY_DIRTY_FROM_NAME(AFPSRDestructible, bBroken, this);

	// Fixed order: HandleBrokenAuthority BEFORE ApplyBrokenState BEFORE FireBrokenPresentation. AFPSRDoor's
	// HandleBrokenAuthority override notifies the flow field of the broken seam using the door leaf's bounds —
	// that collision is still live at this point; ApplyBrokenState (next) is what disables it, so any
	// authority-only reaction that needs the pre-broken collision/component state MUST run first.
	HandleBrokenAuthority(Killer);
	ApplyBrokenState();
	FireBrokenPresentation();
}

void AFPSRDestructible::OnRep_Broken()
{
	if (bBroken)
	{
		ApplyBrokenState();
		FireBrokenPresentation();
	}
}

void AFPSRDestructible::HandleBrokenAuthority(AActor* Breaker)
{
	FFPSRDestructibleRewardContext Context;
	Context.Source = this;
	Context.Breaker = Breaker;
	Context.World = GetWorld();

	for (const TObjectPtr<UFPSRDestructibleReward>& Reward : Rewards)
	{
		if (!Reward)
		{
			continue; // an author left an empty Instanced slot — skip, don't crash the rest of the array
		}
		Reward->Grant(Context);
	}
}

void AFPSRDestructible::ApplyBrokenState()
{
	SetActorEnableCollision(false);
	SetActorHiddenInGame(true);
}

void AFPSRDestructible::FireBrokenPresentation()
{
	OnDestructibleBroken();
}

void AFPSRDestructible::FireDamageStagePresentation(int32 StageIndex, float HealthPct, float Threshold)
{
	OnDestructibleDamageStage(StageIndex, HealthPct, Threshold);
}
