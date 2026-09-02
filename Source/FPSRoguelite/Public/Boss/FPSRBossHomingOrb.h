// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Boss/FPSRPatternActorInterface.h"
#include "Combat/FPSRVitals.h"
#include "GameFramework/Actor.h"
#include "FPSRBossHomingOrb.generated.h"

class AFPSRBossBase;
class UFPSREnemyHealthComponent;
class UProjectileMovementComponent;
class USphereComponent;
class UStaticMeshComponent;

/** BOSS1 pattern 3 — a homing orb the players can shoot down.
 *
 *  🔴 **AActor, deliberately not APawn.** FPSRCombat::CanAffectTarget only applies the open-grid reachability gate
 *  to targets that are pawns; everything else bypasses it (a destructible IS the wall, so gating it would make walls
 *  unbreakable). An orb declared as a pawn therefore becomes INVULNERABLE to every weapon the moment it floats over
 *  a blocked cell — and a 60 cm prop is a blocked cell. AFPSRMissionFleeTarget gets away with being a pawn only
 *  because it runs along the floor. The collision OBJECT TYPE is what the weapon queries actually look at, and that
 *  is independent of the C++ class, so an AActor with an ECC_Pawn sphere is found by every damage path exactly as
 *  before while stepping out of the gate. Nothing here needs a controller or possession.
 *
 *  It is also not an AFPSRProjectile: that class's header pins deterministic straight-line movement as the premise
 *  for client-side prediction, and its object type (WorldDynamic) is one the damage queries never gather.
 *
 *  🔴 **Never uses FTimerManager.** Tracking and lifetime run on accumulators fed by the freeze-gated tick, because
 *  world timers keep counting through the §2-2 freeze. The boss pushes the freeze edge in via IFPSRPatternActor. */
UCLASS()
class FPSROGUELITE_API AFPSRBossHomingOrb : public AActor, public IFPSRPatternActor
{
	GENERATED_BODY()

public:
	AFPSRBossHomingOrb();

	UFUNCTION(BlueprintPure, Category = "FPSR|Boss")
	UFPSREnemyHealthComponent* GetHealthComponent() const { return Health; }

	/** Server: hand the orb its target and tuning. It HOVERS first (Params.GraceSeconds) and only then chases —
	 *  the pattern's own telegraph, on top of the boss's system wind-up.
	 *  The damage INSTIGATOR is the orb itself, not the boss: the hit-direction indicator reads the instigator's
	 *  LOCATION, so crediting the boss would draw every orb hit as coming from the arena centre even when the orb
	 *  came up behind the player. Telemetry still classifies it as boss pressure through the owner fallback. */
	void ServerLaunch(AFPSRBossBase* OwningBoss, APawn* Target, const FFPSRBossOrbLaunchParams& Params,
		const FFPSRDamageSpec& InSpec);

	//~IFPSRPatternActor
	virtual void SetSimulationPaused(bool bPaused) override;
	//~End IFPSRPatternActor

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	UFUNCTION()
	void HandleOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex, bool bFromSweep, const FHitResult& Sweep);

	UFUNCTION()
	void HandleBlockingHit(UPrimitiveComponent* HitComp, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		FVector NormalImpulse, const FHitResult& Hit);

	UFUNCTION()
	void HandleDeath(AActor* DeadActor, AActor* Killer);

	/** Shot down by the players — a break, with no blast. Presentation stays entirely in Blueprint. */
	UFUNCTION(BlueprintImplementableEvent, Category = "FPSR|Boss")
	void OnOrbDestroyedCosmetic();

	/** Detonated — it reached a player, hit geometry, or arrived at where its target died. Distinct from the event
	 *  above precisely because these two must LOOK different: one is the players winning the exchange, the other is
	 *  the boss landing it. */
	UFUNCTION(BlueprintImplementableEvent, Category = "FPSR|Boss")
	void OnOrbDetonatedCosmetic();

	UPROPERTY(VisibleAnywhere, Category = "FPSR|Boss|Orb")
	TObjectPtr<USphereComponent> Sphere;

	UPROPERTY(VisibleAnywhere, Category = "FPSR|Boss|Orb")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(VisibleAnywhere, Category = "FPSR|Boss|Orb")
	TObjectPtr<UFPSREnemyHealthComponent> Health;

	UPROPERTY(VisibleAnywhere, Category = "FPSR|Boss|Orb")
	TObjectPtr<UProjectileMovementComponent> Movement;

	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Boss|Orb", meta = (ClampMin = "1.0"))
	float InitialSpeed = 900.0f;

	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Boss|Orb", meta = (ClampMin = "1.0"))
	float MaxSpeed = 1400.0f;

	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Boss|Orb", meta = (ClampMin = "0.0"))
	float HomingAccelerationMagnitude = 2200.0f;

	/** After tracking expires the orb flies straight for this long, then expires on its own. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Boss|Orb", meta = (ClampMin = "0.0"))
	float PostTrackLifetimeSeconds = 3.0f;

	/** Destroying on the death frame would race the bDead replication, so clients would never see the orb break.
	 *  Holding for a moment lets that edge land. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Boss|Orb", meta = (ClampMin = "0.0"))
	float DeathDwellSeconds = 0.15f;

private:
	/** What the orb is doing right now. Modelled explicitly because "chasing" and "heading for the spot where my
	 *  target died" look identical from the outside but end differently. */
	enum class EOrbState : uint8
	{
		/** Hovering by the boss, not yet chasing. */
		Grace,
		/** Homing on a live target. */
		Chase,
		/** The target is gone. Flying to where they last stood, to detonate THERE.
		 *  🔴 This is why losing the hunted player is not a reprieve: the blast lands exactly where teammates would
		 *  gather to revive them. Simply deleting the orbs would have made letting someone die the correct play. */
		DivertToLastKnown,
		/** Shot down; holding briefly so the death replicates before the actor goes. */
		DeathDwell
	};

	/** Server: detonate here — a blast that damages EVERY player in radius — and destroy. */
	void ServerDetonate();

	TWeakObjectPtr<AFPSRBossBase> OwnerBoss;
	TWeakObjectPtr<APawn> TargetPawn;
	FFPSRDamageSpec DamageSpec;
	FFPSRBossOrbLaunchParams LaunchParams;

	/** Where the target last legally stood. Refreshed every tick while chasing, so it is always current when the
	 *  target is lost. */
	FVector LastKnownTargetLocation = FVector::ZeroVector;

	EOrbState State = EOrbState::Grace;
	float StateElapsedSeconds = 0.0f;
	float TrackSecondsRemaining = 0.0f;
	float PostTrackSecondsRemaining = 0.0f;
	bool bSimulationPaused = false;
	FVector PausedVelocity = FVector::ZeroVector;
};
