// Copyright Epic Games, Inc. All Rights Reserved.

#include "Weapon/FPSRProjectile.h"
#include "Weapon/FPSRProjectileSubsystem.h"
#include "Weapon/FPSRWeaponFragment.h"
#include "Combat/FPSRCombatStatics.h"
#include "Combat/FPSRWeakpointComponent.h"
#include "FPSRCollisionChannels.h"
#include "Enemy/FPSREnemyHealthComponent.h"
#include "Hero/FPSRCharacter.h"
#include "Core/FPSRGameState.h"
#include "Core/FPSRPlayerController.h"
#include "Core/FPSRLogChannels.h"

#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "TimerManager.h"
#include "CollisionShape.h"

namespace
{
	/** Rebuild a minimal server FireContext from a projectile's spawn-time params so the U18c OnKill bridge can run at
	 *  damage time (the live FFPSRFireContext is long gone — the firing ability ended when the projectile launched).
	 *  Instance is the weak weapon ref (null if the weapon was swapped/dropped mid-flight — the bridge then no-ops). */
	FFPSRFireContext MakeProjectileFireContext(const FFPSRProjectileParams& Params, UWorld* World)
	{
		FFPSRFireContext Ctx;
		Ctx.Avatar = Cast<APawn>(Params.InstigatorActor);
		Ctx.Controller = Ctx.Avatar ? Ctx.Avatar->GetController() : nullptr;
		Ctx.World = World;
		Ctx.Instance = Params.WeaponInstance.Get();
		Ctx.ShotCount = 1;
		Ctx.bAuthority = true; // every caller is inside a HasAuthority() gate
		return Ctx;
	}
}

AFPSRProjectile::AFPSRProjectile()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetReplicateMovement(true);
	InitialLifeSpan = 0.0f;

	// Root collision sphere
	CollisionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("CollisionSphere"));
	CollisionSphere->InitSphereRadius(16.0f);
	CollisionSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision); // Enabled on Activate()
	SetRootComponent(CollisionSphere);

	// Projectile movement component
	ProjectileMovement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("ProjectileMovement"));
	ProjectileMovement->UpdatedComponent = CollisionSphere;
	ProjectileMovement->bShouldBounce = false;
	ProjectileMovement->bAutoActivate = false;
	ProjectileMovement->bRotationFollowsVelocity = true;

	// Visual mesh (no collision)
	MeshComp = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	MeshComp->SetupAttachment(CollisionSphere);
	MeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	// NOTE: Mesh asset assigned by content, not hardcoded
}

void AFPSRProjectile::BeginPlay()
{
	Super::BeginPlay();

	if (CollisionSphere)
	{
		CollisionSphere->OnComponentHit.AddDynamic(this, &AFPSRProjectile::OnSphereHit);
		CollisionSphere->OnComponentBeginOverlap.AddDynamic(this, &AFPSRProjectile::OnSphereOverlap);
	}
}

void AFPSRProjectile::Launch(const FFPSRProjectileParams& InParams, const FVector& Direction)
{
	Params = InParams;
	PierceRemaining = Params.Pierce;
	HitActors.Reset();
	bDealtEnemyDamage = false;

	if (ProjectileMovement)
	{
		// Restore the updated component: a prior world hit calls UProjectileMovementComponent::StopSimulating(),
		// which clears UpdatedComponent (and zeroes Velocity). Without this, a pooled reuse would sit stationary.
		ProjectileMovement->SetUpdatedComponent(CollisionSphere);
		ProjectileMovement->ProjectileGravityScale = Params.GravityScale;
		ProjectileMovement->InitialSpeed = Params.InitialSpeed;
		ProjectileMovement->MaxSpeed = Params.InitialSpeed;
		ProjectileMovement->Velocity = Direction.GetSafeNormal() * Params.InitialSpeed;
		ProjectileMovement->Activate();
	}

	// Setup instigator and owner
	if (Params.InstigatorActor)
	{
		SetInstigator(Cast<APawn>(Params.InstigatorActor));
		SetOwner(Params.InstigatorActor);

		if (CollisionSphere)
		{
			CollisionSphere->IgnoreActorWhenMoving(Params.InstigatorActor, true);
		}
	}

	// Start lifetime timer. Clamp to a small positive minimum: SetTimer with rate <= 0 CLEARS the timer instead
	// of scheduling it, so a content-authored Lifetime <= 0 would leave the projectile flying forever (and with
	// InitialLifeSpan = 0 it never auto-destroys), pinning a pool/cap slot until eviction.
	if (UWorld* World = GetWorld())
	{
		constexpr float MinLifetimeSeconds = 0.05f;
		World->GetTimerManager().SetTimer(
			LifetimeTimer,
			this,
			&AFPSRProjectile::OnLifetimeExpired,
			FMath::Max(Params.Lifetime, MinLifetimeSeconds),
			false
		);
	}
}

