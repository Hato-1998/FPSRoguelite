// Copyright Epic Games, Inc. All Rights Reserved.

#include "Run/FPSRStageDirectorSubsystem.h"
#include "Run/FPSRRunScheduleDataAsset.h"
#include "Run/FPSRRunDirectorSubsystem.h"
#include "Arena/FPSRArenaActor.h"
#include "Arena/FPSRArenaStreamSubsystem.h"
#include "Enemy/FPSREnemySpawnSubsystem.h" // CarryEnemiesToNewStage (Phase A leftover-swarm carry-over)
#include "Hero/FPSRCharacter.h"
#include "Core/FPSRLogChannels.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "TimerManager.h"

void UFPSRStageDirectorSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	if (!HasServerAuthority())
	{
		return; // sublevel visibility is driven by the server; clients follow via the engine's streaming status RPCs
	}

	// ADR 0012 axis 5: park arena N+1 the moment stage N begins, and stage 1 begins here. Waiting for the first
	// suppressor to break would put AddToWorld back inside the transition window, which is the whole thing this
	// mechanism exists to avoid.
	//
	// The starting arena is read from UFPSRArenaStreamSubsystem's roster (bStartsActive) rather than from
	// GameState: the roster is built from level packages, which are guaranteed loaded by now (LoadMap flushes
	// always-loaded sublevels before InitializeActorsForPlay), whereas GameState's ActiveArena depends on actor
	// initialisation order that this callback deliberately does not assume.
	if (const UFPSRArenaStreamSubsystem* Stream = InWorld.GetSubsystem<UFPSRArenaStreamSubsystem>())
	{
		const int32 StartOrder = Stream->FindStartingStageOrder();
		if (StartOrder != INDEX_NONE)
		{
			ParkArenaAfter(StartOrder);
		}
	}
}

void UFPSRStageDirectorSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DealingTimerHandle);
		World->GetTimerManager().ClearTimer(SwapReadyTimerHandle);
		World->GetTimerManager().ClearTimer(FadeTimerHandle);
	}
	Super::Deinitialize();
}

int32 UFPSRStageDirectorSubsystem::GetCurrentStageOrder() const
{
	const AFPSRGameState* GS = GetGS();
	const AFPSRArenaActor* Arena = GS ? GS->GetActiveArena() : nullptr;
	return Arena ? Arena->GetStageOrder() : INDEX_NONE;
}

void UFPSRStageDirectorSubsystem::ParkArenaAfter(int32 StageOrder) const
{
	UWorld* World = GetWorld();
	UFPSRArenaStreamSubsystem* Stream = World ? World->GetSubsystem<UFPSRArenaStreamSubsystem>() : nullptr;
	if (!Stream)
	{
		return;
	}
	// RequestParkAfter, not RequestPark(GetNextStageOrder(...)): at world begin the successor's sublevel package
	// can still be async-loading, so "who comes next" is not answerable yet. Resolving it here once would read
	// as "there is no next arena" and never retry — and the run would cycle the first arena to itself, silently.
	Stream->RequestParkAfter(StageOrder);
}

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
	// Phase A phase split: FadeOut (was Swapping before the fade phases existed) — see the header comment.
	return bRunPaused ? EFPSRStageTransitionPhase::Pending : EFPSRStageTransitionPhase::FadeOut;
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
	if (NextPhase == EFPSRStageTransitionPhase::Pending)
	{
		// The freeze is still up (see above) — dealing has closed either way, so no end-timestamp left to carry.
		// HandleRunStateChanged (already bound since RequestTransition) now also branches on Pending and calls
		// TrySwap() the instant it clears. Nothing else to arm here.
		GS->SetStageTransition(EFPSRStageTransitionPhase::Pending, 0.0f);
		return;
	}

	// Phase A phase split: dealing closed and no freeze -> FadeOut (EnterFadeOut sets the phase itself, with the
	// real fade-length end-timestamp — no intermediate SetStageTransition needed here).
	EnterFadeOut();
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

	// A swap PerformSwap deferred because a card-selection freeze was up (see its guard): resume the moment the
	// freeze clears. Gated on bSwapDeferredByFreeze, not on the phase alone — OnRunStateChanged fires for many
	// unrelated reasons while Swapping is legitimately waiting on destination readiness, and an ungated BeginSwap
	// here would race the readiness poll into a SECOND PerformSwap (double stage-index commit). BeginSwap rather
	// than PerformSwap directly: it re-verifies destination readiness, which may have changed while frozen.
	if (Phase == EFPSRStageTransitionPhase::Swapping && bSwapDeferredByFreeze)
	{
		if (!GS->IsRunPaused())
		{
			bSwapDeferredByFreeze = false;
			BeginSwap();
		}
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
	// StagePhaseEndServerTime out by exactly that much so IsStageDealingOpen — which every client evaluates
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

	// Phase A phase split: Pending -> FadeOut (used to go straight to Swapping + BeginSwap).
	EnterFadeOut();
}

