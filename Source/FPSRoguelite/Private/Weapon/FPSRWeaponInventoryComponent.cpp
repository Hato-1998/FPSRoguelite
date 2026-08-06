// Copyright Epic Games, Inc. All Rights Reserved.

#include "Weapon/FPSRWeaponInventoryComponent.h"
#include "Weapon/FPSRWeaponDataAsset.h"
#include "Weapon/FPSRWeaponInstance.h"
#include "Weapon/FPSRWeaponFireComponent.h"
#include "Hero/FPSRCharacter.h"
#include "Hero/FPSRCharacterMovementComponent.h"
#include "Core/FPSRGameState.h"
#include "Core/FPSRLogChannels.h"

#include "AbilitySystemComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "AbilitySystemGlobals.h"
#include "Abilities/GameplayAbility.h"
#include "GameplayAbilitySpec.h"
#include "Net/UnrealNetwork.h"
#include "Net/Core/PushModel/PushModel.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "FPSRWeaponInventory"

UFPSRWeaponInventoryComponent::UFPSRWeaponInventoryComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
	// Replicate the per-slot UFPSRWeaponInstance subobjects via the registered list. The owning actor
	// (AFPSRCharacter) must also enable bReplicateUsingRegisteredSubObjectList (engine requirement).
	bReplicateUsingRegisteredSubObjectList = true;
	// Slots are sized from SlotDefinitions in InitializeComponent (a Blueprint override isn't applied yet here);
	// this is only a sane fallback for a component that never gets initialized.
	Slots.SetNum(MaxSlots);
	bWantsInitializeComponent = true;

	// Shipping layout: the last slot is melee-only, the rest are ranged. The melee slot's exclusive claim on Melee is
	// what makes the others refuse a knife, so they are left empty (= "everything unclaimed") and need no edit when a
	// new ranged archetype is added. The melee slot's DefaultWeapon (bare hands) is content and is assigned in the
	// character Blueprint — no asset paths in C++ (§6-2).
	SlotDefinitions.SetNum(MaxSlots);
	SlotDefinitions.Last().AcceptedArchetypes = { EFPSRWeaponArchetype::Melee };
}

void UFPSRWeaponInventoryComponent::InitializeComponent()
{
	Super::InitializeComponent();

	if (SlotDefinitions.Num() > MaxSlots)
	{
		UE_LOG(LogFPSR, Error,
			TEXT("[Weapon] SlotDefinitions has %d entries but only %d slots can be selected (there are %d equip inputs). The extra slots are ignored."),
			SlotDefinitions.Num(), MaxSlots, MaxSlots);
	}
	Slots.SetNum(FMath::Clamp(SlotDefinitions.Num(), 1, MaxSlots));
}

void UFPSRWeaponInventoryComponent::BeginPlay()
{
	Super::BeginPlay();

	// Server owns the reload timer; bind the run-freeze observer so an in-flight reload pauses during the
	// card-selection freeze (Game.MD §2-2). Clients never run reload, so they don't bind.
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		if (AFPSRGameState* RunState = GetWorld() ? GetWorld()->GetGameState<AFPSRGameState>() : nullptr)
		{
			RunState->OnRunStateChanged.AddDynamic(this, &UFPSRWeaponInventoryComponent::HandleRunStateChanged);
			BoundRunState = RunState;
		}
	}
}

void UFPSRWeaponInventoryComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (BoundRunState.IsValid())
	{
		BoundRunState->OnRunStateChanged.RemoveDynamic(this, &UFPSRWeaponInventoryComponent::HandleRunStateChanged);
	}
	BoundRunState.Reset();

	Super::EndPlay(EndPlayReason);
}

void UFPSRWeaponInventoryComponent::HandleRunStateChanged()
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !GetWorld())
	{
		return;
	}
	const AFPSRGameState* RunState = GetWorld()->GetGameState<AFPSRGameState>();
	if (!RunState)
	{
		return;
	}

	// PauseTimer/UnPauseTimer are no-ops on an inactive/invalid handle, so this is safe whether or not a reload
	// is actually in progress; the remaining time is preserved across the freeze.
	FTimerManager& TimerManager = GetWorld()->GetTimerManager();
	if (RunState->IsRunPaused())
	{
		TimerManager.PauseTimer(ReloadTimerHandle);
	}
	else
	{
		TimerManager.UnPauseTimer(ReloadTimerHandle);
	}
}

void UFPSRWeaponInventoryComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	FDoRepLifetimeParams Params;
	Params.bIsPushBased = true;
	DOREPLIFETIME_WITH_PARAMS_FAST(UFPSRWeaponInventoryComponent, Slots, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(UFPSRWeaponInventoryComponent, CurrentSlotIndex, Params);
}

UAbilitySystemComponent* UFPSRWeaponInventoryComponent::GetOwnerASC() const
{
	return UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(GetOwner());
}

bool UFPSRWeaponInventoryComponent::SlotAcceptsArchetype(int32 SlotIndex, EFPSRWeaponArchetype Archetype) const
{
	if (!SlotDefinitions.IsValidIndex(SlotIndex))
	{
		return false;
	}

	const FFPSRWeaponSlotDefinition& Definition = SlotDefinitions[SlotIndex];
	if (Definition.AcceptedArchetypes.Num() > 0)
	{
		return Definition.AcceptedArchetypes.Contains(Archetype);
	}

	// Empty list = "whatever no other slot has claimed". Asking the OTHER slots (rather than assuming the claims come
	// first in the array) is what makes the rule independent of slot order: a catch-all slot turns an archetype down
	// because some slot claimed it, not because that slot happened to be earlier.
	for (int32 Other = 0; Other < SlotDefinitions.Num(); ++Other)
	{
		if (Other != SlotIndex && SlotDefinitions[Other].AcceptedArchetypes.Contains(Archetype))
		{
			return false;
		}
	}
	return true;
}

bool UFPSRWeaponInventoryComponent::IsSlotHoldingOnlyDefault(int32 SlotIndex) const
{
	if (!Slots.IsValidIndex(SlotIndex) || !SlotDefinitions.IsValidIndex(SlotIndex))
	{
		return false;
	}
	const UFPSRWeaponInstance* Instance = Slots[SlotIndex].Get();
	const UFPSRWeaponDataAsset* Default = SlotDefinitions[SlotIndex].DefaultWeapon;
	return Instance != nullptr && Default != nullptr && Instance->GetSource() == Default;
}

int32 UFPSRWeaponInventoryComponent::FindFreeSlotFor(EFPSRWeaponArchetype Archetype) const
{
	for (int32 Index = 0; Index < Slots.Num(); ++Index)
	{
		if (!SlotAcceptsArchetype(Index, Archetype))
		{
			continue;
		}
		// A slot still holding nothing but its bare-hands default counts as free: a real pickup REPLACES the default
		// rather than needing a slot of its own (that is the one exception to "no weapon swapping", §2-4).
		if (Slots[Index] == nullptr || IsSlotHoldingOnlyDefault(Index))
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

void UFPSRWeaponInventoryComponent::SetSlotWeapon(int32 SlotIndex, UFPSRWeaponDataAsset* WeaponData, bool bAutoEquip)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !Slots.IsValidIndex(SlotIndex) || !WeaponData)
	{
		return;
	}

	// Unregister the outgoing instance BEFORE the pointer is lost. Skipping this leaves the replaced bare-hands
	// instance in the registered subobject list, where it keeps being considered for replication for the rest of the
	// run. Every create/replace goes through this one function so no path can forget it.
	if (UFPSRWeaponInstance* Outgoing = Slots[SlotIndex].Get())
	{
		RemoveReplicatedSubObject(Outgoing);
	}

	UFPSRWeaponInstance* Instance = NewObject<UFPSRWeaponInstance>(this);
	Instance->InitializeWithSource(WeaponData);
	// Start with a full magazine (resolved MagSize; no modifiers yet at pickup = base).
	Instance->SetCurrentAmmo(Instance->GetResolvedStats().MagSize);
	AddReplicatedSubObject(Instance);

	Slots[SlotIndex] = Instance;
	MARK_PROPERTY_DIRTY_FROM_NAME(UFPSRWeaponInventoryComponent, Slots, this);

	if (bAutoEquip && CurrentSlotIndex == INDEX_NONE)
	{
		EquipSlot(SlotIndex);
	}
	else if (SlotIndex == CurrentSlotIndex)
	{
		// The weapon in the CURRENTLY equipped slot was replaced — a knife unlock landing while the player is holding
		// bare hands. EquipSlot deliberately early-outs on an unchanged index, so everything it would normally set up
		// (fire ability, walk speed, recoil/heat profile, first-person visual, swap cooldown) would keep describing
		// the weapon that just went away. Do the same work here; the equipped SLOT hasn't changed, only its contents,
		// which is why this can't just call EquipSlot. Remote clients reach the same state through OnRep_Slots.
		RefreshEquippedAbility();
		PushEquippedWalkSpeed();

		if (UFPSRWeaponFireComponent* FireComp = GetOwner()->FindComponentByClass<UFPSRWeaponFireComponent>())
		{
			FireComp->OnWeaponEquipped(EquipFireCooldown);
		}
		if (AFPSRCharacter* Char = Cast<AFPSRCharacter>(GetOwner()))
		{
			Char->RefreshEquippedWeaponVisual();
		}
		if (const UWorld* World = GetWorld())
		{
			ServerNextAllowedFireTime = World->GetTimeSeconds() + EquipFireCooldown;
		}
	}
}

