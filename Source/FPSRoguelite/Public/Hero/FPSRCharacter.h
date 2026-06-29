// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/Character.h"
#include "AbilitySystemInterface.h"
#include "GameplayTagContainer.h"
#include "FPSRCharacter.generated.h"

class UAbilitySystemComponent;
class UFPSRAbilitySystemComponent;
class UCameraComponent;
class USkeletalMeshComponent;
class UInputAction;
class UFPSRWeaponInventoryComponent;
class UFPSRWeaponFireComponent;
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

	/** Owner-client: refresh the first-person weapon mesh + arms anim when the equipped weapon changes
	 *  (called from the inventory's server EquipSlot + client OnRep). No-op on non-locally-controlled pawns. */
	void RefreshFirstPersonWeaponVisual();

	/** Owner-client: play the equipped weapon's per-shot cosmetics (fire montage + sound + muzzle flash). */
	void PlayWeaponFireCosmetics();

	/** Server->all: play the spatialized fire SFX for REMOTE observers so teammates hear each other's weapon fire
	 *  (B4). The owning client already played it locally (PlayWeaponFireCosmetics), so the implementation early-outs
	 *  on IsLocallyControlled to avoid double-play. Unreliable (cosmetic — drops gracefully on packet loss). Fired
	 *  once per server-confirmed shot from FPSRWeaponHooks::NotifyFire (the central, all-weapons fire-confirm site). */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastFireCosmetics();

protected:
	void InitAbilitySystem();

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

	/** Socket on FirstPersonArms the weapon meshes attach to (pack default "SOCKET_Weapon"). C++-created component
	 *  sockets can't be edited in the BP, so this exposes the default here; the design-time preview attaches to it,
	 *  and a weapon DA's WeaponAttachSocket overrides it per-weapon at equip. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Mesh")
	FName WeaponAttachSocketName = FName(TEXT("SOCKET_Weapon"));

	UPROPERTY(VisibleAnywhere, Category = "FPSR|Weapon")
	TObjectPtr<UFPSRWeaponInventoryComponent> WeaponInventory;

	UPROPERTY(VisibleAnywhere, Category = "FPSR|Weapon")
	TObjectPtr<UFPSRWeaponFireComponent> WeaponFire;

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

	/** Server: end the dash window — restore Pawn collision blocking. */
	void EndDash();

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

	/** Server-only: world time of the last accepted contact hit (i-frame gate). */
	float LastDamagedTime = -1000.0f;

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

	UPROPERTY(Transient)
	TObjectPtr<USoundBase> CachedFireSound;

	UPROPERTY(Transient)
	TObjectPtr<UParticleSystem> CachedMuzzleFlash;

	FName CachedMuzzleSocket = NAME_None;

	/** The weapon mesh currently shown (skeletal OR static — whichever the equipped weapon's DA provides). Fire
	 *  cosmetics (muzzle flash / sound) attach here so they track the active mesh. Null when no weapon is equipped. */
	UPROPERTY(Transient)
	TObjectPtr<UMeshComponent> ActiveWeaponMesh;

	/** Arms anim default captured at BeginPlay, so a weapon's per-weapon ArmsAnimInstanceClass override can be
	 *  reverted when the next weapon has none. Only touched once an override has actually been applied
	 *  (bArmsAnimOverridden), so weapons that never set an override leave the BP-authored arms anim untouched. */
	bool bArmsAnimOverridden = false;
	bool bDefaultArmsUsesBlueprint = false;
	UPROPERTY(Transient)
	TSubclassOf<UAnimInstance> DefaultArmsAnimClass;
};