void UFPSRStageDirectorSubsystem::EnterFadeOut()
{
	AFPSRGameState* GS = GetGS();
	if (!GS)
	{
		return;
	}

	const float FadeSeconds = GetStageFadeOutSeconds();
	if (FadeSeconds <= 0.0f)
	{
		// 0-length fade: still publish the FadeOut phase (IsStageTransitionActive/replication stay consistent with
		// a real fade), but skip the timer and fall straight through to OnFadeOutComplete — the pre-Phase-A hard-cut
		// path (spec A3.2).
		GS->SetStageTransition(EFPSRStageTransitionPhase::FadeOut, 0.0f);
		OnFadeOutComplete();
		return;
	}

	GS->SetStageTransition(EFPSRStageTransitionPhase::FadeOut, GS->GetServerWorldTimeSeconds() + FadeSeconds);
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			FadeTimerHandle, this, &UFPSRStageDirectorSubsystem::OnFadeOutComplete, FadeSeconds, /*bLoop*/false);
	}
}

void UFPSRStageDirectorSubsystem::OnFadeOutComplete()
{
	AFPSRGameState* GS = GetGS();
	if (!GS)
	{
		return;
	}

	// Swapping now means "blacked out + destination activated/regenerated, waiting for/running the actual swap" —
	// see the header comment on EFPSRStageTransitionPhase::Swapping. The client fade holds alpha at 1.0 for the
	// WHOLE Swapping phase, so publishing it here (before the hold below) is what makes the hold read as blackout.
	GS->SetStageTransition(EFPSRStageTransitionPhase::Swapping, 0.0f);

	// Minimum full-blackout dwell (StageBlackoutHoldSeconds, user request 2026-08-20): without it the normal
	// destination-ready case swaps the very frame the fade-out finishes and the blackout reads as a flicker.
	// SERIALIZED before BeginSwap's readiness wait rather than run in parallel — the wait is 0 in the normal case
	// (the destination is parked a stage early), so parallelizing would only complicate the worst case, where a
	// slow client already stretches the blackout past the hold anyway. 0 = no dwell, pre-parameter behavior.
	const float HoldSeconds = GetStageBlackoutHoldSeconds();
	UWorld* World = GetWorld();
	if (HoldSeconds <= 0.0f || !World)
	{
		OnBlackoutHoldComplete();
		return;
	}
	World->GetTimerManager().SetTimer(
		FadeTimerHandle, this, &UFPSRStageDirectorSubsystem::OnBlackoutHoldComplete, HoldSeconds, /*bLoop*/false);
}

void UFPSRStageDirectorSubsystem::OnBlackoutHoldComplete()
{
	// BeginSwap (unchanged from before the phase split) either commits immediately (the normal ADR 0012 axis-5
	// case, destination already parked+visible) or polls until every connection can see the destination.
	BeginSwap();
}

