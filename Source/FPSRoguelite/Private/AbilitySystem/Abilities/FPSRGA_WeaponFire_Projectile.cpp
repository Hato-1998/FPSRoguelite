// Copyright Epic Games, Inc. All Rights Reserved.

#include "AbilitySystem/Abilities/FPSRGA_WeaponFire_Projectile.h"
#include "AbilitySystem/Attributes/FPSRCombatSet.h"
#include "Weapon/FPSRWeaponInventoryComponent.h"
#include "Weapon/FPSRWeaponInstance.h"
#include "Weapon/FPSRWeaponFragment.h"
#include "Weapon/FPSRWeaponDataAsset.h"
#include "Weapon/FPSRProjectile.h"
#include "Weapon/FPSRProjectileSubsystem.h"
#include "Weapon/FPSRProjectileTypes.h"
#include "Core/FPSRGameState.h"
#include "Core/FPSRPlayerState.h"
#include "Core/FPSRLogChannels.h"

#include "AbilitySystemComponent.h"
#include "GameFramework/Pawn.h"
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
	float ProjectileSpeed = 3000.0f;
	float GravityScale = 0.0f;
	float AOERadius = 0.0f;
	float Lifetime = 5.0f;
	float SpreadDegrees = 0.0f;
	int32 ProjectilePierce = 0;
	float KnockbackStrength = 0.0f;
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

	// Spawn from the player view point.
	FVector ViewLocation;
	FRotator ViewRotation;
	Controller->GetPlayerViewPoint(ViewLocation, ViewRotation);
	const FVector Start = ViewLocation;
	const FVector BaseDir = ViewRotation.Vector();

	// Server-authoritative spawn: AcquireProjectile returns null on clients (cosmetic prediction is a follow-up).
	if (FireCtx.bAuthority)
	{
		// Global damage multiplier + crit (chance/multiplier) baked at spawn; the projectile rolls crit per impact
		// server-authoritatively and notifies the owner's hit-marker. (OnHitActor fragments on projectiles remain
		// a follow-up needing a projectile->fragment callback.)
		float DamageMultiplier = 1.0f;
		float CritChance = 0.0f;
		float CritMultiplier = 1.0f;
		if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
		{
			DamageMultiplier = ASC->GetNumericAttribute(UFPSRCombatSet::GetGlobalDamageMultiplierAttribute());
			CritChance = ASC->GetNumericAttribute(UFPSRCombatSet::GetGlobalCritChanceAttribute());
			CritMultiplier = ASC->GetNumericAttribute(UFPSRCombatSet::GetGlobalCritMultiplierAttribute());
		}

		UFPSRProjectileSubsystem* ProjSub = World->GetSubsystem<UFPSRProjectileSubsystem>();
		UFPSRWeaponDataAsset* Weapon = Instance ? Instance->GetSource() : nullptr;
		TSubclassOf<AFPSRProjectile> ProjClass = Weapon ? Weapon->ProjectileClass : nullptr;
		if (ProjSub)
		{
			const float MuzzleOffset = 100.0f;
			for (int32 Round = 0; Round < NumRounds; ++Round)
			{
				const FVector Dir = (SpreadDegrees > 0.0f)
					? FMath::VRandCone(BaseDir, FMath::DegreesToRadians(SpreadDegrees))
					: BaseDir;

				FFPSRProjectileParams Params;
				Params.InitialSpeed = ProjectileSpeed;
				Params.GravityScale = GravityScale;
				Params.Damage = Damage * DamageMultiplier;
				Params.CritChance = CritChance;
				Params.CritMultiplier = CritMultiplier;
				Params.Lifetime = Lifetime;
				Params.ExplosionRadius = AOERadius;
				Params.Pierce = ProjectilePierce;
				Params.KnockbackStrength = KnockbackStrength;
				// Self-damage on unless the NoSelfDamage card suppressed it (PreFire). Knockback is independent —
				// a self-no-damage rocket still launches the instigator (rocket jump).
				Params.bSelfDamage = !FireCtx.bSuppressSelfDamage;
				Params.Team = EFPSRProjectileTeam::Player;
				Params.InstigatorActor = Avatar;

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
