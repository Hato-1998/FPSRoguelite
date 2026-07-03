// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/GameUserSettings.h"
#include "FPSRGameUserSettings.generated.h"

/** Broadcast whenever a crosshair appearance setting (color / thickness) changes, live preview included. The HUD
 *  crosshair subscribes and re-reads the settings to re-apply them to its material in real time. MasterVolume
 *  has no analogue because the audio subsystem applies volume immediately on set. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnCrosshairSettingsChanged);

/** Project UGameUserSettings subclass = the persistence owner for local settings (audio + crosshair appearance).
 *  Values are config fields saved to Saved/Config/.../GameUserSettings.ini automatically.
 *
 *  Registered via DefaultEngine.ini [/Script/Engine.Engine] GameUserSettingsClassName. This class only
 *  stores/loads values; the audible apply is done by UFPSRAudioSubsystem and the crosshair apply by the HUD
 *  widget, so persistence stays separated from the systems that consume the values. Extensibility: new local
 *  settings are future fields here (no central change — Game.md extensibility-first).
 *
 *  Crosshair size is intentionally NOT a setting: the crosshair is truthful (its spread == the weapon's actual
 *  dispersion cone projected to screen), so only appearance (color / thickness) is player-adjustable. */
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

	/** Current crosshair fill color (RGBA). */
	UFUNCTION(BlueprintPure, Category = "FPSR|Crosshair")
	FLinearColor GetCrosshairColor() const { return CrosshairColor; }

	/** Set + persist the crosshair color. ALWAYS broadcasts OnCrosshairSettingsChanged so the HUD re-applies it
	 *  live; bSave=false skips the disk write (e.g. a live preview) until committed. */
	UFUNCTION(BlueprintCallable, Category = "FPSR|Crosshair")
	void SetCrosshairColor(FLinearColor InColor, bool bSave = true);

	/** Current crosshair thickness multiplier (0.5 .. 2.0, 1.0 = default). */
	UFUNCTION(BlueprintPure, Category = "FPSR|Crosshair")
	float GetCrosshairThickness() const { return CrosshairThickness; }

	/** Set + persist the crosshair thickness multiplier (clamped 0.5..2.0). ALWAYS broadcasts
	 *  OnCrosshairSettingsChanged so a live drag re-applies to the HUD; bSave=false skips the disk write. */
	UFUNCTION(BlueprintCallable, Category = "FPSR|Crosshair")
	void SetCrosshairThickness(float InThickness, bool bSave = true);

	/** Fires on every crosshair appearance change (color / thickness, live preview included). BlueprintAssignable
	 *  for BP/content consumers. */
	UPROPERTY(BlueprintAssignable, Category = "FPSR|Crosshair")
	FOnCrosshairSettingsChanged OnCrosshairSettingsChanged;

protected:
	/** Master output volume scalar, 0 (mute) .. 1 (full). Persisted to GameUserSettings.ini. */
	UPROPERTY(config)
	float MasterVolume = 1.0f;

	/** Crosshair fill color (RGBA), default opaque white. Persisted to GameUserSettings.ini. */
	UPROPERTY(config)
	FLinearColor CrosshairColor = FLinearColor::White;

	/** Crosshair thickness multiplier, 0.5 (thin) .. 2.0 (thick), 1.0 = default. Persisted to GameUserSettings.ini. */
	UPROPERTY(config)
	float CrosshairThickness = 1.0f;
};
