// Copyright Epic Games, Inc. All Rights Reserved.

#include "Core/FPSRSessionSubsystem.h"
#include "Core/FPSRGameFlowSettings.h"
#include "Core/FPSRLogChannels.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemUtils.h"
#include "OnlineSessionSettings.h"
#include "Interfaces/OnlineExternalUIInterface.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

namespace
{
	// All sessions in this game use the canonical game-session name.
	const FName GFPSRSessionName = NAME_GameSession;
}

void UFPSRSessionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Bind the Steam overlay invite-accept handler once (primary join path). Fired when the local user accepts a
	// friend's invite — we join the carried search result directly (no search needed).
	if (IOnlineSessionPtr Sessions = GetSessionInterface())
	{
		InviteAcceptedHandle = Sessions->AddOnSessionUserInviteAcceptedDelegate_Handle(
			FOnSessionUserInviteAcceptedDelegate::CreateUObject(this, &UFPSRSessionSubsystem::HandleSessionUserInviteAccepted));
	}
}

void UFPSRSessionSubsystem::Deinitialize()
{
	if (IOnlineSessionPtr Sessions = GetSessionInterface())
	{
		if (InviteAcceptedHandle.IsValid())   { Sessions->ClearOnSessionUserInviteAcceptedDelegate_Handle(InviteAcceptedHandle); }
		if (CreateSessionCompleteHandle.IsValid())  { Sessions->ClearOnCreateSessionCompleteDelegate_Handle(CreateSessionCompleteHandle); }
		if (FindSessionsCompleteHandle.IsValid())   { Sessions->ClearOnFindSessionsCompleteDelegate_Handle(FindSessionsCompleteHandle); }
		if (JoinSessionCompleteHandle.IsValid())    { Sessions->ClearOnJoinSessionCompleteDelegate_Handle(JoinSessionCompleteHandle); }
		if (DestroySessionCompleteHandle.IsValid()) { Sessions->ClearOnDestroySessionCompleteDelegate_Handle(DestroySessionCompleteHandle); }
	}

	Super::Deinitialize();
}

IOnlineSessionPtr UFPSRSessionSubsystem::GetSessionInterface() const
{
	const UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	IOnlineSubsystem* OSS = Online::GetSubsystem(World);
	return OSS ? OSS->GetSessionInterface() : nullptr;
}

void UFPSRSessionSubsystem::HostSession(int32 MaxPlayers)
{
	IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid())
	{
		UE_LOG(LogFPSR, Error, TEXT("[Session] HostSession: no session interface (Steam not initialized)."));
		OnHostComplete.Broadcast(false);
		return;
	}

	// If a stale session lingers (e.g. returned from a prior run), tear it down first so CreateSession succeeds.
	if (Sessions->GetNamedSession(GFPSRSessionName) != nullptr)
	{
		Sessions->DestroySession(GFPSRSessionName);
	}

	FOnlineSessionSettings SessionSettings;
	SessionSettings.NumPublicConnections = FMath::Max(1, MaxPlayers);
	SessionSettings.NumPrivateConnections = 0;
	SessionSettings.bIsLANMatch = false;                 // Steam only (user-confirmed — no LAN fallback).
	SessionSettings.bShouldAdvertise = true;
	SessionSettings.bAllowJoinInProgress = true;
	SessionSettings.bAllowInvites = true;
	SessionSettings.bUsesPresence = true;
	SessionSettings.bAllowJoinViaPresence = true;
	SessionSettings.bUseLobbiesIfAvailable = true;       // Steam lobbies — required for friend invites/overlay.

	CreateSessionCompleteHandle = Sessions->AddOnCreateSessionCompleteDelegate_Handle(
		FOnCreateSessionCompleteDelegate::CreateUObject(this, &UFPSRSessionSubsystem::HandleCreateSessionComplete));

	if (!Sessions->CreateSession(0, GFPSRSessionName, SessionSettings))
	{
		// Synchronous failure — the completion delegate won't fire, so clean up and report here.
		Sessions->ClearOnCreateSessionCompleteDelegate_Handle(CreateSessionCompleteHandle);
		UE_LOG(LogFPSR, Error, TEXT("[Session] CreateSession returned false."));
		OnHostComplete.Broadcast(false);
	}
}

