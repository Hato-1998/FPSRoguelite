// Copyright Epic Games, Inc. All Rights Reserved.

#include "Core/FPSRPlayerState.h"
#include "AbilitySystem/FPSRAbilitySystemComponent.h"
#include "AbilitySystem/Attributes/FPSRHealthSet.h"
#include "AbilitySystem/Attributes/FPSRCombatSet.h"
#include "GameplayEffect.h"
#include "Weapon/FPSRWeaponInventoryComponent.h"
#include "Weapon/FPSRWeaponFireComponent.h"
#include "Weapon/FPSRWeaponDataAsset.h"
#include "Hero/FPSRCharacter.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h" // U P-F: GetOwningController()->IsLocalController()/HasAuthority() for the ack gate
#include "Core/FPSRLogChannels.h"      // U P-F: LogFPSR (topology ack fail-open diagnostic)
#include "Net/UnrealNetwork.h"
#include "Net/Core/PushModel/PushModel.h"

AFPSRPlayerState::AFPSRPlayerState()
{
	AbilitySystemComponent = CreateDefaultSubobject<UFPSRAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	AbilitySystemComponent->SetIsReplicated(true);
	AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);

	HealthSet = CreateDefaultSubobject<UFPSRHealthSet>(TEXT("HealthSet"));
	CombatSet = CreateDefaultSubobject<UFPSRCombatSet>(TEXT("CombatSet"));

	// PlayerState updates frequently so GAS state stays responsive for clients.
	SetNetUpdateFrequency(100.0f);
}

UAbilitySystemComponent* AFPSRPlayerState::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

void AFPSRPlayerState::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
	{
		ResetRerollCharges();
	}
}

void AFPSRPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	FDoRepLifetimeParams Params;
	Params.bIsPushBased = true;
	DOREPLIFETIME_WITH_PARAMS_FAST(AFPSRPlayerState, LifeState, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(AFPSRPlayerState, RunRerollCharges, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(AFPSRPlayerState, CardPicksPending, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(AFPSRPlayerState, WeaponUnlockPicksPending, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(AFPSRPlayerState, AllWeaponsMods, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(AFPSRPlayerState, SelectedWeapon, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(AFPSRPlayerState, bReady, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(AFPSRPlayerState, LobbySeatIndex, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(AFPSRPlayerState, CurrentMapId, Params);
}

void AFPSRPlayerState::SetLobbySeatIndex(int32 NewSeat)
{
	if (!HasAuthority() || LobbySeatIndex == NewSeat)
	{
		return;
	}
	LobbySeatIndex = NewSeat;
	MARK_PROPERTY_DIRTY_FROM_NAME(AFPSRPlayerState, LobbySeatIndex, this);
}

void AFPSRPlayerState::SetCurrentMapId(const FGameplayTag& NewMapId)
{
	// Committed occupancy (multimap Tier 0). Idempotent + low-churn: the stream/allocator subsystem calls this only when
	// a player has settled in a new map, NOT every boundary AABB frame, so this Push-Model property rarely dirties even
	// though the PlayerState replicates at 100Hz for GAS.
	if (!HasAuthority() || CurrentMapId == NewMapId)
	{
		return;
	}
	CurrentMapId = NewMapId;
	MARK_PROPERTY_DIRTY_FROM_NAME(AFPSRPlayerState, CurrentMapId, this);
}

void AFPSRPlayerState::MarkTopologyJoin(int32 Gen, float Now)
{
	if (!HasAuthority() || JoinTopologyGeneration >= 0)
	{
		return; // idempotent: only the first sighting stamps the join generation + fail-open clock
	}
	JoinTopologyGeneration = Gen;
	TopologyJoinTime = Now;
}

void AFPSRPlayerState::SetAckedTopologyGeneration(int32 Gen)
{
	if (!HasAuthority())
	{
		return;
	}
	AckedTopologyGeneration = FMath::Max(AckedTopologyGeneration, Gen); // monotone — an ack never regresses
}

bool AFPSRPlayerState::IsTopologyAckSatisfied(bool bLocalAuthority, int32 AckedGen, int32 JoinGen, float JoinTime, float Now, float Timeout)
{
	if (bLocalAuthority)
	{
		return true; // host/standalone owning controller IS the server — it authored the topology, no OnRep to ack against
	}
	if (JoinGen < 0)
	{
		return false; // not yet marked -> fail-closed (leading-edge seal: a just-arrived joiner doesn't participate yet)
	}
	return AckedGen >= JoinGen || (Now - JoinTime > Timeout); // acked, or fail-open past the timeout (RPC loss guard)
}

bool AFPSRPlayerState::HasAckedJoinTopology(float Now) const
{
	const AController* C = GetOwningController();
	const bool bLocalAuthority = C && C->IsLocalController() && C->HasAuthority();
	const bool bSatisfied = IsTopologyAckSatisfied(
		bLocalAuthority, AckedTopologyGeneration, JoinTopologyGeneration, TopologyJoinTime, Now, TopologyAckTimeoutSeconds);

	// Diagnose a fail-open (the timeout let the player through without the client's ack) once — a lost ack RPC / softlock
	// is otherwise invisible. Only meaningful for a remote client that was marked but never acked its join generation.
	if (bSatisfied && !bLocalAuthority && JoinTopologyGeneration >= 0
		&& AckedTopologyGeneration < JoinTopologyGeneration && !bLoggedTopologyFailOpen)
	{
		bLoggedTopologyFailOpen = true;
		UE_LOG(LogFPSR, Warning,
			TEXT("[Topology] Ack fail-open for '%s': joined generation %d never acked within %.0fs (RPC loss?) — participating anyway."),
			*GetPlayerName(), JoinTopologyGeneration, TopologyAckTimeoutSeconds);
	}
	return bSatisfied;
}

void AFPSRPlayerState::ResetTopologyAck()
{
	JoinTopologyGeneration = -1;
	AckedTopologyGeneration = -1;
	TopologyJoinTime = 0.0f;
	bLoggedTopologyFailOpen = false;
}

bool AFPSRPlayerState::ConsumeRerollCharge()
{
	if (!HasAuthority())
	{
		return false;
	}

	if (RunRerollCharges > 0)
	{
		--RunRerollCharges;
		MARK_PROPERTY_DIRTY_FROM_NAME(AFPSRPlayerState, RunRerollCharges, this);
		OnRerollChargesChanged.Broadcast();
		return true;
	}

	return false;
}

void AFPSRPlayerState::ResetRerollCharges()
{
	if (!HasAuthority())
	{
		return;
	}

	RunRerollCharges = DefaultRerollCharges;
	MARK_PROPERTY_DIRTY_FROM_NAME(AFPSRPlayerState, RunRerollCharges, this);
	OnRerollChargesChanged.Broadcast();
}

void AFPSRPlayerState::SetRerollCharges(int32 NewCharges)
{
	if (!HasAuthority())
	{
		return;
	}

	RunRerollCharges = FMath::Max(NewCharges, 0);
	MARK_PROPERTY_DIRTY_FROM_NAME(AFPSRPlayerState, RunRerollCharges, this);
	OnRerollChargesChanged.Broadcast();
}

void AFPSRPlayerState::OnRep_RunRerollCharges()
{
	OnRerollChargesChanged.Broadcast();
}

void AFPSRPlayerState::AddCardPick()
{
	if (!HasAuthority())
	{
		return;
	}

	++CardPicksPending;
	MARK_PROPERTY_DIRTY_FROM_NAME(AFPSRPlayerState, CardPicksPending, this);
	OnCardPicksChanged.Broadcast();
}

bool AFPSRPlayerState::ConsumeCardPick()
{
	if (!HasAuthority() || CardPicksPending <= 0)
	{
		return false;
	}

	--CardPicksPending;
	MARK_PROPERTY_DIRTY_FROM_NAME(AFPSRPlayerState, CardPicksPending, this);
	OnCardPicksChanged.Broadcast();
	return true;
}

void AFPSRPlayerState::AddWeaponUnlockPick()
{
	if (!HasAuthority())
	{
		return;
	}

	++WeaponUnlockPicksPending;
	MARK_PROPERTY_DIRTY_FROM_NAME(AFPSRPlayerState, WeaponUnlockPicksPending, this);
	OnCardPicksChanged.Broadcast();
}

bool AFPSRPlayerState::ConsumeWeaponUnlockPick()
{
	if (!HasAuthority() || WeaponUnlockPicksPending <= 0)
	{
		return false;
	}

	--WeaponUnlockPicksPending;
	MARK_PROPERTY_DIRTY_FROM_NAME(AFPSRPlayerState, WeaponUnlockPicksPending, this);
	OnCardPicksChanged.Broadcast();
	return true;
}

void AFPSRPlayerState::OnRep_CardPicksPending()
{
	OnCardPicksChanged.Broadcast();
}

void AFPSRPlayerState::AddAllWeaponsModifier(const FFPSRWeaponStatMod& Mod)
{
	if (!HasAuthority())
	{
		return;
	}

	AllWeaponsMods.Mods.Add(Mod);
	MARK_PROPERTY_DIRTY_FROM_NAME(AFPSRPlayerState, AllWeaponsMods, this);

	// AllWeapons affects every owned weapon: invalidate all instance caches on the owning pawn (server).
	if (APawn* Pawn = GetPawn())
	{
		if (UFPSRWeaponInventoryComponent* Inv = Pawn->FindComponentByClass<UFPSRWeaponInventoryComponent>())
		{
			Inv->MarkAllInstancesResolvedDirty();
			if (AFPSRCharacter* Char = Cast<AFPSRCharacter>(Pawn))
			{
				Char->NotifyEquippedWeaponModifiersChanged(Inv->GetCurrentInstance());
			}
		}
	}
}

void AFPSRPlayerState::SetLifeState(EFPSRLifeState NewState)
{
	if (!HasAuthority() || LifeState == NewState)
	{
		return;
	}
	LifeState = NewState;
	MARK_PROPERTY_DIRTY_FROM_NAME(AFPSRPlayerState, LifeState, this);
}

void AFPSRPlayerState::OnRep_LifeState()
{
	// Owning client: stop the local fire loop immediately on death so a held trigger doesn't keep
	// auto-firing until the input gate catches up next frame, and clear the local (non-replicated) ADS
	// state so the camera can't stay zoom-latched if the result UI swallows the ADS-release input.
	// Server-side cancellation/aim-clear is handled in AFPSRCharacter::HandleOutOfHealth (CancelAllAbilities).
	// Fires for DBNO and Dead alike (both are not-Alive) so a downed player also drops the local fire/ADS latch.
	if (LifeState != EFPSRLifeState::Alive)
	{
		if (APawn* OwnerPawn = GetPawn())
		{
			if (UFPSRWeaponFireComponent* Fire = OwnerPawn->FindComponentByClass<UFPSRWeaponFireComponent>())
			{
				Fire->StopFiring();
				Fire->SetAiming(false);
			}
		}
	}

	// Mirror the server's downed locomotion on clients so movement prediction matches: locked while not Alive,
	// normal (combat-mult) speed once revived back to Alive. (Mirrors the move-speed-multiplier client sync path.)
	// Test !Alive (not ==DBNO): the DBNO->Dead transition on a team wipe (GameMode::NotifyPlayerDefeated) goes
	// through SetLifeState alone with no direct ApplyDownedLocomotion call, so ==DBNO would restore walk speed on
	// remote clients while the listen-server host — which gets no OnRep — keeps it locked. HandleOutOfHealth passes
	// true and PerformRevive passes false, so !Alive is the consistent predicate across all three paths.
	if (AFPSRCharacter* OwnerChar = Cast<AFPSRCharacter>(GetPawn()))
	{
		OwnerChar->ApplyDownedLocomotion(LifeState != EFPSRLifeState::Alive);
	}
}

void AFPSRPlayerState::OnRep_AllWeaponsMods()
{
	// Client: a new AllWeapons modifier replicated — invalidate every instance's resolved-stat cache so the
	// next read recomputes with the updated stack.
	if (APawn* Pawn = GetPawn())
	{
		if (UFPSRWeaponInventoryComponent* Inv = Pawn->FindComponentByClass<UFPSRWeaponInventoryComponent>())
		{
			Inv->MarkAllInstancesResolvedDirty();
			if (AFPSRCharacter* Char = Cast<AFPSRCharacter>(Pawn))
			{
				Char->NotifyEquippedWeaponModifiersChanged(Inv->GetCurrentInstance());
			}
		}
	}
}

void AFPSRPlayerState::SetSelectedWeapon(UFPSRWeaponDataAsset* Weapon)
{
	if (!HasAuthority() || SelectedWeapon == Weapon)
	{
		return;
	}
	SelectedWeapon = Weapon;
	MARK_PROPERTY_DIRTY_FROM_NAME(AFPSRPlayerState, SelectedWeapon, this);

	// Can't stay ready with no weapon — clearing the pick un-readies (keeps the ready guard consistent). Swapping to
	// another valid weapon leaves ready intact.
	if (!Weapon)
	{
		SetReady(false);
	}

	// Listen-server host's own UI doesn't get OnRep — broadcast directly so the host's lobby highlight updates too.
	OnLoadoutChanged.Broadcast();
}

void AFPSRPlayerState::OnRep_SelectedWeapon()
{
	OnLoadoutChanged.Broadcast();
}

void AFPSRPlayerState::SetReady(bool bNewReady)
{
	if (!HasAuthority() || bReady == bNewReady)
	{
		return;
	}

	// Guard: readying requires a chosen loadout weapon (no ready-with-empty-hands). Un-readying is always allowed.
	if (bNewReady && !SelectedWeapon)
	{
		return;
	}

	bReady = bNewReady;
	MARK_PROPERTY_DIRTY_FROM_NAME(AFPSRPlayerState, bReady, this);
	// Listen-server host gets no OnRep — broadcast directly so the host's ready button updates too.
	OnReadyChanged.Broadcast();
}

void AFPSRPlayerState::OnRep_Ready()
{
	OnReadyChanged.Broadcast();
}

void AFPSRPlayerState::ResetRunState()
{
	if (!HasAuthority())
	{
		return;
	}

	// Life state back to alive.
	SetLifeState(EFPSRLifeState::Alive);

	// Lobby ready resets on every (re)entry — a returning party must re-ready (U11a).
	SetReady(false);

	// Pending card / weapon-unlock picks.
	CardPicksPending = 0;
	WeaponUnlockPicksPending = 0;
	MARK_PROPERTY_DIRTY_FROM_NAME(AFPSRPlayerState, CardPicksPending, this);
	MARK_PROPERTY_DIRTY_FROM_NAME(AFPSRPlayerState, WeaponUnlockPicksPending, this);
	OnCardPicksChanged.Broadcast();

	// Reroll charges to default.
	ResetRerollCharges();

	// AllWeapons-scope modifiers (these survive pawn respawn, so they must be cleared explicitly).
	AllWeaponsMods.Mods.Empty();
	MARK_PROPERTY_DIRTY_FROM_NAME(AFPSRPlayerState, AllWeaponsMods, this);
	if (APawn* Pawn = GetPawn())
	{
		if (UFPSRWeaponInventoryComponent* Inv = Pawn->FindComponentByClass<UFPSRWeaponInventoryComponent>())
		{
			Inv->MarkAllInstancesResolvedDirty();
			if (AFPSRCharacter* Char = Cast<AFPSRCharacter>(Pawn))
			{
				Char->NotifyEquippedWeaponModifiersChanged(Inv->GetCurrentInstance());
			}
		}
	}

	// Loadout pick is re-chosen each lobby visit.
	SetSelectedWeapon(nullptr);

	// Multimap occupancy back to the Default (unset) map for a fresh run.
	SetCurrentMapId(FGameplayTag());

	// U (P-F): clear the topology late-join ack so a returning player re-marks + re-acks against the new run's topology.
	ResetTopologyAck();

	// Fresh-run ASC baseline (merge-gate P1): the ASC + attribute sets live on the PlayerState and survive the
	// lobby<->run seamless travel, so a wiped/buffed player would otherwise start the next run at 0 HP (death
	// state) or with stale run effects. Clear run-applied gameplay effects, then restore full health. (Weapon
	// fire abilities are re-granted per equip by the inventory, so ability specs are intentionally left alone.)
	if (AbilitySystemComponent)
	{
		// Clear card-granted passive abilities first (U18c) — they live on the persistent ASC and would otherwise
		// carry into the next run. ClearAbility ends any active instance (the passive's OnRemoveAbility cleanup runs).
		for (const FGameplayAbilitySpecHandle& Handle : CardGrantedAbilityHandles)
		{
			AbilitySystemComponent->ClearAbility(Handle);
		}
		CardGrantedAbilityHandles.Empty();
		DamageEventListenerCount = 0;

		AbilitySystemComponent->RemoveActiveEffects(FGameplayEffectQuery());
		AbilitySystemComponent->SetNumericAttributeBase(
			UFPSRHealthSet::GetHealthAttribute(),
			AbilitySystemComponent->GetNumericAttribute(UFPSRHealthSet::GetMaxHealthAttribute()));
	}
}

void AFPSRPlayerState::AddCardGrantedAbility(FGameplayAbilitySpecHandle Handle, bool bIsDamageEventListener)
{
	if (!HasAuthority() || !Handle.IsValid())
	{
		return;
	}
	CardGrantedAbilityHandles.Add(Handle);
	if (bIsDamageEventListener)
	{
		++DamageEventListenerCount;
	}
}

void AFPSRPlayerState::CopyProperties(APlayerState* PlayerState)
{
	Super::CopyProperties(PlayerState);

	// Carry the lobby loadout pick across the lobby->gameplay seamless travel so the gameplay pawn can grant it.
	// Run-progression fields are deliberately NOT copied — they are reset on lobby entry regardless.
	if (AFPSRPlayerState* PS = Cast<AFPSRPlayerState>(PlayerState))
	{
		// Raw copy (not SetSelectedWeapon — travel state hand-off must not run the setter's SetReady/broadcast side
		// effects), but still mark the push-model property dirty so the new PlayerState replicates the loadout, matching
		// every other mutation of this field.
		PS->SelectedWeapon = SelectedWeapon;
		MARK_PROPERTY_DIRTY_FROM_NAME(AFPSRPlayerState, SelectedWeapon, PS);
	}
}
