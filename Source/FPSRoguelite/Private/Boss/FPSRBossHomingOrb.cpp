// Copyright Epic Games, Inc. All Rights Reserved.

#include "Boss/FPSRBossHomingOrb.h"

#include "Boss/FPSRBossBase.h"
#include "Combat/FPSRTargeting.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Enemy/FPSREnemyHealthComponent.h"
#include "Engine/World.h"
#include "FPSRCollisionChannels.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Hero/FPSRCharacter.h"

AFPSRBossHomingOrb::AFPSRBossHomingOrb()
{
	// 🔴 Explicit: AActor defaults this to false and nothing in this hierarchy turns it on. The orb's tracking,
	// lifetime and death dwell all run off Tick, so losing this line makes a live-looking orb that never moves,
	// never expires and never resolves — with a green build.
	PrimaryActorTick.bCanEverTick = true;

	bReplicates = true;
	SetReplicateMovement(true);
	// The arena diagonal (226 m) is larger than the default net-cull distance (150 m), so a distance-culled orb
	// would simply vanish for the teammate on the far side of the arena. Five short-lived actors is a cheap price
	// for "everyone can see the thing chasing their friend".
	bAlwaysRelevant = true;

	Sphere = CreateDefaultSubobject<USphereComponent>(TEXT("Sphere"));
	SetRootComponent(Sphere);
	Sphere->InitSphereRadius(45.0f);
	Sphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	// Object type ECC_Pawn is what makes every existing weapon query find this orb with no new damage code
	// (FPSRCombat::AddDamageablePawnObjectTypes gathers exactly this). Responses are then set explicitly, because
	// USphereComponent's default profile overlaps everything — which would let the orb drift through walls.
	Sphere->SetCollisionObjectType(ECC_Pawn);
	Sphere->SetCollisionResponseToAllChannels(ECR_Ignore);
	Sphere->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);       // walls and props stop it
	Sphere->SetCollisionResponseToChannel(ECC_FPSRDestructible, ECR_Block);  // suppressors/props are solid to it
	Sphere->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);        // aim traces must be able to hit it
	Sphere->SetCollisionResponseToChannel(ECC_FPSRPlayerPawn, ECR_Overlap);  // contact, not body-blocking
	Sphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);             // passes freely through the swarm
	Sphere->SetGenerateOverlapEvents(true);

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Sphere);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// The same non-GAS health component the swarm and the boss use — that shared component IS the reason the whole
	// existing weapon pipeline can damage this thing without a line of new damage code.
	Health = CreateDefaultSubobject<UFPSREnemyHealthComponent>(TEXT("Health"));

	Movement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("Movement"));
	Movement->bAutoActivate = false;
	Movement->bRotationFollowsVelocity = true;
	Movement->ProjectileGravityScale = 0.0f;
	Movement->bIsHomingProjectile = true;
}

void AFPSRBossHomingOrb::BeginPlay()
{
	Super::BeginPlay();

	if (Health)
	{
		// 🔴 Not a kill: this blocks lifesteal, the DealtDamage event, kill credit and on-kill fragments. (It has
		// nothing to do with XP — XP only drops from AFPSREnemyBase::HandleDeath, and this is not one.) Without it a
		// boss that keeps summoning orbs becomes a renewable source of on-kill procs.
		Health->SetCountsAsKill(false);

		if (HasAuthority())
		{
			Health->OnDeath.AddDynamic(this, &AFPSRBossHomingOrb::HandleDeath);
		}
	}

	if (HasAuthority() && Sphere)
	{
		Sphere->OnComponentBeginOverlap.AddDynamic(this, &AFPSRBossHomingOrb::HandleOverlap);
		Sphere->OnComponentHit.AddDynamic(this, &AFPSRBossHomingOrb::HandleBlockingHit);
	}

	SetActorTickEnabled(HasAuthority());
}

void AFPSRBossHomingOrb::ServerLaunch(AFPSRBossBase* OwningBoss, APawn* Target, float InHealth, float InTrackSeconds,
	float InDamage, const FFPSRDamageSpec& InSpec)
{
	if (!HasAuthority())
	{
		return;
	}

	OwnerBoss = OwningBoss;
	// Owner (not instigator) is the boss: the director's telemetry walks the owner chain to classify this as boss
	// pressure, while the instigator stays the orb so hit-direction feedback points at where the hit came from.
	SetOwner(OwningBoss);

	TargetPawn = Target;
	ContactDamage = InDamage;
	DamageSpec = InSpec;
	TrackSecondsRemaining = InTrackSeconds;
	PostTrackSecondsRemaining = PostTrackLifetimeSeconds;

	if (Health)
	{
		// InitializeMaxHealth also pins MaxShield to 0, which is what keeps the orb out of VIT1's shield-regen time
		// axis entirely — one less clock to reason about under the freeze.
		Health->InitializeMaxHealth(FMath::Max(1.0f, InHealth));
	}

	if (Movement)
	{
		Movement->InitialSpeed = InitialSpeed;
		Movement->MaxSpeed = MaxSpeed;
		Movement->HomingAccelerationMagnitude = HomingAccelerationMagnitude;
		Movement->Velocity = GetActorForwardVector() * InitialSpeed;
		Movement->Activate();
	}

	ServerRetarget();
}

