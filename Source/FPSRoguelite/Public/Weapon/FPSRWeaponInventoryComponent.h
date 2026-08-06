// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"
#include "GameplayAbilitySpecHandle.h"
#include "Weapon/FPSRWeaponTypes.h"
#include "FPSRWeaponInventoryComponent.generated.h"

class UFPSRWeaponDataAsset;
class UFPSRWeaponInstance;
class UAbilitySystemComponent;
class AFPSRGameState;

/**
 * What one inventory slot is for. Designers author these on the inventory component in the character Blueprint;
 * nothing in C++ names a specific slot, so adding a slot (or changing which archetypes a slot takes) is a data edit.
 *
 * The alternative — an EWeaponSlotKind enum with an archetype->kind mapping — was rejected because that mapping is a
 * central switch every new archetype has to be added to (project rule: new conventions are data + a small subclass,
 * with zero central edits).
 */
USTRUCT(BlueprintType)
struct FPSROGUELITE_API FFPSRWeaponSlotDefinition
{
	GENERATED_BODY()

	//~ EditAnywhere, not EditDefaultsOnly, on both members below. Where this struct can be edited is already decided by
	//~ the property that holds it (UFPSRWeaponInventoryComponent::SlotDefinitions is EditDefaultsOnly, so the array
	//~ never appears on a placed instance); repeating the restriction on the members buys nothing and blocks headless
	//~ authoring — the Python bridge treats a loose struct as an instance and refuses DisableEditOnInstance fields.

	/** Archetypes this slot takes. A NON-EMPTY list is an EXCLUSIVE claim: those archetypes go only here, so listing
	 *  Melee on the third slot is by itself what makes the first two refuse a knife. An EMPTY list means "everything
	 *  no other slot has claimed", which is why the ranged slots need no editing when a new ranged archetype appears.
	 *  Resolution is slot-order independent — the claims are gathered first, then the leftovers are worked out. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "슬롯", meta = (DisplayName = "받는 아키타입(비우면=다른 칸이 안 가져간 나머지)"))
	TArray<EFPSRWeaponArchetype> AcceptedArchetypes;

	/** Weapon this slot holds when nothing has been picked up for it — bare hands on the melee slot. Filling the slot
	 *  instead of allowing an empty one to be equipped is what keeps every "no weapon equipped" branch out of the
	 *  equip / fire / ability / visual code: the slot is simply never empty. A picked-up weapon REPLACES this one
	 *  rather than needing a free slot. Null = this slot really does start empty. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "슬롯", meta = (DisplayName = "기본 무기(맨손 등, 비우면 빈 칸)"))
	TObjectPtr<UFPSRWeaponDataAsset> DefaultWeapon = nullptr;
};

/**
 * Server-authoritative weapon inventory. Grants the equipped weapon's fire ability.
 *
 * Slots are TYPED, not interchangeable: what each one takes comes from SlotDefinitions, which ships as two ranged
 * slots plus a melee slot holding bare hands. Two consequences worth knowing before touching this class:
 *  - "is there a free slot" is always a per-archetype question (HasFreeSlotFor). A melee slot being open does not
 *    mean a rifle can be granted, and answering it the old way is how an unlock card gets offered and then grants
 *    nothing.
 *  - a slot holding only its DefaultWeapon counts as free — a pickup REPLACES bare hands instead of needing a slot
 *    of its own. That is the single exception to "no weapon swapping" (§2-4).
 */
UCLASS(ClassGroup = (FPSR), meta = (BlueprintSpawnableComponent))
class FPSROGUELITE_API UFPSRWeaponInventoryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UFPSRWeaponInventoryComponent();

	/** Hard ceiling on slot count. The real count comes from SlotDefinitions; this only bounds a mis-authored array
	 *  (there are exactly three equip inputs, so a fourth slot would be unreachable until IA/IMC work adds a key). */
	static constexpr int32 MaxSlots = 3;

	/** What each slot takes and what it falls back to. Authored per-character in the Blueprint's component defaults;
	 *  the C++ defaults describe the shipping layout (two ranged, one melee-with-bare-hands) minus the content
	 *  pointer, which cannot be named here (no hard-coded asset paths in C++, §6-2). */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Weapon", meta = (DisplayName = "슬롯 정의"))
	TArray<FFPSRWeaponSlotDefinition> SlotDefinitions;

	/** Minimum delay (seconds) after a weapon swap before the next shot may fire. Prevents bypassing a weapon's
	 *  fire cadence by rapidly swapping slots; also the natural "weapon swap" feel. Applied on both the server gate
	 *  (ServerNextAllowedFireTime) and the owning client's recoil prediction (FireComponent). */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Weapon", meta = (ClampMin = "0.0"))
	float EquipFireCooldown = 0.2f;

