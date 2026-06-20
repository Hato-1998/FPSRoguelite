// Copyright Epic Games, Inc. All Rights Reserved.

#include "AbilitySystem/Attributes/FPSRCombatSet.h"
#include "Net/UnrealNetwork.h"
#include "Hero/FPSRCharacter.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/Pawn.h"

UFPSRCombatSet::UFPSRCombatSet()
{
	InitGlobalCritChance(0.05f);
	InitGlobalCritMultiplier(2.0f);
	InitGlobalDamageMultiplier(1.0f);
	InitLuck(0.0f);
	InitPickupRadius(1.0f);
	InitXPGain(1.0f);
	InitMoveSpeedMultiplier(1.0f);
}

void UFPSRCombatSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION_NOTIFY(UFPSRCombatSet, GlobalCritChance, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UFPSRCombatSet, GlobalCritMultiplier, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UFPSRCombatSet, GlobalDamageMultiplier, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UFPSRCombatSet, Luck, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UFPSRCombatSet, PickupRadius, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UFPSRCombatSet, XPGain, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UFPSRCombatSet, MoveSpeedMultiplier, COND_None, REPNOTIFY_Always);
}

void UFPSRCombatSet::OnRep_GlobalCritChance(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UFPSRCombatSet, GlobalCritChance, OldValue);
}

void UFPSRCombatSet::OnRep_GlobalCritMultiplier(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UFPSRCombatSet, GlobalCritMultiplier, OldValue);
}

void UFPSRCombatSet::OnRep_GlobalDamageMultiplier(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UFPSRCombatSet, GlobalDamageMultiplier, OldValue);
}

void UFPSRCombatSet::OnRep_Luck(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UFPSRCombatSet, Luck, OldValue);
}

void UFPSRCombatSet::OnRep_PickupRadius(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UFPSRCombatSet, PickupRadius, OldValue);
}

void UFPSRCombatSet::OnRep_XPGain(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UFPSRCombatSet, XPGain, OldValue);
}

void UFPSRCombatSet::OnRep_MoveSpeedMultiplier(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UFPSRCombatSet, MoveSpeedMultiplier, OldValue);
	// Client / simulated-proxy path: in Mixed replication mode the card GE does not replicate to sim proxies,
	// so this OnRep is the only signal that reaches them (§2-3-6). Idempotent assignment — harmless if the
	// server PostAttributeChange path also ran on the owning client.
	ApplyMoveSpeedToOwner();
}

void UFPSRCombatSet::PostAttributeChange(const FGameplayAttribute& Attribute, float OldValue, float NewValue)
{
	Super::PostAttributeChange(Attribute, OldValue, NewValue);
	if (Attribute == GetMoveSpeedMultiplierAttribute())
	{
		ApplyMoveSpeedToOwner();
	}
}

void UFPSRCombatSet::ApplyMoveSpeedToOwner()
{
	// The ASC (and this attribute set) is owned by the PlayerState; the controlled pawn is the character.
	AActor* OwnerActor = GetOwningActor();
	APawn* Pawn = nullptr;
	if (APlayerState* PS = Cast<APlayerState>(OwnerActor))
	{
		Pawn = PS->GetPawn();
	}
	else
	{
		Pawn = Cast<APawn>(OwnerActor);
	}
	if (AFPSRCharacter* Character = Cast<AFPSRCharacter>(Pawn))
	{
		Character->ApplyMoveSpeedMultiplier(GetMoveSpeedMultiplier());
	}
}

#if !UE_BUILD_SHIPPING
#include "HAL/IConsoleManager.h"
#include "GameFramework/PlayerController.h"
#include "Core/FPSRPlayerState.h"
#include "AbilitySystem/FPSRAbilitySystemComponent.h"
#include "Core/FPSRLogChannels.h"

namespace
{
	// Debug aid (U18a a1 verification): drive MoveSpeedMultiplier so the attribute -> CMC path is visible in PIE.
	// Authority-only effect (SetNumericAttributeBase) — run in standalone/listen-server PIE to feel the speed change.
	FAutoConsoleCommandWithWorldAndArgs GCmd_SetMoveSpeedMult(
		TEXT("FPSR.SetMoveSpeedMult"),
		TEXT("Set the local player's MoveSpeedMultiplier base value (debug). Usage: FPSR.SetMoveSpeedMult [mult]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (!World)
			{
				return;
			}
			APlayerController* PC = World->GetFirstPlayerController();
			AFPSRPlayerState* PS = PC ? PC->GetPlayerState<AFPSRPlayerState>() : nullptr;
			UFPSRAbilitySystemComponent* ASC = PS ? PS->GetFPSRAbilitySystemComponent() : nullptr;
			if (!ASC)
			{
				UE_LOG(LogFPSR, Warning, TEXT("[Combat] SetMoveSpeedMult: player ASC not found"));
				return;
			}
			float Mult = 1.0f;
			if (Args.Num() > 0)
			{
				Mult = FCString::Atof(*Args[0]);
			}
			ASC->SetNumericAttributeBase(UFPSRCombatSet::GetMoveSpeedMultiplierAttribute(), Mult);
			UE_LOG(LogFPSR, Log, TEXT("[Combat] MoveSpeedMultiplier set to %.2f"), Mult);
		}));
}
#endif // !UE_BUILD_SHIPPING
