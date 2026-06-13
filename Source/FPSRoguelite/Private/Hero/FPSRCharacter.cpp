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
#include "FPSRCollisionChannels.h"

#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Sound/SoundBase.h"
#include "Particles/ParticleSystem.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Materials/MaterialInterface.h"

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
	// Player uses a distinct object channel so enemies can block the player while ignoring EACH OTHER (the swarm
	// overlaps and spreads via soft separation instead of expensive mutual physics blocking — Game.MD §1/§5).
	GetCapsuleComponent()->SetCollisionObjectType(ECC_FPSRPlayerPawn);

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

	// Attach the weapon meshes to the arms' weapon socket so the design-time preview (and runtime, when a weapon
	// DA leaves WeaponAttachSocket empty) sits at the grip. C++-created component sockets aren't editable in the BP,
	// hence WeaponAttachSocketName carries the default.
	WeaponMesh1P = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("WeaponMesh1P"));
	WeaponMesh1P->SetupAttachment(FirstPersonArms, WeaponAttachSocketName);
	WeaponMesh1P->SetOnlyOwnerSee(true);
	WeaponMesh1P->bCastDynamicShadow = false;
	WeaponMesh1P->CastShadow = false;
	WeaponMesh1P->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	WeaponMeshStatic1P = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WeaponMeshStatic1P"));
	WeaponMeshStatic1P->SetupAttachment(FirstPersonArms, WeaponAttachSocketName);
	WeaponMeshStatic1P->SetOnlyOwnerSee(true);
	WeaponMeshStatic1P->bCastDynamicShadow = false;
	WeaponMeshStatic1P->CastShadow = false;
	WeaponMeshStatic1P->SetCollisionEnabled(ECollisionEnabled::NoCollision);

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

void AFPSRCharacter::BeginPlay()
{
	Super::BeginPlay();

	// Bind regardless of local-control state: BeginPlay can run before the controller ref replicates on a remote
	// client's own pawn, so gating the bind on IsLocallyControlled() here would permanently miss it. The GameState
	// delegate is the trigger; the camera PP is only APPLIED for the locally controlled pawn (checked at apply time,
	// when controller state is stable). Binding on proxies / server-side pawns is a cheap no-op.
	TryBindVisionDelegate();
}

void AFPSRCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(VisionBindRetryTimerHandle);
	}

	if (bVisionDelegateBound)
	{
		if (AFPSRGameState* GS = GetWorld() ? GetWorld()->GetGameState<AFPSRGameState>() : nullptr)
		{
			GS->OnRunStateChanged.RemoveDynamic(this, &AFPSRCharacter::HandleRunStateChanged_Vision);
		}
		bVisionDelegateBound = false;
	}

	Super::EndPlay(EndPlayReason);
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
		// All archetypes (incl. ChargeLaser) fire through the single-press path: StartFiring activates the weapon's
		// fire ability, and ChargeLaser's ability runs the whole charge sequence server-side from its own timers.
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

void AFPSRCharacter::ServerDash_Implementation(FVector DashDirection)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// No dashing during the card-selection freeze (mirror the ServerEquipSlot server gate).
	// Input_Dash already gates client-side, but a dash RPC in flight when the freeze replicates must be rejected
	// on the server too, or the run is no longer globally stopped.
	if (IsRunFrozen())
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

void AFPSRCharacter::TryBindVisionDelegate()
{
	if (bVisionDelegateBound)
	{
		return;
	}

	AFPSRGameState* GS = GetWorld() ? GetWorld()->GetGameState<AFPSRGameState>() : nullptr;
	if (!GS)
	{
		// GameState not replicated yet — retry shortly (local client only).
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(VisionBindRetryTimerHandle, this, &AFPSRCharacter::TryBindVisionDelegate, 0.25f, false);
		}
		return;
	}

	GS->OnRunStateChanged.AddDynamic(this, &AFPSRCharacter::HandleRunStateChanged_Vision);
	bVisionDelegateBound = true;

	// Apply the current state immediately (in case the restriction was already active when we bound).
	HandleRunStateChanged_Vision();
}

