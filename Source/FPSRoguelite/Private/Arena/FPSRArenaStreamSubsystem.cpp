// Copyright Epic Games, Inc. All Rights Reserved.

#include "Arena/FPSRArenaStreamSubsystem.h"
#include "Arena/FPSRArenaActor.h"
#include "Core/FPSRLogChannels.h"
#include "Engine/Level.h"
#include "Engine/LevelStreaming.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

bool UFPSRArenaStreamSubsystem::ShouldCreateSubsystem(UObject* Outer) const
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

bool UFPSRArenaStreamSubsystem::HasServerAuthority() const
{
	const UWorld* World = GetWorld();
	return World && World->GetNetMode() != NM_Client;
}

void UFPSRArenaStreamSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PollTimer);
	}
	Pending.Reset();
	Roster.Reset();
	PendingParkAfterOrder = INDEX_NONE;
	Super::Deinitialize();
}

void UFPSRArenaStreamSubsystem::RefreshRoster() const
{
	Roster.Reset();

	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	for (ULevelStreaming* Streaming : World->GetStreamingLevels())
	{
		if (!Streaming)
		{
			continue;
		}
		// GetLoadedLevel() is non-null once the PACKAGE is loaded, which happens well before the level is made
		// visible — that gap is the whole point here. An arena that is still loading simply is not in the roster
		// yet; RefreshRoster is called again on the next query rather than caching an incomplete answer.
		const ULevel* Level = Streaming->GetLoadedLevel();
		if (!Level)
		{
			continue;
		}

		AFPSRArenaActor* Found = nullptr;
		for (AActor* Actor : Level->Actors)
		{
			if (AFPSRArenaActor* Arena = Cast<AFPSRArenaActor>(Actor))
			{
				if (Found)
				{
					// Invariant 4 (one arena = one sublevel = one mask). Two in one level makes "which mask does
					// this level carry" ambiguous, and every spatial membership answer with it. The editor
					// validator is what should have caught this; say so here rather than picking one silently.
					UE_LOG(LogFPSR, Error,
						TEXT("[ArenaStream] Level '%s' contains more than one AFPSRArenaActor (%s and %s). ADR 0012 invariant 4: one arena per sublevel. Keeping %s."),
						*Streaming->GetWorldAssetPackageName(), *Found->GetName(), *Arena->GetName(), *Found->GetName());
					break;
				}
				Found = Arena;
			}
		}
		if (!Found)
		{
			continue; // an ordinary (non-arena) sublevel — not this subsystem's business
		}

		FFPSRArenaSlot Slot;
		Slot.PackageName = Streaming->GetWorldAssetPackageFName();
		Slot.StageOrder = Found->GetStageOrder();
		Slot.Arena = Found;
		Slot.bStartsActive = Found->StartsActive();
		Roster.Add(Slot);
	}

	Roster.Sort([](const FFPSRArenaSlot& A, const FFPSRArenaSlot& B)
	{
		// Ties broken by package name so the order does not depend on the streaming list's own order. Two arenas
		// sharing a StageOrder is an authoring mistake the editor validator flags; this only guarantees the answer
		// is stable, not that it matches the tie-break AFPSRArenaActor::FindAllInWorld picks (that one sorts by
		// actor name, and the two lists exist for different jobs — this one includes arenas not yet in the world).
		return A.StageOrder != B.StageOrder
			? A.StageOrder < B.StageOrder
			: A.PackageName.LexicalLess(B.PackageName);
	});
}

bool UFPSRArenaStreamSubsystem::FindSlot(int32 StageOrder, FFPSRArenaSlot& OutSlot) const
{
	RefreshRoster();
	if (const FFPSRArenaSlot* Found = Roster.FindByPredicate([StageOrder](const FFPSRArenaSlot& S) { return S.StageOrder == StageOrder; }))
	{
		OutSlot = *Found;
		return true;
	}
	return false;
}

int32 UFPSRArenaStreamSubsystem::FindStartingStageOrder() const
{
	RefreshRoster();
	for (const FFPSRArenaSlot& Slot : Roster)
	{
		if (Slot.bStartsActive)
		{
			return Slot.StageOrder;
		}
	}
	return Roster.Num() > 0 ? Roster[0].StageOrder : INDEX_NONE;
}

int32 UFPSRArenaStreamSubsystem::GetNextStageOrder(int32 CurrentStageOrder) const
{
	RefreshRoster();
	if (Roster.Num() == 0)
	{
		return INDEX_NONE;
	}
	const int32 Index = Roster.IndexOfByPredicate(
		[CurrentStageOrder](const FFPSRArenaSlot& S) { return S.StageOrder == CurrentStageOrder; });
	// An unknown current arena falls to the first entry rather than to "no next": the roster is authored content
	// and the caller has a transition to run either way, so advancing to a real arena beats stalling the run.
	const int32 NextIndex = (Index == INDEX_NONE) ? 0 : (Index + 1) % Roster.Num();
	return Roster[NextIndex].StageOrder;
}

