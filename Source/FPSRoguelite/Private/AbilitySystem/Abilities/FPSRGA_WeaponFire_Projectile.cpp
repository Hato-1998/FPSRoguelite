// Copyright Epic Games, Inc. All Rights Reserved.

#include "AbilitySystem/Abilities/FPSRGA_WeaponFire_Projectile.h"
#include "AbilitySystem/Attributes/FPSRCombatSet.h"
#include "Weapon/FPSRWeaponInventoryComponent.h"
#include "Weapon/FPSRWeaponInstance.h"
#include "Weapon/FPSRWeaponFireComponent.h"
#include "Weapon/FPSRRecoilComponent.h"
#include "Weapon/FPSRWeaponFragment.h"
#include "Weapon/FPSRWeaponDataAsset.h"
#include "Weapon/FPSRProjectile.h"
#include "Weapon/FPSRProjectileSubsystem.h"
#include "Weapon/FPSRProjectileTypes.h"
#include "Core/FPSRGameState.h"
#include "Core/FPSRPlayerState.h"
#include "Core/FPSRLogChannels.h"
#include "Hero/FPSRCharacterMovementComponent.h"

#include "AbilitySystemComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Character.h"
#include "GameFramework/Controller.h"
#include "Engine/World.h"

UFPSRGA_WeaponFire_Projectile::UFPSRGA_WeaponFire_Projectile()
{
}

