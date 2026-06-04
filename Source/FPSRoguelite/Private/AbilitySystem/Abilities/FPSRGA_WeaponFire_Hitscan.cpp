// Copyright Epic Games, Inc. All Rights Reserved.

#include "AbilitySystem/Abilities/FPSRGA_WeaponFire_Hitscan.h"
#include "AbilitySystem/Attributes/FPSRCombatSet.h"
#include "Weapon/FPSRWeaponInventoryComponent.h"
#include "Weapon/FPSRWeaponDataAsset.h"
#include "Weapon/FPSRWeaponFireComponent.h"
#include "Enemy/FPSREnemyHealthComponent.h"
#include "Core/FPSRGameState.h"
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

	// Resolve weapon stats from the equipped weapon (fallback to defaults).
	float Damage = 10.0f;
	float Range = 10000.0f;
	float SpreadDegrees = 1.0f;
	UFPSRWeaponInventoryComponent* Inventory = Avatar->FindComponentByClass<UFPSRWeaponInventoryComponent>();
	UFPSRWeaponDataAsset* Weapon = Inventory ? Inventory->GetCurrentWeapon() : nullptr;
	if (Weapon)
	{
		Damage = Weapon->BaseStats.Damage;
		Range = Weapon->BaseStats.Range;
		SpreadDegrees = Weapon->BaseStats.SpreadDegrees;
	}

	// Server-authoritative gates: empty mag / reloading / fire-rate. Then consume ammo.
	if (Avatar->HasAuthority() && Inventory)
	{
		if (Inventory->IsReloading() || Inventory->GetCurrentAmmo() <= 0)
		{
			EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
			return;
		}
		if (Weapon)
		{
			const float MinInterval = 1.0f / FMath::Max(Weapon->BaseStats.FireRate, 0.01f);
			if (!Inventory->ServerTryConsumeFireInterval(MinInterval))
			{
				EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
				return;
			}
		}
		Inventory->ConsumeAmmo(1);
	}

	// Add bloom from sustained fire, then tighten spread while aiming down sights.
	if (UFPSRWeaponFireComponent* FireComp = Avatar->FindComponentByClass<UFPSRWeaponFireComponent>())
	{
		SpreadDegrees += FireComp->GetCurrentBloom();
		if (FireComp->IsAiming() && Weapon && Weapon->BaseStats.bHasADS)
		{
			SpreadDegrees *= Weapon->BaseStats.ADSSpreadMultiplier;
		}
	}

	// Trace from the player view point, randomized within the spread cone.
	FVector ViewLocation;
	FRotator ViewRotation;
	Controller->GetPlayerViewPoint(ViewLocation, ViewRotation);
	const FVector Start = ViewLocation;
	const FVector BaseDir = ViewRotation.Vector();
	const FVector ShotDir = (SpreadDegrees > 0.0f)
		? FMath::VRandCone(BaseDir, FMath::DegreesToRadians(SpreadDegrees))
		: BaseDir;
	const FVector End = Start + ShotDir * Range;

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

	// Server-authoritative damage application.
	if (bHit && Avatar->HasAuthority())
	{
		float FinalDamage = Damage;
		if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
		{
			const float CritChance = ASC->GetNumericAttribute(UFPSRCombatSet::GetGlobalCritChanceAttribute());
			const float CritMultiplier = ASC->GetNumericAttribute(UFPSRCombatSet::GetGlobalCritMultiplierAttribute());
			const float DamageMultiplier = ASC->GetNumericAttribute(UFPSRCombatSet::GetGlobalDamageMultiplierAttribute());

			FinalDamage *= DamageMultiplier;
			if (FMath::FRand() < CritChance)
			{
				FinalDamage *= CritMultiplier;
			}
		}

		if (AActor* HitActor = Hit.GetActor())
		{
			if (UFPSREnemyHealthComponent* HealthComp = HitActor->FindComponentByClass<UFPSREnemyHealthComponent>())
			{
				HealthComp->ApplyDamage(FinalDamage, Avatar);
			}
		}

		UE_LOG(LogFPSR, Verbose, TEXT("[Fire] hit=%s dmg=%.1f"), *GetNameSafe(Hit.GetActor()), FinalDamage);
	}

	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}
