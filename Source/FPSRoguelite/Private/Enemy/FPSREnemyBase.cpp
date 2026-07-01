// Copyright Epic Games, Inc. All Rights Reserved.

#include "Enemy/FPSREnemyBase.h"
#include "Enemy/FPSREnemyHealthComponent.h"
#include "Enemy/FPSREnemySpawnSubsystem.h"
#include "Hero/FPSRCharacter.h"
#include "Pickup/FPSRPickupSubsystem.h"
#include "Core/FPSRLogChannels.h"

#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
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
	// Ignore OTHER enemies (also ECC_Pawn): the swarm overlaps and spreads via soft separation steering instead
	// of mutual physics blocking, which would gridlock a dense crowd and stack co-spawned enemies (Game.MD §1/§5).
	// Walls (WorldStatic), the rifle trace (Visibility) and the player (ECC_FPSRPlayerPawn) stay blocked.
	Capsule->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
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

	// Bind the world-space health bar / floating-damage widget to the health component once (server + clients).
	// Pooling-safe: the actor + widget persist across dormancy, so this single bind survives every reuse.
	InitHealthBarWidget();
}

void AFPSREnemyBase::InitHealthBarWidget()
{
	// Force the BP-added world-space widget to exist NOW (it can otherwise be created lazily on first render — after
	// BeginPlay — which would leave the BP bind on a null widget). Then let the BP bind it to OnHealthChanged. Runs on
	// clients too (the bar is a client visual; OnHealthChanged is client-fired via OnRep_Health, B12).
	if (UWidgetComponent* WidgetComp = FindComponentByClass<UWidgetComponent>())
	{
		WidgetComp->InitWidget();
	}
	OnHealthBarReady();
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
	// Wake the pooled actor for its WHOLE active life so its server movement (AddActorWorldOffset each pass)
	// replicates to clients. A pooled reuse returns from DORM_DormantAll, and FlushNetDormancy would force only ONE
	// update — the still-dormant enemy then stops streaming its transform (invisible / frozen on clients while the
	// server enemy keeps moving and dealing damage), and the death hide set in Deactivate never reaches them (zombie).
	// DORM_Awake here + DORM_DormantAll in Deactivate makes the awake->dormant transition flush the final
	// hidden/dead state. Mirrors the projectile pool recipe (FPSRProjectile::Activate/Deactivate).
	SetNetDormancy(DORM_Awake);

	if (HealthComponent)
	{
		HealthComponent->ResetForReuse();
	}

	CurrentMoveSpeed = MoveSpeed * FMath::FRandRange(0.9f, 1.1f);
	VerticalVelocity = 0.0f; // reset fall state for the reused actor
	bGrounded = false;       // re-check ground on the first update (may spawn on a rooftop)
	GroundRecheckTimer = 0.0f;
	KnockbackVelocityXY = FVector::ZeroVector; // clear residual knockback from a prior life
	ClearExitPath();                           // drop any leftover path; AcquireEnemy re-assigns it if this spawn point has one
}

void AFPSREnemyBase::SetExitPath(const TArray<FVector>& InWaypoints)
{
	if (!HasAuthority())
	{
		return;
	}
	ExitPath = InWaypoints;
	ExitPathIndex = 0;
	ExitPathElapsed = 0.0f;
	bFollowingExitPath = (ExitPath.Num() > 0);
}

void AFPSREnemyBase::ClearExitPath()
{
	ExitPath.Reset();
	ExitPathIndex = 0;
	ExitPathElapsed = 0.0f;
	bFollowingExitPath = false;
}

bool AFPSREnemyBase::ConsumeExitPathSteering(const FVector& MyLocation, float ScaledDeltaSeconds, FVector& OutDir)
{
	if (!bFollowingExitPath)
	{
		return false;
	}

	// Skip past any waypoints already reached (XY), then steer toward the next one.
	while (ExitPathIndex < ExitPath.Num())
	{
		FVector ToWp = ExitPath[ExitPathIndex] - MyLocation;
		ToWp.Z = 0.0f;
		if (ToWp.SizeSquared() <= FMath::Square(ExitWaypointReachRadius))
		{
			++ExitPathIndex;          // reached: advance
			ExitPathElapsed = 0.0f;   // progress made — reset the stall timer
			continue;
		}

		// Not yet reached: tick the stall safety timer; a misplaced/blocked waypoint hands off to the flow-field.
		ExitPathElapsed += ScaledDeltaSeconds;
		if (ExitPathElapsed >= ExitPathTimeout)
		{
			ClearExitPath();
			return false;
		}

		const FVector Dir = ToWp.GetSafeNormal();
		if (Dir.IsNearlyZero())
		{
			// Degenerate (waypoint directly above/below): treat as reached to avoid spinning in place.
			++ExitPathIndex;
			ExitPathElapsed = 0.0f;
			continue;
		}
		OutDir = Dir;
		return true;
	}

	// Path exhausted — hand off to flow-field player-chase.
	ClearExitPath();
	return false;
}

