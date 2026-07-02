// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/GameUserSettings.h"
#include "FPSRGameUserSettings.generated.h"

/** Broadcast whenever CrosshairScale changes (live drag included). The HUD crosshair subscribes to rescale in
 *  real time; MasterVolume has no analogue because the audio subsystem applies volume immediately on set. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCrosshairSettingsChanged, float, NewScale);

/** Project UGameUserSettings subclass = the persistence owner for local audio (and future) settings.
 *  MasterVolume is a config float saved to Saved/Config/.../GameUserSettings.ini automatically.
 *
 *  Registered via DefaultEngine.ini [/Script/Engine.Engine] GameUserSettingsClassName (FSoftClassPath —
 *  the engine resolves it in UEngine::Init; the runtime TSubclassOf is GameUserSettings*Class*). This class
 *  only stores/loads the value; the audible apply is done by UFPSRAudioSubsystem so persistence and the
 *  SoundMix/SoundClass routing stay separated. Extensibility: per-category volumes are future fields here
 *  (no central change — Game.md extensibility-first). */
UCLASS()
class FPSROGUELITE_API UFPSRGameUserSettings : public UGameUserSettings
{
	GENERATED_BODY()

public:
	UFPSRGameUserSettings();

	/** Convenience accessor (returns the active instance cast to this type, or nullptr if not registered). */
	static UFPSRGameUserSettings* Get();

	/** Current master volume scalar (0..1). */
	UFUNCTION(BlueprintPure, Category = "FPSR|Audio")
	float GetMasterVolume() const { return MasterVolume; }

	/** Set + persist the master volume (clamped 0..1). Persistence only — call the audio subsystem to apply.
	 *  bSave=false lets a live drag update the value without a disk write per frame (save on release). */
	UFUNCTION(BlueprintCallable, Category = "FPSR|Audio")
	void SetMasterVolume(float InVolume, bool bSave = true);

	/** Current crosshair size multiplier (0.5 .. 2.5, 1.0 = default). */
	UFUNCTION(BlueprintPure, Category = "FPSR|Crosshair")
	float GetCrosshairScale() const { return CrosshairScale; }

	/** Set + persist the crosshair size multiplier (clamped 0.5..2.5). ALWAYS broadcasts
	 *  OnCrosshairSettingsChanged so a live drag rescales the HUD crosshair; bSave=false skips the disk
	 *  write until the drag is released. */
	UFUNCTION(BlueprintCallable, Category = "FPSR|Crosshair")
	void SetCrosshairScale(float InScale, bool bSave = true);

	/** Fires on every crosshair-scale change (live drag included). BlueprintAssignable for BP/content consumers. */
	UPROPERTY(BlueprintAssignable, Category = "FPSR|Crosshair")
	FOnCrosshairSettingsChanged OnCrosshairSettingsChanged;

protected:
	/** Master output volume scalar, 0 (mute) .. 1 (full). Persisted to GameUserSettings.ini. */
	UPROPERTY(config)
	float MasterVolume = 1.0f;

	/** Crosshair size multiplier, 0.5 (small) .. 2.5 (large), 1.0 = default. Persisted to GameUserSettings.ini. */
	UPROPERTY(config)
	float CrosshairScale = 1.0f;
};