void UFPSRWeaponInventoryComponent::ServerSeedDefaultSlots()
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	for (int32 Index = 0; Index < Slots.Num(); ++Index)
	{
		// Writing ONLY to a null slot is the whole idempotency guarantee. PossessedBy can run more than once for the
		// same pawn (re-possession, a seamless travel that reuses it); finding the slot already populated means a
		// knife the player picked up is never reverted to bare hands by a second seed.
		if (Slots[Index] == nullptr && SlotDefinitions.IsValidIndex(Index) && SlotDefinitions[Index].DefaultWeapon)
		{
			// Never auto-equips: seeding runs BEFORE the starting weapons are granted, and equipping here would make
			// bare hands the equipped weapon and leave the starting rifle unequipped.
			SetSlotWeapon(Index, SlotDefinitions[Index].DefaultWeapon, /*bAutoEquip=*/false);
		}
	}
}

int32 UFPSRWeaponInventoryComponent::AddWeapon(UFPSRWeaponDataAsset* WeaponData)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !WeaponData)
	{
		return INDEX_NONE;
	}

	const int32 TargetSlot = FindFreeSlotFor(WeaponData->GetArchetype());
	if (TargetSlot == INDEX_NONE)
	{
		return INDEX_NONE;
	}

	SetSlotWeapon(TargetSlot, WeaponData, /*bAutoEquip=*/true);
	return TargetSlot;
}

bool UFPSRWeaponInventoryComponent::HasFreeSlotFor(EFPSRWeaponArchetype Archetype) const
{
	return FindFreeSlotFor(Archetype) != INDEX_NONE;
}

bool UFPSRWeaponInventoryComponent::HasAnyFreeSlot() const
{
	for (int32 Index = 0; Index < Slots.Num(); ++Index)
	{
		if (Slots[Index] == nullptr || IsSlotHoldingOnlyDefault(Index))
		{
			return true;
		}
	}
	return false;
}

