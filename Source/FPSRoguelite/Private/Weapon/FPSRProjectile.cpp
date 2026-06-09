// Copyright Epic Games, Inc. All Rights Reserved.

#include "Weapon/FPSRProjectile.h"
#include "Weapon/FPSRProjectileSubsystem.h"
#include "Enemy/FPSREnemyHealthComponent.h"
#include "Hero/FPSRCharacter.h"
#include "Core/FPSRGameState.h"
#include "Core/FPSRLogChannels.h"

#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "TimerManager.h"
#include "CollisionShape.h"

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
		CollisionSphere->SetGenerateOverlapEvents(true);
	}
}

void AFPSRProjectile::Deactivate()
{
	bActive = false;
	// Reset the freeze flag so a recycled projectile starts unpaused; otherwise a projectile force-released
	// mid-freeze would keep bSimulationPaused=true and the next reuse's pause/resume logic would no-op.
	bSimulationPaused = false;

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
	// global run freeze (no damage during card-selection, Game.MD §2-2).
	if (!bActive || !HasAuthority() || IsRunFrozen() || Params.Mode != EFPSRProjectileMode::ServerAuthoritative)
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

	// Single-target (optionally piercing): apply the direct hit, then stop once pierce is exhausted.
	TryDamageActor(OtherActor);
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

	// AOE radial damage — server-authoritative only. A cosmetic-predicted round must never apply damage, even
	// when it detonates against terrain (OnSphereHit -> HandleImpact bypasses the overlap-path mode gate).
	if (Params.Mode == EFPSRProjectileMode::ServerAuthoritative && Params.ExplosionRadius > 0.0f)
	{
		if (UWorld* World = GetWorld())
		{
			const FCollisionShape Sphere = FCollisionShape::MakeSphere(Params.ExplosionRadius);

			// Query pawns by OBJECT TYPE, not the Pawn trace channel: a target that has set its Pawn-channel
			// response to Ignore (e.g. a player mid-dash, AFPSRCharacter::ServerDash) is still found here.
			// Object-type queries match on what an actor IS, not how it responds, so the blast can't be dodged
			// by a transient channel-response change.
			FCollisionObjectQueryParams ObjectParams;
			ObjectParams.AddObjectTypesToQuery(ECC_Pawn);
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
				if (Target && !Damaged.Contains(Target) && TryDamageActor(Target))
				{
					Damaged.Add(Target);
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
		// Player projectiles damage swarm enemies (identified by their non-GAS health component).
		return Target->FindComponentByClass<UFPSREnemyHealthComponent>() != nullptr;
	}
	if (Params.Team == EFPSRProjectileTeam::Enemy)
	{
		// Enemy projectiles damage player characters.
		return Target->IsA(AFPSRCharacter::StaticClass());
	}
	return false;
}

bool AFPSRProjectile::TryDamageActor(AActor* Target)
{
	if (!HasAuthority() || !IsHostileTarget(Target))
	{
		return false;
	}

	if (Params.Team == EFPSRProjectileTeam::Player)
	{
		if (UFPSREnemyHealthComponent* HealthComp = Target->FindComponentByClass<UFPSREnemyHealthComponent>())
		{
			HealthComp->ApplyDamage(Params.Damage, Params.InstigatorActor);
			return true;
		}
	}
	else if (Params.Team == EFPSRProjectileTeam::Enemy)
	{
		if (AFPSRCharacter* Character = Cast<AFPSRCharacter>(Target))
		{
			Character->ApplyContactDamage(Params.Damage, Params.InstigatorActor);
			return true;
		}
	}

	return false;
}

void AFPSRProjectile::ReleaseToPool()
{
	// Cosmetic-predicted projectiles are client-local and never tracked by the server pool (see subsystem
	// SCOPE) — destroy them directly. The pool's ReleaseProjectile would no-op on an untracked actor, leaving
	// the cosmetic visual alive forever.
	if (Params.Mode != EFPSRProjectileMode::CosmeticPredicted)
	{
		if (UWorld* World = GetWorld())
		{
			if (UFPSRProjectileSubsystem* Subsystem = World->GetSubsystem<UFPSRProjectileSubsystem>())
			{
				Subsystem->ReleaseProjectile(this);
				return;
			}
		}
	}

	// Cosmetic projectile, or no subsystem available: deactivate and destroy.
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
