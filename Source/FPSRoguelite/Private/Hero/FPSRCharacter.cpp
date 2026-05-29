// Copyright Epic Games, Inc. All Rights Reserved.

#include "Hero/FPSRCharacter.h"
#include "Core/FPSRPlayerState.h"
#include "Core/FPSRLogChannels.h"
#include "AbilitySystem/FPSRAbilitySystemComponent.h"
#include "Weapon/FPSRWeaponInventoryComponent.h"
#include "Weapon/FPSRWeaponFireComponent.h"
#include "Weapon/FPSRWeaponDataAsset.h"

#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"

#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "UObject/ConstructorHelpers.h"

AFPSRCharacter::AFPSRCharacter()
{
	PrimaryActorTick.bCanEverTick = false;

	GetCapsuleComponent()->InitCapsuleSize(34.0f, 88.0f);

	bUseControllerRotationYaw = true;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;

	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->bOrientRotationToMovement = false;
		MoveComp->MaxWalkSpeed = 600.0f;
	}

	FirstPersonCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
	FirstPersonCamera->SetupAttachment(GetCapsuleComponent());
	FirstPersonCamera->SetRelativeLocation(FVector(0.0f, 0.0f, 64.0f));
	FirstPersonCamera->bUsePawnControlRotation = true;

	FirstPersonArms = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("FirstPersonArms"));
	FirstPersonArms->SetupAttachment(FirstPersonCamera);
	FirstPersonArms->SetOnlyOwnerSee(true);
	FirstPersonArms->bCastDynamicShadow = false;
	FirstPersonArms->CastShadow = false;

	if (USkeletalMeshComponent* BodyMesh = GetMesh())
	{
		BodyMesh->SetOwnerNoSee(true);
	}

	WeaponInventory = CreateDefaultSubobject<UFPSRWeaponInventoryComponent>(TEXT("WeaponInventory"));
	WeaponFire = CreateDefaultSubobject<UFPSRWeaponFireComponent>(TEXT("WeaponFire"));

	// Input actions generated under /Game/Input. The IMC is added by AFPSRPlayerController.
	static ConstructorHelpers::FObjectFinder<UInputAction> MoveFwdFinder(TEXT("/Game/Input/IA_MoveForward.IA_MoveForward"));
	if (MoveFwdFinder.Succeeded()) { MoveForwardAction = MoveFwdFinder.Object; }
	static ConstructorHelpers::FObjectFinder<UInputAction> MoveRightFinder(TEXT("/Game/Input/IA_MoveRight.IA_MoveRight"));
	if (MoveRightFinder.Succeeded()) { MoveRightAction = MoveRightFinder.Object; }
	static ConstructorHelpers::FObjectFinder<UInputAction> LookFinder(TEXT("/Game/Input/IA_Look.IA_Look"));
	if (LookFinder.Succeeded()) { LookAction = LookFinder.Object; }
	static ConstructorHelpers::FObjectFinder<UInputAction> JumpFinder(TEXT("/Game/Input/IA_Jump.IA_Jump"));
	if (JumpFinder.Succeeded()) { JumpAction = JumpFinder.Object; }
	static ConstructorHelpers::FObjectFinder<UInputAction> FireFinder(TEXT("/Game/Input/IA_Fire.IA_Fire"));
	if (FireFinder.Succeeded()) { FireAction = FireFinder.Object; }
	static ConstructorHelpers::FObjectFinder<UInputAction> Slot1Finder(TEXT("/Game/Input/IA_EquipSlot1.IA_EquipSlot1"));
	if (Slot1Finder.Succeeded()) { EquipSlot1Action = Slot1Finder.Object; }
	static ConstructorHelpers::FObjectFinder<UInputAction> Slot2Finder(TEXT("/Game/Input/IA_EquipSlot2.IA_EquipSlot2"));
	if (Slot2Finder.Succeeded()) { EquipSlot2Action = Slot2Finder.Object; }
	static ConstructorHelpers::FObjectFinder<UInputAction> Slot3Finder(TEXT("/Game/Input/IA_EquipSlot3.IA_EquipSlot3"));
	if (Slot3Finder.Succeeded()) { EquipSlot3Action = Slot3Finder.Object; }

	// Default weapons (created by the user under /Game/Weapons at test time). Null until then.
	static ConstructorHelpers::FObjectFinder<UFPSRWeaponDataAsset> RifleFinder(TEXT("/Game/Weapons/DA_Weapon_Rifle.DA_Weapon_Rifle"));
	if (RifleFinder.Succeeded()) { DefaultPrimaryWeapon = RifleFinder.Object; }
	static ConstructorHelpers::FObjectFinder<UFPSRWeaponDataAsset> KnifeFinder(TEXT("/Game/Weapons/DA_Weapon_Knife.DA_Weapon_Knife"));
	if (KnifeFinder.Succeeded()) { DefaultSecondaryWeapon = KnifeFinder.Object; }
}

void AFPSRCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	InitAbilitySystem();

	if (HasAuthority() && WeaponInventory)
	{
		if (DefaultPrimaryWeapon)
		{
			WeaponInventory->AddWeapon(DefaultPrimaryWeapon);
		}
		if (DefaultSecondaryWeapon)
		{
			WeaponInventory->AddWeapon(DefaultSecondaryWeapon);
		}
	}
}

void AFPSRCharacter::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();
	InitAbilitySystem();
}

void AFPSRCharacter::InitAbilitySystem()
{
	AFPSRPlayerState* PS = GetPlayerState<AFPSRPlayerState>();
	if (!PS)
	{
		return;
	}

	AbilitySystemComponent = PS->GetFPSRAbilitySystemComponent();
	if (AbilitySystemComponent)
	{
		AbilitySystemComponent->InitAbilityActorInfo(PS, this);
	}
}

void AFPSRCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!EIC)
	{
		UE_LOG(LogFPSR, Error, TEXT("[Input] PlayerInputComponent is not a UEnhancedInputComponent"));
		return;
	}

	UE_LOG(LogFPSR, Verbose, TEXT("[Input] Binding character actions"));

	if (MoveForwardAction) { EIC->BindAction(MoveForwardAction, ETriggerEvent::Triggered, this, &AFPSRCharacter::Input_MoveForward); }
	if (MoveRightAction)   { EIC->BindAction(MoveRightAction, ETriggerEvent::Triggered, this, &AFPSRCharacter::Input_MoveRight); }
	if (LookAction)        { EIC->BindAction(LookAction, ETriggerEvent::Triggered, this, &AFPSRCharacter::Input_Look); }
	if (JumpAction)
	{
		EIC->BindAction(JumpAction, ETriggerEvent::Started, this, &ACharacter::Jump);
		EIC->BindAction(JumpAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);
	}
	if (FireAction)
	{
		EIC->BindAction(FireAction, ETriggerEvent::Started, this, &AFPSRCharacter::Input_Fire);
		EIC->BindAction(FireAction, ETriggerEvent::Completed, this, &AFPSRCharacter::Input_FireReleased);
	}
	if (EquipSlot1Action) { EIC->BindAction(EquipSlot1Action, ETriggerEvent::Started, this, &AFPSRCharacter::Input_EquipSlot1); }
	if (EquipSlot2Action) { EIC->BindAction(EquipSlot2Action, ETriggerEvent::Started, this, &AFPSRCharacter::Input_EquipSlot2); }
	if (EquipSlot3Action) { EIC->BindAction(EquipSlot3Action, ETriggerEvent::Started, this, &AFPSRCharacter::Input_EquipSlot3); }
}

void AFPSRCharacter::Input_MoveForward(const FInputActionValue& Value)
{
	const float AxisValue = Value.Get<float>();
	if (Controller && AxisValue != 0.0f)
	{
		AddMovementInput(GetActorForwardVector(), AxisValue);
	}
}

void AFPSRCharacter::Input_MoveRight(const FInputActionValue& Value)
{
	const float AxisValue = Value.Get<float>();
	if (Controller && AxisValue != 0.0f)
	{
		AddMovementInput(GetActorRightVector(), AxisValue);
	}
}

void AFPSRCharacter::Input_Look(const FInputActionValue& Value)
{
	const FVector2D LookAxis = Value.Get<FVector2D>();
	AddControllerYawInput(LookAxis.X);
	AddControllerPitchInput(-LookAxis.Y);
}

void AFPSRCharacter::Input_Fire(const FInputActionValue& Value)
{
	if (WeaponFire)
	{
		WeaponFire->StartFiring();
	}
}

void AFPSRCharacter::Input_FireReleased(const FInputActionValue& Value)
{
	if (WeaponFire)
	{
		WeaponFire->StopFiring();
	}
}

void AFPSRCharacter::Input_EquipSlot1(const FInputActionValue& Value) { ServerEquipSlot(0); }
void AFPSRCharacter::Input_EquipSlot2(const FInputActionValue& Value) { ServerEquipSlot(1); }
void AFPSRCharacter::Input_EquipSlot3(const FInputActionValue& Value) { ServerEquipSlot(2); }

void AFPSRCharacter::ServerEquipSlot_Implementation(int32 SlotIndex)
{
	if (WeaponInventory)
	{
		WeaponInventory->EquipSlot(SlotIndex);
	}
}

UAbilitySystemComponent* AFPSRCharacter::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}
