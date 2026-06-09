// Copyright Epic Games, Inc. All Rights Reserved.

#include "Enemy/FPSREnemyBase.h"
#include "Enemy/FPSREnemyHealthComponent.h"
#include "Enemy/FPSREnemySpawnSubsystem.h"
#include "Pickup/FPSRPickupSubsystem.h"
#include "Core/FPSRLogChannels.h"

#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "UObject/ConstructorHelpers.h"

AFPSREnemyBase::AFPSREnemyBase()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetReplicateMovement(true);

	Capsule = CreateDefaultSubobject<UCapsuleComponent>(TEXT("Capsule"));
	Capsule->InitCapsuleSize(40.0f, 90.0f);
	Capsule->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Capsule->SetCollisionObjectType(ECC_Pawn);
	Capsule->SetCollisionResponseToAllChannels(ECR_Block);
	SetRootComponent(Capsule);

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Capsule);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetRelativeLocation(FVector(0.0f, 0.0f, -90.0f));
	Mesh->SetRelativeScale3D(FVector(0.8f, 0.8f, 1.8f));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		Mesh->SetStaticMesh(CubeMesh.Object);
	}

	HealthComponent = CreateDefaultSubobject<UFPSREnemyHealthComponent>(TEXT("HealthComponent"));
}

void AFPSREnemyBase::BeginPlay()
{
	Super::BeginPlay();

	if (HealthComponent)
	{
		HealthComponent->OnDeath.AddDynamic(this, &AFPSREnemyBase::HandleDeath);
	}
}

void AFPSREnemyBase::HandleDeath(AActor* DeadActor, AActor* Killer)
{
	if (UWorld* World = GetWorld())
	{
		if (UFPSRPickupSubsystem* Pickups = World->GetSubsystem<UFPSRPickupSubsystem>())
		{
			Pickups->SpawnXPPickup(GetActorLocation(), XPReward);
		}

		if (UFPSREnemySpawnSubsystem* Sub = World->GetSubsystem<UFPSREnemySpawnSubsystem>())
		{
			Sub->ReleaseEnemy(this);
			return;
		}
	}
	Destroy();
}

void AFPSREnemyBase::Activate(const FVector& Location)
{
	SetActorLocation(Location);
	SetActorHiddenInGame(false);
	SetActorEnableCollision(true);
	FlushNetDormancy();

	if (HealthComponent)
	{
		HealthComponent->ResetForReuse();
	}

	CurrentMoveSpeed = MoveSpeed * FMath::FRandRange(0.9f, 1.1f);
	VerticalVelocity = 0.0f; // reset fall state for the reused actor
	bGrounded = false;       // re-check ground on the first update (may spawn on a rooftop)
	GroundRecheckTimer = 0.0f;
}

void AFPSREnemyBase::Deactivate()
{
	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);
	SetNetDormancy(DORM_DormantAll);
}

void AFPSREnemyBase::TickServerMovement(const FVector& MoveDirection, float ScaledDeltaSeconds)
{
	if (!HasAuthority() || (HealthComponent && HealthComponent->IsDead()))
	{
		return;
	}

	// Horizontal steering (flow-field + separation), swept so it blocks against walls.
	FVector Dir = MoveDirection;
	Dir.Z = 0.0f;
	if (Dir.SizeSquared() > KINDA_SMALL_NUMBER)
	{
		const FVector Normalized = Dir.GetSafeNormal();
		AddActorWorldOffset(Normalized * CurrentMoveSpeed * ScaledDeltaSeconds, true);
		SetActorRotation(Normalized.Rotation());
	}

	// Vertical: ground-follow + gravity ALWAYS (even when not steering) so enemies never float and a
	// rooftop-spawned enemy falls before chasing.
	ApplyGravity(ScaledDeltaSeconds);
}

void AFPSREnemyBase::ApplyGravity(float ScaledDeltaSeconds)
{
	UWorld* World = GetWorld();
	if (!World || !Capsule)
	{
		return;
	}

	// Amortize: a stably grounded enemy re-checks the floor only every GroundRecheckInterval; airborne enemies
	// (falling) run every update so they land promptly (Codex P1 — no per-frame scene query for the whole swarm).
	GroundRecheckTimer -= ScaledDeltaSeconds;
	if (bGrounded && GroundRecheckTimer > 0.0f)
	{
		return;
	}
	GroundRecheckTimer = GroundRecheckInterval;

	const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	const FVector Loc = GetActorLocation();

	// Down-trace against STATIC world ONLY — ignore other pawns/dynamic actors so a falling enemy doesn't 'land'
	// on the swarm and jitter (Codex P2). Short probe; the fall step is clamped below so the floor is always
	// within reach on the next update (no tunneling).
	FCollisionObjectQueryParams ObjParams;
	ObjParams.AddObjectTypesToQuery(ECC_WorldStatic);
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(FPSREnemyGround), false, this);
	FHitResult Hit;
	const FVector TraceStart(Loc.X, Loc.Y, Loc.Z + HalfHeight);
	const FVector TraceEnd(Loc.X, Loc.Y, Loc.Z + HalfHeight - GroundProbeDistance);

	if (World->LineTraceSingleByObjectType(Hit, TraceStart, TraceEnd, ObjParams, QueryParams))
	{
		const float TargetZ = Hit.ImpactPoint.Z + HalfHeight; // capsule center resting on the floor
		const float Diff = Loc.Z - TargetZ;

		// Snap only within tolerance in EITHER direction (a surface far above is a ledge to route around, not
		// ground to teleport onto; a far-below floor means the enemy is airborne).
		if (FMath::Abs(Diff) <= GroundSnapTolerance)
		{
			if (!FMath::IsNearlyZero(Diff))
			{
				SetActorLocation(FVector(Loc.X, Loc.Y, TargetZ), false); // small slope/step correction
			}
			VerticalVelocity = 0.0f;
			bGrounded = true;
			return;
		}

		if (Diff > 0.0f)
		{
			// Above the floor within the probe — fall under gravity, clamped to land exactly on it.
			VerticalVelocity -= GravityAccel * ScaledDeltaSeconds;
			float NewZ = Loc.Z + VerticalVelocity * ScaledDeltaSeconds;
			if (NewZ <= TargetZ)
			{
				NewZ = TargetZ;
				VerticalVelocity = 0.0f;
				bGrounded = true;
			}
			else
			{
				bGrounded = false;
			}
			SetActorLocation(FVector(Loc.X, Loc.Y, NewZ), false);
			return;
		}
		// Diff < -tolerance: a static surface is far ABOVE the feet (overhang) — fall through to the path below.
	}

	// No reachable floor within the probe — fall, clamping the step so it can't overshoot the probe range and
	// tunnel below the floor before the next update's trace can catch it.
	VerticalVelocity -= GravityAccel * ScaledDeltaSeconds;
	const float MaxFallStep = FMath::Max(GroundProbeDistance - 2.0f * HalfHeight - GroundSnapTolerance, 1.0f);
	const float StepZ = FMath::Max(VerticalVelocity * ScaledDeltaSeconds, -MaxFallStep);
	SetActorLocation(FVector(Loc.X, Loc.Y, Loc.Z + StepZ), false);
	bGrounded = false;
}
