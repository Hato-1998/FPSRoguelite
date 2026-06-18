// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/FPSRLobbyWidget.h"
#include "Core/FPSRLobbyPlayerController.h"
#include "Core/FPSRPlayerState.h"
#include "Core/FPSRGameFlowSettings.h"
#include "Core/FPSRSessionSubsystem.h"
#include "Weapon/FPSRLoadoutPoolDataAsset.h"
#include "CommonInputModeTypes.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "TimerManager.h"

void UFPSRLobbyWidget::NativeConstruct()
{
	Super::NativeConstruct();
	TryBindPlayerState();
}

void UFPSRLobbyWidget::NativeDestruct()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(BindRetryTimer);
	}
	if (bBoundToPlayerState)
	{
		if (const APlayerController* PC = GetOwningPlayer())
		{
			if (AFPSRPlayerState* PS = PC->GetPlayerState<AFPSRPlayerState>())
			{
				PS->OnLoadoutChanged.RemoveDynamic(this, &UFPSRLobbyWidget::HandleLoadoutChanged);
				PS->OnReadyChanged.RemoveDynamic(this, &UFPSRLobbyWidget::HandleReadyChanged);
			}
		}
		bBoundToPlayerState = false;
	}
	Super::NativeDestruct();
}

TOptional<FUIInputConfig> UFPSRLobbyWidget::GetDesiredInputConfig() const
{
	return FUIInputConfig(ECommonInputMode::Menu, EMouseCaptureMode::NoCapture, false);
}

bool UFPSRLobbyWidget::TryBindPlayerState()
{
	if (bBoundToPlayerState)
	{
		return true;
	}

	const APlayerController* PC = GetOwningPlayer();
	AFPSRPlayerState* PS = PC ? PC->GetPlayerState<AFPSRPlayerState>() : nullptr;
	if (PS)
	{
		PS->OnLoadoutChanged.AddDynamic(this, &UFPSRLobbyWidget::HandleLoadoutChanged);
		PS->OnReadyChanged.AddDynamic(this, &UFPSRLobbyWidget::HandleReadyChanged);
		bBoundToPlayerState = true;
		// Initial paint with whatever state already exists.
		OnLoadoutRefreshed();
		OnReadyRefreshed();
		return true;
	}

	// PlayerState not replicated yet on a remote client — retry shortly (weak lambda discards the bool result).
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(BindRetryTimer, FTimerDelegate::CreateWeakLambda(this, [this]() { TryBindPlayerState(); }), 0.25f, false);
	}
	return false;
}

void UFPSRLobbyWidget::HandleLoadoutChanged()
{
	OnLoadoutRefreshed();
}

void UFPSRLobbyWidget::HandleReadyChanged()
{
	OnReadyRefreshed();
}

void UFPSRLobbyWidget::SelectLoadoutWeapon(int32 PoolIndex)
{
	if (AFPSRLobbyPlayerController* PC = Cast<AFPSRLobbyPlayerController>(GetOwningPlayer()))
	{
		PC->ServerSelectLoadoutWeapon(PoolIndex);
	}
}

void UFPSRLobbyWidget::ToggleReady()
{
	AFPSRLobbyPlayerController* PC = Cast<AFPSRLobbyPlayerController>(GetOwningPlayer());
	const AFPSRPlayerState* PS = PC ? PC->GetPlayerState<AFPSRPlayerState>() : nullptr;
	if (PC && PS)
	{
		PC->ServerSetReady(!PS->IsReady());
	}
}

void UFPSRLobbyWidget::RequestShowInvite()
{
	if (const UGameInstance* GI = GetGameInstance())
	{
		if (UFPSRSessionSubsystem* Session = GI->GetSubsystem<UFPSRSessionSubsystem>())
		{
			Session->ShowInviteUI();
		}
	}
}

void UFPSRLobbyWidget::JoinLobbyByCode(const FString& Code)
{
	if (const UGameInstance* GI = GetGameInstance())
	{
		if (UFPSRSessionSubsystem* Session = GI->GetSubsystem<UFPSRSessionSubsystem>())
		{
			Session->JoinByCode(Code);
		}
	}
}

UFPSRLoadoutPoolDataAsset* UFPSRLobbyWidget::GetLoadoutPool() const
{
	const UFPSRGameFlowSettings* Settings = GetDefault<UFPSRGameFlowSettings>();
	return Settings ? Settings->LoadoutPool.LoadSynchronous() : nullptr;
}

UFPSRWeaponDataAsset* UFPSRLobbyWidget::GetSelectedWeapon() const
{
	const APlayerController* PC = GetOwningPlayer();
	const AFPSRPlayerState* PS = PC ? PC->GetPlayerState<AFPSRPlayerState>() : nullptr;
	return PS ? PS->GetSelectedWeapon() : nullptr;
}

bool UFPSRLobbyWidget::IsLocalPlayerReady() const
{
	const APlayerController* PC = GetOwningPlayer();
	const AFPSRPlayerState* PS = PC ? PC->GetPlayerState<AFPSRPlayerState>() : nullptr;
	return PS && PS->IsReady();
}

bool UFPSRLobbyWidget::IsLocalPlayerHost() const
{
	const UWorld* World = GetWorld();
	return World && World->GetNetMode() != NM_Client;
}

FString UFPSRLobbyWidget::GetLobbyCode() const
{
	if (const UGameInstance* GI = GetGameInstance())
	{
		if (UFPSRSessionSubsystem* Session = GI->GetSubsystem<UFPSRSessionSubsystem>())
		{
			return Session->GetLobbyCode();
		}
	}
	return FString();
}
