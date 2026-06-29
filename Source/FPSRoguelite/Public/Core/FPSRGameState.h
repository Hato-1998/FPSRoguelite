// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/GameStateBase.h"
#include "FPSRGameState.generated.h"

class AFPSRBossBase;

/** Macro run phase. Combat = normal run / mission window; Boss = final boss (no timer, no missions).
 *  Global freeze during card selection is the separate bRunPaused flag, independent of the phase. */
UENUM(BlueprintType)
enum class ERunPhase : uint8
{
	Combat UMETA(DisplayName = "Combat"),
	Boss   UMETA(DisplayName = "Boss")
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnRunStateChanged);

/** Fired once on the server when the run ends (EndRunFreeze, victory or defeat). Decoupled hook so flow logic
 *  (e.g. the GameMode's post-run travel back to the lobby, P7 §3-5) can react WITHOUT editing EndRun's body —
 *  keeps the victory caller (U3) and the lobby-return caller (U11) on independent code paths. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnRunEnded);

/** Fired on every client (and the host) when the active boss is set or cleared (B11). Boss = the spawned boss, or
 *  null when the boss is gone — the HUD boss health bar shows/hides and (re)binds to the boss health on this. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnActiveBossChanged, AFPSRBossBase*, Boss);

/** Server-authoritative run progression state (shared XP, party level, run phase, global freeze).
 *  Redesign 2026-06-04 (Game.MD §2-2): on level-up (or mission clear) the run globally freezes — enemies
 *  and players stop — until every player finishes their card picks. Replicated via Push Model. */
