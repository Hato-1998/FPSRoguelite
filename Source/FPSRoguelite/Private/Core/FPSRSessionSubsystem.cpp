// Copyright Epic Games, Inc. All Rights Reserved.

#include "Core/FPSRSessionSubsystem.h"
#include "Core/FPSRGameFlowSettings.h"
#include "Core/FPSRLogChannels.h"
#include "Core/FPSRFlowLog.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemUtils.h"
#include "OnlineSessionSettings.h"
#include "Online/OnlineSessionNames.h"   // SEARCH_LOBBIES
#include "Interfaces/OnlineExternalUIInterface.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

namespace
{
	// All sessions in this game use the canonical game-session name.
	const FName GFPSRSessionName = NAME_GameSession;

	// Advertised session-settings key carrying the human-typeable lobby join code (U11a).
	const FName GFPSRLobbyCodeKey = FName(TEXT("FPSR_LOBBYCODE"));

	// Lobby code: fixed-length, uppercase alphanumerics (typeable, no ambiguous casing).
	const int32 GFPSRLobbyCodeLen = 6;
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
	FPSRFlowLog::Event(this, TEXT("SESSION"), FString::Printf(TEXT("HostSession requested (MaxPlayers=%d)"), MaxPlayers));

	// Hosting is a server-only act, and this is the earliest place to say so: everything below (stale-session
	// teardown, CreateSession, the ServerTravel in HandleCreateSessionComplete) is wrong on a machine that is
	// already someone else's client. Blocking here rather than at the travel keeps a client from leaving an
	// advertised orphan session behind and from overwriting CurrentLobbyCode with a code nobody can join.
	// Gate on NM_Client specifically, NOT "Standalone only": the shipping main menu is NM_Standalone but a PIE
	// listen-server window's menu is NM_ListenServer, and both are legitimate hosts.
	const UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!World || World->GetNetMode() == NM_Client)
	{
		UE_LOG(LogFPSR, Warning, TEXT("[Session] HostSession ignored — this instance is a client (NetMode=%d)."),
			World ? static_cast<int32>(World->GetNetMode()) : -1);
		FPSRFlowLog::Event(this, TEXT("SESSION"), TEXT("HostSession BLOCKED (client)"));
		OnHostComplete.Broadcast(false);   // every other failure path broadcasts; silence would hang a waiting UI
		return;
	}

	IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid())
	{
		UE_LOG(LogFPSR, Error, TEXT("[Session] HostSession: no session interface (Steam not initialized)."));
		OnHostComplete.Broadcast(false);
		return;
	}

	// A registered session we did NOT create is one we JOINED — tearing it down below would silently drop us out of
	// somebody else's game (the join path registers under the same GFPSRSessionName, see JoinSearchResult). The
	// NetMode gate above catches this once we have travelled to the host, but not in the window between
	// JoinSession succeeding and ClientTravel completing, so key the decision on ownership rather than timing.
	if (Sessions->GetNamedSession(GFPSRSessionName) != nullptr && !bLocalSessionIsHosted)
	{
		UE_LOG(LogFPSR, Warning, TEXT("[Session] HostSession refused — already joined a session we did not host. Leave it first."));
		FPSRFlowLog::Event(this, TEXT("SESSION"), TEXT("HostSession REFUSED (joined session, not ours)"));
		OnHostComplete.Broadcast(false);
		return;
	}

	PendingHostMaxPlayers = FMath::Max(1, MaxPlayers);

	// If a stale session lingers (e.g. quit to menu then pressed Play again), tear it down FIRST and defer the
	// create to the destroy-complete callback — DestroySession is async, so creating immediately would race the
	// still-registered session and most OSS backends reject it (host flow stuck).
	if (Sessions->GetNamedSession(GFPSRSessionName) != nullptr)
	{
		FPSRFlowLog::Event(this, TEXT("SESSION"), TEXT("Stale session present - destroying before host"));
		bHostAfterDestroy = true;
		DestroySessionCompleteHandle = Sessions->AddOnDestroySessionCompleteDelegate_Handle(
			FOnDestroySessionCompleteDelegate::CreateUObject(this, &UFPSRSessionSubsystem::HandleDestroySessionComplete));
		if (!Sessions->DestroySession(GFPSRSessionName))
		{
			// Synchronous failure — the completion delegate won't fire, so undo and report instead of hanging. (merge-gate P2)
			Sessions->ClearOnDestroySessionCompleteDelegate_Handle(DestroySessionCompleteHandle);
			bHostAfterDestroy = false;
			OnHostComplete.Broadcast(false);
		}
		return;
	}

	CreateSessionInternal();
}

