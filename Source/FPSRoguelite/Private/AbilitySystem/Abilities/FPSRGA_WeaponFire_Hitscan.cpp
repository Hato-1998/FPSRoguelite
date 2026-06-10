// Copyright Epic Games, Inc. All Rights Reserved.

#include "AbilitySystem/Abilities/FPSRGA_WeaponFire_Hitscan.h"
#include "AbilitySystem/Attributes/FPSRCombatSet.h"
#include "Weapon/FPSRWeaponInventoryComponent.h"
#include "Weapon/FPSRWeaponInstance.h"
#include "Weapon/FPSRWeaponFragment.h"
#include "Weapon/FPSRWeaponDataAsset.h"
#include "Weapon/FPSRWeaponFireComponent.h"
#include "Enemy/FPSREnemyHealthComponent.h"
#include "Core/FPSRGameState.h"
#include "Core/FPSRPlayerController.h"
#include "Core/FPSRLogChannels.h"

#include "AbilitySystemComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"

UFPSRGA_WeaponFire_Hitscan::UFPSRGA_WeaponFire_Hitscan()
{
}

void UFPSRGA_WeaponFire_Hitscan::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	APawn* Avatar = Cast<APawn>(ActorInfo->AvatarActor.Get());
	AController* Controller = Avatar ? Avatar->GetController() : nullptr;
	UWorld* World = Avatar ? Avatar->GetWorld() : nullptr;
	if (!Avatar || !Controller || !World)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// No firing while the run is frozen for card selection (Game.MD §2-2).
	if (const AFPSRGameState* RunState = World->GetGameState<AFPSRGameState>())
	{
		if (RunState->IsRunPaused())
		{
			EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
			return;
		}
	}

	// Resolve weapon stats from the equipped weapon instance (base stats × accumulated modifiers; fallback to defaults).
	float Damage = 10.0f;
	float Range = 10000.0f;
	float SpreadDegrees = 1.0f;
	int32 PelletCount = 1;
	int32 MaxPenetration = 1;
	UFPSRWeaponInventoryComponent* Inventory = Avatar->FindComponentByClass<UFPSRWeaponInventoryComponent>();
	UFPSRWeaponInstance* Instance = Inventory ? Inventory->GetCurrentInstance() : nullptr;
	const FFPSRWeaponStatBlock* Stats = Instance ? &Instance->GetResolvedStats() : nullptr;
	if (Stats)
	{
		Damage = Stats->Damage;
		Range = Stats->Range;
		SpreadDegrees = Stats->SpreadDegrees;
		PelletCount = FMath::Clamp(Stats->PelletCount, 1, 32); // upper-bound authored data to cap traces/activation
		MaxPenetration = FMath::Max(1, Stats->MaxPenetration);
	}

	// Server-authoritative gates: empty mag / reloading / fire-rate. Ammo is consumed later, once the fragment
	// hooks have determined the round count (each round costs one magazine round; a round fires PelletCount pellets, §2-4-1).
	if (Avatar->HasAuthority() && Inventory)
	{
		if (Inventory->IsReloading() || Inventory->GetCurrentAmmo() <= 0)
		{
			EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
			return;
		}
		if (Stats)
		{
			const float MinInterval = 1.0f / FMath::Max(Stats->FireRate, 0.01f);
			if (!Inventory->ServerTryConsumeFireInterval(MinInterval))
			{
				EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
				return;
			}
		}
	}

	// Add bloom from sustained fire, then tighten spread while aiming down sights.
	if (UFPSRWeaponFireComponent* FireComp = Avatar->FindComponentByClass<UFPSRWeaponFireComponent>())
	{
		SpreadDegrees += FireComp->GetCurrentBloom();
		if (FireComp->IsAiming() && Stats && Stats->bHasADS)
		{
			SpreadDegrees *= Stats->ADSSpreadMultiplier;
		}
	}

	// Behavior fragments (P4-B-2): build the per-activation context and let fragments adjust the shot count
	// (multishot / shotgun spread). Hooks are stateless and run a handful of times — no per-hit allocation.
	FFPSRFireContext FireCtx;
	FireCtx.Avatar = Avatar;
	FireCtx.Controller = Controller;
	FireCtx.World = World;
	FireCtx.Instance = Instance;
	FireCtx.ShotCount = 1;
	FireCtx.bAuthority = Avatar->HasAuthority();

	const TArray<TObjectPtr<UFPSRWeaponFragment>>* Fragments = Instance ? &Instance->GetActiveFragments() : nullptr;
	if (Fragments)
	{
		for (const TObjectPtr<UFPSRWeaponFragment>& Frag : *Fragments)
		{
			if (Frag) { Frag->PreFire(FireCtx); }
		}
		for (const TObjectPtr<UFPSRWeaponFragment>& Frag : *Fragments)
		{
			if (Frag) { Frag->ModifyShotCount(FireCtx); }
		}
	}
	int32 NumRounds = FMath::Clamp(FireCtx.ShotCount, 1, 32);
	// One trigger pull may fire multiple rounds via multishot; each round costs one magazine round.
	// A round fires PelletCount pellets. Clamp the round count to the rounds actually loaded —
	// CurrentAmmo is replicated, so the server and the owning client clamp to the same pre-fire value —
	// then the server deducts exactly that many. The empty-mag gate above guarantees at least one round
	// remains, so at least one round always fires.
	if (Inventory)
	{
		NumRounds = FMath::Min(NumRounds, FMath::Max(Inventory->GetCurrentAmmo(), 1));
		if (Avatar->HasAuthority())
		{
			Inventory->ConsumeAmmo(NumRounds);
		}
	}

	// Crit/damage multipliers from the ASC are fetched once; crit is rolled per hit so each pellet / pierced
	// enemy can crit independently.
	float DamageMultiplier = 1.0f;
	float CritChance = 0.0f;
	float CritMultiplier = 1.0f;
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		CritChance = ASC->GetNumericAttribute(UFPSRCombatSet::GetGlobalCritChanceAttribute());
		CritMultiplier = ASC->GetNumericAttribute(UFPSRCombatSet::GetGlobalCritMultiplierAttribute());
		DamageMultiplier = ASC->GetNumericAttribute(UFPSRCombatSet::GetGlobalDamageMultiplierAttribute());
	}

	// Trace from the player view point; each pellet is randomized within the spread cone.
	FVector ViewLocation;
	FRotator ViewRotation;
	Controller->GetPlayerViewPoint(ViewLocation, ViewRotation);
	const FVector Start = ViewLocation;
	const FVector BaseDir = ViewRotation.Vector();

	// Hit-marker feedback aggregated across pellets so a multishot fires at most one pulse per activation
	// (Game.MD §2-14). All markers are server-authoritative: with random spread the client and server traces
	// can diverge, so a client-predicted "Hit" could be a false positive / miss vs the authoritative damage.
	// The server confirms the strongest outcome to the owning client (Game.MD §6-2 server authority).
	bool bServerHit = false;
	bool bServerCrit = false;
	bool bServerKill = false;

	// Lambda to apply damage to a single hit actor (factors out logic for both single-trace and pierce paths).
	auto ApplyDamageToActor = [&](AActor* HitActor) -> void
	{
		if (!HitActor)
		{
			return;
		}

		UFPSREnemyHealthComponent* HealthComp = HitActor->FindComponentByClass<UFPSREnemyHealthComponent>();
		if (!HealthComp)
		{
			return;
		}

		if (FireCtx.bAuthority)
		{
			float FinalDamage = Damage * DamageMultiplier;
			bool bCrit = false;
			if (CritChance > 0.0f && FMath::FRand() < CritChance)
			{
				FinalDamage *= CritMultiplier;
				bCrit = true;
			}

			// Per-hit behavior hooks (e.g. bonus/leech) can adjust the damage before it lands.
			if (Fragments)
			{
				for (const TObjectPtr<UFPSRWeaponFragment>& Frag : *Fragments)
				{
					if (Frag) { Frag->OnHitActor(FireCtx, HitActor, FinalDamage); }
				}
			}

			HealthComp->ApplyDamage(FinalDamage, Avatar);
			bServerHit = true;
			if (HealthComp->IsDead()) { bServerKill = true; }
			else if (bCrit) { bServerCrit = true; }

			UE_LOG(LogFPSR, Verbose, TEXT("[Fire] hit=%s dmg=%.1f"), *GetNameSafe(HitActor), FinalDamage);
		}
	};

	// Nested loop: outer over rounds (each costs 1 ammo), inner over pellets per round.
	for (int32 Round = 0; Round < NumRounds; ++Round)
	{
		for (int32 Pellet = 0; Pellet < PelletCount; ++Pellet)
		{
			const FVector PelletDir = (SpreadDegrees > 0.0f)
				? FMath::VRandCone(BaseDir, FMath::DegreesToRadians(SpreadDegrees))
				: BaseDir;
			const FVector End = Start + PelletDir * Range;

			// Branch on penetration: single-trace for low penetration, multi-trace for piercing.
			if (MaxPenetration <= 1)
			{
				// Single-trace path: rifle, shotgun, burst — stops at first enemy.
				FHitResult Hit;
				FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(FPSRWeaponFire), false, Avatar);
				const bool bHit = World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, QueryParams);

#if ENABLE_DRAW_DEBUG
				const FVector DebugEnd = bHit ? Hit.ImpactPoint : End;
				DrawDebugLine(World, Start, DebugEnd, FColor::Yellow, false, 0.5f, 0, 1.0f);
				if (bHit)
				{
					DrawDebugPoint(World, Hit.ImpactPoint, 10.0f, FColor::Red, false, 0.5f);
				}
#endif

				AActor* HitActor = bHit ? Hit.GetActor() : nullptr;
				ApplyDamageToActor(HitActor);
			}
			else
			{
				// Pierce path: sniper — pass through enemies but stop at world geometry that blocks the weapon
				// (Visibility) channel. Gather the pawns on the line first, then run a single Visibility trace
				// that ignores those pawns: the wall cutoff then matches exactly what blocks a normal hitscan
				// shot (movable cover/doors that block Visibility included), while query-only dynamics that
				// ignore Visibility — e.g. in-flight AFPSRProjectile collision spheres — do NOT count as walls.
				FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(FPSRWeaponFire), false, Avatar);

				// Find all pawns along the line (enemies are pierced; non-enemy pawns are skipped below).
				TArray<FHitResult> PawnHits;
				FCollisionObjectQueryParams PawnObjParams;
				PawnObjParams.AddObjectTypesToQuery(ECC_Pawn);
				World->LineTraceMultiByObjectType(PawnHits, Start, End, PawnObjParams, QueryParams);

				// Wall = first Visibility-blocking geometry, ignoring the pawns (which also block Visibility)
				// so an enemy never masquerades as the wall and shortens the pierce.
				FCollisionQueryParams WallParams(SCENE_QUERY_STAT(FPSRWeaponFireWall), false, Avatar);
				for (const FHitResult& PawnHit : PawnHits)
				{
					if (AActor* PawnActor = PawnHit.GetActor()) { WallParams.AddIgnoredActor(PawnActor); }
				}
				FHitResult WallHit;
				const bool bWall = World->LineTraceSingleByChannel(WallHit, Start, End, ECC_Visibility, WallParams);
				const float WallDist = bWall ? WallHit.Distance : Range;

