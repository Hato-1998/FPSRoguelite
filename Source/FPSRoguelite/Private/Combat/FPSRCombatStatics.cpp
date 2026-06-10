// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/FPSRCombatStatics.h"
#include "FPSRCollisionChannels.h"
#include "Enemy/FPSREnemyHealthComponent.h"
#include "Enemy/FPSREnemyBase.h"
#include "Hero/FPSRCharacter.h"
#include "Core/FPSRGameState.h"
#include "Core/FPSRPlayerController.h"

#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "CollisionShape.h"

namespace FPSRCombat
{
	/** Small upward bias added to each radial knockback direction so a foot-level blast pops targets up (rocket
	 *  jump / launch feel) instead of sliding them flat along the ground. */
	static constexpr float KnockbackUpwardBias = 0.35f;

	bool IsFriendlyFireEnabled(const UWorld* World)
	{
		const AFPSRGameState* GS = World ? World->GetGameState<AFPSRGameState>() : nullptr;
		return GS && GS->IsFriendlyFireEnabled();
	}

	float GetFriendlyFireScale(const UWorld* World)
	{
		const AFPSRGameState* GS = World ? World->GetGameState<AFPSRGameState>() : nullptr;
		return GS ? GS->GetFriendlyFireDamageScale() : 0.5f;
	}

	void AddDamageablePawnObjectTypes(FCollisionObjectQueryParams& OutParams)
	{
		OutParams.AddObjectTypesToQuery(ECC_Pawn);            // swarm enemies
		OutParams.AddObjectTypesToQuery(ECC_FPSRPlayerPawn);  // player characters (distinct object channel)
	}

	float ResolveDamage(const AActor* Instigator, const AActor* Target, float BaseDamage, bool bAllowSelf, const UWorld* World)
	{
		if (!Target || BaseDamage <= 0.0f)
		{
			return 0.0f;
		}

		// Self: only explosions self-damage (bAllowSelf); direct shots/melee never hit the instigator.
		if (Target == Instigator)
		{
			return bAllowSelf ? BaseDamage : 0.0f;
		}

		// Swarm enemy (identified by its non-GAS health component): always full damage.
		if (Target->FindComponentByClass<UFPSREnemyHealthComponent>())
		{
			return BaseDamage;
		}

		// Another player (friendly): only when friendly fire is enabled, scaled.
		if (Target->IsA(AFPSRCharacter::StaticClass()))
		{
			return IsFriendlyFireEnabled(World) ? BaseDamage * GetFriendlyFireScale(World) : 0.0f;
		}

		return 0.0f;
	}

	FDamageResult ApplyDamage(AActor* Target, float FinalDamage, AActor* Instigator)
	{
		FDamageResult Result;
		if (!Target || FinalDamage <= 0.0f)
		{
			return Result;
		}

		if (UFPSREnemyHealthComponent* HealthComp = Target->FindComponentByClass<UFPSREnemyHealthComponent>())
		{
			HealthComp->ApplyDamage(FinalDamage, Instigator);
			Result.bApplied = true;
			Result.bWasEnemy = true;
			Result.bKilled = HealthComp->IsDead();
			return Result;
		}

		if (AFPSRCharacter* Character = Cast<AFPSRCharacter>(Target))
		{
			// Player death (DBNO) is a later phase and isn't reported back here, so bKilled stays false — friendly
			// knockback therefore always lands (intended: allies get launched).
			Character->ApplyContactDamage(FinalDamage, Instigator);
			Result.bApplied = true;
			return Result;
		}

		return Result;
	}

	void NotifyHitMarker(const AActor* Instigator, bool bCrit, bool bKill)
	{
		const APawn* InstigatorPawn = Cast<APawn>(Instigator);
		AController* InstigatorController = InstigatorPawn ? InstigatorPawn->GetController() : nullptr;
		if (AFPSRPlayerController* OwnerPC = Cast<AFPSRPlayerController>(InstigatorController))
		{
			const EFPSRHitMarkerType MarkerType = bKill ? EFPSRHitMarkerType::Kill
				: (bCrit ? EFPSRHitMarkerType::Crit : EFPSRHitMarkerType::Hit);
			OwnerPC->ClientNotifyHitMarker(MarkerType);
		}
	}

