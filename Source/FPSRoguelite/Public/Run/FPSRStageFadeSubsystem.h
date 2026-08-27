// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "Core/FPSRGameState.h" // EFPSRStageTransitionPhase (used by value in ComputeStageFadeAlpha, worldless)
#include "FPSRStageFadeSubsystem.generated.h"

class UMaterialInstanceDynamic;
class UFPSRStageFadeCameraModifier;
class APlayerController;

/**
 * Client-side cosmetic driver for the stage-transition fade (Phase B — the visual half of ADR 0010 D6's
 * FadeOut/Swapping/FadeIn phases; UFPSRStageDirectorSubsystem, Phase A, owns only the TIMING those phases run on).
 * Reads AFPSRGameState's replicated StageTransitionPhase/StagePhaseEndServerTime + the replicated run schedule's
 * StageFadeOutSeconds/StageFadeInSeconds and turns them into a 0..1 alpha applied to a full-screen post-process
 * material (StageFadePostProcessMaterial, UFPSRStageFadeSettings) on the local player's view every frame.
 *
 * Read-only cosmetic: this subsystem owns no gameplay state and writes nothing back to the server — it only
 * consumes already-replicated values, so it needs no server-authoritative counterpart.
 *
 * UTickableWorldSubsystem (not a plain WorldSubsystem) because the alpha has to update smoothly every frame, the
 * same shape as UFPSREnemyCosmeticLODSubsystem. Excluded entirely on a dedicated server via ShouldCreateSubsystem
 * (IsRunningDedicatedServer() — the same global-flag check UFPSREnemyCosmeticLODSubsystem uses, deliberately NOT
 * UWorld::GetNetMode(): NetMode is a per-NetDriver value that is not guaranteed to be settled yet by the time
 * subsystem creation runs, where IsRunningDedicatedServer() is a process-wide flag set at startup). A listen-server
 * host IS a local viewer and is deliberately still covered.
 */
UCLASS()
class FPSROGUELITE_API UFPSRStageFadeSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	//~ USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	//~ End USubsystem

	//~ FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual ETickableTickType GetTickableTickType() const override;
	virtual bool IsTickable() const override;
	virtual UWorld* GetTickableGameObjectWorld() const override;
	//~ End FTickableGameObject

	/** Pure, worldless alpha calc (unit-tested in FPSRStageTransitionTest.cpp, no world/GameState needed):
	 *   - FadeOut: End<=0 or FadeOutSeconds<=0 -> 1.0 (0-div guard, matches the hard-cut 0-length path); else
	 *     1 - Clamp((End-Now)/FadeOutSeconds, 0, 1) — ramps 0->1 as the window closes.
	 *   - Swapping: always 1.0 (screen stays fully faded through the destination-ready wait, however long it runs).
	 *   - FadeIn: End<=0 or FadeInSeconds<=0 -> 0.0; else Clamp((End-Now)/FadeInSeconds, 0, 1) — ramps 1->0.
	 *   - None/Grace/Pending: 0.0 (no transition in progress, or still in the dealing window — the fade hasn't
	 *     started yet). */
	static float ComputeStageFadeAlpha(EFPSRStageTransitionPhase Phase, float NowServerTime, float PhaseEndServerTime,
		float FadeOutSeconds, float FadeInSeconds);

private:
	/** Resolve UFPSRStageFadeSettings::StageFadePostProcessMaterial (soft ref) and build FadeMID from it. Called
	 *  once from Initialize — the material is small (a single full-screen PP pass), so one synchronous load at
	 *  subsystem startup is cheap and avoids a load stall on the first transition. Sets bMaterialLoadAttempted
	 *  either way; a failed resolve logs a Warning ONCE and every later Tick simply finds FadeMID null and no-ops
	 *  (Tick does not retry — a content-authoring gap should be visible once, not spammed every frame). */
	void EnsureFadeMaterialLoaded();

	/** Resolve (creating + installing on first use) the local player controller's stage-fade camera modifier.
	 *  Returns null if PC or its PlayerCameraManager isn't available yet (very first frames). */
	UFPSRStageFadeCameraModifier* GetOrCreateModifier(APlayerController* PC);

	/** StageFadeOutSeconds from the run schedule, or the same 0.8s fallback UFPSRStageDirectorSubsystem's
	 *  DefaultStageFadeOutSeconds uses when the run has no schedule asset — MUST match: the server computed
	 *  StagePhaseEndServerTime from that same fallback, and ComputeStageFadeAlpha's ramp is only linear if this
	 *  denominator agrees with the span the server actually used. */
	float GetStageFadeOutSeconds() const;

	/** FadeIn counterpart of GetStageFadeOutSeconds above (same 0.8s fallback, same reason). */
	float GetStageFadeInSeconds() const;

	/** GC-protected dynamic material instance driving the fade (its FadeAlpha scalar param is written every Tick
	 *  that alpha>0). Null until EnsureFadeMaterialLoaded resolves a source material. */
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> FadeMID;

	/** Set once Initialize's single load attempt has run (success or failure) — see EnsureFadeMaterialLoaded. */
	bool bMaterialLoadAttempted = false;

	/** The player controller CachedModifier was resolved against. Not a UPROPERTY (matches
	 *  UFPSREnemyCosmeticLODSubsystem::RegisteredEnemies' own weak-array convention) — a weak ref needs no GC
	 *  reflection to be invalidated safely; it is only ever compared against, never dereferenced without a
	 *  validity check. Re-resolved in GetOrCreateModifier whenever it stops matching GetFirstLocalPlayerController
	 *  (map travel, respawn re-possession). */
	TWeakObjectPtr<APlayerController> CachedPC;

	/** GC-protected camera modifier installed on CachedPC's PlayerCameraManager (AddNewCameraModifier). Refreshed
	 *  every Tick via SetFadeState rather than re-created — see GetOrCreateModifier. */
	UPROPERTY()
	TObjectPtr<UFPSRStageFadeCameraModifier> CachedModifier;

	/** Edge tracker for the once-per-transition engage/release log in Tick — logging every alpha>0 frame would be
	 *  spam, logging nothing made "fade never ran" and "fade ran but drew nothing" (the silently-uncompilable
	 *  material of the first live-fire PIE) indistinguishable. */
	bool bFadeEngaged = false;
};
