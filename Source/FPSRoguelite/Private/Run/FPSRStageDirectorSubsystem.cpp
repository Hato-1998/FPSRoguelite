// Copyright Epic Games, Inc. All Rights Reserved.

#include "Run/FPSRStageDirectorSubsystem.h"
#include "Run/FPSRRunScheduleDataAsset.h"
#include "Run/FPSRRunDirectorSubsystem.h"
#include "Arena/FPSRArenaActor.h"
#include "Arena/FPSRArenaStreamSubsystem.h"
#include "Enemy/FPSREnemySpawnSubsystem.h" // CarryEnemiesToNewStage (Phase A leftover-swarm carry-over)
#include "Pickup/FPSRPickupSubsystem.h" // CarryPickupsToNewStage (dealing-window XP gem carry-over, ADR 0010 D6)
#include "Weapon/FPSRProjectileSubsystem.h" // ReleaseEnemyProjectiles (전환 시작 시 탄막 제거, ADR 0010 D6)
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
	PendingDestinationStageOrder = INDEX_NONE;
	Super::Deinitialize();
}

int32 UFPSRStageDirectorSubsystem::GetCurrentStageOrder() const
{
	const AFPSRGameState* GS = GetGS();
	const AFPSRArenaActor* Arena = GS ? GS->GetActiveArena() : nullptr;
	return Arena ? Arena->GetStageOrder() : INDEX_NONE;
}

