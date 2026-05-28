// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"
#include "GameplayAbilitySpecHandle.h"
#include "FPSRWeaponInventoryComponent.generated.h"

class UFPSRWeaponDataAsset;
class UAbilitySystemComponent;

/** Server-authoritative 3-slot weapon inventory. Grants the equipped weapon's fire ability. */
UCLASS(ClassGroup = (FPSR), meta = (BlueprintSpawnableComponent))
class FPSROGUELITE_API UFPSRWeaponInventoryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UFPSRWeaponInventoryComponent();

	static constexpr int32 MaxSlots = 3;

	/** Server: add weapon to the first free slot; auto-equips if nothing is equipped. Returns slot index or INDEX_NONE. */
	int32 AddWeapon(UFPSRWeaponDataAsset* WeaponData);

	/** Server: equip the given slot index (grants its fire ability). */
	void EquipSlot(int32 SlotIndex);

	UFUNCTION(BlueprintPure, Category = "FPSR|Weapon")
	UFPSRWeaponDataAsset* GetCurrentWeapon() const;

	UFUNCTION(BlueprintPure, Category = "FPSR|Weapon")
	int32 GetCurrentSlotIndex() const { return CurrentSlotIndex; }

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	UFUNCTION()
	void OnRep_CurrentSlotIndex();

	UAbilitySystemComponent* GetOwnerASC() const;

	/** Server: clears the previous fire ability and grants the current weapon's fire ability. */
	void RefreshEquippedAbility();

	UPROPERTY(Replicated)
	TArray<TObjectPtr<UFPSRWeaponDataAsset>> WeaponSlots;

	UPROPERTY(ReplicatedUsing = OnRep_CurrentSlotIndex)
	int32 CurrentSlotIndex = -1;

	/** Server-only handle to the currently granted fire ability. */
	FGameplayAbilitySpecHandle GrantedFireAbilityHandle;
};
