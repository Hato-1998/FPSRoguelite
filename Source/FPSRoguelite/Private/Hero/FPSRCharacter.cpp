// Copyright Epic Games, Inc. All Rights Reserved.

#include "Hero/FPSRCharacter.h"
#include "Engine/Texture2D.h"
#include "Core/FPSRPlayerController.h"
#include "Core/FPSRPlayerState.h"
#include "Core/FPSRGameMode.h"
#include "Core/FPSRLogChannels.h"
#include "Core/FPSRGameState.h"
#include "AbilitySystem/FPSRAbilitySystemComponent.h"
#include "AbilitySystem/Attributes/FPSRHealthSet.h"
#include "AbilitySystem/Attributes/FPSRCombatSet.h"
#include "AbilitySystemComponent.h"
#include "Weapon/FPSRWeaponInventoryComponent.h"
#include "Weapon/FPSRWeaponInstance.h"
#include "Weapon/FPSRWeaponFireComponent.h"
#include "Weapon/FPSRRecoilComponent.h"
#include "Weapon/FPSRWeaponFragment.h"
#include "Weapon/FPSRWeaponDataAsset.h"
#include "Weapon/FPSRWeaponAnimInstance.h"
#include "Weapon/FPSRWeaponPartSelector.h"
#include "Hero/FPSRPlayerFeedbackComponent.h"
#include "Hero/FPSRBlindspotAudioComponent.h"
#include "Hero/FPSRReviveComponent.h"
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
		MoveComp->MaxWalkSpeed = BaseWalkSpeed;
	}

	FirstPersonCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
	FirstPersonCamera->SetupAttachment(GetCapsuleComponent());
	FirstPersonCamera->SetRelativeLocation(FVector(0.0f, 0.0f, 64.0f));
	FirstPersonCamera->bUsePawnControlRotation = true;
	// Motion blur off on the player view: the camera-parented 1P weapon gets a large world-space velocity during camera
	// recoil / the ADS fire kick and would smear (ghost) even while screen-static, and a fast swarm reads better crisp.
	// This is the only camera, so it disables motion blur game-wide (a deliberate art choice for this genre).
	FirstPersonCamera->PostProcessSettings.bOverride_MotionBlurAmount = true;
	FirstPersonCamera->PostProcessSettings.MotionBlurAmount = 0.0f;

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

	// Third-person weapon mesh (U19): attached to the 3P body, visible to REMOTE observers only (SetOwnerNoSee — the
	// exact inverse of the 1P weapon's SetOnlyOwnerSee). Unlike the 1P weapon it keeps its shadow (world-visible).
	// The mesh + per-weapon body socket are set per-equip in RefreshFirstPersonWeaponVisual.
	WeaponMesh3P = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("WeaponMesh3P"));
	WeaponMesh3P->SetupAttachment(GetMesh());
	WeaponMesh3P->SetOwnerNoSee(true);
	WeaponMesh3P->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	WeaponInventory = CreateDefaultSubobject<UFPSRWeaponInventoryComponent>(TEXT("WeaponInventory"));
	WeaponFire = CreateDefaultSubobject<UFPSRWeaponFireComponent>(TEXT("WeaponFire"));
	RecoilComponent = CreateDefaultSubobject<UFPSRRecoilComponent>(TEXT("RecoilComponent"));
	PlayerFeedback = CreateDefaultSubobject<UFPSRPlayerFeedbackComponent>(TEXT("PlayerFeedback"));
	BlindspotAudio = CreateDefaultSubobject<UFPSRBlindspotAudioComponent>(TEXT("BlindspotAudio"));
	ReviveComponent = CreateDefaultSubobject<UFPSRReviveComponent>(TEXT("ReviveComponent"));

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
			const int32 UnlockPicks = PS ? PS->GetWeaponUnlockPicksPending() : 0;
			const FString RunMsg = FString::Printf(TEXT("Lv %d   XP %d / %d   Picks %d (+%d unlock)   [%s%s]"),
				RunState->GetPartyLevel(), RunState->GetSharedXP(), RunState->GetRequiredXPForNextLevel(),
				CardPicks, UnlockPicks, RunState->IsCombatPhase() ? TEXT("Combat") : TEXT("Boss"),
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

	// Capture the BP-authored arms anim default so a per-weapon ArmsAnimInstanceClass override can be reverted later
	// (P2-B). Mode may be "Use Animation Asset" (single-node idle) or a real AnimBP — restore the matching one.
	if (FirstPersonArms)
	{
		bDefaultArmsUsesBlueprint = (FirstPersonArms->GetAnimationMode() == EAnimationMode::AnimationBlueprint);
		DefaultArmsAnimClass = FirstPersonArms->AnimClass;
		// Hip base for procedural ADS: UpdateAimDownSights interpolates the arms toward/from this each frame (owner-local).
		BaseArmsRelLoc = FirstPersonArms->GetRelativeLocation();
		BaseArmsRelRot = FirstPersonArms->GetRelativeRotation();
		ADSAimLoc = BaseArmsRelLoc;
		ADSAimRot = BaseArmsRelRot;
		PreviousControlRotation = GetControlRotation();
	}
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
			GS->OnRunStateChanged.RemoveDynamic(this, &AFPSRCharacter::HandleRunStateChanged_Movement);
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
		// Lobby loadout pick (P7 §3-8): the chosen weapon is the single weapon for the run. Only when no pick
		// was made (e.g. debug FPSR.TravelGame straight into gameplay, bypassing the lobby) do we fall back to
		// the character BP's default loadout, so direct-to-gameplay testing still spawns armed.
		UFPSRWeaponDataAsset* SelectedWeapon = nullptr;
		if (const AFPSRPlayerState* PS = GetPlayerState<AFPSRPlayerState>())
		{
			SelectedWeapon = PS->GetSelectedWeapon();
		}

		if (SelectedWeapon)
		{
			WeaponInventory->AddWeapon(SelectedWeapon);
		}
		else
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

	// Reflect the current MoveSpeedMultiplier once (attribute may have replicated before this pawn existed,
	// or the pawn was possessed after the attribute was already set). Safe default 1.0 if the set isn't ready.
	if (const AFPSRPlayerState* FPSRPS = GetPlayerState<AFPSRPlayerState>())
	{
		if (const UFPSRCombatSet* CombatSet = FPSRPS->GetCombatSet())
		{
			ApplyMoveSpeedMultiplier(CombatSet->GetMoveSpeedMultiplier());
		}
	}
}

void AFPSRCharacter::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();
	InitAbilitySystem();

	// Reflect the current MoveSpeedMultiplier once (attribute may have replicated before this pawn existed,
	// or the pawn was possessed after the attribute was already set). Safe default 1.0 if the set isn't ready.
	if (const AFPSRPlayerState* FPSRPS = GetPlayerState<AFPSRPlayerState>())
	{
		if (const UFPSRCombatSet* CombatSet = FPSRPS->GetCombatSet())
		{
			ApplyMoveSpeedMultiplier(CombatSet->GetMoveSpeedMultiplier());
		}
	}
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

		// Run-start meta-progression stat seam (U10): server-authoritative, applied once the ASC actor info is ready.
		if (HasAuthority())
		{
			ApplyMetaProgressionEffects();
		}

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

void AFPSRCharacter::ApplyMetaProgressionEffects()
{
	// U10 seam — intentionally empty. P0-③ applies the player's persisted meta stats here as server-authoritative
	// GameplayEffects (idiom used across this codebase: ASC->MakeOutgoingSpec -> ApplyGameplayEffectSpecToSelf). Kept
	// as a named entry point so the run-start stat path has a single, discoverable insertion site (RunFlow §2-11).
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
	if (MenuAction)
	{
		EIC->BindAction(MenuAction, ETriggerEvent::Started, this, &AFPSRCharacter::Input_Menu);
	}

	// The pawn's input setup is the one hook guaranteed to run for the locally-controlled pawn after a travel
	// possession (the swapped gameplay PC's own SetupInputComponent does NOT re-run, so its mapping context would
	// otherwise never land — actions bound here but no key->action map = dead input). Apply the mapping context here.
	if (AFPSRPlayerController* FPSRPC = Cast<AFPSRPlayerController>(GetController()))
	{
		FPSRPC->ApplyDefaultMappingContext(TEXT("Char::SetupPlayerInputComponent"));
	}
}

bool AFPSRCharacter::IsRunFrozen() const
{
	const AFPSRGameState* GS = GetWorld() ? GetWorld()->GetGameState<AFPSRGameState>() : nullptr;
	return GS && GS->IsRunPaused();
}

bool AFPSRCharacter::IsAiming() const
{
	// Owner-local aim state lives on the weapon-fire component (set by Input_ADS* + ServerSetAiming). Exposed here as a
	// BlueprintPure so the 1P arms AnimBP can read a live, self-resetting aiming flag to drive its ADS pose/transitions.
	return WeaponFire && WeaponFire->IsAiming();
}

bool AFPSRCharacter::IsADSVisualActive() const
{
	// Owner-local ADS blend crossing the visual threshold. Reload-aware for free: a reload makes UpdateAimDownSights
	// treat the weapon as not-aiming, so CurrentADSAlpha interps back toward 0 and this drops during the reload. Only
	// updated on the locally-controlled client (server/remote keep it 0) — gates owner-local scope cosmetics only.
	return CurrentADSAlpha >= 0.5f;
}