bool UFPSRArenaStreamSubsystem::IsLevelVisibleLocally(FName PackageName) const
{
	const UWorld* World = GetWorld();
	if (!World || PackageName.IsNone())
	{
		return false;
	}
	for (ULevelStreaming* Streaming : World->GetStreamingLevels())
	{
		if (Streaming && Streaming->GetWorldAssetPackageFName() == PackageName)
		{
			// LoadedVisible = AddToWorld fully complete, so components (and their collision) are registered. This
			// is the same predicate UFPSRMapStreamSubsystem gates on, and the reason neither gates on the latent
			// LoadStreamLevel callback: that fires before registration finishes.
			return Streaming->GetLevelStreamingState() == ELevelStreamingState::LoadedVisible
				&& Streaming->GetLoadedLevel() != nullptr;
		}
	}
	return false;
}

void UFPSRArenaStreamSubsystem::RequestPark(int32 StageOrder)
{
	if (!HasServerAuthority())
	{
		return;
	}

	UWorld* World = GetWorld();
	FFPSRArenaSlot Slot;
	if (!World || !FindSlot(StageOrder, Slot))
	{
		// Not an error. A map with one arena cycles to itself (AFPSRArenaActor cycling is intended behaviour, not
		// an authoring gap), so "there is no next arena to park" is a normal steady state.
		return;
	}

	if (IsLevelVisibleLocally(Slot.PackageName))
	{
		// Already in the world on this machine. Clients may still be catching up — that is what
		// IsReadyForEveryone answers — but there is nothing more for the server to request.
		return;
	}
	for (const FPendingPark& P : Pending)
	{
		if (P.StageOrder == StageOrder)
		{
			return; // already parking
		}
	}

	// bShouldBlockOnLoad stays FALSE. Setting it means bConsiderTimeLimit=false inside AddToWorld, i.e. the whole
	// level enters the world in one frame — a hitch by definition, and the exact cost this whole mechanism exists
	// to move out of the transition window. bMakeVisibleAfterLoad is true because visibility is the expensive
	// half we are pre-paying; loading alone would not shorten anything.
	FLatentActionInfo LatentInfo;
	LatentInfo.UUID = NextLatentUUID++;
	LatentInfo.CallbackTarget = this;
	UGameplayStatics::LoadStreamLevel(World, Slot.PackageName, /*bMakeVisibleAfterLoad*/ true,
		/*bShouldBlockOnLoad*/ false, LatentInfo);

	Pending.Add({ StageOrder, 0.0f });
	UE_LOG(LogFPSR, Log, TEXT("[ArenaStream] parking arena stage %d ('%s') — visibility requested."),
		StageOrder, *Slot.PackageName.ToString());

	if (!World->GetTimerManager().IsTimerActive(PollTimer))
	{
		World->GetTimerManager().SetTimer(PollTimer, this, &UFPSRArenaStreamSubsystem::PollPending, PollInterval, true);
	}
}

void UFPSRArenaStreamSubsystem::RequestParkAfter(int32 CurrentStageOrder)
{
	if (!HasServerAuthority() || CurrentStageOrder == INDEX_NONE)
	{
		return;
	}

	const int32 NextOrder = GetNextStageOrder(CurrentStageOrder);
	if (NextOrder != INDEX_NONE && NextOrder != CurrentStageOrder)
	{
		RequestPark(NextOrder);
		return;
	}

	// The roster cannot answer yet — either the successor's package is still loading, or this really is a
	// single-arena map. Those look identical right now, so retry on the poll timer instead of guessing.
	PendingParkAfterOrder = CurrentStageOrder;
	ParkAfterElapsed = 0.0f;
	if (UWorld* World = GetWorld())
	{
		if (!World->GetTimerManager().IsTimerActive(PollTimer))
		{
			World->GetTimerManager().SetTimer(PollTimer, this, &UFPSRArenaStreamSubsystem::PollPending, PollInterval, true);
		}
	}
}

