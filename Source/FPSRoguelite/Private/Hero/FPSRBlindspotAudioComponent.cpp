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

	// Find the nearest enemy that is close AND outside the forward view cone (behind/side = blind spot).
	const AFPSREnemyBase* NearestThreat = nullptr;
	FVector NearestThreatDir2D = FVector::ZeroVector;
	float NearestDistSq = RadiusSq;

	for (TActorIterator<AFPSREnemyBase> It(World); It; ++It)
	{
		const AFPSREnemyBase* Enemy = *It;
		if (Enemy == nullptr || Enemy->IsHidden()) // hidden == pooled/dormant (Deactivate), skip cheaply
		{
			continue;
		}

		FVector ToEnemy = Enemy->GetActorLocation() - ViewLocation;
		ToEnemy.Z = 0.0f;
		const float DistSq = ToEnemy.SizeSquared();
		if (DistSq > RadiusSq || DistSq >= NearestDistSq) // distance cull = Significance gate (§5-1)
		{
			continue;
		}

		FVector ToEnemyDir = ToEnemy;
		if (!ToEnemyDir.Normalize())
		{
			continue; // enemy on top of the player — no meaningful direction
		}

		// Outside the forward cone? (dot < cos(halfAngle) means the angle exceeds the half-angle = blind spot).
		if (FVector::DotProduct(Forward2D, ToEnemyDir) >= CosHalfAngle)
		{
			continue; // in front / within view — not a blind-spot threat
		}

		NearestThreat = Enemy;
		NearestThreatDir2D = ToEnemyDir;
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

	PlayWarningCue(ViewLocation, NearestThreatDir2D);
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

void UFPSRBlindspotAudioComponent::PlayWarningCue(const FVector& ViewLocation, const FVector& ThreatDirection)
{
	if (WarningSound == nullptr)
	{
		return;
	}
	// Place the cue a fixed short distance toward the threat so the engine spatializes it relative to the
	// listener (left/right panning) at a consistent loudness regardless of the enemy's real distance. NOTE:
	// stereo panning resolves left/right reliably; front/back disambiguation needs headphones + HRTF (U13).
	const FVector CueLocation = ViewLocation + ThreatDirection * CueDistance;
	UGameplayStatics::SpawnSoundAtLocation(this, WarningSound, CueLocation, FRotator::ZeroRotator,
		1.0f, 1.0f, 0.0f, ResolveAttenuation());
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
	PlayWarningCue(ViewLocation, Dir);
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
