// Copyright Epic Games, Inc. All Rights Reserved.

#include "Hero/FPSRCharacter.h"
#include "Blueprint/UserWidget.h"
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
#include "Weapon/FPSRWeaponAnimInstance.h"
#include "Weapon/FPSRWeaponPartSelector.h"
#include "Anim/FPSRGunMotionCurves.h"
#include "Hero/FPSRPlayerFeedbackComponent.h"
#include "Hero/FPSRBlindspotAudioComponent.h"
#include "Hero/FPSRCharacterMovementComponent.h"
#include "Hero/FPSRReviveComponent.h"
#include "Director/FPSRDirectorSensorSubsystem.h"
#include "FPSRCollisionChannels.h"

#include "Camera/CameraComponent.h"
#include "Debug/DebugDrawService.h"
#include "Engine/Canvas.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Engine/StaticMeshSocket.h"
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

AFPSRCharacter::AFPSRCharacter(const FObjectInitializer& ObjectInitializer)
	// ADR 0001: locomotion state lives in one place. Installing the component here (rather than in a BP) means every
	// player character gets it, including any Blueprint subclass, without per-asset wiring that could be forgotten.
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UFPSRCharacterMovementComponent>(ACharacter::CharacterMovementComponentName))
{
	// Needed in every build now: the stance camera blend has to keep moving the eye height after the stance itself has
	// flipped. Four player pawns, so this is unrelated to the per-actor budget the 200-300 enemies are held to.
	PrimaryActorTick.bCanEverTick = true;

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
	}
	// Hand the authored baseline to the movement component instead of writing MaxWalkSpeed here: that value is
	// composed from four layers (authored / equipped weapon / card multiplier / downed) and the component owns the
	// composition. Writing it directly from any one of them erases the others (ADR 0001, walk-speed single writer).
	if (UFPSRCharacterMovementComponent* FPSRMovement = GetFPSRMovement())
	{
		FPSRMovement->SetAuthoredBaseWalkSpeed(BaseWalkSpeed);
	}

	FirstPersonCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
	FirstPersonCamera->SetupAttachment(GetCapsuleComponent());
	// Drive the eye height off the inherited BaseEyeHeight rather than a literal, so the crouch overrides
	// (OnStartCrouch/OnEndCrouch) and this initial placement can't drift apart. The forward correction is included so the
	// BP viewport preview shows roughly where the camera actually ends up; the authoritative placement is
	// UpdateStanceCamera every frame, which also applies the capsule clamp (and sees the BP's overridden values, which
	// are deserialized after this constructor has run).
	FirstPersonCamera->SetRelativeLocation(
		FirstPersonCameraOffset + FVector(0.0f, 0.0f, BaseEyeHeight));
	FirstPersonCamera->bUsePawnControlRotation = true;
	// Motion blur off on the player view: the camera-parented 1P weapon gets a large world-space velocity during camera
	// recoil / the ADS fire kick and would smear (ghost) even while screen-static, and a fast swarm reads better crisp.
	// This is the only camera, so it disables motion blur game-wide (a deliberate art choice for this genre).
	FirstPersonCamera->PostProcessSettings.bOverride_MotionBlurAmount = true;
	FirstPersonCamera->PostProcessSettings.MotionBlurAmount = 0.0f;

	// True First Person (ADR 0002): the owner sees the SAME body mesh everyone else does, so the arms component and the
	// owner-only / owner-hidden visibility split are both gone. What replaces the split is one bone — the head — hidden
	// only while we look through this pawn's eyes (UpdateFirstPersonBodyVisibility). Body visibility is left at the
	// engine default here on purpose: SetOwnerNoSee(true) would make the owner's own body invisible to themselves.

	// Attach the weapon meshes to the BODY's grip socket so the design-time preview (and runtime, when a weapon DA
	// leaves WeaponAttachSocket empty) sits in the hand. C++-created component sockets aren't editable in the BP, hence
	// WeaponAttachSocketName carries the default. Shadows stay ON: the weapon is a world object now, not a screen-space
	// prop, so it has to cast like the body that holds it.
	WeaponMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("WeaponMesh"));
	WeaponMesh->SetupAttachment(GetMesh(), WeaponAttachSocketName);
	WeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	WeaponMeshStatic = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WeaponMeshStatic"));
	WeaponMeshStatic->SetupAttachment(GetMesh(), WeaponAttachSocketName);
	WeaponMeshStatic->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// The separate 3P weapon mesh is gone (ADR 0002). It existed so remote observers could see a weapon the owner-only
	// 1P mesh hid from them; with one mesh serving both there is nothing left to mirror — and a survey of all 9 weapon
	// DataAssets found its mesh field had never been authored, so teammates' weapons were invisible the whole time.

	// The owner's arms. Attached to the CAMERA (ADR 0003): they no longer replay the body's pose, they run their own
	// graph on their own skeleton, so what they need from a parent is the VIEW — not the body's transform. Being a
	// camera child also means the arms inherit the view rotation the engine applies post-tick
	// (UCameraComponent::GetCameraView -> SetWorldRotation), which is why UpdateAimDownSights writes them in RELATIVE
	// space; see the note there. Owner-only: without this the arms render on top of the body for everyone else.
	// Mesh, anim class and rest transform are all authored on this component in the BP — nothing here overwrites them.
	// Hidden by default so a BP that authors no arms mesh never flashes them; RefreshFirstPersonRendering owns it after.
	FirstPersonArms = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("FirstPersonArms"));
	FirstPersonArms->SetupAttachment(FirstPersonCamera);
	FirstPersonArms->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FirstPersonArms->SetOnlyOwnerSee(true);
	FirstPersonArms->SetVisibility(false);

	// The body has to EVALUATE its pose every frame, not merely tick its graph. RefreshFirstPersonRendering tags the body
	// WorldSpaceRepresentation, which takes it out of the owner's main render pass, and ShouldUpdateTransform() gates
	// RefreshBoneTransforms on exactly that ("was I recently rendered"). ACharacter's constructor default is
	// AlwaysTickPose, so the graph keeps UPDATING while it never EVALUATES: Speed and every other input stay live while
	// the pose stays frozen on the last frame that happened to be drawn. The arms no longer share that fate (they have
	// their own graph, ADR 0003) but the BODY still owes the owner a full-body SHADOW, and a shadow needs live bones —
	// so this stays. Unconditional: this is the player, of whom there are at most 4. The per-actor budget this project
	// is built around is about enemies, and they keep their own setting.
	GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;

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
static TAutoConsoleVariable<int32> CVarMovementDebug(
	TEXT("FPSR.Movement.Debug"),
	1,
	TEXT("Show the top-right movement readout (planar speed + locomotion state) for the local player. 1 = on (default), 0 = off."),
	ECVF_Cheat);

void AFPSRCharacter::DrawMovementDebug(UCanvas* Canvas, APlayerController* PC)
{
	if (!Canvas || !IsLocallyControlled() || CVarMovementDebug.GetValueOnGameThread() == 0)
	{
		return;
	}
	const UFPSRCharacterMovementComponent* FPSRMovement = GetFPSRMovement();
	if (!FPSRMovement)
	{
		return;
	}

	const FString SpeedText = FString::Printf(TEXT("SPEED %.0f"), FPSRMovement->GetPlanarSpeed());
	const FString StateText = FString::Printf(TEXT("STATE %s"), *FPSRMovement->GetLocomotionStateName());

	UFont* Font = GEngine ? GEngine->GetMediumFont() : nullptr;
	if (!Font)
	{
		return;
	}

	// Right-align: measure each line and offset from the canvas width so the text stays anchored to the top-right
	// corner at any resolution.
	const float RightMargin = 24.0f;
	float LineWidth = 0.0f, LineHeight = 0.0f;

	Canvas->StrLen(Font, SpeedText, LineWidth, LineHeight);
	Canvas->SetDrawColor(FColor::Yellow);
	Canvas->DrawText(Font, SpeedText, Canvas->SizeX - LineWidth - RightMargin, 24.0f);

	Canvas->StrLen(Font, StateText, LineWidth, LineHeight);
	Canvas->SetDrawColor(FPSRMovement->IsOnWall() ? FColor::Cyan : (FPSRMovement->IsSliding() ? FColor::Orange : FColor::White));
	Canvas->DrawText(Font, StateText, Canvas->SizeX - LineWidth - RightMargin, 24.0f + LineHeight + 2.0f);

	// Only when it fires. A clamp that quietly eats an authored FirstPersonCameraOffset reads as "the value does nothing"
	// from the BP side; saying how much was cut turns that into a number the designer can act on.
	if (LastCameraClampAmount > KINDA_SMALL_NUMBER)
	{
		const FString ClampText = FString::Printf(TEXT("CAM CLAMPED %.1fcm"), LastCameraClampAmount);
		Canvas->StrLen(Font, ClampText, LineWidth, LineHeight);
		Canvas->SetDrawColor(FColor::Red);
		Canvas->DrawText(Font, ClampText, Canvas->SizeX - LineWidth - RightMargin, 24.0f + (LineHeight + 2.0f) * 2.0f);
	}
}

#endif

void AFPSRCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	UpdateStanceCamera();
	UpdateCameraFieldOfView(DeltaSeconds);
	UpdateFirstPersonBodyVisibility();

	// State-pose-on-weapon refactor (§4): advance the slide/airborne/holster alphas and, on the gun-anchor attach
	// path, write the resulting delta onto the weapon HERE — after UpdateFirstPersonBodyVisibility above (so
	// bFirstPersonSplitActive/bWeaponAttachIsGunAnchor reflect this frame's split state), and before FirstPersonArms
	// ticks (BeginPlay wires FirstPersonArms->AddTickPrerequisiteActor(this), so this Tick always runs first). That
	// ordering is what lets the arms' hand-IK nodes (which read GetRightHandGripInGunFrame /
	// GetLeftHandGripInGunFrame / GetWeaponPartFrameInGunSpace during their own anim-graph evaluation) see THIS
	// frame's weapon placement instead of last frame's — see ApplyWeaponStatePose's own header comment.
	ApplyWeaponStatePose(DeltaSeconds);

#if ENABLE_DRAW_DEBUG
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
#endif
}

void AFPSRCharacter::BeginPlay()
{
	Super::BeginPlay();

	// Tick after the movement component so CachedCameraWorldZ is the eye position movement actually left behind this
	// frame. Without it the next stance change would hold the view to a position one movement step stale, and the
	// blend would start from slightly the wrong place.
	AddTickPrerequisiteComponent(GetCharacterMovement());
	CachedCameraWorldZ = FirstPersonCamera ? FirstPersonCamera->GetComponentLocation().Z : 0.0f;

	// The authored FOV becomes the base every camera offset is expressed against. Taken here, before the first
	// UpdateCameraFieldOfView writes anything, so it is the BP's value and not something this class already moved.
	if (FirstPersonCamera)
	{
		BaseFieldOfView = FirstPersonCamera->FieldOfView;
	}

	// Same class of reason, one layer down: UpdateAimDownSights (driven from the weapon-fire tick) solves against the
	// grip socket of whichever mesh currently holds the weapon, so it has to run after THAT mesh's pose exists this
	// frame. Reading a stale socket would make the gun trail the hand by a frame during fast movement. Both are
	// registered unconditionally rather than following the split: the prerequisite is a tick-graph edge, re-registering
	// it every time the player goes down and comes back would be churn for no gain, and the mesh that isn't holding the
	// weapon costs nothing but an ordering constraint that was already true. Note the pose may still be finalized by the
	// parallel-animation completion task at the END of the tick group; if PIE shows residual lag, measure here first.
	if (WeaponFire)
	{
		if (GetMesh())
		{
			WeaponFire->AddTickPrerequisiteComponent(GetMesh());
		}
		if (FirstPersonArms)
		{
			WeaponFire->AddTickPrerequisiteComponent(FirstPersonArms);
		}
	}

	// State-pose-on-weapon refactor (§4): close the tick-prerequisite chain so it reads CMC -> Character (this Tick,
	// which now runs ApplyWeaponStatePose) -> FirstPersonArms (anim graph, hand-IK reads the weapon placement
	// ApplyWeaponStatePose just wrote) -> WeaponFire (UpdateAimDownSights, which needs the arms pose already
	// finalized — see that function's comment). Without this edge, an actor's own Tick and its components' Tick have
	// no ordering relationship by default — AddTickPrerequisiteActor is the documented way to add one (engine:
	// UActorComponent::AddTickPrerequisiteActor, ActorComponent.cpp — makes FirstPersonArms's tick depend on
	// AFPSRCharacter's PrimaryActorTick). Symmetric with the WeaponFire->AddTickPrerequisiteComponent(FirstPersonArms)
	// edge just above; together the two calls form one unbroken chain rather than two independent ones.
	if (FirstPersonArms)
	{
		FirstPersonArms->AddTickPrerequisiteActor(this);
	}

	// Push the authored baseline AGAIN here, not only from the constructor: a Blueprint subclass's override of
	// BaseWalkSpeed is deserialized from the archetype AFTER the C++ constructor has run, so the constructor only ever
	// sees the C++ default. (The old code wrote MaxWalkSpeed straight from the constructor and had the same blind
	// spot — a BP that raised BaseWalkSpeed silently kept walking at 600.)
	if (UFPSRCharacterMovementComponent* FPSRMovement = GetFPSRMovement())
	{
		FPSRMovement->SetAuthoredBaseWalkSpeed(BaseWalkSpeed);
	}

#if ENABLE_DRAW_DEBUG
	// Movement readout. Registered for every pawn (the draw itself early-outs on non-local ones) because
	// IsLocallyControlled() isn't reliable yet at BeginPlay on a remote client's own pawn.
	MovementDebugDrawHandle = UDebugDrawService::Register(
		TEXT("Game"), FDebugDrawDelegate::CreateUObject(this, &AFPSRCharacter::DrawMovementDebug));
#endif

	// Bind regardless of local-control state: BeginPlay can run before the controller ref replicates on a remote
	// client's own pawn, so gating the bind on IsLocallyControlled() here would permanently miss it. The GameState
	// delegate is the trigger; the camera PP is only APPLIED for the locally controlled pawn (checked at apply time,
	// when controller state is stable). Binding on proxies / server-side pawns is a cheap no-op.
	TryBindVisionDelegate();

	// Seed the look-sway reference so the first frame's control-rotation delta isn't a huge jump.
	PreviousControlRotation = GetControlRotation();

	// Capture the arms' AUTHORED placement before anything writes to the component. This is the hip end of the ADS
	// blend when the arms hold the weapon, and it must be a snapshot: UpdateAimDownSights overwrites the relative
	// transform every frame, so a live read would feed last frame's solve back in. Taken here rather than in the
	// constructor because a BP subclass's override of the component transform is deserialized after the C++ constructor
	// runs — the same blind spot that made BaseWalkSpeed have to be re-pushed above.
	if (FirstPersonArms)
	{
		ArmsHipRelativeTransform = FirstPersonArms->GetRelativeTransform();
	}

	// Establish the weapon's visibility from the state we actually spawned in, rather than assuming "visible" and
	// waiting for something to change. A pawn possessed mid-air onto a wall, or re-created by seamless travel, has no
	// movement-mode edge coming to fix it up.
	RefreshWeaponVisibility();

	// Same reasoning for the first-person split: a listen-server host is already locally controlled here, so it must
	// not wait for a controller-change edge that will never arrive. A remote client's pawn is NOT locally controlled
	// yet at this point — NotifyControllerChanged catches that one.
	RefreshFirstPersonRendering();

#if WITH_EDITOR
	// Authoring-loop shim (fparms-gunanchor-ik) — see the header comment on OnEditorObjectPropertyChanged. Editor
	// (PIE/standalone-in-editor) only: the delegate itself doesn't exist outside WITH_EDITOR.
	EditorSocketChangedHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddUObject(this, &AFPSRCharacter::OnEditorObjectPropertyChanged);
#endif
}

void AFPSRCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
#if ENABLE_DRAW_DEBUG
	if (MovementDebugDrawHandle.IsValid())
	{
		UDebugDrawService::Unregister(MovementDebugDrawHandle);
		MovementDebugDrawHandle.Reset();
	}
#endif

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

#if WITH_EDITOR
	FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(EditorSocketChangedHandle);
#endif

	Super::EndPlay(EndPlayReason);
}

#if WITH_EDITOR
void AFPSRCharacter::OnEditorObjectPropertyChanged(UObject* Object, FPropertyChangedEvent& Event)
{
	// Editor-only shim for the socket-tuning loop: the grip cache is a constant re-solved only on equip/part changes
	// (design — see GetRightHandGripInGunFrame's comment), so editing a socket in the editor had no effect until a
	// PIE restart. Any socket property change re-attaches (and therefore re-caches) once — AttachWeaponMeshes is
	// idempotent and this only fires on human editor edits (low frequency), so reacting to every socket without
	// filtering is free. Not filtering to "is this socket mine" on purpose: a socket's Outer can be either the mesh
	// or the skeleton (this project has actually hit both), so a narrower check risks silently missing an update —
	// the wide net is the safer failure mode (one harmless extra no-op re-attach).
	if (Cast<USkeletalMeshSocket>(Object) || Cast<UStaticMeshSocket>(Object))
	{
		AttachWeaponMeshes(); // internally chains into RefreshHandGripInGunFrameCache
	}
}
#endif

void AFPSRCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	InitAbilitySystem();

	if (HasAuthority() && WeaponInventory)
	{
		// Fill each slot's authored default (bare hands on the melee slot) BEFORE granting the starting weapon. The
		// order matters: seeding never equips, so the granted weapon below still finds nothing equipped and becomes
		// the one in hand. Seeding afterwards would leave the melee slot empty until the first melee pickup.
		WeaponInventory->ServerSeedDefaultSlots();

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

	// NOTE (aim state): deliberately NOT cleared here. A fresh pawn is already un-aimed on every machine, and clearing
	// on the authority only bites on re-possession of a LIVE pawn — where it would make the server say hip while the
	// owner, which COND_SkipOwner sends no correction to, keeps its predicted ADS. That is the divergence, not the fix.
	// See ADR 0002 "조준 비트 복제" before adding one.

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
		// Routed through Input_Jump (not ACharacter::Jump directly) so a jump press clears the crouch/slide intent.
		EIC->BindAction(JumpAction, ETriggerEvent::Started, this, &AFPSRCharacter::Input_Jump);
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
	if (CrouchAction)
	{
		// Triggered (per-frame while held), not Started — see Input_CrouchHeld.
		EIC->BindAction(CrouchAction, ETriggerEvent::Triggered, this, &AFPSRCharacter::Input_CrouchHeld);
		EIC->BindAction(CrouchAction, ETriggerEvent::Completed, this, &AFPSRCharacter::Input_CrouchReleased);
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

bool AFPSRCharacter::IsMovementFrozen() const
{
	// See the header: a stage transition's grace window freezes movement ONLY — fire/ADS/reload/equip keep reading
	// IsRunFrozen() alone (unchanged) so they stay live through it.
	const AFPSRGameState* GS = GetWorld() ? GetWorld()->GetGameState<AFPSRGameState>() : nullptr;
	return IsRunFrozen() || (GS && GS->IsStageTransitionActive());
}

bool AFPSRCharacter::IsAiming() const
{
	// Aim state lives on the weapon-fire component (Input_ADS* on the owner + ServerSetAiming on the server, replicated
	// from there to the other clients), so this answers on every machine — that is what lets the shared body AnimBP
	// pose the aim for teammates too. See the header for the one accepted asymmetry (rejected aim-on on the owner).
	return WeaponFire && WeaponFire->IsAiming();
}

bool AFPSRCharacter::IsReloading() const
{
	// The flag lives on the replicated weapon INSTANCE (the inventory forwards to the equipped one), so like IsAiming
	// this answers on every machine. Same facade reason: the component is not reachable from an AnimBP.
	return WeaponInventory && WeaponInventory->IsReloading();
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

float AFPSRCharacter::ResolveADSTargetFOV(float HipTargetFOV, float BaseADSFOV, bool bBaseWantsADS) const
{
	if (!bBaseWantsADS)
	{
		return HipTargetFOV;
	}

	// A fullscreen scope drops the zoom during a reload (design decision — show the weapon + reload animation);
	// non-scope sights keep their zoom through the reload (existing behavior). Owner-local; reads the replicated
	// reload edge via IsReloading().
	if (CachedScopeDescriptor.bScopeOverlay && WeaponInventory && WeaponInventory->IsReloading())
	{
		return HipTargetFOV;
	}

	// Per-sight magnification: the ACTIVE sight's own AimFieldOfView (iron / reddot / scope each carry their own zoom),
	// falling back to the weapon's default ADS FOV when this sight authors none (<=0). No active sight descriptor =>
	// AimFieldOfView 0 => weapon default, so weapons without an authored sight are unchanged.
	return CachedScopeDescriptor.AimFieldOfView > 0.0f ? CachedScopeDescriptor.AimFieldOfView : BaseADSFOV;
}

void AFPSRCharacter::UpdateScopeWeaponVisibility()
{
	RefreshWeaponVisibility();
}

void AFPSRCharacter::RefreshWeaponVisibility(bool bForce)
{
	// ONE place decides whether the weapon meshes are drawn, because there is more than one reason to hide them and
	// two independent togglers would each undo the other: a scope released while on a wall, or a wall released while
	// scoped, would leave the gun in whichever state ran last.
	//
	// The two reasons do not have the same scope, which is the whole reason this needs care:
	//  - SCOPE is owner-local. Component visibility is a LOCAL render flag, and teammates are supposed to keep seeing
	//    the gun while its owner is looking down a scope. Gated on IsLocallyControlled so that a call arriving from a
	//    non-local path (a movement mode change on a proxy) cannot hide someone else's weapon.
	//  - WALL is every machine. The wall clip is bare-handed on all of them, so a gun still parented to hand_R would
	//    float beside the hand for everyone watching, not just the owner.
	const bool bHideForScope = IsLocallyControlled()
		&& IsScopeVisualActive() && CachedScopeDescriptor.bHideWeaponWhileScoped;

	const UFPSRCharacterMovementComponent* Move = GetFPSRMovement();
	bool bHideForWall = Move && Move->IsOnWall();

	// Holster-authored delay (§6): instead of vanishing the instant the wall gate closes, an authored weapon
	// descends into its holster pose (ApplyWeaponStatePose) and the mesh stays visible until that descent actually
	// finishes. This only ever DELAYS a wall-hide that was already decided above — it can never invent a new reason
	// to hide or show the weapon, and it never touches bHideForScope. Three guards, all required:
	//  1. bHideForWall already true — nothing to delay otherwise.
	//  2. IsLocallyControlled() — HolsterBlendAlpha is advanced ONLY in ApplyWeaponStatePose, which itself early-outs
	//     on anything but the owning client (owner-local cosmetic solve). A simulated proxy's alpha therefore never
	//     leaves 0, which — left unguarded — would silently defeat this delay forever (0 < 1.0 never gates true) as
	//     an accident of an unadvanced float rather than a deliberate choice. Made explicit here instead: remote
	//     observers (3P) keep the PRE-EXISTING instant hide. The wall clip is bare-handed on every machine, so a
	//     delayed hide would show the gun floating beside an empty hand for anyone watching — the opposite of what
	//     this track exists to fix.
	//  3. bFirstPersonSplitActive && bCachedHasHolsterPose — the delay is only honest when a descent will ACTUALLY be
	//     rendered, so this has to match the condition the holster pose itself is composed on. That pose rides the
	//     ARMS (UpdateAimDownSights, camera space), not the weapon, so the split being active is the whole
	//     requirement — deliberately NOT bWeaponAttachIsGunAnchor, which gates the SLIDE/AIRBORNE poses in
	//     ApplyWeaponStatePose. Gun-anchor is an attach detail of where the weapon hangs; the arms descend either way.
	//     bCachedHasHolsterPose covers the other half: an unauthored weapon has no pose to show during a delay, so it
	//     keeps the exact legacy instant-hide behavior (zero regression).
	if (bHideForWall && IsLocallyControlled() && bFirstPersonSplitActive && bCachedHasHolsterPose)
	{
		bHideForWall = (HolsterBlendAlpha >= 1.0f);
	}

	// SetVisibility below and the bWeaponHidden latch remain THIS function's alone to write — the delay above only
	// feeds an input into bHideForWall, it does not add a second visibility toggle or a second latch.
	const bool bShouldHide = bHideForScope || bHideForWall;
	if (bShouldHide == bWeaponHidden && !bForce)
	{
		return; // toggle only on change, unless the components were rebuilt under us
	}
	bWeaponHidden = bShouldHide;
	if (WeaponMesh)
	{
		WeaponMesh->SetVisibility(!bShouldHide, /*bPropagateToChildren=*/true);
	}
	if (WeaponMeshStatic)
	{
		WeaponMeshStatic->SetVisibility(!bShouldHide, /*bPropagateToChildren=*/true);
	}
}

void AFPSRCharacter::RefreshFirstPersonRendering()
{
	USkeletalMeshComponent* BodyMesh = GetMesh();
	if (!BodyMesh || !FirstPersonArms)
	{
		return;
	}

	// The split gates on an AUTHORED arms mesh, and that is the load-bearing part of this function. Flipping the body
	// to WorldSpaceRepresentation makes the engine set bOwnerNoSee — so doing it without arms to put in their place
	// leaves the owner staring at an empty screen. Same rule as the left-hand IK: with nothing valid to point at, the
	// feature turns OFF rather than running wrong (invariant 9's spirit — content decides, code fails safe).
	//
	// "Authored" is read straight off the COMPONENT. This code sets neither the mesh nor the anim class: both are slots
	// the Blueprint already owns, and a data field that overwrote them would make those slots a lie — you assign one,
	// the viewport agrees, and the runtime quietly swaps in something else. (That is not hypothetical: the field this
	// replaced was still pointing at a deprecated arms mesh on a different skeleton, so PIE would have shown the wrong
	// arms with none of the poses applying.) The rest of this project already keeps the component authoritative —
	// AFPSREnemyBase and AFPSRXPPickup only fall back to config when the slot is empty.
	//
	// The other half of the gate is "are we LOOKING THROUGH this pawn", not "is this pawn locally controlled" — the same
	// question head hiding already asks (ADR 0002 invariant 5), and the two must agree or the owner ends up watching a
	// teammate with their own arms floating over the picture. It also settles where the weapon hangs while downed: with
	// the arms gone the gun goes back to the body's hand, so a spectated corpse is not empty-handed (ADR 0003 inv. 14).
	// IsLocallyControlled stays in front of it because only the local pawn owns any of this render state at all.
	const bool bSplit = (FirstPersonArms->GetSkeletalMeshAsset() != nullptr)
		&& IsLocallyControlled() && IsViewedThroughOwnEyes();

	FirstPersonArms->SetVisibility(bSplit, /*bPropagateToChildren=*/true);

	// FirstPerson: drawn in the first-person pass and, per the engine, with every shadow flag forced off
	// (PrimitiveSceneProxy.cpp — bIsFirstPerson clears bCastDynamicShadow/Static/Volumetric/Far). That is what we
	// want: the body below still casts the full-body shadow, so tagging the arms keeps it from being cast twice.
	FirstPersonArms->SetFirstPersonPrimitiveType(
		bSplit ? EFirstPersonPrimitiveType::FirstPerson : EFirstPersonPrimitiveType::None);

	// WorldSpaceRepresentation is what hides the body from its owner: the engine sets bOwnerNoSee=true and
	// bCastHiddenShadow=false internally so the special first-person shadow path in VSM picks it up instead of the
	// regular one. Teammates are unaffected — bOwnerNoSee is per-view, so they keep seeing a whole body.
	BodyMesh->SetFirstPersonPrimitiveType(
		bSplit ? EFirstPersonPrimitiveType::WorldSpaceRepresentation : EFirstPersonPrimitiveType::None);

	// The weapon follows the hand that is actually on screen, so it moves with this decision (ADR 0003 invariant 14).
	// Only on a CHANGE: this function runs from several lifecycle paths and re-attaching every time would fight the
	// per-frame ADS solve. AttachWeaponMeshes is deliberately narrower than RefreshEquippedWeaponVisual — the full
	// refresh replays the equip montage, which would fire every time the player is downed or comes back.
	if (bFirstPersonSplitActive != bSplit)
	{
		bFirstPersonSplitActive = bSplit;
		AttachWeaponMeshes();
	}

	// NOTE on bEnableFirstPersonScale / bEnableFirstPersonFieldOfView (both engine-default false, and left that way):
	// turning them on would render first-person primitives at their own FOV and scale, which is the usual fix for a gun
	// clipping into walls — but it moves where the sight RENDERS versus where UpdateAimDownSights puts it, and this
	// project pins the sight to the view centre-line. That is a separate decision with its own measurement (ADR 0003).
}

EFirstPersonPrimitiveType AFPSRCharacter::GetWeaponFirstPersonPrimitiveType() const
{
	return bFirstPersonSplitActive ? EFirstPersonPrimitiveType::FirstPerson : EFirstPersonPrimitiveType::None;
}

bool AFPSRCharacter::IsViewedThroughOwnEyes() const
{
	// "Are we looking through THIS pawn's eyes" — the single question behind head hiding, the arms split, and which hand
	// the weapon hangs off. Polling the view target beats edge-detecting SetViewTargetWithBlend: the 0.3s spectate blend
	// has no single "switched" instant. Callers all guard on a change latch, so the steady state is one pointer compare.
	const APlayerController* LocalPC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	return LocalPC && LocalPC->GetViewTarget() == this;
}

FName AFPSRCharacter::ResolveWeaponAttachSocket(const UFPSRWeaponDataAsset* Weapon) const
{
	// One rule, read from both this class's own runtime attach path (AttachWeaponMeshes, right below) and the weapon-
	// assembler editor tool — see the header comment for why this had to move out of AttachWeaponMeshes itself.
	return (Weapon && !Weapon->WeaponAttachSocket.IsNone()) ? Weapon->WeaponAttachSocket : WeaponAttachSocketName;
}

bool AFPSRCharacter::GetFirstPersonViewSetup(FTransform& OutCameraRelativeToArms, FMinimalViewInfo& OutViewInfo) const
{
	if (!FirstPersonCamera || !FirstPersonArms)
	{
		return false;
	}

	// 위상 무관 — 두 컴포넌트의 실제 월드 트랜스폼끼리 상대를 구한다(헤더 주석 참조). 팔을 항등에 세운 프리뷰
	// 씬에서는 이 값이 그대로 카메라를 놓을 자리가 된다.
	OutCameraRelativeToArms = FirstPersonCamera->GetComponentTransform().GetRelativeTransform(FirstPersonArms->GetComponentTransform());

	// 게임이 매 프레임 부르는 그 함수 그대로(ULocalPlayer::CalcSceneView -> 뷰타깃 -> UCameraComponent::GetCameraView).
	// FOV/AspectRatio/bConstrainAspectRatio/1인칭 파라미터/PP 가 한 번에 채워지므로 툴이 필드를 베낄 필요가 없다.
	FirstPersonCamera->GetCameraView(0.0f, OutViewInfo);
	return true;
}

void AFPSRCharacter::AttachWeaponMeshes()
{
	// Which hand is on screen decides which hand holds the gun (ADR 0003 invariant 14). Attachment is per-component
	// LOCAL state — the engine only replicates the actor's root attachment — so every machine answers this for itself:
	// the owner sees the gun in their first-person arms, everyone else sees it in the body's hand, from one component
	// and one weapon DataAsset. That is what keeps the "two sets of weapon fields, one of them never authored" bug
	// (ADR 0002) from coming back as "two sets of weapon COMPONENTS".
	USkeletalMeshComponent* GripMesh = bFirstPersonSplitActive && FirstPersonArms ? FirstPersonArms.Get() : GetMesh();
	if (!GripMesh)
	{
		return;
	}

	// Per-weapon DA socket overrides the character default. The same name has to resolve on BOTH skeletons, which it
	// does: SOCKET_Weapon is authored on the body's grip hand and on the arms' (S_Mannequin).
	const UFPSRWeaponDataAsset* Weapon = WeaponInventory ? WeaponInventory->GetCurrentWeapon() : nullptr;
	const FName AttachSocket = ResolveWeaponAttachSocket(Weapon);
	const float AttachScale = Weapon ? Weapon->WeaponAttachScale : 1.0f;

	// Gun-anchor IK (fparms-gunanchor-ik): true first-person arms authored with the two-bone hand-IK rig
	// (ik_hand_root > ik_hand_gun > ik_hand_l/r) anchor the weapon to the ik_hand_gun BONE instead of the grip socket.
	// ik_hand_gun sits OUTSIDE the arm's FK chain (a child of ik_hand_root, not of the elbow/wrist), so once the arms'
	// animgraph starts driving RIGHT-hand IK off it, that IK can no longer drag the weapon around. That is a
	// PRECONDITION for adding right-hand IK safely, not a fix for an existing problem — today the weapon still rides
	// hand_r's own socket and nothing moves hand_r, so no loop exists yet. Put IK on hand_r WITHOUT this anchor change
	// and the loop appears immediately: hand IK -> hand_r -> weapon riding hand_r's socket -> IK reaching for its own
	// moving target.
	// The animgraph is responsible for CopyBone-ing ik_hand_gun := hand_r (component space) every frame — that is
	// Blueprint work, done separately. Until it exists, the weapon renders wherever the retargeted animation's
	// ik_hand_gun TRACK happens to hold it — the retargeter authors a track for every bone but does not solve IK
	// chains (documented retargeter pitfall), so that track is a static, arbitrary leftover value, not a real rest
	// pose. Measured on FP_Rifle_Idle: component-space (-56.65, -0.34, 111.68) — floating in front of the chest, not
	// at the feet or anywhere anatomically meaningful. Either way it reads as obviously wrong, which is the point:
	// "not wired up yet" has to be loud, not a subtle few-cm offset someone could mistake for a tuning value.
	const bool bArmsHaveGunBone = GripMesh == FirstPersonArms && GripMesh->GetBoneIndex(IkHandGunBoneName) != INDEX_NONE;
	bool bUseGunBoneAnchor = false;
	FTransform GunBoneRelativeTransform = FTransform::Identity;
	if (bArmsHaveGunBone)
	{
		// The anchor's correctness rests on an equality this function cannot see from the socket side alone:
		// "AttachSocket's parent bone == the source bone the ABP's CopyBone reads from (ExpectedGripSourceBoneName)".
		// Verify what IS checkable from here rather than assume it:
		//  1. AttachSocket must resolve to a REAL socket object, not fall through to a bone-name or missing-name
		//     fallback. Engine evidence (USkinnedMeshComponent::GetSocketTransform, SkinnedMeshComponent.cpp): a real
		//     socket returns its authored local transform for RTS_ParentBoneSpace (3272-3275); a BONE name instead
		//     returns that bone's CURRENT POSED bone-local transform (3286-3299) — reading that once, here, at attach
		//     time would bake whatever pose happened to be live at that instant in as a PERMANENT offset; and if the
		//     name resolves to neither, the component's own raw world transform leaks through untouched (3262's
		//     initial value) — the weapon would jump map-scale and nothing would log why. This project has already
		//     lost an arms socket to a silent rename once (SOCKET_Weapon has vanished across a restart before) — this
		//     is exactly the kind of blind spot that bit us then, so it gets a real check now.
		//  2. That socket's PARENT BONE must be ExpectedGripSourceBoneName (hand_r) — the weapon-assembler editor tool
		//     can move a socket to a different bone (bMovedBone), which would silently invalidate the anchor's whole
		//     premise. The animgraph half of the equality — does the ABP's CopyBone actually read FROM this bone — is
		//     Blueprint-side and unverifiable from C++ at all; only the socket half is enforced here, so a mismatch on
		//     THAT half is at least loud instead of silent.
		// GetSocketInfoByName also returns the socket's local transform, so reusing it here removes the second
		// GetSocketTransform(RTS_ParentBoneSpace) lookup this block used to make.
		FTransform SocketLocalTransform = FTransform::Identity;
		int32 SocketBoneIndex = INDEX_NONE;
		const USkeletalMeshSocket* Socket = GripMesh->GetSocketInfoByName(AttachSocket, SocketLocalTransform, SocketBoneIndex);
		if (!Socket)
		{
			UE_LOG(LogFPSR, Warning, TEXT("%s: AttachWeaponMeshes gun-anchor skipped — '%s' does not resolve to a real socket on %s (falling back to socket-attach, which will likely mis-place the weapon too if the name is simply wrong)."),
				*GetName(), *AttachSocket.ToString(), *GripMesh->GetName());
		}
		else
		{
			const FName ActualSourceBone = GripMesh->GetBoneName(SocketBoneIndex);
			if (ActualSourceBone != ExpectedGripSourceBoneName)
			{
				UE_LOG(LogFPSR, Warning, TEXT("%s: AttachWeaponMeshes gun-anchor skipped — socket '%s' is parented to bone '%s', not ExpectedGripSourceBoneName ('%s') that the arms' CopyBone is expected to read from. Falling back to socket-attach."),
					*GetName(), *AttachSocket.ToString(), *ActualSourceBone.ToString(), *ExpectedGripSourceBoneName.ToString());
			}
			else
			{
				// AttachSocket's (SOCKET_Weapon) fixed offset from ITS OWN parent bone (hand_r) — authoring data,
				// NOT a live read of hand_r's current pose. Re-basing the weapon onto ik_hand_gun WITH this exact
				// offset reproduces the old hand_r-socket world position the instant ik_hand_gun's CopyBone makes
				// it equal hand_r again.
				bUseGunBoneAnchor = true;
				GunBoneRelativeTransform = FTransform(SocketLocalTransform.GetRotation(), SocketLocalTransform.GetLocation(), FVector(AttachScale));
			}
		}
	}

	// State-pose-on-weapon refactor: cache the base this attach just resolved, and whether it is the gun-anchor path,
	// BEFORE the AttachOne lambda below runs — ApplyWeaponStatePose (AFPSRCharacter::Tick) adds its delta on top of
	// CachedWeaponAttachRelative every frame, and GetWeaponRootPlacementInGunFrame reads it instead of the weapon's
	// live RelativeTransform (see that function + CachedWeaponAttachRelative's own header comments for why). The
	// fallback path gets Identity/false, which is what keeps ApplyWeaponStatePose a no-op there (current ADS-owns-the-
	// world-write behavior, unchanged). Also reset the derived per-frame delta: a re-attach (equip/part-rebuild) means
	// whatever state-pose delta was riding the PREVIOUS weapon no longer means anything for grip/part-frame readers
	// until ApplyWeaponStatePose recomputes it fresh next tick.
	CachedWeaponAttachRelative = bUseGunBoneAnchor ? GunBoneRelativeTransform : FTransform::Identity;
	bWeaponAttachIsGunAnchor = bUseGunBoneAnchor;
	CachedWeaponStatePoseDelta = FTransform::Identity;

	// Snap (not KeepRelative) so the weapon sits exactly where the skeleton's grip socket was authored.
	// SnapToTargetNotIncludingScale leaves scale alone (engine: its scale rule is KeepWorld), which is what lets the
	// explicit scale below be the single place the weapon's size is decided.
	//
	// FirstPerson tags a primitive as drawn in the first-person pass with every shadow flag forced off
	// (PrimitiveSceneProxy.cpp — bIsFirstPerson clears bCastDynamicShadow/Static/Volumetric/Far). Riding the arms, the
	// gun lives in CAMERA space, so a shadow from it would be cast from the viewer's eye into the world — the accepted
	// cost of ADR 0003 axis 2 is "no gun in your own shadow", not "a gun-shaped smear beside you". On the body it keeps
	// its shadow, because there it IS a world object.
	auto AttachOne = [GripMesh, AttachSocket, AttachScale, bUseGunBoneAnchor, GunBoneRelativeTransform, this](UMeshComponent* Comp)
	{
		if (!Comp)
		{
			return;
		}
		if (bUseGunBoneAnchor)
		{
			// SnapToTargetNotIncludingScale here only decides the FIRST relative transform the attach computes — it is
			// immediately overwritten below by the exact offset baked above. The single SetRelativeTransform call (not
			// a separate SetRelativeScale3D) is deliberate: this FTransform already carries AttachScale, so a second
			// scale call would double-apply it.
			Comp->AttachToComponent(GripMesh, FAttachmentTransformRules::SnapToTargetNotIncludingScale, IkHandGunBoneName);
			Comp->SetRelativeTransform(GunBoneRelativeTransform);
		}
		else
		{
			// Body, an arms mesh without the ik_hand_gun bone, or the socket-verification checks above failed (see
			// the UE_LOG Warning sites) — unchanged fallback, Snap straight onto the grip socket.
			Comp->AttachToComponent(GripMesh, FAttachmentTransformRules::SnapToTargetNotIncludingScale, AttachSocket);
			Comp->SetRelativeScale3D(FVector(AttachScale));
		}
		Comp->SetFirstPersonPrimitiveType(GetWeaponFirstPersonPrimitiveType());
	};
	AttachOne(WeaponMesh);
	AttachOne(WeaponMeshStatic);

	// The modular parts ride the weapon mesh, so they need no re-attach — but the render tag does NOT propagate down an
	// attachment chain, and a barrel left untagged in camera space casts exactly the stray shadow the tag above exists
	// to remove. Parts created later pick this up at creation (RebuildPartsFromSelection).
	for (UStaticMeshComponent* Part : WeaponPartComponents)
	{
		if (Part)
		{
			Part->SetFirstPersonPrimitiveType(GetWeaponFirstPersonPrimitiveType());
		}
	}

	// Re-attaching restores the components to visible, while this class's hidden latch still says "hidden" — so a plain
	// call would early-out and hand a wall-hung (or scoped) player their gun back. Forced, same as after an equip.
	RefreshWeaponVisibility(/*bForce=*/true);

	// The attach target/offset just changed (hand swap, weapon swap, or a plain re-attach) — the gun-frame grip is a
	// constant derived from exactly that, so refresh it here rather than re-solving it every animation frame.
	RefreshHandGripInGunFrameCache();
}

void AFPSRCharacter::OnMovementModeChanged(EMovementMode PrevMovementMode, uint8 PreviousCustomMode)
{
	Super::OnMovementModeChanged(PrevMovementMode, PreviousCustomMode);

	// Event-driven rather than polled in Tick, and hooked HERE rather than in the movement component: letting the
	// movement component reach into weapon rendering would blur ADR 0001's rule that everything else only QUERIES it.
	// Fires on simulated proxies too — the engine's ApplyNetworkMovementMode goes through SetMovementMode.
	RefreshWeaponVisibility();

	// Grabbing a wall cancels any in-flight reload — both hands are on the wall, the exact same reason the fire gate
	// closes, so this reads the SAME predicate (CanFireInCurrentState) rather than inventing a second one: one reason,
	// one predicate, so the two can never drift apart. CancelReload checks authority internally, so this single call
	// site is correct on every machine — the server actually cancels, and the owner finds out via OnRep_Reloading.
	// This function also runs on SIMULATED PROXIES (the engine's ApplyNetworkMovementMode routes through
	// SetMovementMode too), but CancelReload's HasAuthority() early-out makes that call a harmless no-op there.
	if (WeaponInventory)
	{
		const UFPSRCharacterMovementComponent* Move = GetFPSRMovement();
		if (Move && !Move->CanFireInCurrentState())
		{
			WeaponInventory->CancelReload();
		}
	}
}

void AFPSRCharacter::UpdateFirstPersonBodyVisibility()
{
	USkeletalMeshComponent* BodyMesh = GetMesh();
	if (!BodyMesh || GetNetMode() == NM_DedicatedServer)
	{
		return; // nothing renders on a dedicated server
	}

	// ADR 0002 invariant 5: hide the head when we are looking THROUGH this pawn's eyes, NOT when this pawn is "the local
	// player". The two answers differ in both directions — a DBNO teammate spectating this pawn (§2-13) sees through
	// this pawn's camera and would otherwise be inside its skull, while the later spectator/debug 3P rig becomes the
	// view target itself and must get the head back. The change guard below makes the steady state one pointer compare.
	const bool bViewedThroughOwnEyes = IsViewedThroughOwnEyes();
	if (bViewedThroughOwnEyes == bHeadHiddenForOwnView)
	{
		return;
	}
	// Latch the decision BEFORE the data checks below, so a mis-authored bone name warns once per view-target change
	// instead of once per frame.
	bHeadHiddenForOwnView = bViewedThroughOwnEyes;

	// The arms split answers to the same question (ADR 0003), and this is the only place that notices the view target
	// moved — the lifecycle hooks (spawn, possession) cannot see a spectate transition. Ordering does not matter: the
	// two touch different components, and RefreshFirstPersonRendering re-reads the view target rather than trusting a
	// latch, so it is correct whether it runs before or after the head work below.
	RefreshFirstPersonRendering();

	if (HeadBoneName.IsNone())
	{
		return; // head hiding deliberately disabled by data
	}
	if (BodyMesh->GetBoneIndex(HeadBoneName) == INDEX_NONE)
	{
		// HideBoneByName does nothing and logs nothing when the skeleton lacks the bone (engine:
		// SkinnedMeshComponent.cpp -> GetBoneIndex -> INDEX_NONE -> return), which is exactly how a mesh swap would
		// ship a first-person view of the inside of a head with no error anywhere. Say so out loud (invariant 9).
		UE_LOG(LogFPSR, Warning, TEXT("%s: HeadBoneName '%s' not found on %s — first-person head hiding is disabled."),
			*GetName(), *HeadBoneName.ToString(),
			BodyMesh->GetSkeletalMeshAsset() ? *BodyMesh->GetSkeletalMeshAsset()->GetName() : TEXT("<no mesh>"));
		return;
	}

	// Children of the hidden bone go with it (engine: BVS_HiddenByParent), so hair / eyes / glasses need no extra work.
	// Per-component, so the owner's copy of this pawn is the only one affected — a remote client's copy keeps its head.
	if (bViewedThroughOwnEyes)
	{
		BodyMesh->HideBoneByName(HeadBoneName, PBO_None);
	}
	else
	{
		BodyMesh->UnHideBoneByName(HeadBoneName);
	}
}

TSubclassOf<UUserWidget> AFPSRCharacter::GetActiveScopeOverlayWidgetClass() const
{
	// Loaded only when a scope is actually active — the HUD calls this on the scoped edge (not per frame), so the
	// synchronous soft-ptr load happens at most once per scope-in. Null (HUD falls back to its own class) when none authored.
	if (!IsScopeVisualActive())
	{
		return nullptr;
	}
	return CachedScopeDescriptor.ScopeOverlayWidgetClass.LoadSynchronous();
}

bool AFPSRCharacter::IsScopeVignetteEnabled() const
{
	return IsScopeVisualActive() && CachedScopeDescriptor.bScopeVignette;
}

float AFPSRCharacter::GetStanceEyeTravel() const
{
	const ACharacter* DefaultCharacter = GetDefault<ACharacter>(GetClass());
	const UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	if (!DefaultCharacter || !MoveComp)
	{
		return 0.0f;
	}
	// Both measured from the feet, so the difference is exactly how far the view drops between the two stances.
	const float StandingEye = GetDefaultHalfHeight() + DefaultCharacter->BaseEyeHeight;
	const float CrouchedEye = MoveComp->GetCrouchedHalfHeight() + CrouchedEyeHeight;
	return FMath::Abs(StandingEye - CrouchedEye);
}

FVector AFPSRCharacter::ClampPointInsideCapsule(const FVector& CapsuleSpacePoint, float& OutClampedAmount) const
{
	OutClampedAmount = 0.0f;
	const UCapsuleComponent* Capsule = GetCapsuleComponent();
	if (!Capsule)
	{
		return CapsuleSpacePoint;
	}

	// LATERAL ONLY, against the capsule RADIUS — not the cross-section at the camera's height, and Z is not touched.
	//
	// The height is the stance system's, not this function's. UpdateStanceCamera holds the view ABOVE the crouched
	// capsule on purpose while a crouch eases in (relative Z ~81 inside a 40-tall capsule), so clamping Z would truncate
	// that blend into a snap, and a cross-section computed at that height would be zero — collapsing the forward offset
	// the instant the player crouches and restoring it a moment later. Both are regressions, and neither buys anything:
	//
	// What the clamp is actually for is the wall case (ADR 0002 실패 흐름 ①) — the shooting origin IS this camera, so a
	// camera that reaches past a wall means bullets that come from past it. Walls are vertical, and the capsule can never
	// bring its axis closer than Radius to one. So "never further from the axis than Radius" already makes reaching past
	// a wall impossible, at every height, with no dependence on stance. Strict capsule containment would only add
	// something for ceilings and floors, which the stance system already bounds and which nobody shoots through.
	const float MaxLateral = FMath::Max(0.0f, Capsule->GetScaledCapsuleRadius() - FMath::Max(0.0f, CameraCapsuleClampMargin));

	const FVector2D Lateral(CapsuleSpacePoint.X, CapsuleSpacePoint.Y);
	const float LateralSize = Lateral.Size();
	if (LateralSize <= MaxLateral)
	{
		return CapsuleSpacePoint;
	}

	// Scale rather than cut per-axis, so an offset that uses both X and Y keeps its direction.
	const FVector2D Scaled = (LateralSize > KINDA_SMALL_NUMBER) ? (Lateral / LateralSize * MaxLateral) : FVector2D::ZeroVector;
	OutClampedAmount = LateralSize - MaxLateral;
	return FVector(Scaled.X, Scaled.Y, CapsuleSpacePoint.Z);
}

void AFPSRCharacter::BeginStanceCameraBlend()
{
	if (!FirstPersonCamera)
	{
		return;
	}

	// Where the camera would sit right now with nothing held back: the movement component has already resized and moved
	// the capsule, and Super has already switched BaseEyeHeight, so this is the new stance's finished position.
	// FirstPersonCameraOffset.Z has to be in here — CachedCameraWorldZ below already carries it, so leaving it out would
	// bias every stance change by exactly that much and start the blend from a view that was never there.
	const float NominalWorldZ = GetCapsuleComponent()->GetComponentLocation().Z + BaseEyeHeight + FirstPersonCameraOffset.Z;

	// Hold the view exactly where it ended last frame and let UpdateStanceCamera ease it from there. Measuring the jump
	// rather than predicting it is what makes this right in every case — on the ground the capsule is re-anchored at the
	// feet, in the air at its centre, and on a remote proxy (DBNO spectating a team-mate) it is not moved at all.
	// The bound only exists so a respawn or teleport landing on the same frame can't strand the camera; it is not a
	// tuning value.
	const float SanityLimit = FMath::Max(GetStanceEyeTravel(), 1.0f) * 2.0f;
	CameraEyeOffsetStart = FMath::Clamp(CachedCameraWorldZ - NominalWorldZ, -SanityLimit, SanityLimit);
}

void AFPSRCharacter::UpdateStanceCamera()
{
	if (!FirstPersonCamera)
	{
		return;
	}

	float EyeOffset = 0.0f;
	if (!FMath::IsNearlyZero(CameraEyeOffsetStart))
	{
		// The clock belongs to the movement component, which owns the stance (invariant 1). Sharing it is what makes the
		// view, the walk-speed cap and — once it is authored — the stance animation finish on the same frame.
		const UFPSRCharacterMovementComponent* FPSRMovement = GetFPSRMovement();
		const float Progress = FPSRMovement ? FPSRMovement->GetStanceTransitionProgress() : 1.0f;

		// Eased rather than linear: a constant-rate drop stops dead at the end and reads as a clunk.
		EyeOffset = CameraEyeOffsetStart * (1.0f - FMath::SmoothStep(0.0f, 1.0f, Progress));
		if (FMath::IsNearlyZero(EyeOffset))
		{
			CameraEyeOffsetStart = 0.0f; // settled
			EyeOffset = 0.0f;
		}
	}

	// The whole camera position, composed here and nowhere else (invariant 10 — one writer per frame). The layers are
	// stance base -> fixed correction -> capsule clamp. ADR 0002's eye anchor adds one more between the correction and
	// the clamp (a damped head-bone deviation); it is deliberately absent until the body AnimBP has weapon poses worth
	// following, and it will feed the SAME clamp when it arrives.
	// FirstPersonCameraOffset is in capsule space, and the capsule's yaw IS the control yaw, so its X is "forward along
	// the way you're looking" — which is what keeps the camera out in front of the chest instead of inside it.
	const FVector DesiredRelative(
		FirstPersonCameraOffset.X,
		FirstPersonCameraOffset.Y,
		BaseEyeHeight + FirstPersonCameraOffset.Z + EyeOffset);

	float ClampedAmount = 0.0f;
	const FVector ClampedRelative = ClampPointInsideCapsule(DesiredRelative, ClampedAmount);
#if ENABLE_DRAW_DEBUG
	LastCameraClampAmount = ClampedAmount;
#endif

	if (!FirstPersonCamera->GetRelativeLocation().Equals(ClampedRelative))
	{
		FirstPersonCamera->SetRelativeLocation(ClampedRelative);
	}

	// Recorded after the move, so the next stance change has this frame's finished eye position to hold on to.
	CachedCameraWorldZ = FirstPersonCamera->GetComponentLocation().Z;
}

void AFPSRCharacter::SetBaseFieldOfView(float NewBaseFieldOfView)
{
	// Bounded rather than trusted: this is a settings entry point, and a 0 or negative FOV is not a preference, it is a
	// broken projection. The bound is authored data (BaseFieldOfViewRange), so the slider that will call this can read
	// the same numbers instead of carrying its own copy.
	BaseFieldOfView = FMath::Clamp(NewBaseFieldOfView, BaseFieldOfViewRange.X, BaseFieldOfViewRange.Y);
}

void AFPSRCharacter::UpdateCameraFieldOfView(float DeltaSeconds)
{
	// Owner-local. On a remote pawn the camera keeps its authored FOV, and that is deliberate: a DBNO teammate
	// spectating this pawn renders through this very component (CalcCamera), so writing OUR slide/ADS state here would
	// show up on THEIR screen.
	if (!FirstPersonCamera || !IsLocallyControlled())
	{
		return;
	}

	// The movement component owns the state; this only reads it (ADR 0001 invariant 1). Tick already runs after the
	// movement component (AddTickPrerequisiteComponent in BeginPlay), so this is what the move actually settled on
	// this frame rather than last frame's answer.
	const UFPSRCharacterMovementComponent* FPSRMovement = GetFPSRMovement();
	const bool bSliding = FPSRMovement && FPSRMovement->IsSliding();

	// Eased, never snapped — and eased on the OFFSET rather than on the final FOV, so that starting a slide, aiming
	// mid-slide and ending the slide are all just target changes with nothing to sequence.
	SlideFOVOffsetCurrent = FMath::FInterpTo(
		SlideFOVOffsetCurrent,
		bSliding ? SlideFieldOfViewOffset : 0.0f,
		DeltaSeconds,
		FMath::Max(0.01f, SlideFieldOfViewInterpSpeed));

	// Base (settings) + stance offsets = where the view sits when NOT aiming. Everything is relative to
	// BaseFieldOfView, which is why a future FOV slider needs no other change than SetBaseFieldOfView.
	const float HipTargetFOV = BaseFieldOfView + SlideFOVOffsetCurrent;

	float TargetFOV = HipTargetFOV;
	// Fallback speed for the no-weapon case (unarmed, or between equips). The offset is already eased above, so this
	// only shapes how the camera chases a base-FOV change.
	float InterpSpeed = SlideFieldOfViewInterpSpeed;

	UFPSRWeaponInstance* Instance = WeaponInventory ? WeaponInventory->GetCurrentInstance() : nullptr;
	if (Instance)
	{
		const FFPSRWeaponStatBlock& Stats = Instance->GetResolvedStats();
		// Movement state gates the ADS FOV the same way it gates UpdateAimDownSights' pose/sight solve (single
		// judgment source, CanFireInCurrentState()) — a wall-hang drops the zoom back to hip FOV instead of leaving
		// the player staring down a scope for a gun that just left their hands. FPSRMovement is already resolved
		// above for the slide FOV offset, so this reuses it rather than re-fetching the component.
		const bool bBaseWantsADS = WeaponFire && WeaponFire->IsAiming() && Stats.bHasADS
			&& (!FPSRMovement || FPSRMovement->CanFireInCurrentState());
		// REPLACES the hip target rather than adding to it: while aiming, the sight's magnification is the whole point
		// and a stance offset stacked on top of a ~30 degree scope would read as the zoom changing.
		TargetFOV = ResolveADSTargetFOV(HipTargetFOV, Stats.ADSFieldOfView, bBaseWantsADS);
		InterpSpeed = Stats.ADSInterpSpeed;
	}

	FirstPersonCamera->FieldOfView = FMath::FInterpTo(
		FirstPersonCamera->FieldOfView, TargetFOV, DeltaSeconds, FMath::Max(0.01f, InterpSpeed));
}

void AFPSRCharacter::OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
	Super::OnStartCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust); // refreshes BaseEyeHeight -> CrouchedEyeHeight
	BeginStanceCameraBlend();
}

void AFPSRCharacter::OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
	Super::OnEndCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust); // restores BaseEyeHeight from the class default
	BeginStanceCameraBlend();
}

