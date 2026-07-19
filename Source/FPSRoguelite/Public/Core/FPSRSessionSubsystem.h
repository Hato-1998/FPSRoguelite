// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Subsystems/GameInstanceSubsystem.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "OnlineSessionSettings.h"   // FOnlineSessionSearchResult full definition (PendingJoinResult by-value member)
#include "FPSRSessionSubsystem.generated.h"

/** Result delegate for the host/find/join async flows (BP-bindable). bWasSuccessful = the OSS call succeeded. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FFPSRSessionResult, bool, bWasSuccessful);

/**
 * Thin wrapper over the OnlineSubsystem session interface (Steam-only, P7 §3-2). Drives the lobby/multiplayer
 * flow: host a session and travel into the lobby, accept Steam friend invites to join, and tear sessions down.
 *
 * Server authority / travel: on a successful host the subsystem ServerTravels (listen) into the lobby map; on a
 * successful join the local PlayerController ClientTravels to the host. The primary join path is the Steam
 * overlay invite (OnSessionUserInviteAccepted) — FindSessions/JoinFoundSession exist for an in-game browser but
 * are secondary. Lyra's UCommonSessionSubsystem is the (lightweight) reference; we skip the Request objects.
 */
UCLASS()
class FPSROGUELITE_API UFPSRSessionSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Host a new session (MaxPlayers slots) and, on success, ServerTravel into the lobby map (P7 §3-3). */
	UFUNCTION(BlueprintCallable, Category = "FPSR|Session")
	void HostSession(int32 MaxPlayers = 4);

	/** Search for advertised sessions (secondary path — invites are primary). Results populate the internal
	 *  search; bind OnFindComplete then call JoinFoundSession by index. */
	UFUNCTION(BlueprintCallable, Category = "FPSR|Session")
	void FindSessions();

	/** Number of results from the last FindSessions call. */
	UFUNCTION(BlueprintPure, Category = "FPSR|Session")
	int32 GetSearchResultCount() const;

	/** Join the search result at the given index (from the last FindSessions). */
	UFUNCTION(BlueprintCallable, Category = "FPSR|Session")
	void JoinFoundSession(int32 SearchResultIndex);

	/** Destroy the current session (leaving the lobby / shutting down the host). */
	UFUNCTION(BlueprintCallable, Category = "FPSR|Session")
	void DestroySession();

	/** Open the Steam overlay friend-invite UI for the current session. */
	UFUNCTION(BlueprintCallable, Category = "FPSR|Session")
	void ShowInviteUI();

	/** This lobby's 6-char join code (U11a). Host = the code generated at create; client = the code advertised on
	 *  the joined session's settings. Empty if there is no session yet / it hasn't replicated. */
	UFUNCTION(BlueprintPure, Category = "FPSR|Session")
	FString GetLobbyCode() const;

	/** Join a lobby by its 6-char code (U11a). Tears down the current session first (async) if one exists, then
	 *  searches for the advertised code and joins the first exact match. Reports the lookup via OnJoinByCodeComplete;
	 *  the actual join then flows through OnJoinComplete (same as the invite/browser path). */
	UFUNCTION(BlueprintCallable, Category = "FPSR|Session")
	void JoinByCode(const FString& Code);

	/** Fired when CreateSession completes (success → already traveling to the lobby). */
	UPROPERTY(BlueprintAssignable, Category = "FPSR|Session")
	FFPSRSessionResult OnHostComplete;

	/** Fired when FindSessions completes (success → GetSearchResultCount results available). */
	UPROPERTY(BlueprintAssignable, Category = "FPSR|Session")
	FFPSRSessionResult OnFindComplete;

	/** Fired when a join completes (success → already client-traveling to the host). */
	UPROPERTY(BlueprintAssignable, Category = "FPSR|Session")
	FFPSRSessionResult OnJoinComplete;

	/** Fired when a JoinByCode lookup resolves: true → a matching session was found and the join was started (watch
	 *  OnJoinComplete for the result); false → no session matched the code / the search failed. */
	UPROPERTY(BlueprintAssignable, Category = "FPSR|Session")
	FFPSRSessionResult OnJoinByCodeComplete;

private:
	/** Resolve the active session interface (Steam) for this world. Null if the OSS/session interface is absent. */
	IOnlineSessionPtr GetSessionInterface() const;

	/** Build the session settings and issue CreateSession (uses PendingHostMaxPlayers). Split out so the host flow
	 *  can defer it until an async DestroySession of a stale session completes. */
	void CreateSessionInternal();

	/** Issue a FindSessions filtered by PendingJoinCode (the code-join search). Split out so the join-by-code flow
	 *  can defer it until an async DestroySession of a stale session completes. */
	void FindSessionsByCode();

	/** Shared join path for both a search result and an accepted invite. */
	void JoinSearchResult(const FOnlineSessionSearchResult& SearchResult);

	/** Generate a random 6-char (A-Z0-9) lobby code (host side, at session create). */
	static FString GenerateLobbyCode();

	//~ Native OSS delegate handlers (bound via CreateUObject — not UFUNCTIONs).
	void HandleCreateSessionComplete(FName SessionName, bool bWasSuccessful);
	void HandleFindSessionsComplete(bool bWasSuccessful);
	void HandleJoinSessionComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result);
	void HandleDestroySessionComplete(FName SessionName, bool bWasSuccessful);
	void HandleSessionUserInviteAccepted(const bool bWasSuccessful, const int32 ControllerId, FUniqueNetIdPtr UserId, const FOnlineSessionSearchResult& InviteResult);

	FDelegateHandle CreateSessionCompleteHandle;
	FDelegateHandle FindSessionsCompleteHandle;
	FDelegateHandle JoinSessionCompleteHandle;
	FDelegateHandle DestroySessionCompleteHandle;
	FDelegateHandle InviteAcceptedHandle;

	/** Search state for FindSessions (kept alive across the async call so the callback can read results). */
	TSharedPtr<FOnlineSessionSearch> SearchSettings;

	/** MaxPlayers carried into the deferred CreateSession after a stale session is destroyed. */
	int32 PendingHostMaxPlayers = 4;

	/** True while a stale session is being destroyed before (re)hosting — the destroy callback then creates. */
	bool bHostAfterDestroy = false;

	/** Did WE create the currently registered session, or did we join someone else's? Hosted and joined sessions
	 *  register under the same name (GFPSRSessionName), so the name alone can't tell them apart — and HostSession's
	 *  "tear down the stale session first" step would otherwise drop us out of a game we had joined. Set on
	 *  create-complete, cleared on join-complete and destroy-complete. */
	bool bLocalSessionIsHosted = false;

	/** This host's generated lobby code (empty on a pure client — read GetLobbyCode from the joined session there). */
	FString CurrentLobbyCode;

	/** True while the active FindSessions is a code-join lookup (the complete handler joins the match instead of
	 *  just reporting results). */
	bool bSearchingByCode = false;

	/** True while a stale session is being destroyed before a code-join search — the destroy callback then searches. */
	bool bFindByCodeAfterDestroy = false;

	/** The code being joined (carried into the deferred FindSessionsByCode after a stale session is destroyed). */
	FString PendingJoinCode;

	/** True while an existing session is destroyed before an invite/browser join — the destroy callback then joins. */
	bool bJoinAfterDestroy = false;

	/** The search result to join once the deferred DestroySession completes (invite/browser join — merge-gate P2). */
	FOnlineSessionSearchResult PendingJoinResult;
};
