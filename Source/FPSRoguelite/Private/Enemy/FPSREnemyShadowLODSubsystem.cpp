// Copyright Epic Games, Inc. All Rights Reserved.

#include "Enemy/FPSREnemyShadowLODSubsystem.h"

#include "Enemy/FPSREnemyBase.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Settings/FPSREnemyRenderSettings.h"

void UFPSREnemyShadowLODSubsystem::RegisterEnemy(AFPSREnemyBase* Enemy)
{
	if (Enemy)
	{
		RegisteredEnemies.AddUnique(Enemy);
	}
}

void UFPSREnemyShadowLODSubsystem::UnregisterEnemy(AFPSREnemyBase* Enemy)
{
	if (Enemy)
	{
		RegisteredEnemies.RemoveSingleSwap(Enemy, EAllowShrinking::No);
	}
}

bool UFPSREnemyShadowLODSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}

	// A dedicated server renders nothing, so there is no shadow to budget and no local viewer to measure against.
	// Refuse creation outright rather than early-returning in Tick: no registration, no pass, no cost at all. (A
	// listen-server host IS a viewer and is deliberately not covered by this check.)
	if (IsRunningDedicatedServer())
	{
		return false;
	}

	const UFPSREnemyRenderSettings* Settings = GetDefault<UFPSREnemyRenderSettings>();
	return Settings && Settings->bEnableEnemyShadowLOD;
}

ETickableTickType UFPSREnemyShadowLODSubsystem::GetTickableTickType() const
{
	// Never tick the CDO/template (matching UFPSREnemyMetricsSubsystem).
	return IsTemplate() ? ETickableTickType::Never : ETickableTickType::Conditional;
}

bool UFPSREnemyShadowLODSubsystem::IsTickable() const
{
	const UWorld* World = GetWorld();
	return World != nullptr && World->IsGameWorld() && RegisteredEnemies.Num() > 0;
}

UWorld* UFPSREnemyShadowLODSubsystem::GetTickableGameObjectWorld() const
{
	return GetWorld();
}

TStatId UFPSREnemyShadowLODSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UFPSREnemyShadowLODSubsystem, STATGROUP_Tickables);
}

void UFPSREnemyShadowLODSubsystem::Tick(float DeltaTime)
{
	const UFPSREnemyRenderSettings* Settings = GetDefault<UFPSREnemyRenderSettings>();
	if (!Settings)
	{
		return;
	}

	TimeSinceLastPass += DeltaTime;
	if (TimeSinceLastPass < Settings->ShadowUpdateInterval)
	{
		return;
	}
	TimeSinceLastPass = 0.0f;

	UWorld* World = GetWorld();

	// Measure from THIS machine's viewer. The pawn (not the camera) is the anchor so the band does not breathe with
	// look direction or ADS, and so a spectating / no-pawn frame simply skips rather than measuring from the origin.
	const APlayerController* PC = GEngine ? GEngine->GetFirstLocalPlayerController(World) : nullptr;
	const APawn* LocalPawn = PC ? PC->GetPawn() : nullptr;
	if (!LocalPawn)
	{
		return;
	}
	const FVector ViewerLocation = LocalPawn->GetActorLocation();

	// Radius 0 is the documented "enemy shadows off entirely" setting: every enemy lands outside the band below, so
	// the same code path produces the blanket-off behaviour without a special case.
	const float OnRadius = FMath::Max(Settings->ShadowCastRadius, 0.0f);
	const float OffRadius = OnRadius + FMath::Max(Settings->ShadowCastHysteresis, 0.0f);
	const float OnRadiusSq = FMath::Square(OnRadius);
	const float OffRadiusSq = FMath::Square(OffRadius);

	for (int32 Index = RegisteredEnemies.Num() - 1; Index >= 0; --Index)
	{
		AFPSREnemyBase* Enemy = RegisteredEnemies[Index].Get();
		if (!Enemy)
		{
			// Compact stale weak entries here rather than relying on EndPlay ordering during teardown.
			RegisteredEnemies.RemoveAtSwap(Index, 1, EAllowShrinking::No);
			continue;
		}

		// A pooled-but-released enemy is hidden (AFPSREnemyBase::Release), so it renders nothing and its shadow flag
		// is irrelevant until the next acquire re-shows it — and that acquire happens inside the band check below on
		// a later pass anyway.
		if (Enemy->IsHidden())
		{
			continue;
		}

		const UStaticMeshComponent* EnemyMesh = Enemy->GetMesh();
		if (!EnemyMesh)
		{
			continue;
		}

		// Hysteresis: an enemy that is already casting keeps its shadow out to the wider radius, so one walking the
		// boundary does not strobe on and off every pass.
		const bool bCasting = EnemyMesh->CastShadow;
		const float ThresholdSq = bCasting ? OffRadiusSq : OnRadiusSq;
		const float DistSq = FVector::DistSquared(Enemy->GetActorLocation(), ViewerLocation);

		Enemy->SetShadowCasting(DistSq <= ThresholdSq);
	}
}
