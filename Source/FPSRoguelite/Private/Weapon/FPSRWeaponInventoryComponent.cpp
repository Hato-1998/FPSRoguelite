// Copyright Epic Games, Inc. All Rights Reserved.

#include "Weapon/FPSRWeaponInventoryComponent.h"
#include "Weapon/FPSRWeaponDataAsset.h"
#include "Core/FPSRLogChannels.h"

#include "AbilitySystemComponent.h"
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
}

void UFPSRWeaponInventoryComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	FDoRepLifetimeParams Params;
	Params.bIsPushBased = true;
	DOREPLIFETIME_WITH_PARAMS_FAST(UFPSRWeaponInventoryComponent, WeaponSlots, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(UFPSRWeaponInventoryComponent, CurrentSlotIndex, Params);
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

	CurrentSlotIndex = SlotIndex;
	MARK_PROPERTY_DIRTY_FROM_NAME(UFPSRWeaponInventoryComponent, CurrentSlotIndex, this);
	RefreshEquippedAbility();

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
