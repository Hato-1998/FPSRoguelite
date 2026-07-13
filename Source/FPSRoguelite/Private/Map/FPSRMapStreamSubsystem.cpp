// Copyright Epic Games, Inc. All Rights Reserved.

#include "Map/FPSRMapStreamSubsystem.h"
#include "Map/FPSRBoundaryBlocker.h"
#include "Enemy/FPSRFlowFieldSubsystem.h"
#include "Enemy/FPSREnemySpawnSubsystem.h"
#include "Core/FPSRLogChannels.h"
#include "Engine/World.h"
#include "Engine/LevelStreaming.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "EngineUtils.h"

bool UFPSRMapStreamSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}
	if (const UWorld* World = Cast<UWorld>(Outer))
	{
		return World->IsGameWorld();
	}
	return false;
}

bool UFPSRMapStreamSubsystem::HasServerAuthority() const
{
	const UWorld* World = GetWorld();
	return World && World->GetNetMode() != NM_Client;
}

void UFPSRMapStreamSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PollTimer);
	}
	Pending.Reset();
	ReadyMaps.Reset();
	Super::Deinitialize();
}

void UFPSRMapStreamSubsystem::RequestStreamIn(const FGameplayTag& MapId, FName LevelName)
{
	if (!HasServerAuthority() || !MapId.IsValid() || LevelName.IsNone())
	{
		return;
	}
	if (ReadyMaps.Contains(MapId))
	{
		return; // already streamed + opened
	}
	for (const FPendingStream& P : Pending)
	{
		if (P.MapId == MapId)
		{
			return; // already streaming in
		}
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Begin async load + make visible. bShouldBlockOnLoad=false so we don't hitch the whole frame; we gate the
	// game-side actions (bake / spawn / boundary) on the VERIFIED LoadedVisible state in PollPending, never on this
	// latent callback (which fires — and would fire even on a bad name — before collision is fully registered).
	FLatentActionInfo LatentInfo;
	LatentInfo.UUID = NextLatentUUID++;
	LatentInfo.CallbackTarget = this;
	UGameplayStatics::LoadStreamLevel(World, LevelName, /*bMakeVisibleAfterLoad*/ true, /*bShouldBlockOnLoad*/ false, LatentInfo);

	Pending.Add({ MapId, LevelName, 0.0f });
	UE_LOG(LogFPSR, Log, TEXT("[MapStream] stream-in requested: map '%s' -> level '%s'."), *MapId.ToString(), *LevelName.ToString());

	if (!World->GetTimerManager().IsTimerActive(PollTimer))
	{
		World->GetTimerManager().SetTimer(PollTimer, this, &UFPSRMapStreamSubsystem::PollPending, PollInterval, true);
	}
}

bool UFPSRMapStreamSubsystem::IsLevelCollisionReady(FName LevelName) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	const ULevelStreaming* LS = UGameplayStatics::GetStreamingLevel(const_cast<UWorld*>(World), LevelName);
	// LoadedVisible = AddToWorld has fully completed, so components (and their WorldStatic collision) are registered —
	// the exact predicate the flow-field bake's traces need (Codex R4). GetLoadedLevel() guards a null terminal state.
	return LS && LS->GetLevelStreamingState() == ELevelStreamingState::LoadedVisible && LS->GetLoadedLevel() != nullptr;
}

void UFPSRMapStreamSubsystem::PollPending()
{
	if (!HasServerAuthority())
	{
		return;
	}
	for (int32 i = Pending.Num() - 1; i >= 0; --i)
	{
		Pending[i].Elapsed += PollInterval;
		if (IsLevelCollisionReady(Pending[i].LevelName))
		{
			const FGameplayTag ReadyMap = Pending[i].MapId;
			Pending.RemoveAt(i);
			HandleMapReady(ReadyMap);
		}
		else if (Pending[i].Elapsed >= StreamTimeout)
		{
			UE_LOG(LogFPSR, Error,
				TEXT("[MapStream] stream-in of level '%s' (map '%s') did not reach LoadedVisible within %.0fs — passage stays sealed. Check the level name is an authored streaming sublevel of the persistent map."),
				*Pending[i].LevelName.ToString(), *Pending[i].MapId.ToString(), StreamTimeout);
			Pending.RemoveAt(i); // leave the boundary blocker up (never open on an unverified signal)
		}
	}

	if (Pending.Num() == 0)
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(PollTimer);
		}
	}
}

void UFPSRMapStreamSubsystem::HandleMapReady(const FGameplayTag& MapId)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// 1. Bake the per-map flow field now that the sublevel's WorldStatic collision is registered. If the streamed level
	//    has no AFPSRFlowFieldBoundsVolume tagged with this MapId (a content-authoring error), the map is NOT routable —
	//    leave the boundary SEALED and do NOT mark it ready rather than open a map with no flow field / occupancy routing
	//    (Codex merge-gate P2). The sealed door surfaces the content error instead of a silently-broken map.
	UFPSRFlowFieldSubsystem* Flow = World->GetSubsystem<UFPSRFlowFieldSubsystem>();
	if (!Flow || !Flow->BakeDiscoveredMap(MapId))
	{
		UE_LOG(LogFPSR, Error,
			TEXT("[MapStream] map '%s' is visible but its flow-field bake FAILED (no bounds volume with that MapId in the streamed sublevel) — passage stays sealed. Author an AFPSRFlowFieldBoundsVolume with MapId '%s' in that sublevel."),
			*MapId.ToString(), *MapId.ToString());
		return; // do NOT add to ReadyMaps / re-cache / open the boundary
	}
	ReadyMaps.Add(MapId);

	// 2. Re-cache spawn points so the streamed sublevel's points become selectable (they weren't cached at world begin).
	if (UFPSREnemySpawnSubsystem* Spawn = World->GetSubsystem<UFPSREnemySpawnSubsystem>())
	{
		Spawn->RefreshSpawnPointCache();
	}

	// 3. Drop every boundary blocker guarding this map so players may cross (replicated -> clients drop it too).
	int32 Opened = 0;
	for (TActorIterator<AFPSRBoundaryBlocker> It(World); It; ++It)
	{
		if (AFPSRBoundaryBlocker* Blocker = *It)
		{
			if (Blocker->GetTargetMapId() == MapId)
			{
				Blocker->SetBlocking(false);
				++Opened;
			}
		}
	}

	UE_LOG(LogFPSR, Log, TEXT("[MapStream] map '%s' READY: field baked, spawn points recached, %d boundary blocker(s) opened."),
		*MapId.ToString(), Opened);
}
