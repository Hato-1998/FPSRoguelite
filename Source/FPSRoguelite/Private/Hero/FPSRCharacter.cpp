// Copyright Epic Games, Inc. All Rights Reserved.

#include "Hero/FPSRCharacter.h"
#include "Core/FPSRPlayerState.h"
#include "AbilitySystem/FPSRAbilitySystemComponent.h"

#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"

#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "UObject/ConstructorHelpers.h"

AFPSRCharacter::AFPSRCharacter()
{
	PrimaryActorTick.bCanEverTick = false;

	GetCapsuleComponent()->InitCapsuleSize(34.0f, 88.0f);

	// FPS rotation: body yaws with controller, pitch handled by camera only.
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

	// First-person arms: owner-only. Mesh asset assigned later (hero content).
	FirstPersonArms = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("FirstPersonArms"));
	FirstPersonArms->SetupAttachment(FirstPersonCamera);
	FirstPersonArms->SetOnlyOwnerSee(true);
	FirstPersonArms->bCastDynamicShadow = false;
	FirstPersonArms->CastShadow = false;

	// Third-person body (inherited Mesh): hidden from the owner, visible to others.
	if (USkeletalMeshComponent* BodyMesh = GetMesh())
	{
		BodyMesh->SetOwnerNoSee(true);
	}

	// Load default input assets generated under /Game/Input (see Scripts/gen_input_assets.py).
	static ConstructorHelpers::FObjectFinder<UInputMappingContext> IMCFinder(TEXT("/Game/Input/IMC_Default.IMC_Default"));
	if (IMCFinder.Succeeded()) { DefaultMappingContext = IMCFinder.Object; }

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

	static ConstructorHelpers::FObjectFinder<UInputAction> SwitchFinder(TEXT("/Game/Input/IA_SwitchWeapon.IA_SwitchWeapon"));
	if (SwitchFinder.Succeeded()) { SwitchWeaponAction = SwitchFinder.Object; }
}

void AFPSRCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	InitAbilitySystem();
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

	// Add the default mapping context for the locally controlled player.
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
			ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
		{
			if (DefaultMappingContext)
			{
				Subsystem->AddMappingContext(DefaultMappingContext, 0);
			}
		}
	}

	if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		if (MoveForwardAction)
		{
			EIC->BindAction(MoveForwardAction, ETriggerEvent::Triggered, this, &AFPSRCharacter::Input_MoveForward);
		}
		if (MoveRightAction)
		{
			EIC->BindAction(MoveRightAction, ETriggerEvent::Triggered, this, &AFPSRCharacter::Input_MoveRight);
		}
		if (LookAction)
		{
			EIC->BindAction(LookAction, ETriggerEvent::Triggered, this, &AFPSRCharacter::Input_Look);
		}
		if (JumpAction)
		{
			EIC->BindAction(JumpAction, ETriggerEvent::Started, this, &ACharacter::Jump);
			EIC->BindAction(JumpAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);
		}
	}
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

UAbilitySystemComponent* AFPSRCharacter::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}
