// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CommonUserWidget.h"
#include "Core/FPSRGameState.h"
#include "FPSRRunHUDWidget.generated.h"

/** Passive run-state HUD base (Game layer). Exposes replicated run state (GameState) + the local player's
 *  pending picks (PlayerState) to WBP via BlueprintPure getters, and fires OnRunStateUpdated whenever they
 *  change. Event-driven: binds GameState OnRunStateChanged + local PlayerState OnCardPicksChanged, no polling.
 *  Read-only mirror — input routing stays with the Game-layer widget (UFPSRXPBarWidget). (Game.MD §2-2/§2-14). */
UCLASS()
class FPSROGUELITE_API UFPSRRunHUDWidget : public UCommonUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UFUNCTION()
	void HandleRunStateChanged();

	UFUNCTION()
	void HandleCardPicksChanged();

	/** Lazily bind the owning PlayerState's pick delegate once it replicates (remote-client timing). */
	void EnsurePlayerStateBound();

	/** Retry the PlayerState bind on a timer until it succeeds — covers the case where the HUD constructs
	 *  before the owning PlayerState replicates AND the run is already paused (so no OnRunStateChanged fires
	 *  to drive the retry). On success: refresh once and stop the timer. */
	void RetryPlayerStateBind();

	/** WBP refresh hook: fired on construct and whenever run state / local pending picks change. */
	UFUNCTION(BlueprintImplementableEvent, Category = "FPSR|HUD")
	void OnRunStateUpdated();

	/** Replicated survival seconds (pauses during freeze / after boss). */
	UFUNCTION(BlueprintPure, Category = "FPSR|HUD")
	float GetRunClockSeconds() const;

	/** Run clock formatted mm:ss. */
	UFUNCTION(BlueprintPure, Category = "FPSR|HUD")
	FText GetRunClockText() const;

	UFUNCTION(BlueprintPure, Category = "FPSR|HUD")
	ERunPhase GetRunPhase() const;

	UFUNCTION(BlueprintPure, Category = "FPSR|HUD")
	bool IsRunPaused() const;

	UFUNCTION(BlueprintPure, Category = "FPSR|HUD")
	int32 GetPartyLevel() const;

	UFUNCTION(BlueprintPure, Category = "FPSR|HUD")
	int32 GetSharedXP() const;

	UFUNCTION(BlueprintPure, Category = "FPSR|HUD")
	int32 GetRequiredXPForNextLevel() const;

	/** Shared XP progress toward the next party level, 0..1. */
	UFUNCTION(BlueprintPure, Category = "FPSR|HUD")
	float GetXPProgress01() const;

	/** Local player's pending level-up card picks. */
	UFUNCTION(BlueprintPure, Category = "FPSR|HUD")
	int32 GetLocalCardPicksPending() const;

	/** Local player's pending mission-reward card picks. */
	UFUNCTION(BlueprintPure, Category = "FPSR|HUD")
	int32 GetLocalMissionRewardPicksPending() const;

private:
	bool bPlayerStateBound = false;

	/** Looping retry timer for the late-replicating PlayerState bind (cleared on bind success / destruct). */
	FTimerHandle PlayerStateBindRetryHandle;
};
