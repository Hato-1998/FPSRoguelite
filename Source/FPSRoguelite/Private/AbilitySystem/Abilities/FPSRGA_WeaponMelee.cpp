// Copyright Epic Games, Inc. All Rights Reserved.

#include "AbilitySystem/Abilities/FPSRGA_WeaponMelee.h"
#include "AbilitySystem/Attributes/FPSRCombatSet.h"
#include "Weapon/FPSRWeaponInventoryComponent.h"
#include "Weapon/FPSRWeaponInstance.h"
#include "Weapon/FPSRWeaponDataAsset.h"
#include "Enemy/FPSREnemyHealthComponent.h"
#include "Core/FPSRGameState.h"
#include "Core/FPSRPlayerController.h"
#include "Core/FPSRLogChannels.h"

#include "AbilitySystemComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"
#include "Engine/World.h"
#include "Engine/EngineTypes.h"
#include "Engine/OverlapResult.h"
#include "DrawDebugHelpers.h"

UFPSRGA_WeaponMelee::UFPSRGA_WeaponMelee()
{
}

void UFPSRGA_WeaponMelee::ActivateAbility(
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

	// No attacking while the run is frozen for card selection (Game.MD §2-2) — mirrors the hitscan / projectile /
	// charge-laser fire abilities. Without this, holding the melee button into a freeze keeps the FireComponent
	// tick re-activating this ability and applying damage while the run is supposed to be globally stopped.
	if (const AFPSRGameState* RunState = World->GetGameState<AFPSRGameState>())
	{
		if (RunState->IsRunPaused())
		{
			EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
			return;
		}
	}

	float Damage = 15.0f;
	float MeleeRadius = 175.0f;
	float MeleeAttackDelay = 0.0f;
	UFPSRWeaponInventoryComponent* Inventory = Avatar->FindComponentByClass<UFPSRWeaponInventoryComponent>();
	if (Inventory)
	{
		if (UFPSRWeaponInstance* Instance = Inventory->GetCurrentInstance())
		{
			const FFPSRWeaponStatBlock& Stats = Instance->GetResolvedStats();
			Damage = Stats.Damage;
			MeleeRadius = Stats.MeleeRadius;
			MeleeAttackDelay = Stats.MeleeAttackDelay;
		}
	}

	FVector ViewLocation;
	FRotator ViewRotation;
	Controller->GetPlayerViewPoint(ViewLocation, ViewRotation);
	const FVector Center = ViewLocation + ViewRotation.Vector() * (MeleeRadius * 0.5f);

#if ENABLE_DRAW_DEBUG
	DrawDebugSphere(World, Center, MeleeRadius, 12, FColor::Cyan, false, 0.5f, 0, 1.0f);
#endif

	if (Avatar->HasAuthority())
	{
		// Server-authoritative melee cooldown gate (mirrors the local FireComponent delay).
		if (Inventory && MeleeAttackDelay > 0.0f && !Inventory->ServerTryConsumeFireInterval(MeleeAttackDelay))
		{
			EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
			return;
		}

		float FinalDamage = Damage;
		bool bSwingCrit = false;
		if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
		{
			const float CritChance = ASC->GetNumericAttribute(UFPSRCombatSet::GetGlobalCritChanceAttribute());
			const float CritMultiplier = ASC->GetNumericAttribute(UFPSRCombatSet::GetGlobalCritMultiplierAttribute());
			const float DamageMultiplier = ASC->GetNumericAttribute(UFPSRCombatSet::GetGlobalDamageMultiplierAttribute());

			FinalDamage *= DamageMultiplier;
			if (FMath::FRand() < CritChance)
			{
				FinalDamage *= CritMultiplier;
				bSwingCrit = true;
			}
		}

		TArray<FOverlapResult> Overlaps;
		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(FPSRWeaponMelee), false, Avatar);
		const bool bAny = World->OverlapMultiByChannel(
			Overlaps, Center, FQuat::Identity, ECC_Pawn,
			FCollisionShape::MakeSphere(MeleeRadius), QueryParams);

		bool bAnyHit = false;
		bool bAnyKill = false;
		if (bAny)
		{
			for (const FOverlapResult& Overlap : Overlaps)
			{
				AActor* HitActor = Overlap.GetActor();
				if (!HitActor || HitActor == Avatar)
				{
					continue;
				}
				if (UFPSREnemyHealthComponent* HealthComp = HitActor->FindComponentByClass<UFPSREnemyHealthComponent>())
				{
					HealthComp->ApplyDamage(FinalDamage, Avatar);
					bAnyHit = true;
					if (HealthComp->IsDead()) { bAnyKill = true; }
				}
			}
		}

		// Melee has no client prediction (server-only overlap), so all markers come from the server here.
		// One pulse per swing, strongest outcome (Kill > Crit > Hit). (Game.MD §2-14)
		if (bAnyHit)
		{
			if (AFPSRPlayerController* OwnerPC = Cast<AFPSRPlayerController>(Controller))
			{
				const EFPSRHitMarkerType MarkerType = bAnyKill ? EFPSRHitMarkerType::Kill
					: (bSwingCrit ? EFPSRHitMarkerType::Crit : EFPSRHitMarkerType::Hit);
				OwnerPC->ClientNotifyHitMarker(MarkerType);
			}
		}
	}

	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}
