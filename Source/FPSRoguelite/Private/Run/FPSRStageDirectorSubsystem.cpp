// Copyright Epic Games, Inc. All Rights Reserved.

#include "Run/FPSRStageDirectorSubsystem.h"
#include "Run/FPSRRunScheduleDataAsset.h"
#include "Arena/FPSRArenaActor.h"
#include "Enemy/FPSREnemySpawnSubsystem.h"
#include "Hero/FPSRCharacter.h"
#include "Core/FPSRPlayerState.h"
#include "Core/FPSRLogChannels.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "TimerManager.h"

bool UFPSRStageDirectorSubsystem::HasServerAuthority() const
{
	const UWorld* World = GetWorld();
	return World && World->GetNetMode() != NM_Client;
}

AFPSRGameState* UFPSRStageDirectorSubsystem::GetGS() const
{
	UWorld* World = GetWorld();
	return World ? World->GetGameState<AFPSRGameState>() : nullptr;
}

// ---------------------------------------------------------------------------------------------------------------
// Pure, worldless predicates — see the header for what each one means and why it is exposed static.
// ---------------------------------------------------------------------------------------------------------------

EFPSRStageTransitionPhase UFPSRStageDirectorSubsystem::DecidePhaseAfterDealing(bool bRunPaused)
{
	return bRunPaused ? EFPSRStageTransitionPhase::Pending : EFPSRStageTransitionPhase::Swapping;
}

bool UFPSRStageDirectorSubsystem::CanSwapNow(bool bRunPaused)
{
	return !bRunPaused;
}

int32 UFPSRStageDirectorSubsystem::ComputeStageSeed(int32 BaseSeed, int32 StageIndex)
{
	// Deterministic integer formula — see the header. StageSeedStride just needs to be a fixed, reasonably large
	// odd-ish spread; the exact value carries no other meaning.
	return BaseSeed + StageIndex * StageSeedStride;
}

int32 UFPSRStageDirectorSubsystem::NextArenaIndex(int32 CurrentIndex, int32 Count)
{
	if (Count <= 0)
	{
		return INDEX_NONE;
	}
	return (CurrentIndex + 1) % Count;
}

bool UFPSRStageDirectorSubsystem::IsDealingOpen(EFPSRStageTransitionPhase Phase, float NowServerTime, float DealingEndServerTime)
{
	return Phase == EFPSRStageTransitionPhase::Grace && NowServerTime < DealingEndServerTime;
}

// ---------------------------------------------------------------------------------------------------------------
// State machine
// ---------------------------------------------------------------------------------------------------------------

void UFPSRStageDirectorSubsystem::RequestTransition()
{
	if (!HasServerAuthority())
	{
		return;
	}
	AFPSRGameState* GS = GetGS();
	if (!GS)
	{
		return;
	}

	// A transition is already running: several suppressors can exist in one arena, or one explosion can finish
	// more than one at once — only the FIRST request may start the state machine, the rest are silently ignored.
	if (GS->GetStageTransitionPhase() != EFPSRStageTransitionPhase::None)
	{
		return;
	}

	const float GraceSeconds = GS->GetRunSchedule() ? GS->GetRunSchedule()->StageGraceSeconds : DefaultStageGraceSeconds;
	const float DealingEnd = GS->GetServerWorldTimeSeconds() + GraceSeconds;
	GS->SetStageTransition(EFPSRStageTransitionPhase::Grace, DealingEnd);

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			DealingTimerHandle, this, &UFPSRStageDirectorSubsystem::OnDealingWindowClosed, GraceSeconds, /*bLoop*/false);
	}

	UE_LOG(LogFPSR, Log, TEXT("[StageDirector] Transition requested — dealing window %.1fs."), GraceSeconds);
}

bool UFPSRStageDirectorSubsystem::IsTransitioning() const
{
	const AFPSRGameState* GS = GetGS();
	return GS && GS->IsStageTransitionActive();
}

void UFPSRStageDirectorSubsystem::OnDealingWindowClosed()
{
	AFPSRGameState* GS = GetGS();
	if (!GS)
	{
		return;
	}

	const EFPSRStageTransitionPhase NextPhase = DecidePhaseAfterDealing(GS->IsRunPaused());
	GS->SetStageTransition(NextPhase, 0.0f); // dealing has closed either way — no end-timestamp left to carry

	if (NextPhase == EFPSRStageTransitionPhase::Pending)
	{
		// The card-selection freeze is still up — wait for it to clear (invariant 8, see DecidePhaseAfterDealing)
		// rather than swap underneath the card screen. Bind once; PerformSwap unbinds on the way out.
		if (!bBoundRunStateChanged)
		{
			GS->OnRunStateChanged.AddDynamic(this, &UFPSRStageDirectorSubsystem::HandleRunStateChanged);
			bBoundRunStateChanged = true;
		}
	}
	else
	{
		PerformSwap();
	}
}

void UFPSRStageDirectorSubsystem::HandleRunStateChanged()
{
	AFPSRGameState* GS = GetGS();
	if (!GS || GS->GetStageTransitionPhase() != EFPSRStageTransitionPhase::Pending)
	{
		return; // OnRunStateChanged fires for many unrelated reasons — only act while actually Pending
	}
	TrySwap();
}

void UFPSRStageDirectorSubsystem::TrySwap()
{
	AFPSRGameState* GS = GetGS();
	if (!GS || GS->GetStageTransitionPhase() != EFPSRStageTransitionPhase::Pending)
	{
		return;
	}
	if (!CanSwapNow(GS->IsRunPaused()))
	{
		return; // the freeze that put us in Pending hasn't cleared yet
	}

	GS->SetStageTransition(EFPSRStageTransitionPhase::Swapping, 0.0f);
	PerformSwap();
}

