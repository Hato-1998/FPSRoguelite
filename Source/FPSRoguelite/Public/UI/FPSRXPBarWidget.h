// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CommonActivatableWidget.h"
#include "FPSRXPBarWidget.generated.h"

class UProgressBar;
class UTextBlock;

/** Shared XP HUD (Game layer). Event-driven: binds to GameState OnRunStateChanged (shared XP/level)
 *  and the local PlayerState OnCardPicksChanged (pending picks). No polling. Placeholder layout (Game.MD §2-2/§3). */
UCLASS()
class FPSROGUELITE_API UFPSRXPBarWidget : public UCommonActivatableWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	/** HUD layer = game-playing state: capture mouse for look, hide cursor, route input to the game.
	 *  As the persistent Game-layer widget, this also restores game input when a modal above it (e.g. the
	 *  card-select widget's Menu config) is dismissed. (P4: a dedicated HUD-layout widget can own this.) */
	virtual TOptional<FUIInputConfig> GetDesiredInputConfig() const override;

	UFUNCTION()
	void HandleRunStateChanged();

	UFUNCTION()
	void HandleCardPicksChanged();

	/** Refresh all bound widgets from current run state + local pending picks. */
	void UpdateDisplay();

	/** Lazily bind the owning PlayerState's pick delegate once it replicates (remote-client timing).
	 *  Re-attempted from run-state events so the pending-pick display can't stay stale. */
	void EnsurePlayerStateBound();

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> XPBar;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> LevelText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> StackText;

private:
	bool bPlayerStateBound = false;
};
