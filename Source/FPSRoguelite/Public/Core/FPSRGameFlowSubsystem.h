// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Core/FPSRGameFlowTypes.h"
#include "FPSRGameFlowSubsystem.generated.h"

/** Manages game flow: menu → run transitions and run outcomes (victory/defeat).
 *  Lives on the GameInstance; callable from PC/UI and by the run director.
 *  Server-authoritative: only travels on the authority (standalone/listen server). */
UCLASS()
class FPSROGUELITE_API UFPSRGameFlowSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** Travel to the run map (from the menu). Clears any stale outcome from the prior run. */
	UFUNCTION(BlueprintCallable, Category = "FPSR|Flow")
	void StartRun();

	/** Travel to the menu map after a run completes, caching the outcome for menu UI.
	 *  Must be called on the authority (server/host). */
	UFUNCTION(BlueprintCallable, Category = "FPSR|Flow")
	void ReturnToMenu(EFPSRRunOutcome Outcome);

	/** Get the cached outcome from the last run (used by menu/result UI to show victory/defeat state). */
	UFUNCTION(BlueprintPure, Category = "FPSR|Flow")
	EFPSRRunOutcome GetLastRunOutcome() const { return LastRunOutcome; }

	/** Reset the cached outcome (called at the start of each run). */
	UFUNCTION(BlueprintCallable, Category = "FPSR|Flow")
	void ClearLastRunOutcome() { LastRunOutcome = EFPSRRunOutcome::None; }

private:
	/** Cached outcome of the last completed run; persists across the travel to the menu. */
	UPROPERTY(Transient)
	EFPSRRunOutcome LastRunOutcome = EFPSRRunOutcome::None;
};
