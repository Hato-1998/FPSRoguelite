// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/Character.h"
#include "AbilitySystemInterface.h"
#include "GameplayTagContainer.h"
#include "Weapon/FPSRWeaponDataAsset.h"
#include "FPSRCharacter.generated.h"

class UAbilitySystemComponent;
class UFPSRAbilitySystemComponent;
class UFPSRCharacterMovementComponent;
class UCameraComponent;
class USkeletalMeshComponent;
class UInputAction;
class UFPSRWeaponInventoryComponent;
class UFPSRWeaponInstance;
class UFPSRWeaponFireComponent;
class UFPSRRecoilComponent;
class UFPSRWeaponDataAsset;
class UMaterialInterface;
class UFPSRPlayerFeedbackComponent;
class UFPSRReviveComponent;
class UFPSRBlindspotAudioComponent;
struct FInputActionValue;
class UStaticMeshComponent;
class UMeshComponent;
class UAnimInstance;
class UAnimMontage;
class USoundBase;
class UParticleSystem;
class UUserWidget;

/** Base player character: first-person camera + ONE shared body mesh (True First Person, ADR 0002) + Enhanced Input +
 *  weapon inventory/firing. ASC lives on PlayerState. */
UCLASS()
class FPSROGUELITE_API AFPSRCharacter : public ACharacter, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	/** Takes an FObjectInitializer so the character's movement component can be swapped to
	 *  UFPSRCharacterMovementComponent (ADR 0001: that component is the single owner of locomotion state). */
	AFPSRCharacter(const FObjectInitializer& ObjectInitializer);

	/** The locomotion component, already typed. Never null in practice (the ctor installs it), but callers should
	 *  still null-check — a Blueprint subclass can technically override the component class. */
	UFUNCTION(BlueprintPure, Category = "FPSR|Movement")
	UFPSRCharacterMovementComponent* GetFPSRMovement() const;

	/** True when this pawn may START or CONTINUE special locomotion (slide today, wall-hang later). False during the
	 *  card-selection freeze and while downed. Reads replicated state (GameState run-paused + PlayerState life state)
	 *  so the server and the owning client agree — the movement component gates on this, and prediction requires both
	 *  machines to reach the same answer. Public because the movement component is a separate class. */
	bool CanPerformSpecialMovement() const;

	virtual void PossessedBy(AController* NewController) override;
	virtual void OnRep_PlayerState() override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void NotifyControllerChanged() override;

	/** First-person spectate fix (U9 §2-13): a DBNO teammate views this pawn via SetViewTarget. When this pawn isn't
	 *  locally controlled, UCameraComponent skips bUsePawnControlRotation (it only follows the LOCAL controller) so the
	 *  spectator's view would track yaw (replicated actor rotation) but not pitch. Drive the view from
	 *  GetBaseAimRotation() — its pitch comes from the replicated RemoteViewPitch16 — so up/down aim is reflected. The
	 *  locally-controlled owner keeps the default camera-component path. */
	virtual void CalcCamera(float DeltaTime, struct FMinimalViewInfo& OutResult) override;

	/** Drives the stance camera blend (and, in debug builds, the on-screen readouts). Ticks after the movement
	 *  component so the cached eye position is the one movement actually left behind this frame. */
	virtual void Tick(float DeltaSeconds) override;

#if ENABLE_DRAW_DEBUG
	/** Top-right movement readout (speed + locomotion state), drawn through the engine's debug-draw service because
	 *  AddOnScreenDebugMessage can only stack at the top LEFT, where the existing HP/run debug already lives. Registered
	 *  for the local player only; toggle with FPSR.Movement.Debug. */
	void DrawMovementDebug(class UCanvas* Canvas, class APlayerController* PC);
	FDelegateHandle MovementDebugDrawHandle;
