// Copyright Epic Games, Inc. All Rights Reserved.

#include "Hero/FPSRReviveComponent.h"
#include "Hero/FPSRCharacter.h"
#include "Core/FPSRPlayerState.h"
#include "Core/FPSRGameState.h"
#include "AbilitySystem/Attributes/FPSRHealthSet.h"
#include "AbilitySystemComponent.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "Net/Core/PushModel/PushModel.h"

UFPSRReviveComponent::UFPSRReviveComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);
}

void UFPSRReviveComponent::BeginPlay()
{
	Super::BeginPlay();

	// Only the server runs the revive logic (proximity scan + gauge). Disable the tick everywhere else; ReviveProgress
	// replicates to clients for the HUD gauge regardless.
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		PrimaryComponentTick.SetTickFunctionEnable(false);
	}
}

void UFPSRReviveComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	FDoRepLifetimeParams Params;
	Params.bIsPushBased = true;
	DOREPLIFETIME_WITH_PARAMS_FAST(UFPSRReviveComponent, ReviveProgress, Params);
}

void UFPSRReviveComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (!OwnerPawn || !OwnerPawn->HasAuthority())
	{
		return; // server-authoritative
	}

	AFPSRPlayerState* OwnerPS = OwnerPawn->GetPlayerState<AFPSRPlayerState>();
	if (!OwnerPS || !OwnerPS->IsDBNO())
	{
		// Not downed: keep the gauge cleared (handles the just-revived / never-downed case).
		if (ReviveProgress != 0.0f)
		{
			SetReviveProgress(0.0f);
		}
		return;
	}

	// Keep the downed player's camera pointed at a living ally (re-picks if that ally moves out of the party / dies).
	// Maintained even during the card-selection freeze so the spectate view stays valid.
	UpdateDownedSpectate();

	// No revive while the run is globally frozen for card selection (freeze-gate symmetry, §2-2).
	const UWorld* World = GetWorld();
	AFPSRGameState* GS = World ? World->GetGameState<AFPSRGameState>() : nullptr;
	if (GS && GS->IsRunPaused())
	{
		return;
	}

	// Is any ALIVE ally within revive radius (2D)?
	bool bAllyNear = false;
	if (GS)
	{
		const FVector MyLoc = OwnerPawn->GetActorLocation();
		for (APlayerState* PS : GS->PlayerArray)
		{
			AFPSRPlayerState* AllyPS = Cast<AFPSRPlayerState>(PS);
			if (!AllyPS || AllyPS == OwnerPS || !AllyPS->IsAlive())
			{
				continue;
			}
			const APawn* AllyPawn = AllyPS->GetPawn();
			if (!AllyPawn)
			{
				continue;
			}
			FVector ToAlly = AllyPawn->GetActorLocation() - MyLoc;
			ToAlly.Z = 0.0f;
			if (ToAlly.SizeSquared() <= FMath::Square(ReviveRadius))
			{
				bAllyNear = true;
				break;
			}
		}
	}

	const float Step = DeltaTime / FMath::Max(0.1f, ReviveSeconds);
	if (bAllyNear)
	{
		SetReviveProgress(FMath::Min(1.0f, ReviveProgress + Step));
		if (ReviveProgress >= 1.0f)
		{
			PerformRevive();
		}
	}
	else if (ReviveDecayMultiplier > 0.0f && ReviveProgress > 0.0f)
	{
		SetReviveProgress(FMath::Max(0.0f, ReviveProgress - Step * ReviveDecayMultiplier));
	}
}

void UFPSRReviveComponent::SetReviveProgress(float NewProgress)
{
	if (ReviveProgress == NewProgress)
	{
		return;
	}
	ReviveProgress = NewProgress;
	MARK_PROPERTY_DIRTY_FROM_NAME(UFPSRReviveComponent, ReviveProgress, this);
	OnReviveProgressChanged.Broadcast(ReviveProgress); // host has no OnRep — refresh the host gauge directly
}