void UFPSRSessionSubsystem::HandleCreateSessionComplete(FName SessionName, bool bWasSuccessful)
{
	if (IOnlineSessionPtr Sessions = GetSessionInterface())
	{
		Sessions->ClearOnCreateSessionCompleteDelegate_Handle(CreateSessionCompleteHandle);
	}

	UE_LOG(LogFPSR, Log, TEXT("[Session] CreateSession '%s' %s"), *SessionName.ToString(), bWasSuccessful ? TEXT("OK") : TEXT("FAILED"));
	OnHostComplete.Broadcast(bWasSuccessful);

	if (!bWasSuccessful)
	{
		return;
	}

	// Travel the listen server into the lobby hub (every run, solo or co-op, starts here — user-confirmed).
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	const UFPSRGameFlowSettings* Settings = GetDefault<UFPSRGameFlowSettings>();
	const FName LobbyPackage = Settings ? Settings->GetLevelPackageName(Settings->LobbyMap) : NAME_None;
	if (World && LobbyPackage != NAME_None)
	{
		World->ServerTravel(LobbyPackage.ToString() + TEXT("?listen"));
	}
	else
	{
		UE_LOG(LogFPSR, Error, TEXT("[Session] Hosted but LobbyMap is null/invalid — cannot travel to lobby."));
	}
}

void UFPSRSessionSubsystem::FindSessions()
{
	IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid())
	{
		OnFindComplete.Broadcast(false);
		return;
	}

	SearchSettings = MakeShared<FOnlineSessionSearch>();
	SearchSettings->MaxSearchResults = 20;
	SearchSettings->bIsLanQuery = false;

	FindSessionsCompleteHandle = Sessions->AddOnFindSessionsCompleteDelegate_Handle(
		FOnFindSessionsCompleteDelegate::CreateUObject(this, &UFPSRSessionSubsystem::HandleFindSessionsComplete));

	if (!Sessions->FindSessions(0, SearchSettings.ToSharedRef()))
	{
		Sessions->ClearOnFindSessionsCompleteDelegate_Handle(FindSessionsCompleteHandle);
		OnFindComplete.Broadcast(false);
	}
}

void UFPSRSessionSubsystem::HandleFindSessionsComplete(bool bWasSuccessful)
{
	if (IOnlineSessionPtr Sessions = GetSessionInterface())
	{
		Sessions->ClearOnFindSessionsCompleteDelegate_Handle(FindSessionsCompleteHandle);
	}

	const int32 Count = (SearchSettings.IsValid()) ? SearchSettings->SearchResults.Num() : 0;
	UE_LOG(LogFPSR, Log, TEXT("[Session] FindSessions %s — %d result(s)"), bWasSuccessful ? TEXT("OK") : TEXT("FAILED"), Count);
	OnFindComplete.Broadcast(bWasSuccessful);
}

int32 UFPSRSessionSubsystem::GetSearchResultCount() const
{
	return SearchSettings.IsValid() ? SearchSettings->SearchResults.Num() : 0;
}

void UFPSRSessionSubsystem::JoinFoundSession(int32 SearchResultIndex)
{
	if (!SearchSettings.IsValid() || !SearchSettings->SearchResults.IsValidIndex(SearchResultIndex))
	{
		UE_LOG(LogFPSR, Warning, TEXT("[Session] JoinFoundSession: invalid index %d."), SearchResultIndex);
		OnJoinComplete.Broadcast(false);
		return;
	}
	JoinSearchResult(SearchSettings->SearchResults[SearchResultIndex]);
}

