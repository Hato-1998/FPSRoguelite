// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "Weapon/FPSRWeaponTypes.h"
#include "FPSRWeaponInstance.generated.h"

class UFPSRWeaponDataAsset;
class UFPSRWeaponFragment;
class AFPSRPlayerState;

/**
 * One server-only timed crit buff (cards 3/5). Fragments are stateless shared assets, so the remaining duration has
 * nowhere to live but the weapon INSTANCE — which is exactly why the buff does not follow the weapon across a swap.
 * Expiry is measured against the freeze-paused combat clock (AFPSRGameState::GetCombatClockSeconds, VIT1), not world
 * time, so a card-selection freeze cannot eat into it.
 */
struct FFPSRTimedCritBuff
{
	/** Refresh key = the fragment ASSET that granted this buff (G1 P1-1), not a tag — this repo's fragment identity
	 *  convention is already "identity = asset pointer" (UFPSRWeaponInstance::HasFragment), so no extra authoring is
	 *  needed and two different cards can never stomp each other. The SAME fragment re-applying OVERWRITES (= card 5's
	 *  "sliding again resets the duration"); two DIFFERENT fragments' contributions ADD. */
	const UFPSRWeaponFragment* Source = nullptr;
	float ChanceAdd = 0.0f;
	float MultiplierAdd = 0.0f;
	/** Expiry, in combat-clock seconds. */
	float ExpiryCombatTime = 0.0f;
};

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

	// --- Timed crit buffs (CRIT1 cards 3/5): server-only, non-replicated (§7 — clients never learn about these) ---

	/** Server: add or refresh a timed crit buff. A buff with the same Source already active has its numbers and
	 *  expiry OVERWRITTEN; a different Source ADDS a new entry. No-op when `Source == nullptr`, `Duration <= 0`,
	 *  or this instance is not on the authority (authority = `GetTypedOuter<AActor>()->HasAuthority()` — the
	 *  instance lives inside a pawn's weapon inventory). If the slot is already full (4), the soonest-to-expire
	 *  entry is evicted first. */
	void ApplyTimedCritBuff(const UFPSRWeaponFragment* Source, float ChanceAdd, float MultiplierAdd, float Duration);

	/** Sum of every still-alive buff's contribution. Lazily prunes expired entries on the way, so **this unit adds
	 *  zero per-actor ticks**. Authority-independent — on a client the array is simply always empty, so this
	 *  naturally returns 0,0. Falls back to combat-clock 0 (i.e. treats every buff as expired) if no GameState can
	 *  be found (lobby / not-yet-running world). */
	void SumActiveCritBuffs(float& OutChanceAdd, float& OutMultiplierAdd);

	/** Clears every timed buff. Call sites: ① the weapon instance is (re)initialized — a fresh InitializeWithSource
	 *  ② a run ends and state resets. Deliberately NOT called on holster/weapon-swap — the buff belongs to the
	 *  INSTANCE (§3-③), so holstering and re-equipping the same weapon must not lose it. */
	void ClearTimedCritBuffs();

protected:
	UFUNCTION()
	void OnRep_Source();

	UFUNCTION()
	void OnRep_Modifiers();

	/** Client notify on the (already-replicated) reload flag — no new replication, just a RepNotify. Routes the
	 *  edge to the owning character's reload cosmetics: owner client plays the 1P arms ReloadMontage, remote
	 *  clients play the body ReloadMontage (event-driven; replaces per-frame AnimBP polling). */
	UFUNCTION()
	void OnRep_Reloading();

	/** Client notify: fragments changed (e.g. a Burst fragment flips the resolved fire mode). Invalidate the resolved
	 *  cache so the next GetResolvedStats() recomputes with the new fragment set. */
	UFUNCTION()
	void OnRep_ActiveFragments();

	void RecomputeResolved();

	AFPSRPlayerState* ResolveOwningPlayerState() const;

	/** W-U1: route a modifier/fragment change to the owning character so it can re-evaluate modular part selection. */
	void NotifyOwnerModifiersChanged();

	UPROPERTY(ReplicatedUsing = OnRep_Source)
	TObjectPtr<UFPSRWeaponDataAsset> Source;

	/** ThisWeapon-scope accumulated modifiers. */
	UPROPERTY(ReplicatedUsing = OnRep_Modifiers)
	FFPSRWeaponModContainer Modifiers;

	/** Accumulated behavior fragments (references to shared, stateless fragment assets). */
	UPROPERTY(ReplicatedUsing = OnRep_ActiveFragments)
	TArray<TObjectPtr<UFPSRWeaponFragment>> ActiveFragments;

	UPROPERTY(Replicated)
	int32 CurrentAmmo = 0;

	UPROPERTY(ReplicatedUsing = OnRep_Reloading)
	bool bReloading = false;

	// --- Transient resolved-stat cache (not replicated; computed on demand on both server and clients) ---
	FFPSRWeaponStatBlock CachedResolved;
	bool bResolvedDirty = true;

private:
	/** Server-only, **non-replicated**. The one UObject-shaped field, Source, is a raw const pointer to an
	 *  always-loaded fragment DA that this array only ever COMPARES against (never dereferences) — a plain (not
	 *  UPROPERTY) pointer is safe for that. Inline-4: the hot path never allocates (2 concurrent buffs is the
	 *  realistic ceiling; 4 is a safety margin, not a designed number). */
	TArray<FFPSRTimedCritBuff, TInlineAllocator<4>> ActiveCritBuffs;
};