void AFPSRProjectile::Activate(const FVector& Location, const FFPSRProjectileParams& InParams, const FVector& Direction)
{
	SetActorLocation(Location);
	SetActorHiddenInGame(false);

	// Establish the FULL launch state (bActive, Params, movement, lifetime) BEFORE enabling collision. Enabling
	// collision runs UpdateOverlaps, which fires begin-overlap SYNCHRONOUSLY for a pawn the projectile spawns
	// inside (point-blank / reuse). If bActive were still false or Params stale, OnSphereOverlap would drop that
	// initial overlap and the shot would pass through (no later begin-overlap fires for an already-overlapping
	// actor). Keep the projectile continuously awake so its movement replicates for the whole flight — a pooled
	// reuse returns from DORM_DormantAll and FlushNetDormancy would only force one update. (Codex 2026-06-09.)
	bActive = true;
	SetNetDormancy(DORM_Awake);
	Launch(InParams, Direction);

	// Enable collision last — a pawn already overlapping the muzzle now triggers OnSphereOverlap with valid state.
	SetActorEnableCollision(true);
	if (CollisionSphere)
	{
		CollisionSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		CollisionSphere->SetCollisionObjectType(ECC_WorldDynamic);
		CollisionSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
		CollisionSphere->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
		CollisionSphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
		// Also overlap the player object channel so a projectile can hit a friendly player (friendly fire) or a
		// player target (enemy-team projectile). IsHostileTarget/ResolveDamage still gate whether damage lands.
		CollisionSphere->SetCollisionResponseToChannel(ECC_FPSRPlayerPawn, ECR_Overlap);
		CollisionSphere->SetCollisionResponseToChannel(ECC_FPSRWeakpoint, ECR_Overlap);
		CollisionSphere->SetGenerateOverlapEvents(true);
	}
}

void AFPSRProjectile::Deactivate()
{
	bActive = false;
	// Reset the freeze flag so a recycled projectile starts unpaused; otherwise a projectile force-released
	// mid-freeze would keep bSimulationPaused=true and the next reuse's pause/resume logic would no-op.
	bSimulationPaused = false;
	HitActors.Reset();

	// Clear lifetime timer
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(LifetimeTimer);
	}

	// Stop movement
	if (ProjectileMovement)
	{
		ProjectileMovement->StopMovementImmediately();
		ProjectileMovement->Deactivate();
	}

	// Drop the per-launch instigator move-ignore so a pooled reuse for a different shooter doesn't keep
	// ignoring the previous instigator (the ignore list would otherwise accumulate across reuses).
	if (CollisionSphere)
	{
		CollisionSphere->ClearMoveIgnoreActors();
	}

	// Hide and disable collision
	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);
	SetNetDormancy(DORM_DormantAll);
}

void AFPSRProjectile::OnSphereHit(UPrimitiveComponent* HitComp, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	FVector NormalImpulse, const FHitResult& Hit)
{
	if (!bActive || !HasAuthority() || IsRunFrozen())
	{
		return;
	}

	// World block: impact (detonates an AOE round; a single-hit round simply expires here).
	HandleImpact(Hit.ImpactPoint);
}

