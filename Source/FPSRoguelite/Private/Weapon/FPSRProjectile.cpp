// Copyright Epic Games, Inc. All Rights Reserved.

#include "Weapon/FPSRProjectile.h"
#include "Weapon/FPSRProjectileSubsystem.h"
#include "Enemy/FPSREnemyHealthComponent.h"
#include "Hero/FPSRCharacter.h"
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

	// Start lifetime timer
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			LifetimeTimer,
			this,
			&AFPSRProjectile::OnLifetimeExpired,
			Params.Lifetime,
			false
		);
	}
}

void AFPSRProjectile::Activate(const FVector& Location, const FFPSRProjectileParams& InParams, const FVector& Direction)
{
	SetActorLocation(Location);
	SetActorHiddenInGame(false);
	SetActorEnableCollision(true);

	// Re-apply collision responses (mirror enemy Activate pattern)
	if (CollisionSphere)
	{
		CollisionSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		CollisionSphere->SetCollisionObjectType(ECC_WorldDynamic);
		CollisionSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
		CollisionSphere->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
		CollisionSphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
		CollisionSphere->SetGenerateOverlapEvents(true);
	}

	FlushNetDormancy();
	bActive = true;
	Launch(InParams, Direction);
}

void AFPSRProjectile::Deactivate()
{
	bActive = false;

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
	if (!bActive || !HasAuthority())
	{
		return;
	}

	// World block: impact (detonates an AOE round; a single-hit round simply expires here).
	HandleImpact(Hit.ImpactPoint);
}

void AFPSRProjectile::OnSphereOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex, bool bFromSweep, const FHitResult& Sweep)
{
	// Server-authoritative damage only; guard re-entrancy (a released projectile must not act again).
	if (!bActive || !HasAuthority() || Params.Mode != EFPSRProjectileMode::ServerAuthoritative)
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
	if (!bActive || !HasAuthority())
	{
		return;
	}

	// AOE radial damage: overlap every pawn in range and damage the hostile ones once each (the instigator
	// is ignored by the query and by IsHostileTarget/TryDamageActor).
	if (Params.ExplosionRadius > 0.0f)
	{
		if (UWorld* World = GetWorld())
		{
			const FCollisionShape Sphere = FCollisionShape::MakeSphere(Params.ExplosionRadius);
			FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(ProjectileAOE), false, this);
			if (Params.InstigatorActor)
			{
				QueryParams.AddIgnoredActor(Params.InstigatorActor);
			}

			TArray<FOverlapResult> Overlaps;
			World->OverlapMultiByChannel(Overlaps, ImpactPoint, FQuat::Identity, ECC_Pawn, Sphere, QueryParams);

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
	if (UWorld* World = GetWorld())
	{
		if (UFPSRProjectileSubsystem* Subsystem = World->GetSubsystem<UFPSRProjectileSubsystem>())
		{
			Subsystem->ReleaseProjectile(this);
			return;
		}
	}

	// Fallback: deactivate and destroy if no subsystem
	Deactivate();
	Destroy();
}
