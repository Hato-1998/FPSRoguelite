// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "FPSRArenaStreamSubsystem.generated.h"

class AFPSRArenaActor;
class ULevelStreaming;

/** One arena the persistent level knows about, whether or not its sublevel is in the world yet. */
struct FFPSRArenaSlot
{
	/** Package name of the arena's sublevel — the key everything else uses (LoadStreamLevel, and the names a
	 *  client reports back in UNetConnection::ClientVisibleLevelNames). */
	FName PackageName;

	/** Authored transition order, read off the arena actor inside the (loaded) level. */
	int32 StageOrder = 0;

	/** The arena actor itself. Valid as soon as the PACKAGE is loaded — an actor exists as an object in
	 *  ULevel::Actors before AddToWorld ever runs, which is what lets the roster be built from arenas that are
	 *  not visible yet. Weak because a level can be unloaded out from under us. */
	TWeakObjectPtr<AFPSRArenaActor> Arena;

	/** Authored to be the live arena at level start (exactly one should be). */
	bool bStartsActive = false;
};

/**
 * Parks the NEXT arena's sublevel a whole stage before it is needed (ADR 0012 axis 5).
 *
 * ## Why a stage early and not at the swap
 *
 * Streaming a level in is two steps and the expensive one is not the load. `bShouldBeLoaded` reads the package
 * off disk asynchronously and barely touches the game thread; `bShouldBeVisible` is what triggers AddToWorld —
 * actor world-entry, RouteActorInitialize, component registration — which the engine time-slices at 5 ms per
 * frame (`s.LevelStreamingActorsUpdateTimeLimit`, BaseEngine.ini) and spreads over many frames. Preloading
 * alone cannot shorten it, because AddToWorld does not start until ShouldBeVisible() is true
 * (`LevelStreaming.cpp`), and forcing it into one frame with bShouldBlockOnLoad is a hitch by definition.
 *
 * In four-player co-op that lands on ADR 0010 invariant 8: the damage-dealing window during a stage transition
 * has to be a FIXED length, not a function of the slowest player's disk. So the cost is paid a stage ahead,
 * outside any window, and the swap itself becomes show + teleport + publish.
 *
 * ## Why the replication of all this is the engine's job
 *
 * Sublevel visibility is PER CLIENT: the server filters actor replication by
 * `UNetConnection::ClientVisibleLevelNames` (NetDriver.cpp), so a client that has not finished making the arena
 * visible receives none of its actors. The engine already carries the instruction both ways —
 * `FStreamLevelAction::ActivateLevel` notifies every PlayerController when the server streams a level in, and
 * `AGameModeBase::ReplicateStreamingStatus` catches up late joiners; clients report back through
 * `APlayerController::ServerUpdateLevelVisibility`. This subsystem adds NO transport of its own. It does mean
 * arena sublevels have to be authored into the persistent level's streaming list rather than created at
 * runtime: `ClientUpdateLevelStreamingStatus` looks the streaming object up by package name and logs "Unable to
 * find streaming object" if the client has no matching entry (PlayerController.cpp), so a server-side
 * LoadLevelInstance would simply never reach anyone.
 *
 * ## Shape
 *
 * Deliberately the same shape as UFPSRMapStreamSubsystem: server-only, a poll timer, and game-side actions
 * gated on the VERIFIED LoadedVisible state rather than on a latent callback (which fires before collision is
 * registered, and fires even for a level name that does not exist).
 */
