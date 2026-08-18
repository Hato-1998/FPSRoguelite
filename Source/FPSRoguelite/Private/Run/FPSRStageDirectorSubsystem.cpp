// Copyright Epic Games, Inc. All Rights Reserved.

#include "Run/FPSRStageDirectorSubsystem.h"
#include "Run/FPSRRunScheduleDataAsset.h"
#include "Run/FPSRRunDirectorSubsystem.h"
#include "Arena/FPSRArenaActor.h"
#include "Enemy/FPSREnemySpawnSubsystem.h"
#include "Hero/FPSRCharacter.h"
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

	// F4: a stage swap is not supported mid-boss. Destructibles (suppressors included) sit outside the transition
	// invulnerability gate on purpose and stay breakable during the boss fight, but the boss itself is neither an
	// AFPSREnemyBase (so ReleaseAllEnemies never touches it) nor teleported by PerformSwap's player-move step — a
	// swap during Boss phase would strand it alone in the old arena with its collision switched off, and the run
	// could never end (no path to victory or defeat). Reject and let the suppressor stay broken but inert rather
	// than run a transition with no way to finish it.
	if (GS->GetRunPhase() != ERunPhase::Combat)
	{
		UE_LOG(LogFPSR, Log, TEXT("[StageDirector] Transition requested during Boss phase — ignored (arena swap is not supported mid-boss)."));
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

	// F3: bind from the MOMENT Grace starts, not just once a still-open card freeze pushes the transition into
	// Pending (the old bind point). A level-up card freeze can land AT ANY POINT during the dealing window — killing
	// the frozen swarm for guaranteed XP is basically the window's intended use, and that XP can trigger a level-up
	// mid-window — and HandleRunStateChanged (below) now needs to see every freeze edge WHILE Grace is still active
	// to pause/resume DealingTimerHandle. Unbind stays in PerformSwap, so one subscription now spans the transition's
	// whole Grace->Pending->Swapping lifetime instead of only Pending->Swapping.
	if (!bBoundRunStateChanged)
	{
		GS->OnRunStateChanged.AddDynamic(this, &UFPSRStageDirectorSubsystem::HandleRunStateChanged);
		bBoundRunStateChanged = true;
	}
	// Seed the freeze-edge tracker from the live value (mirrors UFPSRFlowFieldSubsystem::TryBindRunStateHandler) so
	// a freeze already up the instant Grace starts reads as "no change" rather than a phantom edge on the next
	// broadcast. In practice bRunPaused should already be false here (breaking the suppressor that called this
	// requires firing, which the card freeze itself blocks), but seeding costs nothing and this is not the place to
	// assume that invariant can never change.
	bWasRunPausedForDealing = GS->IsRunPaused();

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

	// F3: DealingTimerHandle is now paused for the full duration of any card freeze that lands while Grace is
	// active (HandleRunStateChanged, bound since RequestTransition), so this callback can only fire while the
	// timer was actually counting down — i.e. GS->IsRunPaused() should always read false here; a freeze arriving
	// mid-window would have paused the timer before it could reach this callback at all. DecidePhaseAfterDealing
	// is kept as the documented fallback for the one freeze source that ISN'T a dealing-window card pick: the RUN
	// ending (EndRunFreeze) pins bRunPaused permanently, and Pending correctly holds rather than swapping behind
	// the result screen.
	const EFPSRStageTransitionPhase NextPhase = DecidePhaseAfterDealing(GS->IsRunPaused());
	GS->SetStageTransition(NextPhase, 0.0f); // dealing has closed either way — no end-timestamp left to carry

	if (NextPhase == EFPSRStageTransitionPhase::Pending)
	{
		// The freeze is still up (see above) — HandleRunStateChanged (already bound since RequestTransition) now
		// also branches on Pending and calls TrySwap() the instant it clears. Nothing else to arm here.
		return;
	}

	PerformSwap();
}