void UFPSRArenaStreamSubsystem::PollPending()
{
	if (!HasServerAuthority())
	{
		return;
	}

	if (PendingParkAfterOrder != INDEX_NONE)
	{
		ParkAfterElapsed += PollInterval;
		const int32 NextOrder = GetNextStageOrder(PendingParkAfterOrder);
		if (NextOrder != INDEX_NONE && NextOrder != PendingParkAfterOrder)
		{
			PendingParkAfterOrder = INDEX_NONE;
			RequestPark(NextOrder);
		}
		else if (ParkAfterElapsed >= RosterResolveTimeout)
		{
			// Log, not Warning: a one-arena map is a legitimate configuration (it cycles to itself), and this is
			// the only place that can tell the reader which of the two situations they are in.
			UE_LOG(LogFPSR, Log,
				TEXT("[ArenaStream] no arena follows stage %d after %.0fs — either this map has a single arena (it will cycle to itself), or the next arena's sublevel never entered the roster. The roster needs the PACKAGE loaded, which for an authored sublevel means Levels panel > right-click > Change Streaming Method > Always Loaded (ULevelStreamingAlwaysLoaded::ShouldBeLoaded is the only authorable way to force it; bShouldBeLoaded itself is not EditAnywhere). Leave 'Visibility in Game' OFF on a reserve arena — ShouldBeVisible is NOT overridden by that class, so Always Loaded + hidden is exactly 'loaded but not visible'."),
				PendingParkAfterOrder, RosterResolveTimeout);
			PendingParkAfterOrder = INDEX_NONE;
		}
	}

	for (int32 i = Pending.Num() - 1; i >= 0; --i)
	{
		const int32 StageOrder = Pending[i].StageOrder;
		FFPSRArenaSlot Slot;
		if (!FindSlot(StageOrder, Slot))
		{
			UE_LOG(LogFPSR, Warning, TEXT("[ArenaStream] arena stage %d vanished from the roster while parking — dropped."), StageOrder);
			Pending.RemoveAt(i);
			continue;
		}

		Pending[i].Elapsed += PollInterval;

		if (IsLevelVisibleLocally(Slot.PackageName))
		{
			// It is in the world now, so hide it. The arena's own BeginPlay already did this (bStartsActive is
			// false on a reserve arena) — this is deliberately redundant, because "a parked arena ends up hidden"
			// is a property of parking, not something to infer from how the arena happens to be authored. Both
			// calls are idempotent.
			if (AFPSRArenaActor* Arena = Slot.Arena.Get())
			{
				if (!Arena->IsArenaActive())
				{
					Arena->SetArenaActive(false);
				}
			}

			TArray<FString> NotReady;
			GetConnectionsNotReady(StageOrder, NotReady);
			UE_LOG(LogFPSR, Log,
				TEXT("[ArenaStream] arena stage %d ('%s') parked on the server after %.1fs; %d client(s) still catching up."),
				StageOrder, *Slot.PackageName.ToString(), Pending[i].Elapsed, NotReady.Num());

			Pending.RemoveAt(i);
			continue;
		}

		if (Pending[i].Elapsed >= ParkTimeout)
		{
			// Warn and stop polling THIS one, but do not mark it unusable: the level may still land, and the
			// transition's own readiness check is what decides whether the swap can go ahead. Silence here would
			// turn a stuck stream into a mystery stall at the next transition instead.
			UE_LOG(LogFPSR, Error,
				TEXT("[ArenaStream] arena stage %d ('%s') did not reach LoadedVisible within %.0fs. Check it is an authored streaming sublevel of the persistent map (Levels panel > Add Existing) — a level created at runtime cannot work here, because clients resolve streaming status by package name against their OWN persistent level."),
				StageOrder, *Slot.PackageName.ToString(), ParkTimeout);
			Pending.RemoveAt(i);
		}
	}

	if (Pending.Num() == 0 && PendingParkAfterOrder == INDEX_NONE)
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(PollTimer);
		}
	}
}

void UFPSRArenaStreamSubsystem::GetConnectionsNotReady(int32 StageOrder, TArray<FString>& OutNames) const
{
	OutNames.Reset();

	const UWorld* World = GetWorld();
	FFPSRArenaSlot Slot;
	if (!World || !FindSlot(StageOrder, Slot))
	{
		return; // unknown arena — see IsReadyForEveryone on why that reads as "nothing to wait for"
	}

	if (!IsLevelVisibleLocally(Slot.PackageName))
	{
		OutNames.Add(TEXT("server"));
	}

	// Only the server can answer for clients, and this is the authoritative source: it is the same set
	// UNetDriver consults to decide whether an actor in a streamed level may be replicated to a connection, so a
	// name missing here means that client would genuinely receive none of the new arena's actors.
	const UNetDriver* NetDriver = World->GetNetDriver();
	if (!NetDriver)
	{
		return; // standalone — the server check above is the whole answer
	}
	for (const UNetConnection* Connection : NetDriver->ClientConnections)
	{
		if (!Connection)
		{
			continue;
		}
		if (!Connection->ClientVisibleLevelNames.Contains(Slot.PackageName))
		{
			OutNames.Add(Connection->GetName());
		}
	}
}

bool UFPSRArenaStreamSubsystem::IsReadyForEveryone(int32 StageOrder) const
{
	TArray<FString> NotReady;
	GetConnectionsNotReady(StageOrder, NotReady);
	return NotReady.Num() == 0;
}
