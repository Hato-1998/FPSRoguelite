// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/GameStateBase.h"
#include "FPSRGameState.generated.h"

UENUM(BlueprintType)
enum class ERunPhase : uint8
{
	Combat   UMETA(DisplayName = "Combat"),
	Breather UMETA(DisplayName = "Breather"),
	Boss     UMETA(DisplayName = "Boss")
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnRunStateChanged);

/** Server-authoritative run progression state (shared XP, party level, run phase).
 *  Level-up picks are per-player on AFPSRPlayerState::CardPicksPending (Game.MD §2-2).
 *  Replicated via Push Model. Phase controls enemy spawning/attacks. */
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

	/** Current round index (0-based). Driven by the run director (server). */
	UFUNCTION(BlueprintPure, Category = "FPSR|Run")
	int32 GetCurrentRound() const { return CurrentRound; }

	/** Banked (cleared) mission rewards awaiting selection in the next breather (Game.MD §2-8; P4-A counts only). */
	UFUNCTION(BlueprintPure, Category = "FPSR|Run")
	int32 GetBankedMissionRewards() const { return BankedMissionRewards; }

	/** Replicated run clock (combat-elapsed seconds, low-frequency mirror for UI). */
	UFUNCTION(BlueprintPure, Category = "FPSR|Run")
	float GetRunClockSeconds() const { return RunClockSeconds; }

	/** XP required to advance FROM the given level to the next. */
	UFUNCTION(BlueprintPure, Category = "FPSR|Run")
	int32 GetRequiredXP(int32 Level) const;

	/** XP required for the current PartyLevel -> next. */
	UFUNCTION(BlueprintPure, Category = "FPSR|Run")
	int32 GetRequiredXPForNextLevel() const { return GetRequiredXP(PartyLevel); }

	/** Server: add shared XP and process any level-ups (grants per-player card picks; no freeze, Game.MD §2-2). */
	void AddSharedXP(int32 Amount);

	/** Server: set the run phase (Combat / Breather / Boss). */
	void SetRunPhase(ERunPhase NewPhase);

	/** Server: enter the breather (= SetRunPhase(Breather), which presents pending level-up offers). */
	void BeginBreather() { SetRunPhase(ERunPhase::Breather); }

	/** Server: set the current round index (run director). */
	void SetCurrentRound(int32 NewRound);

	/** Server: bank cleared mission rewards (presented in the next breather; P4-A increments the count only). */
	void AddBankedMissionReward(int32 Count = 1);

	/** Server: clear banked mission rewards (consumed at breather; P4-B). */
	void ResetBankedMissionRewards();

	/** Server: update the replicated run clock (low-frequency UI mirror). */
	void SetRunClockSeconds(float Seconds);

	UPROPERTY(BlueprintAssignable, Category = "FPSR|Run")
	FOnRunStateChanged OnRunStateChanged;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	UFUNCTION()
	void OnRep_RunState();

	/** Server: present a level-up card offer to every player who has pending picks and no active offer.
	 *  Called on breather entry and when picks are granted while already in the breather (§2-2). */
	void PresentPendingLevelUpOffers();

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
	int32 CurrentRound = 0;

	UPROPERTY(ReplicatedUsing = OnRep_RunState)
	int32 BankedMissionRewards = 0;

	UPROPERTY(ReplicatedUsing = OnRep_RunState)
	float RunClockSeconds = 0.0f;
};
