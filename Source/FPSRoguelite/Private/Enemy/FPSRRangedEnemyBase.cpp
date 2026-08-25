// Copyright Epic Games, Inc. All Rights Reserved.

#include "Enemy/FPSRRangedEnemyBase.h"
#include "Enemy/FPSREnemySpawnSubsystem.h"
#include "Enemy/FPSREnemyHealthComponent.h" // IsDead() — HealthComponent is only forward-declared in FPSREnemyBase.h
#include "Weapon/FPSRProjectile.h"
#include "Weapon/FPSRProjectileSubsystem.h"
#include "Weapon/FPSRProjectileTypes.h"
#include "Hero/FPSRCharacter.h"
#include "Core/FPSRPlayerController.h"
#include "Core/FPSRLogChannels.h"
#include "FPSRCollisionChannels.h"

#include "Engine/World.h"
#include "CollisionQueryParams.h"
#include "Net/UnrealNetwork.h"
#include "Net/Core/PushModel/PushModel.h"

AFPSRRangedEnemyBase::AFPSRRangedEnemyBase()
{
	// Ranged enemies hold at distance to shoot rather than closing to melee: stop advancing further out (within the
	// engage range so they stop, then charge). Tunable per archetype in the BP child.
	StopDistance = 900.0f;
}

void AFPSRRangedEnemyBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	FDoRepLifetimeParams Params;
	Params.bIsPushBased = true;
	DOREPLIFETIME_WITH_PARAMS_FAST(AFPSRRangedEnemyBase, bCharging, Params);
}

void AFPSRRangedEnemyBase::Activate(const FVector& Location)
{
	Super::Activate(Location);
	// Fresh reuse: any prior hold was released by the matching Deactivate; clear defensively so no stale warning/token
	// leaks into the new life, then reset the cycle.
	bHoldingToken = false;
	HeldTargetPC = nullptr;
	ResetRangedCycle();

	// Defensive reset: bCharging should already be false via ReleaseRangedHold on every teardown path (Deactivate /
	// EnterDyingState / EndPlay all route through it), but Activate is the ONE pool-reuse entry point every archetype
	// life passes through — belt-and-suspenders so a reused actor can never render the non-targeted charge telegraph
	// before its first real charge this life.
	if (bCharging)
	{
		bCharging = false;
		MARK_PROPERTY_DIRTY_FROM_NAME(AFPSRRangedEnemyBase, bCharging, this);
	}
}

void AFPSRRangedEnemyBase::Deactivate()
{
	// Pool release / death-dwell completion / kill-Z recycle all route here — close the warning + release the token
	// on EVERY teardown path (not just an explicit abort) so a Reliable 'off' is never dropped and the concurrency
	// count never leaks.
	ReleaseRangedHold();
	ResetRangedCycle();
	Super::Deactivate();
}

void AFPSRRangedEnemyBase::EnterDyingState()
{
	// Same reason as Deactivate() above, but earlier: whichever teardown reaches this enemy first, the held ranged
	// state must close HERE, not wait for the LATER Deactivate() call — a ranged corpse can now dwell for
	// GetDeathDwellSeconds() before that runs, and the target's warning indicator (+ this enemy's concurrency token)
	// must not stay held for the whole dwell window.
	ReleaseRangedHold();
	ResetRangedCycle();
	Super::EnterDyingState();
}

void AFPSRRangedEnemyBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ReleaseRangedHold();
	Super::EndPlay(EndPlayReason);
}

