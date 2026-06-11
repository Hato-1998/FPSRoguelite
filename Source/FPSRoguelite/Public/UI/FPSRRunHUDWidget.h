// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CommonUserWidget.h"
#include "Core/FPSRGameState.h"
#include "FPSRRunHUDWidget.generated.h"

/** Passive run-state HUD base (Game layer). Exposes replicated run state (GameState) to WBP via BlueprintPure
 *  getters and fires OnRunStateUpdated whenever it changes. Event-driven: binds GameState OnRunStateChanged,
 *  no polling. Read-only mirror — input routing stays with the Game-layer widget. (Game.MD §2-2/§2-14).
 *  Pending card picks are NOT surfaced here: level-up immediately opens the card modal, so a count is redundant. */
UCLASS()
class FPSROGUELITE_API UFPSRRunHUDWidget : public UCommonUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UFUNCTION()
	void HandleRunStateChanged();

	/** WBP refresh hook: fired on construct and whenever run state changes. */
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
};
