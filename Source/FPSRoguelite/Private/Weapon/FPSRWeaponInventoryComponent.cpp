// Copyright Epic Games, Inc. All Rights Reserved.

#include "Weapon/FPSRWeaponInventoryComponent.h"
#include "Weapon/FPSRWeaponDataAsset.h"
#include "Core/FPSRLogChannels.h"

#include "AbilitySystemComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "AbilitySystemGlobals.h"
#include "Abilities/GameplayAbility.h"
#include "GameplayAbilitySpec.h"
#include "Net/UnrealNetwork.h"
#include "Net/Core/PushModel/PushModel.h"

UFPSRWeaponInventoryComponent::UFPSRWeaponInventoryComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
	WeaponSlots.SetNum(MaxSlots);
	SlotAmmo.SetNum(MaxSlots);
}

void UFPSRWeaponInventoryComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	FDoRepLifetimeParams Params;
	Params.bIsPushBased = true;
	DOREPLIFETIME_WITH_PARAMS_FAST(UFPSRWeaponInventoryComponent, WeaponSlots, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(UFPSRWeaponInventoryComponent, CurrentSlotIndex, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(UFPSRWeaponInventoryComponent, SlotAmmo, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(UFPSRWeaponInventoryComponent, bReloading, Params);
}

UAbilitySystemComponent* UFPSRWeaponInventoryComponent::GetOwnerASC() const
{
	return UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(GetOwner());
}

int32 UFPSRWeaponInventoryComponent::AddWeapon(UFPSRWeaponDataAsset* WeaponData)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !WeaponData)
	{
		return INDEX_NONE;
	}

	const int32 FreeSlot = WeaponSlots.IndexOfByPredicate(
		[](const TObjectPtr<UFPSRWeaponDataAsset>& W) { return W == nullptr; });
	if (FreeSlot == INDEX_NONE)
	{
		return INDEX_NONE;
	}

	WeaponSlots[FreeSlot] = WeaponData;
	MARK_PROPERTY_DIRTY_FROM_NAME(UFPSRWeaponInventoryComponent, WeaponSlots, this);

	SlotAmmo[FreeSlot] = WeaponData->BaseStats.MagSize;
	MARK_PROPERTY_DIRTY_FROM_NAME(UFPSRWeaponInventoryComponent, SlotAmmo, this);

	if (CurrentSlotIndex == INDEX_NONE)
	{
		EquipSlot(FreeSlot);
	}
	return FreeSlot;
}

void UFPSRWeaponInventoryComponent::EquipSlot(int32 SlotIndex)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	if (!WeaponSlots.IsValidIndex(SlotIndex) || WeaponSlots[SlotIndex] == nullptr)
	{
		return;
	}

	// Switching weapons cancels any in-progress reload, remembering the slot so it resumes on re-equip.
	if (bReloading)
	{
		GetWorld()->GetTimerManager().ClearTimer(ReloadTimerHandle);
		bReloading = false;
		MARK_PROPERTY_DIRTY_FROM_NAME(UFPSRWeaponInventoryComponent, bReloading, this);
		PendingReloadSlot = CurrentSlotIndex;
	}

	CurrentSlotIndex = SlotIndex;
	MARK_PROPERTY_DIRTY_FROM_NAME(UFPSRWeaponInventoryComponent, CurrentSlotIndex, this);
	RefreshEquippedAbility();

	// Fresh weapon: clear the previous slot's fire-rate gate so the first shot isn't blocked by it.
	ServerNextAllowedFireTime = 0.0f;

	// Re-equipping a weapon whose reload was cancelled mid-switch resumes the reload ONLY if its
	// magazine is empty. If ammo remains, the reload stays cancelled (player keeps the partial mag).
	if (PendingReloadSlot == CurrentSlotIndex)
	{
		PendingReloadSlot = INDEX_NONE;
		if (SlotAmmo.IsValidIndex(CurrentSlotIndex) && SlotAmmo[CurrentSlotIndex] <= 0)
		{
			StartReload();
		}
	}

	UE_LOG(LogFPSR, Verbose, TEXT("[Weapon] Equipped slot %d"), SlotIndex);
}