#endif

	/** Drop / raise the first-person camera with the crouch capsule. Without this the camera keeps its standing offset
	 *  and pokes out the TOP of the crouched capsule (standing eye 152cm vs crouched capsule 80cm tall), which lets the
	 *  view clip through geometry the capsule is still blocked by. Super updates BaseEyeHeight for us — the engine
	 *  derives CrouchedEyeHeight from the movement component's crouched half-height — so both overrides just push the
	 *  refreshed value onto the camera. Reacting to a state the movement component owns; not owning it (invariant 1). */
	virtual void OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;
	virtual void OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;

	//~IAbilitySystemInterface
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	//~End IAbilitySystemInterface

	/** Owner-client: request a server-authoritative reload (used by auto-reload when the mag empties). */
	void RequestReload();

	/** Server: apply contact damage from an enemy to this character's Health (clamped via HealthSet). */
	void ApplyContactDamage(float DamageAmount, AActor* DamageInstigator, FGameplayTag DamageType = FGameplayTag());

	/** Push the card/meta move-speed multiplier layer into the movement component. Called by UFPSRCombatSet when
	 *  MoveSpeedMultiplier changes (server + client). The resulting MaxWalkSpeed is composed there, not here. */
	void ApplyMoveSpeedMultiplier(float Mult);

	/** Push the downed (DBNO) locomotion layer + refresh enemy-pawn collision (U9 DBNO). Called server-side on
	 *  DBNO/revive and on the owning client from AFPSRPlayerState::OnRep_LifeState so movement prediction matches
	 *  (mirrors ApplyMoveSpeedMultiplier). */
	void ApplyDownedLocomotion(bool bDowned);

	/** Server: start a grace window of Seconds — i-frames in ApplyContactDamage + the capsule passes through enemy
	 *  pawns (ECC_Pawn) so the player can escape a surround. Used by the post-revive grace
	 *  (UFPSRReviveComponent::PerformRevive) and the post-card-selection resume grace (HandleRunStateChanged_Movement).
	 *  No-op off authority or for Seconds <= 0. (U9 §2-13) */
	void BeginGraceWindow(float Seconds);

	/** Local reviver HUD (U9 §2-13): if this player is alive and standing within a DBNO ally's revive radius, returns
	 *  that ally's revive progress (0..1) so the HUD can show a "reviving teammate" gauge; returns -1 when not reviving
	 *  anyone. Client-side query over already-replicated data (LifeState + ReviveProgress). */
	UFUNCTION(BlueprintPure, Category = "FPSR|Revive")
	float GetReviveTargetProgress() const;

	/** BlueprintPure aim-down-sights state. Forwards the aim flag on WeaponFire (which is only VisibleAnywhere, not
	 *  BlueprintReadable, so an AnimBP can't reach it directly — hence this accessor). Resets to false the instant ADS is
	 *  released (Input_ADSReleased -> SetAiming(false)), so an aim state driven by this reverts cleanly to hip.
	 *
	 *  VALID ON EVERY MACHINE — this is what the shared body AnimBP reads for the aim pose (ADR 0002 step 4). The owner
	 *  and the server write it directly (input edge / ServerSetAiming); non-owning clients receive it replicated
	 *  (COND_SkipOwner, push model). One accepted asymmetry: if the server REJECTS an aim-on (freeze / downed) the owner
	 *  is not sent a correction, so it can read true there for up to a round trip — the same OnRep that made the client's
	 *  view stale is what clears it. See UFPSRWeaponFireComponent::SetAiming + GetLifetimeReplicatedProps. */
	UFUNCTION(BlueprintPure, Category = "FPSR|Weapon")
	bool IsAiming() const;

	/** Owner-local ADS blend alpha (0 = hip, 1 = fully aimed). Only updated on the locally-controlled client. (W-U2) */
	UFUNCTION(BlueprintPure, Category = "FPSR|Weapon")
	float GetADSAlpha() const { return CurrentADSAlpha; }

	/** True while the owner-local ADS visual is active (procedural-sight blend alpha past the visual threshold — needs
	 *  an AimSocket). Reload-aware. Gates the scope overlay + weapon-hide (a scope IS a sight, so requiring the socket
	 *  is correct here). For the crosshair use IsADSFOVActive instead. (W-U2) */
	UFUNCTION(BlueprintPure, Category = "FPSR|Weapon")
	bool IsADSVisualActive() const;

	/** True while the weapon is visually committing to ADS (FOV zoom / sight): aiming an ADS weapon and not reloading.
	 *  AimSocket-INDEPENDENT (unlike IsADSVisualActive) so a bHasADS weapon without a procedural sight still reads as
	 *  aiming — it tracks the same commit the FOV zoom uses, so the crosshair-hide can't desync from the zoom.
	 *  Reload-aware. Owner-local. (W-U2) */
	UFUNCTION(BlueprintPure, Category = "FPSR|Weapon")
	bool IsADSFOVActive() const;

	/** True while a full-screen SCOPE is visually active: ADS visual active AND the currently-active sight part carries
	 *  a scope-overlay descriptor. Owner-local. (W-U2) */
	UFUNCTION(BlueprintPure, Category = "FPSR|Weapon")
	bool IsScopeVisualActive() const;

	/** Owner-local: resolve the effective camera FOV target for the ADS/scope interp (called from the weapon-fire tick).
	 *  Non-scope weapons return the passed base target unchanged; scope weapons apply the strong scope FOV and drop the
	 *  zoom during a reload (per design). bBaseWantsADS = the caller's existing (bIsAiming && Stats.bHasADS). (W-U2) */
	float ResolveADSTargetFOV(float DefaultFOV, float BaseADSFOV, bool bBaseWantsADS) const;

	/** Owner-local: hide/show the weapon meshes (and their child parts) based on whether a full-screen scope is active.
	 *  Called each frame from the weapon-fire tick (which already no-ops for non-local pawns). Component visibility is a
	 *  local render flag, not replicated, so this only clears the OWNER's view — teammates still see the gun. (W-U2) */
	void UpdateScopeWeaponVisibility();

	/** 활성 사이트의 스코프 오버레이 위젯 클래스(스코프 시각 활성 시). 없으면 null(HUD가 폴백 사용). 호출은 스코프
	 *  진입 엣지에서(프레임마다 아님) — 소프트 참조를 동기 로드한다. (스코프 위젯화) */
	UFUNCTION(BlueprintPure, Category = "FPSR|Weapon")
	TSubclassOf<UUserWidget> GetActiveScopeOverlayWidgetClass() const;

	/** True while a scope is active AND its descriptor requests the edge vignette. For the HUD scope overlay. (W-U2) */
	UFUNCTION(BlueprintPure, Category = "FPSR|Weapon")
	bool IsScopeVignetteEnabled() const;

	/** Refresh the equipped weapon's mesh + attachment + cached cosmetics when the equipped weapon changes (called from
	 *  the inventory's server EquipSlot + client OnRep). Runs on EVERY machine: one weapon mesh now serves the owner and
	 *  remote observers alike (ADR 0002), so a client that skipped this would render a teammate holding nothing. */
	void RefreshEquippedWeaponVisual();

	/** Owner-client: play the equipped weapon's per-shot cosmetics (fire montage + sound + muzzle flash). */
	void PlayWeaponFireCosmetics();

	/** Owner-client per-frame procedural aim-down-sights: place the WEAPON (relative to the camera) so its AimSocket sits
	 *  on the camera's forward centre-line when aiming, interpolated by the weapon's ADSInterpSpeed. Called from
	 *  UFPSRWeaponFireComponent::TickComponent (which already ticks + owns the aim state). No-op on remote pawns /
	 *  weapons without ADS or an AimSocket. Interpolates location AND rotation: when the weapon's bADSAlignRotation is
	 *  set it aligns the AimSocket frame to the camera, removing the authored hip cant so the sight reads level (else
	 *  translation-only, keeping the authored weapon tilt).
	 *
	 *  ADR 0002 measured the retargeted aim pose from the real eye position and found the sight +29.7 deg across / -27.1
	 *  deg down — outside a 55-deg ADS screen — so the pack's pose is a starting point, not a residual to nudge. The gun
	 *  is RE-PLACED against the camera, which is the single authored exception to invariant 4: remote observers keep the
	 *  weapon on the grip socket and read the aim from the body animation. Adding a second exception means re-opening
	 *  invariant 4 itself. */
	void UpdateAimDownSights(float DeltaTime);

	/** Left-hand grip anchor for the body AnimBP's two-bone IK (ADR 0002 step 4): the WORLD transform of the equipped
	 *  weapon's LeftHandSocket, resolved to whichever modular part carries it (handguard) or the receiver. Returns false
	 *  when this weapon authors no left-hand grip (melee / one-handed / unarmed) or the socket is missing — the pack's
	 *  bullpup handguards have no SOCKET_LeftHand, so the AnimBP must branch on this rather than assume a target.
	 *  Set the Two Bone IK node's effector space to World Space.
	 *  GAME THREAD ONLY: read it from NativeUpdateAnimation / BlueprintUpdateAnimation into a member, not from a
	 *  thread-safe update (it walks component attachment state). */
	UFUNCTION(BlueprintPure, Category = "FPSR|Weapon")
	bool GetLeftHandGripTransform(FTransform& OutGripWorld) const;

	/** Play reload cosmetics on a server-confirmed reload-start edge (called from UFPSRWeaponInstance::OnRep_Reloading,
	 *  which fires on every client holding the replicated instance). One body mesh, one montage, owner and remotes alike
	 *  (ADR 0002 invariant 4), plus the weapon's own magazine/bolt montage. No-op when bIsReloading is false, during the
	 *  level-up freeze, or when the equipped weapon has no reload montage. The play rate is scaled so the montage length
	 *  matches the ReloadTime. */
	void HandleReloadStateChanged(bool bIsReloading);

	/** W-U1 signature-diff rebuild: the equipped weapon's stat modifiers / behavior fragments changed (parts may need
	 *  to evolve). Coalesced to next tick, equipped-only, no-op on a dedicated server. Called from
	 *  UFPSRWeaponInstance::NotifyOwnerModifiersChanged and the PlayerState's AllWeapons-mod sites (mirrors the
	 *  cross-class notify pattern of HandleReloadStateChanged above). */
	void NotifyEquippedWeaponModifiersChanged(const UFPSRWeaponInstance* ChangedInstance);

	/** Server->all: play the spatialized fire SFX for REMOTE observers so teammates hear each other's weapon fire
	 *  (B4). The owning client already played it locally (PlayWeaponFireCosmetics), so the implementation early-outs
	 *  on IsLocallyControlled to avoid double-play. Unreliable (cosmetic — drops gracefully on packet loss). Fired
	 *  once per server-confirmed shot from FPSRWeaponHooks::NotifyFire (the central, all-weapons fire-confirm site). */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastFireCosmetics();

