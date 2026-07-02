// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetaProgression/FPSRSaveGameSubsystem.h"

#include "MetaProgression/RogueliteSaveGame.h"
#include "Settings/FPSRSaveSettings.h"
#include "Core/FPSRLogChannels.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/UObjectGlobals.h"   // FCoreUObjectDelegates

void UFPSRSaveGameSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// A dedicated server has no local player and must NEVER own a player's meta save. Keep a transient default so
	// GetSaveGame() stays non-null, but skip the disk load and every persistence op (RequestSave early-outs too).
	if (IsRunningDedicatedServer())
	{
		SaveGame = NewObject<URogueliteSaveGame>(this);
		UE_LOG(LogFPSR, Log, TEXT("[Save] Dedicated server — meta save disabled (per-player local ownership)."));
		return;
	}

	LoadOrCreate();

	// Belt-and-suspenders retry: re-flush a still-pending save whenever a new map finishes loading. A run-end save is
	// a RELIABLE RPC issued ~PostRunTravelDelay before the post-run ServerTravel, so it normally reaches this
	// subsystem and starts (and the GameInstance carries the async write across travel) before the client leaves.
	PostLoadMapHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &UFPSRSaveGameSubsystem::HandlePostLoadMap);
}

void UFPSRSaveGameSubsystem::Deinitialize()
{
	FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadMapHandle);
	Super::Deinitialize();
}

void UFPSRSaveGameSubsystem::LoadOrCreate()
{
	const UFPSRSaveSettings* Settings = GetDefault<UFPSRSaveSettings>();
	const FString PrimarySlot = Settings->PrimarySlotName;
	const FString BackupSlot = Settings->BackupSlotName;
	const int32 UserIndex = Settings->UserIndex;

	URogueliteSaveGame* Loaded = nullptr;

	if (UGameplayStatics::DoesSaveGameExist(PrimarySlot, UserIndex))
	{
		Loaded = Cast<URogueliteSaveGame>(UGameplayStatics::LoadGameFromSlot(PrimarySlot, UserIndex));
		if (!Loaded)
		{
			UE_LOG(LogFPSR, Warning, TEXT("[Save] Primary slot '%s' failed to load (corrupt/incompatible) — trying backup '%s'."), *PrimarySlot, *BackupSlot);
		}
	}

	if (!Loaded && !BackupSlot.IsEmpty() && UGameplayStatics::DoesSaveGameExist(BackupSlot, UserIndex))
	{
		Loaded = Cast<URogueliteSaveGame>(UGameplayStatics::LoadGameFromSlot(BackupSlot, UserIndex));
		if (Loaded)
		{
			UE_LOG(LogFPSR, Warning, TEXT("[Save] Recovered meta save from backup slot '%s'."), *BackupSlot);
		}
	}

	if (!Loaded)
	{
		Loaded = NewObject<URogueliteSaveGame>(this);
		UE_LOG(LogFPSR, Log, TEXT("[Save] No usable meta save — created defaults (v%d)."), URogueliteSaveGame::CurrentSaveVersion);
	}

	SaveGame = Loaded;

	// Bring an older save up to the current schema; re-persist so the migration lands on disk.
	if (SaveGame->MigrateIfNeeded())
	{
		RequestSave(EFPSRSaveReason::Migration);
	}
}

void UFPSRSaveGameSubsystem::RequestSave(EFPSRSaveReason Reason)
{
	// Per-player local ownership: the server never persists a save (it only signals clients to persist their own).
	if (IsRunningDedicatedServer())
	{
		return;
	}
	if (!SaveGame)
	{
		UE_LOG(LogFPSR, Warning, TEXT("[Save] RequestSave(reason=%d) with no save object — ignored."), static_cast<int32>(Reason));
		return;
	}

	const UFPSRSaveSettings* Settings = GetDefault<UFPSRSaveSettings>();
	bSavePending = true;
	PendingReason = Reason;

	FAsyncSaveGameToSlotDelegate Done;
	Done.BindUObject(this, &UFPSRSaveGameSubsystem::HandleAsyncSaveComplete);
	UGameplayStatics::AsyncSaveGameToSlot(SaveGame, Settings->PrimarySlotName, Settings->UserIndex, Done);
}

void UFPSRSaveGameSubsystem::HandleAsyncSaveComplete(const FString& SlotName, int32 UserIndex, bool bSuccess)
{
	if (bSuccess)
	{
		bSavePending = false;

		// Mirror the just-written primary into the backup slot, so a later CORRUPT primary can be recovered on load.
		// The backup is written only AFTER a good primary — a failed primary never clobbers the last-good backup.
		const UFPSRSaveSettings* Settings = GetDefault<UFPSRSaveSettings>();
		if (SaveGame && !Settings->BackupSlotName.IsEmpty() && Settings->BackupSlotName != Settings->PrimarySlotName)
		{
			UGameplayStatics::AsyncSaveGameToSlot(SaveGame, Settings->BackupSlotName, Settings->UserIndex, FAsyncSaveGameToSlotDelegate());
		}
		UE_LOG(LogFPSR, Log, TEXT("[Save] Saved '%s' (reason=%d)."), *SlotName, static_cast<int32>(PendingReason));
	}
	else
	{
		// Keep bSavePending = true so the next map load retries; leave the backup untouched (last-good preserved).
		UE_LOG(LogFPSR, Error, TEXT("[Save] FAILED to save '%s' (reason=%d) — backup preserved, will retry on next map load."), *SlotName, static_cast<int32>(PendingReason));
	}

	OnSaveComplete.Broadcast(bSuccess, PendingReason);
}

void UFPSRSaveGameSubsystem::HandlePostLoadMap(UWorld* LoadedWorld)
{
	// If a prior save never confirmed (e.g. interrupted by travel), retry now that we're in a fresh map. Harmless
	// when nothing is pending.
	if (bSavePending && !IsRunningDedicatedServer())
	{
		UE_LOG(LogFPSR, Warning, TEXT("[Save] Map load with a pending save — re-flushing (reason=%d)."), static_cast<int32>(PendingReason));
		RequestSave(PendingReason);
	}
}

FName UFPSRSaveGameSubsystem::ResolveCardId(const TMap<FName, FName>& Redirects, FName StoredId)
{
	if (StoredId.IsNone() || Redirects.Num() == 0)
	{
		return StoredId;
	}

	FName Current = StoredId;
	// Follow a short redirect chain (A -> B -> C), bounded so a misconfigured cycle can't spin forever.
	for (int32 Hops = 0; Hops < 8; ++Hops)
	{
		const FName* Next = Redirects.Find(Current);
		if (!Next || Next->IsNone() || *Next == Current)
		{
			break;
		}
		Current = *Next;
	}
	return Current;
}

FName UFPSRSaveGameSubsystem::ResolveCardId(FName StoredId) const
{
	return ResolveCardId(GetDefault<UFPSRSaveSettings>()->DeprecatedCardIdRedirects, StoredId);
}