UCLASS()
class FPSROGUELITE_API AFPSRGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	AFPSRGameState();

	UFUNCTION(BlueprintPure, Category = "FPSR|Run")
	int32 GetSharedXP() const { return SharedXP; }

	UFUNCTION(BlueprintPure, Category = "FPSR|Run")
	int32 GetPartyLevel() const { return PartyLevel; }

	UFUNCTION(BlueprintPure, Category = "FPSR|Run")
	ERunPhase GetRunPhase() const { return RunPhase; }

	bool IsCombatPhase() const { return RunPhase == ERunPhase::Combat; }

	/** Global freeze flag: true while any player is selecting cards (opening seed / level-up / mission
	 *  reward). When true, enemies and players are frozen (gameplay-state gate, not TimeDilation). */
	UFUNCTION(BlueprintPure, Category = "FPSR|Run")
	bool IsRunPaused() const { return bRunPaused; }

	/** Mission-driven global vision restriction (LimitedVision mission). Cosmetic camera post-process applied
	 *  per-client; gameplay-neutral. Server-authoritative, replicated like bRunPaused. */
	UFUNCTION(BlueprintPure, Category = "FPSR|Run")
	bool IsVisionRestricted() const { return bVisionRestricted; }

	/** Host-controlled friendly-fire toggle (P5 §2-2). When false, player weapons do no damage to OTHER players;
	 *  when true, friendly hits land at GetFriendlyFireDamageScale(). Self-damage (explosions) and knockback are
	 *  independent of this flag. Server sets it; replicated for UI mirroring (damage resolves server-side). */
	UFUNCTION(BlueprintPure, Category = "FPSR|Run")
	bool IsFriendlyFireEnabled() const { return bFriendlyFireEnabled; }

	/** Damage multiplier applied to friendly-player hits while friendly fire is enabled (0.5 = half damage). */
	UFUNCTION(BlueprintPure, Category = "FPSR|Run")
	float GetFriendlyFireDamageScale() const { return FriendlyFireDamageScale; }

	/** Server: enable/disable friendly fire for the run (host setting entry point; debug cmd FPSR.SetFriendlyFire). */
	void SetFriendlyFireEnabled(bool bEnabled);

	/** Replicated run clock (survival seconds; pauses during freeze and after boss, low-frequency UI mirror). */
	UFUNCTION(BlueprintPure, Category = "FPSR|Run")
	float GetRunClockSeconds() const { return RunClockSeconds; }

	/** Lobby ready-countdown remaining seconds (0 when not counting down). Replicated via the server-time end stamp,
	 *  so it reads correctly on BOTH the host and remote clients (the lobby GameMode timer is server-only). (U11a) */
	UFUNCTION(BlueprintPure, Category = "FPSR|Lobby")
	float GetLobbyReadyCountdownRemaining() const;

	/** XP required to advance FROM the given level to the next. */
	UFUNCTION(BlueprintPure, Category = "FPSR|Run")
	int32 GetRequiredXP(int32 Level) const;

	/** XP required for the current PartyLevel -> next. */
	UFUNCTION(BlueprintPure, Category = "FPSR|Run")
	int32 GetRequiredXPForNextLevel() const { return GetRequiredXP(PartyLevel); }

	/** Server: add shared XP and process level-ups. Grants per-player picks and freezes the run for
	 *  selection (Game.MD §2-2). */
	void AddSharedXP(int32 Amount);

	/** Server: set the macro run phase (Combat / Boss). */
	void SetRunPhase(ERunPhase NewPhase);

	/** Server: set the global freeze flag directly (normally driven by RefreshPauseState). */
	void SetRunPaused(bool bPaused);

	/** Server: end-of-run terminal freeze. Pins the global freeze on (reuses bRunPaused) and latches bRunEnded so
	 *  RefreshPauseState stops recomputing — the world stays frozen behind the result screen even if a player's
	 *  card selection completes after EndRun. Idempotent. */
	void EndRunFreeze();

	/** Server: toggle the global vision restriction (driven by the LimitedVision mission). */
	void SetVisionRestricted(bool bRestricted);

	/** Server: recompute the freeze state from outstanding player selections and (re)present needed offers.
	 *  Paused iff any connected player still has a pending pick or an active offer; unpauses when all done. */
	void RefreshPauseState();

	/** Server: update the replicated run clock (low-frequency UI mirror). */
	void SetRunClockSeconds(float Seconds);

	/** Server: arm/clear the replicated lobby ready-countdown. Pass the server-world-time the run will start at
	 *  (GetServerWorldTimeSeconds() + countdown), or 0 to clear. Drives GetLobbyReadyCountdownRemaining on all. */
	void SetLobbyCountdownEndTime(float ServerTimeSeconds);

	UPROPERTY(BlueprintAssignable, Category = "FPSR|Run")
	FOnRunStateChanged OnRunStateChanged;

	/** Server: broadcast once when the run ends (see FOnRunEnded). Subscribers travel/clean up after the run. */
	UPROPERTY(BlueprintAssignable, Category = "FPSR|Run")
	FOnRunEnded OnRunEnded;

	/** Active boss (B11) — replicated so a client HUD boss bar can locate the boss and bind its health. Null = none. */
	UFUNCTION(BlueprintPure, Category = "FPSR|Run")
	AFPSRBossBase* GetActiveBoss() const { return ActiveBoss; }

	/** Server: set/clear the active boss (called by the run director on boss spawn / clear). Replicates + broadcasts. */
	void SetActiveBoss(AFPSRBossBase* InBoss);

	/** Fires on all clients (+ host) when the active boss is set/cleared (B11) — the HUD boss bar binds here to
	 *  show/hide and (re)bind the boss's UFPSREnemyHealthComponent::OnHealthChanged (now client-fired via B12). */
	UPROPERTY(BlueprintAssignable, Category = "FPSR|Run")
	FOnActiveBossChanged OnActiveBossChanged;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	UFUNCTION()
	void OnRep_RunState();

	UFUNCTION()
	void OnRep_ActiveBoss();

	/** XP required to advance from level 1; each level adds XPPerLevel (linear curve placeholder —
	 *  a UCurveFloat data-driven curve is a follow-up, Game.MD §2-8). Editor-tunable. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Run")
	int32 XPBaseRequired = 100;

	/** Per-level increase to the XP requirement: GetRequiredXP(L) = XPBaseRequired + (L-1)*XPPerLevel. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Run")
	int32 XPPerLevel = 50;

	/** Party levels at which a weapon-unlock pick is granted to everyone (U18b). Data-driven (no hardcode). */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Run")
	TArray<int32> WeaponUnlockMilestones = { 20, 30, 40 };

	UPROPERTY(ReplicatedUsing = OnRep_RunState)
	int32 SharedXP = 0;

	UPROPERTY(ReplicatedUsing = OnRep_RunState)
	int32 PartyLevel = 1;

	UPROPERTY(ReplicatedUsing = OnRep_RunState)
	ERunPhase RunPhase = ERunPhase::Combat;

	UPROPERTY(ReplicatedUsing = OnRep_RunState)
	bool bRunPaused = false;

	UPROPERTY(ReplicatedUsing = OnRep_RunState)
	bool bVisionRestricted = false;

	UPROPERTY(ReplicatedUsing = OnRep_RunState)
	float RunClockSeconds = 0.0f;

	/** Server-world-time stamp at which the lobby ready-countdown completes (0 = not counting down). Replicated so a
	 *  remote client can compute the remaining time against the synced server clock (the GameMode timer is host-only). */
	UPROPERTY(ReplicatedUsing = OnRep_RunState)
	float LobbyCountdownEndServerTime = 0.0f;

	/** Host friendly-fire setting (replicated; read server-side for damage, mirrored to clients for UI). */
	UPROPERTY(ReplicatedUsing = OnRep_RunState)
	bool bFriendlyFireEnabled = false;

	/** Active boss (B11). Replicated to all so the HUD boss bar can find it; the boss is bAlwaysRelevant so its
	 *  UFPSREnemyHealthComponent replicates regardless of distance. Set/cleared by the run director (server). */
	UPROPERTY(ReplicatedUsing = OnRep_ActiveBoss)
	TObjectPtr<AFPSRBossBase> ActiveBoss = nullptr;

	/** Friendly-player damage multiplier while friendly fire is on (editor-tunable; 0.5 = half). */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Run")
	float FriendlyFireDamageScale = 0.5f;

	/** Server-only terminal latch: true once the run has ended (EndRunFreeze). Pins bRunPaused on by making
	 *  RefreshPauseState early-out, so the world stays frozen behind the result screen. Not replicated — the
	 *  visible freeze rides the replicated bRunPaused; this resets naturally on the next run (fresh GameState). */
	bool bRunEnded = false;
};
