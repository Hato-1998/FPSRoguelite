// Copyright Epic Games, Inc. All Rights Reserved.

#include "Boss/FPSRBossHomingOrb.h"

#include "Boss/FPSRBossBase.h"
#include "Combat/FPSRCombatStatics.h"
#include "Combat/FPSRTargeting.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Enemy/FPSREnemyHealthComponent.h"
#include "Engine/World.h"
#include "FPSRCollisionChannels.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Hero/FPSRCharacter.h"
#include "Net/Core/PushModel/PushModel.h"
#include "Net/UnrealNetwork.h"
#if !UE_BUILD_SHIPPING
#include "DrawDebugHelpers.h"
#endif

AFPSRBossHomingOrb::AFPSRBossHomingOrb()
{
	// 🔴 Explicit: AActor defaults this to false and nothing in this hierarchy turns it on. The orb's grace, chase,
	// divert and death dwell all run off Tick, so losing this line makes a live-looking orb that never moves, never
	// expires and never resolves — with a green build.
	PrimaryActorTick.bCanEverTick = true;

	bReplicates = true;
	SetReplicateMovement(true);
	// The arena diagonal (226 m) is larger than the default net-cull distance (150 m), so a distance-culled orb would
	// simply vanish for the teammate on the far side. Five short-lived actors is a cheap price for "everyone can see
	// the thing chasing their friend" — which the pattern depends on, since the mark is announced to everyone.
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
	Sphere->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);       // walls and props stop it (and it detonates)
	Sphere->SetCollisionResponseToChannel(ECC_FPSRDestructible, ECR_Block);  // suppressors / arena props are solid to it
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
	Movement->bShouldBounce = false;
	Movement->ProjectileGravityScale = 0.0f;
}

void AFPSRBossHomingOrb::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	FDoRepLifetimeParams Params;
	Params.bIsPushBased = true;
	DOREPLIFETIME_WITH_PARAMS_FAST(AFPSRBossHomingOrb, bDetonated, Params);
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

		// The CLIENT half of the shot-down cosmetic: OnDeathCosmetic is broadcast from OnRep_bDead, which only ever
		// runs on non-authority machines. Bound unconditionally; the authority calls HandleDestroyedCosmetic itself
		// from HandleDeath, because a listen-server host never receives its own OnRep.
		Health->OnDeathCosmetic.AddDynamic(this, &AFPSRBossHomingOrb::HandleDestroyedCosmetic);

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

void AFPSRBossHomingOrb::ServerLaunch(AFPSRBossBase* OwningBoss, APawn* Target, const FFPSRBossOrbLaunchParams& Params,
	const FFPSRDamageSpec& InSpec)
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
	LaunchParams = Params;
	DamageSpec = InSpec;
	TrackSecondsRemaining = Params.TrackSeconds;
	PostTrackSecondsRemaining = PostTrackLifetimeSeconds;
	LastKnownTargetLocation = Target ? Target->GetActorLocation() : GetActorLocation();

	if (Health)
	{
		// InitializeMaxHealth also pins MaxShield to 0, which keeps the orb out of VIT1's shield-regen time axis
		// entirely — one less clock to reason about under the freeze.
		Health->InitializeMaxHealth(FMath::Max(1.0f, Params.Health));
	}

	if (Movement)
	{
		Movement->InitialSpeed = InitialSpeed;
		Movement->MaxSpeed = MaxSpeed;
		Movement->HomingAccelerationMagnitude = HomingAccelerationMagnitude;
		Movement->bIsHomingProjectile = false;
		Movement->Velocity = FVector::ZeroVector;
	}

	// Hovering, not yet chasing: the flight forms up around the boss so the party can read it before it commits.
	// This is the pattern's own telegraph, distinct from the boss's system-level wind-up.
	State = EOrbState::Grace;
	StateElapsedSeconds = 0.0f;
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
	else if (State != EOrbState::Grace)
	{
		// Grace deliberately does not resume movement — it never had any to begin with.
		Movement->Velocity = PausedVelocity;
		Movement->Activate();
	}
}

