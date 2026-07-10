// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/Character.h"
#include "AbilitySystemInterface.h"
#include "GameplayTagContainer.h"
#include "Weapon/FPSRWeaponDataAsset.h"
#include "FPSRCharacter.generated.h"

class UAbilitySystemComponent;
class UFPSRAbilitySystemComponent;
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
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void NotifyControllerChanged() override;

	/** First-person spectate fix (U9 §2-13): a DBNO teammate views this pawn via SetViewTarget. When this pawn isn't
	 *  locally controlled, UCameraComponent skips bUsePawnControlRotation (it only follows the LOCAL controller) so the
	 *  spectator's view would track yaw (replicated actor rotation) but not pitch. Drive the view from
	 *  GetBaseAimRotation() — its pitch comes from the replicated RemoteViewPitch16 — so up/down aim is reflected. The
	 *  locally-controlled owner keeps the default camera-component path. */
	virtual void CalcCamera(float DeltaTime, struct FMinimalViewInfo& OutResult) override;

#if ENABLE_DRAW_DEBUG
	virtual void Tick(float DeltaSeconds) override;
#endif

	//~IAbilitySystemInterface
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	//~End IAbilitySystemInterface

	/** Owner-client: request a server-authoritative reload (used by auto-reload when the mag empties). */
	void RequestReload();

	/** Server: apply contact damage from an enemy to this character's Health (clamped via HealthSet). */
	void ApplyContactDamage(float DamageAmount, AActor* DamageInstigator, FGameplayTag DamageType = FGameplayTag());

	/** Set MaxWalkSpeed = BaseWalkSpeed * Mult. Called by UFPSRCombatSet when MoveSpeedMultiplier changes (server + client). */
	void ApplyMoveSpeedMultiplier(float Mult);

	/** Set MaxWalkSpeed for the downed (crawl) vs normal state (U9 DBNO). Called server-side on DBNO/revive and on the
	 *  owning client from AFPSRPlayerState::OnRep_LifeState so movement prediction matches (mirrors ApplyMoveSpeedMultiplier). */
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

	/** BlueprintPure aim-down-sights state for the 1P arms AnimBP: drive the ADS pose/state AND its EXIT transition off
	 *  this. Forwards the owner-local aim flag on WeaponFire (which is only VisibleAnywhere, not BlueprintReadable, so the
	 *  AnimBP can't reach it directly — hence this accessor). Resets to false the instant ADS is released
	 *  (Input_ADSReleased -> SetAiming(false)), so an aim state driven by this reverts cleanly to hip when leaving ADS. */
	UFUNCTION(BlueprintPure, Category = "FPSR|Weapon")
	bool IsAiming() const;

	/** Owner-client: refresh the first-person weapon mesh + arms anim when the equipped weapon changes
	 *  (called from the inventory's server EquipSlot + client OnRep). No-op on non-locally-controlled pawns. */
	void RefreshFirstPersonWeaponVisual();

	/** Owner-client: play the equipped weapon's per-shot cosmetics (fire montage + sound + muzzle flash). */
	void PlayWeaponFireCosmetics();

	/** Owner-client per-frame procedural aim-down-sights: interpolate the 1P arms (relative to the camera) so the equipped
	 *  weapon's AimSocket sits on the camera's forward centre-line when aiming, interpolated by the weapon's
	 *  ADSInterpSpeed. Called from UFPSRWeaponFireComponent::TickComponent (which already ticks + owns the aim state).
	 *  No-op on remote pawns / weapons without ADS or an AimSocket. Interpolates location AND rotation: when the weapon's
	 *  bADSAlignRotation is set it aligns the AimSocket frame to the camera, removing the authored hip cant so the sight
	 *  reads level (else translation-only, keeping the authored weapon tilt). */
	void UpdateAimDownSights(float DeltaTime);

	/** Play reload cosmetics on a server-confirmed reload-start edge (called from UFPSRWeaponInstance::OnRep_Reloading,
	 *  which fires on every client holding the replicated instance). Owner client -> 1P arms ReloadMontage; remote
	 *  clients -> 3P body ReloadMontage3P. No-op when bIsReloading is false, during the level-up freeze, or when the
	 *  equipped weapon has no reload montage. The play rate is scaled so the montage length matches the ReloadTime. */
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
	void InitAbilitySystem();

	/** Server-authoritative run-start seam (U10): meta-progression stat effects are applied here, right after the ASC
	 *  actor info is initialized. Empty at U10 — the real GameplayEffects land in P0-③. Server-authoritative because
	 *  GAS attributes must be applied on the authority and replicate down; this does NOT commit to where the per-player
	 *  meta payload comes from (client-reported vs server-side) — that provenance is decided in P0-③. RunFlow §2-11. */
	void ApplyMetaProgressionEffects();

	/** True while the run is globally frozen for card selection (Game.MD §2-2) — gates player input. */
	bool IsRunFrozen() const;

	/** True when this player can't ACT (fire / dash / swap / reload / ADS) — i.e. NOT alive (DBNO downed, or Dead).
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

	/** Server (authority): on the run-freeze (§2-2) halt residual locomotion — notably an in-flight dash impulse —
	 *  and cancel any in-progress dash so the player can't drift across the frozen card screen. CMC replicates the stop. */
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
	void Input_Dash(const FInputActionValue& Value);
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

	/** Server: perform a collision-ignoring dash in DashDirection (input is client-side; dash is server-authoritative). */
	UFUNCTION(Server, Reliable)
	void ServerDash(FVector DashDirection);

	UPROPERTY()
	TObjectPtr<UFPSRAbilitySystemComponent> AbilitySystemComponent;

	UPROPERTY(VisibleAnywhere, Category = "FPSR|Camera")
	TObjectPtr<UCameraComponent> FirstPersonCamera;

	/** Optional post-process material for the LimitedVision mission (tunnel/radial mask). When unset, a built-in
	 *  vignette fallback is used so the effect works without content. Assigned in the BP subclass (no hardcoded path). */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Vision")
	TObjectPtr<UMaterialInterface> VisionRestrictionMaterial = nullptr;

	/** Built-in fallback vignette intensity used when VisionRestrictionMaterial is unset. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Vision", meta = (ClampMin = "0.0"))
	float VisionVignetteIntensity = 1.4f;

	/** First-person arms, visible to the owning client only. */
	UPROPERTY(VisibleAnywhere, Category = "FPSR|Mesh")
	TObjectPtr<USkeletalMeshComponent> FirstPersonArms;

	/** First-person weapon skeletal mesh (firearms), owner-only. Mesh set from the equipped weapon's DataAsset. */
	UPROPERTY(VisibleAnywhere, Category = "FPSR|Mesh")
	TObjectPtr<USkeletalMeshComponent> WeaponMesh1P;

	/** First-person weapon static mesh (e.g. melee), owner-only. Mesh set from the equipped weapon's DataAsset. */
	UPROPERTY(VisibleAnywhere, Category = "FPSR|Mesh")
	TObjectPtr<UStaticMeshComponent> WeaponMeshStatic1P;

	/** Third-person weapon skeletal mesh (U19), attached to the 3P body mesh and visible to REMOTE observers only
	 *  (SetOwnerNoSee — the exact inverse of WeaponMesh1P's SetOnlyOwnerSee). Mesh set per-equip from the weapon DA. */
	UPROPERTY(VisibleAnywhere, Category = "FPSR|Mesh")
	TObjectPtr<USkeletalMeshComponent> WeaponMesh3P;

	/** Socket on FirstPersonArms the weapon meshes attach to (pack default "SOCKET_Weapon"). C++-created component
	 *  sockets can't be edited in the BP, so this exposes the default here; the design-time preview attaches to it,
	 *  and a weapon DA's WeaponAttachSocket overrides it per-weapon at equip. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Mesh")
	FName WeaponAttachSocketName = FName(TEXT("SOCKET_Weapon"));

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

	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Input")
	TObjectPtr<UInputAction> ReloadAction;

	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Input")
	TObjectPtr<UInputAction> ADSAction;

	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Input")
	TObjectPtr<UInputAction> DashAction;

	/** Esc — opens the settings overlay (non-pause). */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Input")
	TObjectPtr<UInputAction> MenuAction;

	/** Server: end the dash collision-ignore window (recomputes the enemy-pawn response — it may stay ignored if a
	 *  grace window is still active). */
	void EndDash();

	/** Server: end the grace collision-ignore window (recomputes the enemy-pawn response). */
	void EndGraceWindow();

	/** Server: recompute the capsule's response to enemy pawns (ECC_Pawn) — ignore while dashing OR within a grace
	 *  window (both derive from server timestamps, so an overlap composes correctly), block otherwise. Shared by dash
	 *  + grace window so neither restores blocking while the other is still active. */
	void RefreshPawnCollisionResponse();

	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Dash")
	float DashSpeed = 2000.0f;

	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Dash")
	float DashDuration = 0.2f;

	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Dash")
	float DashCooldown = 2.0f;

	/** Baseline walk speed before MoveSpeedMultiplier. Designers may tune per-hero. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement")
	float BaseWalkSpeed = 600.0f;

	/** Downed (DBNO) crawl speed as a fraction of the normal walk speed (Game.MD §2-13). Balance value. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DownedMoveScale = 0.3f;

	/** Server-only: world time of last dash (init far in the past so the first dash is allowed). */
	float LastDashTime = -1000.0f;

	/** Server-only: timer to end the dash collision-ignore window. */
	FTimerHandle DashEndTimerHandle;

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
	 *  per-shot soft-pointer loads). Refreshed in RefreshFirstPersonWeaponVisual. */
	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> CachedFireMontage;

	/** Cached hard ref for the equipped weapon's 1P arms reload montage (owner-only-used). Refreshed on equip. */
	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> CachedReloadMontage;

	/** Cached hard refs for the equipped weapon MESH's bolt montages (fire/reload), played on WeaponMesh1P's own
	 *  AnimInstance so the bolt/magazine syncs with the arm montages (owner-only-used). Refreshed on equip. */
	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> CachedWeaponFireMontage;

	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> CachedWeaponReloadMontage;

	UPROPERTY(Transient)
	TObjectPtr<USoundBase> CachedFireSound;

	UPROPERTY(Transient)
	TObjectPtr<UParticleSystem> CachedMuzzleFlash;

	FName CachedMuzzleSocket = NAME_None;

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
	 *  the sight then moves the ADS reference — falling back to the receiver (WeaponMesh1P) when no part provides it.
	 *  Convention-based: the part that owns a socket named AimSocket wins (first in WeaponParts1P order). */
	UPROPERTY(Transient)
	TObjectPtr<UMeshComponent> CachedAimComponent;

	// --- Procedural aim-down-sights (owner-local) ---
	/** FirstPersonArms relative-to-camera transform captured on BeginPlay (the "hip" base the ADS interps to/from). */
	FVector BaseArmsRelLoc = FVector::ZeroVector;
	FRotator BaseArmsRelRot = FRotator::ZeroRotator;
	/** Runtime ADS blend state: interpolated alpha (0 = hip, 1 = fully aimed) + the EXACT aim-pose arms transform,
	 *  recomputed each aiming frame. Blending hip<->aim by alpha (instead of chasing the live target with a lagging
	 *  transform interp) glues the sight onto the centre-line at full ADS, so weapon/arm animation sway/bob is cancelled
	 *  AT THE SIGHT and the reticle holds steady rather than wobbling as an interp lags the animated socket. */
	float CurrentADSAlpha = 0.0f;
	FVector ADSAimLoc = FVector::ZeroVector;
	FRotator ADSAimRot = FRotator::ZeroRotator;
	/** Equipped weapon's ADS params, cached on equip (RefreshFirstPersonWeaponVisual). */
	FName CachedAimSocket = NAME_None;
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

	/** Runtime-created modular weapon-part components (U15), child-attached to WeaponMesh1P and rebuilt on each
	 *  weapon change. Owner-only-visible (match the 1P weapon mesh). Empty for static/melee/partless weapons. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> WeaponPartComponents1P;

	/** W-U1: pending next-tick parts rebuild flag (coalesces a burst of modifier/fragment OnReps into one rebuild). */
	bool bWeaponPartsRebuildPending = false;
	/** W-U1: hash of the last-applied selected part set; ProcessPendingWeaponPartsRebuild skips rebuild when unchanged.
	 *  Transient, NOT replicated, NOT saved (§2-A gate②) — each machine recomputes from replicated stats/fragments. */
	uint32 LastWeaponPartSignature = 0;

	/** Destroy any existing modular part components and rebuild them from the equipped weapon's WeaponParts1P list
	 *  (only when a SKELETAL weapon mesh is shown; static/melee/empty attach nothing). Called from the weapon refresh. */
	void RefreshWeaponPartComponents(const UFPSRWeaponDataAsset* Weapon);

	/** W-U1: rebuild the modular parts from an already-computed selection (shared by equip + modifier-change paths).
	 *  Tears down existing part components, RESETS CachedMuzzle/AimComponent, attaches the selection, then re-resolves
	 *  the muzzle/aim source components. */
	void RebuildPartsFromSelection(const TArray<FFPSRWeaponPartAttachment>& Selected);

	/** W-U1 signature-diff rebuild (next-tick coalesced half of NotifyEquippedWeaponModifiersChanged, see the public
	 *  declaration above for the full contract). */
	void ProcessPendingWeaponPartsRebuild();

	/** Arms anim default captured at BeginPlay, so a weapon's per-weapon ArmsAnimInstanceClass override can be
	 *  reverted when the next weapon has none. Only touched once an override has actually been applied
	 *  (bArmsAnimOverridden), so weapons that never set an override leave the BP-authored arms anim untouched. */
	bool bArmsAnimOverridden = false;
	bool bDefaultArmsUsesBlueprint = false;
	UPROPERTY(Transient)
	TSubclassOf<UAnimInstance> DefaultArmsAnimClass;
};