float UFPSRStageDirectorSubsystem::GetStageBlackoutHoldSeconds() const
{
	const AFPSRGameState* GS = GetGS();
	const UFPSRRunScheduleDataAsset* Schedule = GS ? GS->GetRunSchedule() : nullptr;
	return Schedule ? Schedule->StageBlackoutHoldSeconds : DefaultStageBlackoutHoldSeconds;
}

float UFPSRStageDirectorSubsystem::GetStageFadeOutSeconds() const
{
	const AFPSRGameState* GS = GetGS();
	const UFPSRRunScheduleDataAsset* Schedule = GS ? GS->GetRunSchedule() : nullptr;
	return Schedule ? Schedule->StageFadeOutSeconds : DefaultStageFadeOutSeconds;
}

void UFPSRStageDirectorSubsystem::EnterFadeIn()
{
	AFPSRGameState* GS = GetGS();
	if (!GS)
	{
		return;
	}

	const float FadeSeconds = GetStageFadeInSeconds();
	if (FadeSeconds <= 0.0f)
	{
		GS->SetStageTransition(EFPSRStageTransitionPhase::FadeIn, 0.0f);
		OnFadeInComplete();
		return;
	}

	GS->SetStageTransition(EFPSRStageTransitionPhase::FadeIn, GS->GetServerWorldTimeSeconds() + FadeSeconds);
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			FadeTimerHandle, this, &UFPSRStageDirectorSubsystem::OnFadeInComplete, FadeSeconds, /*bLoop*/false);
	}
}

void UFPSRStageDirectorSubsystem::OnFadeInComplete()
{
	AFPSRGameState* GS = GetGS();
	if (!GS)
	{
		return;
	}

	// Post-swap grace, granted HERE (the exact unfreeze moment) rather than at PerformSwap — granting at the swap
	// would let the FadeIn length silently eat the window (enemies are frozen through FadeIn, so grace spent there
	// protects nothing). Carried enemies stand at their pre-transition RELATIVE positions — a melee enemy that was
	// at contact range when the suppressor broke is at contact range NOW, and everything unfreezes on this same
	// frame; without this window that enemy lands a zero-reaction-time hit the old release-everything design made
	// structurally impossible (merge-review finding C2). Same BeginGraceWindow mechanic as spawn/revive protection.
	const UFPSRRunScheduleDataAsset* Schedule = GS->GetRunSchedule();
	const float GraceSeconds = Schedule ? Schedule->StagePostSwapGraceSeconds : DefaultStagePostSwapGraceSeconds;
	if (GraceSeconds > 0.0f)
	{
		if (UWorld* World = GetWorld())
		{
			for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
			{
				const APlayerController* PC = It->Get();
				if (AFPSRCharacter* Char = PC ? Cast<AFPSRCharacter>(PC->GetPawn()) : nullptr)
				{
					Char->BeginGraceWindow(GraceSeconds);
				}
			}
		}
	}

	GS->SetStageTransition(EFPSRStageTransitionPhase::None, 0.0f);
}

float UFPSRStageDirectorSubsystem::GetStageFadeInSeconds() const
{
	const AFPSRGameState* GS = GetGS();
	const UFPSRRunScheduleDataAsset* Schedule = GS ? GS->GetRunSchedule() : nullptr;
	return Schedule ? Schedule->StageFadeInSeconds : DefaultStageFadeInSeconds;
}

void UFPSRStageDirectorSubsystem::BeginSwap()
{
	SwapReadyElapsed = 0.0f;
	if (TrySwapIfDestinationReady())
	{
		return; // the normal case — parking a stage early means the destination is already everywhere
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		PerformSwap();
		return;
	}

	TArray<FString> NotReady;
	if (const UFPSRArenaStreamSubsystem* Stream = World->GetSubsystem<UFPSRArenaStreamSubsystem>())
	{
		Stream->GetConnectionsNotReady(Stream->GetNextStageOrder(GetCurrentStageOrder()), NotReady);
	}
	UE_LOG(LogFPSR, Warning,
		TEXT("[StageDirector] Destination arena is not visible to %d connection(s) yet (%s) — holding the swap for up to %.1fs. The dealing window has already closed, so the reward was not extended."),
		NotReady.Num(), *FString::Join(NotReady, TEXT(", ")), GetSwapReadyTimeoutSeconds());

	World->GetTimerManager().SetTimer(
		SwapReadyTimerHandle, this, &UFPSRStageDirectorSubsystem::PollSwapReadiness, SwapReadyPollInterval, /*bLoop*/true);
}