void AFPSREnemyBase::Deactivate()
{
	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);
	SetNetDormancy(DORM_DormantAll);
}

EFPSRServerAttackResult AFPSREnemyBase::ServerTickAttack(const FFPSRServerAttackContext& Ctx)
{
	// Melee contact attack: in horizontal range + within the vertical gap (no through-floor hits) + cooldown elapsed
	// + the target player's attack-token budget allows. Behaviour-identical refactor of the spawn subsystem's former
	// inline attack block — the subsystem now delegates the decision here so ranged archetypes can override it.
	if (Ctx.TargetChar
		&& Ctx.DistSqToTarget <= (AttackRange * AttackRange)
		&& Ctx.bVerticalInRange
		&& CanAttack(Ctx.Now)
		&& Ctx.bMeleeTokenAvailable)
	{
		Ctx.TargetChar->ApplyContactDamage(Ctx.ContactDamage, this);
		NotifyAttacked(Ctx.Now);
		return EFPSRServerAttackResult::MeleeAttacked;
	}
	return EFPSRServerAttackResult::None;
}

void AFPSREnemyBase::TickServerMovement(const FVector& MoveDirection, const FVector& FaceDirection, float ScaledDeltaSeconds)
{
	if (!HasAuthority() || (HealthComponent && HealthComponent->IsDead()))
	{
		return;
	}

	// Knockback (explosion push): a decaying horizontal impulse. While it's active, suppress flow-field steering
	// so the push isn't immediately cancelled by the enemy walking back toward the player.
	const bool bKnockbackActive = !KnockbackVelocityXY.IsNearlyZero(1.0f);

	// Horizontal steering (flow-field + separation), swept so it blocks against walls.
	FVector Dir = MoveDirection;
	Dir.Z = 0.0f;
	if (!bKnockbackActive && Dir.SizeSquared() > KINDA_SMALL_NUMBER)
	{
		const FVector Normalized = Dir.GetSafeNormal();
		const float MoveDist = CurrentMoveSpeed * ScaledDeltaSeconds;

		// Walk ALONG the ground slope (the swarm equivalent of CharacterMovement's MoveAlongFloor): project the steering
		// onto the last-known ground plane and move at full speed along it, so the enemy ascends/descends ramps and stair
		// inclines SMOOTHLY instead of jamming flat against them each tick (the earlier jam-then-slide was janky). On flat
		// ground GroundNormal is up -> a plain horizontal move. GroundNormal is refreshed by ApplyGravity every tick while
		// on a slope (forced below).
		FVector MoveDir = FVector::VectorPlaneProject(Normalized, GroundNormal);
		MoveDir = MoveDir.IsNearlyZero() ? Normalized : MoveDir.GetSafeNormal();
		FHitResult MoveHit;
		AddActorWorldOffset(MoveDir * MoveDist, true, &MoveHit);

		if (MoveHit.bBlockingHit)
		{
			const FVector Remaining = MoveDir * MoveDist * (1.0f - MoveHit.Time);
			if (MoveHit.ImpactNormal.Z >= WalkableSlopeNormalZ)
			{
				// (a) Hit a WALKABLE SLOPE (stepping from flat ground ONTO a ramp/incline): slide the remainder UP ALONG
				// it so we mount the slope; next tick GroundNormal reflects the slope and the move follows it directly.
				if (!Remaining.IsNearlyZero())
				{
					AddActorWorldOffset(FVector::VectorPlaneProject(Remaining, MoveHit.ImpactNormal), true);
				}
			}
			else if (bGrounded && !Remaining.IsNearlyZero())
			{
				// (b) RISER / LEDGE / ramp-crest LIP (anything not a walkable slope — covers the whole normal-Z range below
				// WalkableSlopeNormalZ, so a face between a ramp and vertical no longer stalls the enemy dead). Step up so it
				// climbs what the flow field routed it toward. A ramp/stair top onto a platform can present a lip taller than
				// one flat GroundSnapTolerance step, so try progressively taller lifts and take the SMALLEST that lets the
				// re-advance make progress (no over-hop on small risers). On a SLOPE (cresting a ramp — GroundNormal tilted)
				// allow up to MaxCrestStepUp; on FLAT ground cap at one step so enemies don't scale walls the field routes
				// around. Each lift is swept (stops under a low ceiling); ApplyGravity settles onto the top. Revert if none
				// clears (taller than the cap = a wall, not a riser) so we don't bob against it.
				//
				// Re-advance along the FLOW (FaceDirection), NOT the separation-laden move dir: a lifted enemy carrying the
				// lateral separation push of its stair-mates would walk off the side of a narrow flight and fall. Climbing
				// FORWARD (toward the objective) keeps it on the stairs. Magnitude = the blocked remainder of this move.
				FVector StepFwd = FaceDirection;
				StepFwd.Z = 0.0f;
				StepFwd = StepFwd.IsNearlyZero() ? MoveDir.GetSafeNormal2D() : StepFwd.GetSafeNormal();
				const FVector StepAdvance = StepFwd * (MoveDist * (1.0f - MoveHit.Time));
				const FVector PreStepLoc = GetActorLocation();
				const float MaxLift = (GroundNormal.Z < 0.99f) ? MaxCrestStepUp : GroundSnapTolerance;
				bool bCleared = false;
				for (float Lift = GroundSnapTolerance; Lift <= MaxLift + KINDA_SMALL_NUMBER; Lift += GroundSnapTolerance)
				{
					SetActorLocation(PreStepLoc, false);
					AddActorWorldOffset(FVector(0.0f, 0.0f, Lift), true);
					FHitResult StepFwdHit;
					AddActorWorldOffset(StepAdvance, true, &StepFwdHit);
					if (!(StepFwdHit.bBlockingHit && StepFwdHit.Time < KINDA_SMALL_NUMBER))
					{
						bCleared = true; // this lift cleared the riser/lip (re-advance made progress)
						break;
					}
				}
				if (!bCleared)
				{
					SetActorLocation(PreStepLoc, false);
				}
			}
		}

		// On a slope (or right after hitting a rise), re-trace the ground THIS tick so GroundNormal tracks the incline and
		// ApplyGravity re-snaps us onto it — no float/jitter while climbing. Flat movers keep the cheap amortized recheck.
		if (GroundNormal.Z < 0.99f || MoveHit.bBlockingHit)
		{
			GroundRecheckTimer = 0.0f;
		}

		// Face the PLAYER (FaceDirection), not the move direction: at StopDistance the move is separation-only and its
		// direction jitters, which would spin the enemy 360deg in place. FaceDirection is stable (toward the target).
		FVector FaceXY = FaceDirection;
		FaceXY.Z = 0.0f;
		if (!FaceXY.IsNearlyZero())
		{
			SetActorRotation(FaceXY.GetSafeNormal().Rotation());
		}
	}

	if (bKnockbackActive)
	{
		AddActorWorldOffset(KnockbackVelocityXY * ScaledDeltaSeconds, true); // swept: blocks against walls
		const float DecayFactor = FMath::Exp(-ScaledDeltaSeconds / FMath::Max(KnockbackDecayTime, 0.01f));
		KnockbackVelocityXY *= DecayFactor;
		if (KnockbackVelocityXY.IsNearlyZero(1.0f))
		{
			KnockbackVelocityXY = FVector::ZeroVector;
		}
	}

	// Vertical: ground-follow + gravity ALWAYS (even when not steering) so enemies never float and a
	// rooftop-spawned enemy falls before chasing.
	ApplyGravity(ScaledDeltaSeconds);
}