void AFPSRProjectile::OnSphereOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex, bool bFromSweep, const FHitResult& Sweep)
{
	// Server-authoritative damage only; guard re-entrancy (a released projectile must not act again) and the
	// global run freeze (no damage during card-selection, Game.MD §2-2). NOTE: an overlap on the exact
	// freeze-transition frame (before the subsystem suspends movement) is intentionally dropped here — a
	// 1-frame, single-missed-hit edge with no deferred-overlap handling (world hits, which can stick the round,
	// are deferred via HandleImpact on resume; overlaps don't stop the round, so the cost isn't justified).
	if (!bActive || !HasAuthority() || IsRunFrozen())
	{
		return;
	}

	// Only react to a valid hostile (also screens out self/instigator and friendly pass-throughs).
	if (!IsHostileTarget(OtherActor))
	{
		return;
	}

	if (Params.ExplosionRadius > 0.0f)
	{
		// AOE round: detonate at the projectile's location. The radial sweep in HandleImpact applies the
		// damage to every hostile in range (including this one), so we do NOT also apply a direct hit here —
		// that would double-damage the contacted target.
		HandleImpact(GetActorLocation());
		return;
	}

	// Single-target (optionally piercing): dedupe per actor (a body + weakpoint overlap on the same enemy must
	// damage once), re-query weakpoints at damage time so a body-first overlap still upgrades to a weakpoint hit.
	if (HitActors.Contains(OtherActor))
	{
		return;
	}
	HitActors.Add(OtherActor);

	const float WeakpointMult = CollisionSphere
		? FPSRCombat::GetBestWeakpointMultiplierForSphere(OtherActor, CollisionSphere->GetComponentLocation(), CollisionSphere->GetScaledSphereRadius())
		: 1.0f;

	bool bCrit = false;
	bool bKill = false;
	bool bWasEnemy = false;
	bool bDamaged = false;
	// Marker fires only when REAL damage landed on an enemy — a corpse re-hit (bDamaged false) is inert. The hit is
	// still consumed below (pierce decrements unconditionally), and a friendly hit raises no marker (bWasEnemy false).
	if (TryDamageActor(OtherActor, WeakpointMult, bCrit, bKill, bWasEnemy, bDamaged) && bWasEnemy && bDamaged)
	{
		NotifyInstigatorHitMarker(bCrit, WeakpointMult > 1.0f, bKill);
	}
	if (bDamaged)
	{
		bDealtEnemyDamage = true; // not a miss — suppresses the OnMiss hook at release
	}
	--PierceRemaining;
	if (PierceRemaining < 0)
	{
		ReleaseToPool();
	}
}

void AFPSRProjectile::OnLifetimeExpired()
{
	if (!bActive)
	{
		return;
	}
	ReleaseToPool();
}

void AFPSRProjectile::HandleImpact(const FVector& ImpactPoint)
{
	// No impacts during the global run freeze — preserve state (don't damage and don't release).
	if (!bActive || !HasAuthority() || IsRunFrozen())
	{
		return;
	}

	// AOE radial damage (server-authoritative; HasAuthority checked above).
	if (Params.ExplosionRadius > 0.0f)
	{
		if (UWorld* World = GetWorld())
		{
			if (Params.Team == EFPSRProjectileTeam::Player)
			{
				// Unified player explosion: enemy/self/friendly damage resolution + radial knockback + a single
				// hit-marker, all server-authoritative. bAllowSelf is the baked self-damage flag (NoSelfDamage card
				// clears it); knockback is independent of damage (still launches at 0 damage, excludes the killed).
				const FPSRCombat::FExplosionResult Outcome = FPSRCombat::ApplyExplosion(
					World, ImpactPoint, Params.ExplosionRadius, Params.Damage,
					Params.CritChance, Params.CritMultiplier, Params.InstigatorActor,
					/*bAllowSelf*/ Params.bSelfDamage, Params.KnockbackStrength);

				// OnKill trigger (server): fire once per enemy this blast freshly killed (bazooka reload-on-kill etc.).
				// Rebuild the FireContext from spawn params; the weak weapon ref no-ops the bridge if the weapon is gone.
				if (Outcome.KilledEnemies.Num() > 0)
				{
					const FFPSRFireContext KillCtx = MakeProjectileFireContext(Params, World);
					for (AActor* KilledActor : Outcome.KilledEnemies)
					{
						FPSRWeaponHooks::NotifyKill(KillCtx, KilledActor);
					}
				}
				if (Outcome.bAnyEnemyHit)
				{
					bDealtEnemyDamage = true; // splash connected — not a miss
				}
			}
			else
			{
				// Enemy-team AOE (B1 follow-up): existing radial loop damages players only via TryDamageActor. Query
				// pawns by OBJECT TYPE (both channels) so a dashing player — Pawn-response Ignore — is still found.
				const FCollisionShape Sphere = FCollisionShape::MakeSphere(Params.ExplosionRadius);
				FCollisionObjectQueryParams ObjectParams;
				FPSRCombat::AddDamageablePawnObjectTypes(ObjectParams);
				FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(ProjectileAOE), false, this);
				if (Params.InstigatorActor)
				{
					QueryParams.AddIgnoredActor(Params.InstigatorActor);
				}

				TArray<FOverlapResult> Overlaps;
				World->OverlapMultiByObjectType(Overlaps, ImpactPoint, FQuat::Identity, ObjectParams, Sphere, QueryParams);

				TArray<AActor*> Damaged;
				for (const FOverlapResult& Overlap : Overlaps)
				{
					AActor* Target = Overlap.GetActor();
					bool bCrit = false;
					bool bKill = false;
					bool bWasEnemy = false;
					bool bDamaged = false;
					if (Target && !Damaged.Contains(Target) && TryDamageActor(Target, 1.0f, bCrit, bKill, bWasEnemy, bDamaged))
					{
						Damaged.Add(Target);
					}
				}
			}
		}
	}

	// Always release after impact (release is idempotent via the bActive guard + subsystem dedup).
	ReleaseToPool();
}

