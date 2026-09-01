// Copyright Epic Games, Inc. All Rights Reserved.

#include "Enemy/FPSREnemyHealthComponent.h"
#include "Core/FPSRGameState.h"
#include "Core/FPSRLogChannels.h"
#include "Net/UnrealNetwork.h"
#include "Net/Core/PushModel/PushModel.h"

namespace
{
	/** VIT1: the freeze-paused combat clock (§5-6) — the single time axis both storages (this component and
	 *  AFPSRCharacter) regen against. 0 if the world/GameState isn't reachable yet (very early BeginPlay); that
	 *  just means "no elapsed time" until the next real read settles it. */
	float GetCombatClockNow(const UActorComponent* Component)
	{
		const AActor* Owner = Component ? Component->GetOwner() : nullptr;
		const UWorld* World = Owner ? Owner->GetWorld() : nullptr;
		const AFPSRGameState* GameState = World ? World->GetGameState<AFPSRGameState>() : nullptr;
		return GameState ? GameState->GetCombatClockSeconds() : 0.0f;
	}
}

UFPSREnemyHealthComponent::UFPSREnemyHealthComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UFPSREnemyHealthComponent::BeginPlay()
{
	Super::BeginPlay();

	if (GetOwner() && GetOwner()->HasAuthority())
	{
		Health = MaxHealth;
		MARK_PROPERTY_DIRTY_FROM_NAME(UFPSREnemyHealthComponent, Health, this);
		MARK_PROPERTY_DIRTY_FROM_NAME(UFPSREnemyHealthComponent, MaxHealth, this);

		// VIT1: symmetric initial state for the shield pool. MaxShield is still 0 here for a freshly-constructed
		// actor (InitializeVitals hasn't run yet), so this is a no-op in practice for the swarm-spawn path — kept for
		// the same defensive reason the Health line above exists: a bare content actor that never calls
		// InitializeVitals/InitializeMaxHealth still starts from a well-defined 0/0 "no shield" state.
		Shield = MaxShield;
		MARK_PROPERTY_DIRTY_FROM_NAME(UFPSREnemyHealthComponent, Shield, this);
		MARK_PROPERTY_DIRTY_FROM_NAME(UFPSREnemyHealthComponent, MaxShield, this);
		ShieldAtLastDamage = MaxShield;
		LastDamageCombatTime = GetCombatClockNow(this);
	}
}

void UFPSREnemyHealthComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	FDoRepLifetimeParams Params;
	Params.bIsPushBased = true;
	DOREPLIFETIME_WITH_PARAMS_FAST(UFPSREnemyHealthComponent, Health, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(UFPSREnemyHealthComponent, MaxHealth, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(UFPSREnemyHealthComponent, bDead, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(UFPSREnemyHealthComponent, Shield, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(UFPSREnemyHealthComponent, MaxShield, Params);
}

FPSRVitals::FResult UFPSREnemyHealthComponent::ApplyDamage(float DamageAmount, AActor* DamageInstigator, const FFPSRDamageSpec& Spec)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || bDead || DamageAmount <= 0.0f)
	{
		return FPSRVitals::FResult();
	}

	// Settle any regen owed since the last hit BEFORE spending — otherwise a shield that finished its delay between
	// hits would look like it never regenerated (the enemy is tickless: this catch-up IS its only regen moment).
	CatchUpShieldRegen();

	FPSRVitals::FPool Pool;
	Pool.Shield = Shield;
	Pool.MaxShield = MaxShield;
	Pool.Health = Health;
	Pool.MaxHealth = MaxHealth;

	// Enemies have no per-instance mitigation ATTRIBUTE layer (that's a player-only concept, §9) — the profile's own
	// per-DamageType coefficients are the final value. VitalsProfile null (no profile authored yet, §11-1) resolves
	// to 1.0/1.0 defense and the current 0.95 default reduction cap — zero-regression.
	FPSRVitals::FMitigation Mit;
	if (VitalsProfile)
	{
		VitalsProfile->ResolveDefense(Spec.DamageType, Mit.ShieldDefense, Mit.HealthDefense);
		Mit.MaxTotalReduction = VitalsProfile->MaxTotalReduction;
	}

	const FPSRVitals::FResult Result = FPSRVitals::ApplyDamage(Pool, DamageAmount, Spec, Mit);

	Shield = Pool.Shield;
	Health = Pool.Health;
	MARK_PROPERTY_DIRTY_FROM_NAME(UFPSREnemyHealthComponent, Shield, this);
	MARK_PROPERTY_DIRTY_FROM_NAME(UFPSREnemyHealthComponent, Health, this);

	// Re-anchor the regen clock to THIS hit. The enemy has no card/GE layer to rebase against (§5-4-1 is a
	// player-only concern) — ApplyDamage is simultaneously the only spender AND the only anchor-setter here.
	ShieldAtLastDamage = Shield;
	LastDamageCombatTime = GetCombatClockNow(this);

	// Server-side health-change notification (before death) — drives cosmetic damage stages (e.g. door crack/break
	// thresholds). Fired on the lethal hit too (NewHealth == 0), so the final stage runs ahead of OnDeath.
	OnHealthChanged.Broadcast(Health, MaxHealth);

	if (Health <= 0.0f)
	{
		bDead = true;
		MARK_PROPERTY_DIRTY_FROM_NAME(UFPSREnemyHealthComponent, bDead, this);
		OnDeath.Broadcast(GetOwner(), DamageInstigator);
	}

	return Result;
}

void UFPSREnemyHealthComponent::CatchUpShieldRegen()
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || bDead || MaxShield <= 0.0f || Shield >= MaxShield)
	{
		return;
	}

	const float Now = GetCombatClockNow(this);
	const float NewShield = FPSRVitals::ComputeRegeneratedShield(ShieldAtLastDamage, MaxShield,
		Now - LastDamageCombatTime, ShieldRegenPerSecond, ShieldRegenDelaySeconds, ShieldBrokenRegenDelaySeconds);

	// Monotonic-increase guard (mirrors AFPSRCharacter's player driver, §5-5) — regen never DECREASES the shield;
	// ApplyDamage is the only spender. This also naturally skips a pointless dirty-mark when nothing actually moved
	// yet (still inside the regen delay window).
	if (NewShield > Shield + KINDA_SMALL_NUMBER)
	{
		Shield = NewShield;
		MARK_PROPERTY_DIRTY_FROM_NAME(UFPSREnemyHealthComponent, Shield, this);
	}
}