bool AFPSRCharacter::IsScopeVisualActive() const
{
	return IsADSVisualActive() && CachedScopeDescriptor.bScopeOverlay;
}

bool AFPSRCharacter::IsADSFOVActive() const
{
	// The ADS FOV-zoom commit itself: aiming an ADS weapon and not reloading. AimSocket-independent (bCachedHasADS,
	// not the procedural-sight blend), so a bHasADS weapon with no sight still reads as aiming — the crosshair-hide
	// then tracks the same commit the FOV zoom uses (ResolveADSTargetFOV) and can't desync from it. Owner-local.
	if (!bCachedHasADS || !WeaponFire || !WeaponFire->IsAiming())
	{
		return false;
	}
	return !(WeaponInventory && WeaponInventory->IsReloading());
}

float AFPSRCharacter::ResolveADSTargetFOV(float DefaultFOV, float BaseADSFOV, bool bBaseWantsADS) const
{
	if (!bBaseWantsADS)
	{
		return DefaultFOV;
	}

	// A fullscreen scope drops the zoom during a reload (design decision — show the weapon + reload animation);
	// non-scope sights keep their zoom through the reload (existing behavior). Owner-local; reads the replicated
	// reload edge via IsReloading().
	if (CachedScopeDescriptor.bScopeOverlay && WeaponInventory && WeaponInventory->IsReloading())
	{
		return DefaultFOV;
	}

	// Per-sight magnification: the ACTIVE sight's own AimFieldOfView (iron / reddot / scope each carry their own zoom),
	// falling back to the weapon's default ADS FOV when this sight authors none (<=0). No active sight descriptor =>
	// AimFieldOfView 0 => weapon default, so weapons without an authored sight are unchanged.
	return CachedScopeDescriptor.AimFieldOfView > 0.0f ? CachedScopeDescriptor.AimFieldOfView : BaseADSFOV;
}

void AFPSRCharacter::UpdateScopeWeaponVisibility()
{
	// Hide the 1P arms (propagate to children: weapon mesh + parts) while a full-screen scope is active, so the scope
	// overlay reads without the gun in the way. Toggle only on change. Owner-local: the caller (weapon-fire tick)
	// already no-ops for non-local pawns, and the arms are OnlyOwnerSee regardless. Auto-restores when the scope drops
	// (release / reload / weapon swap) because IsScopeVisualActive() then returns false.
	const bool bShouldHide = IsScopeVisualActive() && CachedScopeDescriptor.bHideWeaponWhileScoped;
	if (bShouldHide == bWeaponHiddenForScope)
	{
		return;
	}
	bWeaponHiddenForScope = bShouldHide;
	if (FirstPersonArms)
	{
		FirstPersonArms->SetVisibility(!bShouldHide, /*bPropagateToChildren=*/true);
	}
}

UTexture2D* AFPSRCharacter::GetActiveScopeReticle() const
{
	// Loaded only when a scope is actually active — the HUD calls this on the scoped edge (not per frame), so the
	// synchronous soft-ptr load happens at most once per scope-in. Null (WBP default reticle) when none authored.
	if (!IsScopeVisualActive())
	{
		return nullptr;
	}
	return CachedScopeDescriptor.ScopeReticle.LoadSynchronous();
}

bool AFPSRCharacter::IsScopeVignetteEnabled() const
{
	return IsScopeVisualActive() && CachedScopeDescriptor.bScopeVignette;
}

bool AFPSRCharacter::IsIncapacitatedLocal() const
{
	// Not a live participant: DBNO (downed) OR Dead. Gates actions (fire/dash/swap/reload/ADS) + contact damage.
	const AFPSRPlayerState* PS = GetPlayerState<AFPSRPlayerState>();
	return !PS || !PS->IsAlive();
}

bool AFPSRCharacter::IsTrulyDeadLocal() const
{
	// Truly out of the run (blocks even crawl/look). DBNO is NOT truly dead — it can still crawl + look around.
	const AFPSRPlayerState* PS = GetPlayerState<AFPSRPlayerState>();
	return PS && PS->IsDead();
}

bool AFPSRCharacter::CanJumpInternal_Implementation() const
{
	// No jumping while downed (DBNO) or dead.
	return Super::CanJumpInternal_Implementation() && !IsIncapacitatedLocal();
}

void AFPSRCharacter::ApplyMoveSpeedMultiplier(float Mult)
{
	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->MaxWalkSpeed = BaseWalkSpeed * Mult;
	}
}

void AFPSRCharacter::ApplyDownedLocomotion(bool bDowned)
{
	UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	if (!MoveComp)
	{
		return;
	}
	if (bDowned)
	{
		// Stationary: DBNO no longer crawls — the player stays where it fell and spectates an ally (§2-13). Movement
		// input is also gated for !Alive (Input_Move*), so this is belt-and-suspenders against residual slide.
		// (DownedMoveScale is now unused — kept for a possible future crawl toggle.)
		MoveComp->MaxWalkSpeed = 0.0f;
	}
	else
	{
		// Restore to the combat-multiplier-driven speed (revive / re-enter Alive). Default 1.0 if the set isn't ready.
		float Mult = 1.0f;
		if (const AFPSRPlayerState* FPSRPS = GetPlayerState<AFPSRPlayerState>())
		{
			if (const UFPSRCombatSet* CombatSet = FPSRPS->GetCombatSet())
			{
				Mult = CombatSet->GetMoveSpeedMultiplier();
			}
		}
		MoveComp->MaxWalkSpeed = BaseWalkSpeed * Mult;
	}
}