bool AFPSRProjectile::IsHostileTarget(AActor* Target) const
{
	if (!Target || Target == Params.InstigatorActor)
	{
		return false;
	}

	if (Params.Team == EFPSRProjectileTeam::Player)
	{
		// Player projectiles damage swarm enemies (identified by their non-GAS health component)...
		if (Target->FindComponentByClass<UFPSREnemyHealthComponent>())
		{
			return true;
		}
		// ...and friendly players ONLY when friendly fire is on (otherwise a teammate is a pure pass-through —
		// a non-piercing round doesn't stop on them, an AOE round doesn't detonate on them).
		if (Target->IsA(AFPSRCharacter::StaticClass()))
		{
			return FPSRCombat::IsFriendlyFireEnabled(GetWorld());
		}
		return false;
	}
	if (Params.Team == EFPSRProjectileTeam::Enemy)
	{
		// Enemy projectiles damage player characters.
		return Target->IsA(AFPSRCharacter::StaticClass());
	}
	return false;
}

bool AFPSRProjectile::TryDamageActor(AActor* Target, float WeakpointMultiplier, bool& bOutCrit, bool& bOutKill, bool& bOutWasEnemy, bool& bOutDamaged)
{
	bOutCrit = false;
	bOutKill = false;
	bOutWasEnemy = false;
	bOutDamaged = false;
	if (!HasAuthority() || !IsHostileTarget(Target))
	{
		return false;
	}

	// Roll crit per impact using the chance/multiplier baked from the instigator's ASC at spawn (mirrors the
	// hitscan ability's per-hit crit; enemy projectiles carry CritChance 0 so they never crit).
	float FinalDamage = Params.Damage;
	if (Params.CritChance > 0.0f && FMath::FRand() < Params.CritChance)
	{
		FinalDamage *= Params.CritMultiplier;
		bOutCrit = true;
	}

	if (Params.Team == EFPSRProjectileTeam::Player)
	{
		// Direct hit (single-target/piercing): never self-damage (bAllowSelf=false); the unified resolver applies
		// the enemy/friendly rules (friendly is 0 when FF is off — already screened by IsHostileTarget above).
		FinalDamage *= WeakpointMultiplier;
		const float Resolved = FPSRCombat::ResolveDamage(Params.InstigatorActor, Target, FinalDamage, /*bAllowSelf*/ false, GetWorld());
		if (Resolved > 0.0f)
		{
			const FPSRCombat::FDamageResult Result = FPSRCombat::ApplyDamage(Target, Resolved, Params.InstigatorActor);
			bOutKill = Result.bKilled;
			bOutWasEnemy = Result.bWasEnemy;
			bOutDamaged = Result.DamageDealt > 0.0f; // real health removed (0 for a corpse re-hit) — gates the marker
			// OnKill trigger (server): this direct hit freshly killed an enemy (sniper etc.). Rebuild a minimal
			// FireContext from spawn params — the weak weapon ref no-ops the bridge if the weapon is gone.
			if (Result.bKilled)
			{
				FPSRWeaponHooks::NotifyKill(MakeProjectileFireContext(Params, GetWorld()), Target);
			}
			// Return CONSUMPTION (bApplied), not damage: a friendly-fire hit (DamageDealt 0 for a player target) must
			// still consume the projectile (pierce/release), so the marker is gated separately on bOutDamaged below.
			return Result.bApplied;
		}
		return false;
	}
	if (Params.Team == EFPSRProjectileTeam::Enemy)
	{
		// Enemy-team projectiles damage players only (team-specific path retained until B1 generalizes it).
		if (AFPSRCharacter* Character = Cast<AFPSRCharacter>(Target))
		{
			Character->ApplyContactDamage(FinalDamage, Params.InstigatorActor);
			return true;
		}
	}

	return false;
}

