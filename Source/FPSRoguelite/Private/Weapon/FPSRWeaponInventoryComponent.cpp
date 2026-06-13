// Copyright Epic Games, Inc. All Rights Reserved.

#include "Weapon/FPSRWeaponInventoryComponent.h"
#include "Weapon/FPSRWeaponDataAsset.h"
#include "Weapon/FPSRWeaponInstance.h"
#include "Weapon/FPSRWeaponFireComponent.h"
#include "Hero/FPSRCharacter.h"
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

UFPSRWeaponInventoryComponent::UFPSRWeaponInventoryComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
	// Replicate the per-slot UFPSRWeaponInstance subobjects via the registered list. The owning actor
	// (AFPSRCharacter) must also enable bReplicateUsingRegisteredSubObjectList (engine requirement).
	bReplicateUsingRegisteredSubObjectList = true;
	Slots.SetNum(MaxSlots);
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

int32 UFPSRWeaponInventoryComponent::AddWeapon(UFPSRWeaponDataAsset* WeaponData)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !WeaponData)
	{
		return INDEX_NONE;
	}

	const int32 FreeSlot = Slots.IndexOfByPredicate(
		[](const TObjectPtr<UFPSRWeaponInstance>& W) { return W == nullptr; });
	if (FreeSlot == INDEX_NONE)
	{
		return INDEX_NONE;
	}

	UFPSRWeaponInstance* Instance = NewObject<UFPSRWeaponInstance>(this);
	Instance->InitializeWithSource(WeaponData);
	// Start with a full magazine (resolved MagSize; no modifiers yet at pickup = base).
	Instance->SetCurrentAmmo(Instance->GetResolvedStats().MagSize);
	AddReplicatedSubObject(Instance);

	Slots[FreeSlot] = Instance;
	MARK_PROPERTY_DIRTY_FROM_NAME(UFPSRWeaponInventoryComponent, Slots, this);

	if (CurrentSlotIndex == INDEX_NONE)
	{
		EquipSlot(FreeSlot);
	}
	return FreeSlot;
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
		Char->RefreshFirstPersonWeaponVisual();
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
		Char->RefreshFirstPersonWeaponVisual();
	}
}

void UFPSRWeaponInventoryComponent::OnRep_Slots()
{
	// On a remote owning client the instance subobjects can arrive after CurrentSlotIndex (whose OnRep then saw no
	// weapon and cleared the meshes). Refresh again now that the slot instances + their Source are present.
	if (AFPSRCharacter* Char = Cast<AFPSRCharacter>(GetOwner()))
	{
		Char->RefreshFirstPersonWeaponVisual();
	}
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
	for (const TObjectPtr<UFPSRWeaponInstance>& Instance : Slots)
	{
		if (Instance && Instance->GetSource())
		{
			Result.Add(Instance->GetSource());
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

	ServerNextAllowedFireTime = Now + MinInterval;
	return true;
}
