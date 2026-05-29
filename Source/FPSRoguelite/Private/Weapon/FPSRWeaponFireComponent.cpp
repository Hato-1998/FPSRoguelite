// Copyright Epic Games, Inc. All Rights Reserved.

#include "Weapon/FPSRWeaponFireComponent.h"
#include "Weapon/FPSRWeaponInventoryComponent.h"
#include "Weapon/FPSRWeaponDataAsset.h"
#include "Weapon/FPSRWeaponTypes.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "GameFramework/Pawn.h"

UFPSRWeaponFireComponent::UFPSRWeaponFireComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

UFPSRWeaponInventoryComponent* UFPSRWeaponFireComponent::GetInventory() const
{
	return GetOwner() ? GetOwner()->FindComponentByClass<UFPSRWeaponInventoryComponent>() : nullptr;
}

void UFPSRWeaponFireComponent::StartFiring()
{
	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (!OwnerPawn || !OwnerPawn->IsLocallyControlled())
	{
		return;
	}

	UFPSRWeaponInventoryComponent* Inventory = GetInventory();
	UFPSRWeaponDataAsset* Weapon = Inventory ? Inventory->GetCurrentWeapon() : nullptr;
	if (!Weapon)
	{
		return;
	}

	bWantsToFire = true;
	TimeSinceLastShot = 0.0f;

	if (Weapon->BaseStats.FireMode == EFPSRFireMode::Burst)
	{
		BurstShotsRemaining = FMath::Max(1, Weapon->BaseStats.BurstCount);
	}

	// Immediate first shot on press.
	FireOneShot();
	if (Weapon->BaseStats.FireMode == EFPSRFireMode::Burst && BurstShotsRemaining > 0)
	{
		--BurstShotsRemaining;
	}
}

void UFPSRWeaponFireComponent::StopFiring()
{
	bWantsToFire = false;
}

void UFPSRWeaponFireComponent::FireOneShot()
{
	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (!OwnerPawn)
	{
		return;
	}

	UFPSRWeaponInventoryComponent* Inventory = GetInventory();
	UFPSRWeaponDataAsset* Weapon = Inventory ? Inventory->GetCurrentWeapon() : nullptr;
	if (!Weapon)
	{
		return;
	}

	const FFPSRWeaponStatBlock& Stats = Weapon->BaseStats;

	// Activate the weapon's fire ability (trace + damage; predicted + server-authoritative).
	if (Weapon->FireAbility)
	{
		if (UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(OwnerPawn))
		{
			ASC->TryActivateAbilityByClass(Weapon->FireAbility);
		}
	}

	// Camera recoil (local feel only).
	if (Stats.RecoilVertical != 0.0f)
	{
		OwnerPawn->AddControllerPitchInput(-Stats.RecoilVertical);
		AccumulatedRecoilPitch += Stats.RecoilVertical;
	}
	if (Stats.RecoilHorizontal != 0.0f)
	{
		OwnerPawn->AddControllerYawInput(FMath::FRandRange(-Stats.RecoilHorizontal, Stats.RecoilHorizontal));
	}

	// Bloom grows with each shot.
	CurrentBloom = FMath::Min(CurrentBloom + Stats.BloomPerShot, Stats.MaxBloom);
}

void UFPSRWeaponFireComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (!OwnerPawn || !OwnerPawn->IsLocallyControlled())
	{
		return;
	}

	UFPSRWeaponInventoryComponent* Inventory = GetInventory();
	UFPSRWeaponDataAsset* Weapon = Inventory ? Inventory->GetCurrentWeapon() : nullptr;
	if (!Weapon)
	{
		return;
	}

	const FFPSRWeaponStatBlock& Stats = Weapon->BaseStats;
	const float Interval = 1.0f / FMath::Max(Stats.FireRate, 0.01f);

	const bool bAutoFiring = (bWantsToFire && Stats.FireMode == EFPSRFireMode::FullAuto);
	const bool bBurstFiring = (Stats.FireMode == EFPSRFireMode::Burst && BurstShotsRemaining > 0);

	if (bAutoFiring || bBurstFiring)
	{
		TimeSinceLastShot += DeltaTime;
		int32 Safety = 0;
		while (TimeSinceLastShot >= Interval && Safety < 16)
		{
			if (Stats.FireMode == EFPSRFireMode::Burst && BurstShotsRemaining <= 0)
			{
				break;
			}
			FireOneShot();
			if (Stats.FireMode == EFPSRFireMode::Burst)
			{
				BurstShotsRemaining = FMath::Max(0, BurstShotsRemaining - 1);
			}
			TimeSinceLastShot -= Interval;
			++Safety;
		}
	}

	// Recoil recovery when not actively firing (returns the view toward pre-spray).
	if (!bWantsToFire && AccumulatedRecoilPitch > 0.0f)
	{
		const float Recover = FMath::Min(Stats.RecoilRecoveryRate * DeltaTime, AccumulatedRecoilPitch);
		OwnerPawn->AddControllerPitchInput(Recover);
		AccumulatedRecoilPitch -= Recover;
	}

	// Bloom recovery.
	if (CurrentBloom > 0.0f)
	{
		CurrentBloom = FMath::Max(0.0f, CurrentBloom - Stats.BloomRecoveryRate * DeltaTime);
	}
}
