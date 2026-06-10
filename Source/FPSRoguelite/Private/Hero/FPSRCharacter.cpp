// Copyright Epic Games, Inc. All Rights Reserved.

#include "Hero/FPSRCharacter.h"
#include "Core/FPSRPlayerController.h"
#include "Core/FPSRPlayerState.h"
#include "Core/FPSRLogChannels.h"
#include "Core/FPSRGameState.h"
#include "AbilitySystem/FPSRAbilitySystemComponent.h"
#include "AbilitySystem/Attributes/FPSRHealthSet.h"
#include "AbilitySystemComponent.h"
#include "Weapon/FPSRWeaponInventoryComponent.h"
#include "Weapon/FPSRWeaponFireComponent.h"
#include "Weapon/FPSRWeaponDataAsset.h"
#include "Hero/FPSRPlayerFeedbackComponent.h"

#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "Engine/Engine.h"

#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputActionValue.h"

AFPSRCharacter::AFPSRCharacter()
{
#if ENABLE_DRAW_DEBUG
	PrimaryActorTick.bCanEverTick = true; // debug-only: on-screen health readout (replaced by HUD in P3)
#else
	PrimaryActorTick.bCanEverTick = false;
#endif

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
	PlayerFeedback = CreateDefaultSubobject<UFPSRPlayerFeedbackComponent>(TEXT("PlayerFeedback"));

	// Required so the inventory component's registered weapon-instance subobjects replicate (engine: the
	// owning actor must also opt into the registered subobject list, not just the component).
	bReplicateUsingRegisteredSubObjectList = true;

	// Input actions, default weapons, and the mapping context are assigned in the
	// Blueprint subclass (BP_FPSRCharacter / BP_FPSRPlayerController) — no hardcoded
	// content paths in C++.
}

#if ENABLE_DRAW_DEBUG
void AFPSRCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Debug scaffolding (replaced by HUD in P3): on-screen health / dead readout for the local player.
	if (GEngine && IsLocallyControlled())
	{
		if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
		{
			const float Health = ASC->GetNumericAttribute(UFPSRHealthSet::GetHealthAttribute());
			const float MaxHealth = ASC->GetNumericAttribute(UFPSRHealthSet::GetMaxHealthAttribute());
			const bool bDead = Health <= 0.0f;
			const FString Msg = bDead
				? FString::Printf(TEXT("DEAD  (HP 0 / %.0f)"), MaxHealth)
				: FString::Printf(TEXT("HP: %.0f / %.0f"), Health, MaxHealth);
			GEngine->AddOnScreenDebugMessage((uint64)(UPTRINT)this, 0.0f, bDead ? FColor::Red : FColor::Green, Msg);
		}

		if (const AFPSRGameState* RunState = GetWorld() ? GetWorld()->GetGameState<AFPSRGameState>() : nullptr)
		{
			AFPSRPlayerState* PS = GetPlayerState<AFPSRPlayerState>();
			const int32 CardPicks = PS ? PS->GetCardPicksPending() : 0;
			const int32 RewardPicks = PS ? PS->GetMissionRewardPicksPending() : 0;
			const FString RunMsg = FString::Printf(TEXT("Lv %d   XP %d / %d   Picks %d (+%d rwd)   [%s%s]"),
				RunState->GetPartyLevel(), RunState->GetSharedXP(), RunState->GetRequiredXPForNextLevel(),
				CardPicks, RewardPicks, RunState->IsCombatPhase() ? TEXT("Combat") : TEXT("Boss"),
				RunState->IsRunPaused() ? TEXT(" FROZEN") : TEXT(""));
			GEngine->AddOnScreenDebugMessage((uint64)(UPTRINT)this + 1, 0.0f, FColor::Cyan, RunMsg);
		}
	}
}
#endif

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

		// Bind health out-of-health callback (server-only).
		if (HasAuthority())
		{
			if (const UFPSRHealthSet* HealthSet = AbilitySystemComponent->GetSet<UFPSRHealthSet>())
			{
				if (!HealthSet->OnOutOfHealth.IsBoundToObject(this))
				{
					HealthSet->OnOutOfHealth.AddUObject(this, &AFPSRCharacter::HandleOutOfHealth);
				}
			}
		}
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
	if (ReloadAction) { EIC->BindAction(ReloadAction, ETriggerEvent::Started, this, &AFPSRCharacter::Input_Reload); }
	if (ADSAction)
	{
		EIC->BindAction(ADSAction, ETriggerEvent::Started, this, &AFPSRCharacter::Input_ADSPressed);
		EIC->BindAction(ADSAction, ETriggerEvent::Completed, this, &AFPSRCharacter::Input_ADSReleased);
	}
	if (DashAction)
	{
		EIC->BindAction(DashAction, ETriggerEvent::Started, this, &AFPSRCharacter::Input_Dash);
	}
}