void UFPSRSessionSubsystem::CreateSessionInternal()
{
	IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid())
	{
		OnHostComplete.Broadcast(false);
		return;
	}

	FOnlineSessionSettings SessionSettings;
	SessionSettings.NumPublicConnections = PendingHostMaxPlayers;
	SessionSettings.NumPrivateConnections = 0;
	SessionSettings.bIsLANMatch = false;                 // Steam only (user-confirmed — no LAN fallback).
	SessionSettings.bShouldAdvertise = true;
	SessionSettings.bAllowJoinInProgress = true;
	SessionSettings.bAllowInvites = true;
	SessionSettings.bUsesPresence = true;
	SessionSettings.bAllowJoinViaPresence = true;
	SessionSettings.bUseLobbiesIfAvailable = true;       // Steam lobbies — required for friend invites/overlay.

	// Generate and advertise the lobby join code so a friend can join by typing it (U11a). ViaOnlineService so it
	// rides the session's online metadata and is queryable/readable by searchers and joined clients.
	CurrentLobbyCode = GenerateLobbyCode();
	SessionSettings.Set(GFPSRLobbyCodeKey, CurrentLobbyCode, EOnlineDataAdvertisementType::ViaOnlineService);

	FPSRFlowLog::Event(this, TEXT("SESSION"), FString::Printf(TEXT("CreateSession issued (code=%s, max=%d)"), *CurrentLobbyCode, PendingHostMaxPlayers));
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
	FPSRFlowLog::Event(this, TEXT("SESSION"), FString::Printf(TEXT("CreateSession complete: %s"), bWasSuccessful ? TEXT("OK") : TEXT("FAILED")));
	OnHostComplete.Broadcast(bWasSuccessful);

	if (!bWasSuccessful)
	{
		return;
	}

	// This registered session is OURS — a later HostSession may tear it down as stale, and a later Join must not
	// mistake it for someone else's (see the ownership gate in HostSession).
	bLocalSessionIsHosted = true;

	// Travel the listen server into the lobby hub (every run, solo or co-op, starts here — user-confirmed).
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	const UFPSRGameFlowSettings* Settings = GetDefault<UFPSRGameFlowSettings>();
	const FName LobbyPackage = Settings ? Settings->GetLevelPackageName(Settings->LobbyMap) : NAME_None;
	// Defence in depth: HostSession already refuses on a client, so reaching a client here would mean a new caller
	// bypassed it. ServerTravel is the call that actually corrupts a client (World.cpp sets NextURL with no
	// authority check, and the client then tries to become a second listen server in-process), so re-check.
	if (World && World->GetNetMode() == NM_Client)
	{
		UE_LOG(LogFPSR, Error, TEXT("[Session] Hosted on a client — refusing ServerTravel (would hijack this client's connection)."));
	}
	else if (World && LobbyPackage != NAME_None)
	{
		FPSRFlowLog::Event(this, TEXT("SESSION"), FString::Printf(TEXT("ServerTravel -> lobby (%s)"), *LobbyPackage.ToString()));
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

	FPSRFlowLog::Event(this, TEXT("SESSION"), TEXT("FindSessions issued"));
	SearchSettings = MakeShared<FOnlineSessionSearch>();
	SearchSettings->MaxSearchResults = 20;
	SearchSettings->bIsLanQuery = false;
	// Our sessions are Steam presence LOBBIES (bUseLobbiesIfAvailable). Without SEARCH_LOBBIES, FindSessions queries
	// the dedicated-server browser (which our lobbies are NOT in) and returns 0 — query the lobby list instead.
	SearchSettings->QuerySettings.Set(SEARCH_LOBBIES, true, EOnlineComparisonOp::Equals);

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

	// Code-join lookup: the query filtered by code, but app id 480 is shared and some backends apply filters loosely,
	// so re-verify each result's advertised code client-side (exact) and join the first true match.
	if (bSearchingByCode)
	{
		bSearchingByCode = false;
		UE_LOG(LogFPSR, Log, TEXT("[Session] JoinByCode search %s — %d candidate(s) for '%s'"), bWasSuccessful ? TEXT("OK") : TEXT("FAILED"), Count, *PendingJoinCode);
		FPSRFlowLog::Event(this, TEXT("JOIN"), FString::Printf(TEXT("Code search complete: %s, %d candidate(s) for '%s'"), bWasSuccessful ? TEXT("OK") : TEXT("FAILED"), Count, *PendingJoinCode));
		if (bWasSuccessful && SearchSettings.IsValid())
		{
			for (const FOnlineSessionSearchResult& Result : SearchSettings->SearchResults)
			{
				FString Code;
				if (Result.Session.SessionSettings.Get(GFPSRLobbyCodeKey, Code) && Code.Equals(PendingJoinCode, ESearchCase::IgnoreCase))
				{
					OnJoinByCodeComplete.Broadcast(true);   // match found — the join now flows through OnJoinComplete.
					JoinSearchResult(Result);
					return;
				}
			}
		}
		UE_LOG(LogFPSR, Warning, TEXT("[Session] JoinByCode: no session matched code '%s'."), *PendingJoinCode);
		FPSRFlowLog::Event(this, TEXT("JOIN"), FString::Printf(TEXT("JoinByCode FAILED: no session matched code '%s'"), *PendingJoinCode));
		OnJoinByCodeComplete.Broadcast(false);
		return;
	}

	UE_LOG(LogFPSR, Log, TEXT("[Session] FindSessions %s — %d result(s)"), bWasSuccessful ? TEXT("OK") : TEXT("FAILED"), Count);
	FPSRFlowLog::Event(this, TEXT("SESSION"), FString::Printf(TEXT("FindSessions complete: %s, %d result(s)"), bWasSuccessful ? TEXT("OK") : TEXT("FAILED"), Count));
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
	FPSRFlowLog::Event(this, TEXT("JOIN"), FString::Printf(TEXT("JoinSearchResult (resultValid=%d)"), SearchResult.IsValid() ? 1 : 0));
	IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid() || !SearchResult.IsValid())
	{
		OnJoinComplete.Broadcast(false);
		return;
	}

	// If a session already exists (hosting, or sitting in a lobby when a Steam invite is accepted), tear it down
	// FIRST and defer the join to the destroy-complete callback — JoinSession with an already-registered name is
	// rejected by most OSS backends. (JoinByCode destroys before searching, so it reaches here already clean.) (merge-gate P2)
	if (Sessions->GetNamedSession(GFPSRSessionName) != nullptr)
	{
		bJoinAfterDestroy = true;
		PendingJoinResult = SearchResult;
		DestroySessionCompleteHandle = Sessions->AddOnDestroySessionCompleteDelegate_Handle(
			FOnDestroySessionCompleteDelegate::CreateUObject(this, &UFPSRSessionSubsystem::HandleDestroySessionComplete));
		if (!Sessions->DestroySession(GFPSRSessionName))
		{
			// Synchronous failure — the completion delegate won't fire, so undo and report instead of hanging. (merge-gate P2)
			Sessions->ClearOnDestroySessionCompleteDelegate_Handle(DestroySessionCompleteHandle);
			bJoinAfterDestroy = false;
			OnJoinComplete.Broadcast(false);
		}
		return;
	}

	JoinSessionCompleteHandle = Sessions->AddOnJoinSessionCompleteDelegate_Handle(
		FOnJoinSessionCompleteDelegate::CreateUObject(this, &UFPSRSessionSubsystem::HandleJoinSessionComplete));

	FPSRFlowLog::Event(this, TEXT("JOIN"), TEXT("JoinSession issued"));
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
	if (bSuccess)
	{
		// Registered under the same name as a hosted session, but this one belongs to the host — HostSession must
		// refuse to "clean it up as stale", which would drop us out of their game without telling anyone.
		bLocalSessionIsHosted = false;
	}
	UE_LOG(LogFPSR, Log, TEXT("[Session] JoinSession '%s' — %s"), *SessionName.ToString(), LexToString(Result));
	FPSRFlowLog::Event(this, TEXT("JOIN"), FString::Printf(TEXT("JoinSession complete: %s"), LexToString(Result)));
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
			FPSRFlowLog::Event(this, TEXT("JOIN"), FString::Printf(TEXT("ClientTravel -> %s"), *ConnectInfo));
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
	FPSRFlowLog::Event(this, TEXT("SESSION"), FString::Printf(TEXT("DestroySession complete: %s"), bWasSuccessful ? TEXT("OK") : TEXT("FAILED")));

	// Our session is gone — drop the host code (a fresh one is generated if we re-host below) and the ownership
	// claim with it, so the next HostSession sees a clean slate rather than a stale "we hosted this" flag.
	CurrentLobbyCode.Reset();
	bLocalSessionIsHosted = false;

	// Host flow: this destroy was a pre-host teardown of a stale session — now create the new one.
	if (bHostAfterDestroy)
	{
		bHostAfterDestroy = false;
		if (bWasSuccessful)
		{
			CreateSessionInternal();
		}
		else
		{
			OnHostComplete.Broadcast(false);
		}
	}
	// Join-by-code flow: this destroy tore down our own (host/joined) session before searching — now search.
	else if (bFindByCodeAfterDestroy)
	{
		bFindByCodeAfterDestroy = false;
		if (bWasSuccessful)
		{
			FindSessionsByCode();
		}
		else
		{
			OnJoinByCodeComplete.Broadcast(false);
		}
	}
	// Invite/browser join flow: this destroy tore down an existing session before joining — now join the pending result.
	else if (bJoinAfterDestroy)
	{
		bJoinAfterDestroy = false;
		if (bWasSuccessful)
		{
			JoinSearchResult(PendingJoinResult);
		}
		else
		{
			OnJoinComplete.Broadcast(false);
		}
	}
}