void UFPSRWeaponInventoryComponent::EquipSlot(int32 SlotIndex)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	if (!Slots.IsValidIndex(SlotIndex) || Slots[SlotIndex] == nullptr)
	{
		return;
	}
	// Re-pressing the slot key of the ALREADY equipped weapon is not a swap — bail before the swap side effects.
	// Falling through would (a) cancel an in-progress reload and then decline to resume it (the resume below only
	// fires on an empty magazine, so a partial-mag reload is silently lost) and (b) impose EquipFireCooldown for a
	// swap that never happened. It also desyncs remote clients: CurrentSlotIndex is written with its own value, so
	// the changelist sees no change and OnRep_CurrentSlotIndex never runs — the owning client never applies the
	// cooldown the server just imposed and predicts shots (recoil/bloom) the server then rejects. The listen-server
	// host is unaffected (it applies the cooldown locally), so this would be a host/remote asymmetry too.
	if (SlotIndex == CurrentSlotIndex)
	{
		return;
	}

	// Switching weapons cancels any in-progress reload, remembering the slot so it resumes on re-equip.
	if (UFPSRWeaponInstance* Current = GetCurrentInstance())
	{
		if (Current->IsReloading())
		{
			GetWorld()->GetTimerManager().ClearTimer(ReloadTimerHandle);
			Current->SetReloading(false);
			PendingReloadSlot = CurrentSlotIndex;
		}
	}

	CurrentSlotIndex = SlotIndex;
	MARK_PROPERTY_DIRTY_FROM_NAME(UFPSRWeaponInventoryComponent, CurrentSlotIndex, this);
	RefreshEquippedAbility();
	PushEquippedWalkSpeed();

	// Switching weapons cancels any in-progress ChargeLaser charge (server side): a banked charge must not survive
	// an equip and fire later as a free full-charge beam on re-equip. OnWeaponEquipped also imposes the post-swap
	// fire cooldown on the owning client's recoil prediction (mirrors the server gate below).
	if (AActor* Owner = GetOwner())
	{
		if (UFPSRWeaponFireComponent* FireComp = Owner->FindComponentByClass<UFPSRWeaponFireComponent>())
		{
			FireComp->OnWeaponEquipped(EquipFireCooldown);
		}
	}

	if (AFPSRCharacter* Char = Cast<AFPSRCharacter>(GetOwner()))
	{
		Char->RefreshEquippedWeaponVisual();
	}

	// Impose a minimum post-swap cooldown before the next shot. A reset to 0 would let a rapid A->B->A swap re-fire
	// A instantly, bypassing its fire cadence. The new weapon still isn't blocked by the PREVIOUS weapon's (possibly
	// long) interval — this is a fixed swap time, not the old cadence.
	ServerNextAllowedFireTime = GetWorld()->GetTimeSeconds() + EquipFireCooldown;

	// Re-equipping a weapon whose reload was cancelled mid-switch resumes the reload ONLY if its
	// magazine is empty. If ammo remains, the reload stays cancelled (player keeps the partial mag).
	if (PendingReloadSlot == CurrentSlotIndex)
	{
		PendingReloadSlot = INDEX_NONE;
		if (UFPSRWeaponInstance* Current = GetCurrentInstance())
		{
			if (Current->GetCurrentAmmo() <= 0)
			{
				StartReload();
			}
		}
	}

	UE_LOG(LogFPSR, Verbose, TEXT("[Weapon] Equipped slot %d"), SlotIndex);
}

void UFPSRWeaponInventoryComponent::RefreshEquippedAbility()
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	UAbilitySystemComponent* ASC = GetOwnerASC();
	if (!ASC)
	{
		return;
	}

	if (GrantedFireAbilityHandle.IsValid())
	{
		ASC->ClearAbility(GrantedFireAbilityHandle);
		GrantedFireAbilityHandle = FGameplayAbilitySpecHandle();
	}

	UFPSRWeaponDataAsset* Weapon = GetCurrentWeapon();
	if (Weapon && Weapon->FireAbility)
	{
		GrantedFireAbilityHandle = ASC->GiveAbility(FGameplayAbilitySpec(Weapon->FireAbility, 1, INDEX_NONE, this));
	}
}

UFPSRWeaponInstance* UFPSRWeaponInventoryComponent::GetCurrentInstance() const
{
	return Slots.IsValidIndex(CurrentSlotIndex) ? Slots[CurrentSlotIndex].Get() : nullptr;
}

UFPSRWeaponDataAsset* UFPSRWeaponInventoryComponent::GetCurrentWeapon() const
{
	UFPSRWeaponInstance* Instance = GetCurrentInstance();
	return Instance ? Instance->GetSource() : nullptr;
}

void UFPSRWeaponInventoryComponent::PushEquippedWalkSpeed()
{
	AFPSRCharacter* Char = Cast<AFPSRCharacter>(GetOwner());
	UFPSRCharacterMovementComponent* Movement = Char ? Char->GetFPSRMovement() : nullptr;
	if (!Movement)
	{
		return;
	}

	// One-way push: the inventory hands the movement component a number, and the movement component composes it with
	// the other speed layers. It never asks the movement component anything back, so a new locomotion state can't
	// ripple into the weapon code (ADR 0001 module boundary). 0 = this weapon has no opinion, use the character's.
	const UFPSRWeaponDataAsset* Weapon = GetCurrentWeapon();
	Movement->SetLoadoutWalkSpeed(Weapon ? Weapon->WalkSpeed : 0.0f);
}