UFPSRCharacterMovementComponent* AFPSRCharacter::GetFPSRMovement() const
{
	return Cast<UFPSRCharacterMovementComponent>(GetCharacterMovement());
}

bool AFPSRCharacter::CanPerformSpecialMovement() const
{
	// Both terms read replicated state (GameState run-paused/stage-transition, PlayerState life state), so the
	// server and the owning client agree — the movement component gates the slide on this and prediction needs
	// both to match. IsMovementFrozen (not IsRunFrozen) so a stage transition also blocks slide/wall-hang entry.
	return !IsMovementFrozen() && !IsIncapacitatedLocal();
}

bool AFPSRCharacter::IsIncapacitatedLocal() const
{
	// Not a live participant: DBNO (downed) OR Dead. Gates actions (fire/swap/reload/ADS) + contact damage.
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
	if (IsIncapacitatedLocal())
	{
		return false;
	}

	// Engine default is `!IsCrouched() && JumpIsAllowedInternal()`. That leading crouch term is a SECOND, separate
	// block from the one inside CanAttemptJump — clearing only the latter still leaves this one refusing every jump
	// made from a crouch or a slide. Drop just this term and keep JumpIsAllowedInternal, so jump count and hold-time
	// rules stay entirely engine-governed (CanAttemptJump, which we also override, is called from inside it).
	return JumpIsAllowedInternal();
}

void AFPSRCharacter::ApplyMoveSpeedMultiplier(float Mult)
{
	// One layer of the walk-speed composition; the movement component owns the result (ADR 0001).
	if (UFPSRCharacterMovementComponent* FPSRMovement = GetFPSRMovement())
	{
		FPSRMovement->SetMoveSpeedMultiplier(Mult);
	}
}

void AFPSRCharacter::ApplyDownedLocomotion(bool bDowned)
{
	// Stationary while downed: DBNO no longer crawls — the player stays where it fell and spectates an ally (§2-13).
	// Movement input is also gated for !Alive (Input_Move*), so this is belt-and-suspenders against residual slide.
	//
	// Pushed as a LAYER rather than written straight to MaxWalkSpeed. The old code set 0 here and recomputed
	// BaseWalkSpeed * multiplier on revive, which meant a speed card landing WHILE downed went through
	// ApplyMoveSpeedMultiplier, overwrote the 0 and let a downed player walk. Composing the layers makes that
	// impossible: the downed layer wins for as long as it is set, whatever else changes underneath it.
	if (UFPSRCharacterMovementComponent* FPSRMovement = GetFPSRMovement())
	{
		FPSRMovement->SetDownedLocomotion(bDowned);
	}

	// Downed body should not physically block / get pushed by the swarm (mirror of the grace-window pass-through).
	// Recompute through the shared collision helper so it composes with any active grace window and, being
	// keyed off the (already-updated) LifeState, restores enemy blocking on revive. This runs on the server
	// (HandleOutOfHealth), the owning client (OnRep_LifeState) and revive (PerformRevive) — the same symmetric hook
	// — so the DBNO pass-through is server+client symmetric with no extra RPC/OnRep.
	RefreshPawnCollisionResponse();
}

