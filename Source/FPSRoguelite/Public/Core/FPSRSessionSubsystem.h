// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Subsystems/GameInstanceSubsystem.h"
#include "Interfaces/OnlineSessionInterface.h"
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

	/** Fired when CreateSession completes (success → already traveling to the lobby). */
	UPROPERTY(BlueprintAssignable, Category = "FPSR|Session")
	FFPSRSessionResult OnHostComplete;

	/** Fired when FindSessions completes (success → GetSearchResultCount results available). */
	UPROPERTY(BlueprintAssignable, Category = "FPSR|Session")
	FFPSRSessionResult OnFindComplete;

	/** Fired when a join completes (success → already client-traveling to the host). */
	UPROPERTY(BlueprintAssignable, Category = "FPSR|Session")
	FFPSRSessionResult OnJoinComplete;

private:
	/** Resolve the active session interface (Steam) for this world. Null if the OSS/session interface is absent. */
	IOnlineSessionPtr GetSessionInterface() const;

	/** Build the session settings and issue CreateSession (uses PendingHostMaxPlayers). Split out so the host flow
	 *  can defer it until an async DestroySession of a stale session completes. */
	void CreateSessionInternal();

	/** Shared join path for both a search result and an accepted invite. */
	void JoinSearchResult(const FOnlineSessionSearchResult& SearchResult);

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
};
