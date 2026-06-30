// Copyright Epic Games, Inc. All Rights Reserved.

#include "Enemy/FPSRRangedEnemyBase.h"
#include "Enemy/FPSREnemySpawnSubsystem.h"
#include "Weapon/FPSRProjectile.h"
#include "Weapon/FPSRProjectileSubsystem.h"
#include "Weapon/FPSRProjectileTypes.h"
#include "Hero/FPSRCharacter.h"
#include "Core/FPSRPlayerController.h"
#include "Core/FPSRLogChannels.h"
#include "FPSRCollisionChannels.h"

#include "Engine/World.h"
#include "CollisionQueryParams.h"

AFPSRRangedEnemyBase::AFPSRRangedEnemyBase()
{
	// Ranged enemies hold at distance to shoot rather than closing to melee: stop advancing further out (within the
	// engage range so they stop, then charge). Tunable per archetype in the BP child.
	StopDistance = 900.0f;
}

void AFPSRRangedEnemyBase::Activate(const FVector& Location)
{
	Super::Activate(Location);
	// Fresh reuse: any prior hold was released by the matching Deactivate; clear defensively so no stale warning/token
	// leaks into the new life, then reset the cycle.
	bHoldingToken = false;
	HeldTargetPC = nullptr;
	ResetRangedCycle();
}

void AFPSRRangedEnemyBase::Deactivate()
{
	// Pool release / death / kill-Z recycle all route here — close the warning + release the token on EVERY teardown
	// path (not just an explicit abort) so a Reliable 'off' is never dropped and the concurrency count never leaks.
	ReleaseRangedHold();
	ResetRangedCycle();
	Super::Deactivate();
}

void AFPSRRangedEnemyBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ReleaseRangedHold();
	Super::EndPlay(EndPlayReason);
}

EFPSRServerAttackResult AFPSRRangedEnemyBase::ServerTickAttack(const FFPSRServerAttackContext& Ctx)
{
	// The subsystem already early-returns the whole pass while the run is frozen, so DeltaSeconds only accrues during
	// active gameplay — the charge/cooldown accumulators below are freeze-paused for free. Ranged never deals melee
	// contact damage, so we always return None (no melee token consumed).
	const float Dt = Ctx.DeltaSeconds;
	const bool bHaveTarget = (Ctx.TargetChar != nullptr) && (Ctx.TargetController != nullptr);
	const bool bInRange = bHaveTarget
		&& FVector::DistSquared(GetActorLocation(), Ctx.TargetLocation) <= FMath::Square(RangedEngageRange);

	switch (ChargeState)
	{
	case EFPSRRangedChargeState::Idle:
	{
		UFPSREnemySpawnSubsystem* Sub = GetWorld() ? GetWorld()->GetSubsystem<UFPSREnemySpawnSubsystem>() : nullptr;
		// Cheap gates first (range, then a read-only token peek) so a capped-out idle ranged enemy never pays for the
		// line-of-sight trace every pass at swarm scale (Game.MD §5). Acquire only after LOS confirms a clear shot.
		if (bInRange && Sub && Sub->IsRangedTokenAvailable(Ctx.TargetController) && HasLineOfSight(Ctx.TargetChar, Ctx.TargetLocation))
		{
			if (Sub->TryAcquireRangedToken(Ctx.TargetController))
			{
				ChargeState = EFPSRRangedChargeState::Charging;
				ChargeElapsed = 0.0f;
				bHoldingToken = true;
				HeldTargetPC = Ctx.TargetController;
				LastWarnLocation = GetActorLocation();
				SendRangedWarning(true); // telegraph: the target gets a directional warning to dodge
			}
		}
		break;
	}
	case EFPSRRangedChargeState::Charging:
	{
		// Abort if the target left range, became non-engageable (DBNO/dead players are filtered out of the
		// subsystem's PlayerPawns, so the nearest target changes), or we re-targeted a different player. Release the
		// token + clear the warning, then briefly cool down to avoid instant re-charge flicker.
		const bool bSameTarget = HeldTargetPC.IsValid() && (Ctx.TargetController == HeldTargetPC.Get());
		if (!bInRange || !bSameTarget)
		{
			ReleaseRangedHold();
			ChargeState = EFPSRRangedChargeState::Cooldown;
			CooldownElapsed = 0.0f;
			break;
		}

		ChargeElapsed += Dt;

		// Track the moving source: re-send the warning location once we've drifted (separation nudges us while we
		// hold), so the indicator points at where we actually are. Throttled by distance (no per-frame Reliable spam).
		if (FVector::DistSquared(GetActorLocation(), LastWarnLocation) > WarnResendDistSq)
		{
			LastWarnLocation = GetActorLocation();
			SendRangedWarning(true);
		}

		if (ChargeElapsed >= RangedChargeTime)
		{
			FireProjectile(Ctx);
			ReleaseRangedHold(); // shot away — clear the warning + free the token (no longer "attempting")
			ChargeState = EFPSRRangedChargeState::Cooldown;
			CooldownElapsed = 0.0f;
		}
		break;
	}
	case EFPSRRangedChargeState::Cooldown:
	{
		CooldownElapsed += Dt;
		if (CooldownElapsed >= RangedFireCooldown)
		{
			ChargeState = EFPSRRangedChargeState::Idle;
		}
		break;
	}
	}

	return EFPSRServerAttackResult::None;
}

