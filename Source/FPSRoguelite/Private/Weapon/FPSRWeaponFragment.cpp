// Copyright Epic Games, Inc. All Rights Reserved.

#include "Weapon/FPSRWeaponFragment.h"
#include "Combat/FPSRCombatStatics.h"
#include "Weapon/FPSRWeaponInstance.h"
#include "Weapon/FPSRWeaponInventoryComponent.h"
#include "AbilitySystem/Attributes/FPSRCombatSet.h"
#include "Hero/FPSRCharacter.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"
#include "AbilitySystemComponent.h"

namespace FPSRWeaponHooks
{
	void NotifyFire(const FFPSRFireContext& Context)
	{
		if (!Context.Instance) { return; }
		for (const TObjectPtr<UFPSRWeaponFragment>& Frag : Context.Instance->GetActiveFragments())
		{
			if (Frag) { Frag->OnFire(Context); }
		}

		// B4 (cosmetic): the server broadcasts the fire SFX to all clients so teammates hear each other's fire. This
		// is the central per-shot, all-weapons fire-confirm site (every fire GA calls NotifyFire), so a new weapon GA
		// gets remote fire audio for free. Gate on authority so only the server originates the multicast; the owner's
		// own shot is played locally and MulticastFireCosmetics skips the locally-controlled owner (no double-play).
		if (Context.bAuthority)
		{
			if (AFPSRCharacter* Char = Cast<AFPSRCharacter>(Context.Avatar))
			{
				Char->MulticastFireCosmetics();
			}
		}
	}

	void NotifyMiss(const FFPSRFireContext& Context)
	{
		if (!Context.Instance) { return; }
		for (const TObjectPtr<UFPSRWeaponFragment>& Frag : Context.Instance->GetActiveFragments())
		{
			if (Frag) { Frag->OnMiss(Context); }
		}
	}

	void NotifyKill(const FFPSRFireContext& Context, AActor* KilledActor)
	{
		if (!Context.Instance || !KilledActor) { return; }
		for (const TObjectPtr<UFPSRWeaponFragment>& Frag : Context.Instance->GetActiveFragments())
		{
			if (Frag) { Frag->OnKill(Context, KilledActor); }
		}
	}

	void NotifyAim(const FFPSRFireContext& Context, bool bAiming)
	{
		if (!Context.Instance) { return; }
		for (const TObjectPtr<UFPSRWeaponFragment>& Frag : Context.Instance->GetActiveFragments())
		{
			if (Frag) { Frag->OnAim(Context, bAiming); }
		}
	}

	FFPSRCritContext BuildCritContext(const FFPSRFireContext& Context, const UAbilitySystemComponent* ASC)
	{
		// Base = the ASC's global attributes (0/1 if there is no ASC at all — a non-GAS instigator never crits).
		FFPSRCritContext Crit;
		if (ASC)
		{
			Crit.Chance = ASC->GetNumericAttribute(UFPSRCombatSet::GetGlobalCritChanceAttribute());
			Crit.Multiplier = ASC->GetNumericAttribute(UFPSRCombatSet::GetGlobalCritMultiplierAttribute());
		}
		if (!Context.Instance)
		{
			return Crit; // no weapon instance -> no timed buffs, no fragments to run ModifyCrit over
		}

		// Layer the weapon's active timed buffs (cards 3/5) on top of the ASC base.
		float BuffChanceAdd = 0.0f;
		float BuffMultiplierAdd = 0.0f;
		Context.Instance->SumActiveCritBuffs(BuffChanceAdd, BuffMultiplierAdd);
		Crit.Chance += BuffChanceAdd;
		Crit.Multiplier += BuffMultiplierAdd;

		// Let every active fragment adjust the rider fields (cards 1/2/4) before the context is baked for this
		// activation. Stateless, once per activation — the same cost class as ModifyFireMode.
		for (const TObjectPtr<UFPSRWeaponFragment>& Frag : Context.Instance->GetActiveFragments())
		{
			if (Frag) { Frag->ModifyCrit(Context, Crit); }
		}
		return Crit;
	}

	void NotifyReloadFinished(APawn* Avatar, UFPSRWeaponInstance* Instance)
	{
		if (!Instance) { return; }
		// No live activation to reuse — synthesize a minimal context (mirrors AFPSRCharacter::ServerSetAiming_
		// Implementation's OnAim composition). Caller (FinishReload) already gates on HasAuthority().
		FFPSRFireContext Context;
		Context.Avatar = Avatar;
		Context.Controller = Avatar ? Avatar->GetController() : nullptr;
		Context.World = Avatar ? Avatar->GetWorld() : nullptr;
		Context.Instance = Instance;
		Context.bAuthority = true;
		for (const TObjectPtr<UFPSRWeaponFragment>& Frag : Instance->GetActiveFragments())
		{
			if (Frag) { Frag->OnReloadFinished(Context); }
		}
	}