void AFPSRCharacter::NotifyControllerChanged()
{
	Super::NotifyControllerChanged();

	// A pawn possessed AFTER BeginPlay (e.g. a late join while LimitedVision is already active) binds the vision
	// delegate before it is locally controlled, so the initial apply was skipped. Re-check now that the
	// controller is known so the local player catches a restriction that won't broadcast again.
	if (IsLocallyControlled())
	{
		TryBindVisionDelegate();   // ensure bound (no-op if already)
		HandleRunStateChanged_Vision();
	}
}

void AFPSRCharacter::HandleRunStateChanged_Vision()
{
	// Camera post-process only affects the local view — ignore on simulated proxies / server-side non-local pawns.
	if (!IsLocallyControlled())
	{
		return;
	}

	const AFPSRGameState* GS = GetWorld() ? GetWorld()->GetGameState<AFPSRGameState>() : nullptr;
	if (!GS)
	{
		return;
	}

	const bool bRestricted = GS->IsVisionRestricted();
	if (bRestricted != bVisionRestrictionApplied)
	{
		ApplyVisionRestriction(bRestricted);
	}
}

void AFPSRCharacter::ApplyVisionRestriction(bool bRestricted)
{
	if (!FirstPersonCamera)
	{
		return;
	}

	FPostProcessSettings& PP = FirstPersonCamera->PostProcessSettings;

	if (bRestricted)
	{
		if (VisionRestrictionMaterial)
		{
			PP.AddBlendable(VisionRestrictionMaterial, 1.0f);
		}
		else
		{
			// Built-in fallback: heavy vignette darkening the screen edges. Save the camera's authored vignette
			// override so it can be restored when the mission ends (don't clobber it).
			bSavedVignetteOverride = PP.bOverride_VignetteIntensity;
			SavedVignetteIntensity = PP.VignetteIntensity;
			PP.bOverride_VignetteIntensity = true;
			PP.VignetteIntensity = VisionVignetteIntensity;
		}
	}
	else
	{
		if (VisionRestrictionMaterial)
		{
			PP.RemoveBlendable(VisionRestrictionMaterial);
		}
		else
		{
			// Restore the camera's pre-mission vignette settings instead of force-disabling the override.
			PP.bOverride_VignetteIntensity = bSavedVignetteOverride;
			PP.VignetteIntensity = SavedVignetteIntensity;
		}
	}

	bVisionRestrictionApplied = bRestricted;
}

