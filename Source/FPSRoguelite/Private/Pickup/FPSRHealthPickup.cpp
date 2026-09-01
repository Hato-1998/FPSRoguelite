// Copyright Epic Games, Inc. All Rights Reserved.

#include "Pickup/FPSRHealthPickup.h"
#include "Core/FPSRGameState.h"
#include "Core/FPSRPlayerState.h"
#include "Hero/FPSRCharacter.h"
#include "AbilitySystem/Attributes/FPSRHealthSet.h"
#include "AbilitySystemComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"
#include "Net/Core/PushModel/PushModel.h"

AFPSRHealthPickup::AFPSRHealthPickup()
{
	// A modest interval, not every-frame: collection isn't latency-sensitive (mirrors AFPSRMission_CarryNoHit's own
	// 0.1s tick), and a map only ever holds a handful of these.
	PrimaryActorTick.TickInterval = 0.1f;
	bReplicates = true;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision); // distance-check collection, like AFPSRXPPickup — no physical overlap needed
	// No placeholder mesh resolution in C++ (unlike AFPSRXPPickup's XPGemMesh config fallback — no
	// UFPSRPlaceholderVisualSettings entry for this pickup exists, and adding one is outside VIT1's file list).
	// Content assigns the mesh (VIT1 §11-1 user-work item 5, "힐팩 액터를 아레나에 배치").
	SetRootComponent(Mesh);
}

void AFPSRHealthPickup::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	FDoRepLifetimeParams Params;
	Params.bIsPushBased = true;
	DOREPLIFETIME_WITH_PARAMS_FAST(AFPSRHealthPickup, bAvailable, Params);
}

void AFPSRHealthPickup::BeginPlay()
{
	Super::BeginPlay();

	// Collection/respawn is server-authoritative; clients only receive the replicated bAvailable (see OnRep_Available)
	// and never need to scan for players themselves — same reasoning AFPSRXPPickup disables its own client tick.
	if (!HasAuthority())
	{
		SetActorTickEnabled(false);
	}
}

void AFPSRHealthPickup::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!HasAuthority())
	{
		return; // collection/respawn is server-authoritative; clients receive bAvailable via replication.
	}

	UWorld* World = GetWorld();
	const AFPSRGameState* GameState = World ? World->GetGameState<AFPSRGameState>() : nullptr;
	if (!GameState)
	{
		return;
	}

	// Global freeze (card selection) / stage transition: every other progressing server loop gates on this the same
	// way (W1 P2-1, mirrors AFPSRXPPickup::Tick) — no collecting, no respawn countdown while the world is stopped.
	if (GameState->IsRunPaused() || GameState->IsStageTransitionActive())
	{
		return;
	}

	if (!bAvailable)
	{
		// Combat-clock respawn (§5-6): RespawnSeconds of ACTUAL run time, not wall-clock — a card-selection freeze
		// held open for 30s must not secretly cook down the respawn timer for everyone.
		if (RespawnSeconds > 0.0f && GameState->GetCombatClockSeconds() >= RespawnAtCombatTime)
		{
			SetAvailable(true);
		}
		return;
	}

	const FVector PickupLocation = GetActorLocation();
	const float CollectRadiusSq = CollectRadius * CollectRadius;

	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* PC = It->Get();
		APawn* PlayerPawn = PC ? PC->GetPawn() : nullptr;
		AFPSRCharacter* Character = Cast<AFPSRCharacter>(PlayerPawn);
		if (!Character)
		{
			continue;
		}

		// Non-alive players (DBNO downed or Dead) don't collect (mirrors AFPSRXPPickup's same gate).
		const AFPSRPlayerState* PS = PC->GetPlayerState<AFPSRPlayerState>();
		if (!PS || !PS->IsAlive())
		{
			continue;
		}

		if (FVector::DistSquared(Character->GetActorLocation(), PickupLocation) > CollectRadiusSq)
		{
			continue;
		}

		const UAbilitySystemComponent* ASC = Character->GetAbilitySystemComponent();
		const UFPSRHealthSet* HealthSet = ASC ? ASC->GetSet<UFPSRHealthSet>() : nullptr;
		if (!HealthSet)
		{
			continue;
		}

		// A player already full can't consume this (4-player co-op: don't let a topped-up teammate waste it walking
		// through on the way to something else).
		if (bRequireMissingHealth && HealthSet->GetHealth() >= HealthSet->GetMaxHealth())
		{
			continue;
		}

		const float HealAmount = HealFlat + HealthSet->GetMaxHealth() * HealMaxHealthFraction;
		if (HealAmount <= 0.0f)
		{
			continue; // authored to heal nothing on this axis combination — never consume for no effect
		}

		// Collector ONLY, not the whole party (VIT1 §5-8) — "the hurt one goes and gets it" is the co-op decision
		// this pickup is meant to create; healing everyone would make it a resource nobody has to think about.
		Character->ApplyHealing(HealAmount, this);

		SetAvailable(false);
		if (RespawnSeconds > 0.0f)
		{
			RespawnAtCombatTime = GameState->GetCombatClockSeconds() + RespawnSeconds;
		}
		break; // one collector per pass — a second player arriving the same tick waits for the next available pickup
	}
}

void AFPSRHealthPickup::SetAvailable(bool bInAvailable)
{
	if (bAvailable == bInAvailable)
	{
		return;
	}
	bAvailable = bInAvailable;
	MARK_PROPERTY_DIRTY_FROM_NAME(AFPSRHealthPickup, bAvailable, this);

	// The listen-server host gets no OnRep — apply the visual directly (same pattern as this project's other
	// replicated-flag setters, e.g. AFPSRGameState::SetActiveBoss).
	if (Mesh)
	{
		Mesh->SetVisibility(bAvailable, true);
	}
}

void AFPSRHealthPickup::OnRep_Available()
{
	if (Mesh)
	{
		Mesh->SetVisibility(bAvailable, true);
	}
}
