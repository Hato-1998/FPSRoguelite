// Copyright Epic Games, Inc. All Rights Reserved.

#include "Weapon/FPSRRecoilComponent.h"
#include "Core/FPSRGameState.h"
#include "Core/FPSRPlayerState.h"

#include "GameFramework/Pawn.h"
#include "Engine/World.h"

bool UFPSRRecoilComponent::IsRecoilSuppressed() const
{
	// Global run-freeze (card selection): no camera drift while the world is paused for a pick (Game.MD §2-2).
	if (const UWorld* World = GetWorld())
	{
		if (const AFPSRGameState* GS = World->GetGameState<AFPSRGameState>())
		{
			if (GS->IsRunPaused())
			{
				return true;
			}
		}
	}
	// Not alive (DBNO downed / dead): recoil + recovery are suppressed, mirroring the fire input/GA alive-gate (U9).
	if (const APawn* OwnerPawn = Cast<APawn>(GetOwner()))
	{
		if (const AFPSRPlayerState* PS = OwnerPawn->GetPlayerState<AFPSRPlayerState>())
		{
			if (!PS->IsAlive())
			{
				return true;
			}
		}
	}
	return false;
}

bool UFPSRRecoilComponent::ProcessDeltaRecoilRotation(FRotator& DeltaRecoilRotation)
{
	// false = the plugin skips applying this uplift delta this frame (base default is true).
	return !IsRecoilSuppressed();
}

bool UFPSRRecoilComponent::ProcessDeltaRecoveryRotation(FRotator& DeltaRecoveryRotation)
{
	return !IsRecoilSuppressed();
}