void AFPSRCharacter::RefreshFirstPersonWeaponVisual()
{
	// Owner-client cosmetic only (listen-server host's own pawn + remote autonomous proxy both reach here).
	if (!IsLocallyControlled() || !WeaponInventory)
	{
		return;
	}

	const UFPSRWeaponDataAsset* Weapon = WeaponInventory->GetCurrentWeapon();

	// Reset cached fire cosmetics; repopulated below when a weapon is equipped.
	CachedFireMontage = nullptr;
	CachedFireSound = nullptr;
	CachedMuzzleFlash = nullptr;
	CachedMuzzleSocket = NAME_None;

	ActiveWeaponMesh = nullptr;

	if (!Weapon)
	{
		// No weapon: hide both meshes.
		if (WeaponMesh1P) { WeaponMesh1P->SetSkeletalMeshAsset(nullptr); }
		if (WeaponMeshStatic1P) { WeaponMeshStatic1P->SetStaticMesh(nullptr); }
		return;
	}

	// Per-weapon DA socket overrides the character default (SOCKET_Weapon). KeepRelativeTransform preserves the
	// BP-authored relative offset so designers can fine-tune the grip alignment in the BP viewport.
	const FName AttachSocket = Weapon->WeaponAttachSocket.IsNone() ? WeaponAttachSocketName : Weapon->WeaponAttachSocket;

	// Skeletal weapon mesh (firearms) takes priority; static mesh (melee) is the fallback.
	USkeletalMesh* SkelMesh = Weapon->WeaponMesh1P.IsNull() ? nullptr : Weapon->WeaponMesh1P.LoadSynchronous();
	UStaticMesh* StaticMesh = (SkelMesh == nullptr && !Weapon->WeaponMeshStatic1P.IsNull())
		? Weapon->WeaponMeshStatic1P.LoadSynchronous() : nullptr;

	if (WeaponMesh1P)
	{
		// SetSkeletalMeshAsset is the engine's documented setter (calls SetSkeletalMesh(NewMesh, false)) — UE5.7.
		// The weapon mesh has its OWN skeleton (SKEL_LPAMG_<W>); arm anims drive the arms only (applied below).
		WeaponMesh1P->SetSkeletalMeshAsset(SkelMesh);
		WeaponMesh1P->AttachToComponent(FirstPersonArms, FAttachmentTransformRules::KeepRelativeTransform, AttachSocket);
	}
	if (WeaponMeshStatic1P)
	{
		WeaponMeshStatic1P->SetStaticMesh(StaticMesh);
		WeaponMeshStatic1P->AttachToComponent(FirstPersonArms, FAttachmentTransformRules::KeepRelativeTransform, AttachSocket);
	}

	// Track which mesh is actually shown so fire cosmetics attach to it (skeletal firearm vs static melee/preview).
	ActiveWeaponMesh = SkelMesh ? Cast<UMeshComponent>(WeaponMesh1P)
		: (StaticMesh ? Cast<UMeshComponent>(WeaponMeshStatic1P) : nullptr);

	// Optional per-weapon arms anim BP applied to the arms mesh (the pack ships per-weapon arm anims).
	if (FirstPersonArms && !Weapon->ArmsAnimInstanceClass.IsNull())
	{
		if (UClass* ArmsAnimClass = Weapon->ArmsAnimInstanceClass.LoadSynchronous())
		{
			FirstPersonArms->SetAnimInstanceClass(ArmsAnimClass);
		}
	}

	// Cache per-shot fire cosmetics (resolve soft refs once, here, not per shot).
	CachedFireMontage = Weapon->FireMontage.IsNull() ? nullptr : Weapon->FireMontage.LoadSynchronous();
	CachedFireSound = Weapon->FireSound.IsNull() ? nullptr : Weapon->FireSound.LoadSynchronous();
	CachedMuzzleFlash = Weapon->MuzzleFlash.IsNull() ? nullptr : Weapon->MuzzleFlash.LoadSynchronous();
	CachedMuzzleSocket = Weapon->MuzzleSocket;

	// Optional equip montage on the arms.
	if (FirstPersonArms && !Weapon->EquipMontage.IsNull())
	{
		if (UAnimMontage* EquipM = Weapon->EquipMontage.LoadSynchronous())
		{
			if (UAnimInstance* AnimInst = FirstPersonArms->GetAnimInstance())
			{
				AnimInst->Montage_Play(EquipM);
			}
		}
	}
}

void AFPSRCharacter::PlayWeaponFireCosmetics()
{
	if (!IsLocallyControlled())
	{
		return;
	}

	// Fire montage on the arms.
	if (CachedFireMontage && FirstPersonArms)
	{
		if (UAnimInstance* AnimInst = FirstPersonArms->GetAnimInstance())
		{
			AnimInst->Montage_Play(CachedFireMontage);
		}
	}

	// Fire sound + muzzle flash attach to the ACTIVE weapon mesh (skeletal firearm or static preview) so they track
	// whichever mesh the equipped weapon shows. CachedMuzzleSocket is a socket on that mesh (NAME_None = mesh origin).
	if (ActiveWeaponMesh)
	{
		if (CachedFireSound)
		{
			UGameplayStatics::SpawnSoundAttached(CachedFireSound, ActiveWeaponMesh);
		}
		if (CachedMuzzleFlash)
		{
			UGameplayStatics::SpawnEmitterAttached(CachedMuzzleFlash, ActiveWeaponMesh, CachedMuzzleSocket);
		}
	}
}

UAbilitySystemComponent* AFPSRCharacter::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}