EFPSRServerAttackResult AFPSRRangedEnemyBase::ServerTickAttack(const FFPSRServerAttackContext& Ctx)
{
	// Defensive — see AFPSREnemyBase::ServerTickAttack's identical guard for why this is added despite being
	// structurally unreachable today (BeginDying already removes a dying enemy from the subsystem's per-pass set).
	if (HealthComponent && HealthComponent->IsDead())
	{
		return EFPSRServerAttackResult::None;
	}

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

				// Drive the Attack cosmetic at the CHARGE-length rate so the material's (Time-EnterTime)*Rate
				// progress reaches exactly 1.0 the moment the shot fires (not the melee AttackAnimHoldSeconds
				// default), and hold it there for the same span so TickServerMovement's walk/idle branch can't stomp
				// it mid-charge (a stationary/slow-repositioning charger can still read as bMoved on a separation-
				// jitter pass).
				const float ChargeRate = 1.0f / FMath::Max(KINDA_SMALL_NUMBER, RangedChargeTime);
				SetAnimState(EFPSRAnimState::Attack, ChargeRate);
				AttackAnimHoldUntil = Ctx.Now + RangedChargeTime;

				// Non-targeted client telegraph (user decision, see bCharging's own comment): replicate the charge
				// to EVERY client, not just the Reliable-RPC'd target.
				bCharging = true;
				MARK_PROPERTY_DIRTY_FROM_NAME(AFPSRRangedEnemyBase, bCharging, this);
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
			NotifyAttacked(Ctx.Now); // ADR 0008: unify the melee/ranged "attack succeeded" signal for stall detection
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

	// Block on STATIC geometry (walls / door frames) AND breakable geometry — CLOSED AFPSRDoor leaves and arena
	// props (ECC_FPSRDestructible, Enemy.md §2-6; these used to ride ECC_FPSRPlayerPawn). Without that channel a
	// ranged enemy would "see" — and shoot — through a closed door to the player behind it. The enemy projectile
	// now BLOCKS on destructibles too, so this is no longer the only thing preventing a through-door hit, but it
	// is still the cheaper gate: it stops the shot from being taken at all instead of eating it on the door.
	// The player channel stays queried so a teammate's body also breaks LOS. Ignore self + the target so neither
	// counts as an occluder. Other ENEMIES (ECC_Pawn) are intentionally NOT queried — an enemy projectile passes
	// through them, so they don't block LOS.
	FCollisionObjectQueryParams ObjParams;
	ObjParams.AddObjectTypesToQuery(ECC_WorldStatic);
	ObjParams.AddObjectTypesToQuery(ECC_FPSRPlayerPawn);
	ObjParams.AddObjectTypesToQuery(ECC_FPSRDestructible);
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
	// bCharging is cleared UNCONDITIONALLY here, ahead of the bHoldingToken early-return below, so the non-targeted-
	// client telegraph never sticks true across a teardown that races the charge state — this function is already
	// the single "idempotent, safe on every teardown path" recovery point (Deactivate / EnterDyingState / EndPlay /
	// both ServerTickAttack exits all route through it), exactly mirroring why the warning RPC below always fires.
	if (bCharging)
	{
		bCharging = false;
		MARK_PROPERTY_DIRTY_FROM_NAME(AFPSRRangedEnemyBase, bCharging, this);
		// Also release the movement-anim hold immediately: an ABORTED charge (target left range / re-targeted) must
		// not leave the cosmetic Attack pose stuck through the remainder of the original RangedChargeTime window
		// while the enemy is actually free to move (a successful FIRE reaches this at ChargeElapsed>=RangedChargeTime,
		// i.e. Ctx.Now is already ~AttackAnimHoldUntil, so clearing it here early is a no-op harm-wise on that path).
		AttackAnimHoldUntil = -1.0f;
	}

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

void AFPSRRangedEnemyBase::OnRep_Charging()
{
	// Non-targeted client telegraph (see bCharging's header comment). True -> enter the Attack cosmetic at the
	// charge-length rate, mirroring the server's own SetAnimState call in ServerTickAttack's Idle->Charging
	// transition. False -> release the hold so the next PostNetReceiveLocationAndRotation re-derives Walk/Idle from
	// the replicated transform, same as any other attack tell falling out of range.
	if (bCharging)
	{
		SetAnimState(EFPSRAnimState::Attack, 1.0f / FMath::Max(KINDA_SMALL_NUMBER, RangedChargeTime));
		// Hold the cosmetic for the charge length on THIS client, mirroring the authority-side hold that
		// ServerTickAttack stamps alongside its own SetAnimState. A ranged enemy fires from far outside
		// AttackRange, so PostNetReceiveLocationAndRotation's melee tell never claims it and its walk/idle branch
		// would otherwise erase this telegraph on the very next net update — a charge the user decided to
		// replicate specifically so it could be READ would be visible for one frame out of RangedChargeTime.
		if (const UWorld* World = GetWorld())
		{
			AttackAnimHoldUntil = World->GetTimeSeconds() + RangedChargeTime;
		}
	}
	else
	{
		// Aborted or completed charge: drop the hold immediately, exactly as ClearRangedReservation does on the
		// authority side, so the enemy does not sit in the charge pose while it is already free to move.
		AttackAnimHoldUntil = -1.0f;
	}
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