bool AFPSRCharacter::IsRunFrozen() const
{
	const AFPSRGameState* GS = GetWorld() ? GetWorld()->GetGameState<AFPSRGameState>() : nullptr;
	return GS && GS->IsRunPaused();
}

void AFPSRCharacter::Input_MoveForward(const FInputActionValue& Value)
{
	if (IsRunFrozen())
	{
		GetCharacterMovement()->StopMovementImmediately(); // kill residual slide during the freeze
		return;
	}
	const float AxisValue = Value.Get<float>();
	if (Controller && AxisValue != 0.0f)
	{
		AddMovementInput(GetActorForwardVector(), AxisValue);
	}
}

void AFPSRCharacter::Input_MoveRight(const FInputActionValue& Value)
{
	if (IsRunFrozen())
	{
		GetCharacterMovement()->StopMovementImmediately();
		return;
	}
	const float AxisValue = Value.Get<float>();
	if (Controller && AxisValue != 0.0f)
	{
		AddMovementInput(GetActorRightVector(), AxisValue);
	}
}

void AFPSRCharacter::Input_Look(const FInputActionValue& Value)
{
	if (IsRunFrozen())
	{
		return; // camera frozen during card selection (mouse goes to the card UI in Menu input mode)
	}
	const FVector2D LookAxis = Value.Get<FVector2D>();
	AddControllerYawInput(LookAxis.X);

	const float PitchInput = -LookAxis.Y; // negative = up, positive = down (matches AddControllerPitchInput)
	AddControllerPitchInput(PitchInput);

	// Forward downward input so manual recoil compensation cancels pending auto-recovery (no overshoot).
	if (WeaponFire && PitchInput > 0.0f)
	{
		WeaponFire->NotifyPlayerPitchCompensation(PitchInput);
	}
}

void AFPSRCharacter::Input_Fire(const FInputActionValue& Value)
{
	if (IsRunFrozen())
	{
		return; // no firing during the card-selection freeze
	}
	if (WeaponFire)
	{
		WeaponFire->StartFiring();
		// ChargeLaser: tell the server to start measuring the charge (anti-cheat alpha). Mirrors the ADS
		// pattern (local state + server RPC). Other archetypes fire via the GA's own activation replication.
		if (WeaponFire->IsChargingLaser())
		{
			ServerStartChargeLaser();
		}
	}
}

void AFPSRCharacter::Input_FireReleased(const FInputActionValue& Value)
{
	if (WeaponFire)
	{
		// Capture the charge state before StopFiring clears it: if this release ends a ChargeLaser charge, the
		// authoritative beam is activated by the server via this ordered Character-channel RPC (paired with the
		// ServerStartChargeLaser sent on press), not by GAS client→server activation, so begin always precedes
		// release on the server and there is no cross-channel race.
		const bool bWasChargingLaser = WeaponFire->IsChargingLaser();
		WeaponFire->StopFiring();
		if (bWasChargingLaser)
		{
			ServerReleaseChargeLaser();
		}
	}
}

void AFPSRCharacter::Input_EquipSlot1(const FInputActionValue& Value) { ServerEquipSlot(0); }
void AFPSRCharacter::Input_EquipSlot2(const FInputActionValue& Value) { ServerEquipSlot(1); }
void AFPSRCharacter::Input_EquipSlot3(const FInputActionValue& Value) { ServerEquipSlot(2); }

void AFPSRCharacter::Input_Reload(const FInputActionValue& Value)
{
	ServerReload();
}

void AFPSRCharacter::Input_ADSPressed(const FInputActionValue& Value)
{
	if (WeaponFire) { WeaponFire->SetAiming(true); }
	ServerSetAiming(true);
}

void AFPSRCharacter::Input_ADSReleased(const FInputActionValue& Value)
{
	if (WeaponFire) { WeaponFire->SetAiming(false); }
	ServerSetAiming(false);
}

void AFPSRCharacter::Input_Dash(const FInputActionValue& Value)
{
	if (IsRunFrozen())
	{
		return; // no dashing during the card-selection freeze
	}
	FVector Direction = GetLastMovementInputVector();
	Direction.Z = 0.0f;
	if (Direction.IsNearlyZero())
	{
		Direction = GetActorForwardVector();
		Direction.Z = 0.0f;
	}
	ServerDash(Direction.GetSafeNormal());
}

