// Copyright Epic Games, Inc. All Rights Reserved.

#include "Run/FPSRStageFadeSubsystem.h"

#include "Run/FPSRStageFadeCameraModifier.h"
#include "Run/FPSRRunScheduleDataAsset.h"
#include "Settings/FPSRStageFadeSettings.h"
#include "Core/FPSRGameState.h"
#include "Core/FPSRLogChannels.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

namespace FPSRStageFade
{
	// Mirrors UFPSRStageDirectorSubsystem::DefaultStageFadeOutSeconds/DefaultStageFadeInSeconds (private to that
	// class) — MUST stay equal: the server falls back to that constant when a run has no schedule asset, and this
	// client-side driver has to divide by the exact same span or the fade ramp desyncs from the server's actual
	// phase timing (see GetStageFadeOutSeconds/GetStageFadeInSeconds header comments). Also matches
	// UFPSRRunScheduleDataAsset::StageFadeOutSeconds/StageFadeInSeconds' own authored default, so an asset-less
	// run and a freshly-authored schedule look identical.
	constexpr float DefaultStageFadeOutSeconds = 0.8f;
	constexpr float DefaultStageFadeInSeconds = 0.8f;
}

void UFPSRStageFadeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	EnsureFadeMaterialLoaded();
}

bool UFPSRStageFadeSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}

	// A dedicated server renders nothing and has no local viewer to fade — refuse creation outright (no
	// registration, no Tick, no cost at all), same reasoning as UFPSREnemyShadowLODSubsystem::ShouldCreateSubsystem.
	// IsRunningDedicatedServer() is a process-wide flag settled at startup, unlike UWorld::GetNetMode() which is a
	// per-NetDriver value not guaranteed to be resolved yet at subsystem-creation time. A listen-server host IS a
	// viewer and is deliberately not covered by this check.
	return !IsRunningDedicatedServer();
}

ETickableTickType UFPSRStageFadeSubsystem::GetTickableTickType() const
{
	// Never tick the CDO/template (matches UFPSREnemyShadowLODSubsystem/UFPSREnemyMetricsSubsystem).
	return IsTemplate() ? ETickableTickType::Never : ETickableTickType::Conditional;
}

bool UFPSRStageFadeSubsystem::IsTickable() const
{
	const UWorld* World = GetWorld();
	return World != nullptr && World->IsGameWorld();
}

UWorld* UFPSRStageFadeSubsystem::GetTickableGameObjectWorld() const
{
	return GetWorld();
}

TStatId UFPSRStageFadeSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UFPSRStageFadeSubsystem, STATGROUP_Tickables);
}

void UFPSRStageFadeSubsystem::EnsureFadeMaterialLoaded()
{
	if (bMaterialLoadAttempted)
	{
		return;
	}
	bMaterialLoadAttempted = true;

	const UFPSRStageFadeSettings* Settings = GetDefault<UFPSRStageFadeSettings>();
	UMaterialInterface* SourceMaterial = Settings ? Settings->StageFadePostProcessMaterial.LoadSynchronous() : nullptr;
	if (!SourceMaterial)
	{
		UE_LOG(LogFPSR, Warning,
			TEXT("[StageFade] StageFadePostProcessMaterial not set/resolved — stage-transition fade will not render ")
			TEXT("(configure FPSR Stage Fade settings). Transition TIMING (UFPSRStageDirectorSubsystem) is unaffected."));
		return;
	}

	FadeMID = UMaterialInstanceDynamic::Create(SourceMaterial, this);
}

float UFPSRStageFadeSubsystem::GetStageFadeOutSeconds() const
{
	const UWorld* World = GetWorld();
	const AFPSRGameState* GS = World ? World->GetGameState<AFPSRGameState>() : nullptr;
	const UFPSRRunScheduleDataAsset* Schedule = GS ? GS->GetRunSchedule() : nullptr;
	return Schedule ? Schedule->StageFadeOutSeconds : FPSRStageFade::DefaultStageFadeOutSeconds;
}

float UFPSRStageFadeSubsystem::GetStageFadeInSeconds() const
{
	const UWorld* World = GetWorld();
	const AFPSRGameState* GS = World ? World->GetGameState<AFPSRGameState>() : nullptr;
	const UFPSRRunScheduleDataAsset* Schedule = GS ? GS->GetRunSchedule() : nullptr;
	return Schedule ? Schedule->StageFadeInSeconds : FPSRStageFade::DefaultStageFadeInSeconds;
}

