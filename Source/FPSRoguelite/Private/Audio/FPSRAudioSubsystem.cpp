// Copyright Epic Games, Inc. All Rights Reserved.

#include "Audio/FPSRAudioSubsystem.h"
#include "Settings/FPSRGameUserSettings.h"
#include "Settings/FPSRAudioSettings.h"
#include "Core/FPSRLogChannels.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundMix.h"
#include "Sound/SoundClass.h"
#include "Engine/World.h"

void UFPSRAudioSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	ApplyMasterVolume();
}

float UFPSRAudioSubsystem::GetMasterVolume() const
{
	const UFPSRGameUserSettings* Settings = UFPSRGameUserSettings::Get();
	return Settings ? Settings->GetMasterVolume() : 1.0f;
}

void UFPSRAudioSubsystem::SetMasterVolume(float InVolume, bool bSave)
{
	if (UFPSRGameUserSettings* Settings = UFPSRGameUserSettings::Get())
	{
		Settings->SetMasterVolume(InVolume, bSave);
	}
	ApplyMasterVolume();
}

void UFPSRAudioSubsystem::ApplyMasterVolume()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Dedicated servers have no audio device — nothing to apply (listen-server hosts DO have one).
	if (World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	const UFPSRAudioSettings* AudioSettings = GetDefault<UFPSRAudioSettings>();
	if (!AudioSettings)
	{
		return;
	}

	// Soft refs are resolved on demand (LoadSynchronous) — the assets are authored as content; until they
	// exist this safely no-ops so the build/smoke stay green (Codex plan gate BLOCKER 2).
	USoundMix* SoundMix = AudioSettings->MasterSoundMix.LoadSynchronous();
	USoundClass* SoundClass = AudioSettings->MasterSoundClass.LoadSynchronous();
	if (!SoundMix || !SoundClass)
	{
		UE_LOG(LogFPSR, Verbose,
			TEXT("[Audio] Master SoundMix/SoundClass not set — master volume not applied (configure FPSR Audio settings)."));
		return;
	}

	const float Volume = GetMasterVolume();

	// Engine signature (UE5.7, GameplayStatics.h): SetSoundMixClassOverride(World, Mix, Class, Volume, Pitch,
	// FadeInTime, bApplyToChildren). FadeInTime=0 = immediate; bApplyToChildren=true so child classes inherit.
	UGameplayStatics::SetSoundMixClassOverride(World, SoundMix, SoundClass, Volume, 1.0f, 0.0f, true);
	UGameplayStatics::PushSoundMixModifier(World, SoundMix);
}

#if !UE_BUILD_SHIPPING
// Debug console: FPSR.SetMasterVolume <0..1> — resolves the world's audio subsystem and sets the volume.
static FAutoConsoleCommandWithWorldAndArgs GFPSRSetMasterVolumeCmd(
	TEXT("FPSR.SetMasterVolume"),
	TEXT("Set the master volume scalar 0..1 (persists + applies). Usage: FPSR.SetMasterVolume 0.5"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
		[](const TArray<FString>& Args, UWorld* World)
		{
			if (!World)
			{
				return;
			}
			if (Args.Num() < 1)
			{
				if (const UFPSRAudioSubsystem* Sub = World->GetSubsystem<UFPSRAudioSubsystem>())
				{
					UE_LOG(LogFPSR, Display, TEXT("[Audio] MasterVolume = %.2f"), Sub->GetMasterVolume());
				}
				return;
			}
			if (UFPSRAudioSubsystem* Sub = World->GetSubsystem<UFPSRAudioSubsystem>())
			{
				const float Vol = FCString::Atof(*Args[0]);
				Sub->SetMasterVolume(Vol, true);
				UE_LOG(LogFPSR, Display, TEXT("[Audio] MasterVolume set to %.2f"), Sub->GetMasterVolume());
			}
		}));
#endif