bool UFPSRStageDirectorSubsystem::TrySwapIfDestinationReady()
{
	UWorld* World = GetWorld();
	const UFPSRArenaStreamSubsystem* Stream = World ? World->GetSubsystem<UFPSRArenaStreamSubsystem>() : nullptr;
	if (!Stream)
	{
		PerformSwap(); // no streaming subsystem (non-game world / teardown) — nothing to wait on
		return true;
	}

	// Which arena the swap will land on has to come from the ROSTER, not from AFPSRArenaActor::FindAllInWorld:
	// FindAllInWorld iterates the world, so an arena whose sublevel is not visible yet is invisible to it and the
	// cycle would silently return the CURRENT arena — i.e. exactly the case this gate exists to catch would read
	// as "ready".
	const int32 NextOrder = Stream->GetNextStageOrder(GetCurrentStageOrder());
	if (!Stream->IsReadyForEveryone(NextOrder))
	{
		return false;
	}

	if (UWorld* W = GetWorld())
	{
		W->GetTimerManager().ClearTimer(SwapReadyTimerHandle);
	}
	PerformSwap();
	return true;
}

void UFPSRStageDirectorSubsystem::PollSwapReadiness()
{
	SwapReadyElapsed += SwapReadyPollInterval;

	if (TrySwapIfDestinationReady())
	{
		return;
	}

	if (SwapReadyElapsed < GetSwapReadyTimeoutSeconds())
	{
		return;
	}

	// Give up waiting and swap anyway. A player whose client still has not made the level visible will see the new
	// arena pop in when their streaming catches up; standing frozen indefinitely is worse, and an unbounded hold
	// would hand one slow machine the power to stall everyone's run.
	TArray<FString> NotReady;
	if (const UFPSRArenaStreamSubsystem* Stream = GetWorld() ? GetWorld()->GetSubsystem<UFPSRArenaStreamSubsystem>() : nullptr)
	{
		Stream->GetConnectionsNotReady(Stream->GetNextStageOrder(GetCurrentStageOrder()), NotReady);
	}
	UE_LOG(LogFPSR, Error,
		TEXT("[StageDirector] Destination arena still not visible to %d connection(s) (%s) after %.1fs — swapping anyway. Those clients will see the arena appear late."),
		NotReady.Num(), *FString::Join(NotReady, TEXT(", ")), SwapReadyElapsed);

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SwapReadyTimerHandle);
	}
	PerformSwap();
}

float UFPSRStageDirectorSubsystem::GetSwapReadyTimeoutSeconds() const
{
	const AFPSRGameState* GS = GetGS();
	const UFPSRRunScheduleDataAsset* Schedule = GS ? GS->GetRunSchedule() : nullptr;
	return Schedule ? Schedule->StageSwapReadyTimeoutSeconds : DefaultSwapReadyTimeoutSeconds;
}

