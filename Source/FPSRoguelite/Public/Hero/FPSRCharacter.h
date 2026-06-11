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
class UMaterialInterface;
class UFPSRPlayerFeedbackComponent;
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
	void ApplyContactDamage(float DamageAmount, AActor* DamageInstigator);

protected:
	void InitAbilitySystem();

	/** True while the run is globally frozen for card selection (Game.MD §2-2) — gates player input. */
	bool IsRunFrozen() const;

	/** Bound to the health set's OnOutOfHealth (server). Placeholder: logs; full DBNO/respawn is P5. */
	void HandleOutOfHealth();

	/** Local client: react to GameState OnRunStateChanged — apply/clear the mission vision restriction PP. */
	UFUNCTION()
	void HandleRunStateChanged_Vision();

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

	UPROPERTY(VisibleAnywhere, Category = "FPSR|Weapon")
	TObjectPtr<UFPSRWeaponInventoryComponent> WeaponInventory;

	UPROPERTY(VisibleAnywhere, Category = "FPSR|Weapon")
	TObjectPtr<UFPSRWeaponFireComponent> WeaponFire;

	/** Local-only hit-marker + threat-indicator feedback (Game.MD §2-14). Not replicated; WBP HUD binds to it. */
	UPROPERTY(VisibleAnywhere, Category = "FPSR|Feedback")
	TObjectPtr<UFPSRPlayerFeedbackComponent> PlayerFeedback;

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

	/** Server: end the dash window — restore Pawn collision blocking. */
	void EndDash();

	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Dash")
	float DashSpeed = 2000.0f;

	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Dash")
	float DashDuration = 0.2f;

	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Dash")
	float DashCooldown = 2.0f;

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
};
