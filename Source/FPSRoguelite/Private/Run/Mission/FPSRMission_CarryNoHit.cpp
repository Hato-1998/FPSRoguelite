// Copyright Epic Games, Inc. All Rights Reserved.

#include "Run/Mission/FPSRMission_CarryNoHit.h"
#include "Run/Mission/FPSRMissionOrb.h"
#include "Core/FPSRPlayerState.h"
#include "AbilitySystem/FPSRAbilitySystemComponent.h"
#include "AbilitySystem/Attributes/FPSRHealthSet.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"

AFPSRMission_CarryNoHit::AFPSRMission_CarryNoHit()
{
	PrimaryActorTick.TickInterval = 0.1f;
}

void AFPSRMission_CarryNoHit::OnMissionActivated()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	CarrySeconds = 0.0f;
	Carrier = nullptr;
	LastCarrierHealth = -1.0f;
	OrbHomeLocation = GetActorLocation();

	UClass* SpawnClass = OrbClass ? OrbClass.Get() : AFPSRMissionOrb::StaticClass();
	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	Orb = World->SpawnActor<AFPSRMissionOrb>(SpawnClass, OrbHomeLocation, FRotator::ZeroRotator, Params);
	if (Orb)
	{
		Orb->OnCollectedNative.AddUObject(this, &AFPSRMission_CarryNoHit::HandleOrbCollected);
	}
	SetMissionProgress(0.0f);
}

void AFPSRMission_CarryNoHit::HandleOrbCollected(AFPSRMissionOrb* InOrb, APawn* Collector)
{
	// First player to reach the orb becomes the carrier; start (or restart) the no-hit timer from their health.
	Carrier = Collector;
	CarrySeconds = 0.0f;
	LastCarrierHealth = GetPawnHealth(Collector);
}

void AFPSRMission_CarryNoHit::OnMissionTickServer(float DeltaSeconds)
{
	if (!Orb)
	{
		return;
	}

	APawn* CarrierPawn = Carrier.Get();
	if (!CarrierPawn)
	{
		// No carrier: park the orb at home and let it be re-collected.
		if (Orb->IsCollected())
		{
			Orb->SetActorLocation(OrbHomeLocation);
			Orb->SetCollected(false);
		}
		CarrySeconds = 0.0f;
		SetMissionProgress(0.0f);
		return;
	}

	// Keep the orb attached above the carrier.
	Orb->SetActorLocation(CarrierPawn->GetActorLocation() + FVector(0.0f, 0.0f, CarryHeight));

	// Any drop in the carrier's health counts as "being hit" and resets the streak. Health regen only raises
	// the baseline, never resets.
	const float CurrentHealth = GetPawnHealth(CarrierPawn);
	if (CurrentHealth >= 0.0f && LastCarrierHealth >= 0.0f && CurrentHealth < LastCarrierHealth - 0.01f)
	{
		CarrySeconds = 0.0f;
	}
	else
	{
		CarrySeconds += DeltaSeconds;
	}
	LastCarrierHealth = CurrentHealth;

	SetMissionProgress(FMath::Clamp(CarrySeconds / FMath::Max(RequiredCarrySeconds, 0.01f), 0.0f, 1.0f));
	if (CarrySeconds >= RequiredCarrySeconds)
	{
		CompleteMission();
	}
}

float AFPSRMission_CarryNoHit::GetPawnHealth(APawn* Pawn) const
{
	if (!Pawn)
	{
		return -1.0f;
	}
	if (AFPSRPlayerState* PS = Pawn->GetPlayerState<AFPSRPlayerState>())
	{
		if (UFPSRAbilitySystemComponent* ASC = PS->GetFPSRAbilitySystemComponent())
		{
			return ASC->GetNumericAttribute(UFPSRHealthSet::GetHealthAttribute());
		}
	}
	return -1.0f;
}

void AFPSRMission_CarryNoHit::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Clean up the spawned orb on ANY teardown (not just mission-ended), so a direct mission destruction
	// (e.g. DestroyActiveMission on boss entry) does not orphan it. Server owns the orb.
	if (HasAuthority() && Orb)
	{
		Orb->OnCollectedNative.RemoveAll(this);
		Orb->Destroy();
		Orb = nullptr;
	}

	Super::EndPlay(EndPlayReason);
}
