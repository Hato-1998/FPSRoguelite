// Copyright Epic Games, Inc. All Rights Reserved.

#include "Hero/FPSRBlindspotAudioComponent.h"

#include "Enemy/FPSREnemyBase.h"
#include "Core/FPSRGameState.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundAttenuation.h"
#include "Engine/Attenuation.h"
#include "HAL/IConsoleManager.h"

UFPSRBlindspotAudioComponent::UFPSRBlindspotAudioComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	SetIsReplicatedByDefault(false); // local cosmetic audio only
}

void UFPSRBlindspotAudioComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!IsLocalView())
	{
		return;
	}

	ScanAccumulator += DeltaTime;
	if (ScanAccumulator < ScanInterval)
	{
		return;
	}
	ScanAccumulator = 0.0f;

	// Suppress during the global freeze (card selection / end-of-run, §2-2).
	const AFPSRGameState* GameState = GetWorld() ? GetWorld()->GetGameState<AFPSRGameState>() : nullptr;
	if (GameState && GameState->IsRunPaused())
	{
		return;
	}

	ScanAndWarn();
}

bool UFPSRBlindspotAudioComponent::IsLocalView() const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	return Pawn != nullptr && Pawn->IsLocallyControlled() && Pawn->IsPlayerControlled();
}

void UFPSRBlindspotAudioComponent::ScanAndWarn()
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	const APlayerController* PC = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	UWorld* World = GetWorld();
	if (PC == nullptr || World == nullptr)
	{
		return;
	}

	FVector ViewLocation;
	FRotator ViewRotation;
	PC->GetPlayerViewPoint(ViewLocation, ViewRotation);
	FVector Forward2D = ViewRotation.Vector();
	Forward2D.Z = 0.0f;
	if (!Forward2D.Normalize())
	{
		return;
	}

	const float RadiusSq = ThreatRadius * ThreatRadius;
	const float CosHalfAngle = FMath::Cos(FMath::DegreesToRadians(BlindspotHalfAngleDeg));

	// Nearest (3D) enemy that is a blind-spot threat: outside the forward HORIZONTAL cone (behind/side) OR steeply
	// ABOVE/BELOW the view plane (off the top/bottom of the screen). 3D distance so an enemy directly overhead — which
	// the old 2D-flattened cull saw as ~zero distance and then skipped (no horizontal direction) — is still found (B9).
	const AFPSREnemyBase* NearestThreat = nullptr;
	FVector NearestThreatDirH = Forward2D; // horizontal cue dir; fallback = in front for a directly-overhead threat
	float NearestElevationDeg = 0.0f;
	float NearestDistSq = RadiusSq;

	for (TActorIterator<AFPSREnemyBase> It(World); It; ++It)
	{
		const AFPSREnemyBase* Enemy = *It;
		if (Enemy == nullptr || Enemy->IsHidden()) // hidden == pooled/dormant (Deactivate), skip cheaply
		{
			continue;
		}

		const FVector ToEnemy = Enemy->GetActorLocation() - ViewLocation;
		const float DistSq = ToEnemy.SizeSquared(); // 3D: includes height
		if (DistSq > RadiusSq || DistSq >= NearestDistSq) // distance cull = Significance gate (§5-1)
		{
			continue;
		}

		// Split into the horizontal plane (panning + behind/side cone) and elevation (pitch).
		FVector ToEnemyH = ToEnemy;
		ToEnemyH.Z = 0.0f;
		const float HorizDist = ToEnemyH.Size();
		const float ElevationDeg = FMath::RadiansToDegrees(FMath::Atan2(ToEnemy.Z, FMath::Max(HorizDist, 1.0f)));

		// Horizontal blind spot: behind / to the side (only meaningful when there's a horizontal direction).
		bool bHorizontalBlindspot = false;
		FVector ToEnemyDirH = Forward2D;
		if (ToEnemyH.Normalize())
		{
			ToEnemyDirH = ToEnemyH;
			bHorizontalBlindspot = FVector::DotProduct(Forward2D, ToEnemyDirH) < CosHalfAngle;
		}
		// Vertical blind spot: steeply above/below regardless of horizontal position (off-screen vertically).
		const bool bVerticalBlindspot = FMath::Abs(ElevationDeg) > VerticalBlindspotAngleDeg;

		if (!bHorizontalBlindspot && !bVerticalBlindspot)
		{
			continue; // within the forward cone and not steeply off-screen — visible, not a threat
		}

		NearestThreat = Enemy;
		NearestThreatDirH = ToEnemyDirH;
		NearestElevationDeg = ElevationDeg;
		NearestDistSq = DistSq;
	}

	if (NearestThreat == nullptr)
	{
		return;
	}

	const float Now = World->GetTimeSeconds();
	if (Now - LastWarnTime < WarnCooldown)
	{
		return;
	}
	LastWarnTime = Now;

	// Pitch conveys elevation (stereo panning only covers the horizontal plane): lerp Below..Above by signed
	// elevation, so an overhead threat reads higher and one below reads lower (B9).
	const float ElevationAlpha = FMath::GetMappedRangeValueClamped(FVector2D(-90.0f, 90.0f), FVector2D(0.0f, 1.0f), NearestElevationDeg);
	const float PitchMultiplier = FMath::Lerp(BelowThreatPitch, AboveThreatPitch, ElevationAlpha);

	PlayWarningCue(ViewLocation, NearestThreatDirH, PitchMultiplier);
}