void AFPSRProjectile::NotifyInstigatorHitMarker(bool bCrit, bool bWeak, bool bKill) const
{
	// Hit-markers belong to the firing player's HUD; enemy-team projectiles have no HUD owner.
	if (!HasAuthority() || Params.Team != EFPSRProjectileTeam::Player || !Params.InstigatorActor)
	{
		return;
	}
	const APawn* InstigatorPawn = Cast<APawn>(Params.InstigatorActor);
	AController* InstigatorController = InstigatorPawn ? InstigatorPawn->GetController() : nullptr;
	if (AFPSRPlayerController* OwnerPC = Cast<AFPSRPlayerController>(InstigatorController))
	{
		const EFPSRHitMarkerType MarkerType = bKill ? EFPSRHitMarkerType::Kill
			: (bWeak ? EFPSRHitMarkerType::Weak
			: (bCrit ? EFPSRHitMarkerType::Crit : EFPSRHitMarkerType::Hit));
		OwnerPC->ClientNotifyHitMarker(MarkerType);
	}
}

void AFPSRProjectile::ReleaseToPool()
{
	// OnMiss trigger (server): a player projectile that ends without ever damaging an enemy is a true miss (expired,
	// fell out of world, or hit only geometry) — fire the behavior hook so e.g. AmmoOnMiss refunds, matching the
	// hitscan/melee/charge-laser miss behavior. bActive guards re-entry (Deactivate clears it during release).
	if (bActive && HasAuthority() && Params.Team == EFPSRProjectileTeam::Player && !bDealtEnemyDamage)
	{
		FPSRWeaponHooks::NotifyMiss(MakeProjectileFireContext(Params, GetWorld()));
	}

	if (UWorld* World = GetWorld())
	{
		if (UFPSRProjectileSubsystem* Subsystem = World->GetSubsystem<UFPSRProjectileSubsystem>())
		{
			Subsystem->ReleaseProjectile(this);
			return;
		}
	}

	// No subsystem available: deactivate and destroy.
	Deactivate();
	Destroy();
}

void AFPSRProjectile::SetSimulationPaused(bool bPaused)
{
	if (bPaused == bSimulationPaused || !bActive)
	{
		return;
	}
	bSimulationPaused = bPaused;

	UWorld* World = GetWorld();
	if (bPaused)
	{
		// Stop the movement component (Velocity is preserved on the component across Deactivate/Activate) and
		// hold the lifetime timer, so the projectile neither moves nor expires during the freeze.
		if (ProjectileMovement)
		{
			ProjectileMovement->Deactivate();
		}
		if (World)
		{
			World->GetTimerManager().PauseTimer(LifetimeTimer);
		}
	}
	else
	{
		if (World)
		{
			World->GetTimerManager().UnPauseTimer(LifetimeTimer);
		}
		if (ProjectileMovement)
		{
			// If a world hit landed on the freeze-transition frame, UProjectileMovementComponent::StopSimulating
			// cleared UpdatedComponent (its tell-tale) but the impact was deferred by the IsRunFrozen gate.
			// Resolve that deferred impact now (the round is resting against the surface) instead of leaving it
			// permanently stopped. Otherwise just resume — Deactivate preserved the velocity.
			if (ProjectileMovement->UpdatedComponent == nullptr && HasAuthority())
			{
				HandleImpact(GetActorLocation());
				return;
			}
			ProjectileMovement->Activate();
		}
	}
}

bool AFPSRProjectile::IsRunFrozen() const
{
	const UWorld* World = GetWorld();
	const AFPSRGameState* GameState = World ? World->GetGameState<AFPSRGameState>() : nullptr;
	return GameState && GameState->IsRunPaused();
}

void AFPSRProjectile::FellOutOfWorld(const UDamageType& DmgType)
{
	// Recycle to the pool rather than letting the engine destroy the actor (which would leave a pending-kill
	// entry in the pool's accounting until GC). If we can't pool it, fall back to the default destroy.
	if (bActive)
	{
		ReleaseToPool();
		return;
	}
	Super::FellOutOfWorld(DmgType);
}