FString UFPSRSessionSubsystem::GetLobbyCode() const
{
	// Host: the code we generated at create. Client: read it off the joined session's advertised settings.
	if (!CurrentLobbyCode.IsEmpty())
	{
		return CurrentLobbyCode;
	}
	if (IOnlineSessionPtr Sessions = GetSessionInterface())
	{
		if (const FNamedOnlineSession* Named = Sessions->GetNamedSession(GFPSRSessionName))
		{
			FString Code;
			if (Named->SessionSettings.Get(GFPSRLobbyCodeKey, Code))
			{
				return Code;
			}
		}
	}
	return FString();
}

void UFPSRSessionSubsystem::JoinByCode(const FString& Code)
{
	IOnlineSessionPtr Sessions = GetSessionInterface();
	const FString Trimmed = Code.TrimStartAndEnd().ToUpper();
	if (!Sessions.IsValid() || Trimmed.IsEmpty())
	{
		UE_LOG(LogFPSR, Warning, TEXT("[Session] JoinByCode: no session interface or empty code."));
		OnJoinByCodeComplete.Broadcast(false);
		return;
	}

	PendingJoinCode = Trimmed;
	FPSRFlowLog::Event(this, TEXT("JOIN"), FString::Printf(TEXT("JoinByCode requested (code=%s)"), *Trimmed));

	// If we currently hold a session (hosting solo, or already in a lobby), tear it down FIRST — joining while a
	// named session is registered races the still-live session (same pattern as the pre-host teardown).
	if (Sessions->GetNamedSession(GFPSRSessionName) != nullptr)
	{
		bFindByCodeAfterDestroy = true;
		CurrentLobbyCode.Reset();   // leaving our own lobby — drop the host code.
		DestroySessionCompleteHandle = Sessions->AddOnDestroySessionCompleteDelegate_Handle(
			FOnDestroySessionCompleteDelegate::CreateUObject(this, &UFPSRSessionSubsystem::HandleDestroySessionComplete));
		if (!Sessions->DestroySession(GFPSRSessionName))
		{
			// Synchronous failure — the completion delegate won't fire, so undo and report instead of hanging. (merge-gate P2)
			Sessions->ClearOnDestroySessionCompleteDelegate_Handle(DestroySessionCompleteHandle);
			bFindByCodeAfterDestroy = false;
			OnJoinByCodeComplete.Broadcast(false);
		}
		return;
	}

	FindSessionsByCode();
}

