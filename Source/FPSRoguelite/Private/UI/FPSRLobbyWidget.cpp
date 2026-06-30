// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/FPSRLobbyWidget.h"
#include "Core/FPSRLobbyPlayerController.h"
#include "Core/FPSRPlayerState.h"
#include "Core/FPSRGameState.h"
#include "Core/FPSRGameFlowSettings.h"
#include "Core/FPSRSessionSubsystem.h"
#include "Core/FPSRFlowLog.h"
#include "Weapon/FPSRLoadoutPoolDataAsset.h"
#include "Weapon/FPSRWeaponDataAsset.h"
#include "Core/FPSRLogChannels.h"
#include "CommonInputModeTypes.h"
#include "CommonButtonBase.h"
#include "Components/Button.h"
#include "Blueprint/WidgetTree.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
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

UWidget* UFPSRLobbyWidget::NativeGetDesiredFocusTarget() const
{
	// CommonUI focuses GetDesiredFocusTarget() when the widget activates. We returned nothing before, so CommonUI
	// fell back to focusing the game viewport (UIActionRouter: "No focus target ... focusing the game viewport") —
	// which routes keyboard/gamepad and CommonUI input actions (Ready/Invite/Join) to the (input-less) lobby pawn
	// instead of the menu. Mouse still worked (Slate hit-testing), but key/pad input was dead. Return the first
	// interactive, enabled, visible button so focus lands on the menu. SetIsFocusable can't be used as a fallback —
	// UUserWidget focusability is fixed at construction (engine deprecation note) — so we key off real buttons.
	UWidget* FocusTarget = nullptr;
	if (WidgetTree)
	{
		WidgetTree->ForEachWidget([&FocusTarget](UWidget* Widget)
		{
			if (FocusTarget == nullptr && Widget != nullptr
				&& (Widget->IsA<UCommonButtonBase>() || Widget->IsA<UButton>())
				&& Widget->GetIsEnabled() && Widget->IsVisible())
			{
				FocusTarget = Widget;
			}
		});
	}

	if (FocusTarget == nullptr)
	{
		UE_LOG(LogFPSR, Warning, TEXT("[Lobby] NativeGetDesiredFocusTarget: no focusable button found — keyboard/gamepad input may not route to the lobby."));
		return Super::NativeGetDesiredFocusTarget();
	}
	return FocusTarget;
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
	FPSRFlowLog::Event(this, TEXT("LOBBY"), FString::Printf(TEXT("SelectLoadoutWeapon(%d)"), PoolIndex));
	if (AFPSRLobbyPlayerController* PC = Cast<AFPSRLobbyPlayerController>(GetOwningPlayer()))
	{
		PC->ServerSelectLoadoutWeapon(PoolIndex);
	}
}

void UFPSRLobbyWidget::ToggleReady()
{
	FPSRFlowLog::Event(this, TEXT("LOBBY"), TEXT("ToggleReady"));
	AFPSRLobbyPlayerController* PC = Cast<AFPSRLobbyPlayerController>(GetOwningPlayer());
	const AFPSRPlayerState* PS = PC ? PC->GetPlayerState<AFPSRPlayerState>() : nullptr;
	if (PC && PS)
	{
		PC->ServerSetReady(!PS->IsReady());
	}
}

void UFPSRLobbyWidget::RequestShowInvite()
{
	FPSRFlowLog::Event(this, TEXT("LOBBY"), TEXT("RequestShowInvite"));
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
	FPSRFlowLog::Event(this, TEXT("LOBBY"), FString::Printf(TEXT("JoinLobbyByCode('%s')"), *Code));
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

float UFPSRLobbyWidget::GetReadyCountdownRemaining() const
{
	const UWorld* World = GetWorld();
	const AFPSRGameState* GS = World ? World->GetGameState<AFPSRGameState>() : nullptr;
	return GS ? GS->GetLobbyReadyCountdownRemaining() : 0.0f;
}

TArray<FFPSRLobbyPlayerRow> UFPSRLobbyWidget::GetLobbyPlayerRows() const
{
	TArray<FFPSRLobbyPlayerRow> Rows;

	const UWorld* World = GetWorld();
	const AGameStateBase* GS = World ? World->GetGameState<AGameStateBase>() : nullptr;
	if (!GS)
	{
		return Rows;
	}

	// Identify the local player's PlayerState so the WBP can highlight its own row.
	const APlayerController* LocalPC = GetOwningPlayer();
	const APlayerState* LocalPS = LocalPC ? LocalPC->PlayerState : nullptr;

	for (const APlayerState* Base : GS->PlayerArray)
	{
		const AFPSRPlayerState* PS = Cast<AFPSRPlayerState>(Base);
		if (!PS || PS->IsOnlyASpectator())
		{
			continue; // skip spectators / non-FPSR states (server-authoritative roster only)
		}

		FFPSRLobbyPlayerRow& Row = Rows.AddDefaulted_GetRef();
		Row.PlayerName = PS->GetPlayerName();
		if (const UFPSRWeaponDataAsset* Weapon = PS->GetSelectedWeapon())
		{
			Row.WeaponName = Weapon->DisplayName;
			Row.bHasWeapon = true;
		}
		Row.bReady = PS->IsReady();
		Row.bIsLocalPlayer = (PS == LocalPS);
	}

	return Rows;
}

FText UFPSRLobbyWidget::GetLobbyPlayerListText() const
{
	const TArray<FFPSRLobbyPlayerRow> Rows = GetLobbyPlayerRows();

	TArray<FString> Lines;
	Lines.Reserve(Rows.Num());
	for (const FFPSRLobbyPlayerRow& Row : Rows)
	{
		const FString SelfMark = Row.bIsLocalPlayer ? TEXT("> ") : TEXT("");
		const FString WeaponLabel = Row.bHasWeapon ? Row.WeaponName.ToString() : TEXT("...");
		const FString ReadyMark = Row.bReady ? TEXT("   [READY]") : TEXT("");
		Lines.Add(FString::Printf(TEXT("%s%s  -  %s%s"), *SelfMark, *Row.PlayerName, *WeaponLabel, *ReadyMark));
	}

	return FText::FromString(FString::Join(Lines, TEXT("\n")));
}