	void NotifySlideStarted(APawn* Avatar)
	{
		// Resolves the weapon itself — keeps the character movement component from having to know weapons exist.
		if (!Avatar || !Avatar->HasAuthority())
		{
			return;
		}
		const UFPSRWeaponInventoryComponent* Inventory = Avatar->FindComponentByClass<UFPSRWeaponInventoryComponent>();
		UFPSRWeaponInstance* Instance = Inventory ? Inventory->GetCurrentInstance() : nullptr;
		if (!Instance)
		{
			return;
		}
		FFPSRFireContext Context;
		Context.Avatar = Avatar;
		Context.Controller = Avatar->GetController();
		Context.World = Avatar->GetWorld();
		Context.Instance = Instance;
		Context.bAuthority = true;
		for (const TObjectPtr<UFPSRWeaponFragment>& Frag : Instance->GetActiveFragments())
		{
			if (Frag) { Frag->OnSlideStarted(Context); }
		}
	}
}

void UFPSRFragment_ExplosiveRounds::OnImpact(const FFPSRFireContext& Context, const FVector& ImpactPoint, bool bAllowSelf, bool& bOutHitEnemy) const
{
	// Server-authoritative: spawn a small radial explosion at the bullet's impact. No crit on the splash (the
	// pellet already rolled its own crit); self/friendly damage and knockback follow the shared explosion rules.
	if (!Context.bAuthority || !Context.World || AOERadius <= 0.0f)
	{
		return;
	}

	// VIT1 §5-9 ③ "Fragment 폭발" row: built here from Context.Instance's ALREADY-RESOLVED stats — no new lookup.
	// DamageType stays empty (Physical); this splash inherits the host weapon's own anti-shield multiplier.
	FFPSRDamageSpec DamageSpec;
	DamageSpec.ShieldDamageMultiplier = Context.Instance ? Context.Instance->GetResolvedStats().ShieldDamageMultiplier : 1.0f;

	const FPSRCombat::FExplosionResult Outcome = FPSRCombat::ApplyExplosion(Context.World, ImpactPoint, AOERadius, AOEDamage,
		FFPSRCritContext{}, Context.Avatar, bAllowSelf, KnockbackStrength, DamageSpec);

	// Report a connecting splash so the firing ability doesn't count this activation as a miss (the ExplosiveRounds +
	// AmmoOnMiss combo must not refund ammo when the wall-splash actually hit an enemy).
	bOutHitEnemy = Outcome.bAnyEnemyHit;

	// A splash kill (e.g. rifle + ExplosiveRounds) fires OnKill too — Context here is the live firing context, so the
	// bridge reaches this weapon's fragments directly (e.g. a reload-on-kill fragment on the same weapon).
	for (AActor* KilledActor : Outcome.KilledEnemies)
	{
		FPSRWeaponHooks::NotifyKill(Context, KilledActor);
	}
}

void UFPSRFragment_AmmoOnMiss::OnMiss(const FFPSRFireContext& Context) const
{
	// Server-authoritative: top up the magazine (clamped — SetCurrentAmmo does not clamp, there is no AddAmmo helper).
	if (!Context.bAuthority || !Context.Instance)
	{
		return;
	}
	const int32 MagSize = Context.Instance->GetResolvedStats().MagSize;
	Context.Instance->SetCurrentAmmo(FMath::Min(Context.Instance->GetCurrentAmmo() + RefillAmount, MagSize));
}

void UFPSRFragment_ReloadOnKill::OnKill(const FFPSRFireContext& Context, AActor* /*KilledActor*/) const
{
	// Server-authoritative: instant top-up, or kick off the weapon's timed reload (no-op if full/already reloading).
	if (!Context.bAuthority || !Context.Instance)
	{
		return;
	}
	if (bInstantRefill)
	{
		// Instant: refill the EXACT killing weapon instance, even if it is now holstered (e.g. a bazooka projectile
		// that landed after a weapon swap). SetCurrentAmmo targets Context.Instance directly, so this is swap-safe.
		Context.Instance->SetCurrentAmmo(Context.Instance->GetResolvedStats().MagSize);
		// A full magazine alone is not usable if the killing weapon is the EQUIPPED one mid-(auto)reload: the fire gate
		// blocks on IsReloading() until the timer completes (the common one-shot case, e.g. bazooka fire->auto-reload->
		// projectile kill). Cancel that reload so the payoff is immediate. A holstered killer already had its reload
		// cancelled at swap, and its still-running timer would otherwise FinishReload the wrong (current) slot.
		if (Context.Avatar)
		{
			if (UFPSRWeaponInventoryComponent* Inventory = Context.Avatar->FindComponentByClass<UFPSRWeaponInventoryComponent>())
			{
				if (Inventory->GetCurrentInstance() == Context.Instance)
				{
					Inventory->CancelReload();
				}
			}
		}
	}
	else if (Context.Avatar)
	{
		if (UFPSRWeaponInventoryComponent* Inventory = Context.Avatar->FindComponentByClass<UFPSRWeaponInventoryComponent>())
		{
			// Timed reload animates the EQUIPPED weapon only. If the kill came from a now-holstered weapon (deferred
			// projectile after a swap), skip rather than reload the wrong slot — StartReload only knows the current slot.
			if (Inventory->GetCurrentInstance() == Context.Instance)
			{
				Inventory->StartReload();
			}
		}
	}
}

