// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "Core/FPSRGameState.h" // EFPSRStageTransitionPhase (used by value in the pure predicates below)
#include "FPSRStageDirectorSubsystem.generated.h"

class AFPSRArenaActor;

/**
 * Server-authoritative stage-transition state machine (ADR 0010 D6). Follows the same shape as
 * UFPSRRunDirectorSubsystem: this subsystem owns the TIMERS and DECISIONS, but every gameplay gate elsewhere
 * (movement, enemy tick, damage, projectiles, XP, run clock) reads the outcome off AFPSRGameState's replicated
 * StageTransitionPhase/StageIndex/ActiveArena rather than this subsystem directly — the same GameState-mediated
 * pattern ERunPhase/bRunPaused already use.
 *
 * Flow: RequestTransition (None -> Grace, arms a one-shot dealing timer) -> OnDealingWindowClosed (Grace ->
 * Pending if the card freeze is up, else straight to Swapping) -> [Pending waits for the freeze to clear, then
 * TrySwap] -> PerformSwap (Swapping -> the actual arena swap -> None).
 */
UCLASS()
class FPSROGUELITE_API UFPSRStageDirectorSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Requested by a broken suppressor's UFPSRDestructibleReward_StageTransition::Grant(). Silently ignored if a
	 *  transition is already in progress (Phase != None) — several suppressors can exist in one arena, or an
	 *  explosion can finish more than one at once, and only the FIRST request should start the state machine. */
	void RequestTransition();

	bool IsTransitioning() const;

	// ---- Pure, worldless predicates (unit-tested in FPSRStageTransitionTest.cpp, no world needed) ----------------

	/** Which phase to enter once the Grace dealing window closes: Pending if the card-selection freeze is up (the
	 *  swap must wait it out — ADR 0010 invariant 8, see AFPSRGameState::IsStageDealingOpen's header comment),
	 *  otherwise straight to Swapping. */
	static EFPSRStageTransitionPhase DecidePhaseAfterDealing(bool bRunPaused);

	/** Whether a Pending transition may swap now — i.e. whether the card-selection freeze has cleared. */
	static bool CanSwapNow(bool bRunPaused);

	/** Deterministic next-stage seed derived from BaseSeed. The server computes this once and replicates the
	 *  resulting ActiveSeed (AFPSRArenaActor never re-rolls locally) — every client must derive the identical
	 *  layout from it, so the formula has to be a pure function of its two inputs. */
	static int32 ComputeStageSeed(int32 BaseSeed, int32 StageIndex);

	/** Next arena index, cycling through Count arenas in StageOrder order. Count<=0 -> INDEX_NONE. A single arena
	 *  (Count==1) returns itself — see PerformSwap's comment on why that is the intended behavior, not a gap. */
	static int32 NextArenaIndex(int32 CurrentIndex, int32 Count);

	/** Same rule as AFPSRGameState::IsStageDealingOpen, exposed static + worldless for the automation test to lock
	 *  down independently of a live GameState instance. */
	static bool IsDealingOpen(EFPSRStageTransitionPhase Phase, float NowServerTime, float DealingEndServerTime);

	/** Seed-space stride between stages (ADR 0010 D6 slice spec): spreads the seed space so adjacent stages don't
	 *  land on similar-looking layouts. Named rather than a bare literal inside ComputeStageSeed. */
	static constexpr int32 StageSeedStride = 7919;

private:
	bool HasServerAuthority() const;
	AFPSRGameState* GetGS() const;

	/** Grace's one-shot dealing timer expired: decide Pending vs. Swapping (DecidePhaseAfterDealing) and act on it. */
	void OnDealingWindowClosed();

	/** Bound to GameState::OnRunStateChanged from the moment RequestTransition enters Grace through PerformSwap
	 *  (F3) — unbound there, same as before. Branches on the CURRENT phase:
	 *   - Grace: tracks the card-selection freeze (bRunPaused) edge and pauses/unpauses DealingTimerHandle across
	 *     it (see bWasRunPausedForDealing) — invariant 8 promises a fixed amount of PLAYER-USABLE dealing time, and
	 *     the card freeze blocks firing (the window's only reward channel), so time spent behind the card screen
	 *     must not count against the window.
	 *   - Pending: swaps the instant the freeze clears (TrySwap), same as before F3. */
	UFUNCTION()
	void HandleRunStateChanged();

	/** Pending -> Swapping -> PerformSwap, guarded by CanSwapNow. Safe to call redundantly — HandleRunStateChanged
	 *  can fire while Pending for reasons unrelated to the freeze (e.g. mission progress ticking on OnRunStateChanged). */
	void TrySwap();

	/** The swap itself: activate the destination, regenerate it, teleport living players, deactivate the source,
	 *  release the leftover swarm, commit the new stage to GameState. See the .cpp for the fixed step order. */
	void PerformSwap();

	/** True once HandleRunStateChanged has been bound to GameState::OnRunStateChanged, so entering Grace never
	 *  binds the same handler twice across repeated transitions. */
	bool bBoundRunStateChanged = false;

	/** F3 edge-tracker: GS->IsRunPaused() as of the last time HandleRunStateChanged acted on it, while Phase ==
	 *  Grace. OnRunStateChanged fires for many unrelated reasons (run clock, mission progress) — comparing against
	 *  this is what turns "the broadcast fired" into "the freeze actually just started/ended", so DealingTimerHandle
	 *  is paused/unpaused exactly once per freeze rather than on every unrelated broadcast (the engine timer calls
	 *  are not idempotent the way SetArenaActive's collision toggles are). Seeded from the live value when Grace is
	 *  entered (RequestTransition), mirroring UFPSRFlowFieldSubsystem::TryBindRunStateHandler's bWasPaused. */
	bool bWasRunPausedForDealing = false;

	/** Fallback dealing-window length when the run has no schedule asset assigned (mirrors
	 *  UFPSRRunDirectorSubsystem::FallbackBossTime — an asset-less run still needs to work, just untuned). */
	static constexpr float DefaultStageGraceSeconds = 8.0f;

	/** One-shot timer for the Grace dealing window (armed by RequestTransition, fires OnDealingWindowClosed). */
	FTimerHandle DealingTimerHandle;
};