void AFPSRCharacter::Input_MoveForward(const FInputActionValue& Value)
{
	// Downed (DBNO) is stationary + spectating an ally (§2-13), so block movement for DBNO and Dead alike. A hard
	// freeze (card select) also stops movement here.
	if (IsRunFrozen() || IsIncapacitatedLocal())
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
	// Downed (DBNO) is stationary (spectating an ally) — same gate as MoveForward.
	if (IsRunFrozen() || IsIncapacitatedLocal())
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
	// Downed (DBNO) spectates a living ally, so the local camera/look is locked (block DBNO + Dead). A hard freeze
	// (card select) also locks it.
	if (IsRunFrozen() || IsIncapacitatedLocal())
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
	if (IsRunFrozen() || IsIncapacitatedLocal())
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

void AFPSRCharacter::Input_EquipSlot1(const FInputActionValue& Value) { if (IsRunFrozen() || IsIncapacitatedLocal()) { return; } ServerEquipSlot(0); }
void AFPSRCharacter::Input_EquipSlot2(const FInputActionValue& Value) { if (IsRunFrozen() || IsIncapacitatedLocal()) { return; } ServerEquipSlot(1); }
void AFPSRCharacter::Input_EquipSlot3(const FInputActionValue& Value) { if (IsRunFrozen() || IsIncapacitatedLocal()) { return; } ServerEquipSlot(2); }

void AFPSRCharacter::Input_Reload(const FInputActionValue& Value)
{
	if (IsRunFrozen() || IsIncapacitatedLocal()) { return; }
	ServerReload();
}

void AFPSRCharacter::Input_ADSPressed(const FInputActionValue& Value)
{
	if (IsRunFrozen() || IsIncapacitatedLocal()) { return; }
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
	if (IsRunFrozen() || IsIncapacitatedLocal())
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

void AFPSRCharacter::Input_Menu(const FInputActionValue& Value)
{
	// Release any held fire/ADS before opening the menu. The settings overlay is a NON-PAUSE Menu overlay, so once
	// it captures UI input the trigger-release IA never reaches Input_FireReleased/Input_ADSReleased — the
	// locally-latched bWantsToFire/aim would otherwise persist and the weapon would keep auto-firing while the menu
	// is open (W1 P2). Mirrors Input_FireReleased + Input_ADSReleased.
	if (WeaponFire)
	{
		WeaponFire->StopFiring();
		WeaponFire->SetAiming(false);
	}
	ServerSetAiming(false);

	// Settings overlay is intentionally available even while dead / during the freeze (it's a menu, not
	// gameplay). The owning PC handles the push; CommonUI Back closes it.
	if (AFPSRPlayerController* FPSRPC = Cast<AFPSRPlayerController>(GetController()))
	{
		FPSRPC->OpenSettingsOverlay();
	}
}

void AFPSRCharacter::ServerEquipSlot_Implementation(int32 SlotIndex)
{
	// No weapon switching during the card-selection freeze: the run is globally stopped, and locking the
	// equipped slot keeps a ThisWeapon-scope card's target deterministic (it can't be swapped mid-offer).
	if (IsRunFrozen() || IsIncapacitatedLocal())
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
	if (IsIncapacitatedLocal()) { return; }
	if (WeaponInventory)
	{
		WeaponInventory->StartReload();
	}
}

void AFPSRCharacter::ServerSetAiming_Implementation(bool bNewAiming)
{
	// Mirror the ServerEquipSlot/ServerDash server gate: reject an in-flight ADS RPC during the freeze so the OnAim
	// behavior hook can't fire while the run is globally stopped (W1 P3-3). Input_ADS already gates client-side.
	if (IsRunFrozen() || IsIncapacitatedLocal()) { return; }
	if (WeaponFire)
	{
		WeaponFire->SetAiming(bNewAiming);
	}

	// OnAim behavior trigger (server): fire after the authoritative aiming state is set. Aiming is weapon-agnostic,
	// but the hooks live on the equipped weapon's fragments, so build a minimal FireContext from it (§2-3-5). This
	// is the only server-authoritative aiming entry point; the hook is fire-only (persistent aim buffs are a follow-up).
	if (WeaponInventory)
	{
		if (UFPSRWeaponInstance* Instance = WeaponInventory->GetCurrentInstance())
		{
			FFPSRFireContext AimCtx;
			AimCtx.Avatar = this;
			AimCtx.Controller = GetController();
			AimCtx.World = GetWorld();
			AimCtx.Instance = Instance;
			AimCtx.bAuthority = true;
			FPSRWeaponHooks::NotifyAim(AimCtx, bNewAiming);
		}
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
	if (IsRunFrozen() || IsIncapacitatedLocal())
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

	// Ignore other pawns (enemies + allies) for the dash window so the player can pass through a surround. Routed
	// through the shared helper so it composes with a post-revive grace window (RefreshPawnCollisionResponse derives
	// "dashing" from LastDashTime + DashDuration, just set above).
	RefreshPawnCollisionResponse();

	// Launch along the dash direction (keep current vertical velocity so air dashes feel natural).
	LaunchCharacter(Direction * DashSpeed, true, false);

	// End the collision-ignore window after DashDuration.
	World->GetTimerManager().SetTimer(DashEndTimerHandle, this, &AFPSRCharacter::EndDash, FMath::Max(0.01f, DashDuration), false);
}

void AFPSRCharacter::EndDash()
{
	// Recompute rather than unconditionally block — a grace window may still want enemy pass-through.
	RefreshPawnCollisionResponse();
}

void AFPSRCharacter::RefreshPawnCollisionResponse()
{
	UCapsuleComponent* Capsule = GetCapsuleComponent();
	if (!Capsule)
	{
		return;
	}
	const UWorld* World = GetWorld();
	const float Now = World ? World->GetTimeSeconds() : 0.0f;
	// Both windows derive from server timestamps so an overlapping dash + grace window compose correctly — whichever
	// ends first doesn't restore enemy blocking while the other is still active.
	const bool bDashing = (Now - LastDashTime) < DashDuration;
	const bool bGrace = Now < GraceUntil;
	Capsule->SetCollisionResponseToChannel(ECC_Pawn, (bDashing || bGrace) ? ECR_Ignore : ECR_Block);
}

void AFPSRCharacter::BeginGraceWindow(float Seconds)
{
	UWorld* World = GetWorld();
	if (!HasAuthority() || Seconds <= 0.0f || !World)
	{
		return;
	}
	GraceUntil = World->GetTimeSeconds() + Seconds;

	// Pass through enemy pawns for the grace window (mirrors the dash collision-ignore) so the player can walk out of a
	// surround — the swarm that downed them (post-revive) or that closed in during the card freeze (post-freeze). The
	// shared helper composes this with any active dash window.
	RefreshPawnCollisionResponse();
	World->GetTimerManager().SetTimer(GraceTimerHandle, this, &AFPSRCharacter::EndGraceWindow, Seconds, false);
}

void AFPSRCharacter::EndGraceWindow()
{
	// Recompute rather than unconditionally block — a dash may still be in its own collision-ignore window.
	RefreshPawnCollisionResponse();
}

void AFPSRCharacter::ApplyContactDamage(float DamageAmount, AActor* DamageInstigator, FGameplayTag DamageType)
{
	(void)DamageType; // U18a seam (D3 elemental)
	if (!HasAuthority() || DamageAmount <= 0.0f)
	{
		return;
	}

	// Non-alive players take no contact damage: a downed (DBNO) player is invulnerable while awaiting revive (a swarm
	// would otherwise instakill the downed body), and a dead player takes no repeated corpse hits. (U9 §2-13)
	if (IsIncapacitatedLocal())
	{
		return;
	}

	const UWorld* World = GetWorld();
	const float Now = World ? World->GetTimeSeconds() : 0.0f;

	// Grace window (§2-13): the player is invulnerable for a short window after a revive (post-revive grace) or after
	// the card-selection freeze resumes (post-freeze grace), so a surrounding swarm can't instantly down them.
	// Server-authoritative timestamp set in BeginGraceWindow.
	if (Now < GraceUntil)
	{
		return;
	}

	// Invulnerability frames: ignore further hits within DamageInvulnerabilityDuration of the last
	// accepted hit, so a swarm can't stack damage in a single window (per-player, server-authoritative).
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
	// Server-authoritative (bound under HasAuthority in InitAbilitySystem). U9 DBNO (Game.MD §2-13): the player goes
	// DOWN (revivable) instead of dying outright. Stop all action, switch to a crawl, then ask the GameMode to check
	// for a team wipe (no Alive players remain) -> EndRun(Defeat). Revive back to Alive is UFPSRReviveComponent.
	UE_LOG(LogFPSR, Log, TEXT("[Player] %s reached 0 health -> DBNO (downed)."), *GetNameSafe(this));

	if (AFPSRPlayerState* PS = GetPlayerState<AFPSRPlayerState>())
	{
		if (!PS->IsAlive())
		{
			return; // already processed (idempotent: already DBNO or Dead)
		}
		PS->SetLifeState(EFPSRLifeState::DBNO);
	}

	// Stop firing and cancel any in-progress ability (e.g. the server-only ChargeLaser charge sequence) so a downed
	// player can't keep dealing damage. Clear aiming so ADS doesn't stay latched.
	if (WeaponFire)
	{
		WeaponFire->StopFiring();
		WeaponFire->SetAiming(false);
	}
	if (AbilitySystemComponent)
	{
		AbilitySystemComponent->CancelAllAbilities();
	}

	// Downed locomotion: drop residual velocity and switch to crawl speed (movement stays ENABLED so the downed
	// player can crawl out of danger / toward an ally). The owning client mirrors this from OnRep_LifeState so its
	// movement prediction matches; revive restores the normal speed.
	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		Move->StopMovementImmediately();
	}
	ApplyDownedLocomotion(true);

	// Switch the downed player's camera to a living ally immediately (spectate, §2-13). The ReviveComponent's server
	// tick maintains / re-picks it; PerformRevive restores the own-pawn view. No-op if no ally (a wipe follows below).
	if (UFPSRReviveComponent* Revive = FindComponentByClass<UFPSRReviveComponent>())
	{
		Revive->UpdateDownedSpectate();
	}

	// Wipe check: if no Alive players remain (solo down, or the last teammate falls) the GameMode ends in Defeat.
	if (AFPSRGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<AFPSRGameMode>() : nullptr)
	{
		GM->NotifyPlayerDefeated();
	}
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

	// Bind both run-state reactions together (lifecycle shared, gated by bVisionDelegateBound): the local-only vision
	// PP and the authority-only movement halt (§2-2 freeze must stop an in-flight dash, not just gate new input).
	GS->OnRunStateChanged.AddDynamic(this, &AFPSRCharacter::HandleRunStateChanged_Vision);
	GS->OnRunStateChanged.AddDynamic(this, &AFPSRCharacter::HandleRunStateChanged_Movement);
	bVisionDelegateBound = true;

	// Apply the current state immediately (in case the restriction / freeze was already active when we bound).
	HandleRunStateChanged_Vision();
	HandleRunStateChanged_Movement();
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

	// Clear ADS when the run enters a freeze (card selection / global freeze). The freeze UI can capture the
	// ADS-release input, otherwise leaving the owner latched in ADS behind the card modal — with W-U2 that means a
	// stuck scope zoom + hidden 1P weapon. Same clear the settings menu (Input_Menu) and the down state
	// (OnRep_LifeState) already do; idempotent (input is gated during freeze so aim can't restart). Owner-local + RPC.
	if (GS->IsRunPaused() && WeaponFire && WeaponFire->IsAiming())
	{
		WeaponFire->SetAiming(false);
		ServerSetAiming(false);
	}

	const bool bRestricted = GS->IsVisionRestricted();
	if (bRestricted != bVisionRestrictionApplied)
	{
		ApplyVisionRestriction(bRestricted);
	}
}

void AFPSRCharacter::HandleRunStateChanged_Movement()
{
	// §2-2 freeze is a STATE gate (not time dilation), so the CharacterMovement keeps integrating while the run is
	// paused. Input-driven moves already gate on IsRunFrozen, but a dash is an impulse (ServerDash -> LaunchCharacter)
	// whose velocity — and its collision-ignore window — would otherwise carry the player across the frozen card screen
	// (the sibling fire/equip/dash-INITIATION gates don't cover an already-launched dash). Run on the authority (the
	// server owns every pawn here; CMC replicates the stop) and halt residual locomotion + cancel any live dash.
	if (!HasAuthority())
	{
		return;
	}
	UWorld* World = GetWorld();
	const AFPSRGameState* GS = World ? World->GetGameState<AFPSRGameState>() : nullptr;
	const bool bPaused = GS && GS->IsRunPaused();

	// Post-card-selection resume grace (§2-13): when the global freeze ENDS, grant a short grace window so a player who
	// unfreezes standing in the swarm isn't hit the instant the world resumes (the card screen otherwise can't be left
	// safely). Fires once on the paused->unpaused transition; BeginGraceWindow is server-only (we're on authority).
	if (bWasRunPausedAuth && !bPaused)
	{
		BeginGraceWindow(PostFreezeInvulnSeconds);
	}
	bWasRunPausedAuth = bPaused;

	if (!bPaused)
	{
		return; // only act on entering the freeze; resume restores normal input-driven control (dash was cancelled)
	}

	GetCharacterMovement()->StopMovementImmediately(); // kill the dash impulse / residual slide so the player is stopped
	FTimerManager& TimerManager = World->GetTimerManager();
	if (TimerManager.IsTimerActive(DashEndTimerHandle))
	{
		// Cancel the in-flight dash cleanly: drop the pending end-timer and restore pawn-collision blocking now.
		TimerManager.ClearTimer(DashEndTimerHandle);
		EndDash();
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
	// Populate the 1P arms/weapon visual on ALL clients (not just the owner). A DOWNED teammate spectates this pawn via
	// SetViewTarget (§2-13 DBNO), and OnlyOwnerSee is evaluated against the VIEW TARGET — so the spectator renders THIS
	// pawn's 1P arms + weapon. The weapon mesh is set dynamically per-equip, so if it isn't populated on the spectator's
	// client the downed viewer sees arms but no gun. The 1P meshes stay OnlyOwnerSee (non-spectating remotes still don't
	// see them); the cached fire cosmetics below are owner-only-used (PlayWeaponFireCosmetics gates on IsLocallyControlled)
	// so they are harmless on remote pawns.
	if (!WeaponInventory)
	{
		return;
	}

	const UFPSRWeaponDataAsset* Weapon = WeaponInventory->GetCurrentWeapon();

	// Reset cached fire cosmetics; repopulated below when a weapon is equipped.
	CachedFireMontage = nullptr;
	CachedReloadMontage = nullptr;
	CachedWeaponFireMontage = nullptr;
	CachedWeaponReloadMontage = nullptr;
	CachedFireSound = nullptr;
	CachedMuzzleFlash = nullptr;
	CachedMuzzleSocket = NAME_None;

	ActiveWeaponMesh = nullptr;
	CachedMuzzleComponent = nullptr;
	CachedAimComponent = nullptr;

	// Reset ADS caching; the arms interp back to the hip base when no weapon provides ADS.
	CachedAimSocket = NAME_None;
	bCachedHasADS = false;
	bCachedADSAlignRotation = true;
	CachedADSAimRotationOffset = FRotator::ZeroRotator;
	bCachedSuppressFireMontagesWhileADS = true;

	// Reset hip procedural weapon motion (owner-local cosmetic); no weapon = no hip motion until the next equip.
	bCachedHasHipMotion = false;

	if (!Weapon)
	{
		// No weapon: hide all meshes (1P + 3P) and drop any modular parts.
		if (WeaponMesh1P) { WeaponMesh1P->SetSkeletalMeshAsset(nullptr); }
		if (WeaponMeshStatic1P) { WeaponMeshStatic1P->SetStaticMesh(nullptr); }
		if (WeaponMesh3P) { WeaponMesh3P->SetSkeletalMeshAsset(nullptr); }
		RefreshWeaponPartComponents(nullptr);
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
		// Per-weapon WEAPON-mesh AnimBP so the bolt/magazine montages (A_FP_WEP_<W>_*) can play on the weapon's own
		// skeleton. Only a skeletal weapon has one; clear it otherwise so a static/next weapon keeps no stale bolt anim.
		if (SkelMesh && !Weapon->WeaponAnimInstanceClass.IsNull())
		{
			if (UClass* WeaponAnimClass = Weapon->WeaponAnimInstanceClass.LoadSynchronous())
			{
				WeaponMesh1P->SetAnimInstanceClass(WeaponAnimClass);
				// Inject the data-driven fire-part recoil params into the weapon AnimInstance (when it's our native base)
				// so the AnimBP's ModifyBone offset comes from the DA curve/distance/axis, not a per-AnimBP hardcode.
				if (UFPSRWeaponAnimInstance* WeaponAnimInst = Cast<UFPSRWeaponAnimInstance>(WeaponMesh1P->GetAnimInstance()))
				{
					WeaponAnimInst->SetFirePartRecoilParams(Weapon->FirePartRecoilCurve, Weapon->FirePartRecoilDistanceCm, Weapon->FirePartRecoilAxis);
				}
			}
		}
		else
		{
			WeaponMesh1P->SetAnimInstanceClass(nullptr);
		}
	}
	if (WeaponMeshStatic1P)
	{
		WeaponMeshStatic1P->SetStaticMesh(StaticMesh);
		WeaponMeshStatic1P->AttachToComponent(FirstPersonArms, FAttachmentTransformRules::KeepRelativeTransform, AttachSocket);
	}

	// Track which mesh is actually shown so fire cosmetics attach to it (skeletal firearm vs static melee/preview).
	ActiveWeaponMesh = SkelMesh ? Cast<UMeshComponent>(WeaponMesh1P)
		: (StaticMesh ? Cast<UMeshComponent>(WeaponMeshStatic1P) : nullptr);

	// Third-person weapon mesh (U19) for REMOTE observers: attach to the 3P body hand socket. This runs on all clients
	// (Refresh is all-clients); WeaponMesh3P is SetOwnerNoSee, so the owner never sees it. Null 3P mesh = nothing shown.
	if (WeaponMesh3P)
	{
		USkeletalMesh* SkelMesh3P = Weapon->WeaponMesh3P.IsNull() ? nullptr : Weapon->WeaponMesh3P.LoadSynchronous();
		WeaponMesh3P->SetSkeletalMeshAsset(SkelMesh3P);
		WeaponMesh3P->AttachToComponent(GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, Weapon->WeaponAttachSocket3P);
	}

	// Optional per-weapon arms anim BP applied to the arms mesh (the pack ships per-weapon arm anims). When the next
	// weapon has no override, revert to the BP-authored default — but only if we actually applied one before, so a
	// loadout that never uses overrides keeps its "Use Animation Asset" idle untouched (P2-B).
	if (FirstPersonArms)
	{
		if (!Weapon->ArmsAnimInstanceClass.IsNull())
		{
			if (UClass* ArmsAnimClass = Weapon->ArmsAnimInstanceClass.LoadSynchronous())
			{
				FirstPersonArms->SetAnimInstanceClass(ArmsAnimClass);
				bArmsAnimOverridden = true;
			}
		}
		else if (bArmsAnimOverridden)
		{
			if (bDefaultArmsUsesBlueprint)
			{
				FirstPersonArms->SetAnimInstanceClass(DefaultArmsAnimClass);
			}
			else
			{
				// Restore single-node mode (re-inits from the component's BP-authored AnimationData / idle).
				FirstPersonArms->SetAnimationMode(EAnimationMode::AnimationSingleNode);
			}
			bArmsAnimOverridden = false;
		}
	}

	// Cache per-shot fire cosmetics (resolve soft refs once, here, not per shot).
	CachedFireMontage = Weapon->FireMontage.IsNull() ? nullptr : Weapon->FireMontage.LoadSynchronous();
	CachedReloadMontage = Weapon->ReloadMontage.IsNull() ? nullptr : Weapon->ReloadMontage.LoadSynchronous();
	CachedWeaponFireMontage = Weapon->WeaponFireMontage.IsNull() ? nullptr : Weapon->WeaponFireMontage.LoadSynchronous();
	CachedWeaponReloadMontage = Weapon->WeaponReloadMontage.IsNull() ? nullptr : Weapon->WeaponReloadMontage.LoadSynchronous();
	CachedFireSound = Weapon->FireSound.IsNull() ? nullptr : Weapon->FireSound.LoadSynchronous();
	CachedMuzzleFlash = Weapon->MuzzleFlash.IsNull() ? nullptr : Weapon->MuzzleFlash.LoadSynchronous();
	CachedMuzzleSocket = Weapon->MuzzleSocket;
	CachedMuzzleFlashRotationOffset = Weapon->MuzzleFlashRotationOffset;

	// ADS params for the owner-local procedural aim-down-sights (UpdateAimDownSights).
	CachedAimSocket = Weapon->AimSocket;
	CachedADSSightDistance = Weapon->ADSSightDistance;
	bCachedHasADS = Weapon->BaseStats.bHasADS;
	bCachedADSAlignRotation = Weapon->bADSAlignRotation;
	CachedADSAimRotationOffset = Weapon->ADSAimRotationOffset;
	bCachedSuppressFireMontagesWhileADS = Weapon->bSuppressFireMontagesWhileADS;
	CachedADSInterpSpeed = Weapon->BaseStats.ADSInterpSpeed;
	CachedADSPositionBobScale = Weapon->ADSPositionBobScale;
	bCachedSuppressWeaponBoltWhileADS = Weapon->bSuppressWeaponBoltWhileADS;
	CachedADSMuzzleFlashScale = Weapon->ADSMuzzleFlashScale;
	CachedADSFireKickDegrees = Weapon->ADSFireKickDegrees;
	CachedADSFireKickRecoveryRate = Weapon->ADSFireKickRecoveryRate;
	CachedADSSwayYawDegrees = Weapon->ADSSwayYawDegrees;
	CachedADSSwayPitchDegrees = Weapon->ADSSwayPitchDegrees;
	CachedADSSwaySpeed = Weapon->ADSSwaySpeed;

	// Hip procedural weapon motion (owner-local cosmetic): cache the profile and reset per-weapon runtime state so a
	// weapon swap doesn't carry over the previous weapon's sway/bob/kick or a stale control-rotation delta.
	CachedHipMotion = Weapon->ProceduralWeaponMotion;
	bCachedHasHipMotion =
		CachedHipMotion.LookSwayAmount > 0.0f || CachedHipMotion.WalkBobHorizontal > 0.0f ||
		CachedHipMotion.WalkBobVertical > 0.0f || CachedHipMotion.FireKickPitchDegrees > 0.0f ||
		CachedHipMotion.FireKickBackwardCm > 0.0f || CachedHipMotion.FireKickUpCm > 0.0f;
	CurrentHipLookSway = FRotator::ZeroRotator;
	HipBobPhase = 0.0f;
	HipFireKickAlpha = 0.0f;
	if (IsLocallyControlled())
	{
		PreviousControlRotation = GetControlRotation();
	}

	// Rebuild modular cosmetic parts on the (skeletal) weapon mesh from the weapon's part list.
	RefreshWeaponPartComponents(Weapon);

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

void AFPSRCharacter::RefreshWeaponPartComponents(const UFPSRWeaponDataAsset* Weapon)
{
	// No weapon / non-skeletal: tear down and reset signature. Parts attach to the SKELETAL weapon mesh only —
	// static/melee/preview weapons carry no modular parts, and the pack part sockets live on SKEL_LPAMG_<W>.
	// ActiveWeaponMesh == WeaponMesh1P means a skeletal weapon is shown.
	if (!Weapon || !WeaponMesh1P || ActiveWeaponMesh != WeaponMesh1P)
	{
		RebuildPartsFromSelection(TArray<FFPSRWeaponPartAttachment>());
		LastWeaponPartSignature = 0;
		return;
	}

	static const TArray<TObjectPtr<UFPSRWeaponFragment>> EmptyFragments;
	UFPSRWeaponInstance* Inst = WeaponInventory ? WeaponInventory->GetCurrentInstance() : nullptr;
	const FFPSRWeaponStatBlock& Resolved = Inst ? Inst->GetResolvedStats() : Weapon->BaseStats;
	const TArray<TObjectPtr<UFPSRWeaponFragment>>& Frags = Inst ? Inst->GetActiveFragments() : EmptyFragments;
	TArray<FFPSRWeaponPartAttachment> Selected;
	FPSRWeaponPartSelector::SelectParts(*Weapon, Resolved, Frags, Selected);
	RebuildPartsFromSelection(Selected);
	LastWeaponPartSignature = FPSRWeaponPartSelector::ComputeSignature(Selected);
}

void AFPSRCharacter::RebuildPartsFromSelection(const TArray<FFPSRWeaponPartAttachment>& Selected)
{
	// Tear down previous parts (weapon swaps / slot evolutions are infrequent, so a full rebuild is simpler than diffing).
	for (UStaticMeshComponent* Part : WeaponPartComponents1P)
	{
		if (Part)
		{
			Part->DestroyComponent();
		}
	}
	WeaponPartComponents1P.Reset();

	// Reset the modular muzzle/aim source caches HERE (both equip + modifier-change paths share this invariant) so a
	// slot swap that drops the socket-carrying part can't leave a dangling pointer to a destroyed component.
	CachedMuzzleComponent = nullptr;
	CachedAimComponent = nullptr;
	CachedScopeDescriptor = FFPSRWeaponScopeDescriptor();

	// Parts attach to the skeletal weapon mesh only (guarded by the caller: ActiveWeaponMesh == WeaponMesh1P).
	if (!WeaponMesh1P)
	{
		return;
	}

	// Parallel to WeaponPartComponents1P (both appended together in the loop): each attached part's scope descriptor,
	// so the aim-resolution below can capture the ACTIVE sight's descriptor by index. (W-U2)
	TArray<FFPSRWeaponScopeDescriptor> AddedScopeDescriptors;

	for (const FFPSRWeaponPartAttachment& PartDef : Selected)
	{
		if (PartDef.Part.IsNull())
		{
			continue; // null entry — skip (null-safe)
		}
		UStaticMesh* PartMesh = PartDef.Part.LoadSynchronous();
		if (!PartMesh)
		{
			continue;
		}
		UStaticMeshComponent* PartComp = NewObject<UStaticMeshComponent>(this);
		PartComp->SetStaticMesh(PartMesh);
		PartComp->SetOnlyOwnerSee(true); // match the 1P weapon mesh visibility (owner + spectating view target)
		PartComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		PartComp->RegisterComponent();
		PartComp->AttachToComponent(WeaponMesh1P, FAttachmentTransformRules::KeepRelativeTransform, PartDef.Socket);
		PartComp->SetRelativeTransform(PartDef.Offset);
		WeaponPartComponents1P.Add(PartComp);
		AddedScopeDescriptors.Add(PartDef.Scope);
	}

	// Re-resolve modular muzzle source: the muzzle socket lives on a cosmetic part (barrel/forestock), so prefer the
	// part component that carries CachedMuzzleSocket — swapping that part then moves the muzzle. When no part provides
	// it, CachedMuzzleComponent stays null and the fire site falls back to the receiver (ActiveWeaponMesh). Convention-
	// based: the part whose mesh owns a socket named MuzzleSocket wins, so no extra DA field is needed.
	if (!CachedMuzzleSocket.IsNone())
	{
		for (UStaticMeshComponent* Part : WeaponPartComponents1P)
		{
			if (Part && Part->DoesSocketExist(CachedMuzzleSocket))
			{
				CachedMuzzleComponent = Part;
				break;
			}
		}
	}

	// Re-resolve modular aim source (same shape as the muzzle above): the AimSocket sits on the SIGHT part (iron sight /
	// optic), so prefer the part component that carries CachedAimSocket — swapping the sight then moves the ADS
	// reference. When no part provides it, CachedAimComponent stays null and UpdateAimDownSights falls back to the
	// receiver (WeaponMesh1P).
	if (!CachedAimSocket.IsNone())
	{
		for (int32 i = 0; i < WeaponPartComponents1P.Num(); ++i)
		{
			UStaticMeshComponent* Part = WeaponPartComponents1P[i];
			if (Part && Part->DoesSocketExist(CachedAimSocket))
			{
				CachedAimComponent = Part;
				// Capture the active sight's scope descriptor (W-U2) so the owner-local ADS visual path can drive the
				// full-screen scope. AddedScopeDescriptors is index-aligned with WeaponPartComponents1P.
				if (AddedScopeDescriptors.IsValidIndex(i))
				{
					CachedScopeDescriptor = AddedScopeDescriptors[i];
				}
				break;
			}
		}
	}
}

void AFPSRCharacter::NotifyEquippedWeaponModifiersChanged(const UFPSRWeaponInstance* ChangedInstance)
{
	// Parts are owner-local cosmetics — never rebuilt on a dedicated server (mirrors HandleReloadStateChanged).
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	// Only the currently-equipped weapon's parts are shown; a non-equipped instance's change is irrelevant until swap.
	if (!WeaponInventory || ChangedInstance != WeaponInventory->GetCurrentInstance())
	{
		return;
	}
	if (bWeaponPartsRebuildPending)
	{
		return; // coalesce a burst into one next-tick rebuild
	}
	bWeaponPartsRebuildPending = true;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimerForNextTick(this, &AFPSRCharacter::ProcessPendingWeaponPartsRebuild);
	}
}

void AFPSRCharacter::ProcessPendingWeaponPartsRebuild()
{
	bWeaponPartsRebuildPending = false; // clear first (Process may early-return)
	const UFPSRWeaponDataAsset* Weapon = WeaponInventory ? WeaponInventory->GetCurrentWeapon() : nullptr;
	if (!Weapon || !WeaponMesh1P || ActiveWeaponMesh != WeaponMesh1P)
	{
		return;
	}
	static const TArray<TObjectPtr<UFPSRWeaponFragment>> EmptyFragments;
	UFPSRWeaponInstance* Inst = WeaponInventory->GetCurrentInstance();
	const FFPSRWeaponStatBlock& Resolved = Inst ? Inst->GetResolvedStats() : Weapon->BaseStats;
	const TArray<TObjectPtr<UFPSRWeaponFragment>>& Frags = Inst ? Inst->GetActiveFragments() : EmptyFragments;
	TArray<FFPSRWeaponPartAttachment> Selected;
	FPSRWeaponPartSelector::SelectParts(*Weapon, Resolved, Frags, Selected);
	const uint32 NewSig = FPSRWeaponPartSelector::ComputeSignature(Selected);
	if (NewSig == LastWeaponPartSignature)
	{
		return; // no visual change → no churn
	}
	RebuildPartsFromSelection(Selected);
	LastWeaponPartSignature = NewSig;
}

void AFPSRCharacter::HandleReloadStateChanged(bool bIsReloading)
{
	// No local rendering on a dedicated server — reload cosmetics are a no-op there (SetReloading calls this on the
	// authority to cover a listen-server host, which DOES render). Only the reload-START edge plays a montage; it
	// ends naturally at its rate-scaled length. Skip during the level-up freeze (§2-2) — reloads don't start while
	// frozen, so we never kick off a cosmetic mid-freeze.
	if (GetNetMode() == NM_DedicatedServer || !bIsReloading || IsRunFrozen())
	{
		return;
	}

	const UFPSRWeaponDataAsset* Weapon = WeaponInventory ? WeaponInventory->GetCurrentWeapon() : nullptr;
	if (!Weapon)
	{
		return;
	}

	// Scale the montage so its play length matches the resolved ReloadTime (the anim must not outlast the reload).
	float ReloadTime = Weapon->BaseStats.ReloadTime;
	if (UFPSRWeaponInstance* Instance = WeaponInventory->GetCurrentInstance())
	{
		ReloadTime = Instance->GetResolvedStats().ReloadTime;
	}

	auto PlayScaledReload = [ReloadTime](UAnimInstance* AnimInst, UAnimMontage* Montage)
	{
		if (!AnimInst || !Montage)
		{
			return;
		}
		const float MontageLen = Montage->GetPlayLength();
		const float Rate = (ReloadTime > KINDA_SMALL_NUMBER && MontageLen > KINDA_SMALL_NUMBER)
			? (MontageLen / ReloadTime) : 1.0f;
		AnimInst->Montage_Play(Montage, Rate);
	};

	if (IsLocallyControlled())
	{
		// Owner client: 1P arms reload montage (cached on equip).
		if (FirstPersonArms)
		{
			PlayScaledReload(FirstPersonArms->GetAnimInstance(), CachedReloadMontage);
		}
		// Owner client: WEAPON-mesh reload montage (magazine/bolt) synced to the same reload (rate-scaled identically).
		if (WeaponMesh1P)
		{
			PlayScaledReload(WeaponMesh1P->GetAnimInstance(), CachedWeaponReloadMontage);
		}
	}
	else if (USkeletalMeshComponent* BodyMesh = GetMesh())
	{
		// Remote observers (U19): 3P body reload montage, loaded on demand from the weapon DA's 3P field. This is the
		// event-driven counterpart to the 3P fire montage — no per-frame AnimBP polling of bReloading is needed.
		UAnimMontage* ReloadM3P = Weapon->ReloadMontage3P.IsNull() ? nullptr : Weapon->ReloadMontage3P.LoadSynchronous();
		PlayScaledReload(BodyMesh->GetAnimInstance(), ReloadM3P);
	}
}

void AFPSRCharacter::PlayWeaponFireCosmetics()
{
	if (!IsLocallyControlled())
	{
		return;
	}

	// While aiming (ADS) the ARM fire montage swings the whole arm and fights the procedural ADS sight-centering
	// (UpdateAimDownSights re-solves the socket each frame), reading as sight shake — so it's suppressed while aiming.
	// The WEAPON bolt montage, however, animates the bolt bone (not the sight) so it KEEPS cycling in ADS as fire
	// feedback (the bolt-reciprocation read), plus the ADS fire kick below — the shot no longer reads as dead-still in ADS.
	// Hip fire and non-ADS weapons keep both montages. Remote/3P fire montages (MulticastFireCosmetics) are separate.
	const bool bAimingADS = bCachedHasADS && WeaponFire && WeaponFire->IsAiming();
	const bool bSuppressArmFireMontage = bCachedSuppressFireMontagesWhileADS && bAimingADS;
	const bool bSuppressWeaponBolt = bCachedSuppressWeaponBoltWhileADS && bAimingADS;

	// Fire montage on the arms.
	if (CachedFireMontage && FirstPersonArms && !bSuppressArmFireMontage)
	{
		if (UAnimInstance* AnimInst = FirstPersonArms->GetAnimInstance())
		{
			AnimInst->Montage_Play(CachedFireMontage);
		}
	}

	// Bolt/action montage on the WEAPON mesh (its own skeleton, SKEL_LPAMG_<W>), synced with the arm recoil above.
	if (CachedWeaponFireMontage && WeaponMesh1P && !bSuppressWeaponBolt)
	{
		if (UAnimInstance* WeaponAnimInst = WeaponMesh1P->GetAnimInstance())
		{
			WeaponAnimInst->Montage_Play(CachedWeaponFireMontage);
		}
	}

	// ADS fire kick: bump the decaying kick angle on each aimed shot (applied about the sight pivot in
	// UpdateAimDownSights so the gun snaps but the reticle holds). Refresh (max, not accumulate) so sustained fire
	// holds a steady kick without running away; the per-frame FInterpTo settles it back between shots.
	if (bAimingADS && CachedADSFireKickDegrees > 0.0f)
	{
		ADSFireKickPitch = FMath::Max(ADSFireKickPitch, CachedADSFireKickDegrees);
	}

	// Hip procedural fire kick: refresh to full each shot (Max, not accumulate) so sustained fire holds a steady kick;
	// decayed each frame in UpdateAimDownSights and faded by (1-ADSalpha) there, so it reads only at hip. Owner-local.
	if (bCachedHasHipMotion &&
		(CachedHipMotion.FireKickPitchDegrees > 0.0f || CachedHipMotion.FireKickBackwardCm > 0.0f || CachedHipMotion.FireKickUpCm > 0.0f))
	{
		HipFireKickAlpha = FMath::Max(HipFireKickAlpha, 1.0f);
	}

	// Fire sound + muzzle flash attach to the ACTIVE weapon mesh (skeletal firearm or static preview) so they track
	// whichever mesh the equipped weapon shows. CachedMuzzleSocket is a socket on that mesh (NAME_None = mesh origin).
	if (ActiveWeaponMesh)
	{
		if (CachedFireSound)
		{
			UGameplayStatics::SpawnSoundAttached(CachedFireSound, ActiveWeaponMesh);
		}
		// Muzzle flash scale while aiming: in ADS the muzzle sits just behind the sight, so a full-size flash washes over
		// the reticle. Shrink it (CachedADSMuzzleFlashScale) so the shot still reads without obscuring the aim; hip fire
		// is always full size. Scale 0 = no flash while aiming. (Spectators use MulticastFireCosmetics at full size —
		// they don't have the shooter's owner-local aim state.)
		const float MuzzleScale = bAimingADS ? CachedADSMuzzleFlashScale : 1.0f;
		if (CachedMuzzleFlash && MuzzleScale > KINDA_SMALL_NUMBER)
		{
			// Muzzle flash attaches to the modular part that owns the muzzle socket (barrel/forestock) when present,
			// else the receiver. CachedMuzzleComponent is resolved per-equip in RefreshWeaponPartComponents.
			UMeshComponent* MuzzleComp = CachedMuzzleComponent ? CachedMuzzleComponent : ActiveWeaponMesh;
			UGameplayStatics::SpawnEmitterAttached(CachedMuzzleFlash, MuzzleComp, CachedMuzzleSocket,
				FVector::ZeroVector, CachedMuzzleFlashRotationOffset, FVector(MuzzleScale));
		}
	}
}

void AFPSRCharacter::UpdateAimDownSights(float DeltaTime)
{
	// Owner-local only — the 1P arms are OnlyOwnerSee and the offset only affects the local view (no-op on a dedicated
	// server or remote pawns). Called every frame from UFPSRWeaponFireComponent::TickComponent (the character's own
	// Tick is debug-only / disabled in shipping).
	if (!IsLocallyControlled() || !FirstPersonArms || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	// The AimSocket may live on a SIGHT part (iron sight / optic) or the receiver — CachedAimComponent resolves to the
	// part that owns it (RefreshWeaponPartComponents), else fall back to the weapon receiver mesh.
	UMeshComponent* AimComp = CachedAimComponent;
	if (!AimComp)
	{
		AimComp = WeaponMesh1P;
	}

	// Relax the ADS glue during a reload: the reload (mag-swap) montage swings the arms a lot, and re-solving each frame to
	// keep the sight glued to centre would make the arms counter-swing hard — a violent shake at reload timing. Treat a
	// reload as not-aiming so the alpha smoothly lowers the gun toward hip for the reload and raises back afterwards; the
	// reload animation then plays cleanly with nothing fighting it. IsReloading() reflects the replicated per-instance
	// reload state, set on the same OnRep as the reload montage, so the relax is in sync with the montage.
	const bool bReloading = WeaponInventory && WeaponInventory->IsReloading();
	const bool bAiming = bCachedHasADS && !CachedAimSocket.IsNone() && WeaponFire && WeaponFire->IsAiming() && !bReloading
		&& AimComp && AimComp->DoesSocketExist(CachedAimSocket);

	// When aiming, recompute the EXACT arms transform (relative to the camera) that lands the weapon's AimSocket on the
	// camera's forward centre-line at ADSSightDistance THIS frame, and store it. The fixed capsule camera doesn't follow a
	// head bone, so the SIGHT is brought to the camera, not the camera to the sight.
	if (bAiming)
	{
		// AimSocket transform RELATIVE TO THE ARMS. The weapon rides the arms rigidly, so this is invariant to the arms'
		// own transform (it cancels out below) — no feedback loop — yet recomputed each frame so it tracks weapon/arm
		// animation (idle sway, walk bob, bolt montage) that moves the socket.
		const FTransform ArmsWorld = FirstPersonArms->GetComponentTransform();
		// Pre-compose the designer's aim rotation offset in the socket's LOCAL frame (identical to authoring that rotation
		// onto the socket) so full-frame alignment points the gun forward for packs whose socket axes are off-forward
		// (this pack's weapon-forward is +Y → ADSAimRotationOffset Yaw 90). Zero offset = use the socket frame as-authored.
		const FTransform SocketRelArms = FTransform(CachedADSAimRotationOffset)
			* AimComp->GetSocketTransform(CachedAimSocket, RTS_World).GetRelativeTransform(ArmsWorld);

		if (bCachedADSAlignRotation)
		{
			// Full-frame alignment: level the socket frame to the camera (identity rotation) AND place it on the centre-
			// line, so the authored hip cant is removed and the sight reads straight. Solve arms-rel-camera from the
			// desired socket-rel-camera: SocketRelArms * ArmsRelCam == Desired (UE composes child * parent), hence
			// ArmsRelCam = SocketRelArms^-1 * Desired.
			const FTransform DesiredSocketRelCam(FRotator::ZeroRotator, FVector(CachedADSSightDistance, 0.0f, 0.0f));
			const FTransform TargetArmsRelCam = SocketRelArms.Inverse() * DesiredSocketRelCam;
			ADSAimLoc = TargetArmsRelCam.GetLocation();
			ADSAimRot = TargetArmsRelCam.Rotator();
		}
		else
		{
			// Translation-only ADS (escape hatch): keep the authored arms rotation and only slide so the socket sits on
			// the centre-line. Socket position under the hip pose is BaseArmsRelRot.Rotate(socket-loc-rel-arms) + arms-loc,
			// so solve arms-loc for a socket at (D, 0, 0).
			ADSAimRot = BaseArmsRelRot;
			ADSAimLoc = FVector(CachedADSSightDistance, 0.0f, 0.0f) - BaseArmsRelRot.RotateVector(SocketRelArms.GetLocation());
		}
	}

	// Interpolate the ADS blend alpha (0 = hip, 1 = fully aimed) at the weapon's ADS speed (matches the FOV interp), then
	// BLEND the hip base and the stored aim pose by it — we don't chase the moving aim target with a lagging transform
	// interp. At full ADS (alpha≈1) the arms take the exact aim transform every frame, so the socket sits precisely on the
	// centre-line and animated sway/bob is cancelled AT THE SIGHT (steady reticle). While disengaging, the stored aim pose
	// is held frozen and alpha decays, so the return to hip stays smooth. Rotation blends on the short arc via slerp.
	const float InterpSpeed = FMath::Max(0.01f, CachedADSInterpSpeed);
	CurrentADSAlpha = FMath::FInterpTo(CurrentADSAlpha, bAiming ? 1.0f : 0.0f, DeltaTime, InterpSpeed);

	// ADS position glue (stabilization knob): ADSAimLoc cancels the animated socket motion each frame, so at bob scale 0
	// the sight is pinned exactly on the centre-line (steady reticle). Lerping the aim location back toward the hip base
	// removes that fraction of the anti-bob correction, letting an equal fraction of the animated positional bob survive
	// (a livelier ADS) while rotation below stays fully glued. Scale 0 (default) == the original exact-glue behaviour.
	const FVector GluedAimLoc = FMath::Lerp(ADSAimLoc, BaseArmsRelLoc, CachedADSPositionBobScale);
	FVector NewLoc = FMath::Lerp(BaseArmsRelLoc, GluedAimLoc, CurrentADSAlpha);
	FQuat NewRot = FQuat::Slerp(BaseArmsRelRot.Quaternion(), ADSAimRot.Quaternion(), CurrentADSAlpha);

	// ADS idle sway + fire kick: BOTH pivot the arms about the pinned sight (camera-space ≈ (ADSSightDistance, 0, 0)),
	// faded by the ADS alpha, so the gun body/muzzle moves while the sight stays on the centre-line (steady reticle).
	//  - Sway: a gentle handheld "breathing" wander (yaw = L-R, subtle pitch) — two out-of-phase sines per axis so it
	//    reads organic rather than a metronome. MOVEMENT-GATED (below): scaled by a smoothed 0..1 speed factor so a
	//    planted/standing-still aim stays dead steady and the sway only lives while walking. Cosmetic + owner-local.
	//  - Kick: the per-shot recoil snap (+pitch = muzzle up) set on each aimed shot (PlayWeaponFireCosmetics), settled
	//    back toward zero each frame. Both are scaled by the ADS alpha so they appear only in ADS and fade on release.
	// Smoothed movement factor: 0 when still → 1 at BaseWalkSpeed, eased (FInterpTo) so the sway winds down after you
	// stop rather than cutting off. GetVelocity() is the pawn's movement velocity; Size2D() ignores jump/gravity.
	const float SwayRefSpeed = FMath::Max(1.0f, BaseWalkSpeed);
	const float SwayMoveTarget = FMath::Clamp(GetVelocity().Size2D() / SwayRefSpeed, 0.0f, 1.0f);
	ADSSwayMoveAlpha = FMath::FInterpTo(ADSSwayMoveAlpha, SwayMoveTarget, DeltaTime, 8.0f);

	FRotator ExtraRot(ADSFireKickPitch, 0.0f, 0.0f);
	if (CurrentADSAlpha > KINDA_SMALL_NUMBER)
	{
		if (ADSSwayMoveAlpha > KINDA_SMALL_NUMBER && (CachedADSSwayYawDegrees > 0.0f || CachedADSSwayPitchDegrees > 0.0f))
		{
			const float T = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
			const float W = CachedADSSwaySpeed;
			ExtraRot.Yaw   += ADSSwayMoveAlpha * CachedADSSwayYawDegrees   * (0.6f * FMath::Sin(T * W)            + 0.4f * FMath::Sin(T * W * 1.7f + 1.1f));
			ExtraRot.Pitch += ADSSwayMoveAlpha * CachedADSSwayPitchDegrees * (0.6f * FMath::Sin(T * W * 0.8f + 0.5f) + 0.4f * FMath::Sin(T * W * 1.3f + 2.3f));
		}
		if (!ExtraRot.IsNearlyZero())
		{
			const FVector Pivot(CachedADSSightDistance, 0.0f, 0.0f);
			const FQuat ExtraQuat = (ExtraRot * CurrentADSAlpha).Quaternion();
			NewLoc = Pivot + ExtraQuat.RotateVector(NewLoc - Pivot);
			NewRot = ExtraQuat * NewRot;
		}
	}
	ADSFireKickPitch = FMath::FInterpTo(ADSFireKickPitch, 0.0f, DeltaTime, CachedADSFireKickRecoveryRate);

	// --- Hip-space procedural weapon motion (owner-local cosmetic) ---
	// Faded OUT as ADS engages (weight = 1-alpha), composed additively into the SAME single write below (no extra
	// SetRelative* call). State is advanced from INVARIANT sources only (control-rotation delta, velocity, accumulated
	// phase, decaying kick) — never from the arms' own transform — so there is no feedback loop. Translation is added in
	// camera space (NewLoc is arms-relative-to-camera: +X forward, +Y right, +Z up → screen-space bob/kick); sway/kick
	// rotation is composed about the arms origin. Fixed order: base → ADS blend (above) → hip additive → write once.
	if (bCachedHasHipMotion)
	{
		// Cosmetic interp uses a clamped dt so a frame spike (or low FPS) can't snap FInterpTo to target (dt*Speed>=1).
		const float HipDt = FMath::Min(DeltaTime, 1.0f / 30.0f);

		// Look-sway: the weapon lags the aim. Target = opposite the per-frame control-rotation delta, clamped; eased so it
		// leans into a turn and recenters when the aim stops. Delta is clamped to reject teleport/respawn jumps.
		const FRotator ControlRot = GetControlRotation();
		FRotator AimDelta = (ControlRot - PreviousControlRotation).GetNormalized();
		PreviousControlRotation = ControlRot;
		const float MaxDelta = 25.0f;
		const float DeltaYaw = FMath::Clamp(AimDelta.Yaw, -MaxDelta, MaxDelta);
		const float DeltaPitch = FMath::Clamp(AimDelta.Pitch, -MaxDelta, MaxDelta);
		FRotator SwayTarget(
			FMath::Clamp(-DeltaPitch * CachedHipMotion.LookSwayAmount, -CachedHipMotion.LookSwayMaxDegrees, CachedHipMotion.LookSwayMaxDegrees),
			FMath::Clamp(-DeltaYaw * CachedHipMotion.LookSwayAmount, -CachedHipMotion.LookSwayMaxDegrees, CachedHipMotion.LookSwayMaxDegrees),
			0.0f);
		CurrentHipLookSway = FMath::RInterpTo(CurrentHipLookSway, SwayTarget, HipDt, CachedHipMotion.LookSwayReturnSpeed);

		// Walk/run bob: velocity-gated figure-8. Reuse the smoothed 0..1 movement factor computed above (ADSSwayMoveAlpha).
		// Phase accumulates with dt (frame-rate independent); vertical runs at 2x the horizontal frequency.
		HipBobPhase += HipDt * CachedHipMotion.WalkBobFrequency * ADSSwayMoveAlpha;
		HipBobPhase = FMath::Fmod(HipBobPhase, 1024.0f);
		const float BobH = FMath::Sin(HipBobPhase * 2.0f * PI) * CachedHipMotion.WalkBobHorizontal * ADSSwayMoveAlpha;
		const float BobV = FMath::Sin(HipBobPhase * 4.0f * PI) * CachedHipMotion.WalkBobVertical * ADSSwayMoveAlpha;

		// Fire kick: decay the per-shot alpha (bumped to 1 in PlayWeaponFireCosmetics), then scale the kick offset/pitch.
		HipFireKickAlpha = FMath::FInterpTo(HipFireKickAlpha, 0.0f, HipDt, CachedHipMotion.FireKickRecoverySpeed);
		const float KickPitch = HipFireKickAlpha * CachedHipMotion.FireKickPitchDegrees;
		const FVector KickOffset(-HipFireKickAlpha * CachedHipMotion.FireKickBackwardCm, 0.0f, HipFireKickAlpha * CachedHipMotion.FireKickUpCm);

		// Compose, weighted by (1-alpha) so it vanishes as you aim (ADS sight-glue owns the pose there).
		const float HipWeight = 1.0f - CurrentADSAlpha;
		if (HipWeight > KINDA_SMALL_NUMBER)
		{
			NewLoc += HipWeight * (FVector(0.0f, BobH, BobV) + KickOffset);
			const FRotator HipRot(CurrentHipLookSway.Pitch + KickPitch, CurrentHipLookSway.Yaw, 0.0f);
			NewRot = (HipRot * HipWeight).Quaternion() * NewRot;
		}
	}

	FirstPersonArms->SetRelativeLocationAndRotation(NewLoc, NewRot);
}

void AFPSRCharacter::MulticastFireCosmetics_Implementation()
{
	// The owning client already played its predicted fire cosmetics locally (PlayWeaponFireCosmetics), so skip here
	// to avoid double-play. On the listen-server host the host pawn is locally controlled and also skips (it heard
	// its own shot). Only REMOTE observers fall through (teammate fire SFX B4 + 1P muzzle/montage for a spectator).
	if (IsLocallyControlled())
	{
		return;
	}

	// Resolve the equipped weapon from the REPLICATED inventory — the owner-only Cached* cosmetics aren't relied on
	// here. GetCurrentWeapon() is valid on every client (the inventory's Slots/CurrentSlotIndex replicate).
	const UFPSRWeaponDataAsset* Weapon = WeaponInventory ? WeaponInventory->GetCurrentWeapon() : nullptr;
	if (!Weapon)
	{
		return;
	}

	const APlayerController* LocalPC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;

	// First-person fire visuals (muzzle flash + arms fire montage) for a DOWNED teammate who is SPECTATING this pawn
	// (§2-13). OnlyOwnerSee 1P meshes render for the view target, so the spectator already sees this pawn's arms+gun;
	// without this they'd see the gun but no muzzle/recoil. Gate strictly on "the local viewer's view target IS this
	// pawn": the muzzle particle is NOT OnlyOwnerSee, so an UNgated spawn would float a muzzle flash at the 1P weapon
	// position inside a (non-spectating) teammate's head. No distance cull here — the spectator's camera is on this pawn.
	if (LocalPC && LocalPC->GetViewTarget() == this)
	{
		if (ActiveWeaponMesh && !Weapon->MuzzleFlash.IsNull())
		{
			if (UParticleSystem* Muzzle = Weapon->MuzzleFlash.LoadSynchronous())
			{
				UMeshComponent* MuzzleComp = CachedMuzzleComponent ? CachedMuzzleComponent : ActiveWeaponMesh;
				UGameplayStatics::SpawnEmitterAttached(Muzzle, MuzzleComp, Weapon->MuzzleSocket,
					FVector::ZeroVector, Weapon->MuzzleFlashRotationOffset);
			}
		}
		if (FirstPersonArms && !Weapon->FireMontage.IsNull())
		{
			if (UAnimMontage* FireMontage = Weapon->FireMontage.LoadSynchronous())
			{
				if (UAnimInstance* AnimInst = FirstPersonArms->GetAnimInstance())
				{
					AnimInst->Montage_Play(FireMontage);
				}
			}
		}
		// Bolt/action on the WEAPON mesh for the spectator too (they see the ally's 1P weapon via OnlyOwnerSee).
		if (WeaponMesh1P && !Weapon->WeaponFireMontage.IsNull())
		{
			if (UAnimMontage* WeaponFireM = Weapon->WeaponFireMontage.LoadSynchronous())
			{
				if (UAnimInstance* WeaponAnimInst = WeaponMesh1P->GetAnimInstance())
				{
					WeaponAnimInst->Montage_Play(WeaponFireM);
				}
			}
		}
	}

	// Third-person body fire montage (U19) for REMOTE observers — plays on every non-owner client (this multicast
	// already early-returned for the locally-controlled owner above). The 3P body is SetOwnerNoSee, so only remotes
	// see it. Placed OUTSIDE the spectator view-target gate above so BOTH a spectating downed teammate AND normal
	// remotes see the shooter's 3P recoil. Null FireMontage3P = no 3P reaction (null-safe, no gameplay effect).
	if (USkeletalMeshComponent* BodyMesh = GetMesh())
	{
		if (!Weapon->FireMontage3P.IsNull())
		{
			if (UAnimMontage* FireM3P = Weapon->FireMontage3P.LoadSynchronous())
			{
				if (UAnimInstance* BodyAnimInst = BodyMesh->GetAnimInstance())
				{
					BodyAnimInst->Montage_Play(FireM3P);
				}
			}
		}
	}

	// Spatialized fire SFX so REMOTE teammates hear each other's fire (B4). Coarse distance cull against the local
	// viewer's pawn so a far-off shot doesn't spawn an audio component (the sound's own attenuation still shapes
	// falloff for audible shots). Cheap belt at the <=4-player scale.
	if (Weapon->FireSound.IsNull())
	{
		return;
	}
	constexpr float FireSoundCullDistance = 8000.0f; // cm (~80 m)
	if (LocalPC)
	{
		// Cull against the local viewer's VIEW TARGET (where the audio listener is), not its pawn: a DBNO spectator's
		// pawn is its downed body (possibly far away), while the listener rides the spectated ally — using GetPawn()
		// would wrongly cull a shot the spectator is right next to. For a normal player the view target IS their pawn.
		if (const AActor* LocalViewActor = LocalPC->GetViewTarget())
		{
			if (FVector::DistSquared(GetActorLocation(), LocalViewActor->GetActorLocation()) > FMath::Square(FireSoundCullDistance))
			{
				return;
			}
		}
	}

	// Spatialized one-shot at the shooter so the sound comes from the teammate's position (the muzzle socket isn't
	// available on remote pawns; actor location is accurate enough for positional fire audio).
	if (USoundBase* FireSound = Weapon->FireSound.LoadSynchronous())
	{
		UGameplayStatics::PlaySoundAtLocation(this, FireSound, GetActorLocation());
	}
}

void AFPSRCharacter::CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult)
{
	// A DBNO teammate spectates a living ally via SetViewTarget (§2-13). On the spectator's machine the ally pawn is
	// NOT locally controlled, so UCameraComponent::GetCameraView skips bUsePawnControlRotation (it only follows the
	// LOCAL controller) — the spectator's view (and the attached 1P arms/weapon) would track yaw (replicated actor
	// rotation) but not pitch. GetBaseAimRotation() gives the full aim: yaw from the actor rotation, pitch from the
	// replicated RemoteViewPitch16 (UE5.7). Drive the camera component with it (CalcCamera runs each frame only while
	// this pawn is the view target) so the attached 1P arms + weapon mesh pitch with the aim, then return that view.
	// The locally-controlled owner keeps the default camera-component path (bUsePawnControlRotation handles pitch).
	if (FirstPersonCamera && !IsLocallyControlled())
	{
		const FRotator AimRotation = GetBaseAimRotation();
		FirstPersonCamera->SetWorldRotation(AimRotation);
		OutResult.Location = FirstPersonCamera->GetComponentLocation();
		OutResult.Rotation = AimRotation;
		OutResult.FOV = FirstPersonCamera->FieldOfView;
		return;
	}

	Super::CalcCamera(DeltaTime, OutResult);
}

float AFPSRCharacter::GetReviveTargetProgress() const
{
	// Local reviver HUD (§2-13): an ALIVE player standing within a DBNO ally's revive radius is reviving them — surface
	// that ally's (replicated) ReviveProgress so the reviver's HUD can show a gauge + prompt. Returns the highest
	// progress among in-range downed allies, or -1 when this player isn't reviving anyone. Pure client-side read of
	// already-replicated data (LifeState + ReviveProgress); no new replication.
	const AFPSRPlayerState* MyPS = GetPlayerState<AFPSRPlayerState>();
	if (!MyPS || !MyPS->IsAlive())
	{
		return -1.0f; // only an alive player can be reviving someone
	}
	const UWorld* World = GetWorld();
	const AFPSRGameState* GS = World ? World->GetGameState<AFPSRGameState>() : nullptr;
	if (!GS)
	{
		return -1.0f;
	}
	const FVector MyLoc = GetActorLocation();
	float BestProgress = -1.0f;
	for (APlayerState* PS : GS->PlayerArray)
	{
		const AFPSRPlayerState* AllyPS = Cast<AFPSRPlayerState>(PS);
		if (!AllyPS || AllyPS == MyPS || !AllyPS->IsDBNO())
		{
			continue;
		}
		const APawn* AllyPawn = AllyPS->GetPawn();
		const UFPSRReviveComponent* AllyRevive = AllyPawn ? AllyPawn->FindComponentByClass<UFPSRReviveComponent>() : nullptr;
		if (!AllyRevive)
		{
			continue;
		}
		FVector ToAlly = AllyPawn->GetActorLocation() - MyLoc;
		ToAlly.Z = 0.0f;
		if (ToAlly.SizeSquared() <= FMath::Square(AllyRevive->GetReviveRadius()))
		{
			BestProgress = FMath::Max(BestProgress, AllyRevive->GetReviveProgress());
		}
	}
	return BestProgress;
}

UAbilitySystemComponent* AFPSRCharacter::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}