void UFPSRGA_WeaponFire_Projectile::ActivateAbility(
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

	// No firing while not alive (DBNO downed or Dead) — server-authoritative gate mirroring the input block (U9).
	if (const AFPSRPlayerState* OwnerPS = Avatar ? Avatar->GetPlayerState<AFPSRPlayerState>() : nullptr)
	{
		if (!OwnerPS->IsAlive())
		{
			EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
			return;
		}
	}

	// No firing while the current locomotion state forbids it (wall-hang: both hands on the wall). Deliberately
	// OUTSIDE any HasAuthority() branch — this ability is LocalPredicted, so ActivateAbility runs on the predicting
	// owning client AND the server, and IsFirePermittedByMovementState() reads CanFireInCurrentState() the same way on
	// both. This is the REAL authority for the wall-hang fire block; UFPSRWeaponFireComponent's client-side check is
	// cosmetic-only prediction that stops the local recoil/montage loop from running ahead of a rejection here.
	if (!IsFirePermittedByMovementState(Avatar))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// Resolve weapon stats from the equipped weapon instance (base stats × accumulated modifiers; fallback to defaults).
	float Damage = 10.0f;
	float ProjectileSpeed = 3000.0f;
	float GravityScale = 0.0f;
	float AOERadius = 0.0f;
	float Lifetime = 5.0f;
	float SpreadDegrees = 0.0f;
	int32 ProjectilePierce = 0;
	float KnockbackStrength = 0.0f;
	float MuzzleOffset = 100.0f; // cm ahead of the view point; data-driven per weapon (default = prior hardcoded value)
	int32 PelletCount = 1;
	UFPSRWeaponInventoryComponent* Inventory = Avatar->FindComponentByClass<UFPSRWeaponInventoryComponent>();
	UFPSRWeaponInstance* Instance = Inventory ? Inventory->GetCurrentInstance() : nullptr;
	const FFPSRWeaponStatBlock* Stats = Instance ? &Instance->GetResolvedStats() : nullptr;
	if (Stats)
	{
		Damage = Stats->Damage;
		ProjectileSpeed = Stats->ProjectileSpeed;
		GravityScale = Stats->ProjectileGravityScale;
		AOERadius = Stats->AOERadius;
		Lifetime = Stats->ProjectileLifetime;
		ProjectilePierce = FMath::Max(0, Stats->ProjectilePierce);
		SpreadDegrees = Stats->SpreadDegrees;
		KnockbackStrength = FMath::Max(0.0f, Stats->KnockbackStrength);
		MuzzleOffset = FMath::Max(0.0f, Stats->ProjectileMuzzleOffset);
		PelletCount = FMath::Clamp(Stats->PelletCount, 1, 32);
	}

	// Grow the dispersion cone with the recoil component's heat-based dynamic spread (+ ADS multiplier), matching the
	// hitscan weapons and the truthful HUD crosshair (single source: HUD + hitscan + projectile all read GetHeatSpread()).
	// Server parity: this authoritative spawn runs on the server, which advances its OWN heat per accepted shot (see the
	// AdvanceHeatForAcceptedShot call after the ammo commit) so a REMOTE client's server-side spread matches its HUD —
	// the heat component is per-machine (non-replicated), server and client each track their own.
	UFPSRRecoilComponent* Recoil = Avatar->FindComponentByClass<UFPSRRecoilComponent>();
	if (UFPSRWeaponFireComponent* FireComp = Avatar->FindComponentByClass<UFPSRWeaponFireComponent>())
	{
		const float HeatSpread = Recoil ? Recoil->GetHeatSpread() : 0.0f;
		const bool bAiming = FireComp->IsAiming();
		// Movement-state spread multiplier (airborne/slide/crouch) — the authoritative spawn cone below must widen the
		// same way the HUD crosshair already promised the player. Fail-open to 1.0 for a non-Character avatar.
		float StateSpreadMultiplier = 1.0f;
		if (const ACharacter* AvatarCharacter = Cast<ACharacter>(Avatar))
		{
			if (const UFPSRCharacterMovementComponent* Move = Cast<UFPSRCharacterMovementComponent>(AvatarCharacter->GetCharacterMovement()))
			{
				StateSpreadMultiplier = Move->GetSpreadMultiplier();
			}
		}
		SpreadDegrees = Stats
			? UFPSRWeaponFireComponent::ComputeSpreadDegrees(*Stats, HeatSpread, bAiming, StateSpreadMultiplier)
			: SpreadDegrees + HeatSpread;
	}

	// Server-authoritative gates: empty mag / reloading / fire-rate. Ammo is consumed after the fragment hooks
	// determine the round count (a multishot AOE fires multiple projectiles, one magazine round each).
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

	// Behavior fragments (P4-B-2): build the per-activation context and let fragments adjust the round count.
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
	// Each round spawns one projectile and costs one magazine round. Clamp to the rounds actually loaded
	// (CurrentAmmo is replicated, so server and owning client agree), then the server deducts that many.
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

	// Server accepted-shot spread parity (see the hitscan GA for the full rationale): advance server-side heat once per
	// accepted activation, read-then-accumulate, remote pawns only (host/SP accumulate owner-locally in FireOneShot).
	if (FireCtx.bAuthority && Recoil && !Avatar->IsLocallyControlled())
	{
		Recoil->AdvanceHeatForAcceptedShot();
	}

	// Spawn from the player view point.
	FVector ViewLocation;
	FRotator ViewRotation;
	Controller->GetPlayerViewPoint(ViewLocation, ViewRotation);
	const FVector Start = ViewLocation;
	const FVector BaseDir = ViewRotation.Vector();

	// Server-authoritative spawn: AcquireProjectile returns null on clients (cosmetic prediction is a follow-up).
	if (FireCtx.bAuthority)
	{
		// Global damage multiplier + crit context baked at spawn; the projectile rolls crit per impact
		// server-authoritatively and notifies the owner's hit-marker. Per-hit OnHitActor fragments already fire for
		// direct-hit/piercing projectiles (see AFPSRProjectile::TryDamageActor); only the AOE splash path
		// (FPSRCombatStatics::ApplyExplosion) does not yet run per-target OnHitActor hooks.
		// CRIT1: BuildCritContext is the ONLY place a crit context gets baked — reading GlobalCritChance/Multiplier
		// straight off the ASC here (as this used to) would silently skip every fragment's ModifyCrit AND this
		// weapon's active timed crit buffs, so the CRIT1 card set would never work on a Projectile-archetype weapon
		// (this GA is the Rifle's actual fire ability — confirmed via DA_Weapon_Rifle's FireAbility reference).
		float DamageMultiplier = 1.0f;
		FFPSRCritContext Crit;
		if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
		{
			DamageMultiplier = ASC->GetNumericAttribute(UFPSRCombatSet::GetGlobalDamageMultiplierAttribute());
			Crit = FPSRWeaponHooks::BuildCritContext(FireCtx, ASC);
		}

		UFPSRProjectileSubsystem* ProjSub = World->GetSubsystem<UFPSRProjectileSubsystem>();
		UFPSRWeaponDataAsset* Weapon = Instance ? Instance->GetSource() : nullptr;
		TSubclassOf<AFPSRProjectile> ProjClass = Weapon ? Weapon->ProjectileClass : nullptr;
		if (ProjSub)
		{
			const int32 TotalProjectiles = NumRounds * PelletCount;
			for (int32 Round = 0; Round < NumRounds; ++Round)
			{
				for (int32 Pellet = 0; Pellet < PelletCount; ++Pellet)
				{
					const FVector Dir = (SpreadDegrees > 0.0f)
						? FMath::VRandCone(BaseDir, FMath::DegreesToRadians(SpreadDegrees))
						: BaseDir;

					FFPSRProjectileParams Params;
					Params.InitialSpeed = ProjectileSpeed;
					Params.GravityScale = GravityScale;
					Params.Damage = Damage * DamageMultiplier;
					Params.Crit = Crit;
					Params.Lifetime = Lifetime;
					Params.ExplosionRadius = AOERadius;
					Params.Pierce = ProjectilePierce;
					Params.KnockbackStrength = KnockbackStrength;
					// Self-damage on unless the NoSelfDamage card suppressed it (PreFire). Knockback is independent —
					// a self-no-damage rocket still launches the instigator (rocket jump).
					Params.bSelfDamage = !FireCtx.bSuppressSelfDamage;
					Params.Team = EFPSRProjectileTeam::Player;
					Params.InstigatorActor = Avatar;
					// Server-only weak back-ref so the projectile can fire the OnKill behavior hook at damage time (U18c).
					Params.WeaponInstance = Instance;
					// VIT1 §5-9 ④: baked at LAUNCH time from the SAME ResolvedStats already read above (Stats is
					// re-derived per-Round/Pellet loop iteration below only via this outer Stats pointer, which is
					// stable for the whole activation) — never re-read at impact, for the identical reason
					// Pierce/GravityScale already travel this way: the weapon may be swapped or the stat re-resolved
					// (a card eaten) before this projectile lands, seconds later.
					Params.DamageSpec.ShieldDamageMultiplier = Stats ? Stats->ShieldDamageMultiplier : 1.0f;
					// Per-activation OnMiss parity: only a single-projectile activation may fire the projectile miss hook
					// (a multishot volley releases asynchronously and would otherwise refund per-projectile / on partial hits).
					Params.bSingleProjectileActivation = (TotalProjectiles == 1);

					// Behavior fragments may adjust projectile params per spawn (speed / radius / lifetime / etc.).
					if (Fragments)
					{
						for (const TObjectPtr<UFPSRWeaponFragment>& Frag : *Fragments)
						{
							if (Frag) { Frag->OnProjectileSpawn(FireCtx, Params); }
						}
					}

					// Clamp the muzzle-offset spawn to the near side of any wall between the camera and the muzzle:
					// without this, firing point-blank into thin cover spawns the projectile past the wall and lets
					// it damage enemies through it. Trace the weapon (Visibility) channel from the viewpoint.
					FVector SpawnLocation = Start + Dir * MuzzleOffset;
					FHitResult MuzzleHit;
					FCollisionQueryParams MuzzleParams(SCENE_QUERY_STAT(FPSRProjectileMuzzle), false, Avatar);
					if (World->LineTraceSingleByChannel(MuzzleHit, Start, SpawnLocation, ECC_Visibility, MuzzleParams))
					{
						SpawnLocation = MuzzleHit.Location; // spawn flush against the wall, on the near side
					}

					ProjSub->AcquireProjectile(ProjClass, SpawnLocation, Dir, Params);
				}
			}
		}
	}

	// Post-fire hooks (after all rounds resolved).
	if (Fragments)
	{
		for (const TObjectPtr<UFPSRWeaponFragment>& Frag : *Fragments)
		{
			if (Frag) { Frag->PostFire(FireCtx); }
		}
	}

	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}
