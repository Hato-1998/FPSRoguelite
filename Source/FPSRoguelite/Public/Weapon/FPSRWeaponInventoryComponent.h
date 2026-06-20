// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"
#include "GameplayAbilitySpecHandle.h"
#include "FPSRWeaponInventoryComponent.generated.h"

class UFPSRWeaponDataAsset;
class UFPSRWeaponInstance;
class UAbilitySystemComponent;
class AFPSRGameState;

/** Server-authoritative 3-slot weapon inventory. Grants the equipped weapon's fire ability. */
UCLASS(ClassGroup = (FPSR), meta = (BlueprintSpawnableComponent))
class FPSROGUELITE_API UFPSRWeaponInventoryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UFPSRWeaponInventoryComponent();

	static constexpr int32 MaxSlots = 3;

	/** Minimum delay (seconds) after a weapon swap before the next shot may fire. Prevents bypassing a weapon's
	 *  fire cadence by rapidly swapping slots; also the natural "weapon swap" feel. Applied on both the server gate
	 *  (ServerNextAllowedFireTime) and the owning client's recoil prediction (FireComponent). */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Weapon", meta = (ClampMin = "0.0"))
	float EquipFireCooldown = 0.2f;

	/** Server: add weapon to the first free slot; auto-equips if nothing is equipped. Returns slot index or INDEX_NONE. */
	int32 AddWeapon(UFPSRWeaponDataAsset* WeaponData);

	/** True if any inventory slot is empty (a new weapon can be added). Const; runs on server and client. */
	bool HasFreeSlot() const;

	/** Server: equip the given slot index (grants its fire ability). */
	void EquipSlot(int32 SlotIndex);

	UFUNCTION(BlueprintPure, Category = "FPSR|Weapon")
	UFPSRWeaponDataAsset* GetCurrentWeapon() const;

	/** Runtime instance of the currently equipped weapon (holds modifiers / resolved stats / ammo). */
	UFUNCTION(BlueprintPure, Category = "FPSR|Weapon")
	UFPSRWeaponInstance* GetCurrentInstance() const;

	UFUNCTION(BlueprintPure, Category = "FPSR|Weapon")
	TArray<UFPSRWeaponDataAsset*> GetOwnedWeapons() const;

	/** Owned weapon instance whose source DataAsset matches Weapon (any slot, equipped or not). null if not owned.
	 *  Lets weapon-scope cards apply to their source weapon rather than the currently equipped one. */
	UFPSRWeaponInstance* GetInstanceForWeapon(const UFPSRWeaponDataAsset* Weapon) const;

	/** Invalidate every weapon instance's resolved-stat cache (e.g. after an AllWeapons modifier changes). */
	void MarkAllInstancesResolvedDirty();

	UFUNCTION(BlueprintPure, Category = "FPSR|Weapon")
	int32 GetCurrentSlotIndex() const { return CurrentSlotIndex; }

	UFUNCTION(BlueprintPure, Category = "FPSR|Weapon")
	int32 GetCurrentAmmo() const;

	UFUNCTION(BlueprintPure, Category = "FPSR|Weapon")
	int32 GetCurrentMagSize() const;

	UFUNCTION(BlueprintPure, Category = "FPSR|Weapon")
	bool IsReloading() const;

	/** Server: consume ammo from the current slot. Returns false if insufficient. */
	bool ConsumeAmmo(int32 Amount = 1);

	/** Server: begin a timed reload of the current slot (no-op if already reloading or full). */
	void StartReload();

	/** Server: returns true if the current weapon's fire interval has elapsed since the last shot, then stamps
	 *  the next-allowed time. A jitter tolerance keeps legitimate fire from being blocked — this is anti-abuse,
	 *  not exact cadence. On non-authority (client prediction) it always returns true (the server is authoritative). */
	bool ServerTryConsumeFireInterval(float MinInterval);

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION()
	void OnRep_CurrentSlotIndex();

	/** Client: the weapon instances (and their Source) may replicate AFTER CurrentSlotIndex on a remote owning
	 *  client; refresh the first-person weapon visual here too so the initial weapon mesh isn't left blank until a
	 *  manual slot switch. */
	UFUNCTION()
	void OnRep_Slots();

	/** Server: pause/resume the in-flight reload timer when the run freezes/unfreezes (Game.MD §2-2), so a reload
	 *  cannot complete during the card-selection freeze. Bound to the GameState's OnRunStateChanged. */
	UFUNCTION()
	void HandleRunStateChanged();

	/** Server timer callback: refill current slot to MagSize. */
	void FinishReload();

	UAbilitySystemComponent* GetOwnerASC() const;

	/** Server: clears the previous fire ability and grants the current weapon's fire ability. */
	void RefreshEquippedAbility();

	/** One runtime instance per slot (nullptr = empty). Replicated as registered subobjects. */
	UPROPERTY(ReplicatedUsing = OnRep_Slots)
	TArray<TObjectPtr<UFPSRWeaponInstance>> Slots;

	UPROPERTY(ReplicatedUsing = OnRep_CurrentSlotIndex)
	int32 CurrentSlotIndex = -1;

	/** Server-only handle to the currently granted fire ability. */
	FGameplayAbilitySpecHandle GrantedFireAbilityHandle;

	/** Server-only reload timer. */
	FTimerHandle ReloadTimerHandle;

	/** Server-only: slot whose reload was cancelled by a weapon switch; re-reloads when re-equipped. */
	int32 PendingReloadSlot = INDEX_NONE;

	/** Server-only: earliest world time (seconds) the next shot is allowed for the current slot. Reset on equip. */
	float ServerNextAllowedFireTime = 0.0f;

	/** Server-only: the GameState we bound OnRunStateChanged to (cached so EndPlay can unbind the exact object). */
	TWeakObjectPtr<AFPSRGameState> BoundRunState;
};