float UFPSRStageFadeSubsystem::ComputeStageFadeAlpha(EFPSRStageTransitionPhase Phase, float NowServerTime,
	float PhaseEndServerTime, float FadeOutSeconds, float FadeInSeconds)
{
	switch (Phase)
	{
	case EFPSRStageTransitionPhase::FadeOut:
		// 0-length fade (PhaseEndServerTime<=0, the hard-cut path — EnterFadeOut publishes End=0 and falls straight
		// through) or a defensive 0-div guard on FadeOutSeconds: both read as "already fully faded".
		if (PhaseEndServerTime <= 0.0f || FadeOutSeconds <= 0.0f)
		{
			return 1.0f;
		}
		return 1.0f - FMath::Clamp((PhaseEndServerTime - NowServerTime) / FadeOutSeconds, 0.0f, 1.0f);

	case EFPSRStageTransitionPhase::Swapping:
		// Screen stays fully faded through the destination-ready wait (BeginSwap/PollSwapReadiness), however long
		// that ends up taking — Phase A's own header comment: "the world stays visually blacked out the whole time".
		return 1.0f;

	case EFPSRStageTransitionPhase::FadeIn:
		if (PhaseEndServerTime <= 0.0f || FadeInSeconds <= 0.0f)
		{
			return 0.0f;
		}
		return FMath::Clamp((PhaseEndServerTime - NowServerTime) / FadeInSeconds, 0.0f, 1.0f);

	case EFPSRStageTransitionPhase::None:
	case EFPSRStageTransitionPhase::Grace:
	case EFPSRStageTransitionPhase::Pending:
	default:
		// Grace/Pending: the dealing window (or the wait for the card freeze to clear) hasn't reached FadeOut yet —
		// the swarm-freeze gates (IsMovementFrozen etc.) are already active by then, but the visual fade has not
		// started. None: no transition in progress.
		return 0.0f;
	}
}

UFPSRStageFadeCameraModifier* UFPSRStageFadeSubsystem::GetOrCreateModifier(APlayerController* PC)
{
	if (!PC || !PC->PlayerCameraManager)
	{
		return nullptr;
	}

	if (CachedPC.Get() == PC && CachedModifier)
	{
		return CachedModifier;
	}

	// PC changed (first use, or the local controller was swapped out from under us — map travel, respawn
	// re-possession). Reuse an already-installed modifier of this class if one somehow exists (defensive; in
	// practice the AddNewCameraModifier branch below is the only place that ever adds one), otherwise install one.
	UFPSRStageFadeCameraModifier* Existing = Cast<UFPSRStageFadeCameraModifier>(
		PC->PlayerCameraManager->FindCameraModifierByClass(UFPSRStageFadeCameraModifier::StaticClass()));
	UFPSRStageFadeCameraModifier* Modifier = Existing ? Existing
		: Cast<UFPSRStageFadeCameraModifier>(
			PC->PlayerCameraManager->AddNewCameraModifier(UFPSRStageFadeCameraModifier::StaticClass()));

	CachedPC = PC;
	CachedModifier = Modifier;
	return Modifier;
}

void UFPSRStageFadeSubsystem::Tick(float DeltaTime)
{
	UWorld* World = GetWorld();
	AFPSRGameState* GS = World ? World->GetGameState<AFPSRGameState>() : nullptr;
	if (!GS)
	{
		return;
	}

	const float Alpha = ComputeStageFadeAlpha(
		GS->GetStageTransitionPhase(),
		GS->GetServerWorldTimeSeconds(),
		GS->GetStagePhaseEndServerTime(),
		GetStageFadeOutSeconds(),
		GetStageFadeInSeconds());

	if (Alpha <= 0.0f || !FadeMID)
	{
		// Nothing to show this frame (or the material never resolved, EnsureFadeMaterialLoaded already warned once).
		// If a modifier from an earlier higher-alpha frame is still installed, tell it to drop the blendable rather
		// than leaving the last non-zero fade stuck on screen (Phase B spec: alpha==0 => no blendable applied).
		if (CachedModifier)
		{
			CachedModifier->SetFadeState(nullptr, 0.0f);
		}
		if (bFadeEngaged)
		{
			bFadeEngaged = false;
			UE_LOG(LogFPSR, Log, TEXT("[StageFade] fade released."));
		}
		return;
	}

	// Edge log, once per transition (same observability rationale as the destructible hit/broken logs): without it,
	// "the fade never rendered" and "the fade rendered but the material drew nothing" (the first live-fire failure —
	// a silently uncompilable material) are indistinguishable in a PIE log.
	if (!bFadeEngaged)
	{
		bFadeEngaged = true;
		UE_LOG(LogFPSR, Log, TEXT("[StageFade] fade engaged (phase %d, alpha %.2f)."),
			static_cast<int32>(GS->GetStageTransitionPhase()), Alpha);
	}

	// GetFirstLocalPlayerController, NOT UWorld::GetFirstPlayerController: on a listen server the latter's list
	// also holds every REMOTE client's controller (Engine.h), which would fade a host's screen off a remote
	// player's transition state. Same reasoning as UFPSREnemyMetricsSubsystem/UFPSREnemyShadowLODSubsystem.
	APlayerController* PC = GEngine ? GEngine->GetFirstLocalPlayerController(World) : nullptr;
	UFPSRStageFadeCameraModifier* Modifier = GetOrCreateModifier(PC);
	if (!Modifier)
	{
		return;
	}

	FadeMID->SetScalarParameterValue(TEXT("FadeAlpha"), Alpha);
	Modifier->SetFadeState(FadeMID, Alpha);
}
