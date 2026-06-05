// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "Weapon/FPSRWeaponTypes.h"
#include "FPSRWeaponInstance.generated.h"

class UFPSRWeaponDataAsset;
class UFPSRWeaponFragment;
class AFPSRPlayerState;

/**
 * Runtime container for one equipped weapon (replicated subobject of UFPSRWeaponInventoryComponent).
 * Holds the source DataAsset, accumulated ThisWeapon stat modifiers, live ammo/reload state, and a cached
 * resolved stat block. Single home for both stat modifiers (P4-B-1) and behavior fragments (P4-B-2).
 *
 * Stat resolution = Source->BaseStats with each axis = (base + Σadditive) × (1 + Σpercent), accumulating the
 * instance's own Modifiers (ThisWeapon) and the owning PlayerState's AllWeaponsMods (AllWeapons).
 */
UCLASS(BlueprintType)
class FPSROGUELITE_API UFPSRWeaponInstance : public UObject
{
	GENERATED_BODY()

public:
	//~UObject networking
	virtual bool IsSupportedForNetworking() const override { return true; }
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Server: bind the source weapon DataAsset (call once right after NewObject). */
	void InitializeWithSource(UFPSRWeaponDataAsset* InSource);

	UFUNCTION(BlueprintPure, Category = "FPSR|Weapon")
	UFPSRWeaponDataAsset* GetSource() const { return Source; }

	/** Base stats with all accumulated modifiers applied (ThisWeapon + owner AllWeapons). Lazily recomputed. */
	const FFPSRWeaponStatBlock& GetResolvedStats();

	/** Server: append a ThisWeapon stat modifier (from a ThisWeapon-scope card). */
	void AddModifier(const FFPSRWeaponStatMod& Mod);

	// --- Behavior fragments (P4-B-2): data-driven hooks that change firing behavior ---
	const TArray<TObjectPtr<UFPSRWeaponFragment>>& GetActiveFragments() const { return ActiveFragments; }

	/** True if this fragment asset is already active on the weapon (identity = asset pointer). */
	bool HasFragment(const UFPSRWeaponFragment* Fragment) const;

	/** Server: add a behavior fragment (deduped). No effect on resolved stats. */
	void AddFragment(UFPSRWeaponFragment* Fragment);

	/** Invalidate the resolved-stat cache (call when AllWeapons mods change). */
	void MarkResolvedDirty() { bResolvedDirty = true; }

	// --- Ammo / reload state (server-authoritative; moved here from the inventory's parallel arrays) ---
	UFUNCTION(BlueprintPure, Category = "FPSR|Weapon")
	int32 GetCurrentAmmo() const { return CurrentAmmo; }

	UFUNCTION(BlueprintPure, Category = "FPSR|Weapon")
	bool IsReloading() const { return bReloading; }

	/** Server: set current ammo (marks dirty for replication). */
	void SetCurrentAmmo(int32 NewAmmo);

	/** Server: set reloading flag (marks dirty for replication). */
	void SetReloading(bool bNewReloading);

protected:
	UFUNCTION()
	void OnRep_Source();

	UFUNCTION()
	void OnRep_Modifiers();

	void RecomputeResolved();

	AFPSRPlayerState* ResolveOwningPlayerState() const;

	UPROPERTY(ReplicatedUsing = OnRep_Source)
	TObjectPtr<UFPSRWeaponDataAsset> Source;

	/** ThisWeapon-scope accumulated modifiers. */
	UPROPERTY(ReplicatedUsing = OnRep_Modifiers)
	FFPSRWeaponModContainer Modifiers;

	/** Accumulated behavior fragments (references to shared, stateless fragment assets). */
	UPROPERTY(Replicated)
	TArray<TObjectPtr<UFPSRWeaponFragment>> ActiveFragments;

	UPROPERTY(Replicated)
	int32 CurrentAmmo = 0;

	UPROPERTY(Replicated)
	bool bReloading = false;

	// --- Transient resolved-stat cache (not replicated; computed on demand on both server and clients) ---
	FFPSRWeaponStatBlock CachedResolved;
	bool bResolvedDirty = true;
};
