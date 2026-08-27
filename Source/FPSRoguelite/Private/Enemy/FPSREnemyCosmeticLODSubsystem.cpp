// Copyright Epic Games, Inc. All Rights Reserved.

#include "Enemy/FPSREnemyCosmeticLODSubsystem.h"

#include "Enemy/FPSREnemyBase.h"
#include "Enemy/FPSREnemyTuning.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Settings/FPSREnemyRenderSettings.h"

void UFPSREnemyCosmeticLODSubsystem::RegisterEnemy(AFPSREnemyBase* Enemy)
{
	if (Enemy)
	{
		RegisteredEnemies.AddUnique(Enemy);
	}
}

void UFPSREnemyCosmeticLODSubsystem::UnregisterEnemy(AFPSREnemyBase* Enemy)
{
	if (Enemy)
	{
		RegisteredEnemies.RemoveSingleSwap(Enemy, EAllowShrinking::No);
	}
}

bool UFPSREnemyCosmeticLODSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}

	// A dedicated server renders nothing, so there is no cosmetic to budget and no local viewer to measure against.
	// Refuse creation outright rather than early-returning in Tick: no registration, no pass, no cost at all. (A
	// listen-server host IS a viewer and is deliberately not covered by this check.)
	//
	// Deliberately NOT gated on bEnableEnemyShadowLOD (as the shadow-only predecessor of this subsystem was, LOD1
	// §5-3 correction): three cosmetic consumers now share this one pass (shadow, health bar, anim freeze), so
	// gating CREATION on any single consumer's own switch would silently kill the OTHER two the moment that switch
	// is off. Concretely, the client anim freeze had no switch of its own at all (it was unconditional in
	// AFPSREnemyBase::PostNetReceiveLocationAndRotation) — turning OFF just the shadow LOD used to have zero effect
	// on it, and gating subsystem creation on that same flag now would make it disappear too. That is a regression,
	// not a no-op: each consumer gates itself independently inside Tick instead.
	return !IsRunningDedicatedServer();
}

ETickableTickType UFPSREnemyCosmeticLODSubsystem::GetTickableTickType() const
{
	// Never tick the CDO/template (matching UFPSREnemyMetricsSubsystem).
	return IsTemplate() ? ETickableTickType::Never : ETickableTickType::Conditional;
}

bool UFPSREnemyCosmeticLODSubsystem::IsTickable() const
{
	const UWorld* World = GetWorld();
	return World != nullptr && World->IsGameWorld() && RegisteredEnemies.Num() > 0;
}

UWorld* UFPSREnemyCosmeticLODSubsystem::GetTickableGameObjectWorld() const
{
	return GetWorld();
}

TStatId UFPSREnemyCosmeticLODSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UFPSREnemyCosmeticLODSubsystem, STATGROUP_Tickables);
}