protected:
	/** Place the first-person camera for this frame: the current BaseEyeHeight plus whatever is left of the stance
	 *  transition's held-back offset. Runs every frame from Tick — the eye height has to keep moving after the stance
	 *  itself has already flipped. */
	void UpdateStanceCamera();

	/** Called from the crouch overrides once the capsule and BaseEyeHeight have already changed. Measures how far the
	 *  view was about to jump and holds it back by exactly that much, so UpdateStanceCamera can ease it away. */
	void BeginStanceCameraBlend();

	/** Hide this pawn's head while we are looking THROUGH its eyes, so True First Person doesn't render the inside of a
	 *  skull. ADR 0002 invariant 5: the condition is the VIEW TARGET, not "is this the local player" — a DBNO teammate
	 *  spectating this pawn looks through this camera too (head must go), and the later spectator/debug 3P rig becomes
	 *  the view target instead (head must come back). Local render state only; never replicated. */
	void UpdateFirstPersonBodyVisibility();

	/** Resolve which mesh carries the equipped weapon's LeftHandSocket — the modular part that owns it (handguard) or
	 *  the receiver. Null when this weapon authors no left-hand grip. Shared by the AnimBP getter. */
	UMeshComponent* ResolveLeftHandGripComponent() const;

	/** Full distance the eye travels between standing and crouching, in cm. Only used to bound the held-back offset. */
	float GetStanceEyeTravel() const;

	/** Hold a capsule-space point within the capsule's RADIUS (minus CameraCapsuleClampMargin), laterally only — the
	 *  height belongs to the stance system, which deliberately parks the view outside the capsule mid-crouch. This is
	 *  where invariant 2 is enforced: the shooting origin IS the camera, and the capsule can never bring its axis closer
	 *  than Radius to a wall, so bounding the camera to that radius makes "shot from past a wall you're pressed against"
	 *  structurally impossible instead of a value someone has to remember to keep small. Returns the held point;
	 *  OutClampedAmount gets how far it had to move (0 = untouched) for the debug readout. */
	FVector ClampPointInsideCapsule(const FVector& CapsuleSpacePoint, float& OutClampedAmount) const;

	void InitAbilitySystem();

	/** Server-authoritative run-start seam (U10): meta-progression stat effects are applied here, right after the ASC
	 *  actor info is initialized. Empty at U10 — the real GameplayEffects land in P0-③. Server-authoritative because
	 *  GAS attributes must be applied on the authority and replicate down; this does NOT commit to where the per-player
	 *  meta payload comes from (client-reported vs server-side) — that provenance is decided in P0-③. RunFlow §2-11. */
	void ApplyMetaProgressionEffects();

	/** True while the run is globally frozen for card selection (Game.MD §2-2) — gates player input. */
	bool IsRunFrozen() const;

	/** True when this player can't ACT (fire / swap / reload / ADS) — i.e. NOT alive (DBNO downed, or Dead).
	 *  Gates actions + contact damage like IsRunFrozen. DBNO still crawls/looks — those gate on IsTrulyDeadLocal. */
	bool IsIncapacitatedLocal() const;

	/** True only when truly Dead (out of the run) — blocks even crawl/look. DBNO returns false (can crawl + look). */
	bool IsTrulyDeadLocal() const;

	/** Bound to the health set's OnOutOfHealth (server). Transitions the player to DBNO (downed) + crawl and runs the
	 *  wipe check (team-wipe -> Defeat). Revive back to Alive is UFPSRReviveComponent (U9 Phase 1B, Game.MD §2-13). */
	void HandleOutOfHealth();

	//~ACharacter: no jumping while incapacitated (DBNO or Dead).
	virtual bool CanJumpInternal_Implementation() const override;

	/** Local client: react to GameState OnRunStateChanged — apply/clear the mission vision restriction PP. */
	UFUNCTION()
	void HandleRunStateChanged_Vision();

	/** Server (authority): on the run-freeze (§2-2) halt residual locomotion (e.g. an in-progress fall) so the player
	 *  can't drift across the frozen card screen. CMC replicates the stop. */
	UFUNCTION()
	void HandleRunStateChanged_Movement();

	/** Local client: try to bind to GameState OnRunStateChanged (GameState may arrive after BeginPlay). */
	void TryBindVisionDelegate();

	/** Local client: apply (true) or clear (false) the camera vision-restriction post-process. Idempotent. */
	void ApplyVisionRestriction(bool bRestricted);

	// Enhanced Input handlers
	void Input_MoveForward(const FInputActionValue& Value);
	void Input_MoveRight(const FInputActionValue& Value);
	void Input_Look(const FInputActionValue& Value);
	void Input_Fire(const FInputActionValue& Value);
	void Input_FireReleased(const FInputActionValue& Value);
	void Input_EquipSlot1(const FInputActionValue& Value);
	void Input_EquipSlot2(const FInputActionValue& Value);
	void Input_EquipSlot3(const FInputActionValue& Value);
	void Input_Reload(const FInputActionValue& Value);
	void Input_ADSPressed(const FInputActionValue& Value);
	void Input_ADSReleased(const FInputActionValue& Value);
	/** Crouch input, bound to Triggered (every frame the key is held) rather than Started. Held = crouch; pressing it
	 *  while running fast enough starts a SLIDE instead (the movement component decides — see
	 *  UFPSRCharacterMovementComponent::CanEnterSlide). This only forwards intent: the engine already ships
	 *  bWantsToCrouch in every move packet, so no extra RPC and no custom flag is needed.
	 *  Per-frame because Input_Jump CLEARS the crouch intent — a once-only Started binding could never restore it while
	 *  the key stayed down, so the player would land from a slide-jump and never crouch again until they let go. */
	void Input_CrouchHeld(const FInputActionValue& Value);
	void Input_CrouchReleased(const FInputActionValue& Value);

	/** Jump input. Drops the crouch/slide intent before jumping: crouch is a HELD state, so jumping without clearing it
	 *  leaves the intent set — the engine stands the player up mid-air, then re-crouches them the instant they land,
	 *  and every following jump is swallowed. Treating a jump press as "let go of crouch" is what makes slide-jump and
	 *  crouch-jump work while the key is still down. */
	void Input_Jump(const FInputActionValue& Value);
	/** Esc: open the settings overlay (delegates to the owning PC; non-pause overlay). */
	void Input_Menu(const FInputActionValue& Value);

	/** Server: equip a weapon slot (input is client-side; equip is server-authoritative). */
	UFUNCTION(Server, Reliable)
	void ServerEquipSlot(int32 SlotIndex);

	/** Server: begin reload (input is client-side; reload is server-authoritative). */
	UFUNCTION(Server, Reliable)
	void ServerReload();

	/** Server: sync aim-down-sights state so the fire GA applies ADS spread server-side. */
	UFUNCTION(Server, Reliable)
	void ServerSetAiming(bool bNewAiming);

	UPROPERTY()
	TObjectPtr<UFPSRAbilitySystemComponent> AbilitySystemComponent;

	UPROPERTY(VisibleAnywhere, Category = "FPSR|Camera")
	TObjectPtr<UCameraComponent> FirstPersonCamera;

	/** Fixed camera correction in CAPSULE space (X = forward, Y = right, Z = up), applied on top of BaseEyeHeight.
	 *
	 *  Not "where the eyes are" — it is the correction this CHARACTER + this CAPSULE need, which is why the name doesn't
	 *  promise anatomy. Without it the camera sits on the capsule's centre axis at neck height: looking forward is fine
	 *  (the body is behind and below the frustum), but looking DOWN aims the frustum straight into your own chest from
	 *  zero distance and you see its inside instead of its surface. Pushing the camera forward puts the chest behind and
	 *  below it, which is the whole fix — height is not what's wrong (ADR 0002 축 2).
	 *
	 *  BUDGET: UpdateStanceCamera holds the lateral result within the capsule radius (invariant 2 — the shooting origin
	 *  is this camera, so a camera reaching past a wall is a shot from past it). Capsule radius is 34, so X+Y together
	 *  have ~32cm to spend after the margin. Z is unbounded here because the stance system owns it.
	 *
	 *  Tune this on the BP CLASS DEFAULTS, not on a PIE instance — server and clients each compute the camera from their
	 *  own copy of this value, so a per-instance tweak makes them disagree about where shots start. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Camera")
	FVector FirstPersonCameraOffset = FVector(10.0f, 0.0f, 0.0f);

	/** Safety inset (cm) kept between the clamped camera and the capsule surface. Small on purpose: the project runs
	 *  NearClipPlane=1.0 (Config/DefaultEngine.ini, engine default is 10) so a couple of cm is enough to keep the near
	 *  plane on the inside. Raising the near plane instead would clip the first-person weapon too, which is why it was
	 *  lowered in the first place. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Camera", meta = (ClampMin = "0.0"))
	float CameraCapsuleClampMargin = 2.0f;

	/** Optional post-process material for the LimitedVision mission (tunnel/radial mask). When unset, a built-in
	 *  vignette fallback is used so the effect works without content. Assigned in the BP subclass (no hardcoded path). */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Vision")
	TObjectPtr<UMaterialInterface> VisionRestrictionMaterial = nullptr;

	/** Built-in fallback vignette intensity used when VisionRestrictionMaterial is unset. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Vision", meta = (ClampMin = "0.0"))
	float VisionVignetteIntensity = 1.4f;

	/** Weapon skeletal mesh (firearms), hanging off the BODY mesh's grip socket — one mesh for the owner and every
	 *  remote observer (ADR 0002). Mesh set from the equipped weapon's DataAsset. */
	UPROPERTY(VisibleAnywhere, Category = "FPSR|Mesh")
	TObjectPtr<USkeletalMeshComponent> WeaponMesh;

	/** Weapon static mesh (e.g. melee), same grip socket + same visibility as WeaponMesh above. */
	UPROPERTY(VisibleAnywhere, Category = "FPSR|Mesh")
	TObjectPtr<UStaticMeshComponent> WeaponMeshStatic;

	/** Socket on the BODY skeleton the weapon meshes attach to (authored on the grip hand — Blu: "SOCKET_Weapon" on
	 *  hand_R). C++-created component sockets can't be edited in the BP, so this exposes the default here; the
	 *  design-time preview attaches to it, and a weapon DA's WeaponAttachSocket overrides it per-weapon at equip.
	 *  Data, not a literal in code (ADR 0002 invariant 9) — swapping the player mesh must not need a recompile. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Mesh")
	FName WeaponAttachSocketName = FName(TEXT("SOCKET_Weapon"));

	/** Bone hidden while looking through this pawn's eyes (Blu: "head"; its children — hair, eyes, glasses — go with it,
	 *  engine: BVS_HiddenByParent). Data for the same reason as WeaponAttachSocketName: HideBoneByName does NOTHING and
	 *  logs NOTHING for a name the skeleton lacks, and this project has already swapped the player mesh twice. None
	 *  disables head hiding. UpdateFirstPersonBodyVisibility warns when the name doesn't resolve. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Mesh")
	FName HeadBoneName = FName(TEXT("head"));

	UPROPERTY(VisibleAnywhere, Category = "FPSR|Weapon")
	TObjectPtr<UFPSRWeaponInventoryComponent> WeaponInventory;

	UPROPERTY(VisibleAnywhere, Category = "FPSR|Weapon")
	TObjectPtr<UFPSRWeaponFireComponent> WeaponFire;

	/** Owner-local recoil + heat-spread driver (CrystalRecoil adapter, P1). Driven by WeaponFire (SetRecoilPattern on
	 *  equip, StartShooting/ApplyShot on fire). Applies to the controller so the server fire trace matches on-screen recoil. */
	UPROPERTY(VisibleAnywhere, Category = "FPSR|Weapon")
	TObjectPtr<UFPSRRecoilComponent> RecoilComponent;

	/** Co-op DBNO revive (U9 §2-13): server-authoritative proximity revive; replicates ReviveProgress for the HUD gauge. */
	UPROPERTY(VisibleAnywhere, Category = "FPSR|Revive")
	TObjectPtr<UFPSRReviveComponent> ReviveComponent;

	/** Local-only hit-marker + threat-indicator feedback (Game.MD §2-14). Not replicated; WBP HUD binds to it. */
	UPROPERTY(VisibleAnywhere, Category = "FPSR|Feedback")
	TObjectPtr<UFPSRPlayerFeedbackComponent> PlayerFeedback;

	/** Local-only blind-spot threat audio (Game.MD §2-14). Not replicated; warns by sound when an enemy is
	 *  close and outside the forward view (audio only — no visual indicator). */
	UPROPERTY(VisibleAnywhere, Category = "FPSR|Feedback")
	TObjectPtr<UFPSRBlindspotAudioComponent> BlindspotAudio;

	/** Starting weapons granted on possession (slot order). Assigned per-character in the BP class defaults
	 *  (EditDefaultsOnly); later folded into a HeroDataAsset. No hard-coded weapon paths in C++ (Game.md §6-2). */
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

	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Input")
	TObjectPtr<UInputAction> ReloadAction;

	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Input")
	TObjectPtr<UInputAction> ADSAction;

	/** Crouch / slide (one key — see Input_CrouchPressed). */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Input")
	TObjectPtr<UInputAction> CrouchAction;

	/** Esc — opens the settings overlay (non-pause). */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Input")
	TObjectPtr<UInputAction> MenuAction;

	/** Server: end the grace collision-ignore window (recomputes the enemy-pawn response). */
	void EndGraceWindow();

	/** Server: recompute the capsule's response to enemy pawns (ECC_Pawn) — ignore while within a grace window or
	 *  while downed (DBNO/Dead), block otherwise. Shared by the grace window + the downed state so neither restores
	 *  blocking while the other is still active. */
	void RefreshPawnCollisionResponse();

	/** Baseline walk speed before MoveSpeedMultiplier. Designers may tune per-hero. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Movement")
	float BaseWalkSpeed = 600.0f;


	/** Invulnerability window (seconds) after taking contact damage; further hits within it are ignored.
	 *  Prevents a swarm from melting the player in one frame. Balance-tunable. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Combat")
	float DamageInvulnerabilityDuration = 0.25f;

	/** Seconds of grace (invulnerable + enemy pass-through) granted when the card-selection freeze ENDS, so a player
	 *  who unfreezes surrounded by the swarm isn't hit the instant the world resumes (U9 §2-13). Balance value; 0 disables. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Combat", meta = (ClampMin = "0.0"))
	float PostFreezeInvulnSeconds = 3.0f;

	/** Server-only: world time of the last accepted contact hit (i-frame gate). */
	float LastDamagedTime = -1000.0f;

	/** Server-only: world time until which the player is invulnerable + passes through enemy pawns (grace window:
	 *  post-revive and post-card-selection resume, U9 §2-13). Set in BeginGraceWindow; read by ApplyContactDamage. */
	float GraceUntil = -1000.0f;

	/** Server-only: timer to end the grace collision-ignore window. */
	FTimerHandle GraceTimerHandle;

	/** Server-only: previous run-paused state (authority) — detects the card-selection freeze ENDING so the resume
	 *  grace fires once on the paused->unpaused transition. */
	bool bWasRunPausedAuth = false;

	/** Local-client: true while the vision-restriction PP is currently applied (idempotency guard). */
	bool bVisionRestrictionApplied = false;

	/** Local-client: saved camera vignette override flag/intensity (fallback path) so the camera's authored
	 *  settings are restored when the mission ends instead of being clobbered. */
	bool bSavedVignetteOverride = false;
	float SavedVignetteIntensity = 0.0f;

	/** Local-client: true once bound to GameState OnRunStateChanged. */
	bool bVisionDelegateBound = false;

	/** Local-client: retry timer for binding the vision delegate before GameState is replicated. */
	FTimerHandle VisionBindRetryTimerHandle;

	/** Cached hard refs for the currently-equipped weapon's fire cosmetics (resolved once on equip to avoid
	 *  per-shot soft-pointer loads). Refreshed in RefreshEquippedWeaponVisual. */
	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> CachedFireMontage;

	/** Cached hard ref for the equipped weapon's body reload montage. Refreshed on equip. */
	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> CachedReloadMontage;

	/** Cached hard refs for the equipped weapon MESH's bolt montages (fire/reload), played on WeaponMesh's own
	 *  AnimInstance so the bolt/magazine syncs with the body montages. Refreshed on equip. */
	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> CachedWeaponFireMontage;

	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> CachedWeaponReloadMontage;

	UPROPERTY(Transient)
	TObjectPtr<USoundBase> CachedFireSound;

	UPROPERTY(Transient)
	TObjectPtr<UParticleSystem> CachedMuzzleFlash;

	FName CachedMuzzleSocket = NAME_None;

	/** Rotation offset applied to the muzzle-flash emitter relative to the muzzle socket (owner-local cosmetic).
	 *  This pack's weapon-forward is +Y, so the flash needs a yaw offset to fire down the barrel (same reason as
	 *  ADSAimRotationOffset). Cached on equip from the weapon DA; designer-tuned per weapon. */
	FRotator CachedMuzzleFlashRotationOffset = FRotator::ZeroRotator;

	/** The weapon mesh currently shown (skeletal OR static — whichever the equipped weapon's DA provides). Fire
	 *  cosmetics (fire sound) attach here so they track the active mesh. Null when no weapon is equipped. */
	UPROPERTY(Transient)
	TObjectPtr<UMeshComponent> ActiveWeaponMesh;

	/** Component the muzzle flash attaches to at CachedMuzzleSocket. On modular weapons the muzzle socket lives on a
	 *  cosmetic PART (barrel/forestock) — swapping the part moves the muzzle — so RefreshWeaponPartComponents resolves
	 *  this to whichever part component carries CachedMuzzleSocket, falling back to ActiveWeaponMesh (receiver) when no
	 *  part provides it. Convention-based: no extra DA field — the part that owns a socket named MuzzleSocket wins. */
	UPROPERTY(Transient)
	TObjectPtr<UMeshComponent> CachedMuzzleComponent;

	/** Component the procedural-ADS AimSocket is read from. Like the muzzle, the sight (iron sight / optic) is a modular
	 *  PART, so RefreshWeaponPartComponents resolves this to whichever part component carries CachedAimSocket — swapping
	 *  the sight then moves the ADS reference — falling back to the receiver (ActiveWeaponMesh) when no part provides it.
	 *  Convention-based: the part that owns a socket named AimSocket wins (first in WeaponParts order). */
	UPROPERTY(Transient)
	TObjectPtr<UMeshComponent> CachedAimComponent;

	/** Component the left-hand grip socket is read from — same resolution as the aim socket above (the handguard is a
	 *  modular part, so swapping it moves the grip). Null when no part carries CachedLeftHandSocket; the getter then
	 *  falls back to the receiver. (ADR 0002 step 4 seam) */
	UPROPERTY(Transient)
	TObjectPtr<UMeshComponent> CachedLeftHandComponent;

	/** Scope descriptor of the currently-active sight part (the one carrying CachedAimSocket), captured alongside
	 *  CachedAimComponent in RebuildPartsFromSelection. Default (bScopeOverlay=false) when no scope sight is active.
	 *  Owner-local cosmetic; not replicated/saved. (W-U2) */
	FFPSRWeaponScopeDescriptor CachedScopeDescriptor;

	/** Tracks whether UpdateScopeWeaponVisibility currently has the weapon hidden for a scope, so it only toggles
	 *  visibility on change (and only ever manages the scope-hide state). Owner-local. (W-U2) */
	bool bWeaponHiddenForScope = false;

	/** Tracks the last head-visibility decision so UpdateFirstPersonBodyVisibility only touches bone visibility when the
	 *  view target actually changes (the steady state is a pointer compare). Local render state; never replicated. */
	bool bHeadHiddenForOwnView = false;

	// --- Procedural aim-down-sights (owner-local) ---
	/** Height (cm) the camera is currently held back from its nominal eye position, measured at the instant the stance
	 *  changed. Eased to 0 across the stance transition; 0 means no blend in flight. Owner-local presentation. */
	float CameraEyeOffsetStart = 0.0f;

	/** World Z the camera ended last frame at. The reference the next stance change holds the view to — measuring the
	 *  jump beats predicting it, because the capsule is re-anchored at the feet on the ground, at its centre in the
	 *  air, and not moved at all on a remote proxy. */
	float CachedCameraWorldZ = 0.0f;

