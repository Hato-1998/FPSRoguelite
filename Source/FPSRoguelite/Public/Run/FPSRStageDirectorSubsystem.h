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
 * Flow (Phase A phase split): RequestTransition (None -> Grace, arms a one-shot dealing timer) -> OnDealingWindowClosed
 * (Grace -> Pending if the card freeze is up, else straight to FadeOut) -> [Pending waits for the freeze to clear,
 * then TrySwap -> FadeOut] -> OnFadeOutComplete (FadeOut -> Swapping -> BeginSwap) -> PerformSwap (the actual arena
 * swap, still inside Swapping -> FadeIn) -> OnFadeInComplete (FadeIn -> None). FadeOut/FadeIn are fixed-length
 * cosmetic holds either side of the swap (0 length = instant, the pre-Phase-A hard-cut behavior); the visual fade
 * itself is Phase B (out of scope here — this subsystem only owns the timing/state).
 */
UCLASS()
class FPSROGUELITE_API UFPSRStageDirectorSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Parks the arena AFTER the starting one as soon as the world begins (ADR 0012 axis 5) — the whole point is
	 *  that AddToWorld is paid a stage early, so the first park cannot wait for the first transition. */
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	virtual void Deinitialize() override;

	/** Requested by a broken suppressor's UFPSRDestructibleReward_StageTransition::Grant(). Silently ignored if a
	 *  transition is already in progress (Phase != None) — several suppressors can exist in one arena, or an
	 *  explosion can finish more than one at once, and only the FIRST request should start the state machine. */
	void RequestTransition();

	bool IsTransitioning() const;

	// ---- Pure, worldless predicates (unit-tested in FPSRStageTransitionTest.cpp, no world needed) ----------------

	/** Which phase to enter once the Grace dealing window closes: Pending if the card-selection freeze is up (the
	 *  swap must wait it out — ADR 0010 invariant 8, see AFPSRGameState::IsStageDealingOpen's header comment),
	 *  otherwise straight to FadeOut (Phase A phase split — FadeOut used to be Swapping's job before the fade
	 *  phases existed; Pending still hands off to FadeOut too, via TrySwap, once the freeze clears). */
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
	 *   - Pending: swaps the instant the freeze clears (TrySwap -> FadeOut, Phase A phase split), same edge as
	 *     before F3. */
	UFUNCTION()
	void HandleRunStateChanged();

	/** Pending -> FadeOut (Phase A phase split — used to go straight to Swapping), guarded by CanSwapNow. Safe to
	 *  call redundantly — HandleRunStateChanged can fire while Pending for reasons unrelated to the freeze (e.g.
	 *  mission progress ticking on OnRunStateChanged). */
	void TrySwap();

	/** Grace/Pending closed -> FadeOut entered: arms a one-shot fade timer (StageFadeOutSeconds, or immediate if 0 —
	 *  the pre-Phase-A hard-cut path) that fires OnFadeOutComplete. The visual fade itself is Phase B; this only
	 *  owns the timing so the swarm/movement freeze (gated on IsStageTransitionActive) already covers it. */
	void EnterFadeOut();

	/** FadeOut's timer expired (or was skipped, 0-length): FadeOut -> Swapping, then BeginSwap (unchanged from
	 *  before the phase split — Swapping now means "blacked out + waiting for/running the swap", not "just
	 *  swapping this frame"). */
	void OnFadeOutComplete();

	/** PerformSwap's final commit entered FadeIn: arms a one-shot fade timer (StageFadeInSeconds, or immediate if 0)
	 *  that fires OnFadeInComplete. */
	void EnterFadeIn();

	/** FadeIn's timer expired (or was skipped): FadeIn -> None. The transition is over. */
	void OnFadeInComplete();

	/** StageFadeOutSeconds from the run schedule, or DefaultStageFadeOutSeconds with no schedule asset. */
	float GetStageFadeOutSeconds() const;

	/** StageFadeInSeconds from the run schedule, or DefaultStageFadeInSeconds with no schedule asset. */
	float GetStageFadeInSeconds() const;

	/**
	 * Entry point for the swap once the dealing window has closed: hand over to PerformSwap the moment the
	 * destination arena is visible to EVERY connection, or after StageSwapReadyTimeoutSeconds regardless.
	 *
	 * Sublevel visibility is per client (UNetConnection::ClientVisibleLevelNames), and the server will not
	 * replicate an actor in a level a given client has not made visible — so swapping to an arena a client
	 * cannot see teleports that player into a space with no geometry, no destructibles and no ActiveArena
	 * pointer. ADR 0012 axis 5 makes this wait normally ZERO by parking a stage early; this is the tail case.
	 *
	 * This does NOT put the wait inside the damage-dealing window. Grace has already ended by the time anything
	 * here runs (see the call sites), so ADR 0010 invariant 8 — a FIXED reward window — is untouched.
	 */
	void BeginSwap();

	/** True (and swaps) if the destination is ready for everyone. False leaves the caller to keep waiting. */
	bool TrySwapIfDestinationReady();

	/** Retry body for BeginSwap's wait; gives up (and swaps anyway, loudly) at the authored timeout. */
	void PollSwapReadiness();

	/** Authored everyone-ready wait cap, or DefaultSwapReadyTimeoutSeconds when the run has no schedule asset. */
	float GetSwapReadyTimeoutSeconds() const;

	/** Ask UFPSRArenaStreamSubsystem to park whatever arena comes after StageOrder. No-op with one arena. */
	void ParkArenaAfter(int32 StageOrder) const;

	/** StageOrder of the arena the run is standing in right now, or INDEX_NONE. */
	int32 GetCurrentStageOrder() const;

	/** The swap itself: activate the destination, regenerate it, teleport living players, carry the leftover swarm
	 *  over to the new arena (Phase A — replaces the old ReleaseAllEnemies), deactivate the source, cancel any
	 *  still-active mission (its objective was left behind in the old arena), commit the new stage to GameState,
	 *  then enter FadeIn (EnterFadeIn) instead of going straight to None. See the .cpp for the fixed step order. */
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

	/** Fallback FadeOut/FadeIn length when the run has no schedule asset assigned (same untuned-fallback pattern as
	 *  DefaultStageGraceSeconds; matches UFPSRRunScheduleDataAsset::StageFadeOutSeconds/StageFadeInSeconds' own
	 *  default so an asset-less run behaves the same as a freshly-authored schedule). */
	static constexpr float DefaultStageFadeOutSeconds = 0.8f;
	static constexpr float DefaultStageFadeInSeconds = 0.8f;

	/** One-shot timer for the Grace dealing window (armed by RequestTransition, fires OnDealingWindowClosed). */
	FTimerHandle DealingTimerHandle;

	/** One-shot timer shared by FadeOut and FadeIn (never both armed at once — the phases are sequential), armed by
	 *  EnterFadeOut/EnterFadeIn, fires OnFadeOutComplete/OnFadeInComplete. */
	FTimerHandle FadeTimerHandle;

	/** Repeating timer for the everyone-ready wait in BeginSwap. Only ever active while Phase == Swapping. */
	FTimerHandle SwapReadyTimerHandle;

	/** Seconds BeginSwap has been waiting for the destination arena to reach every connection. */
	float SwapReadyElapsed = 0.0f;

	/** How often the everyone-ready wait re-checks. Matches UFPSRArenaStreamSubsystem's own poll interval —
	 *  checking faster than the thing being observed updates only burns queries. */
	static constexpr float SwapReadyPollInterval = 0.2f;

	/** Fallback everyone-ready timeout when the run has no schedule asset assigned (mirrors
	 *  DefaultStageGraceSeconds — an asset-less run still needs to work, just untuned). */
	static constexpr float DefaultSwapReadyTimeoutSeconds = 5.0f;
};