#if ENABLE_DRAW_DEBUG
				DrawDebugLine(World, Start, Start + PelletDir * FMath::Min(WallDist, Range), FColor::Yellow, false, 0.5f, 0, 1.0f);
#endif

				// Apply damage to up to MaxPenetration enemies (skipping those behind the wall).
				int32 PenetrationCount = 0;
				for (const FHitResult& PawnHit : PawnHits)
				{
					if (PawnHit.Distance > WallDist)
					{
						continue; // Behind the wall.
					}

					AActor* HitActor = PawnHit.GetActor();
					if (!HitActor)
					{
						continue;
					}

					UFPSREnemyHealthComponent* HealthComp = HitActor->FindComponentByClass<UFPSREnemyHealthComponent>();
					if (!HealthComp)
					{
						continue;
					}

					ApplyDamageToActor(HitActor);
					++PenetrationCount;
					if (PenetrationCount >= MaxPenetration)
					{
						break;
					}
				}
			}
		}
	}

	// Server delivers one marker per activation to the owning client — strongest outcome (Kill > Crit > Hit).
	if (FireCtx.bAuthority && bServerHit)
	{
		if (AFPSRPlayerController* OwnerPC = Cast<AFPSRPlayerController>(Controller))
		{
			const EFPSRHitMarkerType MarkerType = bServerKill ? EFPSRHitMarkerType::Kill
				: (bServerCrit ? EFPSRHitMarkerType::Crit : EFPSRHitMarkerType::Hit);
			OwnerPC->ClientNotifyHitMarker(MarkerType);
		}
	}

	// Post-fire hooks (after all pellets resolved).
	if (Fragments)
	{
		for (const TObjectPtr<UFPSRWeaponFragment>& Frag : *Fragments)
		{
			if (Frag) { Frag->PostFire(FireCtx); }
		}
	}

	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}