void AFPSRCharacter::Input_MoveForward(const FInputActionValue& Value)
{
	// Downed (DBNO) is stationary + spectating an ally (§2-13), so block movement for DBNO and Dead alike. A hard
	// freeze (card select) or an active stage transition (ADR 0010 D6 — movement-only lock) also stops movement here.
	if (IsMovementFrozen() || IsIncapacitatedLocal())
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
	if (IsMovementFrozen() || IsIncapacitatedLocal())
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
	// Client-side UX gate only — the server-authoritative gate lives in UFPSRWeaponInventoryComponent::StartReload.
	// Same client-input + server-RPC pairing this project uses everywhere else (Input_EquipSlot / ServerEquipSlot):
	// don't send an RPC that is guaranteed to be rejected.
	const UFPSRCharacterMovementComponent* Move = GetFPSRMovement();
	if (Move && !Move->CanFireInCurrentState()) { return; }
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

void AFPSRCharacter::Input_CrouchHeld(const FInputActionValue& Value)
{
	// Intent only — whether this becomes a crouch or a slide is the movement component's call (it owns the state, and
	// it needs to reach the same conclusion on the server, which never sees this function).
	if (IsMovementFrozen() || IsIncapacitatedLocal())
	{
		return;
	}
	// Don't re-assert the intent while a jump is pending. Enhanced Input does not guarantee the order two actions fire
	// in within a frame, so without this the handler can run AFTER Input_Jump and restore the crouch it just cleared —
	// on the same frame, before the jump is even processed. bPressedJump stays set until StopJumping, so this also
	// covers the frames between the press and the character actually leaving the ground.
	if (bPressedJump)
	{
		return;
	}
	// Never re-assert the intent in mid-air either. Airborne crouch/slide is forbidden by design anyway, and since this
	// runs every frame it would otherwise undo Input_Jump's clear and re-block jumping.
	const UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	if (MoveComp && MoveComp->IsFalling())
	{
		return;
	}
	Crouch(); // no-op once already crouched; on landing this is what restores the crouch while the key is still held
}

void AFPSRCharacter::Input_Jump(const FInputActionValue& Value)
{
	if (IsMovementFrozen() || IsIncapacitatedLocal())
	{
		return;
	}
	// Read a jump press as "let go of crouch" (see the header). Order matters: clearing the intent first means the
	// jump check later this frame already sees an un-crouched player.
	UnCrouch();
	Jump();
}

void AFPSRCharacter::Input_CrouchReleased(const FInputActionValue& Value)
{
	// Deliberately NOT gated: a release must always land. If the freeze or a down happened while the key was held,
	// swallowing the release would leave bWantsToCrouch stuck on and the player permanently crouched.
	UnCrouch();
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
	// Mirror the ServerEquipSlot server gate: don't let an in-flight ADS RPC start an aim (or fire the OnAim behavior
	// hook) while the run is globally stopped (W1 P3-3). Input_ADS already gates client-side.
	//
	// The gate is DIRECTIONAL: a clear must always land, like Input_CrouchReleased's. The freeze-clear path
	// (HandleRunStateChanged_Vision) sends SetAiming(false) *after* the pause is already active, so gating the clear
	// too left the server latched in ADS with nothing to re-send it afterwards — invisible while this flag only fed
	// spread, but with the flag replicated it freezes a remote player's body in the aim pose for good.
	const bool bBlocked = IsRunFrozen() || IsIncapacitatedLocal();
	if (bNewAiming && bBlocked) { return; }
	if (WeaponFire)
	{
		WeaponFire->SetAiming(bNewAiming);
	}
	// The hook keeps its original gate, so the "no behavior hooks during a freeze" guarantee is unchanged. That is
	// only safe while OnAim is fire-only (below): a STATEFUL aim hook would need the suppressed false edge to clean
	// up, so making it stateful means revisiting this line, not just the hook.
	if (bBlocked) { return; }

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

void AFPSRCharacter::RefreshPawnCollisionResponse()
{
	UCapsuleComponent* Capsule = GetCapsuleComponent();
	if (!Capsule)
	{
		return;
	}
	const UWorld* World = GetWorld();
	const float Now = World ? World->GetTimeSeconds() : 0.0f;
	// The grace window derives from a server timestamp, so it composes with the downed state below — whichever ends
	// first doesn't restore enemy blocking while the other is still active.
	const bool bGrace = Now < GraceUntil;
	// DBNO/Dead: the downed body passes through the swarm (no physical block/push), matching the damage i-frames
	// (IsIncapacitatedLocal gates contact damage too). Reads the replicated LifeState, so it is valid on server and
	// the owning client, and composes with the grace window exactly like the grace window composes with it.
	const bool bDowned = IsIncapacitatedLocal();
	Capsule->SetCollisionResponseToChannel(ECC_Pawn, (bGrace || bDowned) ? ECR_Ignore : ECR_Block);
}

void AFPSRCharacter::BeginGraceWindow(float Seconds)
{
	UWorld* World = GetWorld();
	if (!HasAuthority() || Seconds <= 0.0f || !World)
	{
		return;
	}
	// Overlapping grace windows EXTEND, never shorten: a shorter grace (e.g. the 3s post-freeze resume grace) must not
	// cut an already-running longer one (post-revive, or a debug FPSR.Invuln fixture) — protection only ratchets up.
	const float NewUntil = World->GetTimeSeconds() + Seconds;
	if (NewUntil <= GraceUntil)
	{
		return;
	}
	GraceUntil = NewUntil;

	// Pass through enemy pawns for the grace window so the player can walk out of a surround — the swarm that downed
	// them (post-revive) or that closed in during the card freeze (post-freeze). The shared helper composes this with
	// the downed (DBNO/Dead) pass-through.
	RefreshPawnCollisionResponse();
	World->GetTimerManager().SetTimer(GraceTimerHandle, this, &AFPSRCharacter::EndGraceWindow, Seconds, false);
}

void AFPSRCharacter::EndGraceWindow()
{
	// Recompute rather than unconditionally block — the player may still be downed (DBNO/Dead pass-through).
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

	// Closed-loop director sensor (P0a-0): record the ACCEPTED incoming hit for IncomingDamageRate. Server-only;
	// the sensor classifies the source from DamageInstigator (enemy/boss counted; FF/self/door/mission/env
	// excluded) — the damage-bridge signature is unchanged. (RunFlow §2-8-2)
	if (World)
	{
		if (UFPSRDirectorSensorSubsystem* Sensor = World->GetSubsystem<UFPSRDirectorSensorSubsystem>())
		{
			Sensor->NotifyPlayerDamageTaken(this, DamageAmount, DamageInstigator);
		}
	}

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

		// Closed-loop director sensor (P0a-0): record the Alive->DBNO edge for DownedRecent. Server-only,
		// fires exactly once per down (the IsAlive guard above). (RunFlow §2-8-2)
		if (UWorld* SensorWorld = GetWorld())
		{
			if (UFPSRDirectorSensorSubsystem* Sensor = SensorWorld->GetSubsystem<UFPSRDirectorSensorSubsystem>())
			{
				Sensor->NotifyPlayerDowned(PS);
			}
		}
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

	// Card-select freeze. Order matters: withdraw this player's offer BEFORE recomputing. RefreshPauseState skips
	// non-Alive players, so the recompute can resume the run — and a still-presented offer would then sit over live
	// gameplay on a downed player who is no longer part of the selection, still acceptable by them. Withdrawing keeps
	// the pick (it is only consumed on apply), so the revive path re-presents it. Without the recompute the freeze
	// instead stays pinned on someone who can no longer choose — the same pull-recompute gap the GameMode's Logout
	// hook closes for disconnects. On a wipe the recompute is a no-op: EndRunFreeze has already latched bRunEnded,
	// so the world stays frozen behind the result screen.
	if (AFPSRPlayerController* FPSRPC = Cast<AFPSRPlayerController>(GetController()))
	{
		FPSRPC->WithdrawActiveOffer();
	}
	if (AFPSRGameState* GS = GetWorld() ? GetWorld()->GetGameState<AFPSRGameState>() : nullptr)
	{
		GS->RefreshPauseState();
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
	// PP and the authority-only movement halt (§2-2 freeze must stop residual velocity, not just gate new input).
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

	// Outside the IsLocallyControlled branch on purpose: this hook also fires when a pawn STOPS being locally
	// controlled, and the split has to be undone then or an unpossessed body stays invisible to its old owner.
	RefreshFirstPersonRendering();
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
	// paused. Input-driven moves already gate on IsMovementFrozen, but residual velocity that isn't input-driven (an
	// in-progress fall, and later any non-input locomotion state) would otherwise carry the player across the frozen
	// card screen — or across an active stage transition. Run on the authority (the server owns every pawn here; CMC
	// replicates the stop) and halt it. This handler now also fires on a stage-transition change (GameState::
	// ApplyStageTransitionLocal re-broadcasts OnRunStateChanged for exactly this reason).
	if (!HasAuthority())
	{
		return;
	}
	UWorld* World = GetWorld();
	const AFPSRGameState* GS = World ? World->GetGameState<AFPSRGameState>() : nullptr;
	const bool bPaused = GS && GS->IsRunPaused();
	// Movement-only freeze (mirrors IsMovementFrozen, computed locally since GS is already in hand): the card
	// freeze OR an active stage transition. Used ONLY for the stop-movement block below.
	const bool bMovementFrozen = bPaused || (GS && GS->IsStageTransitionActive());

	// Post-card-selection resume grace (§2-13): deliberately keyed on bPaused (IsRunPaused), NOT bMovementFrozen —
	// this grace protects coming OUT of the CARD screen specifically, where a player can be standing anywhere in an
	// unpredictable swarm. A stage-transition swap needs no such grace: the leftover swarm is released back to the
	// pool before the swap commits (UFPSRStageDirectorSubsystem::PerformSwap step 6), so there is nothing left
	// standing at the destination to ambush the teleported player.
	if (bWasRunPausedAuth && !bPaused)
	{
		BeginGraceWindow(PostFreezeInvulnSeconds);
	}
	bWasRunPausedAuth = bPaused;

	if (!bMovementFrozen)
	{
		return; // only act while movement is actually frozen (card freeze or transition); resume restores input control
	}

	GetCharacterMovement()->StopMovementImmediately(); // kill residual velocity so the player is stopped

	// Invariant 8 / movement-freeze parity: a slide or wall hang already in progress has to end too, whether the
	// freeze is the card screen or a stage transition — either way movement input is about to go dead. (The
	// movement component also self-exits on the same gate, so this is belt-and-braces for the authority side, which
	// is the one that matters for position.)
	if (UFPSRCharacterMovementComponent* FPSRMovement = GetFPSRMovement())
	{
		FPSRMovement->StopSliding();
		FPSRMovement->StopWallHang();
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

void AFPSRCharacter::RefreshBodyAnimLayer(const UFPSRWeaponDataAsset* Weapon)
{
	USkeletalMeshComponent* BodyMesh = GetMesh();
	if (!BodyMesh)
	{
		return;
	}

	// No authored layer is a NORMAL state, not an error: only the rifle animation set has been retargeted so far, and a
	// weapon without one correctly falls back to the body's own base pose.
	UClass* DesiredLayer = nullptr;
	if (Weapon && !Weapon->BodyAnimLayerClass.IsNull())
	{
		DesiredLayer = Weapon->BodyAnimLayerClass.LoadSynchronous();
		if (!DesiredLayer)
		{
			UE_LOG(LogFPSR, Warning,
				TEXT("[Anim] %s: body anim layer '%s' failed to load — falling back to the base body pose."),
				*GetNameSafe(Weapon), *Weapon->BodyAnimLayerClass.ToString());
		}
	}

	// RefreshEquippedWeaponVisual runs on far more than archetype changes (every equip, every parts rebuild), and
	// re-linking tears down and recreates the layer instance — which would restart its state machine mid-stride.
	if (LinkedBodyAnimLayerClass.Get() == DesiredLayer)
	{
		return;
	}

	// LinkAnimClassLayers has no "replace": linking a second set without unlinking the first leaves both running.
	if (LinkedBodyAnimLayerClass)
	{
		BodyMesh->UnlinkAnimClassLayers(LinkedBodyAnimLayerClass);
	}
	LinkedBodyAnimLayerClass = DesiredLayer;
	if (DesiredLayer)
	{
		BodyMesh->LinkAnimClassLayers(DesiredLayer);
	}

	// No main->layer injection step: UFPSRCharacterAnimInstance's main instance pushes its computed values into every
	// linked layer each frame (see its header). That also means this doesn't care how the layer functions are grouped.
}

void AFPSRCharacter::RefreshArmsAnimLayer(const UFPSRWeaponDataAsset* Weapon)
{
	// The arms' half of the same mechanism as RefreshBodyAnimLayer — deliberately a separate function over a separate
	// component with a separate DataAsset field, because the two graphs are independent by design (ADR 0003 invariant
	// 11). Merging them would be the first step back toward one pose serving both cameras, which is what this whole
	// change undid. Owner-machine only: on every other machine FirstPersonArms carries no mesh and no graph.
	if (!FirstPersonArms)
	{
		return;
	}

	UClass* DesiredLayer = nullptr;
	if (Weapon && !Weapon->ArmsAnimLayerClass.IsNull())
	{
		DesiredLayer = Weapon->ArmsAnimLayerClass.LoadSynchronous();
		if (!DesiredLayer)
		{
			UE_LOG(LogFPSR, Warning,
				TEXT("[Anim] %s: first-person arms layer '%s' failed to load — falling back to the arms' base pose."),
				*GetNameSafe(Weapon), *Weapon->ArmsAnimLayerClass.ToString());
		}
	}

	if (LinkedArmsAnimLayerClass.Get() == DesiredLayer)
	{
		return; // re-linking tears down and recreates the layer instance, restarting its state machine mid-stride
	}

	if (LinkedArmsAnimLayerClass)
	{
		FirstPersonArms->UnlinkAnimClassLayers(LinkedArmsAnimLayerClass);
	}
	LinkedArmsAnimLayerClass = DesiredLayer;
	if (DesiredLayer)
	{
		FirstPersonArms->LinkAnimClassLayers(DesiredLayer);
	}
}

void AFPSRCharacter::RefreshEquippedWeaponVisual()
{
	// Runs on EVERY machine. One weapon mesh serves the owner, a spectating downed teammate (§2-13 DBNO), and ordinary
	// remote observers alike (ADR 0002), so a client that skipped this would render a teammate holding nothing — which
	// is precisely the bug the old never-authored 3P mesh field caused. The cached fire cosmetics below are owner-only-
	// used (PlayWeaponFireCosmetics gates on IsLocallyControlled) so they are harmless on remote pawns.
	if (!WeaponInventory)
	{
		return;
	}

	const UFPSRWeaponDataAsset* Weapon = WeaponInventory->GetCurrentWeapon();

	// Before the no-weapon early-out below, deliberately: they have to run for the null case too. The layer decides the
	// POSE, so a stale rifle layer left linked after unequipping keeps a teammate posed around a gun that isn't there.
	RefreshBodyAnimLayer(Weapon);
	RefreshArmsAnimLayer(Weapon);

	// Reset cached fire cosmetics; repopulated below when a weapon is equipped.
	CachedFireMontage = nullptr;
	CachedReloadMontage = nullptr;
	CachedWeaponFireMontage = nullptr;
	CachedWeaponReloadMontage = nullptr;
	CachedArmsReloadMontage = nullptr;
	CachedFireSound = nullptr;
	CachedMuzzleFlash = nullptr;
	CachedMuzzleSocket = NAME_None;

	ActiveWeaponMesh = nullptr;
	CachedMuzzleComponent = nullptr;
	CachedAimComponent = nullptr;
	CachedLeftHandComponent = nullptr;
	CachedRightHandComponent = nullptr;

	// Reset ADS caching; the weapon settles back onto the grip socket when no weapon provides ADS.
	CachedAimSocket = NAME_None;
	CachedLeftHandSocket = NAME_None;
	CachedRightHandSocket = NAME_None;
	bCachedHasADS = false;
	bCachedADSAlignRotation = true;
	CachedADSAimRotationOffset = FRotator::ZeroRotator;
	bCachedSuppressFireMontagesWhileADS = true;

	// Reset hip procedural weapon motion (owner-local cosmetic); no weapon = no hip motion until the next equip.
	bCachedHasHipMotion = false;

	// Reset the holster-pose-authored flag (§6): no weapon = nothing for RefreshWeaponVisibility's delay gate to key
	// off, so it falls back to the legacy instant hide. Recomputed below when a weapon is actually equipped.
	bCachedHasHolsterPose = false;
	bCachedHasReloadPose = false;
	// Same for the temporary force-holster switch — "no weapon" must never inherit the last weapon's request.
	bCachedForceHolsteredPose = false;

	if (!Weapon)
	{
		// No weapon: hide the meshes and drop any modular parts.
		if (WeaponMesh) { WeaponMesh->SetSkeletalMeshAsset(nullptr); }
		if (WeaponMeshStatic) { WeaponMeshStatic->SetStaticMesh(nullptr); }
		RefreshWeaponPartComponents(nullptr);
		return;
	}

	// Skeletal weapon mesh (firearms) takes priority; static mesh (melee) is the fallback.
	USkeletalMesh* SkelMesh = Weapon->WeaponMesh.IsNull() ? nullptr : Weapon->WeaponMesh.LoadSynchronous();
	UStaticMesh* StaticMesh = (SkelMesh == nullptr && !Weapon->WeaponMeshStatic.IsNull())
		? Weapon->WeaponMeshStatic.LoadSynchronous() : nullptr;

	USkeletalMeshComponent* BodyMesh = GetMesh();

	if (WeaponMesh)
	{
		// SetSkeletalMeshAsset is the engine's documented setter (calls SetSkeletalMesh(NewMesh, false)) — UE5.7.
		// The weapon mesh has its OWN skeleton (SKEL_LPAMG_<W>), independent of the body skeleton it hangs off.
		WeaponMesh->SetSkeletalMeshAsset(SkelMesh);
		// Per-weapon WEAPON-mesh AnimBP so the bolt/magazine montages (A_FP_WEP_<W>_*) can play on the weapon's own
		// skeleton. Only a skeletal weapon has one; clear it otherwise so a static/next weapon keeps no stale bolt anim.
		if (SkelMesh && !Weapon->WeaponAnimInstanceClass.IsNull())
		{
			if (UClass* WeaponAnimClass = Weapon->WeaponAnimInstanceClass.LoadSynchronous())
			{
				WeaponMesh->SetAnimInstanceClass(WeaponAnimClass);
				// Inject the data-driven fire-part recoil params into the weapon AnimInstance (when it's our native base)
				// so the AnimBP's ModifyBone offset comes from the DA curve/distance/axis, not a per-AnimBP hardcode.
				if (UFPSRWeaponAnimInstance* WeaponAnimInst = Cast<UFPSRWeaponAnimInstance>(WeaponMesh->GetAnimInstance()))
				{
					WeaponAnimInst->SetFirePartRecoilParams(Weapon->FirePartRecoilCurve, Weapon->FirePartRecoilDistanceCm, Weapon->FirePartRecoilAxis);
				}
			}
		}
		else
		{
			WeaponMesh->SetAnimInstanceClass(nullptr);
		}
	}
	if (WeaponMeshStatic)
	{
		WeaponMeshStatic->SetStaticMesh(StaticMesh);
	}

	// Hand, socket, scale and render tag in one place — shared with the path that runs when the arms come up or go away
	// without the weapon changing (ADR 0003). Note this also re-forces weapon visibility, which the re-attach undoes.
	AttachWeaponMeshes();

	// Track which mesh is actually shown so fire cosmetics attach to it (skeletal firearm vs static melee/preview).
	ActiveWeaponMesh = SkelMesh ? Cast<UMeshComponent>(WeaponMesh)
		: (StaticMesh ? Cast<UMeshComponent>(WeaponMeshStatic) : nullptr);

	// The separate 3P weapon mesh (U19) and the per-weapon ARMS anim BP are both gone (ADR 0002). One weapon mesh now
	// hangs off the body for everyone, and the arms it used to animate no longer exist. The weapon's OWN anim BP
	// (bolt / charging handle) is unaffected — it lives on the weapon mesh and is applied above.

	// Cache per-shot fire cosmetics (resolve soft refs once, here, not per shot).
	CachedFireMontage = Weapon->FireMontage.IsNull() ? nullptr : Weapon->FireMontage.LoadSynchronous();
	CachedReloadMontage = Weapon->ReloadMontage.IsNull() ? nullptr : Weapon->ReloadMontage.LoadSynchronous();
	CachedWeaponFireMontage = Weapon->WeaponFireMontage.IsNull() ? nullptr : Weapon->WeaponFireMontage.LoadSynchronous();
	CachedWeaponReloadMontage = Weapon->WeaponReloadMontage.IsNull() ? nullptr : Weapon->WeaponReloadMontage.LoadSynchronous();
	CachedArmsReloadMontage = Weapon->ArmsReloadMontage.IsNull() ? nullptr : Weapon->ArmsReloadMontage.LoadSynchronous();
	CachedFireSound = Weapon->FireSound.IsNull() ? nullptr : Weapon->FireSound.LoadSynchronous();
	CachedMuzzleFlash = Weapon->MuzzleFlash.IsNull() ? nullptr : Weapon->MuzzleFlash.LoadSynchronous();
	CachedMuzzleSocket = Weapon->MuzzleSocket;
	CachedMuzzleFlashRotationOffset = Weapon->MuzzleFlashRotationOffset;

	// ADS params for the owner-local procedural aim-down-sights (UpdateAimDownSights). Cached BEFORE the parts rebuild
	// below, which resolves CachedAimComponent / CachedLeftHandComponent / CachedRightHandComponent by looking for
	// these socket names on the parts.
	CachedAimSocket = Weapon->AimSocket;
	CachedLeftHandSocket = Weapon->LeftHandSocket;
	CachedRightHandSocket = Weapon->RightHandSocket;
	CachedADSSightDistance = Weapon->ADSSightDistance;
	bCachedHasADS = Weapon->BaseStats.bHasADS;
	bCachedADSAlignRotation = Weapon->bADSAlignRotation;
	CachedADSAimRotationOffset = Weapon->ADSAimRotationOffset;
	bCachedSuppressFireMontagesWhileADS = Weapon->bSuppressFireMontagesWhileADS;
	CachedADSInterpSpeed = Weapon->BaseStats.ADSInterpSpeed;
	CachedADSSwayIdleScale = Weapon->ADSSwayIdleScale;
	CachedADSSwayFreeHorizontalCm = Weapon->ADSSwayFreeHorizontalCm;
	CachedADSSwayFreeVerticalCm = Weapon->ADSSwayFreeVerticalCm;
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

	// Holster pose authored? (§6) — either the offset or the tilt has to actually move the gun, AND the transition
	// has to take real time. A weapon with only a zero HolsterDuration authored (or vice versa) is indistinguishable
	// from "no holster pose" once RefreshWeaponVisibility starts keying off the ALPHA instead of the raw wall state —
	// so both have to be present together or this stays false and that function keeps the legacy instant hide (§5:
	// zero regression for every weapon that hasn't been authored yet).
	bCachedHasHolsterPose =
		(!CachedHipMotion.HolsterPose.Offset.IsNearlyZero() || !CachedHipMotion.HolsterPose.Tilt.IsNearlyZero())
		&& CachedHipMotion.HolsterDuration > 0.0f;

	// 🚧 Reload pose authored? (see ReloadPose's DataAsset comment) — read straight off Weapon->ProceduralWeaponMotion,
	// same as bCachedForceHolsteredPose just below, not off CachedHipMotion: bCachedHasReloadPose is a dedicated
	// cached bool precisely so it (unlike CachedHipMotion) gets cleared on unequip — see its header comment.
	bCachedHasReloadPose =
		!Weapon->ProceduralWeaponMotion.ReloadPose.Offset.IsNearlyZero()
		|| !Weapon->ProceduralWeaponMotion.ReloadPose.Tilt.IsNearlyZero();

	// 🚧 Temporary (see the DataAsset field): this weapon has no authored first-person idle, so keep it holstered for
	// as long as it is held rather than showing the arms in whatever pose the graph falls back to.
	// Kept as its OWN cached bool rather than read straight off CachedHipMotion (where the field lives): that struct is
	// only assigned when a weapon EXISTS and is never cleared on unequip, so reading it there would let the last
	// weapon's request keep driving the holster alpha with nothing equipped. This one is reset in the block above.
	bCachedForceHolsteredPose = Weapon->ProceduralWeaponMotion.bForceHolsteredPose;

	// Snap (not smooth-ramp) both state alphas to whatever the CURRENT movement state already is, rather than
	// starting them both at 0 on every equip. This runs on a weapon SWAP too (not just the first equip), and a
	// mid-wall or mid-air swap must not replay the NEW weapon's holster-descend / airborne-rise animation from
	// scratch — the movement STATE is continuous across the swap (still on the wall, still falling), only the
	// weapon changed, so the pose should already read as "in that state" the instant the new gun appears. This snap
	// point doubles as the hook a future "play a holster motion ON WEAPON SWAP itself" feature would extend from
	// (out of scope here — deliberately not built, just noted so it isn't rediscovered as a mystery gap later).
	const UFPSRCharacterMovementComponent* SwapMove = GetFPSRMovement();
	// Holster snaps in ONE direction only — toward stowed. Snapping to 0 as well would undo the very thing this snap
	// exists for the moment a swap happens on the ground: the alpha would jump to "drawn" and the new weapon would
	// appear fully in-hand instead of rising into view. Letting the DRAW direction blend instead means a ground swap
	// now reads as stow-then-draw for free (the "holster motion on weapon swap" extension previously noted here as
	// out of scope — it falls out of this rule rather than needing its own machinery), while the wall/air case still
	// snaps because there the state itself is continuous across the swap and replaying a descent would be the pop.
	if (SwapMove && !SwapMove->CanFireInCurrentState())
	{
		HolsterBlendAlpha = 1.0f;
	}
	AirborneBlendAlpha = (SwapMove && SwapMove->IsFalling()) ? 1.0f : 0.0f;

	// Rebuild modular cosmetic parts on the (skeletal) weapon mesh from the weapon's part list.
	RefreshWeaponPartComponents(Weapon);

	// An ADS weapon whose AimSocket can't actually resolve fails completely silently today: the bAiming gate in
	// UpdateAimDownSights just goes false, but IsADSFOVActive (which drives the FOV zoom) is AimSocket-independent,
	// so the camera still zooms in while the sight never lines up — reads as "ADS works but is off" instead of the
	// DA misconfiguration it actually is. Checked once per equip, right here (not in the per-frame bAiming gate,
	// which is a hot path): this is the first point where the answer is knowable, because CachedAimComponent is only
	// resolved by RefreshWeaponPartComponents just above (RebuildPartsFromSelection finds the part carrying
	// AimSocket, or leaves it null to fall back to the receiver — the same resolution UpdateAimDownSights's AimComp
	// uses). Gated to the owning client only: RefreshEquippedWeaponVisual runs on EVERY machine (server + every
	// observer, per this function's own header comment), but CachedAimSocket/CachedAimComponent are read only by
	// UpdateAimDownSights, which is itself owner-local-only (its own early-out: !IsLocallyControlled() || ... ||
	// NM_DedicatedServer) — warning from the server or a remote sim proxy here would just duplicate the same report
	// once per connected machine for state nobody there ever reads.
	if (IsLocallyControlled() && bCachedHasADS)
	{
		if (CachedAimSocket.IsNone())
		{
			UE_LOG(LogFPSR, Warning, TEXT("%s: '%s' has ADS enabled but no AimSocket is set — ADS will FOV-zoom without the sight ever aligning."),
				*GetName(), *Weapon->GetName());
		}
		else
		{
			// Same fallback UpdateAimDownSights uses for its own AimComp: prefer the part carrying AimSocket, else
			// the receiver (ActiveWeaponMesh, already set above by the SkelMesh/StaticMesh assignment this function did earlier).
			UMeshComponent* ResolvedAimComp = CachedAimComponent;
			if (!ResolvedAimComp)
			{
				ResolvedAimComp = ActiveWeaponMesh;
			}
			if (!ResolvedAimComp || !ResolvedAimComp->DoesSocketExist(CachedAimSocket))
			{
				UE_LOG(LogFPSR, Warning, TEXT("%s: '%s' AimSocket '%s' not found on %s — ADS will FOV-zoom without the sight ever aligning."),
					*GetName(), *Weapon->GetName(), *CachedAimSocket.ToString(),
					ResolvedAimComp ? *ResolvedAimComp->GetName() : TEXT("<no weapon mesh>"));
			}
		}
	}

	// Optional equip montage on the body. This function runs on every machine, so the swap now reads for remote
	// observers too instead of being an owner-only flourish (one mesh, one anim graph — invariant 4).
	if (BodyMesh && !Weapon->EquipMontage.IsNull())
	{
		if (UAnimMontage* EquipM = Weapon->EquipMontage.LoadSynchronous())
		{
			if (UAnimInstance* AnimInst = BodyMesh->GetAnimInstance())
			{
				AnimInst->Montage_Play(EquipM);
			}
		}
	}

	// The same swap on the first-person arms, for the one machine that has them. Separate asset, separate skeleton —
	// see ArmsEquipMontage in the DataAsset for why that is not a duplicated field.
	if (bFirstPersonSplitActive && FirstPersonArms && !Weapon->ArmsEquipMontage.IsNull())
	{
		if (UAnimMontage* ArmsEquipM = Weapon->ArmsEquipMontage.LoadSynchronous())
		{
			if (UAnimInstance* ArmsAnimInst = FirstPersonArms->GetAnimInstance())
			{
				ArmsAnimInst->Montage_Play(ArmsEquipM);
			}
		}
	}

	// The meshes above were just re-pointed and re-attached, which restores them to visible. Forced, because the latch
	// may already read "hidden" from the wall the character is on — an equip arriving after the wall (they replicate
	// on different paths and can land in either order) would otherwise leave the gun visible for the whole hold.
	RefreshWeaponVisibility(/*bForce=*/true);

	// The equipped weapon (and therefore its grip sockets) just changed. AttachWeaponMeshes and RefreshWeaponPartComponents
	// above already refresh this cache on their own exit, so this call is normally a no-op re-solve — kept anyway so
	// this function's own reassignment of CachedLeftHandSocket/CachedRightHandSocket above is never the one path that
	// forgets to.
	RefreshHandGripInGunFrameCache();
}

void AFPSRCharacter::RefreshWeaponPartComponents(const UFPSRWeaponDataAsset* Weapon)
{
	// No weapon / non-skeletal: tear down and reset signature. Parts attach to the SKELETAL weapon mesh only —
	// static/melee/preview weapons carry no modular parts, and the pack part sockets live on SKEL_LPAMG_<W>.
	// ActiveWeaponMesh == WeaponMesh means a skeletal weapon is shown.
	if (!Weapon || !WeaponMesh || ActiveWeaponMesh != WeaponMesh)
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
	for (UStaticMeshComponent* Part : WeaponPartComponents)
	{
		if (Part)
		{
			Part->DestroyComponent();
		}
	}
	WeaponPartComponents.Reset();
	// P2 (GunMotionTool_Spec.md §4-2): index-aligned with WeaponPartComponents above — reset alongside it so a
	// rebuild never reads a stale base offset / curve-name set against a mismatched index.
	WeaponPartBaseOffsets.Reset();
	WeaponPartCurveNames.Reset();

	// Reset the modular muzzle/aim/left-hand source caches HERE (both equip + modifier-change paths share this
	// invariant) so a slot swap that drops the socket-carrying part can't leave a dangling pointer to a destroyed
	// component.
	CachedMuzzleComponent = nullptr;
	CachedAimComponent = nullptr;
	CachedLeftHandComponent = nullptr;
	CachedRightHandComponent = nullptr;
	CachedScopeDescriptor = FFPSRWeaponScopeDescriptor();

	// Parts attach to the skeletal weapon mesh only (guarded by the caller: ActiveWeaponMesh == WeaponMesh).
	if (!WeaponMesh)
	{
		return;
	}

	// Parallel to WeaponPartComponents (both appended together in the loop): each attached part's scope descriptor,
	// so the aim-resolution below can capture the ACTIVE sight's descriptor by index. (W-U2)
	TArray<FFPSRWeaponScopeDescriptor> AddedScopeDescriptors;
	AddedScopeDescriptors.Reserve(Selected.Num());

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
		// No visibility override (ADR 0002): parts are visible to whoever can see the weapon they hang off, which is now
		// everyone. The old SetOnlyOwnerSee(true) existed only to match an owner-only 1P weapon mesh.
		PartComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		// Match the weapon it hangs off: the render tag isn't inherited through attachment, so a part created while the
		// first-person arms are up would otherwise cast a world shadow from camera space (ADR 0003).
		PartComp->SetFirstPersonPrimitiveType(GetWeaponFirstPersonPrimitiveType());
		PartComp->RegisterComponent();
		PartComp->AttachToComponent(WeaponMesh, FAttachmentTransformRules::KeepRelativeTransform, PartDef.Socket);
		PartComp->SetRelativeTransform(PartDef.Offset);
		WeaponPartComponents.Add(PartComp);
		AddedScopeDescriptors.Add(PartDef.Scope);
		// P2 (GunMotionTool_Spec.md §4-2): snapshot the authored base placement and pre-build this part's curve
		// names — index-aligned with WeaponPartComponents (appended together, same as AddedScopeDescriptors above).
		WeaponPartBaseOffsets.Add(PartDef.Offset);
		WeaponPartCurveNames.Add(FPSRGunMotionCurves::MakePartCurveNames(PartDef.Socket));
	}

	// Re-resolve modular muzzle source: the muzzle socket lives on a cosmetic part (barrel/forestock), so prefer the
	// part component that carries CachedMuzzleSocket — swapping that part then moves the muzzle. When no part provides
	// it, CachedMuzzleComponent stays null and the fire site falls back to the receiver (ActiveWeaponMesh). Convention-
	// based: the part whose mesh owns a socket named MuzzleSocket wins, so no extra DA field is needed.
	if (!CachedMuzzleSocket.IsNone())
	{
		for (UStaticMeshComponent* Part : WeaponPartComponents)
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
	// receiver.
	if (!CachedAimSocket.IsNone())
	{
		for (int32 i = 0; i < WeaponPartComponents.Num(); ++i)
		{
			UStaticMeshComponent* Part = WeaponPartComponents[i];
			if (Part && Part->DoesSocketExist(CachedAimSocket))
			{
				CachedAimComponent = Part;
				// Capture the active sight's scope descriptor (W-U2) so the owner-local ADS visual path can drive the
				// full-screen scope. AddedScopeDescriptors is index-aligned with WeaponPartComponents.
				if (AddedScopeDescriptors.IsValidIndex(i))
				{
					CachedScopeDescriptor = AddedScopeDescriptors[i];
				}
				break;
			}
		}
	}

	// Re-resolve the left-hand grip source, same shape again: the grip sits on the HANDGUARD part, so swapping the
	// handguard moves where the left hand should land. Null when no part carries it — the pack's bullpup handguards
	// (Weapon_B) author no SOCKET_LeftHand at all, so the AnimBP has to survive "no target" as a normal case, not an
	// error. (ADR 0002 step 4 seam)
	if (!CachedLeftHandSocket.IsNone())
	{
		for (UStaticMeshComponent* Part : WeaponPartComponents)
		{
			if (Part && Part->DoesSocketExist(CachedLeftHandSocket))
			{
				CachedLeftHandComponent = Part;
				break;
			}
		}
	}

	// Right-hand mirror of the left-hand re-resolve above (fparms-gunanchor-ik) — same reasoning: the grip socket may
	// live on a part instead of the receiver.
	if (!CachedRightHandSocket.IsNone())
	{
		for (UStaticMeshComponent* Part : WeaponPartComponents)
		{
			if (Part && Part->DoesSocketExist(CachedRightHandSocket))
			{
				CachedRightHandComponent = Part;
				break;
			}
		}
	}

	// The parts (and therefore which component carries each grip socket) just changed — refresh the gun-frame grip
	// cache rather than re-solving it every animation frame.
	RefreshHandGripInGunFrameCache();
	// P2 (GunMotionTool_Spec.md §4-2): same lifecycle point — the attached parts (and their base offsets/curve names
	// above) just changed, so their gun-space frames need rebuilding too.
	RefreshPartFramesInGunSpaceCache();
}

UMeshComponent* AFPSRCharacter::ResolveLeftHandGripComponent() const
{
	if (CachedLeftHandSocket.IsNone())
	{
		return nullptr; // weapon authors no left-hand grip (melee / one-handed / unarmed)
	}
	// The handguard part that carries the socket wins; otherwise the receiver may carry it directly.
	if (CachedLeftHandComponent)
	{
		return CachedLeftHandComponent;
	}
	if (ActiveWeaponMesh && ActiveWeaponMesh->DoesSocketExist(CachedLeftHandSocket))
	{
		return ActiveWeaponMesh;
	}
	return nullptr;
}

UMeshComponent* AFPSRCharacter::ResolveRightHandGripComponent() const
{
	// Right-hand mirror of ResolveLeftHandGripComponent above — identical shape, CachedRightHandSocket instead.
	if (CachedRightHandSocket.IsNone())
	{
		return nullptr; // weapon authors no right-hand grip
	}
	if (CachedRightHandComponent)
	{
		return CachedRightHandComponent;
	}
	if (ActiveWeaponMesh && ActiveWeaponMesh->DoesSocketExist(CachedRightHandSocket))
	{
		return ActiveWeaponMesh;
	}
	return nullptr;
}

bool AFPSRCharacter::GetLeftHandGripTransform(const USceneComponent* ForMesh, FTransform& OutGripWorld) const
{
	const UMeshComponent* GripComp = ResolveLeftHandGripComponent();
	if (!GripComp)
	{
		OutGripWorld = FTransform::Identity;
		return false;
	}

	// Only answer for the mesh the grip is actually ON. The weapon lives in CAMERA space while the first-person arms
	// hold it, and a body that reached for it anyway would swing its left arm toward the camera — which the owner sees,
	// because that body is casting their shadow. Asking "is it attached to me" keeps each graph ignorant of the other
	// (ADR 0003 invariant 11): neither mesh has to know the split exists, only whether it is the one holding the gun.
	if (!ForMesh || !GripComp->IsAttachedTo(ForMesh))
	{
		OutGripWorld = FTransform::Identity;
		return false;
	}

	// LeftHandGripOffset is applied in the grip component's space — the space a socket's RelativeLocation is authored
	// in — so a value found by nudging the socket in the editor can be moved here unchanged. Composing to world after
	// the nudge (rather than offsetting the world result) keeps it rotating with the weapon, which is what "1cm toward
	// the trigger" has to mean when the gun is canted during ADS.
	FTransform Grip = GripComp->GetSocketTransform(CachedLeftHandSocket, RTS_Component);
	Grip.AddToTranslation(LeftHandGripOffset);
	OutGripWorld = Grip * GripComp->GetComponentTransform();
	return true;
}

bool AFPSRCharacter::ComputeGripInGunFrame(UMeshComponent* GripComp, FName GripSocket, const FVector& GripOffset,
	const USceneComponent* ForMesh, FTransform& OutGripInGun) const
{
	OutGripInGun = FTransform::Identity;
	UMeshComponent* const WeaponRoot = ActiveWeaponMesh;
	if (!GripComp || !WeaponRoot || !FirstPersonArms)
	{
		return false;
	}

	// Same reason as GetLeftHandGripTransform above: only answer for the mesh the grip is actually ON (ADR 0003
	// invariant 11) — neither caller has to know the other exists, only whether it is the one holding the gun.
	if (!ForMesh || !GripComp->IsAttachedTo(ForMesh))
	{
		return false;
	}

	// Grip point in GripComp's OWN space. RTS_Component walks GripComp's own bone chain (if it has one) — never the
	// arms' — so nothing here depends on how the ARMS are currently posed: a static part is trivially pose-free, and
	// the receiver's own grip socket is authored on its (effectively rigid) root, same assumption GetLeftHandGripTransform
	// already relies on above.
	//
	// Dev-convenience contract check (no #if needed — this function only runs on attach/part-change events, never per
	// frame, so the cost is irrelevant either way): if GripSocket is parented to an ANIMATED bone on a skeletal
	// receiver (bolt/mag/slide) instead of the root, RTS_Component below folds in whatever pose is live AT THIS
	// INSTANT — and because a reload's part-rebuild can land mid-animation, that pose would get baked into the cache
	// as a permanent offset. Today's weapon data authors grip sockets on the root, so this is safe in practice, but
	// nothing enforces it — warn rather than let a future weapon silently violate it.
	if (const USkinnedMeshComponent* SkinnedGripComp = Cast<USkinnedMeshComponent>(GripComp))
	{
		FTransform GripSocketLocalTransform = FTransform::Identity;
		int32 GripSocketBoneIndex = INDEX_NONE;
		if (SkinnedGripComp->GetSocketInfoByName(GripSocket, GripSocketLocalTransform, GripSocketBoneIndex) && GripSocketBoneIndex != 0)
		{
			UE_LOG(LogFPSR, Warning, TEXT("%s: ComputeGripInGunFrame — grip socket '%s' on %s is parented to bone index %d ('%s'), not the root. If that bone animates (bolt/mag/slide), the cached gun-frame grip will bake in whatever pose was live at the last attach/part-rebuild instead of staying pose-independent."),
				*GetName(), *GripSocket.ToString(), *SkinnedGripComp->GetName(), GripSocketBoneIndex, *SkinnedGripComp->GetBoneName(GripSocketBoneIndex).ToString());
		}
	}

	FTransform X = GripComp->GetSocketTransform(GripSocket, RTS_Component);
	X.AddToTranslation(GripOffset);

	// Climb the attachment chain from GripComp up to (not including) WeaponRoot, staying in RTS_Component space the
	// whole way — never RTS_World — so nothing on this leg reads a posed transform either. GripComp == WeaponRoot
	// (the receiver holding the grip socket directly — the current Rifle path) takes this loop 0 times.
	USceneComponent* Comp = GripComp;
	while (Comp != WeaponRoot)
	{
		USceneComponent* Parent = Comp->GetAttachParent();
		if (!Parent)
		{
			// Chain ran out before reaching WeaponRoot — shouldn't happen given the IsAttachedTo(ForMesh) check above,
			// but refuse rather than compose a meaningless partial transform.
			OutGripInGun = FTransform::Identity;
			return false;
		}
		FTransform HopToParent = Comp->GetRelativeTransform();
		if (!Comp->GetAttachSocketName().IsNone())
		{
			// Comp's RelativeTransform is relative to the SOCKET it rides, not to Parent's own origin — bridge socket-
			// to-origin in the same RTS_Component space (engine convention: World = Relative * SocketTransform; here
			// substituting RTS_Component for RTS_World throughout keeps the whole chain pose-independent).
			HopToParent = HopToParent * Parent->GetSocketTransform(Comp->GetAttachSocketName(), RTS_Component);
		}
		X = X * HopToParent;
		Comp = Parent;
	}

	// Weapon -> gun frame (ik_hand_gun bone space == hand_r bone space once the animgraph's CopyBone runs) —
	// extracted into GetWeaponRootPlacementInGunFrame so future gun-frame consumers reuse the same hop.
	FTransform WeaponInGunFrame;
	if (!GetWeaponRootPlacementInGunFrame(WeaponInGunFrame))
	{
		// Can't happen given the WeaponRoot/FirstPersonArms null-checks already passed above, but stay consistent
		// with "refuse rather than compose a meaningless partial transform" (see the chain-ran-out branch above).
		OutGripInGun = FTransform::Identity;
		return false;
	}

	OutGripInGun = X * WeaponInGunFrame;
	// Scale is carried through the composition above (it affects the composed POSITION via WeaponAttachScale), and
	// normalized only here at the very end — an effector reads location/rotation only, but zeroing scale mid-
	// composition would silently drop that scale from the result instead of just from the output.
	OutGripInGun.SetScale3D(FVector::OneVector);
	return true;
}

bool AFPSRCharacter::GetWeaponRootPlacementInGunFrame(FTransform& OutWeaponInGunFrame) const
{
	// Extracted from ComputeGripInGunFrame. Read which scheme AttachWeaponMeshes actually used for the CURRENT
	// attachment rather than assume one:
	//  - ik_hand_gun bone anchor (see AttachWeaponMeshes): WeaponRoot's own RelativeTransform IS ALREADY the fixed
	//    gun-frame offset — no further hop needed, and no further hop would be SAFE (ik_hand_gun's live bone transform
	//    is posed — exactly the pose-dependence this function must not touch).
	//  - Fallback Snap-on-AttachSocket (body, or an arms mesh with no ik_hand_gun bone): RelativeTransform is only
	//    relative to that SOCKET, so one more fixed hop through the socket's own RTS_ParentBoneSpace offset finishes
	//    the trip into hand_r's frame.
	// Reading WeaponRoot's OWN live attach-socket name (rather than assuming which path ran) is what keeps this correct
	// under either AttachWeaponMeshes path without the caller having to know which one is live.
	OutWeaponInGunFrame = FTransform::Identity;
	UMeshComponent* const WeaponRoot = ActiveWeaponMesh;
	if (!WeaponRoot || !FirstPersonArms)
	{
		return false;
	}

	// State-pose-on-weapon refactor (⚠️ caching-contamination trap — see CachedWeaponAttachRelative's header comment):
	// this function is ONLY ever called from cache-rebuild sites (RefreshHandGripInGunFrameCache via
	// ComputeGripInGunFrame, RefreshPartFramesInGunSpaceCache) — attach/part-change events, not per animation frame.
	// On the gun-anchor path, WeaponRoot->GetRelativeTransform() is no longer purely the fixed attach offset:
	// ApplyWeaponStatePose (AFPSRCharacter::Tick) writes a per-frame state-pose delta on top of it. Reading that LIVE
	// value here would let whichever state pose happens to be riding the weapon AT THE INSTANT a rebuild fires (e.g. a
	// reload's mid-animation part rebuild while a slide pose is live) get folded into a cache that is not re-solved
	// again until the NEXT rebuild — permanently baking in a transient pose. CachedWeaponAttachRelative is the base
	// BEFORE that delta, so reading it instead keeps this function's contract (a pose-free CONSTANT) intact. The
	// fallback path has no such cache contamination risk (nothing writes a relative delta there — see
	// ApplyWeaponStatePose's own gate) and keeps the live read it always had.
	const FTransform WeaponRel = bWeaponAttachIsGunAnchor ? CachedWeaponAttachRelative : WeaponRoot->GetRelativeTransform();
	OutWeaponInGunFrame = WeaponRel;
	if (IkHandGunBoneName.IsNone() || WeaponRoot->GetAttachSocketName() != IkHandGunBoneName)
	{
		const UFPSRWeaponDataAsset* Weapon = WeaponInventory ? WeaponInventory->GetCurrentWeapon() : nullptr;
		const FName AttachSocket = ResolveWeaponAttachSocket(Weapon);
		OutWeaponInGunFrame = WeaponRel * FirstPersonArms->GetSocketTransform(AttachSocket, RTS_ParentBoneSpace);
	}
	return true;
}

void AFPSRCharacter::RefreshHandGripInGunFrameCache()
{
	// Gun-anchor IK (fparms-gunanchor-ik): the grip-in-gun-frame is a pose-free CONSTANT (see ComputeGripInGunFrame),
	// so it is solved HERE — on attach/part changes — and cached, instead of every animation frame. Always solved
	// against the arms: the ik_hand_gun rig this exists for only lives there (see GetRightHandGripInGunFrame).
	FTransform Grip;
	CachedRightGripInGun = ComputeGripInGunFrame(ResolveRightHandGripComponent(), CachedRightHandSocket, RightHandGripOffset, FirstPersonArms, Grip)
		? TOptional<FTransform>(Grip)
		: TOptional<FTransform>();

	CachedLeftGripInGun = ComputeGripInGunFrame(ResolveLeftHandGripComponent(), CachedLeftHandSocket, LeftHandGripOffset, FirstPersonArms, Grip)
		? TOptional<FTransform>(Grip)
		: TOptional<FTransform>();
}

bool AFPSRCharacter::GetRightHandGripInGunFrame(const USceneComponent* ForMesh, FTransform& OutGripInGun) const
{
	// Cache reader only — RefreshHandGripInGunFrameCache is where this is actually solved (on attach/part changes,
	// not every frame). The cache is always solved against the arms (the only mesh with an ik_hand_gun rig), so
	// ForMesh has to be exactly that for a hit — the same "is it on ME" gate GetLeftHandGripTransform enforces live,
	// just pre-baked at cache-solve time instead of re-walked on every call.
	if (!ForMesh || ForMesh != FirstPersonArms || !CachedRightGripInGun.IsSet())
	{
		OutGripInGun = FTransform::Identity;
		return false;
	}
	OutGripInGun = CachedRightGripInGun.GetValue();
	// State-pose-on-weapon correction (§③, gun-space state-pose refactor): CachedRightGripInGun was solved against
	// CachedWeaponAttachRelative (the gun-anchor BASE — see GetWeaponRootPlacementInGunFrame). If a slide/airborne/
	// holster pose is riding on top of that base right now (ApplyWeaponStatePose, AFPSRCharacter::Tick, which runs
	// BEFORE this read this same frame — see its own header comment), the grip has moved with the weapon and the
	// cached value alone would leave the hand behind — worst on the LEFT hand (forward grip, furthest from the gun's
	// own origin; the right hand's grip sits close to it and moves comparatively little). CachedWeaponStatePoseDelta
	// = Base^-1 * Final folds that motion back in with one multiply instead of re-solving the whole chain here.
	if (!CachedWeaponStatePoseDelta.Equals(FTransform::Identity))
	{
		OutGripInGun = OutGripInGun * CachedWeaponStatePoseDelta;
	}
	return true;
}

bool AFPSRCharacter::GetLeftHandGripInGunFrame(const USceneComponent* ForMesh, FTransform& OutGripInGun) const
{
	// Left-hand mirror of GetRightHandGripInGunFrame above (including the state-pose correction).
	if (!ForMesh || ForMesh != FirstPersonArms || !CachedLeftGripInGun.IsSet())
	{
		OutGripInGun = FTransform::Identity;
		return false;
	}
	OutGripInGun = CachedLeftGripInGun.GetValue();
	if (!CachedWeaponStatePoseDelta.Equals(FTransform::Identity))
	{
		OutGripInGun = OutGripInGun * CachedWeaponStatePoseDelta;
	}
	return true;
}

namespace
{
	/** P2 (GunMotionTool_Spec.md §4-1/§4-2): read the 6 FPGM_P_* curves for one part and return the local delta
	 *  (translation = additive, rotation = quaternion to left-multiply). Returns true if ANY of the 6 is currently
	 *  authored — a TX-only gate would silently skip a clip that only authored TZ, which is exactly the bug the spec
	 *  calls out ("6커브 중 하나라도 있으면 적용 — TX 단독 게이트 금지"). */
	bool ReadPartCurveDelta(UAnimInstance* ArmsAnim, const FPSRGunMotionCurves::FPartCurveNames& Names,
		FVector& OutLoc, FQuat& OutRot)
	{
		float TX = 0.0f, TY = 0.0f, TZ = 0.0f, RP = 0.0f, RY = 0.0f, RR = 0.0f;
		bool bAny = false;
		bAny |= ArmsAnim->GetCurveValue(Names.TX, TX);
		bAny |= ArmsAnim->GetCurveValue(Names.TY, TY);
		bAny |= ArmsAnim->GetCurveValue(Names.TZ, TZ);
		bAny |= ArmsAnim->GetCurveValue(Names.RP, RP);
		bAny |= ArmsAnim->GetCurveValue(Names.RY, RY);
		bAny |= ArmsAnim->GetCurveValue(Names.RR, RR);
		OutLoc = FVector(TX, TY, TZ);
		OutRot = FRotator(RP, RY, RR).Quaternion();
		return bAny;
	}
}

bool AFPSRCharacter::GetWeaponPartFrameInGunSpace(FName AttachSocket, FTransform& OutFrame) const
{
	OutFrame = FTransform::Identity;
	if (AttachSocket.IsNone())
	{
		return false;
	}
	const FTransform* Cached = CachedPartFramesInGunSpace.Find(AttachSocket);
	if (!Cached)
	{
		return false;
	}
	OutFrame = *Cached;

	// §4-1 Blend semantics (measured spec gap): the hand needs to follow the part's LIVE motion, not its static
	// authored placement — if a magazine is mid-eject when a hand attaches to it, snapping the hand to the rest frame
	// defeats the point of attaching at all. Cached = Base * T (T = socket+gun composition), so
	// Live = Result * Base^-1 * Cached — T is never needed to derive it. Same composition rule as
	// ApplyWeaponPartCurves (they share ReadPartCurveDelta).
	//
	// State-pose-on-weapon correction (§③): every OutFrame this function can return is ultimately derived from
	// *Cached, which — like the grip caches — was solved against CachedWeaponAttachRelative (the gun-anchor base,
	// via GetWeaponRootPlacementInGunFrame). CachedWeaponStatePoseDelta (Base^-1 * Final, recomputed once per tick by
	// ApplyWeaponStatePose) folds any currently-riding state pose back in with one trailing multiply — same
	// GetRightHandGripInGunFrame idiom, applied at each return below rather than once at the end because this
	// function has multiple early-outs on the pure-cache value.
	UAnimInstance* ArmsAnim = (bFirstPersonSplitActive && FirstPersonArms) ? FirstPersonArms->GetAnimInstance() : nullptr;
	if (!ArmsAnim)
	{
		if (!CachedWeaponStatePoseDelta.Equals(FTransform::Identity))
		{
			OutFrame = OutFrame * CachedWeaponStatePoseDelta;
		}
		return true;
	}
	for (int32 i = 0; i < WeaponPartComponents.Num(); ++i)
	{
		const UStaticMeshComponent* Part = WeaponPartComponents[i];
		if (!Part || Part->GetAttachSocketName() != AttachSocket
			|| !WeaponPartCurveNames.IsValidIndex(i) || !WeaponPartBaseOffsets.IsValidIndex(i))
		{
			continue;
		}
		FVector DeltaLoc; FQuat DeltaRot;
		if (!ReadPartCurveDelta(ArmsAnim, WeaponPartCurveNames[i], DeltaLoc, DeltaRot))
		{
			// 이 파츠에 커브 없음 — 정적 프레임 그대로(단, 상태포즈 보정은 적용).
			if (!CachedWeaponStatePoseDelta.Equals(FTransform::Identity))
			{
				OutFrame = OutFrame * CachedWeaponStatePoseDelta;
			}
			return true;
		}
		const FTransform& Base = WeaponPartBaseOffsets[i];
		FTransform Result = Base;
		Result.SetLocation(Base.GetLocation() + DeltaLoc);
		Result.SetRotation((DeltaRot * Base.GetRotation()).GetNormalized());
		OutFrame = Result * Base.Inverse() * (*Cached);
		if (!CachedWeaponStatePoseDelta.Equals(FTransform::Identity))
		{
			OutFrame = OutFrame * CachedWeaponStatePoseDelta;
		}
		return true;
	}
	if (!CachedWeaponStatePoseDelta.Equals(FTransform::Identity))
	{
		OutFrame = OutFrame * CachedWeaponStatePoseDelta;
	}
	return true;
}

void AFPSRCharacter::RefreshPartFramesInGunSpaceCache()
{
	CachedPartFramesInGunSpace.Reset();

	FTransform WeaponInGunFrame;
	if (!WeaponMesh || !GetWeaponRootPlacementInGunFrame(WeaponInGunFrame))
	{
		return;
	}

	for (int32 i = 0; i < WeaponPartComponents.Num(); ++i)
	{
		UStaticMeshComponent* Part = WeaponPartComponents[i];
		if (!Part || !WeaponPartBaseOffsets.IsValidIndex(i))
		{
			continue;
		}
		const FName Socket = Part->GetAttachSocketName();
		if (Socket.IsNone())
		{
			continue;
		}

		// Part's AUTHORED placement (WeaponPartBaseOffsets[i] — "socket + Offset", BEFORE any FPGM_P_* curve is
		// applied by ApplyWeaponPartCurves) relative to the socket, hopped into WeaponMesh's own RTS_Component space,
		// then composed through the SAME WeaponInGunFrame tail ComputeGripInGunFrame uses for a grip point — landing
		// this in the EXACT SAME frame as CachedRightGripInGun / CachedLeftGripInGun (ik_hand_gun bone-parent space),
		// which is what lets the arms AnimInstance lerp/slerp between a grip target and a part target (§2-1 Blend
		// semantics) without a space mismatch.
		const FTransform PartInWeaponSpace = WeaponPartBaseOffsets[i] * WeaponMesh->GetSocketTransform(Socket, RTS_Component);
		CachedPartFramesInGunSpace.Add(Socket, PartInWeaponSpace * WeaponInGunFrame);
	}
}

void AFPSRCharacter::ApplyWeaponPartCurves()
{
	// Same gate UpdateAimDownSights uses for "am I the one solving against the arms": no split, no arms, nothing to
	// apply curves to (§4-2).
	if (!bFirstPersonSplitActive || !FirstPersonArms || WeaponPartComponents.Num() == 0)
	{
		return;
	}
	UAnimInstance* ArmsAnim = FirstPersonArms->GetAnimInstance();
	if (!ArmsAnim)
	{
		return;
	}

	for (int32 i = 0; i < WeaponPartComponents.Num(); ++i)
	{
		UStaticMeshComponent* Part = WeaponPartComponents[i];
		if (!Part || !WeaponPartCurveNames.IsValidIndex(i) || !WeaponPartBaseOffsets.IsValidIndex(i))
		{
			continue;
		}

		// Gate = "any of the 6 curves present" (ReadPartCurveDelta) — 6 hash lookups per part, parts <=8, one owner,
		// so the budget is irrelevant (§4-2); correctness (no TX-only gate) comes first.
		FVector DeltaLoc; FQuat DeltaRot;
		if (!ReadPartCurveDelta(ArmsAnim, WeaponPartCurveNames[i], DeltaLoc, DeltaRot))
		{
			// The curve just vanished (montage ended/blended out): whatever offset was last applied stays on the
			// component forever unless restored (§4-2 measured pitfall — a magazine left detached permanently).
			// Stateless restore: revert only if it currently differs from the authored base. Equals compares once,
			// so the common "no curve on this clip" case costs a single comparison.
			const FTransform& Base = WeaponPartBaseOffsets[i];
			if (!Part->GetRelativeTransform().Equals(Base))
			{
				Part->SetRelativeTransform(Base);
			}
			continue;
		}

		// "authored socket+Offset base (+) curve local delta" (§4-2): compose the curve delta onto the AUTHORED base,
		// never onto the part's current RelativeTransform — reading that back would already include LAST frame's
		// curve offset and accumulate it frame over frame instead of applying a fresh offset from the same base
		// every time. Translation adds, rotation left-multiplies — both already expressed in the base's own local
		// frame, the space a socket's RelativeLocation/Offset is authored in.
		const FTransform& Base = WeaponPartBaseOffsets[i];
		FTransform Result = Base;
		Result.SetLocation(Base.GetLocation() + DeltaLoc);
		Result.SetRotation((DeltaRot * Base.GetRotation()).GetNormalized());
		Part->SetRelativeTransform(Result);
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
	if (!Weapon || !WeaponMesh || ActiveWeaponMesh != WeaponMesh)
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

	// One body, one montage, owner and remotes alike (ADR 0002 invariant 4) — the separate 3P montage is gone, so the
	// two can no longer be authored out of sync. CachedReloadMontage is populated on every machine (the equip refresh
	// runs everywhere), with an on-demand resolve as the safety net for a pawn that hasn't equipped through that path
	// yet. Event-driven — no per-frame AnimBP polling of bReloading.
	if (USkeletalMeshComponent* BodyMesh = GetMesh())
	{
		UAnimMontage* ReloadM = CachedReloadMontage
			? CachedReloadMontage.Get()
			: (Weapon->ReloadMontage.IsNull() ? nullptr : Weapon->ReloadMontage.LoadSynchronous());
		PlayScaledReload(BodyMesh->GetAnimInstance(), ReloadM);
	}

	// WEAPON-mesh reload montage (magazine/bolt) synced to the same reload (rate-scaled identically). Also for everyone
	// now: the weapon mesh that plays it is the one every observer sees.
	if (WeaponMesh)
	{
		UAnimMontage* WeaponReloadM = CachedWeaponReloadMontage
			? CachedWeaponReloadMontage.Get()
			: (Weapon->WeaponReloadMontage.IsNull() ? nullptr : Weapon->WeaponReloadMontage.LoadSynchronous());
		PlayScaledReload(WeaponMesh->GetAnimInstance(), WeaponReloadM);
	}

	// FIRST-PERSON ARMS reload, on the same edge and the same rate scale. Gated on the split rather than on the
	// component: with the arms down (spectating, or none authored) there is no owner watching them, and playing into a
	// hidden graph would leave a montage running when they come back. This asset is separate from the body's because
	// the two meshes have different skeletons, not because the reload is authored twice (ADR 0003 invariant 13).
	if (bFirstPersonSplitActive && FirstPersonArms)
	{
		UAnimMontage* ArmsReloadM = CachedArmsReloadMontage
			? CachedArmsReloadMontage.Get()
			: (Weapon->ArmsReloadMontage.IsNull() ? nullptr : Weapon->ArmsReloadMontage.LoadSynchronous());
		PlayScaledReload(FirstPersonArms->GetAnimInstance(), ArmsReloadM);
	}
}

void AFPSRCharacter::PlayWeaponFireCosmetics()
{
	if (!IsLocallyControlled())
	{
		return;
	}

	// bSuppressFireMontagesWhileADS keeps its meaning but not its old reason: the BODY fire montage no longer fights the
	// sight (the gun is placed against the camera in ADS, so upper-body recoil can't move the reticle) — it now just
	// decides whether the owner wants to see their own body buck while aiming. Data-driven and owner-local; remote
	// observers always see the recoil (MulticastFireCosmetics), so leaving it on is the setting that matches them.
	// The WEAPON bolt montage animates the bolt bone (not the sight) so it KEEPS cycling in ADS as fire feedback (the
	// bolt-reciprocation read), plus the ADS fire kick below — the shot no longer reads as dead-still in ADS.
	const bool bAimingADS = bCachedHasADS && WeaponFire && WeaponFire->IsAiming();
	const bool bSuppressBodyFireMontage = bCachedSuppressFireMontagesWhileADS && bAimingADS;
	const bool bSuppressWeaponBolt = bCachedSuppressWeaponBoltWhileADS && bAimingADS;

	// Fire montage on the body.
	if (CachedFireMontage && !bSuppressBodyFireMontage)
	{
		if (USkeletalMeshComponent* BodyMesh = GetMesh())
		{
			if (UAnimInstance* AnimInst = BodyMesh->GetAnimInstance())
			{
				AnimInst->Montage_Play(CachedFireMontage);
			}
		}
	}

	// Bolt/action montage on the WEAPON mesh (its own skeleton, SKEL_LPAMG_<W>), synced with the body recoil above.
	if (CachedWeaponFireMontage && WeaponMesh && !bSuppressWeaponBolt)
	{
		if (UAnimInstance* WeaponAnimInst = WeaponMesh->GetAnimInstance())
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

void AFPSRCharacter::ApplyWeaponStatePose(float DeltaTime)
{
	// Owner-local only — same gate UpdateAimDownSights uses below (this is owner-local presentation, never
	// replicated: the components it touches carry only local render state).
	//
	// The delta is cleared on the way out of EVERY early return in this function, not just the zero-delta branch
	// below: CachedWeaponStatePoseDelta is read by GetRight/LeftHandGripInGunFrame and GetWeaponPartFrameInGunSpace,
	// which have no idea this function stopped running. Leaving the last live value behind would keep multiplying a
	// dead slide/holster pose into every grip and part frame indefinitely — e.g. a listen-server host's pawn being
	// UnPossessed mid-slide would freeze that tilt into the hand-IK targets for as long as the pawn lives.
	if (!IsLocallyControlled() || GetNetMode() == NM_DedicatedServer)
	{
		CachedWeaponStatePoseDelta = FTransform::Identity;
		return;
	}

	// --- Alpha advance (moved out of UpdateAimDownSights — see that function's own comment on why: this now runs
	// from AFPSRCharacter::Tick, ahead of FirstPersonArms in the tick-prerequisite chain, instead of from the
	// weapon-fire tick that runs after it) ---
	const UFPSRCharacterMovementComponent* Move = GetFPSRMovement();

	// Independent smoothing clock — same clamp reasoning as UpdateAimDownSights's HipDt (a frame spike or low FPS
	// can't snap FInterpConstantTo to target in one tick).
	const float StateDt = FMath::Min(DeltaTime, 1.0f / 30.0f);

	// Slide: already a smoothed 0..1 alpha owned by the movement component (GetSlideBlend) — nothing to smooth here,
	// only to read. Local, not a member (see the header comment on this — UpdateAimDownSights re-derives the same
	// value the same way for its own StateBobScale, rather than reading a member across the tick boundary).
	const float SlideW = Move ? Move->GetSlideBlend() : 0.0f;

	// Airborne: IsFalling() is a binary source with no smoothing of its own (unlike the slide), so this ramps toward
	// it at the equipped weapon's authored rate — CachedHipMotion.AirborneBlendDuration, 0 = instant snap.
	// FInterpConstantTo (not FInterpTo) so the climb is LINEAR — a fixed transition time regardless of how far
	// AirborneBlendAlpha currently sits from the target, which is what "AirborneBlendDuration seconds" is actually
	// promising the data author.
	const bool bWantsAirborne = Move && Move->IsFalling();
	const float AirborneRate = 1.0f / FMath::Max(CachedHipMotion.AirborneBlendDuration, KINDA_SMALL_NUMBER);
	AirborneBlendAlpha = FMath::FInterpConstantTo(AirborneBlendAlpha, bWantsAirborne ? 1.0f : 0.0f, StateDt, AirborneRate);

	// Holster: same judgment source as the server fire gate (UFPSRWeaponFireComponent::CanFire/FireOneShot, and both
	// fire GAs via UFPSRGameplayAbility::IsFirePermittedByMovementState) and the HUD crosshair (UFPSRRunHUDWidget) —
	// CanFireInCurrentState() — so "can I shoot" and "is the gun in my hands" can never drift apart. This pass
	// advances the alpha, folds it into the delta below, AND (edge-detected right after) drives
	// RefreshWeaponVisibility's holster-delay gate (§6, RefreshWeaponVisibility). HolsterDuration 0 = instant snap.
	// Two independent reasons to be stowed, OR'd: the movement state forbids firing (wall-hang — the original reason,
	// and the one that keeps "can I shoot" and "is the gun in my hands" in lockstep), OR 🚧 the equipped weapon asks
	// to stay stowed because its first-person idle has not been authored yet (bForceHolsteredPose — temporary; that
	// one deliberately does NOT gate attacking, see its DataAsset comment).
	const bool bWantsHolster = bCachedForceHolsteredPose || (Move && !Move->CanFireInCurrentState());
	const float HolsterRate = 1.0f / FMath::Max(CachedHipMotion.HolsterDuration, KINDA_SMALL_NUMBER);
	const float PrevHolsterBlendAlpha = HolsterBlendAlpha;
	HolsterBlendAlpha = FMath::FInterpConstantTo(HolsterBlendAlpha, bWantsHolster ? 1.0f : 0.0f, StateDt, HolsterRate);

	// Rising-edge only (Prev<1, Now>=1): the moment the holster descent FINISHES is the one instant
	// RefreshWeaponVisibility has to re-run to actually hide the (now off-screen) mesh — everything in between this
	// and the previous call is just the pose sliding, nothing to toggle. The FALLING edge (draw) needs no symmetric
	// call: OnMovementModeChanged already fires RefreshWeaponVisibility() the instant the wall gate opens (bHideForWall
	// goes false immediately, independent of HolsterBlendAlpha, so the mesh un-hides that same frame) and the pose
	// simply rises into view from there — there is no "finished rising" moment that needs a visibility toggle, only a
	// "started falling" one. Same "just ask the owner function to re-evaluate" pattern as UpdateScopeWeaponVisibility —
	// this never decides visibility itself, RefreshWeaponVisibility remains the single owner of bWeaponHidden and the
	// SetVisibility calls.
	if (PrevHolsterBlendAlpha < 1.0f && HolsterBlendAlpha >= 1.0f)
	{
		RefreshWeaponVisibility();
	}

	// Reload (🚧 temporary arms-down stand-in for an unauthored first-person reload animation — see ReloadPose's
	// DataAsset comment): same camera-space ARMS treatment as holster above, but its OWN alpha and OWN clock
	// (ReloadBlendDuration) so reload and holster timing never pull on each other. Zeroing ReloadPose in the DA
	// switches this off with no code change. Cannot overlap the holster pose in practice: entering a wall cancels
	// any in-flight reload (OnMovementModeChanged) and StartReload refuses to begin one while wall-hung
	// (UFPSRWeaponInventoryComponent::StartReload), so the two poses never have to be resolved against each other.
	// No RefreshWeaponVisibility call here, unlike the holster edge above: reloading is not a reason to HIDE the
	// mesh (the holster path's delayed-hide is gated on bHideForWall specifically) — this only moves it off-screen
	// via the pose, visibility is untouched.
	const bool bWantsReloadPose = bCachedHasReloadPose && IsReloading();
	const float ReloadRate = 1.0f / FMath::Max(CachedHipMotion.ReloadBlendDuration, KINDA_SMALL_NUMBER);
	ReloadBlendAlpha = FMath::FInterpConstantTo(ReloadBlendAlpha, bWantsReloadPose ? 1.0f : 0.0f, StateDt, ReloadRate);

	// --- Gun-space delta: gun-anchor attach path ONLY (§②/§③ of the plan this implements). The fallback/socket-
	// attach path (body hand, or an arms mesh with no ik_hand_gun bone) already has UpdateAimDownSights writing the
	// weapon's WORLD transform every frame there (bSolvingArms == false) — a relative write here would just be
	// clobbered a moment later, so this function leaves the weapon untouched on that path (current behavior,
	// zero regression for every weapon until content authors the gun-anchor rig).
	// Delta cleared here too (see the top-of-function note): !ActiveWeaponMesh is the UNEQUIP path, which does NOT go
	// through AttachWeaponMeshes (RefreshEquippedWeaponVisual returns early once it has cleared the meshes) — so this
	// is the only place that can stop a slide/holster delta from outliving the weapon it belonged to and being folded
	// into a stale grip cache the arms would then reach for bare-handed.
	if (!bWeaponAttachIsGunAnchor || !FirstPersonArms || !ActiveWeaponMesh)
	{
		CachedWeaponStatePoseDelta = FTransform::Identity;
		return;
	}

	// (1 - CurrentADSAlpha) weight for slide/airborne — the SAME weight UpdateAimDownSights used to apply in place,
	// kept so a player who enters ADS mid-slide still gets the sight glue's own pose instead of fighting this one.
	// Read as a MEMBER here because CurrentADSAlpha is written LATER this same frame — UpdateAimDownSights runs from
	// the weapon-fire tick, which sits AFTER this function's caller (AFPSRCharacter::Tick) in the prerequisite chain
	// (that ordering is deliberate — see UpdateAimDownSights's own comment: the sight-glue solve needs the arms pose
	// finalized, this delta does not). So this reads last frame's alpha — up to one frame stale, same class of cross-
	// tick read as any other member, and immaterial here: it only scales how much of the slide/airborne pose shows
	// through while ADS is transitioning, at CachedADSInterpSpeed's typical rate (a small fraction of one frame's
	// worth of blend).
	//
	// HOLSTER IS DELIBERATELY NOT HERE — it is composed onto the ARMS instead (UpdateAimDownSights, camera space).
	// Slide/airborne are small in-hand adjustments, so moving the gun and letting the hands follow it by IK is both
	// correct and what makes the detail-panel authoring loop work. Stowing is the opposite problem: it has to clear
	// the whole first-person frame (arms included — the requirement is "nothing on screen", and hiding the arms
	// outright was rejected because a real 1P holster ANIMATION would later have to undo it). A gun-space holster
	// cannot deliver that: the shoulders stay pinned to the camera, so past the arm's reach the two-bone IK simply
	// stops following and the gun leaves the hands — gun off-screen, arms still in frame stretched at nothing.
	// Moving the arms takes the gun with it for free (the gun is attached to them), so the whole assembly exits.
	const float StatePoseWeight = 1.0f - CurrentADSAlpha;

	const FVector TotalOffset = StatePoseWeight * (SlideW * CachedHipMotion.SlidePose.Offset + AirborneBlendAlpha * CachedHipMotion.AirbornePose.Offset);
	const FRotator TotalTilt = (CachedHipMotion.SlidePose.Tilt * SlideW + CachedHipMotion.AirbornePose.Tilt * AirborneBlendAlpha) * StatePoseWeight;

	if (TotalOffset.IsNearlyZero() && TotalTilt.IsNearlyZero())
	{
		// Unauthored weapon (every weapon until §5 DA content lands), or a settled state (all three alphas back at
		// 0) — skip the write rather than repeat a no-op transform every frame (§ regression-zero requirement: an
		// unauthored weapon's WeaponMesh panel numbers must read exactly as attached, always). Only touch the
		// component if a PREVIOUS frame actually left a residual delta on it — same restore-to-base idiom
		// ApplyWeaponPartCurves already uses for its own per-part base restore, above.
		if (!ActiveWeaponMesh->GetRelativeTransform().Equals(CachedWeaponAttachRelative))
		{
			ActiveWeaponMesh->SetRelativeTransform(CachedWeaponAttachRelative);
		}
		CachedWeaponStatePoseDelta = FTransform::Identity;
		return;
	}

	// Component-wise addition (NOT quaternion composition) is the entire point of moving this layer onto the weapon
	// (see the plan this implements): a designer dialing in a pose by hand in the WeaponMesh detail panel reads
	// Location/Rotation as three independent numbers each, and Offset/Tilt have to move those SAME three numbers by
	// the SAME amount for the PIE-authoring loop to close (pose in the panel -> read the delta -> type it into the
	// DA). Scale is never touched here — WeaponAttachScale lives untouched inside CachedWeaponAttachRelative.
	const FVector FinalLoc = CachedWeaponAttachRelative.GetLocation() + TotalOffset;
	const FRotator FinalRot = CachedWeaponAttachRelative.Rotator() + TotalTilt;

	ActiveWeaponMesh->SetRelativeLocationAndRotation(FinalLoc, FinalRot);

	// Cached once here rather than re-derived by every grip/part-frame reader below (GetRightHandGripInGunFrame /
	// GetLeftHandGripInGunFrame / GetWeaponPartFrameInGunSpace — up to twice per hand/part PER ANIM FRAME) — this
	// runs once per character tick. Scale carried through from CachedWeaponAttachRelative (matches ActiveWeaponMesh's
	// own untouched scale) so the inverse below is well-formed.
	const FTransform Final(FinalRot, FinalLoc, CachedWeaponAttachRelative.GetScale3D());
	CachedWeaponStatePoseDelta = CachedWeaponAttachRelative.Inverse() * Final;
}

void AFPSRCharacter::UpdateAimDownSights(float DeltaTime)
{
	// Owner-local only. Everyone else keeps the weapon rigidly on the body's grip socket, reading the aim from the body
	// animation; the re-placement below is the one authored exception to invariant 4 (ADR 0002), and it is not
	// replicated — the component's relative transform is local render state, so a listen-server host writing it here
	// cannot leak the displaced pose to its clients.
	if (!IsLocallyControlled() || !FirstPersonCamera || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	// ActiveWeaponMesh is whichever mesh the weapon actually shows (skeletal firearm or static melee) — and it is NULL
	// for a weapon that shows none at all: BARE HANDS. That case still has something to place, because the ARMS are a
	// mesh of their own and the holster layer below has to be able to lower them (measured 2026-08-10: bare hands
	// authored "always holstered" did nothing at any offset value, because this early-out skipped the whole function
	// and the arms simply froze at whatever transform the previously-equipped weapon left behind). So bail only when
	// there is neither a weapon mesh NOR arms to move.
	//
	// Everything weapon-specific below is already gated for the no-weapon case and needs no extra guard: bAiming
	// requires AimComp (null here) so the ADS solve is skipped, the hip block is gated on bCachedHasHipMotion, and the
	// weapon-space branches are all behind !bSolvingArms, which cannot be taken when WeaponCarrier is null.
	UMeshComponent* WeaponCarrier = ActiveWeaponMesh;
	const bool bArmsAvailable = bFirstPersonSplitActive && FirstPersonArms != nullptr;
	if (!WeaponCarrier && !bArmsAvailable)
	{
		return;
	}

	// WHAT the solve moves. With the first-person arms up the weapon is in their hand, so moving the gun alone would
	// slide it out of the grip — the arms are the rigid body that has to travel, and the gun rides along. That is the
	// original shape of this function (it moved the arms before ADR 0002 removed them), which is why the maths below
	// needed no change: ADR 0002 left this as "a change to the target, not to the solve".
	// With no arms, the weapon hangs off an animated hand and moving it alone is the only option available.
	USceneComponent* SolveTarget = WeaponCarrier;
	if (bArmsAvailable)
	{
		SolveTarget = FirstPersonArms;
	}
	const bool bSolvingArms = (SolveTarget != WeaponCarrier);

	// The AimSocket may live on a SIGHT part (iron sight / optic) or the weapon itself — CachedAimComponent resolves to
	// the part that owns it (RefreshWeaponPartComponents), else fall back to the carrier.
	UMeshComponent* AimComp = CachedAimComponent;
	if (!AimComp)
	{
		AimComp = WeaponCarrier;
	}

	// Movement-state gate (server-authoritative source = UFPSRCharacterMovementComponent, ADR 0001 single owner of
	// locomotion state) feeds the ADS intent gate (bAiming, right below) and the walk-bob amplitude scale further
	// down (StateBobScale). The STATE-POSE alphas themselves (slide/airborne/holster) are no longer advanced here —
	// see ApplyWeaponStatePose (AFPSRCharacter::Tick), which this refactor moved them to so the weapon's state-pose
	// delta lands BEFORE FirstPersonArms evaluates its anim graph this frame instead of one frame after it (the same
	// one-frame gun-anchor lag class this project has already fixed once — GetRightHandGripInGunFrame's header
	// comment). AirborneBlendAlpha (member, read further down) is already advanced for THIS frame when this function
	// runs: Character::Tick sits ahead of the weapon-fire tick in the prerequisite chain BeginPlay wires up
	// (CMC -> Character -> Arms -> WeaponFire).
	const UFPSRCharacterMovementComponent* Move = GetFPSRMovement();

	// Re-derived rather than read from a cached member: GetSlideBlend() is already the movement component's own
	// smoothed value, so there is nothing to persist, and ApplyWeaponStatePose computes this identical number from
	// this identical source earlier the SAME frame for its own state-pose delta — recomputing here (only used below,
	// in StateBobScale) is cheaper than threading a copy across the tick boundary.
	const float SlideW = Move ? Move->GetSlideBlend() : 0.0f;

	// Relax the ADS glue during a reload: the reload (mag-swap) montage swings the weapon a lot, and re-solving each frame
	// to keep the sight glued to centre would counter-swing it just as hard — a violent shake at reload timing. Treat a
	// reload as not-aiming so the alpha smoothly lowers the gun toward hip for the reload and raises back afterwards; the
	// reload animation then plays cleanly with nothing fighting it. IsReloading() reflects the replicated per-instance
	// reload state, set on the same OnRep as the reload montage, so the relax is in sync with the montage.
	const bool bReloading = WeaponInventory && WeaponInventory->IsReloading();
	// Movement state also gates the ADS INTENT here, not just the pose layer above: wall-hang already blocks firing
	// (CanFireInCurrentState() false), and the Apex reference releases the sight the instant the state pose takes
	// over. bIsAiming itself (replicated, read via WeaponFire->IsAiming()) is UNTOUCHED by this — it only decides
	// whether THIS owner-local presentation (this solve, plus the matching gate in UpdateCameraFieldOfView) honors
	// that intent this frame. A wall exit self-heals the visual the instant CanFireInCurrentState() goes true again,
	// with no RPC round trip.
	const bool bAiming = bCachedHasADS && !CachedAimSocket.IsNone() && WeaponFire && WeaponFire->IsAiming() && !bReloading
		&& AimComp && AimComp->DoesSocketExist(CachedAimSocket)
		&& (!Move || Move->CanFireInCurrentState());

	// Every frame below is SCALE-FREE. The weapon hangs off the grip socket at WeaponAttachScale (0.85 for the rifle),
	// and composing transforms that still carry that scale would shrink the socket offsets along with the mesh — the
	// sight would land short of the centre-line by exactly the scale factor. Strip scale here and never write it: the
	// attach in RefreshEquippedWeaponVisual is the only place the weapon's size is decided.
	auto RigidFrame = [](const FTransform& T) { return FTransform(T.GetRotation(), T.GetTranslation()); };

	const FTransform CamFrame = RigidFrame(FirstPersonCamera->GetComponentTransform());

	// The HIP end of the blend: where the solve target rests when NOT aiming. It has to be an INDEPENDENT reference —
	// read the target's own transform and last frame's write feeds straight back in and the pose walks away.
	//  - Arms: their authored placement relative to the camera, snapshotted once in BeginPlay. Constant by nature; the
	//    camera is not animated, so there is nothing to re-read each frame.
	//  - Weapon: its parent grip socket, re-read every frame because that parent IS an animated hand. The attach uses
	//    SnapToTarget, so at rest the weapon's rotation and location ARE the socket's and only the scale is ours.
	//    Reading it through the attachment rather than a cached socket name means a designer re-parenting the weapon
	//    cannot desync the hip pose from where the weapon actually hangs.
	FTransform HipRelCam;
	if (bSolvingArms)
	{
		HipRelCam = RigidFrame(ArmsHipRelativeTransform);
	}
	else
	{
		const USceneComponent* GripParent = WeaponCarrier->GetAttachParent();
		if (!GripParent)
		{
			// The weapon isn't hanging off anything, so there is no independent hip reference. Bail rather than fall back
			// to the weapon's own transform — that would feed last frame's write back in and let the pose drift.
			return;
		}
		HipRelCam =
			RigidFrame(GripParent->GetSocketTransform(WeaponCarrier->GetAttachSocketName(), RTS_World)).GetRelativeTransform(CamFrame);
	}

	// When aiming, recompute the EXACT target transform (relative to the camera) that lands the AimSocket on the
	// camera's forward centre-line at ADSSightDistance THIS frame, and store it. The camera doesn't chase the gun; the
	// gun is brought to the camera — which is the whole point of the ADS re-decision (the retargeted aim pose puts the
	// sight ~30 deg off-screen, far past anything a residual correction could absorb).
	if (bAiming)
	{
		// AimSocket transform RELATIVE TO THE SOLVE TARGET. The sight part rides the weapon rigidly and (when solving the
		// arms) the weapon rides the hand rigidly, so this is invariant to where the target currently sits — it cancels
		// out below, no feedback loop — yet it is recomputed each frame so it tracks part swaps and any animation that
		// moves the socket (a bolt montage on the weapon, or the arms' own graph moving the hand).
		const FTransform TargetFrame = RigidFrame(SolveTarget->GetComponentTransform());
		// Pre-compose the designer's aim rotation offset in the socket's LOCAL frame (identical to authoring that rotation
		// onto the socket) so full-frame alignment points the gun forward for packs whose socket axes are off-forward
		// (this pack's weapon-forward is +Y → ADSAimRotationOffset Yaw 90). Zero offset = use the socket frame as-authored.
		const FTransform SocketRelTarget = FTransform(CachedADSAimRotationOffset)
			* RigidFrame(AimComp->GetSocketTransform(CachedAimSocket, RTS_World)).GetRelativeTransform(TargetFrame);

		if (bCachedADSAlignRotation)
		{
			// Full-frame alignment: level the socket frame to the camera (identity rotation) AND place it on the centre-
			// line, so the authored hip cant is removed and the sight reads straight. Solve target-rel-camera from the
			// desired socket-rel-camera: SocketRelTarget * TargetRelCam == Desired (UE composes child * parent), hence
			// TargetRelCam = SocketRelTarget^-1 * Desired.
			const FTransform DesiredSocketRelCam(FRotator::ZeroRotator, FVector(CachedADSSightDistance, 0.0f, 0.0f));
			const FTransform TargetRelCam = SocketRelTarget.Inverse() * DesiredSocketRelCam;
			ADSAimLoc = TargetRelCam.GetLocation();
			ADSAimRot = TargetRelCam.Rotator();
		}
		else
		{
			// Translation-only ADS (escape hatch): keep the hip rotation and only slide so the socket sits on the centre-
			// line. Socket position under the hip pose is HipRot.Rotate(socket-loc-rel-target) + target-loc, so solve
			// target-loc for a socket at (D, 0, 0).
			ADSAimRot = HipRelCam.Rotator();
			ADSAimLoc = FVector(CachedADSSightDistance, 0.0f, 0.0f) - ADSAimRot.RotateVector(SocketRelTarget.GetLocation());
		}
	}

	// Interpolate the ADS blend alpha (0 = hip, 1 = fully aimed) at the weapon's ADS speed (matches the FOV interp), then
	// BLEND the hip pose and the stored aim pose by it — we don't chase the moving aim target with a lagging transform
	// interp. At full ADS (alpha≈1) the weapon takes the exact aim transform every frame, so the socket sits precisely on
	// the centre-line and animated sway/bob is cancelled AT THE SIGHT (steady reticle). While disengaging, the stored aim
	// pose is held frozen and alpha decays, so the return to hip stays smooth. Rotation blends via slerp.
	const float InterpSpeed = FMath::Max(0.01f, CachedADSInterpSpeed);
	CurrentADSAlpha = FMath::FInterpTo(CurrentADSAlpha, bAiming ? 1.0f : 0.0f, DeltaTime, InterpSpeed);

	// Hip pose in camera space — the base every layer below is expressed against. At alpha 0 with no hip motion this
	// resolves back to exactly the grip socket, i.e. the write at the end is a no-op and the gun simply rides the hand.
	const FVector HipBaseLoc = HipRelCam.GetLocation();
	const FQuat HipBaseRot = HipRelCam.GetRotation();

	// ADS position: blend hip -> the solved aim location, which pins the AimSocket on the view centre-line every frame.
	//
	// There used to be an ADSPositionBobScale knob here that lerped ADSAimLoc back toward HipBaseLoc, documented as
	// "lets that fraction of the animated positional bob survive for a livelier ADS". That was FALSE on the arms path
	// and it was removed (measured 2026-08-10, cost a long hunt): HipBaseLoc is ArmsHipRelativeTransform, a CONSTANT
	// snapshotted once in BeginPlay (see the hip-reference block above — the arms hang off the camera, so there is
	// nothing animated to re-read). Blending toward a constant cannot reintroduce motion; it can only drag the gun a
	// FIXED distance off the centre-line, i.e. a permanently misaligned sight that never moves. The knob only ever did
	// what it claimed on the older no-arms path, where the hip reference was re-read from an animated hand each frame.
	// Liveliness during ADS belongs to the sway block below, which moves the gun about the PINNED sight instead.
	FVector NewLoc = FMath::Lerp(HipBaseLoc, ADSAimLoc, CurrentADSAlpha);
	FQuat NewRot = FQuat::Slerp(HipBaseRot, ADSAimRot.Quaternion(), CurrentADSAlpha);

	// ADS idle sway + fire kick: BOTH pivot the weapon about the pinned sight (camera-space ≈ (ADSSightDistance, 0, 0)),
	// faded by the ADS alpha, so the gun body/muzzle moves while the sight stays on the centre-line (steady reticle).
	//  - Sway: a gentle handheld "breathing" wander (yaw = L-R, subtle pitch) — two out-of-phase sines per axis so it
	//    reads organic rather than a metronome. Movement-SCALED, not movement-GATED: ADSSwayIdleScale is the fraction
	//    that survives standing still, so a planted aim still breathes instead of freezing into a photograph (it used
	//    to multiply by the movement factor alone, which meant exactly zero at a standstill — that dead-still ADS is
	//    what this fixes). Cosmetic + owner-local.
	//  - Kick: the per-shot recoil snap (+pitch = muzzle up) set on each aimed shot (PlayWeaponFireCosmetics), settled
	//    back toward zero each frame. Both are scaled by the ADS alpha so they appear only in ADS and fade on release.
	// Smoothed movement factor: 0 when still → 1 at BaseWalkSpeed, eased (FInterpTo) so the sway winds down after you
	// stop rather than cutting off. GetVelocity() is the pawn's movement velocity; Size2D() ignores jump/gravity.
	const float SwayRefSpeed = FMath::Max(1.0f, BaseWalkSpeed);
	const float SwayMoveTarget = FMath::Clamp(GetVelocity().Size2D() / SwayRefSpeed, 0.0f, 1.0f);
	ADSSwayMoveAlpha = FMath::FInterpTo(ADSSwayMoveAlpha, SwayMoveTarget, DeltaTime, 8.0f);

	// Standing still keeps ADSSwayIdleScale of the amplitude; walking ramps it to full. Lerp (not max/gate) so there is
	// no step at the moment the player starts or stops moving.
	const float SwayWeight = FMath::Lerp(FMath::Clamp(CachedADSSwayIdleScale, 0.0f, 1.0f), 1.0f, ADSSwayMoveAlpha);

	FRotator ExtraRot(ADSFireKickPitch, 0.0f, 0.0f);
	if (CurrentADSAlpha > KINDA_SMALL_NUMBER)
	{
		const float T = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
		const float W = CachedADSSwaySpeed;
		if (SwayWeight > KINDA_SMALL_NUMBER && (CachedADSSwayYawDegrees > 0.0f || CachedADSSwayPitchDegrees > 0.0f))
		{
			ExtraRot.Yaw   += SwayWeight * CachedADSSwayYawDegrees   * (0.6f * FMath::Sin(T * W)            + 0.4f * FMath::Sin(T * W * 1.7f + 1.1f));
			ExtraRot.Pitch += SwayWeight * CachedADSSwayPitchDegrees * (0.6f * FMath::Sin(T * W * 0.8f + 0.5f) + 0.4f * FMath::Sin(T * W * 1.3f + 2.3f));
		}
		if (!ExtraRot.IsNearlyZero())
		{
			const FVector Pivot(CachedADSSightDistance, 0.0f, 0.0f);
			const FQuat ExtraQuat = (ExtraRot * CurrentADSAlpha).Quaternion();
			NewLoc = Pivot + ExtraQuat.RotateVector(NewLoc - Pivot);
			NewRot = ExtraQuat * NewRot;
		}

		// Optional FREE sway (defaults to 0 = off): a straight screen-space wander of the whole weapon. Unlike the
		// rotation above it deliberately does NOT pivot about the sight, so the reticle itself drifts — which is the
		// only way to get lateral/vertical positional motion at all. A rigid body cannot translate sideways while a
		// point on it stays fixed; "move the gun but keep the sight nailed" is EXCLUSIVELY a rotation about that
		// sight. Kept as its own opt-in knob (0 by default) because the standing rule here is that aim belongs to the
		// player's input and cosmetics must not move it — raise these only if a little reticle drift is wanted.
		if (SwayWeight > KINDA_SMALL_NUMBER
			&& (CachedADSSwayFreeHorizontalCm > 0.0f || CachedADSSwayFreeVerticalCm > 0.0f))
		{
			// Frequencies deliberately unrelated to the rotation's above, so the two layers never lock into one
			// visible beat.
			const float FreeY = CachedADSSwayFreeHorizontalCm * (0.6f * FMath::Sin(T * W * 0.9f + 0.7f) + 0.4f * FMath::Sin(T * W * 1.5f + 2.0f));
			const float FreeZ = CachedADSSwayFreeVerticalCm   * (0.6f * FMath::Sin(T * W * 1.1f + 1.6f) + 0.4f * FMath::Sin(T * W * 0.7f + 0.3f));
			NewLoc += (SwayWeight * CurrentADSAlpha) * FVector(0.0f, FreeY, FreeZ);
		}
	}
	ADSFireKickPitch = FMath::FInterpTo(ADSFireKickPitch, 0.0f, DeltaTime, CachedADSFireKickRecoveryRate);

	// --- Hip-space procedural weapon motion (owner-local cosmetic) ---
	// Faded OUT as ADS engages (weight = 1-alpha), composed additively into the SAME single write below (no extra
	// SetRelative*/SetWorld* call). State is advanced from INVARIANT sources only (control-rotation delta, velocity,
	// accumulated phase, decaying kick) — never from the weapon's own transform — so there is no feedback loop.
	// Translation is added in camera space (NewLoc is weapon-relative-to-camera: +X forward, +Y right, +Z up →
	// screen-space bob/kick); sway/kick rotation is composed about the weapon origin. Fixed order: base → ADS blend
	// (above) → hip additive → write once.
	// NOTE: what this layer moves follows the same target as the ADS solve above. With the first-person arms up it moves
	// the ARMS, so the gun and the hand holding it travel together — the shape this was written for. Without them it
	// moves the gun alone, which reads as the weapon floating off the grip at any non-zero amplitude; that is why
	// ProceduralWeaponMotion is authored per weapon and defaults to no motion.
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
		// Phase accumulates with dt (frame-rate independent); vertical runs at 2x the horizontal frequency. PHASE is
		// untouched by the state layer (a phase jump would pop the bob mid-cycle) — only the AMPLITUDE scales, via
		// StateBobScale: slide/airborne widen or (typically) zero the bob. Exclusive-resolved-then-lerped by the
		// blend weight (SlideW / AirborneBlendAlpha) rather than a hard switch, so a weapon with default scales
		// (Slide 0, Airborne 1) reproduces the pre-state-layer amplitude exactly and the transition itself is smooth.
		HipBobPhase += HipDt * CachedHipMotion.WalkBobFrequency * ADSSwayMoveAlpha;
		HipBobPhase = FMath::Fmod(HipBobPhase, 1024.0f);
		const float StateBobScale = FMath::Lerp(1.0f, CachedHipMotion.SlideBobScale, SlideW)
			* FMath::Lerp(1.0f, CachedHipMotion.AirborneBobScale, AirborneBlendAlpha);
		const float BobH = FMath::Sin(HipBobPhase * 2.0f * PI) * CachedHipMotion.WalkBobHorizontal * ADSSwayMoveAlpha * StateBobScale;
		const float BobV = FMath::Sin(HipBobPhase * 4.0f * PI) * CachedHipMotion.WalkBobVertical * ADSSwayMoveAlpha * StateBobScale;

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

	// --- State pose / holster layers (§6, GunMotionTool_Spec.md) REMOVED FROM HERE (state-pose-on-weapon refactor):
	// slide/airborne/holster used to be composed additively onto whatever this function's ADS/hip solve produced and
	// written to SolveTarget (the ARMS, on the gun-anchor path) below. They now compose against the WEAPON's own
	// relative transform instead — see ApplyWeaponStatePose (AFPSRCharacter::Tick) — so a designer tuning a pose in
	// PIE reads/writes the SAME WeaponMesh detail-panel numbers the game applies, and rotating about the gun's own
	// origin reproduces the smaller, tighter pose the arms-space version could not (rotating the arms swings the gun
	// through their much larger radius instead). NewLoc/NewRot below are therefore the ADS + hip-motion result ONLY.
	//
	// --- Holster layer (§6): the stow/draw offset, and the ONE state pose that still rides the ARMS rather than the
	// weapon (slide/airborne moved to gun space in the state-pose refactor — see ApplyWeaponStatePose for why holster
	// did not follow them). Requirement is "nothing on screen while stowed", arms included, and only moving the arms
	// achieves that: the gun is attached to them so it exits with them, whereas a gun-space holster would run out of
	// arm reach and drop the gun out of the hands while the arms stayed in frame. Hiding the arms outright was the
	// other candidate and was rejected — a real first-person holster ANIMATION, once authored, would have to undo it.
	//
	// LAST in the composition order and at FULL WEIGHT — no (1-CurrentADSAlpha) gating like the hip layer above —
	// because stowing has to clear the frame regardless of aim state; a holstered gun that stayed glued to a sight
	// pose would defeat the whole point. bAiming above already drops CurrentADSAlpha toward 0 the instant
	// CanFireInCurrentState() goes false (same predicate), so full weight is belt-and-braces rather than the only
	// thing keeping a stowed gun from also reading as aimed.
	//
	// HolsterBlendAlpha is advanced in ApplyWeaponStatePose, which runs from AFPSRCharacter::Tick — AHEAD of this
	// function's weapon-fire tick in the prerequisite chain — so this reads THIS frame's value, not last frame's.
	// SmoothStep (not the raw linear alpha) so the motion eases at both ends instead of stopping on a dime; the alpha
	// itself stays linear (FInterpConstantTo) so a fixed HolsterDuration still means a fixed transition TIME — the
	// ease is a presentation-only reshaping of that same clock.
	if (HolsterBlendAlpha > KINDA_SMALL_NUMBER)
	{
		const float HolsterEase = FMath::SmoothStep(0.0f, 1.0f, HolsterBlendAlpha);
		NewLoc += HolsterEase * CachedHipMotion.HolsterPose.Offset;
		NewRot = (CachedHipMotion.HolsterPose.Tilt * HolsterEase).Quaternion() * NewRot;
	}

	// Reload layer (🚧 temporary, see ApplyWeaponStatePose/ReloadPose comments) — same space, same full weight, same
	// SmoothStep reshaping as the holster layer just above, only keyed off its own alpha. Not mutually exclusive with
	// the holster term above (no else/early-out between them): simply summed, because the two alphas are never both
	// non-zero at once — entering a wall cancels an in-flight reload and refuses a new one while wall-hung, so there
	// is nothing here that needs arbitrating.
	if (ReloadBlendAlpha > KINDA_SMALL_NUMBER)
	{
		const float ReloadEase = FMath::SmoothStep(0.0f, 1.0f, ReloadBlendAlpha);
		NewLoc += ReloadEase * CachedHipMotion.ReloadPose.Offset;
		NewRot = (CachedHipMotion.ReloadPose.Tilt * ReloadEase).Quaternion() * NewRot;
	}

	// The one write, and the ONE place the solved pose is applied. Everything above produced a camera-relative transform;
	// only this decides what receives it, and in which space.
	if (bSolvingArms)
	{
		// The arms ARE a camera child, so the solved transform is already expressed in their parent's space — write it
		// straight in. This is not merely shorter: the engine applies the view rotation to the camera component POST-tick
		// (UCameraComponent::GetCameraView -> SetWorldRotation, CameraComponent.cpp), so a child inherits the rotation the
		// frame is actually RENDERED with, while a world write would pin the arms to the camera frame as it stood during
		// the tick — one frame stale, and visible as the arms lagging a fast turn.
		SolveTarget->SetRelativeLocationAndRotation(NewLoc, NewRot);
	}
	else
	{
		// The weapon's parent is an animated grip socket, so a relative write would re-mix the hand's motion into a pose
		// that was solved against the camera — exactly the sight shake this whole path exists to remove. Compose back to
		// world instead. Scale is untouched either way, so WeaponAttachScale lives.
		const FTransform NewWorld = FTransform(NewRot, NewLoc) * CamFrame;
		SolveTarget->SetWorldLocationAndRotation(NewWorld.GetTranslation(), NewWorld.GetRotation());
	}
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

	// Muzzle flash at the weapon's muzzle. This used to be gated to a SPECTATING viewer only, because the 1P weapon it
	// attached to was invisible to ordinary remotes and the (non-OnlyOwnerSee) particle would have floated inside a
	// teammate's head. That reason died with the 1P/3P split (ADR 0002): the weapon is now in the shooter's hand for
	// everyone, so the flash belongs there for everyone — a teammate firing next to you finally reads as firing.
	if (ActiveWeaponMesh && !Weapon->MuzzleFlash.IsNull())
	{
		if (UParticleSystem* Muzzle = Weapon->MuzzleFlash.LoadSynchronous())
		{
			UMeshComponent* MuzzleComp = CachedMuzzleComponent ? CachedMuzzleComponent : ActiveWeaponMesh;
			UGameplayStatics::SpawnEmitterAttached(Muzzle, MuzzleComp, Weapon->MuzzleSocket,
				FVector::ZeroVector, Weapon->MuzzleFlashRotationOffset);
		}
	}

	// Bolt/action on the WEAPON mesh — same reasoning as the muzzle above: one visible weapon, one bolt cycle.
	if (WeaponMesh && !Weapon->WeaponFireMontage.IsNull())
	{
		if (UAnimMontage* WeaponFireM = Weapon->WeaponFireMontage.LoadSynchronous())
		{
			if (UAnimInstance* WeaponAnimInst = WeaponMesh->GetAnimInstance())
			{
				WeaponAnimInst->Montage_Play(WeaponFireM);
			}
		}
	}

	// Body fire montage for REMOTE observers — plays on every non-owner client (this multicast already early-returned
	// for the locally-controlled owner above). The SAME montage the owner plays (ADR 0002 — the separate 3P montage is
	// gone), so both a spectating downed teammate and normal remotes see the shooter's recoil. Null FireMontage = no
	// reaction (null-safe, no gameplay effect).
	if (USkeletalMeshComponent* BodyMesh = GetMesh())
	{
		if (!Weapon->FireMontage.IsNull())
		{
			if (UAnimMontage* FireM = Weapon->FireMontage.LoadSynchronous())
			{
				if (UAnimInstance* BodyAnimInst = BodyMesh->GetAnimInstance())
				{
					BodyAnimInst->Montage_Play(FireM);
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
	// LOCAL controller) — the spectator's view would track yaw (replicated actor
	// rotation) but not pitch. GetBaseAimRotation() gives the full aim: yaw from the actor rotation, pitch from the
	// replicated RemoteViewPitch16 (UE5.7). Drive the camera component with it (CalcCamera runs each frame only while
	// this pawn is the view target) so the view pitches with the aim, then return that view.
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