void AFPSRRangedEnemyBase::FireProjectile(const FFPSRServerAttackContext& Ctx)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	if (!ProjectileClass)
	{
		UE_LOG(LogFPSR, Warning, TEXT("[RangedEnemy] %s has no ProjectileClass set — shot skipped."), *GetName());
		return;
	}

	UFPSRProjectileSubsystem* ProjSub = World->GetSubsystem<UFPSRProjectileSubsystem>();
	if (!ProjSub)
	{
		return;
	}

	const FVector MuzzleLoc = GetMuzzleLocation();
	FVector Dir = (Ctx.TargetLocation - MuzzleLoc);
	if (Dir.IsNearlyZero())
	{
		Dir = GetActorForwardVector();
	}
	Dir = Dir.GetSafeNormal();

	// Team=Enemy reuses the whole proven projectile/damage bridge: IsHostileTarget hits only players (not other
	// enemies, not the instigator), and damage flows through ApplyContactDamage — no new damage code (Game.MD §2-10).
	FFPSRProjectileParams Params;
	Params.Team = EFPSRProjectileTeam::Enemy;
	Params.InstigatorActor = this;
	Params.Damage = ProjectileDamage;
	Params.CritChance = 0.0f;       // enemy fire never crits (Game.MD §2-10)
	Params.CritMultiplier = 1.0f;
	Params.InitialSpeed = ProjectileSpeed;
	Params.Lifetime = ProjectileLifetime;
	Params.GravityScale = ProjectileGravityScale;
	Params.ExplosionRadius = 0.0f;
	Params.Pierce = 0;
	Params.bSelfDamage = false;
	Params.KnockbackStrength = 0.0f;

	ProjSub->AcquireProjectile(ProjectileClass, MuzzleLoc, Dir, Params);
}

bool AFPSRRangedEnemyBase::HasLineOfSight(const AActor* TargetActor, const FVector& TargetLocation) const
{
	if (!bRequireLineOfSight)
	{
		return true;
	}
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	// Block on STATIC geometry (walls / door frames) AND the player-pawn object channel — which includes CLOSED
	// AFPSRDoor leaves (ECC_FPSRPlayerPawn, Enemy.md §2-6). Without the player-pawn channel a ranged enemy would
	// "see" — and shoot — through a closed door to the player behind it (the enemy projectile overlaps that channel
	// but treats only AFPSRCharacter as hostile, so it would pass straight through the door). Doors only ever break
	// OPEN (never re-close), so gating the SHOT on LOS fully prevents a through-door hit — no barrier can appear in
	// front of an in-flight projectile. Ignore self + the target so neither counts as an occluder. Other ENEMIES
	// (ECC_Pawn) are intentionally NOT queried — an enemy projectile passes through them, so they don't block LOS.
	FCollisionObjectQueryParams ObjParams;
	ObjParams.AddObjectTypesToQuery(ECC_WorldStatic);
	ObjParams.AddObjectTypesToQuery(ECC_FPSRPlayerPawn);
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(FPSRRangedLOS), false, this);
	if (TargetActor)
	{
		QueryParams.AddIgnoredActor(TargetActor);
	}
	FHitResult Hit;
	return !World->LineTraceSingleByObjectType(Hit, GetMuzzleLocation(), TargetLocation, ObjParams, QueryParams);
}

void AFPSRRangedEnemyBase::SendRangedWarning(bool bActive)
{
	if (AFPSRPlayerController* PC = HeldTargetPC.Get())
	{
		// Existing Client+Reliable RPC -> UFPSRPlayerFeedbackComponent::ReceiveRangedTarget. SourceId = our unique id
		// (stable across the charge window; distinct per enemy so concurrent shooters track independently).
		PC->ClientNotifyRangedTarget(static_cast<int32>(GetUniqueID()), GetActorLocation(), bActive);
	}
}

void AFPSRRangedEnemyBase::ReleaseRangedHold()
{
	if (!bHoldingToken)
	{
		return;
	}
	SendRangedWarning(false); // Reliable 'off' — must always fire or the warning indicator sticks forever
	if (UWorld* World = GetWorld())
	{
		if (UFPSREnemySpawnSubsystem* Sub = World->GetSubsystem<UFPSREnemySpawnSubsystem>())
		{
			Sub->ReleaseRangedToken(HeldTargetPC);
		}
	}
	bHoldingToken = false;
	HeldTargetPC = nullptr;
}

void AFPSRRangedEnemyBase::ResetRangedCycle()
{
	ChargeState = EFPSRRangedChargeState::Idle;
	ChargeElapsed = 0.0f;
	CooldownElapsed = 0.0f;
}

FVector AFPSRRangedEnemyBase::GetMuzzleLocation() const
{
	return GetActorTransform().TransformPosition(MuzzleOffset);
}