	/** Server: add weapon to the first slot that ACCEPTS its archetype and is either empty or still holding only its
	 *  default weapon (bare hands); auto-equips if nothing is equipped. Returns slot index or INDEX_NONE. */
	int32 AddWeapon(UFPSRWeaponDataAsset* WeaponData);

	/** Server: fill every still-empty slot with its DefaultWeapon, WITHOUT equipping anything. Idempotent by
	 *  construction — it only ever writes to a null slot — so re-possession or a seamless travel that runs the spawn
	 *  path again cannot overwrite a knife the player picked up with bare hands again.
	 *  Call before granting the starting weapons so the first of those still auto-equips. */
	void ServerSeedDefaultSlots();

	/** True if a weapon of this archetype could be added right now: some slot accepts it AND is empty or holding only
	 *  its default weapon. Archetype-aware because slots are typed — "the melee slot is free" must not make a rifle
	 *  unlock card look grantable. Const; runs on server and client. */
	bool HasFreeSlotFor(EFPSRWeaponArchetype Archetype) const;

	/** True if ANY slot could still take some weapon. A cheap early-out only — the per-archetype test above is what
	 *  decides whether a specific weapon fits. */
	bool HasAnyFreeSlot() const;

	/** True when the slot at this index accepts the given archetype, per the exclusive-claim rule documented on
	 *  FFPSRWeaponSlotDefinition::AcceptedArchetypes. */
	bool SlotAcceptsArchetype(int32 SlotIndex, EFPSRWeaponArchetype Archetype) const;

	/** Server: equip the given slot index (grants its fire ability). */
	void EquipSlot(int32 SlotIndex);

	UFUNCTION(BlueprintPure, Category = "FPSR|Weapon")
	UFPSRWeaponDataAsset* GetCurrentWeapon() const;

	/** Runtime instance of the currently equipped weapon (holds modifiers / resolved stats / ammo). */
	UFUNCTION(BlueprintPure, Category = "FPSR|Weapon")
	UFPSRWeaponInstance* GetCurrentInstance() const;

	/** Weapons the player actually OWNS, in slot order. Slot default weapons (bare hands) are excluded via their DA's
	 *  bExcludeFromProgression, so they can't seed the card pool, can't satisfy a "player has a weapon" check and
	 *  can't be offered as a lobby starting weapon. */
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

	/** Server: cancel an in-progress reload on the current slot (clears the reload timer + reloading flag). Used when
	 *  the magazine became full by other means mid-reload (e.g. instant reload-on-kill) so the fire gate unblocks now. */
	void CancelReload();

	/** Server: returns true if the current weapon's fire interval has elapsed since the last shot, then stamps
	 *  the next-allowed time. A jitter tolerance keeps legitimate fire from being blocked — this is anti-abuse,
	 *  not exact cadence. On non-authority (client prediction) it always returns true (the server is authoritative). */
	bool ServerTryConsumeFireInterval(float MinInterval);

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

#if WITH_EDITOR
	/** Catches slot layouts that can't work at all: more slots than there are equip inputs, an archetype no slot
	 *  accepts (a weapon nobody could ever be given), two slots claiming the same archetype, or a default weapon the
	 *  slot holding it would reject. */
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif

protected:
	/** Size Slots from SlotDefinitions here rather than in the constructor: a Blueprint's override of SlotDefinitions
	 *  is deserialized after the constructor has run, so the constructor only ever sees the C++ default count. */
	virtual void InitializeComponent() override;

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

	/** Server: put (or replace) a weapon in one slot. The ONLY place a slot's instance is created or dropped, so
	 *  registering the new subobject for replication and UNregistering the old one can't be forgotten on one of the
	 *  paths — a stale default-weapon instance left in the registered list would keep replicating after a knife
	 *  replaced it. Pass bAutoEquip=false when seeding defaults so seeding can't steal the starting weapon's equip. */
	void SetSlotWeapon(int32 SlotIndex, UFPSRWeaponDataAsset* WeaponData, bool bAutoEquip);

	/** True when this slot currently holds nothing but its authored DefaultWeapon — i.e. it is free for a real pickup
	 *  even though it isn't null. */
	bool IsSlotHoldingOnlyDefault(int32 SlotIndex) const;

	/** Index of the slot a weapon of this archetype would go into, or INDEX_NONE. Shared by AddWeapon and
	 *  HasFreeSlotFor so "where does it go" and "does it fit" can never disagree — that disagreement is how a card
	 *  gets offered and then silently fails to grant anything. */
	int32 FindFreeSlotFor(EFPSRWeaponArchetype Archetype) const;

	/** Push the equipped weapon's WalkSpeed into the movement component (0 = character default). Called from the
	 *  server equip and from both OnReps, so every machine applies it from its own authoritative view of the slot.
	 *  Deliberately NOT predicted on input: the server can reject an equip (freeze / downed) without replicating
	 *  anything back, which would leave a client that had already applied the faster speed stuck with it. */
	void PushEquippedWalkSpeed();

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