void UFPSRWeaponInventoryComponent::OnRep_CurrentSlotIndex()
{
	// Cosmetic hook for clients (weapon visual swap added later). Also cancel any local ChargeLaser charge and apply
	// the post-swap fire cooldown to recoil prediction so a charge started before this swap can't be released into
	// the newly-equipped weapon, and a rapid swap can't fire early on the client (mirrors the server gate).
	if (AActor* Owner = GetOwner())
	{
		if (UFPSRWeaponFireComponent* FireComp = Owner->FindComponentByClass<UFPSRWeaponFireComponent>())
		{
			FireComp->OnWeaponEquipped(EquipFireCooldown);
		}
	}

	if (AFPSRCharacter* Char = Cast<AFPSRCharacter>(GetOwner()))
	{
		Char->RefreshEquippedWeaponVisual();
	}

	// Apply the equipped weapon's walk speed on this machine too. This is the ONLY point a client changes it — the
	// equip input deliberately doesn't predict it, because the server can turn an equip down (run freeze / downed)
	// without replicating anything back, and a client that had already applied the faster speed would keep it.
	PushEquippedWalkSpeed();
}

void UFPSRWeaponInventoryComponent::OnRep_Slots()
{
	// On a remote owning client the instance subobjects can arrive AFTER CurrentSlotIndex — whose OnRep then ran with a
	// null instance and (a) cleared the meshes AND (b) ran OnWeaponEquipped, which set NO recoil pattern / heat profile
	// (Weapon was null). Re-run BOTH now that the slot instances + their Source are present: refresh the visual AND
	// re-apply the equip setup so the recoil pattern + heat-spread profile the early CurrentSlotIndex OnRep missed land
	// (idempotent — a redundant call on the normal ordering just re-sets the same values).
	if (AActor* Owner = GetOwner())
	{
		if (UFPSRWeaponFireComponent* FireComp = Owner->FindComponentByClass<UFPSRWeaponFireComponent>())
		{
			FireComp->OnWeaponEquipped(EquipFireCooldown);
		}
	}
	if (AFPSRCharacter* Char = Cast<AFPSRCharacter>(GetOwner()))
	{
		Char->RefreshEquippedWeaponVisual();
	}
	// Same late-arrival reason as the visual above: CurrentSlotIndex's OnRep may have run while the instance (and so
	// its Source, which carries WalkSpeed) was still null, leaving the client on the character's default speed.
	PushEquippedWalkSpeed();
}

int32 UFPSRWeaponInventoryComponent::GetCurrentAmmo() const
{
	UFPSRWeaponInstance* Instance = GetCurrentInstance();
	return Instance ? Instance->GetCurrentAmmo() : 0;
}

int32 UFPSRWeaponInventoryComponent::GetCurrentMagSize() const
{
	UFPSRWeaponInstance* Instance = GetCurrentInstance();
	return Instance ? Instance->GetResolvedStats().MagSize : 0;
}

bool UFPSRWeaponInventoryComponent::IsReloading() const
{
	UFPSRWeaponInstance* Instance = GetCurrentInstance();
	return Instance ? Instance->IsReloading() : false;
}

TArray<UFPSRWeaponDataAsset*> UFPSRWeaponInventoryComponent::GetOwnedWeapons() const
{
	TArray<UFPSRWeaponDataAsset*> Result;
	Result.Reserve(Slots.Num());
	for (const TObjectPtr<UFPSRWeaponInstance>& Instance : Slots)
	{
		UFPSRWeaponDataAsset* Source = Instance ? Instance->GetSource() : nullptr;
		// A slot's default weapon (bare hands) occupies a slot but was never GRANTED, so it isn't owned. Letting it
		// through would offer weapon cards targeting bare hands, make "the player has a weapon" true before the first
		// pickup, and put it in the lobby's starting-weapon list.
		if (Source && !Source->bExcludeFromProgression)
		{
			Result.Add(Source);
		}
	}
	return Result;
}

UFPSRWeaponInstance* UFPSRWeaponInventoryComponent::GetInstanceForWeapon(const UFPSRWeaponDataAsset* Weapon) const
{
	if (!Weapon)
	{
		return nullptr;
	}
	for (const TObjectPtr<UFPSRWeaponInstance>& Instance : Slots)
	{
		if (Instance && Instance->GetSource() == Weapon)
		{
			return Instance.Get();
		}
	}
	return nullptr;
}

