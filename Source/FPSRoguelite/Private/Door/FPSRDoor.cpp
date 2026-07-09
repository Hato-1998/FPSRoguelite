// Copyright Epic Games, Inc. All Rights Reserved.

#include "Door/FPSRDoor.h"
#include "Enemy/FPSREnemyHealthComponent.h"
#include "Enemy/FPSRFlowFieldSubsystem.h"
#include "Map/FPSRMapStreamSubsystem.h"
#include "FPSRCollisionChannels.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "Net/Core/PushModel/PushModel.h"

AFPSRDoor::AFPSRDoor()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true; // bBroken replicates so late joiners / clients see the door open

	// Neutral scene root so the leaves and frame are SIBLINGS — hiding the leaves on break (propagated to their own
	// sub-meshes) never hides the frame.
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	// Breakable leaves — the weapon target + barrier. ECC_FPSRPlayerPawn (see header): gathered by every weapon
	// object-query (-> auto damage via HealthComponent), blocks players + enemies, dash-proof. QueryOnly is enough
	// (movement sweeps + weapon queries are all query-based; no physics simulation needed).
	DoorMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DoorMesh"));
	DoorMesh->SetupAttachment(Root);
	DoorMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	DoorMesh->SetCollisionObjectType(ECC_FPSRPlayerPawn);
	DoorMesh->SetCollisionResponseToAllChannels(ECR_Block);
	DoorMesh->SetGenerateOverlapEvents(false);
	// No mesh asset assigned here — Game.MD §2 forbids hardcoding asset paths; the designer assigns SM in BP.

	// Frame (문틀) — inert wall. WorldStatic object type is NEVER gathered by the weapon object-queries
	// (ECC_Pawn / ECC_FPSRPlayerPawn / weakpoint), so shots stop on it as cover with no damage; it still blocks
	// movement and the weapon Visibility wall-trace like normal static geometry. Empty by default (frameless door).
	FrameMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FrameMesh"));
	FrameMesh->SetupAttachment(Root);
	FrameMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	FrameMesh->SetCollisionObjectType(ECC_WorldStatic);
	FrameMesh->SetCollisionResponseToAllChannels(ECR_Block);
	FrameMesh->SetGenerateOverlapEvents(false);

	HealthComponent = CreateDefaultSubobject<UFPSREnemyHealthComponent>(TEXT("HealthComponent"));
	HealthComponent->SetCountsAsKill(false); // destructible, but NOT an enemy (no kill credit / on-kill / lifesteal)
}

void AFPSRDoor::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority())
	{
		if (HealthComponent)
		{
			// Size HP to the designer's durability (overrides the component default), then listen for health changes
			// (damage stages) and death (break).
			HealthComponent->InitializeMaxHealth(Durability);
			HealthComponent->OnHealthChanged.AddDynamic(this, &AFPSRDoor::HandleHealthChanged);
			HealthComponent->OnDeath.AddDynamic(this, &AFPSRDoor::HandleBroken);
		}
	}
	else if (bBroken)
	{
		// Late-joining client: the door was already broken when it became net-relevant — apply the open state now
		// (OnRep won't fire for the initial replicated value).
		ApplyBrokenState();
	}
}

void AFPSRDoor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	FDoRepLifetimeParams Params;
	Params.bIsPushBased = true;
	DOREPLIFETIME_WITH_PARAMS_FAST(AFPSRDoor, bBroken, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(AFPSRDoor, DamageStage, Params);
}

void AFPSRDoor::HandleHealthChanged(float NewHealth, float MaxHealth)
{
	// Server-only (OnHealthChanged broadcasts on authority): advance the damage stage and fire the BP presentation
	// for any thresholds the hit just crossed. Clients fire the same stages via OnRep_DamageStage.
	if (!HasAuthority() || bBroken || MaxHealth <= 0.0f)
	{
		return;
	}

	const float Pct = NewHealth / MaxHealth;

	// Count how many thresholds the current percent is at-or-below (thresholds are descending, so this is the new
	// stage count). Order-independent for the count; index semantics assume descending (see header).
	int32 NewStage = 0;
	for (const float Threshold : DamageStageThresholds)
	{
		if (Pct <= Threshold)
		{
			++NewStage;
		}
	}

	if (NewStage > DamageStage)
	{
		FireDamageStages(DamageStage, NewStage, Pct); // server-local presentation
		DamageStage = static_cast<uint8>(NewStage);
		MARK_PROPERTY_DIRTY_FROM_NAME(AFPSRDoor, DamageStage, this);
	}
}

void AFPSRDoor::OnRep_DamageStage(uint8 OldStage)
{
	if (DamageStage > OldStage)
	{
		FireDamageStages(OldStage, DamageStage, -1.0f); // client: no exact health, report per-stage threshold
	}
}

void AFPSRDoor::FireDamageStages(int32 FromStage, int32 ToStage, float CurrentPct)
{
	for (int32 Stage = FromStage; Stage < ToStage; ++Stage)
	{
		const float Threshold = DamageStageThresholds.IsValidIndex(Stage) ? DamageStageThresholds[Stage] : 0.0f;
		const float ReportPct = (CurrentPct >= 0.0f) ? CurrentPct : Threshold;
		OnDoorDamageStage(Stage, ReportPct, Threshold);
	}
}

void AFPSRDoor::HandleBroken(AActor* DeadActor, AActor* Killer)
{
	if (!HasAuthority() || bBroken)
	{
		return;
	}

	bBroken = true;
	MARK_PROPERTY_DIRTY_FROM_NAME(AFPSRDoor, bBroken, this);

	// U (P-B): open the swarm flow field's seam this door was blocking so enemies cross + the origin-aware combat gate
	// allows across immediately. BEFORE ApplyBrokenState so the leaf collision (door bounds) is still valid; no unified
	// field (single-map) or off-authority makes the subsystem a no-op.
	if (UWorld* World = GetWorld())
	{
		if (UFPSRFlowFieldSubsystem* Flow = World->GetSubsystem<UFPSRFlowFieldSubsystem>())
		{
			Flow->NotifyDoorBroken(this);
		}
	}

	ApplyBrokenState(); // server: open the passage (collision off) + hide the leaves
	OnDoorBroken();     // BP presentation (server)

	// Multimap Tier 0: a giant boundary door streams in the adjacent map when broken. The MapStreamSubsystem bakes the
	// field / re-caches spawn points / drops the boundary blocker once the sublevel's collision is verified ready (S3).
	// A plain (non-streaming) room gate leaves TargetMapId unset and this is a no-op. Client visibility is engine-replicated.
	if (TargetMapId.IsValid() && !TargetLevelName.IsNone())
	{
		if (UWorld* World = GetWorld())
		{
			if (UFPSRMapStreamSubsystem* Stream = World->GetSubsystem<UFPSRMapStreamSubsystem>())
			{
				Stream->RequestStreamIn(TargetMapId, TargetLevelName);
			}
		}
	}
}

void AFPSRDoor::OnRep_Broken()
{
	if (bBroken)
	{
		ApplyBrokenState();
		OnDoorBroken(); // BP presentation (clients)
	}
}

void AFPSRDoor::ApplyBrokenState()
{
	// Disable collision + hide the LEAVES only (propagate to their sub-meshes); the frame is a sibling and stays
	// solid/visible. Not SetActorHiddenInGame/SetActorEnableCollision — those would also take down the frame.
	if (DoorMesh)
	{
		DoorMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		DoorMesh->SetVisibility(false, /*bPropagateToChildren*/ true);
	}
}
