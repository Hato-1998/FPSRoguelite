// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/GameStateBase.h"
#include "FPSRGameState.generated.h"

/** Macro run phase. Combat = normal run / mission window; Boss = final boss (no timer, no missions).
 *  Global freeze during card selection is the separate bRunPaused flag, independent of the phase. */
UENUM(BlueprintType)
enum class ERunPhase : uint8
{
	Combat UMETA(DisplayName = "Combat"),
	Boss   UMETA(DisplayName = "Boss")
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnRunStateChanged);

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

	/** Replicated run clock (survival seconds; pauses during freeze and after boss, low-frequency UI mirror). */
	UFUNCTION(BlueprintPure, Category = "FPSR|Run")
	float GetRunClockSeconds() const { return RunClockSeconds; }

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

	/** Server: recompute the freeze state from outstanding player selections and (re)present needed offers.
	 *  Paused iff any connected player still has a pending pick or an active offer; unpauses when all done. */
	void RefreshPauseState();

	/** Server: update the replicated run clock (low-frequency UI mirror). */
	void SetRunClockSeconds(float Seconds);

	UPROPERTY(BlueprintAssignable, Category = "FPSR|Run")
	FOnRunStateChanged OnRunStateChanged;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	UFUNCTION()
	void OnRep_RunState();

	/** XP required to advance from level 1; each level adds XPPerLevel (linear curve placeholder —
	 *  a UCurveFloat data-driven curve is a follow-up, Game.MD §2-8). Editor-tunable. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Run")
	int32 XPBaseRequired = 100;

	/** Per-level increase to the XP requirement: GetRequiredXP(L) = XPBaseRequired + (L-1)*XPPerLevel. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Run")
	int32 XPPerLevel = 50;

	UPROPERTY(ReplicatedUsing = OnRep_RunState)
	int32 SharedXP = 0;

	UPROPERTY(ReplicatedUsing = OnRep_RunState)
	int32 PartyLevel = 1;

	UPROPERTY(ReplicatedUsing = OnRep_RunState)
	ERunPhase RunPhase = ERunPhase::Combat;

	UPROPERTY(ReplicatedUsing = OnRep_RunState)
	bool bRunPaused = false;

	UPROPERTY(ReplicatedUsing = OnRep_RunState)
	float RunClockSeconds = 0.0f;
};