void AFPSRBossHomingOrb::ServerDetonate()
{
	if (!HasAuthority())
	{
		return;
	}

	// ONE blast path for every way an orb can end (player contact, geometry, or the spot where the target died), so
	// the three cannot drift into three different damage rules. Radius damage rather than single-target: standing
	// together — including standing together to revive someone — is exactly what this pattern punishes.
	FPSRCombat::ApplyHostileExplosion(GetWorld(), GetActorLocation(), LaunchParams.BlastRadiusCm,
		LaunchParams.BlastDamage, this, LaunchParams.BlastKnockback, DamageSpec);

	// Publish the detonation BEFORE dying so clients get an edge to react to — destroying here would leave them with
	// an actor that simply vanished, and the blast (the boss landing its hit) would be invisible to everyone but the
	// host. The despawn is deferred by the same dwell the shot-down path uses, for the same reason.
	if (!bDetonated)
	{
		bDetonated = true;
		MARK_PROPERTY_DIRTY_FROM_NAME(AFPSRBossHomingOrb, bDetonated, this);
		OnOrbDetonatedCosmetic(); // authority half — the host gets no OnRep
	}

	// Stop dead and stop interacting; the actor lingers only long enough for the flag to replicate.
	if (Movement)
	{
		Movement->StopMovementImmediately();
		Movement->Deactivate();
	}
	if (Sphere)
	{
		Sphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	if (Mesh)
	{
		Mesh->SetHiddenInGame(true);
	}
	DetonateDespawnRemaining = FMath::Max(0.05f, DeathDwellSeconds);
}

void AFPSRBossHomingOrb::OnRep_Detonated()
{
	if (bDetonated)
	{
		OnOrbDetonatedCosmetic();
	}
}

void AFPSRBossHomingOrb::HandleDestroyedCosmetic()
{
	// Fires on clients via OnRep_bDead and on the authority via HandleDeath. A detonation is NOT a shot-down, so an
	// orb that already blew up must not also play the break.
	if (!bDetonated)
	{
		OnOrbDestroyedCosmetic();
	}
}

void AFPSRBossHomingOrb::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!HasAuthority())
	{
		return;
	}

#if !UE_BUILD_SHIPPING
	// Drawn BEFORE the freeze gate on purpose: "do the orbs hold still during a level-up freeze" is one of the checks
	// this overlay exists for, and an overlay that disappears exactly when the thing freezes cannot answer it.
	if (FPSRBoss::IsDebugDrawEnabled())
	{
		// The blast radius is the thing you cannot see otherwise, and "how close is too close" is the whole question
		// this pattern asks. The destination line matters just as much: it is the only way to watch the orbs commit
		// to a dead player's last position instead of vanishing.
		const TCHAR* Label = TEXT("?");
		FColor Colour = FColor::White;
		switch (State)
		{
		case EOrbState::Grace:              Label = TEXT("HOVER");  Colour = FColor::Yellow;  break;
		case EOrbState::Chase:              Label = TEXT("CHASE");  Colour = FColor::Red;     break;
		case EOrbState::DivertToLastKnown:  Label = TEXT("DEATH SITE"); Colour = FColor::Magenta; break;
		case EOrbState::DeathDwell:         Label = TEXT("SHOT DOWN"); Colour = FColor::Green; break;
		}
		DrawDebugSphere(GetWorld(), GetActorLocation(), LaunchParams.BlastRadiusCm, 12, Colour, false, -1.0f, 0, 2.0f);
		DrawDebugString(GetWorld(), GetActorLocation() + FVector(0, 0, 120.0f), Label, nullptr, Colour, 0.0f, true);
		if (State == EOrbState::DivertToLastKnown)
		{
			DrawDebugLine(GetWorld(), GetActorLocation(), LastKnownTargetLocation, FColor::Magenta, false, -1.0f, 0, 4.0f);
		}
	}
