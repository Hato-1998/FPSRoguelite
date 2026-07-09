// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/FPSRCombatStatics.h"
#include "FPSRCollisionChannels.h"
#include "Combat/FPSRWeakpointComponent.h"
#include "Enemy/FPSREnemyHealthComponent.h"
#include "Enemy/FPSREnemyBase.h"
#include "Enemy/FPSRFlowFieldSubsystem.h"
#include "Enemy/FPSRFlowFieldComputer.h"
#include "Hero/FPSRCharacter.h"
#include "Core/FPSRGameState.h"
#include "Core/FPSRPlayerController.h"
#include "Core/FPSRPlayerState.h"
#include "AbilitySystem/FPSRAbilitySystemComponent.h"
#include "AbilitySystemComponent.h"

#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "Components/PrimitiveComponent.h"
#include "GameFramework/Actor.h"
#include "CollisionShape.h"

namespace FPSRCombat
{
	/** Small upward bias added to each radial knockback direction so a foot-level blast pops targets up (rocket
	 *  jump / launch feel) instead of sliding them flat along the ground. */
	static constexpr float KnockbackUpwardBias = 0.35f;

	bool CanAffectTarget(const UWorld* World, const AActor* Instigator, const AActor* Target, const FVector& OriginLocation)
	{
		if (Target && Target == Instigator)
		{
			return true; // self is always reachable; bAllowSelf (caller) decides actual self-damage/knockback (rocket jump)
		}
		// The reachability gate applies to PAWNS only (swarm enemies / players). A damageable DOOR is the wall itself — its
		// actor sits on the seam / a blocked gap cell (not a walkable pawn surface), so connectivity would wrongly zero its
		// damage and make streaming gates unbreakable (Codex R11). Non-pawn damageables bypass the gate — there is no "across
		// a wall" concern when shooting the wall itself; ResolveDamage still resolves them.
		if (!Target || !Target->IsA(APawn::StaticClass()))
		{
			return true;
		}
		if (const UFPSRFlowFieldSubsystem* FF = World ? World->GetSubsystem<UFPSRFlowFieldSubsystem>() : nullptr)
		{
			if (const UFPSRFlowFieldComputer* Unified = FF->GetMultiSlotUnifiedComputer())
			{
				// U (P-C): gate on ORIGIN<->TARGET open-grid connectivity — a closed door/wall between them blocks damage/AOE,
				// an open door connects them. AreWorldLocationsConnected fails closed off-grid AND while connectivity is stale
				// (post-mutation, pre-RunBFS) — but connectivity is rebuilt every RunBFS regardless of flow sources, so a
				// source-less field (players airborne/unsnapped) still gates correctly instead of leaking through walls (R15).
				return Unified->AreWorldLocationsConnected(OriginLocation, Target->GetActorLocation());
			}
		}
		// P-G: no MULTI-SLOT unified grid (single-map degenerate grid / pre-build / off-authority) -> allow. A single-map run
		// has no cross-map walls to gate against (the connectivity gate is a multimap notion); this preserves the pre-P-G
		// single-map "MapId allow-all" behavior exactly, without the per-actor MapId lookup.
		return true;
	}

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

	float ResolveDamage(const AActor* Instigator, const AActor* Target, float BaseDamage, bool bAllowSelf, const UWorld* World, const FVector* OriginOverride)
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