void UFPSRSessionSubsystem::FindSessionsByCode()
{
	IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid())
	{
		OnJoinByCodeComplete.Broadcast(false);
		return;
	}

	SearchSettings = MakeShared<FOnlineSessionSearch>();
	SearchSettings->MaxSearchResults = 50;
	SearchSettings->bIsLanQuery = false;
	// Steam: query the LOBBY list (our sessions are presence lobbies), not the dedicated-server browser — otherwise
	// the code search returns 0 candidates even though the host's lobby exists (confirmed via OSS Steam source).
	SearchSettings->QuerySettings.Set(SEARCH_LOBBIES, true, EOnlineComparisonOp::Equals);
	SearchSettings->QuerySettings.Set(GFPSRLobbyCodeKey, PendingJoinCode, EOnlineComparisonOp::Equals);

	FPSRFlowLog::Event(this, TEXT("JOIN"), FString::Printf(TEXT("Code search issued (code=%s)"), *PendingJoinCode));
	bSearchingByCode = true;
	FindSessionsCompleteHandle = Sessions->AddOnFindSessionsCompleteDelegate_Handle(
		FOnFindSessionsCompleteDelegate::CreateUObject(this, &UFPSRSessionSubsystem::HandleFindSessionsComplete));

	if (!Sessions->FindSessions(0, SearchSettings.ToSharedRef()))
	{
		Sessions->ClearOnFindSessionsCompleteDelegate_Handle(FindSessionsCompleteHandle);
		bSearchingByCode = false;
		OnJoinByCodeComplete.Broadcast(false);
	}
}