int32 UFPSRStageDirectorSubsystem::GetDestinationStageOrder() const
{
	if (PendingDestinationStageOrder != INDEX_NONE)
	{
		return PendingDestinationStageOrder; // a named destination (the boss transition) — never cycled to
	}
	const UWorld* World = GetWorld();
	const UFPSRArenaStreamSubsystem* Stream = World ? World->GetSubsystem<UFPSRArenaStreamSubsystem>() : nullptr;
	return Stream ? Stream->GetNextStageOrder(GetCurrentStageOrder()) : INDEX_NONE;
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

int32 UFPSRStageDirectorSubsystem::NextCombatArenaIndex(int32 CurrentIndex, TConstArrayView<EFPSRArenaRole> Roles)
{
	const int32 Count = Roles.Num();
	if (Count <= 0)
	{
		return INDEX_NONE;
	}
	// Built ON NextArenaIndex rather than re-deriving the modulo so the cycle rule stays in ONE place: this is
	// "keep taking the next index until one is Combat", nothing more. A full lap (Count steps) ends back at
	// CurrentIndex, which is what makes the lone-combat-arena self-cycle fall out for free.
	int32 Candidate = CurrentIndex;
	for (int32 Step = 0; Step < Count; ++Step)
	{
		Candidate = NextArenaIndex(Candidate, Count);
		if (Roles.IsValidIndex(Candidate) && Roles[Candidate] == EFPSRArenaRole::Combat)
		{
			return Candidate;
		}
	}
	return INDEX_NONE;
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
	// The suppressor path: no named destination, so BeginTransition cycles to the next COMBAT arena. The return
	// value is deliberately dropped — a rejected suppressor break has nothing to retry (the guards that reject it
	// are "already transitioning" and "mid-boss", neither of which the suppressor can do anything about).
	BeginTransition(INDEX_NONE);
}

bool UFPSRStageDirectorSubsystem::RequestBossTransition(int32 BossStageOrder)
{
	if (BossStageOrder == INDEX_NONE)
	{
		return false; // the caller could not find a boss arena — nothing to start, and it will say so itself
	}

	// 목적지가 정말 보스 아레나인지 **여기서** 확인한다. BeginTransition 은 StageOrder 로만 슬롯을 집고 역할을
	// 보지 않으므로, StageOrder 가 중복이면(=아레나 레벨을 복제하면 기본으로 그렇게 된다 —
	// `Docs/BossStage_ContentGuide.md` §4 가 "복제본이라 지금은 같은 값일 것"이라고 적는다) 보스 전환이
	// 조용히 **전투 아레나**에 착지한다. 그러면 IsInBossArena 가 계속 false 라 게이트가 매 틱 재요청하고,
	// 파티는 보스 없는 전환을 반복해서 본다. 억제기 순환은 인덱스 순환이라 이만큼 취약하지 않았다 —
	// 이 취약성은 역할 기반 라우팅이 새로 만든 것이다. (레드팀 게이트 2026-08-29)
	if (const UWorld* World = GetWorld())
	{
		if (const UFPSRArenaStreamSubsystem* Stream = World->GetSubsystem<UFPSRArenaStreamSubsystem>())
		{
			FFPSRArenaSlot DestSlot;
			if (Stream->FindSlot(BossStageOrder, DestSlot) && DestSlot.Role != EFPSRArenaRole::Boss)
			{
				UE_LOG(LogFPSR, Error,
					TEXT("[StageDirector] Boss transition to stage order %d refused — that roster slot is a %s arena, not the boss arena. StageOrder must be UNIQUE across arena sublevels; a duplicated arena level keeps the original's value. Give the boss arena its own '스테이지 순서'."),
					BossStageOrder, (DestSlot.Role == EFPSRArenaRole::Combat) ? TEXT("combat") : TEXT("non-boss"));
				return false;
			}
		}
	}

	return BeginTransition(BossStageOrder);
}

bool UFPSRStageDirectorSubsystem::BeginTransition(int32 DestinationStageOrder)
{
	if (!HasServerAuthority())
	{
		return false;
	}
	AFPSRGameState* GS = GetGS();
	if (!GS)
	{
		return false;
	}
	// P2-1 (merge-gate 교정): hoisted from the old wrap-block further down so the arena-existence guard a few lines
	// below (right after the boss-phase guard) can use it BEFORE this function commits to Grace / cancels the active
	// mission — see that guard's comment for why. The 3 cleanup calls + timer set further down keep using this World.
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	// A transition is already running: several suppressors can exist in one arena, or one explosion can finish
	// more than one at once — only the FIRST request may start the state machine, the rest are silently ignored.
	// This is also what keeps a suppressor break and the BossTime transition from racing: whichever lands first
	// runs, and the run director simply asks again on its next tick.
	if (GS->GetStageTransitionPhase() != EFPSRStageTransitionPhase::None)
	{
		return false;
	}

	// F4: a stage swap is not supported mid-boss. Destructibles (suppressors included) sit outside the transition
	// invulnerability gate on purpose and stay breakable during the boss fight, but the boss itself is neither an
	// AFPSREnemyBase (so ReleaseAllEnemies never touches it) nor teleported by PerformSwap's player-move step — a
	// swap during Boss phase would strand it alone in the old arena with its collision switched off, and the run
	// could never end (no path to victory or defeat). Reject and let the suppressor stay broken but inert rather
	// than run a transition with no way to finish it.
	//
	// This guard is ALSO what makes the BossTime transition safe (보스 스테이지, 2026-08-28): the run director
	// requests that swap while still in Combat phase and only calls EnterBoss (which sets ERunPhase::Boss) once the
	// swap has landed, so the boss never exists while a transition could strand it.
	if (GS->GetRunPhase() != ERunPhase::Combat)
	{
		UE_LOG(LogFPSR, Log, TEXT("[StageDirector] Transition requested during Boss phase — ignored (arena swap is not supported mid-boss)."));
		return false;
	}

	// P2-1 (merge-gate 교정): same reason as the boss-phase guard just above — a transition that CANNOT finish once
	// started must never start. PerformSwap already checks this (Arenas.Num() == 0 -> abort), but by then Grace has
	// already been entered and the active mission cancelled below — the player would be left with no mission AND no
	// swap. Checking here, before either of those happens, removes that combination instead of only detecting it late.
	TArray<AFPSRArenaActor*> Arenas;
	AFPSRArenaActor::FindAllInWorld(World, Arenas);
	if (Arenas.Num() == 0)
	{
		UE_LOG(LogFPSR, Warning, TEXT("[StageDirector] Transition requested but no AFPSRArenaActor exists in this world — ignored."));
		return false;
	}

	// Same "a transition that CANNOT finish must never start" rule, applied to the NAMED-destination path: verify
	// the arena actually exists in the streaming roster BEFORE committing to Grace. Rejecting here (rather than
	// aborting after the fade) is what lets the run director keep asking while the boss arena's package finishes
	// loading, instead of burning a transition — and the player never sees a fade that goes nowhere.
	if (DestinationStageOrder != INDEX_NONE)
	{
		const UFPSRArenaStreamSubsystem* Stream = World->GetSubsystem<UFPSRArenaStreamSubsystem>();
		FFPSRArenaSlot DestSlot;
		if (!Stream || !Stream->FindSlot(DestinationStageOrder, DestSlot))
		{
			UE_LOG(LogFPSR, Warning,
				TEXT("[StageDirector] Transition to stage order %d requested but no such arena is in the roster yet — ignored (the caller retries)."),
				DestinationStageOrder);
			return false;
		}
	}
	PendingDestinationStageOrder = DestinationStageOrder;

	const float GraceSeconds = GS->GetRunSchedule() ? GS->GetRunSchedule()->StageGraceSeconds : DefaultStageGraceSeconds;
	const float DealingEnd = GS->GetServerWorldTimeSeconds() + GraceSeconds;
	GS->SetStageTransition(EFPSRStageTransitionPhase::Grace, DealingEnd);

	{
		// Cancel whatever mission is still active RIGHT NOW (moved here from PerformSwap's old step 6 — user decision
		// 2026-08-25). Breaking the suppressor that called RequestTransition is the PLAYER'S decision to leave this
		// arena, and the objective is forfeit from that exact instant — THAT is the reason for cancelling this early,
		// NOT a promise that this transition will run to completion (merge-gate P2-1 교정: it does not always — see
		// below). The active mission's objective (spawn point, escort target, etc.) lives in the arena being left;
		// leaving its UI up for the whole transition would read as "still in progress" when it no longer is. The
		// reason this cancel used to live in PerformSwap's step 6 is still TRUE, just a LATER fact about the same
		// arena rather than the trigger for acting: PerformSwap's step 5 switches the old arena's collision off,
		// which makes an in-progress objective physically unreachable — left alone that is a SILENT failure, timing
		// out later with no obvious cause. Cancelling here simply gets ahead of that fact instead of racing it.
		// CancelActiveMission is a pure teardown (no reward grant, no "failed" log) — the mission simply no longer
		// exists, matching neither a success nor a real failure.
		//
		// What actually justifies acting here: the guards above (redundant request / mid-boss / — as of this same
		// fix — no AFPSRArenaActor in the world at all) reject exactly the requests that could never finish once
		// started; everything that gets past them is safe to cancel early. ⚠️ One path still slips through:
		// PerformSwap's Next->ServerRegenerate(NextSeed) can still fail — destination gen-params validity is judged
		// INSIDE that call, not knowable here — and that abort leaves the mission already cancelled while the player
		// stays on the OLD arena. That is an authoring-error path (the kind ADR 0011 E4's validator is meant to catch
		// at world start), accepted rather than fixed here, but PerformSwap's own ServerRegenerate-failure log now
		// says so explicitly, so it is never a silent one.
		if (UFPSRRunDirectorSubsystem* RunDirector = World->GetSubsystem<UFPSRRunDirectorSubsystem>())
		{
			if (RunDirector->CancelActiveMission())
			{
				UE_LOG(LogFPSR, Log, TEXT("[StageDirector] Active mission cancelled — its objective was in the arena being left."));
			}
		}

		// Clear the enemy bullets already in flight (사용자 결정 2026-08-25, PIE). They used to simply FREEZE for the
		// window alongside the swarm (UFPSRProjectileSubsystem's bPausedEnemyOnly edge), which read badly in play:
		// live bullets hung motionless in the air for the whole ~10s transition, and they were still aimed at where
		// the player STOOD — a position PerformSwap is about to teleport them out of, into a different arena. Clearing
		// them is the "탄막 제거" read instead: the dealing window opens on a clean screen. Placed with the mission
		// cancel above for the same reason — after both reject guards, so a mid-boss request that gets ignored does
		// not wipe the screen as a side effect. PLAYER projectiles are deliberately untouched: firing through the
		// window is its whole reward (안 G).
		if (UFPSRProjectileSubsystem* ProjectileSub = World->GetSubsystem<UFPSRProjectileSubsystem>())
		{
			ProjectileSub->ReleaseEnemyProjectiles();
		}

		// Cancel the ranged charges already in progress (사용자 결정 2026-08-26, PIE). Clearing the bullets above is
		// only half of it: an enemy mid-charge has a Reliable directional WARNING up on its target's HUD, and the
		// transition freezes the whole attack pass (UFPSREnemySpawnSubsystem early-returns on
		// IsStageTransitionActive), so that enemy never re-enters ServerTickAttack to close its own hold — the
		// warning stays on screen for the entire transition. The usual teardown paths do not save us here either:
		// they run when an enemy is DESTROYED, and a transition CARRIES enemies over instead. Cancelling also rewinds
		// the charge cycle, so nothing fires without a fresh telegraph on the other side of the swap.
		if (UFPSREnemySpawnSubsystem* SpawnSub = World->GetSubsystem<UFPSREnemySpawnSubsystem>())
		{
			SpawnSub->CancelRangedChargesForTransition();
		}

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

	UE_LOG(LogFPSR, Log, TEXT("[StageDirector] Transition requested (destination %s) — dealing window %.1fs."),
		DestinationStageOrder == INDEX_NONE
			? TEXT("= next combat arena")
			: *FString::Printf(TEXT("= stage order %d"), DestinationStageOrder),
		GraceSeconds);
	return true;
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
		Stream->GetConnectionsNotReady(GetDestinationStageOrder(), NotReady);
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
	// as "ready". GetDestinationStageOrder folds in the boss transition's NAMED destination, so this gate waits on
	// the arena the swap is actually headed for rather than on the one the cycle would have picked.
	const int32 NextOrder = GetDestinationStageOrder();
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
		Stream->GetConnectionsNotReady(GetDestinationStageOrder(), NotReady);
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
	// them: since the dealing-window invulnerability was retired (2026-08-20) a kill during the fades can still
	// grant XP with no transition gate — not through the old dealing-window instant-collect path (merge-gate P3
	// 교정: this branch deleted that path along with the swarm release it used to ride), but through
	// UFPSRPickupSubsystem::SpawnXPPickup's over-cap branch, which calls AddSharedXP directly (no gem spawned, no
	// transition check either) once ActivePickups is already at MaxActivePickups — and AddSharedXP ->
	// RefreshPauseState raises the card-selection freeze. The two pause reasons need opposite reactions:
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
			PendingDestinationStageOrder = INDEX_NONE;
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
		PendingDestinationStageOrder = INDEX_NONE;
		return;
	}
	AFPSRArenaActor* Prev = AFPSRArenaActor::FindActiveInWorld(World);
	const int32 PrevIndex = FMath::Max(0, Arenas.IndexOfByKey(Prev));

	AFPSRArenaActor* Next = nullptr;
	if (PendingDestinationStageOrder != INDEX_NONE)
	{
		// Named destination (the BossTime transition). Look it up by StageOrder rather than cycling — and if it is
		// NOT in the world, ABORT instead of falling back to the cycle. Falling back would silently send the party
		// to an ordinary combat arena and then spawn the boss there on the run director's next tick, which reads as
		// "the boss stage didn't work" with nothing in the log to say why. BeginTransition already refused to start
		// without a roster entry and BeginSwap already waited for visibility, so reaching this is a real fault.
		const int32 WantOrder = PendingDestinationStageOrder;
		AFPSRArenaActor* const* Found = Arenas.FindByPredicate([WantOrder](const AFPSRArenaActor* A)
			{ return A && A->GetStageOrder() == WantOrder; });
		Next = Found ? *Found : nullptr;
		if (!Next)
		{
			UE_LOG(LogFPSR, Error,
				TEXT("[StageDirector] Swap aborted: destination arena (stage order %d) is not in the world — staying on %s. The active mission was already cancelled at transition start and is NOT restored."),
				PendingDestinationStageOrder, Prev ? *Prev->GetPathName() : TEXT("?"));
			GS->SetStageTransition(EFPSRStageTransitionPhase::None, 0.0f);
			PendingDestinationStageOrder = INDEX_NONE;
			return;
		}
	}
	else
	{
		// The suppressor cycle. Boss arenas are SKIPPED (보스 스테이지, 2026-08-28) — that stage carries no
		// suppressor by design, so cycling into it early would strand the party there with no boss and no way out.
		TArray<EFPSRArenaRole> Roles;
		Roles.Reserve(Arenas.Num());
		for (const AFPSRArenaActor* A : Arenas)
		{
			Roles.Add(A ? A->GetArenaRole() : EFPSRArenaRole::Combat);
		}
		const int32 NextIndex = NextCombatArenaIndex(PrevIndex, Roles);
		// A single COMBAT arena cycles to ITSELF: the same skeleton with a freshly rolled seed IS the next stage
		// (new prop layout) — the intended single-arena behavior (see NextCombatArenaIndex's header comment), not an
		// authoring gap, so this does NOT warn. INDEX_NONE means every arena in the world is a boss arena, which IS
		// an authoring fault: stay put rather than swap into the boss stage.
		Next = Arenas.IsValidIndex(NextIndex) ? Arenas[NextIndex] : nullptr;
		if (!Next)
		{
			UE_LOG(LogFPSR, Error,
				TEXT("[StageDirector] Swap aborted: this world has no COMBAT arena to cycle to (every AFPSRArenaActor is authored as a boss arena) — staying on %s."),
				Prev ? *Prev->GetPathName() : TEXT("?"));
			GS->SetStageTransition(EFPSRStageTransitionPhase::None, 0.0f);
			return;
		}
	}

	// The named destination has been CONSUMED — clear it here, once Next is resolved, so every path from this point
	// (success and both aborts below) leaves it clean with one assignment instead of three. Deliberately after the
	// freeze-defer return above, which re-enters PerformSwap later and still needs the destination.
	PendingDestinationStageOrder = INDEX_NONE;

	const int32 NewStageIndex = GS->GetStageIndex() + 1;

	// 2. Activate the destination BEFORE regenerating it — AFPSRArenaActor::ServerRegenerate only publishes the new
	//    layout to the flow field (AdoptArenaField, 구 AdoptArenaSurface) when the arena is already the active one (17d6b320's gate);
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
		UE_LOG(LogFPSR, Error, TEXT("[StageDirector] Swap aborted: %s failed to regenerate (seed %d) — staying on %s. The active mission was already cancelled at transition start and is NOT restored."),
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
		// Round-robin the authored entries, and SPREAD each wrap onto its own ring slot. Without the spread, an arena
		// with fewer entry points than players teleports them onto the SAME point — and the teleport below is
		// deliberately bSweep=false (a sweep does not resolve an overlap anyway), so the capsules end up inside each
		// other and nobody can move. That is not hypothetical: every arena currently ships exactly ONE APlayerStart,
		// so in co-op EVERY player landed on one spot and stuck (PIE 2026-08-25). Authoring one start per player is
		// still the right content fix; this makes the code safe whatever gets authored.
		const int32 EntryIndex = EntryCursor % EntryTransforms.Num();
		const int32 WrapIndex = EntryCursor / EntryTransforms.Num();
		FTransform EntryXform = EntryTransforms[EntryIndex];
		if (WrapIndex > 0)
		{
			// Fan the overflow around its entry point. The angle is derived from the player's own index so two
			// players can never resolve to the same offset, and the radius grows per wrap so a third wrap clears the
			// second. Not mask-snapped: GetPlayerEntryTransforms already snapped the entry itself onto an open cell,
			// and PlayerEntryOverflowRadius is a fraction of a cell so the offset cannot cross into a blocked one.
			const float Angle = (2.0f * PI) * (static_cast<float>(EntryCursor) / static_cast<float>(FMath::Max(1, EntryTransforms.Num() * (WrapIndex + 1))));
			const float Radius = PlayerEntryOverflowRadius * static_cast<float>(WrapIndex);
			EntryXform.AddToTranslation(FVector(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 0.0f));
		}
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
	//      is regenerated + published to the flow field (step 3, AdoptArenaField — CarryEnemiesToNewStage's snap
	//      needs the LIVE grid the enemies are about to land in) and AFTER the player teleport (step 4, so the
	//      per-enemy delta has both the old and new player locations) — BEFORE the previous arena deactivates (step
	//      5, so nothing here needs the OLD arena's collision to still be up). The whole swarm is frozen for the
	//      entire transition (TickEnemyMovement's IsStageTransitionActive gate covers FadeOut/Swapping/FadeIn too),
	//      so there is no tick-order race between this and the movement pass. XP gems ride the same delta right
	//      after (UFPSRPickupSubsystem::CarryPickupsToNewStage, immediately below) — the dealing-window instant
	//      collect this used to feed is gone (사용자 결정 2026-08-25), so gems now have to make this same trip too.
	if (UFPSREnemySpawnSubsystem* SpawnSub = World->GetSubsystem<UFPSREnemySpawnSubsystem>())
	{
		const float CarryFraction = GS->GetRunSchedule() ? GS->GetRunSchedule()->StageCarryOverMaxFraction : 1.0f;
		SpawnSub->CarryEnemiesToNewStage(OldPlayerLocs, NewPlayerLocs, CarryFraction);
	}

	// Same ordering invariant as the swarm carry-over just above: after the destination is regenerated + published
	// to the flow field (step 3) and after the player teleport (step 4), but before the previous arena deactivates
	// (step 5) — CarryPickupsToNewStage's own flow-field snap needs the new arena's live grid, and its per-gem
	// delta needs both the old and new player locations, same as the swarm's.
	if (UFPSRPickupSubsystem* PickupSub = World->GetSubsystem<UFPSRPickupSubsystem>())
	{
		PickupSub->CarryPickupsToNewStage(OldPlayerLocs, NewPlayerLocs);
	}

	// 5. Deactivate the previous arena (a single-arena cycle skips this — Prev == Next there).
	if (Prev && Prev != Next)
	{
		Prev->SetArenaActive(false);
	}

	// 6. 이 단계에 있던 미션 취소는 RequestTransition 으로 앞당겨졌다(사용자 결정 2026-08-25) — 미션 UI가 전환이
	//    "끝난 뒤"가 아니라 "시작하는 즉시" 사라지도록 하기 위해서다. 취소 로직과 근거는 RequestTransition 참고.
	//    (Phase A: the leftover SWARM used to be released here too — "새 아레나 좌표로 재배치하지 않는다" — but user
	//    decision now carries it over instead (step 4.5, CarryEnemiesToNewStage), so there is nothing enemy-related
	//    left to do at this step.)

	// 7. Commit the new stage — every client follows purely from these replicated values (arena visibility toggle +
	//    the OnRunStateChanged re-broadcast in ApplyStageTransitionLocal), no dedicated RPC needed. Then enter
	//    FadeIn (Phase A phase split — used to go straight to None here; EnterFadeIn sets the phase itself).
	GS->SetStageIndex(NewStageIndex);

	// 🔴 스테이지 N>=1 억제기 내구도 적용(ADR 0010 D6 비용 축, 신설 2026-08-26) — 반드시 SetStageIndex **뒤**.
	// 2단계 Next->SetArenaActive(true) 시점에 했다면 GS->GetStageIndex() 가 아직 이전 값이라 EvalStageAt 이
	// 하나 어긋난 앵커를 고른다. Next 를 직접 넘긴다 — GS->GetActiveArena() 는 몇 줄 아래에서야 갱신되므로
	// 그걸 기다릴 이유가 없다. StageDirector -> RunDirector 결합은 CancelActiveMission()(RequestTransition, 위)
	// 으로 이미 있는 선례라 새 결합이 아니다.
	if (UFPSRRunDirectorSubsystem* RunDirector = World->GetSubsystem<UFPSRRunDirectorSubsystem>())
	{
		RunDirector->ApplyStageDifficultyToArena(Next, NewStageIndex);
	}

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
	//
	//    ...unless this IS the boss arena (보스 스테이지, 2026-08-28). There is no stage after the boss stage — the
	//    run ends there — so pre-paying AddToWorld for an arena nobody will ever enter would spend a frame budget
	//    and a level's worth of memory during the fight it is most expensive to spend them in.
	if (Next->GetArenaRole() == EFPSRArenaRole::Boss)
	{
		UE_LOG(LogFPSR, Log, TEXT("[StageDirector] Landed in the boss arena — no successor to park (the run ends here)."));
	}
	else
	{
		ParkArenaAfter(Next->GetStageOrder());
	}
}
