// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/Button.h"
#include "FPSRColorPresetButton.generated.h"

/** Native (payload-capable) click event carrying the clicked swatch's preset index. */
DECLARE_MULTICAST_DELEGATE_OneParam(FFPSRColorPresetClicked, int32 /*PresetIndex*/);

/** A crosshair colour swatch button, generated at runtime (one per entry in UFPSRCrosshairColorPresetDataAsset).
 *
 *  Why a UButton subclass: UButton::OnClicked is a DYNAMIC delegate with no sender/payload, so a widget that
 *  spawns N identical buttons cannot tell which one fired. This subclass carries its own index and re-broadcasts
 *  through a NATIVE delegate that does pass it. That keeps the swatch row fully data-driven with NO extra WBP
 *  asset — the settings widget only needs a container panel to parent these into. */
UCLASS()
class FPSROGUELITE_API UFPSRColorPresetButton : public UButton
{
	GENERATED_BODY()

public:
	/** Fires with this button's PresetIndex when clicked. */
	FFPSRColorPresetClicked OnPresetClicked;

	/** Store the index and wire the inherited dynamic OnClicked to the native event. Call once after construction. */
	void InitPresetButton(int32 InPresetIndex);

	/** Index into the owning preset asset's Presets array (INDEX_NONE until InitPresetButton runs). */
	int32 GetPresetIndex() const { return PresetIndex; }

private:
	UFUNCTION()
	void HandleClickedInternal();

	int32 PresetIndex = INDEX_NONE;
};
