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

		// Swarm enemy (identified by its non-GAS health component): full damage — INCLUDING the whole stage
		// transition (user decision 2026-08-20, ADR 0010 안 H 정정). There used to be an invulnerability gate here
		// ("dealing window closed -> frozen swarm can't be farmed", invariant 8's enforcement) — retired: with the
		// phase split every transition segment is itself fixed-length, so the farmable window is Grace + FadeOut +
		// blackout hold + FadeIn, all authored numbers. The one variable stretch (a slow client's destination-ready
		// wait, StageSwapReadyTimeoutSeconds-capped and normally 0) is accepted as-is — the swarm the player is
		// grinding is the same swarm that carries over, so extra farm time trades against the next stage's own
		// carry-over pressure rather than being free reward.
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

	FDamageResult ApplyDamage(AActor* Target, float FinalDamage, AActor* Instigator, const FFPSRDamageSpec& Spec)
	{
		// VIT1: Spec (DamageType + anti-shield multiplier) is threaded to the leaf appliers, which resolve the layer
		// coefficients from the target's vitals profile. Replaces U18a's forward-compat `FGameplayTag DamageType` seam.
		FDamageResult Result;
		if (!Target || FinalDamage <= 0.0f)
		{
			return Result;
		}

		if (UFPSREnemyHealthComponent* HealthComp = Target->FindComponentByClass<UFPSREnemyHealthComponent>())
		{
			// Capture pre-state so kill is a TRANSITION, not a post-facto read: a corpse re-hit (already dead,
			// ApplyDamage no-ops) reports bKilled = false — kill markers / kill aggregates never double-fire on a
			// corpse. bCountsAsKill gates the combat-CREDIT axes only. A destructible non-enemy (a door,
			// bCountsAsKill=false) still takes damage and is destroyed — DamageDealt / bApplied / the health
			// component's death all run unchanged — but it never counts as an enemy hit (bWasEnemy) or a kill
			// (bKilled), so on-kill fragments, kill markers, and kill credit don't fire on it. Enemies default true
			// -> no behavior change.
			const bool bCountsAsKill = HealthComp->CountsAsKill();
			const bool bWasDeadBefore = HealthComp->IsDead();
			// 🔴 VIT1 combat regression trap 1: DamageDealt is now the VITALS result's TotalSpent() (shield + health
			// actually removed), not HealthBefore-HealthAfter — a hit a shield fully absorbs must still count as real
			// damage (hit-markers / lifesteal / penetration all key off this), and a corpse re-hit / overkill still
			// naturally reports 0 / clamped (FPSRVitals::ApplyDamage no-ops on a dead target's already-empty pools).
			const FPSRVitals::FResult VitalsResult = HealthComp->ApplyDamage(FinalDamage, Instigator, Spec);
			Result.bApplied = true;
			Result.bWasEnemy = bCountsAsKill;
			Result.DamageDealt = VitalsResult.TotalSpent();
			Result.bShieldBroke = VitalsResult.bShieldBroke;
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
			// Player death (DBNO) is a later phase and isn't reported back here, so bKilled stays false. FF is
			// damage-only: the friendly-player check in ApplyExplosion's knockback loop suppresses the ally launch,
			// so bKilled being false no longer means an ally gets moved (only that the corpse-skip doesn't apply).
			// 🔴 VIT1 G1 P2-4 (오진 정정): DamageDealt IS filled here (TotalSpent — misses/DBNO/i-frame all correctly
			// resolve to 0 via ApplyContactDamage's early-outs), for the mission/director/penetration axes that read
			// "real damage dealt" regardless of target kind. bWasEnemy stays false — the lifesteal event gate below
			// is untouched, so "shoot a friendly to heal off their shield" stays impossible (§6 / §11-7).
			const FPSRVitals::FResult VitalsResult = Character->ApplyContactDamage(FinalDamage, Instigator, Spec);
			Result.bApplied = true;
			Result.DamageDealt = VitalsResult.TotalSpent();
			Result.bShieldBroke = VitalsResult.bShieldBroke;
			// 🔴 The marker gate the filled DamageDealt above would otherwise re-open (see FDamageResult's own
			// comment): every damage path keys its hit-marker on `DamageDealt > 0`, which used to be 0 here.
			Result.bTargetIsPlayer = true;
			return Result;
		}

		return Result;
	}

	EFPSRHitMarkerType ResolveHitMarker(bool bKill, bool bShieldBreak, bool bWeak, bool bCrit)
	{
		if (bKill)        { return EFPSRHitMarkerType::Kill; }
		if (bShieldBreak) { return EFPSRHitMarkerType::ShieldBreak; }
		if (bWeak)        { return EFPSRHitMarkerType::Weak; }
		if (bCrit)        { return EFPSRHitMarkerType::Crit; }
		return EFPSRHitMarkerType::Hit;
	}

	void NotifyHitMarker(const AActor* Instigator, bool bCrit, bool bKill, bool bShieldBreak)
	{
		const APawn* InstigatorPawn = Cast<APawn>(Instigator);
		AController* InstigatorController = InstigatorPawn ? InstigatorPawn->GetController() : nullptr;
		if (AFPSRPlayerController* OwnerPC = Cast<AFPSRPlayerController>(InstigatorController))
		{
			// No bWeak here — an explosion has no single targeted weakpoint (ResolveHitMarker's shared priority order).
			const EFPSRHitMarkerType MarkerType = ResolveHitMarker(bKill, bShieldBreak, /*bWeak*/ false, bCrit);
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
		float CritChance, float CritMultiplier, AActor* Instigator, bool bAllowSelf, float KnockbackStrength, const FFPSRDamageSpec& Spec)
	{
		// VIT1: Spec (DamageType + anti-shield multiplier) is forwarded to every per-target ApplyDamage below.
		// Replaces U18a's forward-compat `FGameplayTag DamageType` seam.
		FExplosionResult Outcome;
		if (!World || Radius <= 0.0f)
		{
			return Outcome;
		}

		// Query pawns by OBJECT TYPE (both enemy and player channels), NOT a trace channel: a target that has set
		// its Pawn response to Ignore (e.g. a player in a post-revive grace window, or a downed one) is still found,
		// so the blast can't be dodged by a transient response change. Do NOT ignore the instigator —
		// self-damage/self-knockback are resolved below.
		FCollisionObjectQueryParams ObjectParams;
		AddDamageablePawnObjectTypes(ObjectParams);
		// Breakable geometry too: a rocket splashing a door / suppressor has to damage it. These moved off the
		// player channel onto their own, so the pawn types alone no longer reach them.
		AddDestructibleObjectType(ObjectParams);
		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(FPSRExplosion), false, nullptr);

		TArray<FOverlapResult> Overlaps;
		World->OverlapMultiByObjectType(Overlaps, Center, FQuat::Identity,
			ObjectParams, FCollisionShape::MakeSphere(Radius), QueryParams);

		TSet<AActor*> Processed;
		Processed.Reserve(Overlaps.Num());
		bool bAnyEnemyHit = false;
		bool bAnyCrit = false;
		bool bAnyKill = false;
		bool bAnyShieldBroke = false;
		bool bAnyDamageDealt = false; // visual marker: enemies AND destructible doors only — never a player (FF ally or self-damage)

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
				Result = ApplyDamage(Target, FinalDamage, Instigator, Spec);
			}

			// !bTargetIsPlayer: an FF ally — and, critically, the instigator's OWN self-damage (rocket jump), which
			// is FF-independent — must not raise a hit-marker. See FDamageResult::bTargetIsPlayer.
			if (Result.DamageDealt > 0.0f && !Result.bTargetIsPlayer)
			{
				bAnyDamageDealt = true; // visual marker for enemies AND destructible doors
				if (Result.bWasEnemy)
				{
					bAnyEnemyHit = true;
					bAnyCrit |= bCrit;
					bAnyKill |= Result.bKilled;
					bAnyShieldBroke |= Result.bShieldBroke;
				}
			}
			if (Result.bKilled)
			{
				Outcome.KilledEnemies.Add(Target); // freshly killed (alive->dead this blast) — drives the weapon OnKill bridge
			}

			// Knockback is INDEPENDENT of damage (it can apply at 0 damage, e.g. self-no-damage), but FF is DAMAGE-ONLY:
			// a friendly ally (another player) is never launched by a teammate's blast — FF damage still lands, only the
			// movement is suppressed. Self-knockback (rocket jump, Target == Instigator) and enemy knockback are
			// unaffected. Also skip a pawn this blast just killed (avoid launching a ragdolling/despawning corpse).
			const bool bFriendlyPlayer = (Target != Instigator) && Target->IsA(AFPSRCharacter::StaticClass());
			if (KnockbackStrength > 0.0f && !Result.bKilled && !bFriendlyPlayer)
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

		// Fires on ANY damage dealt — enemies AND destructible doors (door-only blast => plain Hit, Crit/Kill/ShieldBreak enemy-only).
		if (bAnyDamageDealt)
		{
			NotifyHitMarker(Instigator, bAnyCrit, bAnyKill, bAnyShieldBroke); // one marker per explosion (strongest outcome)
		}

		Outcome.bAnyEnemyHit = bAnyEnemyHit;
		return Outcome;
	}

	void AddWeakpointObjectType(FCollisionObjectQueryParams& OutParams)
	{
		OutParams.AddObjectTypesToQuery(ECC_FPSRWeakpoint);
	}

	void AddDestructibleObjectType(FCollisionObjectQueryParams& OutParams)
	{
		OutParams.AddObjectTypesToQuery(ECC_FPSRDestructible); // doors + arena props (억제기)
	}

	bool IsDestructibleGeometry(const UPrimitiveComponent* Component)
	{
		return Component && Component->GetCollisionObjectType() == ECC_FPSRDestructible;
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
		ActorToIndex.Reserve(InHits.Num());
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
