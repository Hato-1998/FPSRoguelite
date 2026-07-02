// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Subsystems/GameInstanceSubsystem.h"
#include "FPSRSaveGameSubsystem.generated.h"

class URogueliteSaveGame;

/** Why a save is being requested — distinguishes the run-end flush, a lobby/menu checkpoint, and a post-load
 *  migration re-persist (RunFlow §2-11 "런중 vs 로비 저장 구분"). Diagnostic + drives retry/broadcast context. */
UENUM(BlueprintType)
enum class EFPSRSaveReason : uint8
{
	RunEnd      UMETA(DisplayName = "Run End"),
	Lobby       UMETA(DisplayName = "Lobby / Menu"),
	Migration   UMETA(DisplayName = "Post-load Migration"),
	Manual      UMETA(DisplayName = "Manual / Debug"),
};

/** Broadcast after each save attempt (success or failure) so UI / systems can react (e.g. a "saving…" spinner or a
 *  save-failure toast). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FFPSROnSaveComplete, bool, bSuccess, EFPSRSaveReason, Reason);

/** The single gateway for all meta-progression save I/O (RunFlow §2-11: UI/Actors NEVER call SaveGameToSlot
 *  directly — everything routes through here). GameInstance-scoped so it persists across level / seamless travel:
 *  an async save started at run end completes after the client travels to the lobby.
 *
 *  Per-player LOCAL ownership: the meta save is each player's own local account asset. On a dedicated server (no
 *  local player) every op is a no-op — the server never reads or writes a player's save. On a listen host / client /
 *  standalone the local player owns and persists its own save. (First-principle: co-op = server drives gameplay,
 *  but meta is local; the server only SIGNALS "persist now", it never touches the bytes.) */
UCLASS()
class FPSROGUELITE_API UFPSRSaveGameSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** The loaded meta save — never null (a fresh default is created if none exists, is corrupt, or on a dedicated
	 *  server). Callers mutate this then RequestSave(); they must not persist it themselves. */
	URogueliteSaveGame* GetSaveGame() const { return SaveGame; }

	/** Async-persist the current save through the configured primary slot. Reason distinguishes run-end vs lobby vs
	 *  migration checkpoints. No-op on a dedicated server. On SUCCESS the backup slot is mirrored; on FAILURE the
	 *  backup is left untouched (last-good preserved) and the save stays "pending" so the next map load retries. */
	void RequestSave(EFPSRSaveReason Reason);

	/** Fires after every save attempt (success or failure). */
	UPROPERTY(BlueprintAssignable, Category = "FPSR|Save")
	FFPSROnSaveComplete OnSaveComplete;

	/** Resolve a stored CardId through a deprecation-redirect map (rename/merge fallback — RunFlow §2-11 "해금 데이터
	 *  삭제/리네임 fallback"). Static + pure so it is unit-testable without a GameInstance. Follows a short redirect
	 *  chain (old -> mid -> new), bounded against cycles; NAME_None or an unmapped key passes through unchanged. */
	static FName ResolveCardId(const TMap<FName, FName>& Redirects, FName StoredId);

	/** Instance convenience: resolve through the project's configured redirects (UFPSRSaveSettings). */
	FName ResolveCardId(FName StoredId) const;

private:
	/** Load the primary slot; on corruption/absence fall back to the backup slot, then to a fresh default. Migrates
	 *  a loaded older save up to the current schema and re-persists if it changed. Skipped on a dedicated server. */
	void LoadOrCreate();

	/** PostLoadMapWithWorld hook: re-flush a save that never confirmed before a travel (belt-and-suspenders). */
	void HandlePostLoadMap(UWorld* LoadedWorld);

	/** AsyncSaveGameToSlot completion — mirrors to backup on success, keeps the save pending + preserves backup on
	 *  failure, then broadcasts OnSaveComplete. */
	void HandleAsyncSaveComplete(const FString& SlotName, int32 UserIndex, bool bSuccess);

	UPROPERTY(Transient)
	TObjectPtr<URogueliteSaveGame> SaveGame = nullptr;

	/** True between a RequestSave and its successful completion; a map load re-flushes while still pending (covers a
	 *  save interrupted by travel). */
	bool bSavePending = false;

	/** Reason of the in-flight/pending save (re-used on a reflush + reported in OnSaveComplete). */
	EFPSRSaveReason PendingReason = EFPSRSaveReason::Manual;

	FDelegateHandle PostLoadMapHandle;
};