void UFPSRStageDirectorSubsystem::HandleRunStateChanged()
{
	AFPSRGameState* GS = GetGS();
	if (!GS)
	{
		return;
	}

	const EFPSRStageTransitionPhase Phase = GS->GetStageTransitionPhase();

	if (Phase == EFPSRStageTransitionPhase::Pending)
	{
		TrySwap();
		return;
	}

	if (Phase != EFPSRStageTransitionPhase::Grace)
	{
		return; // OnRunStateChanged fires for many unrelated reasons — only act while Grace or Pending
	}

	// F3: invariant 8 promises a FIXED amount of PLAYER-USABLE dealing time, not fixed wall-clock time. The
	// card-selection freeze blocks firing (the dealing window's only reward channel — unlike the Grace freeze
	// itself, which deliberately leaves firing live), so every second spent behind the card screen is a second the
	// window could not have been used at all and must not count against it.
	const bool bNowPaused = GS->IsRunPaused();
	if (bNowPaused == bWasRunPausedForDealing)
	{
		return; // no edge — OnRunStateChanged fires for many unrelated reasons (run clock, mission progress, ...)
	}

	// Update the edge tracker BEFORE touching the timer or GameState below: SetStageTransition re-broadcasts
	// OnRunStateChanged synchronously (ApplyStageTransitionLocal), which re-enters this same function from inside
	// this call. Flipping the tracker first is what makes that reentrant call see "no change" and return above,
	// instead of pausing/unpausing (or reading a stale GetTimerRemaining) a second time for one real edge.
	bWasRunPausedForDealing = bNowPaused;

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	FTimerManager& TimerManager = World->GetTimerManager();

	if (bNowPaused)
	{
		TimerManager.PauseTimer(DealingTimerHandle);
		return;
	}

	// Unpause edge: read the remaining time BEFORE UnPauseTimer, while the timer is still definitely Paused
	// (FTimerManager stores the frozen remaining time directly in that state — see TimerManager.cpp), then push
	// GraceDealingEndServerTime out by exactly that much so IsStageDealingOpen — which every client evaluates
	// against this SAME replicated timestamp, not a local timer — still closes at the right FIXED distance from
	// now. One more replication, same mechanism as any other SetStageTransition call.
	const float Remaining = TimerManager.GetTimerRemaining(DealingTimerHandle);
	TimerManager.UnPauseTimer(DealingTimerHandle);
	GS->SetStageTransition(EFPSRStageTransitionPhase::Grace, GS->GetServerWorldTimeSeconds() + Remaining);
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
	//    regenerating first would silently build the layout without ever handing it to the swarm. This order is
	//    also what F2 needs: SetArenaActive(true) resets every AFPSRArenaDestructible in Next's grid back to intact
	//    BEFORE step 3 (re)generates and publishes this stage's mask/flow field — so nothing in the arena is ever
	//    generated-as-blocked while still visually/collision-wise sitting in a broken state left over from Next's
	//    previous activation. "Activate/restore" and "generate for this stage" stay two cleanly ordered phases
	//    rather than interleaved, which matters even though today's Generate() does not itself inspect bBroken (it
	//    always rasterises a found AFPSRArenaDestructible's authored footprint regardless) — reversing the order
	//    would still leave that reasoning silently depending on Generate() never changing that.
	Next->SetArenaActive(true);

	// 3. Regenerate with the deterministic next-stage seed. False means Params were missing/invalid (ADR 0010
	//    invariant 5's fail-fast — see AFPSRArenaActor::ServerRegenerate) and Next is now ACTIVE with nothing valid
	//    published to the flow field. F5: abort the WHOLE swap rather than press on — teleporting players onto a
	//    bad/nonexistent layout, releasing the old swarm, and committing the new stage index would all be
	//    committing to an arena that never actually became playable.
	const int32 NextSeed = ComputeStageSeed(Next->GetInitialSeed(), NewStageIndex);
	if (!Next->ServerRegenerate(NextSeed))
	{
		// Undo step 2 — but ONLY if Next isn't also Prev (the self-cycle case): Prev must be left exactly as it
		// was (still active — it is never deactivated on the success path either until step 5, below, which this
		// return skips), so flipping it inactive here would strand players in an arena that just went solid.
		if (Next != Prev)
		{
			Next->SetArenaActive(false);
		}
		GS->SetStageTransition(EFPSRStageTransitionPhase::None, 0.0f);
		UE_LOG(LogFPSR, Error, TEXT("[StageDirector] Swap aborted: %s failed to regenerate (seed %d) — staying on %s."),
			*Next->GetName(), NextSeed, Prev ? *Prev->GetName() : TEXT("?"));
		return;
	}

	// 4. Teleport every player pawn that exists to the new arena's entry points (round-robin if there are more
	//    players than entries) and kill residual velocity so nobody carries a slide/knockback across the
	//    instantaneous swap.
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
		// F8: gate on "does this controller have a pawn", NOT PS->IsAlive(). EFPSRLifeState has THREE values — a
		// DBNO player still has a live, crawling pawn (see AFPSRCharacter::HandleOutOfHealth) and MUST come along:
		// step 5 below turns off the PREVIOUS arena's floor collision, and that arena is an actor whose own HISM
		// carries the floor, so a DBNO pawn left behind falls through it the instant collision goes away — deleting
		// the revive window at exactly the moment (the heaviest pre-transition fighting) it is most likely needed.
		// Dead is spectator-only (no controlled pawn, or one nobody is piloting) and falls out of this check for
		// free with no need to consult LifeState at all. The old comment here ("mirrors the level-up grant gate")
		// was WRONG: AddSharedXP/RefreshPauseState withhold a card-pick REWARD from a non-Alive player, which is
		// not the same thing as removing the ground out from under them — that gate has nothing to say about a
		// floor teleport.
		if (!Char || EntryTransforms.Num() == 0)
		{
			continue; // no pawn to move, or nowhere to put it
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

	// 6. Cleanup that belongs to the OLD arena, not the new one — cancel whatever mission is still active, then
	//    release every surviving enemy back to the pool (ADR 0010 D6):
	//     - Mission: breaking the suppressor that triggered this swap was the player's choice to leave this arena
	//       — the active mission's objective (spawn point, escort target, etc.) lives in the OLD arena, which
	//       loses its collision the moment step 5 above deactivates it, so the objective becomes physically
	//       unreachable. Left alone that is a SILENT failure: the mission just times out later with no obvious
	//       cause. Cancelling explicitly here makes the loss immediate and attributable to the swap instead.
	//       CancelActiveMission is a pure teardown (no reward grant, no "failed" log) — the mission simply no
	//       longer exists, matching neither a success nor a real failure.
	//     - Enemies: "새 아레나 좌표로 재배치하지 않는다 — 절차 배치와 곱해지면 적이 벽에서 튀어나온다." The old
	//       arena's leftover swarm has no valid standing room in the new arena's freshly rolled layout, so it goes
	//       back to the pool instead of being relocated into it.
	if (UFPSRRunDirectorSubsystem* RunDirector = World->GetSubsystem<UFPSRRunDirectorSubsystem>())
	{
		if (RunDirector->CancelActiveMission())
		{
			UE_LOG(LogFPSR, Log, TEXT("[StageDirector] Active mission cancelled — its objective was in the arena being left."));
		}
	}
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
