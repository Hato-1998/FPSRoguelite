// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"
#include "FPSRReviveComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnReviveProgressChanged, float, Progress);

/** Co-op proximity revive (U9 DBNO, Phase 1B, Game.MD §2-13). Lives on the player pawn (created by AFPSRCharacter).
 *  Server-authoritative: while the owner is DBNO (downed), if any ALIVE ally stands within ReviveRadius the revive
 *  gauge fills; full = revive back to Alive at ReviveHealthFraction of MaxHealth. Auto (no hold-interact). Paused
 *  during the global card-selection freeze (§2-2). ReviveProgress replicates so the downed player's HUD gauge (and a
 *  nearby ally's "reviving" prompt) can bind OnReviveProgressChanged. Solo / all-down never reaches a reviver, so the
 *  GameMode wipe (AFPSRGameMode::NotifyPlayerDefeated) ends the run in Defeat instead. */
UCLASS(ClassGroup = (FPSR), meta = (BlueprintSpawnableComponent))
class FPSROGUELITE_API UFPSRReviveComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UFPSRReviveComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** 0..1 revive gauge for the downed owner (meaningful only while the owner is DBNO; 0 otherwise). */
	UFUNCTION(BlueprintPure, Category = "FPSR|Revive")
	float GetReviveProgress() const { return ReviveProgress; }

	/** Fires on the host (direct) + clients (OnRep) whenever ReviveProgress changes — bind the HUD revive gauge here. */
	UPROPERTY(BlueprintAssignable, Category = "FPSR|Revive")
	FOnReviveProgressChanged OnReviveProgressChanged;

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void OnRep_ReviveProgress();

	/** Server: update + replicate the gauge (broadcasts the delegate; host has no OnRep). */
	void SetReviveProgress(float NewProgress);

	/** Server: gauge full — revive the owner to Alive at ReviveHealthFraction health and clear the gauge. */
	void PerformRevive();

	/** Radius (cm) within which an alive ally fills the gauge. Balance value. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Revive", meta = (ClampMin = "0.0"))
	float ReviveRadius = 300.0f;

	/** Seconds of continuous ally proximity to fully revive. Balance value. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Revive", meta = (ClampMin = "0.1"))
	float ReviveSeconds = 3.0f;

	/** With no ally near, the gauge decays this many times faster than it fills (0 = hold, don't decay). */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Revive", meta = (ClampMin = "0.0"))
	float ReviveDecayMultiplier = 1.0f;

	/** Health fraction (of MaxHealth) restored on revive. Balance value. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Revive", meta = (ClampMin = "0.01", ClampMax = "1.0"))
	float ReviveHealthFraction = 0.5f;

	UPROPERTY(ReplicatedUsing = OnRep_ReviveProgress)
	float ReviveProgress = 0.0f;
};