USoundAttenuation* UFPSRBlindspotAudioComponent::ResolveAttenuation()
{
	if (WarningAttenuation)
	{
		return WarningAttenuation; // designer override (e.g. HRTF) takes precedence
	}

	// Build a spatialized attenuation in code so the cue pans by direction even when WarningSound is a plain
	// 2D SoundWave (no attenuation of its own → otherwise it plays non-spatialized/centered and gives NO
	// directional cue at all). Cached and refreshed from the tunables each call (cheap struct copy).
	if (RuntimeAttenuation == nullptr)
	{
		RuntimeAttenuation = NewObject<USoundAttenuation>(this);
	}
	FSoundAttenuationSettings& Att = RuntimeAttenuation->Attenuation;
	Att.bAttenuate = true;
	Att.bSpatialize = true; // THE fix: enable 3D panning (left/right) relative to the listener
	Att.SpatializationAlgorithm = ESoundSpatializationAlgorithm::SPATIALIZATION_Default; // stereo panning
	Att.AttenuationShape = EAttenuationShape::Sphere;
	Att.AttenuationShapeExtents = FVector(CueDistance, 0.0f, 0.0f); // full volume within the cue distance
	Att.FalloffDistance = FMath::Max(SpatializeFalloffDistance, 1.0f);
	return RuntimeAttenuation;
}

void UFPSRBlindspotAudioComponent::PlayWarningCue(const FVector& ViewLocation, const FVector& ThreatDirection, float PitchMultiplier)
{
	if (WarningSound == nullptr)
	{
		return;
	}
	// Place the cue a fixed short distance toward the threat so the engine spatializes it relative to the listener
	// (left/right panning) at a consistent loudness regardless of the enemy's real distance. Pitch conveys elevation
	// (above/below) which stereo panning can't (B9). NOTE: front/back disambiguation needs headphones + HRTF (U13).
	const FVector CueLocation = ViewLocation + ThreatDirection * CueDistance;
	UGameplayStatics::SpawnSoundAtLocation(this, WarningSound, CueLocation, FRotator::ZeroRotator,
		1.0f, PitchMultiplier, 0.0f, ResolveAttenuation());
}

#if !UE_BUILD_SHIPPING
void UFPSRBlindspotAudioComponent::DebugForceWarn(float AngleDeg)
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	const APlayerController* PC = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	if (PC == nullptr)
	{
		return;
	}
	FVector ViewLocation;
	FRotator ViewRotation;
	PC->GetPlayerViewPoint(ViewLocation, ViewRotation);
	FVector Forward2D = ViewRotation.Vector();
	Forward2D.Z = 0.0f;
	if (!Forward2D.Normalize())
	{
		return;
	}
	const FVector Dir = Forward2D.RotateAngleAxis(AngleDeg, FVector::UpVector);
	LastWarnTime = -1000.0f; // bypass cooldown for the test
	PlayWarningCue(ViewLocation, Dir, 1.0f);
}

namespace
{
	FAutoConsoleCommandWithWorldAndArgs GCmdTestBlindspot(
		TEXT("FPSR.TestBlindspot"),
		TEXT("Test the blind-spot warning cue from the local view. Usage: FPSR.TestBlindspot [angleDeg] (default 180 = directly behind)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			const float Angle = Args.Num() > 0 ? FCString::Atof(*Args[0]) : 180.0f;
			APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
			APawn* Pawn = PC ? PC->GetPawn() : nullptr;
			if (Pawn)
			{
				if (UFPSRBlindspotAudioComponent* BS = Pawn->FindComponentByClass<UFPSRBlindspotAudioComponent>())
				{
					BS->DebugForceWarn(Angle);
				}
			}
		}));
}
#endif