void AFPSREnemyBase::ApplyKnockback(const FVector& Velocity)
{
	if (!HasAuthority() || (HealthComponent && HealthComponent->IsDead()))
	{
		return;
	}
	// Additive: stacking blasts compound. Horizontal goes to the decaying member; vertical feeds VerticalVelocity
	// so the existing gravity integrator carries the enemy up and back down (a launched pop).
	KnockbackVelocityXY += FVector(Velocity.X, Velocity.Y, 0.0f);
	VerticalVelocity += Velocity.Z;
	bGrounded = false;        // leave the ground; re-acquire it on landing
	GroundRecheckTimer = 0.0f; // re-check the floor immediately
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
		const float TargetZ = Hit.ImpactPoint.Z + HalfHeight + GroundRestClearance; // rest just above the floor (not flush — see GroundRestClearance)
		const float Diff = Loc.Z - TargetZ;

		// Snap only within tolerance in EITHER direction (a surface far above is a ledge to route around, not
		// ground to teleport onto; a far-below floor means the enemy is airborne). NOT while rising under a
		// knockback impulse (VerticalVelocity > 0) — snapping then would instantly cancel the launch.
		if (VerticalVelocity <= 0.0f && FMath::Abs(Diff) <= GroundSnapTolerance)
		{
			if (!FMath::IsNearlyZero(Diff))
			{
				SetActorLocation(FVector(Loc.X, Loc.Y, TargetZ), false); // small slope/step correction
			}
			VerticalVelocity = 0.0f;
			bGrounded = true;
			GroundNormal = Hit.ImpactNormal; // remember the slope so TickServerMovement walks along it
			return;
		}

		if (Diff > 0.0f || VerticalVelocity > 0.0f)
		{
			// Above the floor (or launched upward) — integrate ballistically, clamping to land exactly on the floor
			// only while descending (a rising knockback passes up through TargetZ without snapping).
			VerticalVelocity -= GravityAccel * ScaledDeltaSeconds;
			float NewZ = Loc.Z + VerticalVelocity * ScaledDeltaSeconds;
			if (VerticalVelocity <= 0.0f && NewZ <= TargetZ)
			{
				NewZ = TargetZ;
				VerticalVelocity = 0.0f;
				bGrounded = true;
				GroundNormal = Hit.ImpactNormal; // just landed -> remember the slope
			}
			else
			{
				bGrounded = false;
				GroundNormal = FVector::UpVector; // airborne -> steer horizontally
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
	GroundNormal = FVector::UpVector; // airborne -> steer horizontally
}
