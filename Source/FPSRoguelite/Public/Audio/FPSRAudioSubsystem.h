// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "FPSRAudioSubsystem.generated.h"

/** World subsystem that applies the persisted master volume to the audio engine.
 *
 *  Why a WorldSubsystem (not GameInstance): SetSoundMixClassOverride/PushSoundMixModifier are world-scoped,
 *  and re-applying in OnWorldBeginPlay on every map (menu/lobby/run) is robust to audio-device recreation
 *  across level travel. Local setting → no replication, no server authority concern. (Game.md §2-14 audio,
 *  SoundSettings handoff "라이프사이클/엣지".)
 *
 *  The volume value itself lives in UFPSRGameUserSettings (persistence owner); this subsystem reads it and
 *  drives the SoundMix/SoundClass override resolved from UFPSRAudioSettings (soft refs, no hard-coded path). */
UCLASS()
class FPSROGUELITE_API UFPSRAudioSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Re-apply the persisted master volume whenever a world begins play (robust to device recreation). */
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	/** Set the master volume: persist it (UFPSRGameUserSettings) then apply it audibly. bSave=false skips the
	 *  disk write (live slider drag); pass true on release. UI / console entry point. */
	UFUNCTION(BlueprintCallable, Category = "FPSR|Audio")
	void SetMasterVolume(float InVolume, bool bSave = true);

	/** Current persisted master volume (0..1); 1.0 if settings are unavailable. */
	UFUNCTION(BlueprintPure, Category = "FPSR|Audio")
	float GetMasterVolume() const;

	/** Push the configured SoundMix with the current master volume as the SoundClass override. Safe no-op when
	 *  the SoundMix/SoundClass soft refs are unset or fail to load (so the build/smoke pass before the audio
	 *  content is authored) and on dedicated servers (no audio device). */
	void ApplyMasterVolume();
};