#endif

	if (bSimulationPaused)
	{
		return;
	}

	if (DetonateDespawnRemaining >= 0.0f)
	{
		DetonateDespawnRemaining -= DeltaSeconds;
		if (DetonateDespawnRemaining <= 0.0f)
		{
			Destroy();
		}
		return;
	}

	StateElapsedSeconds += DeltaSeconds;

	switch (State)
	{
	case EOrbState::DeathDwell:
		// Shot down: hold briefly so the replicated death edge reaches clients and they can play the break.
		// Deliberately NO blast — an orb the players destroyed must not still deliver its payload, or shooting them
		// down would stop being worth doing.
		if (StateElapsedSeconds >= DeathDwellSeconds)
		{
			Destroy();
		}
		return;

	case EOrbState::Grace:
		if (StateElapsedSeconds >= LaunchParams.GraceSeconds)
		{
			State = EOrbState::Chase;
			StateElapsedSeconds = 0.0f;
			if (Movement)
			{
				// Aim at where the target is NOW, not where it stood when the flight formed up.
				APawn* Target = TargetPawn.Get();
				const FVector Aim = Target ? (Target->GetActorLocation() - GetActorLocation()) : GetActorForwardVector();
				Movement->Velocity = Aim.GetSafeNormal() * InitialSpeed;
				Movement->HomingTargetComponent = Target ? Target->GetRootComponent() : nullptr;
				Movement->bIsHomingProjectile = Target != nullptr;
				Movement->Activate();
			}
		}
		return;

	case EOrbState::Chase:
	{
		APawn* Target = TargetPawn.Get();
		const APlayerController* PC = Target ? Cast<APlayerController>(Target->GetController()) : nullptr;
		const float Clock = OwnerBoss.IsValid() ? OwnerBoss->GetPatternClockSeconds() : 0.0f;
		const bool bTargetGone = !Target || !FPSRTargeting::IsEligibleTarget(PC, Clock, /*bRequireTopologyAck=*/false);

		if (bTargetGone)
		{
			// Divert to where they last stood and detonate THERE (user decision). Deleting the orbs instead would
			// have made "let the marked player die" the correct play; this way the blast lands exactly where the
			// party has to stand to pick them up.
			State = EOrbState::DivertToLastKnown;
			StateElapsedSeconds = 0.0f;
			if (Movement)
			{
				Movement->bIsHomingProjectile = false;
				Movement->HomingTargetComponent = nullptr;
				Movement->Velocity = (LastKnownTargetLocation - GetActorLocation()).GetSafeNormal() * Movement->MaxSpeed;
				Movement->Activate();
			}
			return;
		}

		LastKnownTargetLocation = Target->GetActorLocation();

		TrackSecondsRemaining -= DeltaSeconds;
		if (TrackSecondsRemaining <= 0.0f)
		{
			if (Movement && Movement->bIsHomingProjectile)
			{
				// Tracking expired: keep the momentum, drop the steering. A player who kited it this long has earned
				// the orb sailing past them.
				Movement->bIsHomingProjectile = false;
				Movement->HomingTargetComponent = nullptr;
			}
			PostTrackSecondsRemaining -= DeltaSeconds;
			if (PostTrackSecondsRemaining <= 0.0f)
			{
				// Fizzles out with no blast: it never reached anything, so there is nothing to punish.
				Destroy();
			}
		}
		return;
	}

	case EOrbState::DivertToLastKnown:
		// Detonate on arrival, or once the trip has clearly overrun. The spot can become unreachable (geometry, a
		// closed door), and an orb that never resolves is worse than one that resolves a little early.
		if (FVector::DistSquared(GetActorLocation(), LastKnownTargetLocation) <= FMath::Square(LaunchParams.BlastRadiusCm * 0.25f)
			|| StateElapsedSeconds >= FMath::Max(1.0f, LaunchParams.TrackSeconds))
		{
			ServerDetonate();
		}
		return;
	}
}

void AFPSRBossHomingOrb::HandleOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex, bool bFromSweep, const FHitResult& Sweep)
{
	if (!HasAuthority() || State == EOrbState::DeathDwell || State == EOrbState::Grace)
	{
		return;
	}

	if (Cast<AFPSRCharacter>(OtherActor))
	{
		// ANY player sets it off, not only the marked one — the orb is a physical object, and stepping in front of it
		// for a teammate is a legitimate play. The blast radius keeps that from being a free save.
		ServerDetonate();
	}
}

void AFPSRBossHomingOrb::HandleBlockingHit(UPrimitiveComponent* HitComp, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	FVector NormalImpulse, const FHitResult& Hit)
{
	if (HasAuthority() && State != EOrbState::DeathDwell && State != EOrbState::Grace)
	{
		// Geometry detonates it (user decision). Cover therefore STOPS the orb without making you safe from it —
		// you break the threat with distance, not by putting a wall at your back.
		ServerDetonate();
	}
}

void AFPSRBossHomingOrb::HandleDeath(AActor* DeadActor, AActor* Killer)
{
	if (!HasAuthority())
	{
		return;
	}

	// Stop dead in place and start the dwell. Destroying right here would race the bDead replication, and clients
	// would just see the orb blink out with no break at all.
	if (Movement)
	{
		Movement->StopMovementImmediately();
		Movement->Deactivate();
	}
	if (Sphere)
	{
		Sphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	HandleDestroyedCosmetic(); // authority half; clients get theirs from OnRep_bDead
	State = EOrbState::DeathDwell;
	StateElapsedSeconds = 0.0f;
}