void UFPSREnemyHealthComponent::ResetForReuse()
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	Health = MaxHealth;
	MARK_PROPERTY_DIRTY_FROM_NAME(UFPSREnemyHealthComponent, Health, this);

	// VIT1 §8: full shield + a fresh anchor on every reuse (the SAME rule AFPSRCharacter's own reset path follows —
	// one shared rule, two storages). InitializeVitals immediately follows this in the AcquireEnemy call chain and
	// will usually overwrite these with the newly-resolved spec anyway (deck/profile may differ from the prior
	// life) — this just keeps ResetForReuse correct standalone too.
	Shield = MaxShield;
	MARK_PROPERTY_DIRTY_FROM_NAME(UFPSREnemyHealthComponent, Shield, this);
	ShieldAtLastDamage = MaxShield;
	LastDamageCombatTime = GetCombatClockNow(this);

	bDead = false;
	MARK_PROPERTY_DIRTY_FROM_NAME(UFPSREnemyHealthComponent, bDead, this);

	// Repaint the bound health bar to full on the LISTEN-SERVER HOST (A1). The host has no OnRep, so without this it
	// would keep the last ~0% paint from the prior life until the next hit. Clients already get this for free: the
	// reused actor's Health replicates 0 -> MaxHealth and OnRep_Health fires the same broadcast, so this is purely
	// host/client symmetry (the bar hides at full health; full-health delta is the same one clients already handle).
	OnHealthChanged.Broadcast(Health, MaxHealth);
}

void UFPSREnemyHealthComponent::InitializeMaxHealth(float NewMaxHealth)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || NewMaxHealth <= 0.0f)
	{
		return;
	}

	// MaxHealth replicates (B12) so a runtime-set value (boss/door) reaches clients for a correct health-bar percent.
	MaxHealth = NewMaxHealth;
	Health = NewMaxHealth;
	MARK_PROPERTY_DIRTY_FROM_NAME(UFPSREnemyHealthComponent, Health, this);
	MARK_PROPERTY_DIRTY_FROM_NAME(UFPSREnemyHealthComponent, MaxHealth, this);

	// 🔴 VIT1 regression trap 7: this legacy no-shield entry point EXPLICITLY zeroes the shield pool (rather than
	// leaving whatever was there) — a boss/AFPSRDestructible calling this must never inherit a leftover shield from
	// a prior InitializeVitals life on the same pooled actor.
	MaxShield = 0.0f;
	Shield = 0.0f;
	MARK_PROPERTY_DIRTY_FROM_NAME(UFPSREnemyHealthComponent, Shield, this);
	MARK_PROPERTY_DIRTY_FROM_NAME(UFPSREnemyHealthComponent, MaxShield, this);
	VitalsProfile = nullptr;
	ShieldAtLastDamage = 0.0f;
	LastDamageCombatTime = GetCombatClockNow(this);

	bDead = false;
	MARK_PROPERTY_DIRTY_FROM_NAME(UFPSREnemyHealthComponent, bDead, this);
}

