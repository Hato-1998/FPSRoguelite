// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/Character.h"
#include "AbilitySystemInterface.h"
#include "FPSRCharacter.generated.h"

class UAbilitySystemComponent;
class UFPSRAbilitySystemComponent;
class UCameraComponent;
class USkeletalMeshComponent;
class UInputAction;
class UFPSRWeaponInventoryComponent;
class UFPSRWeaponFireComponent;
class UFPSRWeaponDataAsset;
struct FInputActionValue;

/** Base player character: first-person camera + Separated-Arms meshes + Enhanced Input + weapon inventory/firing. ASC lives on PlayerState. */
UCLASS()
class FPSROGUELITE_API AFPSRCharacter : public ACharacter, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	AFPSRCharacter();

	virtual void PossessedBy(AController* NewController) override;
	virtual void OnRep_PlayerState() override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	//~IAbilitySystemInterface
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	//~End IAbilitySystemInterface

protected:
	void InitAbilitySystem();

	// Enhanced Input handlers
	void Input_MoveForward(const FInputActionValue& Value);
	void Input_MoveRight(const FInputActionValue& Value);
	void Input_Look(const FInputActionValue& Value);
	void Input_Fire(const FInputActionValue& Value);
	void Input_FireReleased(const FInputActionValue& Value);
	void Input_EquipSlot1(const FInputActionValue& Value);
	void Input_EquipSlot2(const FInputActionValue& Value);
	void Input_EquipSlot3(const FInputActionValue& Value);

	/** Server: equip a weapon slot (input is client-side; equip is server-authoritative). */
	UFUNCTION(Server, Reliable)
	void ServerEquipSlot(int32 SlotIndex);

	UPROPERTY()
	TObjectPtr<UFPSRAbilitySystemComponent> AbilitySystemComponent;

	UPROPERTY(VisibleAnywhere, Category = "FPSR|Camera")
	TObjectPtr<UCameraComponent> FirstPersonCamera;

	/** First-person arms, visible to the owning client only. */
	UPROPERTY(VisibleAnywhere, Category = "FPSR|Mesh")
	TObjectPtr<USkeletalMeshComponent> FirstPersonArms;

	UPROPERTY(VisibleAnywhere, Category = "FPSR|Weapon")
	TObjectPtr<UFPSRWeaponInventoryComponent> WeaponInventory;

	UPROPERTY(VisibleAnywhere, Category = "FPSR|Weapon")
	TObjectPtr<UFPSRWeaponFireComponent> WeaponFire;

	/** Starting weapons granted on possession (slot order). Set via ConstructorHelpers (P1) / HeroDataAsset (later). */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Weapon")
	TObjectPtr<UFPSRWeaponDataAsset> DefaultPrimaryWeapon;

	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Weapon")
	TObjectPtr<UFPSRWeaponDataAsset> DefaultSecondaryWeapon;

	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Input")
	TObjectPtr<UInputAction> MoveForwardAction;

	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Input")
	TObjectPtr<UInputAction> MoveRightAction;

	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Input")
	TObjectPtr<UInputAction> LookAction;

	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Input")
	TObjectPtr<UInputAction> JumpAction;

	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Input")
	TObjectPtr<UInputAction> FireAction;

	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Input")
	TObjectPtr<UInputAction> EquipSlot1Action;

	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Input")
	TObjectPtr<UInputAction> EquipSlot2Action;

	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Input")
	TObjectPtr<UInputAction> EquipSlot3Action;
};