void UFPSRStageDirectorSubsystem::PerformSwap()
{
	AFPSRGameState* GS = GetGS();
	UWorld* World = GetWorld();
	if (!GS || !World)
	{
		return;
	}

	// Unsubscribe now (safe even if we never bound — RemoveDynamic on an unbound delegate is a no-op). Leaving this
	// bound past the swap would double-react the next time Pending is entered and OnRunStateChanged fires again.
	if (bBoundRunStateChanged)
	{
		GS->OnRunStateChanged.RemoveDynamic(this, &UFPSRStageDirectorSubsystem::HandleRunStateChanged);
		bBoundRunStateChanged = false;
	}

	// 1. Current arena list (StageOrder order) + the currently-live arena's index within it.
	TArray<AFPSRArenaActor*> Arenas;
	AFPSRArenaActor::FindAllInWorld(World, Arenas);
	if (Arenas.Num() == 0)
	{
		UE_LOG(LogFPSR, Warning, TEXT("[StageDirector] Swap requested but no AFPSRArenaActor exists in this world — aborting."));
		GS->SetStageTransition(EFPSRStageTransitionPhase::None, 0.0f);
		return;
	}
	AFPSRArenaActor* Prev = AFPSRArenaActor::FindActiveInWorld(World);
	const int32 PrevIndex = FMath::Max(0, Arenas.IndexOfByKey(Prev));
	const int32 NextIndex = NextArenaIndex(PrevIndex, Arenas.Num());
	AFPSRArenaActor* Next = Arenas.IsValidIndex(NextIndex) ? Arenas[NextIndex] : Prev;
	// A single arena in the world cycles to ITSELF: the same skeleton with a freshly rolled seed IS the next stage
	// (new prop layout) — the intended single-arena behavior (see NextArenaIndex's header comment), not an
	// authoring gap, so this does NOT warn.

	const int32 NewStageIndex = GS->GetStageIndex() + 1;

	// 2. Activate the destination BEFORE regenerating it — AFPSRArenaActor::ServerRegenerate only publishes the new
	//    layout to the flow field (AdoptArenaSurface) when the arena is already the active one (17d6b320's gate);
	//    regenerating first would silently build the layout without ever handing it to the swarm.
	Next->SetArenaActive(true);

	// 3. Regenerate with the deterministic next-stage seed.
	Next->ServerRegenerate(ComputeStageSeed(Next->GetInitialSeed(), NewStageIndex));

	// 4. Teleport every living player pawn to the new arena's entry points (round-robin if there are more players
	//    than entries) and kill residual velocity so nobody carries a slide/knockback across the instantaneous swap.
	TArray<FTransform> EntryTransforms;
	if (!Next->GetPlayerEntryTransforms(EntryTransforms))
	{
		UE_LOG(LogFPSR, Warning, TEXT("[StageDirector] %s has no authored player entry points (APlayerStart) — using its fallback ring."),
			*Next->GetName());
	}
	int32 EntryCursor = 0;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		AFPSRCharacter* Char = PC ? Cast<AFPSRCharacter>(PC->GetPawn()) : nullptr;
		const AFPSRPlayerState* PS = PC ? PC->GetPlayerState<AFPSRPlayerState>() : nullptr;
		if (!Char || !PS || !PS->IsAlive() || EntryTransforms.Num() == 0)
		{
			continue; // DBNO/Dead players stay put (mirrors the level-up grant gate); no entries = nowhere to place
		}
		const FTransform& EntryXform = EntryTransforms[EntryCursor % EntryTransforms.Num()];
		++EntryCursor;

		Char->SetActorLocationAndRotation(EntryXform.GetLocation(), EntryXform.GetRotation(),
			/*bSweep*/false, nullptr, ETeleportType::TeleportPhysics);
		PC->SetControlRotation(EntryXform.Rotator());
		if (UCharacterMovementComponent* Move = Char->GetCharacterMovement())
		{
			Move->StopMovementImmediately();
		}
	}

	// 5. Deactivate the previous arena (a single-arena cycle skips this — Prev == Next there).
	if (Prev && Prev != Next)
	{
		Prev->SetArenaActive(false);
	}

	// 6. Release every surviving enemy back to the pool (ADR 0010 D6): "새 아레나 좌표로 재배치하지 않는다 —
	//    절차 배치와 곱해지면 적이 벽에서 튀어나온다." The old arena's leftover swarm has no valid standing room in
	//    the new arena's freshly rolled layout, so it goes back to the pool instead of being relocated into it.
	if (UFPSREnemySpawnSubsystem* SpawnSub = World->GetSubsystem<UFPSREnemySpawnSubsystem>())
	{
		SpawnSub->ReleaseAllEnemies();
	}

	// 7. Commit the new stage — every client follows purely from these replicated values (arena visibility toggle +
	//    the OnRunStateChanged re-broadcast in ApplyStageTransitionLocal), no dedicated RPC needed.
	GS->SetStageIndex(NewStageIndex);
	GS->SetActiveArena(Next);
	GS->SetStageTransition(EFPSRStageTransitionPhase::None, 0.0f);

	UE_LOG(LogFPSR, Log, TEXT("[StageDirector] Swap complete: %s -> %s (stage %d, seed %d)."),
		Prev ? *Prev->GetName() : TEXT("?"), *Next->GetName(), NewStageIndex, Next->GetActiveSeed());
}