void AFPSRBossHomingOrb::ServerRetarget()
{
	UWorld* World = GetWorld();
	if (!World || !Movement)
	{
		return;
	}

	const float Clock = OwnerBoss.IsValid() ? OwnerBoss->GetPatternClockSeconds() : 0.0f;

	APawn* Best = nullptr;
	float BestDistanceSq = TNumericLimits<float>::Max();
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (!FPSRTargeting::IsEligibleTarget(PC, Clock, /*bRequireTopologyAck=*/false))
		{
			continue;
		}
		APawn* Candidate = PC->GetPawn();
		if (!Candidate)
		{
			continue;
		}
		const float DistanceSq = FVector::DistSquared(Candidate->GetActorLocation(), GetActorLocation());
		if (DistanceSq < BestDistanceSq)
		{
			BestDistanceSq = DistanceSq;
			Best = Candidate;
		}
	}

	TargetPawn = Best;
	if (Best)
	{
		Movement->HomingTargetComponent = Best->GetRootComponent();
		Movement->bIsHomingProjectile = true;
	}
	else
	{
		// Nobody left to chase — keep flying straight rather than freezing in place or snapping to a corpse.
		Movement->HomingTargetComponent = nullptr;
		Movement->bIsHomingProjectile = false;
		TrackSecondsRemaining = 0.0f;
	}
}

void AFPSRBossHomingOrb::SetSimulationPaused(bool bPaused)
{
	if (bSimulationPaused == bPaused)
	{
		return;
	}
	bSimulationPaused = bPaused;

	if (!Movement)
	{
		return;
	}

	if (bPaused)
	{
		// Deactivate rather than zeroing the velocity, so the orb resumes on exactly the arc it was on. Its timers
		// are accumulators fed by Tick, and Tick stops while paused, so nothing else has to be suspended.
		PausedVelocity = Movement->Velocity;
		Movement->Deactivate();
	}
	else
	{
		Movement->Velocity = PausedVelocity;
		Movement->Activate();
	}
}

void AFPSRBossHomingOrb::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!HasAuthority() || bSimulationPaused)
	{
		return;
	}

	// Shot down: hold briefly so the replicated death edge reaches clients and they can play the break, then go.
	if (DeathDwellRemaining >= 0.0f)
	{
		DeathDwellRemaining -= DeltaSeconds;
		if (DeathDwellRemaining <= 0.0f)
		{
			Destroy();
		}
		return;
	}

	// A target that died, went down or disconnected stops being chased immediately — the alternative is an orb
	// circling a body.
	if (TrackSecondsRemaining > 0.0f)
	{
		const APawn* Current = TargetPawn.Get();
		const AController* Controller = Current ? Current->GetController() : nullptr;
		const APlayerController* PC = Cast<APlayerController>(Controller);
		const float Clock = OwnerBoss.IsValid() ? OwnerBoss->GetPatternClockSeconds() : 0.0f;
		if (!Current || !FPSRTargeting::IsEligibleTarget(PC, Clock, /*bRequireTopologyAck=*/false))
		{
			ServerRetarget();
		}
	}

	if (TrackSecondsRemaining > 0.0f)
	{
		TrackSecondsRemaining -= DeltaSeconds;
		if (TrackSecondsRemaining <= 0.0f && Movement)
		{
			// Tracking expired: keep the momentum, drop the steering. A player who has kited it this long has earned
			// the orb flying past them.
			Movement->bIsHomingProjectile = false;
			Movement->HomingTargetComponent = nullptr;
		}
		return;
	}

	PostTrackSecondsRemaining -= DeltaSeconds;
	if (PostTrackSecondsRemaining <= 0.0f)
	{
		Destroy();
	}
}

void AFPSRBossHomingOrb::HandleOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex, bool bFromSweep, const FHitResult& Sweep)
{
	if (!HasAuthority() || DeathDwellRemaining >= 0.0f)
	{
		return;
	}

	AFPSRCharacter* Character = Cast<AFPSRCharacter>(OtherActor);
	if (!Character)
	{
		return;
	}

	// ApplyContactDamage owns every gate that decides whether this lands (dead / downed / grace / i-frames), so this
	// deliberately re-checks none of them: a second copy of those rules is a second place for them to drift.
	Character->ApplyContactDamage(ContactDamage, this, DamageSpec);
	Destroy();
}

void AFPSRBossHomingOrb::HandleBlockingHit(UPrimitiveComponent* HitComp, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	FVector NormalImpulse, const FHitResult& Hit)
{
	if (HasAuthority() && DeathDwellRemaining < 0.0f)
	{
		// Hitting geometry ends it, with no blast: an orb that detonated on walls would make cover actively dangerous,
		// which is the opposite of what cover is for here.
		Destroy();
	}
}

void AFPSRBossHomingOrb::HandleDeath(AActor* DeadActor, AActor* Killer)
{
	if (!HasAuthority())
	{
		return;
	}

	// Stop dead in place and start the dwell. Destroying right here would race the bDead replication and clients
	// would simply see the orb blink out with no break at all.
	if (Movement)
	{
		Movement->StopMovementImmediately();
		Movement->Deactivate();
	}
	if (Sphere)
	{
		Sphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	OnOrbDestroyedCosmetic();
	DeathDwellRemaining = DeathDwellSeconds;
}