void UFPSRFragment_CritBonusInstance::ModifyCrit(const FFPSRFireContext& Context, FFPSRCritContext& CritInOut) const
{
	// Stack composition (G1 P2-5): ActiveFragments holds one element per stack, so a second copy of this card adds
	// a second BonusRatio rather than overwriting the first.
	CritInOut.BonusInstanceRatio += BonusRatio;
}

void UFPSRFragment_CritLifesteal::ModifyCrit(const FFPSRFireContext& Context, FFPSRCritContext& CritInOut) const
{
	CritInOut.HealRatio += HealRatio;
	// Set-if-empty: the first fragment to claim HealEffect wins (two copies of this card share one DA anyway, but a
	// future second lifesteal-style card must not silently steal the slot from this one).
	if (!CritInOut.HealEffect)
	{
		CritInOut.HealEffect = HealEffect;
	}
}

void UFPSRFragment_CritOnReload::OnReloadFinished(const FFPSRFireContext& Context) const
{
	// State-mutating hook: gate on authority here too (mirrors UFPSRFragment_ExplosiveRounds::OnImpact), even though
	// the NotifyReloadFinished bridge already only fires from an authority-gated call site.
	if (!Context.bAuthority || !Context.Instance)
	{
		return;
	}
	Context.Instance->ApplyTimedCritBuff(this, CritChanceAdd, CritMultiplierAdd, Duration);
}

void UFPSRFragment_WeakpointAlwaysCrit::ModifyCrit(const FFPSRFireContext& Context, FFPSRCritContext& CritInOut) const
{
	CritInOut.bWeakpointAlwaysCrit |= true; // bool stacking rule (G1 P2-5) — harmless even at MaxStacks=1
}

void UFPSRFragment_CritOnSlide::OnSlideStarted(const FFPSRFireContext& Context) const
{
	if (!Context.bAuthority || !Context.Instance)
	{
		return;
	}
	// Re-sliding while this buff is already up REFRESHES it (ApplyTimedCritBuff overwrites on the same Source) —
	// that reset-on-reslide is the card's own wording, not a bug to guard against.
	Context.Instance->ApplyTimedCritBuff(this, CritChanceAdd, 0.0f, Duration);
}


#if WITH_EDITOR
#define LOCTEXT_NAMESPACE "FPSRWeaponFragment"

/** Shared by the two timed-buff fragments: their hook refreshes an existing buff instead of adding to it. */
static EDataValidationResult FPSRValidateSingleStackTimedBuff(
	const UFPSRWeaponFragment& Fragment, EDataValidationResult InResult, FDataValidationContext& Context)
{
	if (Fragment.MaxStacks > 1)
	{
		Context.AddError(FText::Format(
			LOCTEXT("TimedBuffNoStacks", "'{0}' grants a TIMED crit buff, which REFRESHES on re-application instead of adding — a second stack would silently do nothing. Set MaxStacks to 1."),
			FText::FromString(Fragment.GetName())));
		return EDataValidationResult::Invalid;
	}
	return InResult;
}

EDataValidationResult UFPSRFragment_CritOnReload::IsDataValid(FDataValidationContext& Context) const
{
	return FPSRValidateSingleStackTimedBuff(*this, Super::IsDataValid(Context), Context);
}

EDataValidationResult UFPSRFragment_CritOnSlide::IsDataValid(FDataValidationContext& Context) const
{
	return FPSRValidateSingleStackTimedBuff(*this, Super::IsDataValid(Context), Context);
}

EDataValidationResult UFPSRFragment_CritLifesteal::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	if (HealRatio > 0.0f && !HealEffect)
	{
		Context.AddError(FText::Format(
			LOCTEXT("CritLifestealNoHealEffect", "'{0}' has HealRatio {1} but no HealEffect, so the crit heal silently does nothing at runtime. Assign the instant heal GE (GE_Card_LifestealHeal reuses the same SetByCaller.CardMagnitude contract)."),
			FText::FromString(GetName()), FText::AsNumber(HealRatio)));
		Result = EDataValidationResult::Invalid;
	}
	return Result;
}

#undef LOCTEXT_NAMESPACE
#endif // WITH_EDITOR
