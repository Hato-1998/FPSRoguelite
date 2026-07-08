// Copyright Epic Games, Inc. All Rights Reserved.

#include "Run/Mission/FPSRMission_CarryNoHit.h"
#include "Run/Mission/FPSRMissionOrb.h"
#include "Run/Mission/FPSRMissionTuning.h"
#include "Core/FPSRPlayerState.h"
#include "AbilitySystem/FPSRAbilitySystemComponent.h"
#include "AbilitySystem/Attributes/FPSRHealthSet.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"

AFPSRMission_CarryNoHit::AFPSRMission_CarryNoHit()
{
	PrimaryActorTick.TickInterval = 0.1f;
}

TSubclassOf<UFPSRMissionTuning> AFPSRMission_CarryNoHit::GetExpectedTuningClass() const
{
	return UFPSRMissionTuning_CarryNoHit::StaticClass();
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

	// §2-8-1: read from the DataAsset's Tuning (or the tuning subclass's CDO defaults when unset).
	// RequiredCarrySeconds/CarryHeight are read every tick, so cache them; OrbClass is only needed here at spawn.
	const UFPSRMissionTuning_CarryNoHit& T = GetTuning<UFPSRMissionTuning_CarryNoHit>();
	EffRequiredCarrySeconds = T.RequiredCarrySeconds;
	EffCarryHeight = T.CarryHeight;
	const TSubclassOf<AFPSRMissionOrb> EffOrbClass = T.OrbClass;

	UClass* SpawnClass = EffOrbClass ? EffOrbClass.Get() : AFPSRMissionOrb::StaticClass();
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
	Orb->SetActorLocation(CarrierPawn->GetActorLocation() + FVector(0.0f, 0.0f, EffCarryHeight));

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

	SetMissionProgress(FMath::Clamp(CarrySeconds / FMath::Max(EffRequiredCarrySeconds, 0.01f), 0.0f, 1.0f));
	if (CarrySeconds >= EffRequiredCarrySeconds)
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
