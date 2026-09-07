// Copyright Epic Games, Inc. All Rights Reserved.

#include "Enemy/FPSREnemyHealthComponent.h"
#include "Core/FPSRGameState.h"
#include "Core/FPSRLogChannels.h"
#include "Status/FPSRStatus.h"
#include "Weapon/FPSRWeaponInstance.h" // full type needed: TWeakObjectPtr<UFPSRWeaponInstance>::operator= requires a complete type
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

	/** STAT1 §6-1: the STATUS-only clock — deliberately separate from GetCombatClockNow above (see
	 *  AFPSRGameState::GetStatusClockSeconds's own header comment for why widening the combat clock's freeze axis
	 *  instead would be a design change, not a bugfix). Every status timestamp (SlotExpiry/SlotCooldownUntil/
	 *  LastStatusStepClock) lives on THIS axis, never the combat clock. */
	float GetStatusClockNow(const UActorComponent* Component)
	{
		const AActor* Owner = Component ? Component->GetOwner() : nullptr;
		const UWorld* World = Owner ? Owner->GetWorld() : nullptr;
		const AFPSRGameState* GameState = World ? World->GetGameState<AFPSRGameState>() : nullptr;
		return GameState ? GameState->GetStatusClockSeconds() : 0.0f;
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
	// STAT1 §8: the 6th Push Model property — see StatusBits's own header comment for why it must stay class-direct.
	DOREPLIFETIME_WITH_PARAMS_FAST(UFPSREnemyHealthComponent, StatusBits, Params);
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
	// STAT1 §6 방어력감소: per-instance layer, composed HERE (never from the profile) — same "opened at the
	// ApplyDamage call site" shape FMitigation::DirectionalArmorDR already establishes (that field's own comment).
	// ApplyDamage only ever runs on the server (checked at this function's own entry above), so ResolvedStatus is
	// always this component's live, server-authoritative cache here — never the client's default no-op struct.
	Mit.IncomingDamageMultiplier = ResolvedStatus.IncomingDamageMultiplier;

	const FPSRVitals::FResult Result = FPSRVitals::ApplyDamage(Pool, DamageAmount, Spec, Mit);

	Shield = Pool.Shield;
	Health = Pool.Health;
	MARK_PROPERTY_DIRTY_FROM_NAME(UFPSREnemyHealthComponent, Shield, this);
	MARK_PROPERTY_DIRTY_FROM_NAME(UFPSREnemyHealthComponent, Health, this);

	// Re-anchor the regen clock to THIS hit. The enemy has no card/GE layer to rebase against (§5-4-1 is a
	// player-only concern) — ApplyDamage is simultaneously the only spender AND the only anchor-setter here.
	// STAT1 §6-2 (G1r3 R3-3): Spec.bDotRegenAnchorPolicy — set true by the C2 DoT batch pass ONLY — backdates the
	// TIME anchor instead of stamping "now"; the VALUE anchor is re-stamped to the current Shield EITHER WAY. Do not
	// "fix" this by merely freezing/skipping LastDamageCombatTime while still moving ShieldAtLastDamage:
	// ComputeRegeneratedShield is an ABSOLUTE formula (ShieldAtLastDamage + RegenPerSecond * elapsed), not an
	// incremental one, so a frozen time anchor plus a repeatedly-lowered value anchor compounds every earlier tick's
	// regen back in on the NEXT tick. Worked example that a "freeze time" policy fails (MaxShield 100, Regen 10/s,
	// PartialDelay 3s, BrokenDelay 6s, a 5-dmg DoT tick every 0.5s starting from Shield 50 at t=0): t=3.5 -> 25,
	// t=4.5 -> 40, t=5.5 -> 75, t=6.0 -> 100 (fully regenerated while still taking damage every tick, §10 unit test
	// 12). Backdating the time anchor by exactly the delay the post-damage state demands instead reads as "impose no
	// NEW delay, but don't grant free elapsed regen time either" — regen resumes incrementally from here, as if this
	// hit had landed that many seconds ago rather than right now.
	ShieldAtLastDamage = Shield;
	const float Now = GetCombatClockNow(this);
	LastDamageCombatTime = Spec.bDotRegenAnchorPolicy
		? Now - (Shield <= 0.0f ? ShieldBrokenRegenDelaySeconds : ShieldRegenDelaySeconds)
		: Now;

	// Server-side health-change notification (before death) — drives cosmetic damage stages (e.g. door crack/break
	// thresholds). Fired on the lethal hit too (NewHealth == 0), so the final stage runs ahead of OnDeath.
	OnHealthChanged.Broadcast(Health, MaxHealth);

	// 🔴 Authority-side half of the shield-break cosmetic (VIT1 requirement 6, G2 red-team finding 2026-09-02).
	// OnRep_Shield below is the CLIENT half and never runs on the authority, so without this the listen-server HOST
	// plays no shield-break cosmetic all session — and standalone/solo plays none at all. Same "fires on BOTH server
	// and clients" shape as OnHealthChanged just above (AFPSREnemyBase.cpp:166-167); the death cosmetic hit exactly
	// this trap first and documents it at AFPSREnemyBase::HandleDeath ("the host is the one machine that never plays
	// the death state"). No double-fire: OnRep does not run on the authority.
	if (Result.bShieldBroke)
	{
		OnShieldBrokenCosmetic.Broadcast();
	}

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

	// STAT1 §7-6: one of the 4 lifecycle closure points — this is the repo's ONLY existing ResetForReuse call site
	// (AFPSREnemyBase::Activate). The other 3 (EnterDyingState / Deactivate / ServerResetEliteForStageCarry) are
	// C단계's job to also wire — ClearStatusForReuse is public and idempotent specifically so that wiring is a
	// one-line call each, no new plumbing needed here.
	ClearStatusForReuse();

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

// ---------------------------------------------------------------------------------------------------------------
// STAT1 (B단계 — data/pure-function core + storage only). See FPSRStatus.h for the pure-function contract; the 6
// methods below are the ONLY public surface (§7-7) — everything else (movement/attack/damage hooks, the batch pass,
// cards, boss Tick, the OnRep_StatusBits cosmetic broadcast) is C단계/D단계 wiring, not this phase's.
// ---------------------------------------------------------------------------------------------------------------

bool UFPSREnemyHealthComponent::ApplyStatus(const UFPSRStatusCatalogDataAsset* Catalog, uint8 Slot,
	float WeakResist, float StrongResist, AActor* Instigator, UFPSRWeaponInstance* SourceWeapon,
	TArray<uint8, TInlineAllocator<8>>& OutFired)
{
	OutFired.Reset();

	// §5-6: the target gate. bStatusDriverPresent false means nothing will ever call AdvanceStatus for this actor
	// (a door / mission-flee-target / homing orb sharing this component with no C단계 driver registered) — applying
	// here would set a bit that can never expire, so this rejects silently rather than half-applying.
	if (!GetOwner() || !GetOwner()->HasAuthority() || !bStatusDriverPresent || !Catalog)
	{
		return false;
	}

	const uint8 BitsBefore = StatusBits;
	const bool bApplied = FPSRStatus::Apply(StatusBits, StatusServer, *Catalog, Slot,
		GetStatusClockNow(this), WeakResist, StrongResist, OutFired);

	if (bApplied)
	{
		// §7-1 "마지막 시전자 승계": FPSRStatus::Apply never touches actor/weapon references (see FPSRStatus.h's
		// header comment on why), so this wrapper owns DoT kill-credit bookkeeping — unconditionally on every
		// successful apply, since there is only one shared DoT-credit slot (§5-3), not one per status/slot.
		StatusServer.DotInstigator = Instigator;
		StatusServer.DotSourceWeapon = SourceWeapon;

		if (StatusBits != BitsBefore)
		{
			MARK_PROPERTY_DIRTY_FROM_NAME(UFPSREnemyHealthComponent, StatusBits, this);
			ResolvedStatus = FPSRStatus::Resolve(StatusBits, *Catalog); // §7-5: only recompute when bits actually moved
		}
	}

	return bApplied;
}

bool UFPSREnemyHealthComponent::AdvanceStatus(const UFPSRStatusCatalogDataAsset* Catalog,
	float WeakResist, float StrongResist, float& OutDotDamage, AActor*& OutDotInstigator,
	UFPSRWeaponInstance*& OutDotSourceWeapon,
	TArray<uint8, TInlineAllocator<8>>& OutExpired, TArray<uint8, TInlineAllocator<8>>& OutFired)
{
	OutDotDamage = 0.0f;
	OutDotInstigator = nullptr;
	OutDotSourceWeapon = nullptr;
	OutExpired.Reset();
	OutFired.Reset();

	if (!GetOwner() || !GetOwner()->HasAuthority() || !bStatusDriverPresent || !Catalog)
	{
		return false;
	}

	const uint8 BitsBefore = StatusBits;
	const bool bChanged = FPSRStatus::Advance(StatusBits, StatusServer, *Catalog,
		GetStatusClockNow(this), WeakResist, StrongResist, OutDotDamage, OutExpired, OutFired);

	// §6 DoT row: this component deliberately does NOT call ApplyDamage itself with OutDotDamage — the batch pass
	// (C단계) routes it through FPSRCombat::ApplyDamage so lifesteal/bWasEnemy/mission-tracking axes stay alive.

	// C2단계: hand back the DoT's stored kill-credit refs ALONGSIDE the raw damage (see this method's header
	// comment) — resolved from the weak refs HERE, at the moment of the call, so the caller never holds a pointer
	// that could go stale between this step and its own later use of it this same frame.
	if (OutDotDamage > 0.0f)
	{
		OutDotInstigator = StatusServer.DotInstigator.Get();
		OutDotSourceWeapon = StatusServer.DotSourceWeapon.Get();
	}

	if (StatusBits != BitsBefore)
	{
		MARK_PROPERTY_DIRTY_FROM_NAME(UFPSREnemyHealthComponent, StatusBits, this);
		ResolvedStatus = FPSRStatus::Resolve(StatusBits, *Catalog);
	}

	return bChanged;
}

void UFPSREnemyHealthComponent::ClearStatusForReuse()
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	// §7-6 closure checklist, applied verbatim. Public + idempotent (safe to call on an already-clear state, e.g.
	// an actor that never had a status applied) so C단계 can wire the remaining 3 closure points as a one-line call
	// each with no extra guarding.
	if (StatusBits != 0)
	{
		StatusBits = 0;
		MARK_PROPERTY_DIRTY_FROM_NAME(UFPSREnemyHealthComponent, StatusBits, this);
	}

	for (float& Expiry : StatusServer.SlotExpiry)
	{
		Expiry = 0.0f;
	}
	for (float& CooldownUntil : StatusServer.SlotCooldownUntil)
	{
		CooldownUntil = 0.0f;
	}
	// 🔴 §7-4 / this file's cold-start anchor note: a stale LastStatusStepClock is exactly what turns a reused
	// actor's first DoT step into a multi-second burst — this reset is the half of the fix that covers REUSE
	// (FPSRStatus::Apply's own dormant-wake anchor covers a first-ever-spawn actor that never reaches this at all).
	StatusServer.LastStatusStepClock = 0.0f;
	StatusServer.DotAccumulator = 0.0f;
	StatusServer.DotInstigator = nullptr;
	StatusServer.DotSourceWeapon = nullptr;

	ResolvedStatus = FFPSRResolvedStatus();
}

void UFPSREnemyHealthComponent::OnRep_StatusBits()
{
	// STAT1 (B단계): intentionally empty. The cosmetic client-edge broadcast (GMS pub/sub so the 3 remote co-op
	// clients can SEE a status icon/audio cue, §8 — mirrors OnRep_Shield's break-edge pattern above) is D단계 wiring.
	// Declaring this RepNotify now, rather than adding it whole in D단계, is what lets StatusBits replicate
	// correctly TODAY without a later signature change — an empty RepNotify is a legal, well-defined no-op: the
	// property still replicates and OnRep still fires, only the client-side reaction is deferred.
}