void UFPSRWeaponInventoryComponent::MarkAllInstancesResolvedDirty()
{
	for (const TObjectPtr<UFPSRWeaponInstance>& Instance : Slots)
	{
		if (Instance)
		{
			Instance->MarkResolvedDirty();
		}
	}
}

bool UFPSRWeaponInventoryComponent::ConsumeAmmo(int32 Amount)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return false;
	}
	UFPSRWeaponInstance* Instance = GetCurrentInstance();
	if (!Instance || Instance->GetCurrentAmmo() < Amount)
	{
		return false;
	}
	Instance->SetCurrentAmmo(Instance->GetCurrentAmmo() - Amount);
	return true;
}

void UFPSRWeaponInventoryComponent::StartReload()
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	// No reload start during the card-selection freeze (Game.MD §2-2). Single chokepoint for every entry point
	// (manual ServerReload, auto-reload, equip-resume). An already-running reload is paused by HandleRunStateChanged.
	if (const AFPSRGameState* RunState = GetWorld() ? GetWorld()->GetGameState<AFPSRGameState>() : nullptr)
	{
		if (RunState->IsRunPaused())
		{
			return;
		}
	}
	UFPSRWeaponInstance* Instance = GetCurrentInstance();
	if (!Instance || Instance->IsReloading())
	{
		return;
	}
	const FFPSRWeaponStatBlock& Stats = Instance->GetResolvedStats();
	if (Instance->GetCurrentAmmo() >= Stats.MagSize)
	{
		return; // already full
	}

	Instance->SetReloading(true);
	GetWorld()->GetTimerManager().SetTimer(
		ReloadTimerHandle, this, &UFPSRWeaponInventoryComponent::FinishReload,
		FMath::Max(0.01f, Stats.ReloadTime), false);
}

void UFPSRWeaponInventoryComponent::CancelReload()
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	// Mirror the weapon-swap reload-cancel (HandleEquip), minus the PendingReloadSlot resume: the magazine is already
	// full, so there is nothing to resume — just stop the timer and clear the flag so the fire gate reopens.
	UFPSRWeaponInstance* Instance = GetCurrentInstance();
	if (Instance && Instance->IsReloading())
	{
		GetWorld()->GetTimerManager().ClearTimer(ReloadTimerHandle);
		Instance->SetReloading(false);
	}
}

void UFPSRWeaponInventoryComponent::FinishReload()
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	if (UFPSRWeaponInstance* Instance = GetCurrentInstance())
	{
		Instance->SetCurrentAmmo(Instance->GetResolvedStats().MagSize); // infinite reserve: always full
		Instance->SetReloading(false);
	}
}

bool UFPSRWeaponInventoryComponent::ServerTryConsumeFireInterval(float MinInterval)
{
	// Client prediction path: never block locally; the server is authoritative.
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return true;
	}

	const UWorld* World = GetWorld();
	if (!World)
	{
		return true;
	}

	const float Now = World->GetTimeSeconds();
	// Jitter tolerance: allow a shot slightly early so network timing variance never blocks legitimate fire.
	// This gate rejects grossly-too-fast activation (anti-abuse), not exact cadence.
	const float Tolerance = MinInterval * 0.25f;
	if (Now + Tolerance < ServerNextAllowedFireTime)
	{
		return false;
	}

	// Advance from the SCHEDULE, not from the arrival time. Re-anchoring on Now would let the tolerance compound:
	// a client that always fires at the earliest accepted instant (Now = Next - Tolerance) would push the next slot
	// to Next + (MinInterval - Tolerance) every shot, converging on a sustained interval of 0.75 * MinInterval —
	// a permanent +33% rate of fire on every weapon family, not the one-off jitter absorption this gate intends.
	// Taking Max keeps the tolerance one-shot (an early round spends the credit instead of banking it) while an
	// idle gap still re-anchors to Now so the schedule can't accumulate a backlog of owed shots.
	ServerNextAllowedFireTime = FMath::Max(Now, ServerNextAllowedFireTime) + MinInterval;
	return true;
}