		// Reachability guard (P-C): no damage across a closed door/wall (or, pre-U, a streamed map boundary). Origin = the
		// blast Center (explosions pass OriginOverride) or the instigator's location (direct shots). Self is exempt above.
		const FVector Origin = OriginOverride ? *OriginOverride : (Instigator ? Instigator->GetActorLocation() : FVector::ZeroVector);
		if (!CanAffectTarget(World, Instigator, Target, Origin))
		{
			return 0.0f;
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

	/** GAS-native character behavior bridge (U18c §2-3-5): tell the instigating player's ASC how much real damage it
	 *  just dealt, so a lifesteal/regen-style passive GA can react. Gated on a cheap per-player listener count, so a
	 *  player who never picked such a card pays ~nothing on this hot path (the cost scales with that player's
	 *  triggered-ability count, never with enemy count). Server-only (every ApplyDamage caller is authority-gated). */
	static void SendDealtDamageEvent(AActor* Instigator, float DamageDealt)
	{
		APawn* InstigatorPawn = Cast<APawn>(Instigator);
		if (!InstigatorPawn)
		{
			return;
		}
		const AFPSRPlayerState* PS = InstigatorPawn->GetPlayerState<AFPSRPlayerState>();
		if (!PS || !PS->HasDamageEventListeners())
		{
			return;
		}
		if (UAbilitySystemComponent* ASC = PS->GetFPSRAbilitySystemComponent())
		{
			static const FGameplayTag DealtDamageTag = FGameplayTag::RequestGameplayTag(FName("GameplayEvent.Player.DealtDamage"));
			FGameplayEventData EventData;
			EventData.EventTag = DealtDamageTag;
			EventData.Instigator = InstigatorPawn;
			EventData.EventMagnitude = DamageDealt;
			ASC->HandleGameplayEvent(DealtDamageTag, &EventData);
		}
	}

	FDamageResult ApplyDamage(AActor* Target, float FinalDamage, AActor* Instigator, FGameplayTag DamageType)
	{
		// U18a forward-compat seam: DamageType (empty = Physical) is threaded to leaf appliers for D3 elemental; no behavior change in U18a.
		FDamageResult Result;
		if (!Target || FinalDamage <= 0.0f)
		{
			return Result;
		}

		if (UFPSREnemyHealthComponent* HealthComp = Target->FindComponentByClass<UFPSREnemyHealthComponent>())
		{
			// Capture pre-state so kill/damage are TRANSITIONS, not post-facto reads: a corpse re-hit (already dead,
			// ApplyDamage no-ops) and an overkill (damage clamped to remaining health) both report DamageDealt = 0
			// and bKilled = false — so feedback (markers / lifesteal event) never fires on a corpse or rewards overkill.
			// bCountsAsKill gates the combat-CREDIT axes only. A destructible non-enemy (a door, bCountsAsKill=false)
			// still takes damage and is destroyed — DamageDealt / bApplied / the health component's death all run
			// unchanged — but it never counts as an enemy hit (bWasEnemy) or a kill (bKilled), so on-kill fragments,
			// kill markers, and kill credit don't fire on it. Enemies default true → no behavior change.
			const bool bCountsAsKill = HealthComp->CountsAsKill();
			const bool bWasDeadBefore = HealthComp->IsDead();
			const float HealthBefore = HealthComp->GetHealth();
			HealthComp->ApplyDamage(FinalDamage, Instigator, DamageType);
			Result.bApplied = true;
			Result.bWasEnemy = bCountsAsKill;
			Result.DamageDealt = FMath::Max(0.0f, HealthBefore - HealthComp->GetHealth());
			Result.bKilled = bCountsAsKill && (!bWasDeadBefore && HealthComp->IsDead());

			// GAS-native character behavior (lifesteal etc.): event carries the REAL damage dealt (corpse/overkill = 0).
			// Gated on bWasEnemy too, so shooting a door (bCountsAsKill=false) can't feed lifesteal / heal-on-damage
			// (no farming health off a high-HP destructible).
			if (Result.bWasEnemy && Result.DamageDealt > 0.0f)
			{
				SendDealtDamageEvent(Instigator, Result.DamageDealt);
			}
			return Result;
		}

		if (AFPSRCharacter* Character = Cast<AFPSRCharacter>(Target))
		{
			// Player death (DBNO) is a later phase and isn't reported back here, so bKilled stays false — friendly
			// knockback therefore always lands (intended: allies get launched).
			Character->ApplyContactDamage(FinalDamage, Instigator, DamageType);
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

	FExplosionResult ApplyExplosion(UWorld* World, const FVector& Center, float Radius, float Damage,
		float CritChance, float CritMultiplier, AActor* Instigator, bool bAllowSelf, float KnockbackStrength, FGameplayTag DamageType)
	{
		// U18a forward-compat seam: DamageType (empty = Physical) is threaded to leaf appliers for D3 elemental; no behavior change in U18a.
		FExplosionResult Outcome;
		if (!World || Radius <= 0.0f)
		{
			return Outcome;
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
		bool bAnyDamageDealt = false; // visual marker: enemies AND destructible doors (friendly players leave DamageDealt 0)

		for (const FOverlapResult& Overlap : Overlaps)
		{
			AActor* Target = Overlap.GetActor();
			if (!Target || Processed.Contains(Target))
			{
				continue;
			}
			Processed.Add(Target);

			// Reachability guard (P-C): skip a target the blast can't reach — no damage AND no knockback across a closed
			// door/wall (or, pre-U, a streamed boundary). Origin = the blast Center, NOT the instigator. Self exempt.
			if (!CanAffectTarget(World, Instigator, Target, Center))
			{
				continue;
			}

			// Per-target crit roll, then self/friendly resolution (may be 0 = no damage but knockback can still apply).
			float BaseDamage = Damage;
			bool bCrit = false;
			if (CritChance > 0.0f && FMath::FRand() < CritChance)
			{
				BaseDamage *= CritMultiplier;
				bCrit = true;
			}

			const float FinalDamage = ResolveDamage(Instigator, Target, BaseDamage, bAllowSelf, World, &Center);
			FDamageResult Result;
			if (FinalDamage > 0.0f)
			{
				Result = ApplyDamage(Target, FinalDamage, Instigator, DamageType);
			}

			if (Result.DamageDealt > 0.0f)
			{
				bAnyDamageDealt = true; // visual marker for enemies AND destructible doors (not friendly players)
				if (Result.bWasEnemy)
				{
					bAnyEnemyHit = true;
					bAnyCrit |= bCrit;
					bAnyKill |= Result.bKilled;
				}
			}
			if (Result.bKilled)
			{
				Outcome.KilledEnemies.Add(Target); // freshly killed (alive->dead this blast) — drives the weapon OnKill bridge
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

		// Fires on ANY damage dealt — enemies AND destructible doors (door-only blast => plain Hit, Crit/Kill enemy-only).
		if (bAnyDamageDealt)
		{
			NotifyHitMarker(Instigator, bAnyCrit, bAnyKill); // one marker per explosion (strongest outcome)
		}

		Outcome.bAnyEnemyHit = bAnyEnemyHit;
		return Outcome;
	}

	void AddWeakpointObjectType(FCollisionObjectQueryParams& OutParams)
	{
		OutParams.AddObjectTypesToQuery(ECC_FPSRWeakpoint);
	}

	float GetWeakpointMultiplier(const UPrimitiveComponent* Component)
	{
		const UFPSRWeakpointComponent* Weakpoint = Cast<UFPSRWeakpointComponent>(Component);
		return Weakpoint ? FMath::Max(1.0f, Weakpoint->DamageMultiplier) : 1.0f;
	}

	float GetBestWeakpointMultiplierForSphere(const AActor* Target, const FVector& SphereCenter, float SphereRadius)
	{
		if (!Target)
		{
			return 1.0f;
		}
		float Best = 1.0f;
		TArray<UFPSRWeakpointComponent*> Weakpoints;
		Target->GetComponents<UFPSRWeakpointComponent>(Weakpoints);
		for (const UFPSRWeakpointComponent* Wp : Weakpoints)
		{
			// Skip a weakpoint whose query collision is disabled (e.g. a phase-gated boss spot the designer toggled
			// off). The line-trace paths (hitscan/charge-laser) already miss such a component, so the sphere paths
			// (projectile/melee) must match — otherwise the same disabled spot would still boost those hits.
			if (!Wp || !Wp->IsQueryCollisionEnabled())
			{
				continue;
			}
			const float CombinedRadius = SphereRadius + Wp->GetScaledSphereRadius();
			if (FVector::DistSquared(SphereCenter, Wp->GetComponentLocation()) <= CombinedRadius * CombinedRadius)
			{
				Best = FMath::Max(Best, FMath::Max(1.0f, Wp->DamageMultiplier));
			}
		}
		return Best;
	}

	void DedupePawnHitsByActor(const TArray<FHitResult>& InHits, TArray<FResolvedHit>& OutHits)
	{
		// InHits is distance-sorted (LineTraceMulti). First time we see an actor we record its nearest hit; later
		// hits on the same actor only raise the weakpoint multiplier. Output order = nearest-first insertion order.
		TMap<const AActor*, int32> ActorToIndex;
		for (const FHitResult& Hit : InHits)
		{
			AActor* HitActor = Hit.GetActor();
			if (!HitActor)
			{
				continue;
			}
			const float Mult = GetWeakpointMultiplier(Hit.GetComponent());
			if (int32* Found = ActorToIndex.Find(HitActor))
			{
				OutHits[*Found].WeakpointMultiplier = FMath::Max(OutHits[*Found].WeakpointMultiplier, Mult);
			}
			else
			{
				FResolvedHit Entry;
				Entry.Actor = HitActor;
				Entry.Distance = Hit.Distance;
				Entry.ImpactPoint = Hit.ImpactPoint;
				Entry.WeakpointMultiplier = Mult;
				ActorToIndex.Add(HitActor, OutHits.Add(Entry));
			}
		}
	}
}