void UFPSREnemyCosmeticLODSubsystem::Tick(float DeltaTime)
{
	const UFPSREnemyRenderSettings* Settings = GetDefault<UFPSREnemyRenderSettings>();
	if (!Settings)
	{
		return;
	}

	// LOD1 §12-7: one-time (first Tick only) radius-alignment note. An authored ini deliberately parting
	// ShadowCastRadius from the S0 significance radius is a legitimate content choice, not a bug — Log, never
	// ensure/Warning, so it never breaks a debugger or reads as an error.
	if (!bRadiusAlignmentLogged)
	{
		bRadiusAlignmentLogged = true;
		const float ShadowRadiusSq = FMath::Square(Settings->ShadowCastRadius);
		if (ShadowRadiusSq != FPSREnemyTuning::SignificanceS0RadiusSq)
		{
			UE_LOG(LogTemp, Log,
				TEXT("[CosmeticLOD] ShadowCastRadius (sq=%.0f) differs from FPSREnemyTuning::SignificanceS0RadiusSq ")
				TEXT("(sq=%.0f) — authored ini divergence from the S0 significance radius; informational only."),
				ShadowRadiusSq, FPSREnemyTuning::SignificanceS0RadiusSq);
		}
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

	// Shadow on/off radii — unchanged from the shadow-only era. Radius 0 is the documented "enemy shadows off
	// entirely" setting: every enemy lands outside the band below, so the same code path produces the blanket-off
	// behaviour without a special case.
	const float ShadowOnRadius = FMath::Max(Settings->ShadowCastRadius, 0.0f);
	const float ShadowOffRadius = ShadowOnRadius + FMath::Max(Settings->ShadowCastHysteresis, 0.0f);
	const float ShadowOnRadiusSq = FMath::Square(ShadowOnRadius);
	const float ShadowOffRadiusSq = FMath::Square(ShadowOffRadius);

	// Anim-freeze radius (LOD1 follow-up): read from config here, squared ONCE per pass, so no actor ever touches
	// UFPSREnemyRenderSettings. Unlike the two pairs around it this is a single threshold with NO hysteresis.
	//
	// Be honest about what that costs: an enemy sitting exactly on the boundary alternates frozen/unfrozen every
	// pass, so its animation visibly stutters (walk, hold, walk, hold at the pass rate). That is PRE-EXISTING
	// behaviour, not something this follow-up introduced — the client-only freeze this replaced had the same shape
	// (re-evaluated per net update against a bare radius, no hysteresis), so leaving it alone is the no-regression
	// choice and adding hysteresis here would be a new design decision, not a fix. It is survivable because the
	// artifact only exists in a razor-thin shell at the radius AND at that distance the animation is small on screen
	// — the very reason the freeze is affordable in the first place. If it ever reads badly at a SHORT authored
	// radius (e.g. the 5m observation setting), that is the signal to give this the same On/Off pair the other two
	// consumers have.
	const float AnimFreezeRadiusSq = FMath::Square(FMath::Max(Settings->AnimFreezeRadius, 0.0f));

	// Health-bar on/off radii (LOD1) — same shape as the shadow pair above.
	const float HealthBarOnRadius = FMath::Max(Settings->HealthBarVisibleRadius, 0.0f);
	const float HealthBarOffRadius = HealthBarOnRadius + FMath::Max(Settings->HealthBarHysteresis, 0.0f);
	const float HealthBarOnRadiusSq = FMath::Square(HealthBarOnRadius);
	const float HealthBarOffRadiusSq = FMath::Square(HealthBarOffRadius);

	for (int32 Index = RegisteredEnemies.Num() - 1; Index >= 0; --Index)
	{
		AFPSREnemyBase* Enemy = RegisteredEnemies[Index].Get();
		if (!Enemy)
		{
			// Compact stale weak entries here rather than relying on EndPlay ordering during teardown.
			RegisteredEnemies.RemoveAtSwap(Index, 1, EAllowShrinking::No);
			continue;
		}

		// A pooled-but-released enemy is hidden (AFPSREnemyBase::Deactivate), so it renders nothing and none of
		// this pass's consumers matter until the next Activate re-shows it — and that Activate happens inside the
		// band check below on a later pass anyway. A dwelling corpse (EnterDyingState, not yet Deactivate'd) is NOT
		// hidden, so it DOES reach the checks below — see SetViewerLOD's own death guard for why that is safe.
		if (Enemy->IsHidden())
		{
			continue;
		}

		// Distance computed ONCE per enemy per pass and shared by every consumer below (LOD1 §6-1) — 3D, matching
		// the shadow consumer's pre-existing metric. This is also what unifies the anim-freeze metric onto the same
		// 3D measure every other consumer here already used (it used to be a separate client-only XY measure — see
		// FPSREnemyTuning::AnimFreezeRadiusSq's own comment and LOD1 §6-1's metric note).
		const float DistSq = FVector::DistSquared(Enemy->GetActorLocation(), ViewerLocation);

		Enemy->SetViewerLOD(FPSREnemyTuning::ClassifyBand(DistSq), DistSq, AnimFreezeRadiusSq);

		if (Settings->bEnableEnemyShadowLOD)
		{
			if (const UStaticMeshComponent* EnemyMesh = Enemy->GetMesh())
			{
				// Hysteresis: an enemy that is already casting keeps its shadow out to the wider radius, so one
				// walking the boundary does not strobe on and off every pass.
				const bool bWasCasting = EnemyMesh->CastShadow;
				Enemy->SetShadowCasting(
					FPSREnemyTuning::ApplyRadiusHysteresis(bWasCasting, DistSq, ShadowOnRadiusSq, ShadowOffRadiusSq));
			}
		}

		if (Settings->bEnableHealthBarLOD)
		{
			const bool bWasInRange = Enemy->IsHealthBarInRange();
			Enemy->SetHealthBarInRange(
				FPSREnemyTuning::ApplyRadiusHysteresis(bWasInRange, DistSq, HealthBarOnRadiusSq, HealthBarOffRadiusSq));
		}
	}
}