void UFPSRWeaponInventoryComponent::RefreshEquippedAbility()
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	UAbilitySystemComponent* ASC = GetOwnerASC();
	if (!ASC)
	{
		return;
	}

	if (GrantedFireAbilityHandle.IsValid())
	{
		ASC->ClearAbility(GrantedFireAbilityHandle);
		GrantedFireAbilityHandle = FGameplayAbilitySpecHandle();
	}

	UFPSRWeaponDataAsset* Weapon = GetCurrentWeapon();
	if (Weapon && Weapon->FireAbility)
	{
		GrantedFireAbilityHandle = ASC->GiveAbility(FGameplayAbilitySpec(Weapon->FireAbility, 1, INDEX_NONE, this));
	}
}

UFPSRWeaponDataAsset* UFPSRWeaponInventoryComponent::GetCurrentWeapon() const
{
	return WeaponSlots.IsValidIndex(CurrentSlotIndex) ? WeaponSlots[CurrentSlotIndex].Get() : nullptr;
}

void UFPSRWeaponInventoryComponent::OnRep_CurrentSlotIndex()
{
	// Cosmetic hook for clients (weapon visual swap added later).
}

int32 UFPSRWeaponInventoryComponent::GetCurrentAmmo() const
{
	return SlotAmmo.IsValidIndex(CurrentSlotIndex) ? SlotAmmo[CurrentSlotIndex] : 0;
}

int32 UFPSRWeaponInventoryComponent::GetCurrentMagSize() const
{
	const UFPSRWeaponDataAsset* Weapon = GetCurrentWeapon();
	return Weapon ? Weapon->BaseStats.MagSize : 0;
}

bool UFPSRWeaponInventoryComponent::ConsumeAmmo(int32 Amount)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return false;
	}
	if (!SlotAmmo.IsValidIndex(CurrentSlotIndex) || SlotAmmo[CurrentSlotIndex] < Amount)
	{
		return false;
	}
	SlotAmmo[CurrentSlotIndex] -= Amount;
	MARK_PROPERTY_DIRTY_FROM_NAME(UFPSRWeaponInventoryComponent, SlotAmmo, this);
	return true;
}

void UFPSRWeaponInventoryComponent::StartReload()
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || bReloading)
	{
		return;
	}
	const UFPSRWeaponDataAsset* Weapon = GetCurrentWeapon();
	if (!Weapon || !SlotAmmo.IsValidIndex(CurrentSlotIndex))
	{
		return;
	}
	if (SlotAmmo[CurrentSlotIndex] >= Weapon->BaseStats.MagSize)
	{
		return; // already full
	}

	bReloading = true;
	MARK_PROPERTY_DIRTY_FROM_NAME(UFPSRWeaponInventoryComponent, bReloading, this);
	GetWorld()->GetTimerManager().SetTimer(
		ReloadTimerHandle, this, &UFPSRWeaponInventoryComponent::FinishReload,
		FMath::Max(0.01f, Weapon->BaseStats.ReloadTime), false);
}

void UFPSRWeaponInventoryComponent::FinishReload()
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	const UFPSRWeaponDataAsset* Weapon = GetCurrentWeapon();
	if (Weapon && SlotAmmo.IsValidIndex(CurrentSlotIndex))
	{
		SlotAmmo[CurrentSlotIndex] = Weapon->BaseStats.MagSize; // infinite reserve: always full
		MARK_PROPERTY_DIRTY_FROM_NAME(UFPSRWeaponInventoryComponent, SlotAmmo, this);
	}
	bReloading = false;
	MARK_PROPERTY_DIRTY_FROM_NAME(UFPSRWeaponInventoryComponent, bReloading, this);
}

bool UFPSRWeaponInventoryComponent::ServerTryConsumeFireInterval(float MinInterval)
{
	// Client prediction path: never block locally; the server is authoritative.
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return true;
	}

	const UWorld* World = GetWorld();
	if (!World)
	{
		return true;
	}

	const float Now = World->GetTimeSeconds();
	// Jitter tolerance: allow a shot slightly early so network timing variance never blocks legitimate fire.
	// This gate rejects grossly-too-fast activation (anti-abuse), not exact cadence.
	const float Tolerance = MinInterval * 0.25f;
	if (Now + Tolerance < ServerNextAllowedFireTime)
	{
		return false;
	}

	ServerNextAllowedFireTime = Now + MinInterval;
	return true;
}