#if WITH_EDITOR
EDataValidationResult UFPSRWeaponInventoryComponent::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	if (SlotDefinitions.Num() == 0)
	{
		Context.AddError(LOCTEXT("NoSlots", "슬롯 정의가 비어 있습니다 — 무기를 넣을 칸이 하나도 없습니다."));
		Result = EDataValidationResult::Invalid;
	}
	if (SlotDefinitions.Num() > MaxSlots)
	{
		Context.AddError(FText::Format(
			LOCTEXT("TooManySlots", "슬롯 정의가 {0}개인데 선택 가능한 칸은 {1}개뿐입니다(장착 입력이 숫자키 1~{1}). 초과분은 영영 선택할 수 없습니다."),
			FText::AsNumber(SlotDefinitions.Num()), FText::AsNumber(MaxSlots)));
		Result = EDataValidationResult::Invalid;
	}

	// Gather the exclusive claims first, then judge each archetype. Doing it in this order is what lets the check
	// report "two slots claim Melee" and "nothing accepts Shotgun" independently of slot order.
	bool bHasCatchAllSlot = false;
	TMap<EFPSRWeaponArchetype, int32> ClaimCount;
	for (const FFPSRWeaponSlotDefinition& Definition : SlotDefinitions)
	{
		if (Definition.AcceptedArchetypes.Num() == 0)
		{
			bHasCatchAllSlot = true;
			continue;
		}
		for (EFPSRWeaponArchetype Archetype : Definition.AcceptedArchetypes)
		{
			++ClaimCount.FindOrAdd(Archetype);
		}
	}

	if (const UEnum* ArchetypeEnum = StaticEnum<EFPSRWeaponArchetype>())
	{
		// NumEnums() - 1 skips the generated _MAX entry.
		for (int32 EnumIndex = 0; EnumIndex < ArchetypeEnum->NumEnums() - 1; ++EnumIndex)
		{
			const EFPSRWeaponArchetype Archetype = static_cast<EFPSRWeaponArchetype>(ArchetypeEnum->GetValueByIndex(EnumIndex));
			const FText ArchetypeName = ArchetypeEnum->GetDisplayNameTextByIndex(EnumIndex);
			const int32 Claims = ClaimCount.FindRef(Archetype);

			if (Claims > 1)
			{
				Context.AddError(FText::Format(
					LOCTEXT("DoubleClaim", "아키타입 '{0}'을(를) {1}개 칸이 전용으로 선언했습니다. 전용 선언은 한 칸만 해야 합니다(아니면 무기가 어느 칸에 들어갈지가 배열 순서에 좌우됩니다)."),
					ArchetypeName, FText::AsNumber(Claims)));
				Result = EDataValidationResult::Invalid;
			}
			else if (Claims == 0 && !bHasCatchAllSlot)
			{
				Context.AddError(FText::Format(
					LOCTEXT("UnreachableArchetype", "아키타입 '{0}'을(를) 받는 칸이 없습니다 — 그 무기는 영영 획득할 수 없습니다. 어느 칸에 전용 선언하거나, 한 칸의 '받는 아키타입'을 비워 나머지 담당으로 두세요."),
					ArchetypeName));
				Result = EDataValidationResult::Invalid;
			}
		}
	}

	for (int32 Index = 0; Index < SlotDefinitions.Num(); ++Index)
	{
		const UFPSRWeaponDataAsset* Default = SlotDefinitions[Index].DefaultWeapon;
		if (!Default)
		{
			continue;
		}
		if (!SlotAcceptsArchetype(Index, Default->GetArchetype()))
		{
			Context.AddError(FText::Format(
				LOCTEXT("DefaultRejected", "{0}번 칸의 기본 무기 '{1}'은(는) 그 칸이 받지 않는 아키타입입니다 — 채워지기는 하는데 같은 종류의 무기를 주워도 대체되지 않습니다."),
				FText::AsNumber(Index), FText::FromString(Default->GetName())));
			Result = EDataValidationResult::Invalid;
		}
		if (!Default->bExcludeFromProgression)
		{
			Context.AddWarning(FText::Format(
				LOCTEXT("DefaultInProgression", "{0}번 칸의 기본 무기 '{1}'에 '진행에서 제외'가 꺼져 있습니다 — 맨손이 보유 무기로 세어져 카드 풀과 로비 시작무기 목록에 샙니다."),
				FText::AsNumber(Index), FText::FromString(Default->GetName())));
		}
	}

	return Result;
}
#endif

#undef LOCTEXT_NAMESPACE