void UFPSRStageDirectorSubsystem::PerformSwap()
{
	AFPSRGameState* GS = GetGS();
	UWorld* World = GetWorld();
	if (!GS || !World)
	{
		return;
	}

	// FadeOut and Swapping's destination-ready wait both take real wall-clock time, and a freeze CAN land inside
	// them: since the dealing-window invulnerability was retired (2026-08-20) a kill during the fades pays XP
	// instantly (FPSRXPPickup's transition-collect path), and AddSharedXP -> RefreshPauseState raises the
	// card-selection freeze with NO transition gate. The two pause reasons need opposite reactions:
	//  - EndRun (bRunEnded latched): abort. Teleporting players, carrying the swarm and committing a stage index
	//    behind the result screen would all be wrong — and the run is over, so the lost transition is moot.
	//  - Card-selection freeze: HOLD, never abort — the suppressor is already consumed and nothing would ever call
	//    RequestTransition again, so aborting here silently strands the run on this stage forever (merge-review
	//    finding B1, the first version of this guard did exactly that). Stay in Swapping (the screen is already
	//    blacked out, which sits fine under the fullscreen card UI) and let HandleRunStateChanged's unpause edge
	//    re-enter BeginSwap — the same hold-then-resume contract Pending implements before the fades.
	if (GS->IsRunPaused())
	{
		if (GS->HasRunEnded())
		{
			GS->SetStageTransition(EFPSRStageTransitionPhase::None, 0.0f);
			UE_LOG(LogFPSR, Error, TEXT("[StageDirector] Swap aborted: the run has ended — not swapping behind the result screen."));
			return;
		}
		bSwapDeferredByFreeze = true;
		UE_LOG(LogFPSR, Log, TEXT("[StageDirector] Swap deferred: a card-selection freeze is up — holding the blackout until it clears."));
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
	//
	//    POSITION ONLY — XY from the entry point, Z preserved (current height + the arenas' plane delta), and the
	//    view/actor rotation NOT touched. The whole carry-over design (안 H) preserves each enemy's position
	//    RELATIVE to its player, which only reads as "the terrain changed around me" if the player's own facing
	//    and eye height survive the swap too: first live-fire PIE snapped the control rotation to the entry
	//    point's authored facing, and that turn — plus the entry's authored Z — was exactly the "튐" the user
	//    reported, enemies staying put notwithstanding. An entry point's authored ROTATION still matters where it
	//    always did (run-start spawns via GameMode); the mid-run swap deliberately ignores it.
	TArray<FTransform> EntryTransforms;
	if (!Next->GetPlayerEntryTransforms(EntryTransforms))
	{
		UE_LOG(LogFPSR, Warning, TEXT("[StageDirector] %s has no authored player entry points (APlayerStart) — using its fallback ring."),
			*Next->GetName());
	}
	// Phase A (step 4.5 below): pre/post-teleport locations, SAME index in both arrays, for CarryEnemiesToNewStage's
	// per-enemy delta. A pawn-less controller (no Char) or an arena with no entry transforms is skipped on BOTH
	// arrays below (never added to one without the other), so index i in OldPlayerLocs and NewPlayerLocs always
	// describes the SAME player's before/after — CarryEnemiesToNewStage relies on that pairing.
	TArray<FVector> OldPlayerLocs;
	TArray<FVector> NewPlayerLocs;
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

		const FVector OldLoc = Char->GetActorLocation();
		// Z: keep the pawn's own standing height, shifted by the two arenas' base-plane delta (0 while every arena
		// sits on the same Z plane — D2 is single-plane, but the delta keeps this correct if a future arena isn't).
		// Taking the entry point's authored Z instead is what made the first live-fire PIE read as a height pop:
		// a PlayerStart's Z is wherever it was dropped in the level, not this pawn's capsule-center convention.
		const float PlaneDeltaZ = (Prev && Prev != Next) ? (Next->GetActorLocation().Z - Prev->GetActorLocation().Z) : 0.0f;
		const FVector NewLoc(EntryXform.GetLocation().X, EntryXform.GetLocation().Y, OldLoc.Z + PlaneDeltaZ);

		OldPlayerLocs.Add(OldLoc); // BEFORE moving — this player's stage-A carry-over delta source
		NewPlayerLocs.Add(NewLoc);

		Char->SetActorLocation(NewLoc, /*bSweep*/false, nullptr, ETeleportType::TeleportPhysics);
		if (UCharacterMovementComponent* Move = Char->GetCharacterMovement())
		{
			Move->StopMovementImmediately();
		}
	}

	// 4.5. Carry the leftover swarm over to the new arena (Phase A — replaces the old ReleaseAllEnemies at step 6;
	//      see that step's comment for why release is no longer correct). Ordering invariant: AFTER the destination
	//      is regenerated + published to the flow field (step 3, AdoptArenaSurface — CarryEnemiesToNewStage's snap
	//      needs the LIVE grid the enemies are about to land in) and AFTER the player teleport (step 4, so the
	//      per-enemy delta has both the old and new player locations) — BEFORE the previous arena deactivates (step
	//      5, so nothing here needs the OLD arena's collision to still be up). The whole swarm is frozen for the
	//      entire transition (TickEnemyMovement's IsStageTransitionActive gate covers FadeOut/Swapping/FadeIn too),
	//      so there is no tick-order race between this and the movement pass.
	if (UFPSREnemySpawnSubsystem* SpawnSub = World->GetSubsystem<UFPSREnemySpawnSubsystem>())
	{
		const float CarryFraction = GS->GetRunSchedule() ? GS->GetRunSchedule()->StageCarryOverMaxFraction : 1.0f;
		SpawnSub->CarryEnemiesToNewStage(OldPlayerLocs, NewPlayerLocs, CarryFraction);
	}

	// 5. Deactivate the previous arena (a single-arena cycle skips this — Prev == Next there).
	if (Prev && Prev != Next)
	{
		Prev->SetArenaActive(false);
	}

	// 6. Cleanup that belongs to the OLD arena, not the new one — cancel whatever mission is still active (ADR 0010
	//    D6): breaking the suppressor that triggered this swap was the player's choice to leave this arena — the
	//    active mission's objective (spawn point, escort target, etc.) lives in the OLD arena, which loses its
	//    collision the moment step 5 above deactivates it, so the objective becomes physically unreachable. Left
	//    alone that is a SILENT failure: the mission just times out later with no obvious cause. Cancelling
	//    explicitly here makes the loss immediate and attributable to the swap instead. CancelActiveMission is a
	//    pure teardown (no reward grant, no "failed" log) — the mission simply no longer exists, matching neither a
	//    success nor a real failure.
	//    (Phase A: the leftover SWARM used to be released here too — "새 아레나 좌표로 재배치하지 않는다" — but user
	//    decision now carries it over instead (step 4.5, CarryEnemiesToNewStage), so there is nothing enemy-related
	//    left to do at this step.)
	if (UFPSRRunDirectorSubsystem* RunDirector = World->GetSubsystem<UFPSRRunDirectorSubsystem>())
	{
		if (RunDirector->CancelActiveMission())
		{
			UE_LOG(LogFPSR, Log, TEXT("[StageDirector] Active mission cancelled — its objective was in the arena being left."));
		}
	}

	// 7. Commit the new stage — every client follows purely from these replicated values (arena visibility toggle +
	//    the OnRunStateChanged re-broadcast in ApplyStageTransitionLocal), no dedicated RPC needed. Then enter
	//    FadeIn (Phase A phase split — used to go straight to None here; EnterFadeIn sets the phase itself).
	GS->SetStageIndex(NewStageIndex);
	GS->SetActiveArena(Next);
	EnterFadeIn();

	// GetPathName, not GetName: every arena's actor tends to share one object NAME ("FPSRArenaActor_0") because each
	// lives in its own sublevel namespace — a name-only line reads as a self-cycle when the swap actually crossed
	// arenas (first live-fire PIE did exactly that). The path carries the sublevel, which is the distinguishing part.
	UE_LOG(LogFPSR, Log, TEXT("[StageDirector] Swap complete: %s -> %s (stage %d, seed %d)."),
		Prev ? *Prev->GetPathName() : TEXT("?"), *Next->GetPathName(), NewStageIndex, Next->GetActiveSeed());

	// 8. Park the arena AFTER this one, now that this stage has begun (ADR 0012 axis 5). Done LAST so the park
	//    request cannot compete with the swap's own frame, and so GetCurrentStageOrder already reads the arena we
	//    just moved into. The previous arena stays loaded and visible-but-hidden — unloading it would give back
	//    the AddToWorld cost the moment the run cycles around to it again.
	ParkArenaAfter(Next->GetStageOrder());
}