void UFPSRReviveComponent::PerformRevive()
{
	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	AFPSRPlayerState* OwnerPS = OwnerPawn ? OwnerPawn->GetPlayerState<AFPSRPlayerState>() : nullptr;
	if (!OwnerPS)
	{
		return;
	}

	OwnerPS->SetLifeState(EFPSRLifeState::Alive);

	// Restore health to a fraction of max (server-authoritative; Health replicates -> client HUD updates, and the
	// PostAttributeChange re-arms UFPSRHealthSet::OnOutOfHealth for the next down). A scripted set is used rather than
	// an instant GE so revive needs no content asset; swap to a Revive GE if designers want curve/cue hooks later.
	if (UAbilitySystemComponent* ASC = OwnerPS->GetAbilitySystemComponent())
	{
		const UFPSRHealthSet* HealthSet = OwnerPS->GetHealthSet();
		const float MaxHealth = HealthSet ? HealthSet->GetMaxHealth() : 100.0f;
		ASC->SetNumericAttributeBase(UFPSRHealthSet::GetHealthAttribute(), MaxHealth * ReviveHealthFraction);
	}

	// Restore normal locomotion on the server; the owning client mirrors it via AFPSRPlayerState::OnRep_LifeState.
	if (AFPSRCharacter* OwnerChar = Cast<AFPSRCharacter>(OwnerPawn))
	{
		OwnerChar->ApplyDownedLocomotion(false);
	}

	// Camera back to the revived player's own pawn — it never moved (DBNO is stationary), so the player stands up
	// exactly where it fell. (Was spectating a living ally while downed; §2-13.)
	RestoreOwnView();

	SetReviveProgress(0.0f);
}

void UFPSRReviveComponent::UpdateDownedSpectate()
{
	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (!OwnerPawn || !OwnerPawn->HasAuthority())
	{
		return; // server-authoritative; the view-target call replicates to the owning client (ClientSetViewTarget)
	}
	const AFPSRPlayerState* OwnerPS = OwnerPawn->GetPlayerState<AFPSRPlayerState>();
	if (!OwnerPS || !OwnerPS->IsDBNO())
	{
		return; // only spectate while downed
	}
	APlayerController* PC = Cast<APlayerController>(OwnerPawn->GetController());
	if (!PC)
	{
		return;
	}

	// Pick the nearest ALIVE ally pawn (any range) to spectate.
	APawn* Best = nullptr;
	float BestDistSq = TNumericLimits<float>::Max();
	const FVector MyLoc = OwnerPawn->GetActorLocation();
	const UWorld* World = GetWorld();
	if (const AGameStateBase* GS = World ? World->GetGameState() : nullptr)
	{
		for (APlayerState* PS : GS->PlayerArray)
		{
			const AFPSRPlayerState* AllyPS = Cast<AFPSRPlayerState>(PS);
			if (!AllyPS || AllyPS == OwnerPS || !AllyPS->IsAlive())
			{
				continue;
			}
			APawn* AllyPawn = AllyPS->GetPawn();
			if (!AllyPawn)
			{
				continue;
			}
			const float DistSq = FVector::DistSquared(AllyPawn->GetActorLocation(), MyLoc);
			if (DistSq < BestDistSq)
			{
				BestDistSq = DistSq;
				Best = AllyPawn;
			}
		}
	}

	// Only (re)blend when the chosen ally changes (no living ally => keep current view; a wipe ends the run).
	if (Best && Best != SpectatedPawn.Get())
	{
		PC->SetViewTargetWithBlend(Best, 0.3f);
		SpectatedPawn = Best;
	}
}

void UFPSRReviveComponent::RestoreOwnView()
{
	SpectatedPawn = nullptr;
	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (!OwnerPawn || !OwnerPawn->HasAuthority())
	{
		return;
	}
	if (APlayerController* PC = Cast<APlayerController>(OwnerPawn->GetController()))
	{
		PC->SetViewTargetWithBlend(OwnerPawn, 0.3f);
	}
}

void UFPSRReviveComponent::OnRep_ReviveProgress()
{
	OnReviveProgressChanged.Broadcast(ReviveProgress);
}
