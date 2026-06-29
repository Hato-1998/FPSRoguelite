// Copyright Epic Games, Inc. All Rights Reserved.

#include "AbilitySystem/Abilities/FPSRGA_WeaponFire_Hitscan.h"
#include "AbilitySystem/Attributes/FPSRCombatSet.h"
#include "Weapon/FPSRWeaponInventoryComponent.h"
#include "Weapon/FPSRWeaponInstance.h"
#include "Weapon/FPSRWeaponFragment.h"
#include "Weapon/FPSRWeaponDataAsset.h"
#include "Weapon/FPSRWeaponFireComponent.h"
#include "Combat/FPSRCombatStatics.h"
#include "Combat/FPSRWeakpointComponent.h"
#include "Enemy/FPSREnemyHealthComponent.h"
#include "Core/FPSRGameState.h"
#include "Core/FPSRPlayerController.h"
#include "Core/FPSRPlayerState.h"
#include "Core/FPSRLogChannels.h"

#include "AbilitySystemComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"
#include "HAL/IConsoleManager.h"

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

	// No firing once the player is dead (U2 defeat wiring) — server-authoritative gate mirroring the input block.
	if (const AFPSRPlayerState* OwnerPS = Avatar ? Avatar->GetPlayerState<AFPSRPlayerState>() : nullptr)
	{
		if (OwnerPS->IsDead())
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
	// C2: while ADS, the shot is DETERMINISTIC — the bullet follows the crosshair exactly so the (already
	// deterministic) camera recoil pattern IS the learnable spray; the random cone is hip-fire only. Captured here
	// (FireComp scope) and consumed at the trace below. Multi-pellet weapons (shotgun) keep a cone even in ADS
	// (gated at the trace) so a single-line pattern doesn't collapse all pellets onto one ray.
	bool bADSDeterministic = false;
	if (UFPSRWeaponFireComponent* FireComp = Avatar->FindComponentByClass<UFPSRWeaponFireComponent>())
	{
		const float Bloom = FireComp->GetCurrentBloom();
		const bool bAiming = FireComp->IsAiming();
		SpreadDegrees = Stats
			? UFPSRWeaponFireComponent::ComputeSpreadDegrees(*Stats, Bloom, bAiming)
			: SpreadDegrees + Bloom;
		bADSDeterministic = Stats && Stats->bHasADS && bAiming;
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

	// OnFire trigger (server): once per activation, right after the ammo commit (§2-3-5).
	if (FireCtx.bAuthority)
	{
		FPSRWeaponHooks::NotifyFire(FireCtx);
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
	bool bServerWeak = false;
	bool bServerKill = false;
	// Visual hit-marker fires for ANY damageable that lost health — enemies AND destructible non-enemies (doors,
	// bCountsAsKill=false). Distinct from bServerHit (enemy combat-credit: drives OnMiss / Kill·Crit·Weak / lifesteal).
	// A friendly player never sets this: the player damage branch leaves DamageDealt = 0 (FPSRCombatStatics::ApplyDamage).
	bool bServerAnyDamage = false;
	// True if a per-impact fragment (e.g. ExplosiveRounds splash) dealt real damage to an enemy — folded into the
	// miss check so a connecting wall-splash doesn't count as a miss (would otherwise refund AmmoOnMiss on a hit).
	bool bImpactHitEnemy = false;

	// Damageable-pawn object query (enemies via ECC_Pawn + players via ECC_FPSRPlayerPawn), reused per pellet. An
	// ECC_Pawn-only query would miss players (distinct object channel) — so friendly fire would never land. (§2-4)
	FCollisionObjectQueryParams PawnObjParams;
	FPSRCombat::AddDamageablePawnObjectTypes(PawnObjParams);
	FPSRCombat::AddWeakpointObjectType(PawnObjParams);

	// bAllowSelf for any per-impact explosion (ExplosiveRounds card): self unless the NoSelfDamage card cleared it.
	const bool bAllowSelfOnImpact = !FireCtx.bSuppressSelfDamage;

	// Resolve crit + per-hit fragment bonus + self/friendly rules for one target. Returns true if damage landed
	// (bullet stops / spends a penetration here); false = pass-through (a friendly while FF is off).
	auto ApplyToTarget = [&](AActor* HitActor, float WeakpointMult) -> bool
	{
		if (!HitActor || !FireCtx.bAuthority)
		{
			return false;
		}
		float FinalDamage = Damage * DamageMultiplier;
		bool bCrit = false;
		if (CritChance > 0.0f && FMath::FRand() < CritChance)
		{
			FinalDamage *= CritMultiplier;
			bCrit = true;
		}
		if (Fragments)
		{
			for (const TObjectPtr<UFPSRWeaponFragment>& Frag : *Fragments)
			{
				if (Frag) { Frag->OnHitActor(FireCtx, HitActor, FinalDamage); }
			}
		}
		FinalDamage *= WeakpointMult;
		// Direct hitscan never self-damages (bAllowSelf=false); ResolveDamage applies enemy/friendly rules.
		const float Resolved = FPSRCombat::ResolveDamage(Avatar, HitActor, FinalDamage, /*bAllowSelf*/ false, World);
		if (Resolved <= 0.0f)
		{
			return false; // friendly pass-through (FF off): don't stop the bullet, don't spend penetration
		}
		const FPSRCombat::FDamageResult Result = FPSRCombat::ApplyDamage(HitActor, Resolved, Avatar);
		// Markers / kill triggers key on DamageDealt (real health removed), so a corpse re-hit (DamageDealt 0) is inert;
		// the bullet still spends penetration via bApplied below (geometry), it just produces no feedback or kill.
		if (Result.DamageDealt > 0.0f)
		{
			bServerAnyDamage = true; // visual marker for enemies AND destructible doors (not friendly players: DamageDealt 0)
			if (Result.bWasEnemy)
			{
				bServerHit = true;
				if (Result.bKilled) { bServerKill = true; FPSRWeaponHooks::NotifyKill(FireCtx, HitActor); }
				else if (WeakpointMult > 1.0f) { bServerWeak = true; }
				else if (bCrit) { bServerCrit = true; }
			}
		}
		return Result.bApplied;
	};

	// Notify ExplosiveRounds-style fragments of a terminal impact (server-only).
	auto NotifyImpact = [&](const FVector& ImpactPoint)
	{
		if (!FireCtx.bAuthority || !Fragments)
		{
			return;
		}
		for (const TObjectPtr<UFPSRWeaponFragment>& Frag : *Fragments)
		{
			if (Frag)
			{
				bool bFragHitEnemy = false;
				Frag->OnImpact(FireCtx, ImpactPoint, bAllowSelfOnImpact, bFragHitEnemy);
				bImpactHitEnemy |= bFragHitEnemy;
			}
		}
	};

	// Nested loop: outer over rounds (each costs 1 ammo), inner over pellets per round.
	for (int32 Round = 0; Round < NumRounds; ++Round)
	{
		for (int32 Pellet = 0; Pellet < PelletCount; ++Pellet)
		{
			// Hip-fire: random cone (bloom scatter). ADS single-pellet (C2): deterministic — bullet to the crosshair,
			// the camera recoil pattern is the learnable spray (also keeps server/client traces in agreement). Shotgun
			// (PelletCount>1) keeps a cone even in ADS so its pellets still spread.
			const bool bDeterministicShot = bADSDeterministic && PelletCount == 1;
			const FVector PelletDir = (!bDeterministicShot && SpreadDegrees > 0.0f)
				? FMath::VRandCone(BaseDir, FMath::DegreesToRadians(SpreadDegrees))
				: BaseDir;
			const FVector End = Start + PelletDir * Range;

			// Unified path (single-trace = MaxPenetration 1): gather damageable pawns (enemies + players) along the
			// ray, then find the wall (Visibility) cutoff ignoring those pawns so a pawn never masquerades as the
			// wall. Query-only dynamics that ignore Visibility (in-flight projectiles) don't count as walls.
			FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(FPSRWeaponFire), false, Avatar);
			TArray<FHitResult> PawnHits;
			World->LineTraceMultiByObjectType(PawnHits, Start, End, PawnObjParams, QueryParams);

			FCollisionQueryParams WallParams(SCENE_QUERY_STAT(FPSRWeaponFireWall), false, Avatar);
			for (const FHitResult& PawnHit : PawnHits)
			{
				if (AActor* PawnActor = PawnHit.GetActor()) { WallParams.AddIgnoredActor(PawnActor); }
			}
			FHitResult WallHit;
			const bool bWall = World->LineTraceSingleByChannel(WallHit, Start, End, ECC_Visibility, WallParams);
			const float WallDist = bWall ? WallHit.Distance : Range;

#if ENABLE_DRAW_DEBUG
			if (const IConsoleVariable* CVarWeaponDraw = IConsoleManager::Get().FindConsoleVariable(TEXT("FPSR.Debug.WeaponDraw")))
			{
				if (CVarWeaponDraw->GetInt() > 0)
				{
					DrawDebugLine(World, Start, Start + PelletDir * FMath::Min(WallDist, Range), FColor::Yellow, false, 0.5f, 0, 1.0f);
				}
			}
#endif

			// Collapse to one entry per actor (nearest kept, max weakpoint multiplier) so body+weakpoint hits
			// on the same enemy never double-damage or double-spend penetration (U3a).
			TArray<FPSRCombat::FResolvedHit> ResolvedHits;
			FPSRCombat::DedupePawnHitsByActor(PawnHits, ResolvedHits);

			int32 PenetrationCount = 0;
			bool bHasImpact = false;
			FVector ImpactPoint = bWall ? WallHit.ImpactPoint : End;
			for (const FPSRCombat::FResolvedHit& Entry : ResolvedHits)
			{
				if (Entry.Distance > WallDist)
				{
					break; // behind the wall (everything after is too — distance-sorted)
				}
				if (ApplyToTarget(Entry.Actor, Entry.WeakpointMultiplier))
				{
					ImpactPoint = Entry.ImpactPoint; // the bullet lands / detonates here
					bHasImpact = true;
					++PenetrationCount;
					if (PenetrationCount >= MaxPenetration)
					{
						break;
					}
				}
			}

			// Detonate per-impact fragments at the terminal point: last damaged pawn, or the wall if only geometry
			// was hit. A pellet that hit nothing (sky) fires no impact event.
			if (bHasImpact || bWall)
			{
				NotifyImpact(ImpactPoint);
			}
		}
	}

	// Server delivers one marker per activation to the owning client — strongest outcome (Kill > Weak > Crit > Hit).
	// Fires on ANY damage dealt (door-only hit => plain Hit, since Kill/Weak/Crit are enemy-only above).
	if (FireCtx.bAuthority && bServerAnyDamage)
	{
		if (AFPSRPlayerController* OwnerPC = Cast<AFPSRPlayerController>(Controller))
		{
			const EFPSRHitMarkerType MarkerType = bServerKill ? EFPSRHitMarkerType::Kill
				: (bServerWeak ? EFPSRHitMarkerType::Weak
				: (bServerCrit ? EFPSRHitMarkerType::Crit : EFPSRHitMarkerType::Hit));
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

	// OnMiss trigger (server): this activation landed no real damage on any enemy — neither a direct hit (bServerHit)
	// nor a per-impact fragment splash (bImpactHitEnemy, e.g. ExplosiveRounds) connected (§2-3-5).
	if (FireCtx.bAuthority && !bServerHit && !bImpactHitEnemy)
	{
		FPSRWeaponHooks::NotifyMiss(FireCtx);
	}

	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}