UCLASS()
class FPSROGUELITE_API UFPSRArenaStreamSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Deinitialize() override;

	/**
	 * Server: begin making the arena at StageOrder visible, and hide it as soon as it lands.
	 *
	 * Idempotent — safe to call every stage, and safe to call for an arena that is already parked or already
	 * live. No-op off-authority or when the roster has no such arena (a single-arena map cycles to itself and
	 * has nothing to park, which is not an error).
	 */
	void RequestPark(int32 StageOrder);

	/**
	 * Park whatever arena comes AFTER CurrentStageOrder, retrying until the roster can answer.
	 *
	 * The retry is not defensive padding. The roster is built from arena actors inside LOADED level packages, and
	 * at world begin a sublevel can still be async-loading — so a one-shot "park the next arena" at level start
	 * would find a roster of one, conclude there is nothing to park, and never try again. The first transition
	 * would then cycle the arena to ITSELF with no error anywhere, which reads as working.
	 */
	void RequestParkAfter(int32 CurrentStageOrder);

	/**
	 * Is the arena at StageOrder visible on the SERVER and on every client connection?
	 *
	 * This is the "전원 준비" gate. It is meant to be asked a stage ahead, where a false answer still leaves room
	 * to warn or wait; asking it inside the transition window and waiting on it there would make the window a
	 * function of hardware, which is the thing invariant 8 forbids.
	 *
	 * Returns true for an unknown StageOrder: "there is nothing to wait for" and "everyone has it" have to lead
	 * to the same behaviour, or a single-arena map would block its own transitions forever.
	 */
	bool IsReadyForEveryone(int32 StageOrder) const;

	/** Which connections are still missing the arena at StageOrder, for logging. Empty when ready (or unknown). */
	void GetConnectionsNotReady(int32 StageOrder, TArray<FString>& OutNames) const;

	/** The arena authored to be live at level start, or INDEX_NONE. Used to decide what to park first. */
	int32 FindStartingStageOrder() const;

	/**
	 * The StageOrder a transition from CurrentStageOrder would move to, cycling through the roster.
	 *
	 * Deliberately answered from the ROSTER and not from AFPSRArenaActor::FindAllInWorld: that one iterates the
	 * world, so it cannot see an arena whose sublevel has not been made visible yet — which is precisely the
	 * arena a transition is about to need. Asking the world would silently cycle back to the current arena and
	 * the run would never advance.
	 *
	 * A single-arena roster returns that arena (cycling to itself is the intended single-arena behaviour, matching
	 * UFPSRStageDirectorSubsystem::NextArenaIndex). INDEX_NONE only if there are no arenas at all.
	 */
	int32 GetNextStageOrder(int32 CurrentStageOrder) const;

	/** Roster entry for StageOrder. Rebuilds the roster on demand — arena sublevels finish loading asynchronously,
	 *  so the first call can legitimately come before they are all there.
	 *
	 *  Returns a COPY on purpose. The roster is rebuilt by every query, so a pointer into it would dangle the
	 *  moment the caller asked a second question (which is exactly what the poll loop does). */
	bool FindSlot(int32 StageOrder, FFPSRArenaSlot& OutSlot) const;

private:
	bool HasServerAuthority() const;

	/**
	 * (Re)scan the persistent level's streaming levels for arenas.
	 *
	 * The roster is derived, not authored twice: every arena sublevel is listed in the Levels panel (that is
	 * what makes client replication work at all) and carries exactly one AFPSRArenaActor (invariant 4), so the
	 * arena's own StageOrder is the ordering key and nothing has to be kept in sync by hand. Reading it needs
	 * only the PACKAGE to be loaded, not the level to be visible — ULevel::Actors is populated from the package
	 * well before AddToWorld runs, which is exactly the window this subsystem operates in.
	 */
	void RefreshRoster() const;

	/** True when LevelName's streaming level has reached LoadedVisible on this machine (AddToWorld complete =
	 *  components and collision registered). Same predicate UFPSRMapStreamSubsystem uses. */
	bool IsLevelVisibleLocally(FName PackageName) const;

	/** Timer body: hide each parked arena the moment its level lands, and report ones that never arrive. */
	void PollPending();

	/** Arena stage orders currently being parked, with how long they have been waiting. */
	struct FPendingPark
	{
		int32 StageOrder = 0;
		float Elapsed = 0.0f;
	};

	TArray<FPendingPark> Pending;

	/** RequestParkAfter's unresolved request: the arena we want to park the SUCCESSOR of, or INDEX_NONE. */
	int32 PendingParkAfterOrder = INDEX_NONE;

	/** How long PendingParkAfterOrder has gone unresolved. */
	float ParkAfterElapsed = 0.0f;

	/** Mutable because FindSlot/IsReadyForEveryone are const observers that still have to tolerate a roster that
	 *  was not complete the first time it was built (sublevels load asynchronously). */
	mutable TArray<FFPSRArenaSlot> Roster;

	FTimerHandle PollTimer;
	int32 NextLatentUUID = 1;

	static constexpr float PollInterval = 0.2f;   // s between readiness checks
	static constexpr float ParkTimeout = 30.0f;   // s before a stuck park gives up warning (the arena stays queued)

	/** s to keep retrying a RequestParkAfter whose successor is not in the roster yet. Generous because it only
	 *  costs a 0.2 s poll, and the failure it guards (never parking at all) is silent. */
	static constexpr float RosterResolveTimeout = 15.0f;
};