	void ApplyKnockback(AActor* Target, const FVector& Velocity)
	{
		if (!Target || Velocity.IsNearlyZero())
		{
			return;
		}

		if (AFPSRCharacter* Character = Cast<AFPSRCharacter>(Target))
		{
			// Additive launch (false,false): keep existing velocity so a foot-blast + jump compounds into a rocket
			// jump. Server-authoritative; the character movement component replicates the resulting motion.
			Character->LaunchCharacter(Velocity, false, false);
			return;
		}

		if (AFPSREnemyBase* Enemy = Cast<AFPSREnemyBase>(Target))
		{
			Enemy->ApplyKnockback(Velocity);
		}
	}

	void ApplyExplosion(UWorld* World, const FVector& Center, float Radius, float Damage,
		float CritChance, float CritMultiplier, AActor* Instigator, bool bAllowSelf, float KnockbackStrength)
	{
		if (!World || Radius <= 0.0f)
		{
			return;
		}

		// Query pawns by OBJECT TYPE (both enemy and player channels), NOT a trace channel: a target that has set
		// its Pawn response to Ignore (e.g. a dashing player) is still found, so the blast can't be dodged by a
		// transient response change. Do NOT ignore the instigator — self-damage/self-knockback are resolved below.
		FCollisionObjectQueryParams ObjectParams;
		AddDamageablePawnObjectTypes(ObjectParams);
		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(FPSRExplosion), false, nullptr);

		TArray<FOverlapResult> Overlaps;
		World->OverlapMultiByObjectType(Overlaps, Center, FQuat::Identity,
			ObjectParams, FCollisionShape::MakeSphere(Radius), QueryParams);

		TSet<AActor*> Processed;
		bool bAnyEnemyHit = false;
		bool bAnyCrit = false;
		bool bAnyKill = false;

		for (const FOverlapResult& Overlap : Overlaps)
		{
			AActor* Target = Overlap.GetActor();
			if (!Target || Processed.Contains(Target))
			{
				continue;
			}
			Processed.Add(Target);

			// Per-target crit roll, then self/friendly resolution (may be 0 = no damage but knockback can still apply).
			float BaseDamage = Damage;
			bool bCrit = false;
			if (CritChance > 0.0f && FMath::FRand() < CritChance)
			{
				BaseDamage *= CritMultiplier;
				bCrit = true;
			}

			const float FinalDamage = ResolveDamage(Instigator, Target, BaseDamage, bAllowSelf, World);
			FDamageResult Result;
			if (FinalDamage > 0.0f)
			{
				Result = ApplyDamage(Target, FinalDamage, Instigator);
			}

			if (Result.bWasEnemy && Result.bApplied)
			{
				bAnyEnemyHit = true;
				bAnyCrit |= bCrit;
				bAnyKill |= Result.bKilled;
			}

			// Knockback is INDEPENDENT of damage: it applies even at 0 damage (FF-off ally, self-no-damage), and is
			// only skipped for a pawn this blast just killed (avoid launching a ragdolling/despawning corpse).
			if (KnockbackStrength > 0.0f && !Result.bKilled)
			{
				const FVector TargetLoc = Target->GetActorLocation();
				const float Dist = FVector::Dist(Center, TargetLoc);
				const float Falloff = FMath::Clamp(1.0f - Dist / Radius, 0.0f, 1.0f);
				if (Falloff > 0.0f)
				{
					FVector Dir = TargetLoc - Center;
					Dir = Dir.GetSafeNormal();
					Dir = (Dir + FVector(0.0f, 0.0f, KnockbackUpwardBias)).GetSafeNormal(); // slight pop-up
					if (!Dir.IsNearlyZero())
					{
						ApplyKnockback(Target, Dir * KnockbackStrength * Falloff);
					}
				}
			}
		}

		if (bAnyEnemyHit)
		{
			NotifyHitMarker(Instigator, bAnyCrit, bAnyKill); // one marker per explosion (strongest outcome)
		}
	}
}
