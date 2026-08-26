// Copyright Epic Games, Inc. All Rights Reserved.

#include "Enemy/FPSREnemyEliteBase.h"
#include "AbilitySystem/FPSRAbilitySystemComponent.h"
#include "AbilitySystemComponent.h"
#include "GameplayEffect.h"

AFPSREnemyEliteBase::AFPSREnemyEliteBase()
{
	AbilitySystem = CreateDefaultSubobject<UFPSRAbilitySystemComponent>(TEXT("AbilitySystem"));
	AbilitySystem->SetIsReplicated(true);

	// Minimal, NOT Mixed like the player (AFPSRPlayerState::AbilitySystemComponent, FPSRPlayerState.cpp) — see the
	// header's AbilitySystem field comment for the engine quote (AbilitySystemComponent.h:82) this contrast rests on.
	AbilitySystem->SetReplicationMode(EGameplayEffectReplicationMode::Minimal);
}

UAbilitySystemComponent* AFPSREnemyEliteBase::GetAbilitySystemComponent() const
{
	return AbilitySystem;
}

void AFPSREnemyEliteBase::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	// Owner == Avatar == self — see the header doc on this override for why (no PlayerState to split them across,
	// and why this can't move to the constructor). Runs once per actor real-lifetime; not re-run on pool reuse.
	if (AbilitySystem)
	{
		AbilitySystem->InitAbilityActorInfo(this, this);
	}
}

void AFPSREnemyEliteBase::EnterDyingState()
{
	// Cancel any in-flight ability activation BEFORE Super — see header doc. Mirrors Super::EnterDyingState's own
	// ReleaseRangedHold call inside its body: "gameplay ends now" covers the ranged hold AND the ASC alike, so both
	// close at the same point in the teardown, ahead of the collision-off Super performs.
	if (AbilitySystem)
	{
		AbilitySystem->CancelAbilities();
	}
	Super::EnterDyingState();
}

void AFPSREnemyEliteBase::Deactivate()
{
	// Strip active GEs BEFORE Super::Deactivate() flips this actor to DORM_DormantAll at the end of its body — see
	// header doc: the removal's replication must ride the awake->dormant flush, which only happens while still
	// DORM_Awake. (CancelAbilities already ran earlier, in EnterDyingState, for the death path; a non-death teardown
	// — e.g. an immediate ReleaseEnemy with no dying dwell — reaches Deactivate directly, so this doesn't assume
	// EnterDyingState always ran first.)
	if (AbilitySystem)
	{
		AbilitySystem->RemoveActiveEffects(FGameplayEffectQuery());
	}
	Super::Deactivate();
}

void AFPSREnemyEliteBase::Activate(const FVector& Location)
{
	Super::Activate(Location);

	// Defensive re-clear AFTER Super — same position as HealthComponent::ResetForReuse() inside Super's own body.
	// Deactivate/EnterDyingState already close this life's ASC state on every teardown path, but Activate is the
	// ONE pool-reuse entry point every life passes through, so belt-and-suspenders here means a reused elite can
	// never start a new life carrying a stale GE forward from a path that missed a teardown call.
	if (AbilitySystem)
	{
		AbilitySystem->RemoveActiveEffects(FGameplayEffectQuery());
	}
}

void AFPSREnemyEliteBase::ServerResetEliteForStageCarry()
{
	if (!HasAuthority())
	{
		return;
	}

	// See header doc: cancel in-progress abilities only — deliberately do NOT touch active GEs. Infinite GEs (the
	// accumulated pressure the carry-over is meant to preserve) ride along with the enemy untouched.
	if (AbilitySystem)
	{
		AbilitySystem->CancelAbilities();
	}
}
