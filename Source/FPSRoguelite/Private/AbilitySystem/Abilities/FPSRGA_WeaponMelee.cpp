// Copyright Epic Games, Inc. All Rights Reserved.

#include "AbilitySystem/Abilities/FPSRGA_WeaponMelee.h"
#include "AbilitySystem/Attributes/FPSRCombatSet.h"
#include "Weapon/FPSRWeaponInventoryComponent.h"
#include "Weapon/FPSRWeaponDataAsset.h"
#include "Enemy/FPSREnemyHealthComponent.h"
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

	float Damage = 15.0f;
	float MeleeRadius = 175.0f;
	if (UFPSRWeaponInventoryComponent* Inventory = Avatar->FindComponentByClass<UFPSRWeaponInventoryComponent>())
	{
		if (UFPSRWeaponDataAsset* Weapon = Inventory->GetCurrentWeapon())
		{
			Damage = Weapon->BaseStats.Damage;
			MeleeRadius = Weapon->BaseStats.MeleeRadius;
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

		TArray<FOverlapResult> Overlaps;
		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(FPSRWeaponMelee), false, Avatar);
		const bool bAny = World->OverlapMultiByChannel(
			Overlaps, Center, FQuat::Identity, ECC_Pawn,
			FCollisionShape::MakeSphere(MeleeRadius), QueryParams);

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
				}
			}
		}
	}

	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}