#if ENABLE_DRAW_DEBUG
	/** How far the capsule clamp had to pull the camera back this frame (cm, 0 = untouched). Surfaced in the movement
	 *  readout so an authored FirstPersonCameraOffset that is being silently cut reads as "cut", not as "ignored". */
	float LastCameraClampAmount = 0.0f;
#endif

	/** Runtime ADS blend state: interpolated alpha (0 = hip, 1 = fully aimed) + the EXACT aim-pose WEAPON transform
	 *  (relative to the camera), recomputed each aiming frame. Blending hip<->aim by alpha (instead of chasing the live
	 *  target with a lagging transform interp) glues the sight onto the centre-line at full ADS, so body/weapon animation
	 *  sway/bob is cancelled AT THE SIGHT and the reticle holds steady rather than wobbling as an interp lags the
	 *  animated socket. The hip end of the blend is no longer a captured rest pose — it's the grip socket read fresh each
	 *  frame, because the weapon now rides an animated hand instead of a camera-parented arms component. */
	float CurrentADSAlpha = 0.0f;
	FVector ADSAimLoc = FVector::ZeroVector;
	FRotator ADSAimRot = FRotator::ZeroRotator;
	/** Equipped weapon's ADS params, cached on equip (RefreshEquippedWeaponVisual). */
	FName CachedAimSocket = NAME_None;
	/** Equipped weapon's left-hand grip socket (ADR 0002 step 4). None = no left-hand IK for this weapon. */
	FName CachedLeftHandSocket = NAME_None;
	float CachedADSSightDistance = 25.0f;
	bool bCachedHasADS = false;
	bool bCachedADSAlignRotation = true;
	FRotator CachedADSAimRotationOffset = FRotator::ZeroRotator;
	bool bCachedSuppressFireMontagesWhileADS = true;
	float CachedADSInterpSpeed = 12.0f;
	/** Fraction of the aim pose's animated positional bob allowed through while aiming (0 = sight fully glued to the
	 *  centre-line; >0 lets that fraction of the bob survive). Rotation stays fully glued regardless. */
	float CachedADSPositionBobScale = 0.0f;
	bool bCachedSuppressWeaponBoltWhileADS = false;
	float CachedADSMuzzleFlashScale = 0.35f;
	float CachedADSFireKickDegrees = 1.5f;
	float CachedADSFireKickRecoveryRate = 12.0f;
	/** Equipped weapon's ADS idle-sway params, cached on equip (owner-local cosmetic). Amplitudes in degrees about the
	 *  sight pivot (yaw = L-R, pitch = subtle up-down); speed = oscillation frequency. 0 amplitude = no sway. */
	float CachedADSSwayYawDegrees = 0.0f;
	float CachedADSSwayPitchDegrees = 0.0f;
	float CachedADSSwaySpeed = 1.2f;
	/** Current decaying ADS fire-kick angle (deg), owner-local. Bumped on each aimed shot, settled toward 0 each frame;
	 *  applied as a rotation about the AimSocket pivot in UpdateAimDownSights so the gun kicks while the sight stays put. */
	float ADSFireKickPitch = 0.0f;

	/** Smoothed 0..1 movement factor gating the ADS idle sway: 0 when the pawn is standing still (steady aim), ramps
	 *  to 1 with planar speed (relative to BaseWalkSpeed) so the handheld sway only lives while moving. Owner-local. */
	float ADSSwayMoveAlpha = 0.0f;

	// --- Hip-space procedural weapon motion (owner-local cosmetic; P1: look-sway + walk-bob + fire-kick) ---
	/** Control rotation captured last frame, to derive the per-frame aim delta that drives look-sway. Reset on equip
	 *  and initialised in BeginPlay so the first frame's delta isn't a huge jump. */
	FRotator PreviousControlRotation = FRotator::ZeroRotator;
	/** Interpolated look-sway (yaw/pitch degrees) the weapon lags behind the aim by; eases back to zero when the aim stops. */
	FRotator CurrentHipLookSway = FRotator::ZeroRotator;
	/** Accumulated walk-bob phase (cycles), advanced by dt * frequency * movement factor (frame-rate independent). */
	float HipBobPhase = 0.0f;
	/** Decaying per-shot hip fire-kick alpha (0..1). Bumped to 1 on each local shot (PlayWeaponFireCosmetics), settled
	 *  toward 0 each frame; scales the kick offset/pitch. */
	float HipFireKickAlpha = 0.0f;
	/** Equipped weapon's hip procedural-motion profile, cached on equip. */
	FFPSRProceduralWeaponMotionProfile CachedHipMotion;
	/** True when the cached profile has any non-zero amplitude (skip the whole hip block otherwise). */
	bool bCachedHasHipMotion = false;

	/** Runtime-created modular weapon-part components (U15), child-attached to WeaponMesh and rebuilt on each weapon
	 *  change. Visible to everyone, like the weapon they hang off (ADR 0002 — they used to be OnlyOwnerSee). Empty for
	 *  static/melee/partless weapons. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> WeaponPartComponents;

	/** W-U1: pending next-tick parts rebuild flag (coalesces a burst of modifier/fragment OnReps into one rebuild). */
	bool bWeaponPartsRebuildPending = false;
	/** W-U1: hash of the last-applied selected part set; ProcessPendingWeaponPartsRebuild skips rebuild when unchanged.
	 *  Transient, NOT replicated, NOT saved (§2-A gate②) — each machine recomputes from replicated stats/fragments. */
	uint32 LastWeaponPartSignature = 0;

	/** Destroy any existing modular part components and rebuild them from the equipped weapon's WeaponParts list
	 *  (only when a SKELETAL weapon mesh is shown; static/melee/empty attach nothing). Called from the weapon refresh. */
	void RefreshWeaponPartComponents(const UFPSRWeaponDataAsset* Weapon);

	/** W-U1: rebuild the modular parts from an already-computed selection (shared by equip + modifier-change paths).
	 *  Tears down existing part components, RESETS CachedMuzzle/Aim/LeftHandComponent, attaches the selection, then
	 *  re-resolves the muzzle / aim / left-hand source components. */
	void RebuildPartsFromSelection(const TArray<FFPSRWeaponPartAttachment>& Selected);

	/** W-U1 signature-diff rebuild (next-tick coalesced half of NotifyEquippedWeaponModifiersChanged, see the public
	 *  declaration above for the full contract). */
	void ProcessPendingWeaponPartsRebuild();

};