void UFPSRSessionSubsystem::JoinSearchResult(const FOnlineSessionSearchResult& SearchResult)
{
	IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid() || !SearchResult.IsValid())
	{
		OnJoinComplete.Broadcast(false);
		return;
	}

	JoinSessionCompleteHandle = Sessions->AddOnJoinSessionCompleteDelegate_Handle(
		FOnJoinSessionCompleteDelegate::CreateUObject(this, &UFPSRSessionSubsystem::HandleJoinSessionComplete));

	if (!Sessions->JoinSession(0, GFPSRSessionName, SearchResult))
	{
		Sessions->ClearOnJoinSessionCompleteDelegate_Handle(JoinSessionCompleteHandle);
		OnJoinComplete.Broadcast(false);
	}
}

void UFPSRSessionSubsystem::HandleJoinSessionComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result)
{
	IOnlineSessionPtr Sessions = GetSessionInterface();
	if (Sessions.IsValid())
	{
		Sessions->ClearOnJoinSessionCompleteDelegate_Handle(JoinSessionCompleteHandle);
	}

	const bool bSuccess = (Result == EOnJoinSessionCompleteResult::Success);
	UE_LOG(LogFPSR, Log, TEXT("[Session] JoinSession '%s' — %s"), *SessionName.ToString(), LexToString(Result));
	OnJoinComplete.Broadcast(bSuccess);

	if (!bSuccess || !Sessions.IsValid())
	{
		return;
	}

	// Resolve the host address and client-travel the local player to the lobby (the host is already there).
	FString ConnectInfo;
	if (Sessions->GetResolvedConnectString(SessionName, ConnectInfo))
	{
		UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
		if (APlayerController* PC = GetGameInstance() ? GetGameInstance()->GetFirstLocalPlayerController(World) : nullptr)
		{
			PC->ClientTravel(ConnectInfo, TRAVEL_Absolute);
		}
	}
	else
	{
		UE_LOG(LogFPSR, Error, TEXT("[Session] Joined but could not resolve connect string."));
	}
}

void UFPSRSessionSubsystem::DestroySession()
{
	IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid() || Sessions->GetNamedSession(GFPSRSessionName) == nullptr)
	{
		return;
	}

	DestroySessionCompleteHandle = Sessions->AddOnDestroySessionCompleteDelegate_Handle(
		FOnDestroySessionCompleteDelegate::CreateUObject(this, &UFPSRSessionSubsystem::HandleDestroySessionComplete));
	Sessions->DestroySession(GFPSRSessionName);
}

void UFPSRSessionSubsystem::HandleDestroySessionComplete(FName SessionName, bool bWasSuccessful)
{
	if (IOnlineSessionPtr Sessions = GetSessionInterface())
	{
		Sessions->ClearOnDestroySessionCompleteDelegate_Handle(DestroySessionCompleteHandle);
	}
	UE_LOG(LogFPSR, Log, TEXT("[Session] DestroySession '%s' %s"), *SessionName.ToString(), bWasSuccessful ? TEXT("OK") : TEXT("FAILED"));
}

void UFPSRSessionSubsystem::ShowInviteUI()
{
	const UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	IOnlineSubsystem* OSS = Online::GetSubsystem(World);
	const IOnlineExternalUIPtr ExternalUI = OSS ? OSS->GetExternalUIInterface() : nullptr;
	if (ExternalUI.IsValid())
	{
		ExternalUI->ShowInviteUI(0, GFPSRSessionName);
	}
	else
	{
		UE_LOG(LogFPSR, Warning, TEXT("[Session] ShowInviteUI: external UI interface unavailable."));
	}
}

void UFPSRSessionSubsystem::HandleSessionUserInviteAccepted(const bool bWasSuccessful, const int32 ControllerId, FUniqueNetIdPtr UserId, const FOnlineSessionSearchResult& InviteResult)
{
	if (!bWasSuccessful || !InviteResult.IsValid())
	{
		UE_LOG(LogFPSR, Warning, TEXT("[Session] Invite accepted but result invalid."));
		return;
	}
	UE_LOG(LogFPSR, Log, TEXT("[Session] Invite accepted — joining host."));
	JoinSearchResult(InviteResult);
}