void UFPSREnemyHealthComponent::InitializeVitals(const FFPSRResolvedVitals& Resolved)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || Resolved.MaxHealth <= 0.0f)
	{
		return;
	}

	MaxHealth = Resolved.MaxHealth;
	Health = Resolved.MaxHealth;
	MaxShield = Resolved.MaxShield;
	Shield = Resolved.MaxShield;
	MARK_PROPERTY_DIRTY_FROM_NAME(UFPSREnemyHealthComponent, Health, this);
	MARK_PROPERTY_DIRTY_FROM_NAME(UFPSREnemyHealthComponent, MaxHealth, this);
	MARK_PROPERTY_DIRTY_FROM_NAME(UFPSREnemyHealthComponent, Shield, this);
	MARK_PROPERTY_DIRTY_FROM_NAME(UFPSREnemyHealthComponent, MaxShield, this);

	ShieldRegenPerSecond = Resolved.ShieldRegenPerSecond;
	ShieldRegenDelaySeconds = Resolved.ShieldRegenDelaySeconds;
	ShieldBrokenRegenDelaySeconds = Resolved.ShieldBrokenRegenDelaySeconds;
	VitalsProfile = Resolved.Profile;

	// Full-shield anchor (mirrors AFPSRCharacter's InitAbilityActorInfo anchor init, §5-5 — G1 P2-1 후반): a fresh
	// spawn/reuse starts with no pending regen delay, so an immediate CatchUpShieldRegen query reads back MaxShield,
	// not 0.
	ShieldAtLastDamage = MaxShield;
	LastDamageCombatTime = GetCombatClockNow(this);

	bDead = false;
	MARK_PROPERTY_DIRTY_FROM_NAME(UFPSREnemyHealthComponent, bDead, this);
}

void UFPSREnemyHealthComponent::OnRep_bDead()
{
	// Client death notify (U20): fire the cosmetic death signal only on the death edge (bDead true). A pooled reuse
	// replicates bDead true -> false; the false edge is NOT a death, so it doesn't broadcast (the reused actor resets
	// its anim state to Idle on Activate). No new replication — this is a RepNotify on the pre-existing bDead flag.
	if (bDead)
	{
		OnDeathCosmetic.Broadcast();
	}
}

void UFPSREnemyHealthComponent::OnRep_Health()
{
	// Client-side mirror of the server's OnHealthChanged broadcast (B12). Fires when Health OR MaxHealth replicates
	// (both share this RepNotify), so a client health bar / hit flash bound to OnHealthChanged repaints with the
	// correct NewHealth/MaxHealth percent. The server fires the same delegate from the authoritative ApplyDamage.
	OnHealthChanged.Broadcast(Health, MaxHealth);
}

void UFPSREnemyHealthComponent::OnRep_Shield()
{
	// VIT1 §8 initial-sync note: reuse OnHealthChanged as a generic "vitals changed, repaint" ping rather than adding
	// a new delegate — a shield-only hit does NOT replicate Health/MaxHealth (they didn't change), so without this a
	// client-bound health bar would never even hear about it. The re-broadcast carries the current (unchanged)
	// Health/MaxHealth; a widget that also wants the shield reads GetShield()/GetMaxShield() off this same component
	// inside its handler.
	OnHealthChanged.Broadcast(Health, MaxHealth);

	// Break-edge-only cosmetic (requirement 6's enemy-side half) — fires once when Shield crosses from >0 to 0.
	// Client-local edge detection via LastKnownShieldForCosmetic (🔴 zero new replication, §7 — there is no
	// dedicated "it just broke" bit on the wire, only the already-replicated Shield value). A pooled reuse's
	// 0 -> MaxShield transition is the OPPOSITE edge, so it stays silent here — mirrors OnRep_bDead's death-edge-only
	// guard against firing on the reuse's alive-again edge.
	if (LastKnownShieldForCosmetic > 0.0f && FPSRVitals::IsShieldBroken(Shield, MaxShield))
	{
		OnShieldBrokenCosmetic.Broadcast();
	}
	LastKnownShieldForCosmetic = Shield;
}