void AFPSRCharacter::ServerEquipSlot_Implementation(int32 SlotIndex)
{
	// No weapon switching during the card-selection freeze: the run is globally stopped, and locking the
	// equipped slot keeps a ThisWeapon-scope card's target deterministic (it can't be swapped mid-offer).
	if (IsRunFrozen())
	{
		return;
	}
	if (WeaponInventory)
	{
		WeaponInventory->EquipSlot(SlotIndex);
	}
}

void AFPSRCharacter::ServerReload_Implementation()
{
	if (WeaponInventory)
	{
		WeaponInventory->StartReload();
	}
}

void AFPSRCharacter::ServerSetAiming_Implementation(bool bNewAiming)
{
	if (WeaponFire)
	{
		WeaponFire->SetAiming(bNewAiming);
	}
}

void AFPSRCharacter::ServerStartChargeLaser_Implementation()
{
	// No charging during the card-selection freeze (mirrors the other server input gates).
	if (IsRunFrozen())
	{
		return;
	}
	if (WeaponFire)
	{
		WeaponFire->ServerBeginCharge();
	}
}

void AFPSRCharacter::ServerReleaseChargeLaser_Implementation()
{
	// Ordered after ServerStartChargeLaser on the Character channel, so the server's charge-start stamp is
	// already set. Activate the charged beam server-authoritatively. (A release arriving during the freeze still
	// activates: the ability itself gates on the freeze and drops the charge, mirroring the other fire paths.)
	if (WeaponFire)
	{
		WeaponFire->ServerReleaseCharge();
	}
}

void AFPSRCharacter::ServerDash_Implementation(FVector DashDirection)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Cooldown gate (server-authoritative).
	const float Now = World->GetTimeSeconds();
	if ((Now - LastDashTime) < DashCooldown)
	{
		return;
	}

	FVector Direction = DashDirection;
	Direction.Z = 0.0f;
	if (Direction.IsNearlyZero())
	{
		Direction = GetActorForwardVector();
		Direction.Z = 0.0f;
	}
	Direction = Direction.GetSafeNormal();
	if (Direction.IsNearlyZero())
	{
		return;
	}

	LastDashTime = Now;

	// Ignore other pawns (enemies + allies) for the dash window so the player can pass through a surround.
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	}

	// Launch along the dash direction (keep current vertical velocity so air dashes feel natural).
	LaunchCharacter(Direction * DashSpeed, true, false);

	// End the collision-ignore window after DashDuration.
	World->GetTimerManager().SetTimer(DashEndTimerHandle, this, &AFPSRCharacter::EndDash, FMath::Max(0.01f, DashDuration), false);
}

void AFPSRCharacter::EndDash()
{
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	}
}

void AFPSRCharacter::ApplyContactDamage(float DamageAmount, AActor* DamageInstigator)
{
	if (!HasAuthority() || DamageAmount <= 0.0f)
	{
		return;
	}

	// Invulnerability frames: ignore further hits within DamageInvulnerabilityDuration of the last
	// accepted hit, so a swarm can't stack damage in a single window (per-player, server-authoritative).
	const UWorld* World = GetWorld();
	const float Now = World ? World->GetTimeSeconds() : 0.0f;
	if ((Now - LastDamagedTime) < DamageInvulnerabilityDuration)
	{
		return;
	}
	LastDamagedTime = Now;

	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
	{
		ASC->ApplyModToAttribute(UFPSRHealthSet::GetHealthAttribute(), EGameplayModOp::Additive, -DamageAmount);
	}

	// Tell the owning client which direction the hit came from (CoD-style damage indicator, §2-14). Cosmetic,
	// owner-only, unreliable; the client converts the instigator location to a camera-relative angle.
	if (DamageInstigator)
	{
		if (AFPSRPlayerController* PC = Cast<AFPSRPlayerController>(GetController()))
		{
			PC->ClientNotifyDamageFrom(DamageInstigator->GetActorLocation());
		}
	}
}

void AFPSRCharacter::HandleOutOfHealth()
{
	// Placeholder: full Down-But-Not-Out / revive / respawn is P5 (Game.MD §2-13). Log for now.
	UE_LOG(LogFPSR, Warning, TEXT("[Player] %s reached 0 health (DBNO/respawn handling is P5)."), *GetNameSafe(this));
}

void AFPSRCharacter::RequestReload()
{
	ServerReload();
}

UAbilitySystemComponent* AFPSRCharacter::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}
