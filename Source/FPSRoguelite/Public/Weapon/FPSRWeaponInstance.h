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

	/** Number of copies (stacks) of this fragment currently active on the weapon. */
	int32 GetFragmentStackCount(const UFPSRWeaponFragment* Fragment) const;

	/** Distinct behavior fragments currently on the weapon, in first-appearance order (stacks collapse to one entry).
	 *  The replace-index in the swap flow indexes into THIS list — identical on server and clients (ActiveFragments
	 *  replicates in order), so a client's chosen drop index validates against the same list server-side. */
	void GetDistinctFragments(TArray<UFPSRWeaponFragment*>& OutFragments) const;

	/** Number of DISTINCT behavior fragments (stacks of one fragment count as a single slot). */
	int32 GetDistinctFragmentCount() const;

	/** Per-weapon distinct-fragment slot cap (from the source DA; fallback 3 when unset). */
	int32 GetMaxFragmentSlots() const;

	/** True when the weapon already holds GetMaxFragmentSlots() distinct fragments (a new distinct pick needs a swap). */
	bool IsAtFragmentSlotCap() const;

	/** Server: add a behavior fragment. Rejected (returns false) when the fragment is already at MaxStacks, OR when it
	 *  is a NEW distinct fragment and the weapon is at its slot cap (stacking an already-held fragment ignores the cap). */
	bool AddFragment(UFPSRWeaponFragment* Fragment);

	/** Server: remove a behavior fragment entirely (all stacks). Used by the at-cap replacement flow. */
	void RemoveFragment(UFPSRWeaponFragment* Fragment);

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