FString UFPSRSessionSubsystem::GenerateLobbyCode()
{
	// Excludes visually ambiguous chars (0/O, 1/I) for typeability.
	static const TCHAR Alphabet[] = TEXT("ABCDEFGHJKLMNPQRSTUVWXYZ23456789");
	const int32 NumChars = UE_ARRAY_COUNT(Alphabet) - 1;   // drop the null terminator.
	FString Code;
	Code.Reserve(GFPSRLobbyCodeLen);
	for (int32 i = 0; i < GFPSRLobbyCodeLen; ++i)
	{
		Code.AppendChar(Alphabet[FMath::RandRange(0, NumChars - 1)]);
	}
	return Code;
}

void UFPSRSessionSubsystem::ShowInviteUI()
{
	FPSRFlowLog::Event(this, TEXT("SESSION"), TEXT("ShowInviteUI (Steam overlay requested)"));
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
	FPSRFlowLog::Event(this, TEXT("INVITE"), FString::Printf(TEXT("Steam invite accepted (success=%d, resultValid=%d)"), bWasSuccessful ? 1 : 0, InviteResult.IsValid() ? 1 : 0));
	if (!bWasSuccessful || !InviteResult.IsValid())
	{
		UE_LOG(LogFPSR, Warning, TEXT("[Session] Invite accepted but result invalid."));
		return;
	}
	UE_LOG(LogFPSR, Log, TEXT("[Session] Invite accepted — joining host."));
	JoinSearchResult(InviteResult);
}
