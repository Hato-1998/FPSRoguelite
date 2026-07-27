// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "Director/FPSRDirectorSensorTypes.h"
#include "FPSRDirectorSensorSubsystem.generated.h"

class AFPSRGameState;
class AFPSRPlayerState;
class AController;

/** Closed-loop "storyteller" director — SENSOR subsystem (P0a-0 walking skeleton, RunFlow §2-8-2).
 *
 *  Server-authoritative run-telemetry layer. Measures only (design §8-1): raw events, rolling counters,
 *  statistical transforms, lifecycle. It owns NO semantic judgement (never a Score) and changes nothing
 *  in the game. ZERO replication — per-player state is a server-local TMap keyed by a weak PlayerState
 *  pointer (no PlayerState pollution). Exists on server+client in game worlds (project convention) but
 *  every method is gated behind HasServerAuthority(); on a client it is inert.
 *
 *  P0a-0 goal = LOCK THE PLUMBING as golden invariants: lifecycle leak 0, no-progress-during-freeze,
 *  inject->snapshot determinism, Front mapping, enemy/boss source gating. NOT tuning values.
 *
 *  Five signals only: HealthPct · IncomingDamageRate[Enemy]/[Boss] · DownedRecent01(B3) ·
 *  MovementConfinement01(C1) · FrontId. Everything derived/normalised/grouped is P0a-1+.
 *
 *  First principles: server-only, bounded (<=4 players), NO per-enemy tick — the two live hooks sit on
 *  the player damage/down path, aggregation is a 0.5s timer over the <=4-player array. No enemy hot path. */
UCLASS()
class FPSROGUELITE_API UFPSRDirectorSensorSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	//~UWorldSubsystem
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Deinitialize() override;
	//~End UWorldSubsystem

	// ---- Lifecycle (driven by AFPSRGameMode) ---------------------------------------------------------
	/** Reset per-player telemetry to a fresh baseline, subscribe to run-end, arm the aggregation clock. */
	void StartRun();
	/** Explicit teardown: clear the telemetry (tombstone leak 0) and disarm the aggregation clock. */
	void EndRun();
	/** A player left: drop its telemetry immediately (a freeze halts the lazy prune; reconnect = new baseline). */
	void OnPlayerLogout(AController* Exiting);

	// ---- Live hooks (called from the player damage/down choke points; server-authoritative) ----------
	/** IncomingDamageRate hook — an ACCEPTED contact hit landed on a player. Source is classified from the
	 *  instigator here (enemy/boss counted; FF/self/door/mission/env excluded). Bridge signature unchanged. */
	void NotifyPlayerDamageTaken(AActor* Victim, float Amount, AActor* Instigator);
	/** DownedRecent hook — a player transitioned Alive->DBNO (fires once per down). */
	void NotifyPlayerDowned(AFPSRPlayerState* PS);

	// ---- Read-only snapshot (director-consumer stub; NO actuator in P0a-0) ----------------------------
	bool GetPlayerSnapshot(const AFPSRPlayerState* PS, FFPSRPlayerTelemetry& OutSnapshot) const;

	/** Deterministic advance (the timer and the tests both call this). Integrates every tracked player by
	 *  exactly Dt of sensor time; does NOT run itself when frozen (the caller gates via ShouldAdvance). */
	void Advance(float Dt);

	/** Sensor-clock (accumulated unpaused seconds). Exposed for debug/tests. */
	float GetSensorClock() const { return SensorClock; }
	int32 GetTrackedPlayerCount() const { return Telemetry.Num(); }

	// ---- Tunables (public so tests/dumps share the exact params) -------------------------------------
	static const FFPSRConfinementParams& DefaultConfinementParams();
	static constexpr float SampleInterval = 0.5f;
	static constexpr float IncomingRateEwmaAlpha = 0.4f;
	static constexpr float DownedRecentWindow = 10.0f;

#if !UE_BUILD_SHIPPING
	// ---- Injection harness (console FPSR.Telemetry.* + PIE). idx = PlayerArray order (non-spectator). --
	void InjectDamageTaken(int32 Idx, float Amount, EFPSRDamageSourceClass Src);
	void InjectDown(int32 Idx);
	void SetHealthBand(int32 Idx, float Pct, float Duration);
	void SampleMove(int32 Idx, float X, float Y, float Z, float T);
	void SetFront(int32 Idx, FName FrontTagName);
	void DebugLogout(int32 Idx);
	void DumpSnapshot() const;
#endif

private:
	bool HasServerAuthority() const;
	AFPSRGameState* GetGS() const;

	void SensorTick();                                   // repeating timer callback (freeze-gated -> Advance)
	void ReconcilePlayers();                             // add missing players, prune invalid weak keys
	void SamplePlayer(AFPSRPlayerState* PS, FFPSRPlayerTelemetry& T, float Dt); // ASC/pawn I/O -> Integrate
	FFPSRPlayerTelemetry& FindOrAddEntry(AFPSRPlayerState* PS);
	AFPSRPlayerState* ResolvePlayerByIndex(int32 Idx) const;

	UFUNCTION()
	void HandleRunEnded();                               // bound to AFPSRGameState::OnRunEnded

	/** Server-local per-player telemetry. Weak key so a leaving/destroyed PlayerState is pruned, never a
	 *  tombstone; NOT a UPROPERTY (no reflection, no replication, no GC roots held). */
	TMap<TWeakObjectPtr<AFPSRPlayerState>, FFPSRPlayerTelemetry> Telemetry;

	float SensorClock = 0.0f;                            // accumulated UNPAUSED sensor seconds
	bool bRunActive = false;
	bool bRunEndedBound = false;
	FTimerHandle SensorTimerHandle;
};
