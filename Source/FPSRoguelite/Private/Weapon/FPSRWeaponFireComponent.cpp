// Copyright Epic Games, Inc. All Rights Reserved.

#include "Weapon/FPSRWeaponFireComponent.h"
#include "Weapon/FPSRWeaponInventoryComponent.h"
#include "Weapon/FPSRWeaponInstance.h"
#include "Weapon/FPSRWeaponDataAsset.h"
#include "Weapon/FPSRCrosshairStyleDataAsset.h"
#include "Weapon/FPSRWeaponTypes.h"
#include "Weapon/FPSRWeaponFragment.h"
#include "Weapon/FPSRRecoilComponent.h"
#include "Data/CRRecoilPattern.h"
#include "Hero/FPSRCharacter.h"
#include "Core/FPSRGameState.h"
#include "Core/FPSRLogChannels.h" // LogFPSR (was relied on transitively via unity — make the dependency explicit, IWYU)

#include "AbilitySystemComponent.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInterface.h"
#include "AbilitySystemGlobals.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Net/UnrealNetwork.h"
#include "Net/Core/PushModel/PushModel.h"

#if ENABLE_DRAW_DEBUG
// Console toggle for all weapon debug draws (fire/laser trace lines, melee hit sphere, on-screen ammo). Default off; enable with `FPSR.Debug.WeaponDraw 1`.
static TAutoConsoleVariable<int32> CVarFPSRWeaponDebugDraw(
	TEXT("FPSR.Debug.WeaponDraw"),
	0,
	TEXT("Draw weapon debug visuals (fire/laser trace lines, melee hit sphere, on-screen ammo). 0=off (default), 1=on."),
	ECVF_Cheat);
#endif

UFPSRWeaponFireComponent::UFPSRWeaponFireComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// Needed for bIsAiming: the shared body AnimBP runs on every machine, so a teammate's aim state has to reach the
	// non-owning clients (ADR 0002 step 4). The owning actor (AFPSRCharacter) already replicates.
	SetIsReplicatedByDefault(true);
}

void UFPSRWeaponFireComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	FDoRepLifetimeParams Params;
	Params.bIsPushBased = true;
	// SkipOwner: the owner already wrote this on its own input edge (prediction), so an echo can only arrive a round
	// trip late — on a quick aim tap that flickers the owner's ADS FOV and weapon alignment back on for one RTT. Same
	// condition, same reason as the engine's own remote-aim presentation state (APawn::RemoteViewPitch16, Pawn.cpp).
	// The price is that a server-REJECTED aim-on is not corrected on the owner; see SetAiming for why that self-heals.
	Params.Condition = COND_SkipOwner;
	DOREPLIFETIME_WITH_PARAMS_FAST(UFPSRWeaponFireComponent, bIsAiming, Params);
}

void UFPSRWeaponFireComponent::BeginPlay()
{
	Super::BeginPlay();

	// bReplicates is EditDefaultsOnly ("Component Replicates" in the details panel), so a Blueprint that recorded an
	// override on this inherited component beats the constructor above — and the failure is silent: aim would look
	// right to the owner and simply never reach a teammate's body AnimBP (invariant 9's trap class).
	// Repair it rather than only reporting it, so the aim state cannot be switched off by a stale asset. The log stays
	// because the asset still needs fixing — this component has no other networked state, so there is no legitimate
	// reason for the override to exist.
	if (!GetIsReplicated())
	{
		UE_LOG(LogFPSR, Error,
			TEXT("[Weapon] %s: 'Component Replicates' is OFF on WeaponFire in the character Blueprint — the aim state could not reach remote clients (their body AnimBP would never aim). Re-enabled at runtime; clear the override in the asset."),
			*GetNameSafe(GetOwner()));
		SetIsReplicated(true);
	}
}

void UFPSRWeaponFireComponent::SetAiming(bool bNewAiming)
{
	// Writable ONLY where the decision is made: the authority, or the owning client (which predicts its own ADS so the
	// FOV/alignment react without a round trip). On a simulated proxy the value arrives by replication alone, and a
	// local write there would be PERMANENT — push-model replication resends nothing while the server's value stays
	// unchanged, so the teammate's body would stay stuck in (or out of) the aim pose. Blocked structurally, because
	// call sites drift: AFPSRPlayerState::OnRep_LifeState already ran on proxies while believing it was owner-only.
	const AActor* Owner = GetOwner();
	const APawn* OwnerPawn = Cast<APawn>(Owner);
	if (!Owner || (!Owner->HasAuthority() && !(OwnerPawn && OwnerPawn->IsLocallyControlled())))
	{
		return;
	}
	if (bIsAiming == bNewAiming)
	{
		return;
	}
	bIsAiming = bNewAiming;

	// The server is the only writer that replicates. Player pawns are never dormant (only projectiles and enemies
	// use SetNetDormancy); if that ever changes, marking a property dirty does NOT wake a dormant actor and this
	// needs FlushNetDormancy() beside it.
	if (Owner->HasAuthority())
	{
		MARK_PROPERTY_DIRTY_FROM_NAME(UFPSRWeaponFireComponent, bIsAiming, this);
	}
}

FVector2D UFPSRWeaponFireComponent::ComputeShotRecoilDelta(const FFPSRWeaponStatBlock& Stats, int32 ShotIndex)
{
	// Deterministic pattern (no random variance): Pitch = up-kick, Yaw = gentle horizontal drift.
	const float Pitch = Stats.RecoilVertical;
	const float Yaw = FMath::Sin(ShotIndex * Stats.RecoilHorizontalPatternFreq) * Stats.RecoilHorizontal;
	return FVector2D(Yaw, Pitch);
}

float UFPSRWeaponFireComponent::ComputeSpinupFireRate(const FFPSRWeaponStatBlock& Stats, float SpinupElapsed)
{
	if (!Stats.bHasSpinup || Stats.SpinupRampTime <= 0.0f)
	{
		return Stats.FireRate;
	}
	const float Alpha = FMath::Clamp(SpinupElapsed / Stats.SpinupRampTime, 0.0f, 1.0f);
	return FMath::Lerp(Stats.SpinupFireRateStart, Stats.FireRate, Alpha);
}

void UFPSRWeaponFireComponent::NotifyPlayerPitchCompensation(float DownAmount)
{
	if (DownAmount > 0.0f)
	{
		PlayerPitchCompensation += DownAmount;
	}
}

UFPSRWeaponInventoryComponent* UFPSRWeaponFireComponent::GetInventory() const
{
	return GetOwner() ? GetOwner()->FindComponentByClass<UFPSRWeaponInventoryComponent>() : nullptr;
}

UFPSRRecoilComponent* UFPSRWeaponFireComponent::ResolveRecoil()
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return nullptr;
	}
	if (!CachedRecoil)
	{
		CachedRecoil = Owner->FindComponentByClass<UFPSRRecoilComponent>();
	}
	// Bind the recoil to the OWNING controller once it exists (explicit target so a listen-server host's own recoil
	// component targets its own PC, not the plugin's GetFirstPlayerController fallback). Retried until the controller
	// is available (possession can lag component init).
	if (CachedRecoil && !bRecoilTargetSet)
	{
		if (const APawn* OwnerPawn = Cast<APawn>(Owner))
		{
			if (AController* OwningController = OwnerPawn->GetController())
			{
				CachedRecoil->SetTargetController(OwningController);
				bRecoilTargetSet = true;
			}
		}
	}
	return CachedRecoil;
}

float UFPSRWeaponFireComponent::ComputeSpreadDegrees(const FFPSRWeaponStatBlock& Stats, float HeatSpread, bool bAiming)
{
	const float Base = Stats.SpreadDegrees + HeatSpread;
	return (bAiming && Stats.bHasADS) ? Base * Stats.ADSSpreadMultiplier : Base;
}

float UFPSRWeaponFireComponent::GetCurrentSpreadDegrees() const
{
	UFPSRWeaponInventoryComponent* Inv = GetInventory();
	UFPSRWeaponInstance* Inst = Inv ? Inv->GetCurrentInstance() : nullptr;
	if (!Inst)
	{
		return 0.0f;
	}
	// Dynamic spread now comes from the recoil component's heat model (single source shared with the fire GAs).
	const UFPSRRecoilComponent* Recoil = CachedRecoil ? CachedRecoil.Get()
		: (GetOwner() ? GetOwner()->FindComponentByClass<UFPSRRecoilComponent>() : nullptr);
	const float HeatSpread = Recoil ? Recoil->GetHeatSpread() : 0.0f;
	return ComputeSpreadDegrees(Inst->GetResolvedStats(), HeatSpread, bIsAiming);
}

UMaterialInterface* UFPSRWeaponFireComponent::GetEquippedCrosshairMaterial() const
{
	UFPSRWeaponInventoryComponent* Inv = GetInventory();
	UFPSRWeaponInstance* Inst = Inv ? Inv->GetCurrentInstance() : nullptr;
	UFPSRWeaponDataAsset* Src = Inst ? Inst->GetSource() : nullptr;
	if (!Src)
	{
		return nullptr;
	}
	// Crosshair style takes precedence over the legacy per-weapon material.
	if (UFPSRCrosshairStyleDataAsset* Style = Src->CrosshairStyle.LoadSynchronous())
	{
		return Style->Material.LoadSynchronous();
	}
	return Src->CrosshairMaterial.LoadSynchronous();
}

bool UFPSRWeaponFireComponent::GetEquippedCrosshairUsesDynamic() const
{
	UFPSRWeaponInventoryComponent* Inv = GetInventory();
	UFPSRWeaponInstance* Inst = Inv ? Inv->GetCurrentInstance() : nullptr;
	UFPSRWeaponDataAsset* Src = Inst ? Inst->GetSource() : nullptr;
	if (!Src)
	{
		return true;
	}
	if (UFPSRCrosshairStyleDataAsset* Style = Src->CrosshairStyle.LoadSynchronous())
	{
		return Style->bDynamic;
	}
	return Src->bUseDynamicCrosshair;
}

bool UFPSRWeaponFireComponent::CanFire() const
{
	UFPSRWeaponInventoryComponent* Inv = GetInventory();
	return Inv && !Inv->IsReloading() && Inv->GetCurrentAmmo() > 0;
}

void UFPSRWeaponFireComponent::MaybeAutoReload()
{
	UFPSRWeaponInventoryComponent* Inv = GetInventory();
	if (!Inv)
	{
		return;
	}
	// Has ammo or already reloading: clear the guard and do nothing.
	if (Inv->GetCurrentAmmo() > 0 || Inv->IsReloading())
	{
		bReloadRequestPending = false;
		return;
	}
	// Empty and not reloading: request a reload once, only while trying to fire.
	if (!bWantsToFire || Inv->GetCurrentMagSize() <= 0 || bReloadRequestPending)
	{
		return;
	}
	if (AFPSRCharacter* Char = Cast<AFPSRCharacter>(GetOwner()))
	{
		Char->RequestReload();
		bReloadRequestPending = true;
	}
}

void UFPSRWeaponFireComponent::StartFiring()
{
	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (!OwnerPawn || !OwnerPawn->IsLocallyControlled())
	{
		return;
	}

	UFPSRWeaponInventoryComponent* Inventory = GetInventory();
	UFPSRWeaponInstance* Instance = Inventory ? Inventory->GetCurrentInstance() : nullptr;
	if (!Instance)
	{
		return;
	}

	// ChargeLaser: ignore a re-press while a charge sequence is already running on this client. The server-only fire
	// ability already rejects re-activation, but without this gate a re-click — possible when a FireRate card pushes
	// 1/FireRate below ChargeTime — would restart the local recoil ramp, add bloom, and advance NextFireReadyTime,
	// producing a phantom charge. One click = one sequence until it completes (DoChargeTick window) or a weapon swap.
	if (bChargeSequenceActive && Instance->GetSource()
		&& Instance->GetSource()->GetArchetype() == EFPSRWeaponArchetype::ChargeLaser)
	{
		return;
	}

	bWantsToFire = true;
	TimeSinceLastShot = 0.0f;
	ShotsFiredThisSpray = 0;

	const FFPSRWeaponStatBlock& Stats = Instance->GetResolvedStats();

	// ChargeLaser flows through the normal single-shot press path below: one click activates the fire ability, which
	// runs the whole charge sequence server-side (warm-up ticks -> full-power beam). No hold-to-charge state here.

	// Local fire-rate gate for the immediate press shot. The Tick auto/burst path already paces itself via
	// TimeSinceLastShot, but a fresh trigger pull fires immediately — so spam-clicking a single-shot weapon
	// (or swap-spamming) during the cooldown would apply local recoil/bloom for a shot the server-authoritative
	// cadence gate (GA ServerTryConsumeFireInterval) rejects. NextFireReadyTime tracks the same next-allowed time
	// the server uses (set per-shot to Now+1/FireRate, and to Now+swap-cooldown on equip), so recoil only kicks
	// when a shot can actually fire. Melee self-gates on MeleeAttackDelay inside FireOneShot.
	const UFPSRWeaponDataAsset* WeaponSource = Instance->GetSource();
	if (WeaponSource && WeaponSource->GetArchetype() != EFPSRWeaponArchetype::Melee)
	{
		if (GetWorld()->GetTimeSeconds() < NextFireReadyTime)
		{
			return;
		}
	}

	if (Stats.FireMode == EFPSRFireMode::Burst)
	{
		BurstShotsRemaining = FMath::Max(1, Stats.BurstCount);
	}

	// CrystalRecoil (P1): begin a new recoil sequence on trigger press (resets the pattern shot index + enables the
	// recoil tick) for pattern weapons. ChargeLaser (bespoke charge-ramp recoil) and melee (no recoil) don't drive it.
	if (WeaponSource && WeaponSource->GetArchetype() != EFPSRWeaponArchetype::Melee
		&& WeaponSource->GetArchetype() != EFPSRWeaponArchetype::ChargeLaser)
	{
		if (UFPSRRecoilComponent* Recoil = ResolveRecoil())
		{
			Recoil->StartShooting();
		}
	}

	// Immediate first shot on press.
	FireOneShot();
	if (Stats.FireMode == EFPSRFireMode::Burst && BurstShotsRemaining > 0)
	{
		--BurstShotsRemaining;
	}
}

void UFPSRWeaponFireComponent::StopFiring()
{
	// ChargeLaser no longer uses release-to-fire — one click activates the full server-side charge sequence in the
	// fire ability, so releasing the trigger just stops the auto/burst/melee repeat like any other weapon.
	bWantsToFire = false;
	ShotsFiredThisSpray = 0;
	SpinupElapsed = 0.0f;
}

void UFPSRWeaponFireComponent::OnWeaponEquipped(float EquipCooldown)
{
	// Equip boundary (server EquipSlot + client OnRep_CurrentSlotIndex). Impose a minimum post-swap cooldown before
	// the next shot. This clears the PREVIOUS weapon's cadence (a fast weapon isn't blocked by a slow one's interval)
	// while still gating the first shot by a fixed swap time — mirroring the server setting ServerNextAllowedFireTime
	// = Now + swap-cooldown, so swap-spam can't bypass fire cadence. (A mid-charge swap cancels the ChargeLaser fire
	// ability via RefreshEquippedAbility, which clears its timers in EndAbility — no charge state lives here anymore.)
	bChargeSequenceActive = false; // drop any in-progress ChargeLaser recoil ramp on a weapon swap
	SpinupElapsed = 0.0f; // drop spin-up ramp on weapon swap (no spin banking across equip)
	NextFireReadyTime = GetWorld()->GetTimeSeconds() + FMath::Max(0.0f, EquipCooldown);

	// CrystalRecoil (P1): bind the equipped weapon's recoil pattern. A null pattern (melee / ChargeLaser) is ignored by
	// the plugin's SetRecoilPattern — those weapons never call ApplyShot, so a prior weapon's pattern is never applied
	// (FireOneShot also gates ApplyShot on the equipped weapon actually having a pattern).
	if (UFPSRRecoilComponent* Recoil = ResolveRecoil())
	{
		const UFPSRWeaponInventoryComponent* Inv = GetInventory();
		UFPSRWeaponInstance* Inst = Inv ? Inv->GetCurrentInstance() : nullptr;
		const UFPSRWeaponDataAsset* Weapon = Inst ? Inst->GetSource() : nullptr;
		// Recoil pattern + heat-spread profile MUST both be re-applied (or explicitly cleared) on every equip. The
		// plugin's SetRecoilPattern IGNORES null, so a swap to a no-pattern / no-profile weapon (melee, ChargeLaser, or
		// a heat-only weapon) would otherwise KEEP the previous weapon's pattern/curves — the heat-only case still calls
		// ApplyShot (HasSpreadCurves true), whose base path would consume that STALE pattern and apply the wrong kick.
		// Explicit ClearRecoilPattern/ClearSpreadProfile prevents the bleed. Runs on the server (EquipSlot) AND clients
		// (OnRep_CurrentSlotIndex / OnRep_Slots) so both sides' recoil model matches the equipped weapon.
		if (Weapon && Weapon->RecoilPattern)
		{
			Recoil->SetRecoilPattern(Weapon->RecoilPattern);
		}
		else
		{
			Recoil->ClearRecoilPattern();
		}
		if (Weapon)
		{
			Recoil->SetSpreadProfile(Weapon->ShotToHeatCurve, Weapon->HeatToSpreadAngleCurve,
				Weapon->HeatToCooldownPerSecondCurve, Weapon->MaxRecoilHeat, Weapon->RecoilHeatCooldownDelay);
		}
		else
		{
			Recoil->ClearSpreadProfile();
		}
		Recoil->ResetHeat(); // fresh weapon starts cold
	}
}

void UFPSRWeaponFireComponent::FireOneShot()
{
	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (!OwnerPawn)
	{
		return;
	}

	UFPSRWeaponInventoryComponent* Inventory = GetInventory();
	UFPSRWeaponInstance* Instance = Inventory ? Inventory->GetCurrentInstance() : nullptr;
	UFPSRWeaponDataAsset* Weapon = Instance ? Instance->GetSource() : nullptr;
	if (!Weapon)
	{
		return;
	}

	const bool bMelee = (Weapon->GetArchetype() == EFPSRWeaponArchetype::Melee);

	// Ranged: block on empty magazine or during reload. Melee uses no ammo.
	if (!bMelee && (Inventory->IsReloading() || Inventory->GetCurrentAmmo() <= 0))
	{
		return;
	}

	const FFPSRWeaponStatBlock& Stats = Instance->GetResolvedStats();

	// Melee: enforce the configurable attack-rate cooldown (also rate-limits rapid clicks).
	if (bMelee && (GetWorld()->GetTimeSeconds() - LastMeleeTime) < Stats.MeleeAttackDelay)
	{
		return;
	}

	// Activate the weapon's fire ability (trace + damage; predicted + server-authoritative).
	if (Weapon->FireAbility)
	{
		if (UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(OwnerPawn))
		{
			ASC->TryActivateAbilityByClass(Weapon->FireAbility);
		}
	}

	// Owner-client per-shot cosmetics (montage/sound/muzzle flash). Reached only after ammo/cadence/melee-cooldown
	// checks above, so a blocked shot plays nothing. Server damage/trace is unaffected (camera-viewpoint authoritative).
	if (AFPSRCharacter* Char = Cast<AFPSRCharacter>(OwnerPawn))
	{
		Char->PlayWeaponFireCosmetics();
	}

	if (bMelee)
	{
		// Melee has no camera recoil / bloom; just stamp the attack time.
		LastMeleeTime = GetWorld()->GetTimeSeconds();
	}
	else
	{
		// Spin-up weapons ramp the client-local cadence using BASE stats (FireRate-card immune); others use resolved FireRate.
		const float CadenceRate = Weapon->BaseStats.bHasSpinup
			? ComputeSpinupFireRate(Weapon->BaseStats, SpinupElapsed)
			: Stats.FireRate;
		NextFireReadyTime = GetWorld()->GetTimeSeconds() + 1.0f / FMath::Max(CadenceRate, 0.01f);

		// Camera recoil (local feel only), ADS-dependent:
		//  - Hip-fire: weak vertical climb + strong horizontal randomness (scattered, screen stays low).
		//  - ADS: strong vertical climb + low randomness so the deterministic pattern shows (learnable line).
		const bool bADS = bIsAiming && Stats.bHasADS;
		const float VScale = bADS ? Stats.ADSVerticalScale : Stats.HipVerticalScale;
		const float HRandom = bADS ? Stats.ADSHorizontalRandom : Stats.HipHorizontalRandom;

		const FVector2D ShotDelta = ComputeShotRecoilDelta(Stats, ShotsFiredThisSpray);
		const float KickPitch = ShotDelta.Y * VScale;
		float KickYaw = 0.0f;
		if (Stats.RecoilHorizontal != 0.0f)
		{
			const float Variance = FMath::FRandRange(-1.0f, 1.0f) * Stats.RecoilHorizontal * HRandom;
			KickYaw = ShotDelta.X + Variance;
		}

		// ChargeLaser: instead of an instant kick on press, the up-kick CLIMBS gradually over the charge duration and
		// finishes exactly when the beam fires (charge complete). Set up the local ramp here; TickComponent integrates
		// it (and suppresses auto-recovery until the climb finishes). Local feel only — no networking/server-auth.
		// KNOWN LIMITATION (follow-up): this starts from the client's own click. On a REMOTE client the ServerOnly fire
		// ability may be rejected after RPC latency (an ammo/cadence/pause race the local pre-checks above missed), so
		// the ramp + re-press gate can run briefly for a shot that never fired (cosmetic only — server owns all damage;
		// self-clears at ChargeTime). The listen-server host is unaffected. Proper fix = a server charge-start/end
		// client notify, bundled with the client beam VFX follow-up (same signal).
		if (Weapon->GetArchetype() == EFPSRWeaponArchetype::ChargeLaser && Stats.ChargeTime > 0.0f)
		{
			// Mirror the server fire ability's charge duration: apply fragment PreFire + ModifyChargeTime so the ramp
			// finishes exactly when the payoff beam fires, even if a charge-time fragment shortens/extends the charge.
			float RampChargeTime = Stats.ChargeTime;
			FFPSRFireContext Ctx;
			Ctx.Avatar = OwnerPawn;
			Ctx.Controller = OwnerPawn->GetController();
			Ctx.World = GetWorld();
			Ctx.Instance = Instance;
			Ctx.ShotCount = 1;
			Ctx.bAuthority = OwnerPawn->HasAuthority();
			const TArray<TObjectPtr<UFPSRWeaponFragment>>& Frags = Instance->GetActiveFragments();
			for (const TObjectPtr<UFPSRWeaponFragment>& Frag : Frags) { if (Frag) { Frag->PreFire(Ctx); } }
			for (const TObjectPtr<UFPSRWeaponFragment>& Frag : Frags) { if (Frag) { Frag->ModifyChargeTime(Ctx, RampChargeTime); } }

			bChargeSequenceActive = true;
			ChargeRecoilElapsed = 0.0f;
			ChargeRecoilDuration = RampChargeTime;
			ChargeRecoilTotalPitch = KickPitch;
			ChargeRecoilTotalYaw = KickYaw;
		}
		else
		{
			// CrystalRecoil (P1/P2): the recoil component drives BOTH the per-shot kinematic kick (uplift/recovery,
			// needs an authored RecoilPattern) AND the heat-based dynamic spread (needs authored heat curves) — the two
			// are INDEPENDENT (a weapon may have spread with no pattern and vice versa), and the plugin null-guards each
			// (base ApplyShot no-ops without a pattern; the spread heat only advances when curves exist). Strength =
			// ADS/hip vertical scale x the recoil-down CARD scale (resolved vs base RecoilVertical) so the casual-ization
			// levers keep working WITHOUT mutating the shared pattern asset (§2-4-2). Owner-local prediction/feel; the
			// server accumulates its own heat per accepted shot (fire GA) for authoritative-trace parity.
			if (UFPSRRecoilComponent* Recoil = ResolveRecoil())
			{
				if (Weapon->RecoilPattern || Recoil->HasSpreadCurves())
				{
					const float BaseRecoilVertical = Weapon->BaseStats.RecoilVertical;
					const float CardScale = (BaseRecoilVertical > KINDA_SMALL_NUMBER) ? (Stats.RecoilVertical / BaseRecoilVertical) : 1.0f;
					Recoil->SetRecoilStrength(FMath::Max(0.0f, CardScale * VScale));
					Recoil->ApplyShot(); // uplift (if pattern) + heat accumulation (if spread curves) — owner-local
				}
			}
		}
		++ShotsFiredThisSpray;
	}
}

void UFPSRWeaponFireComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (!OwnerPawn || !OwnerPawn->IsLocallyControlled())
	{
		return;
	}

	UFPSRWeaponInventoryComponent* Inventory = GetInventory();
	UFPSRWeaponInstance* Instance = Inventory ? Inventory->GetCurrentInstance() : nullptr;
	UFPSRWeaponDataAsset* Weapon = Instance ? Instance->GetSource() : nullptr;
	if (!Weapon)
	{
		return;
	}

	// Reload restart: when a reload BEGINS, restart the recoil spray pattern from shot 0 so the fresh magazine sprays the
	// learnable pattern again. Holding fire through an (auto-)reload otherwise leaves the pattern's ShotIndex deep in its
	// end-behavior (the "late" sustained pattern), because StartShooting only resets on a fresh trigger press. Detected on
	// the replicated reloading edge (owner-local; this tick is IsLocallyControlled-gated). Melee/ChargeLaser drive no
	// pattern. Spread heat needs no reset — it cools on its own during the no-fire reload window.
	const bool bReloadingNow = Inventory->IsReloading();
	if (bReloadingNow && !bWasReloading
		&& Weapon->GetArchetype() != EFPSRWeaponArchetype::Melee
		&& Weapon->GetArchetype() != EFPSRWeaponArchetype::ChargeLaser)
	{
		if (UFPSRRecoilComponent* Recoil = ResolveRecoil())
		{
			Recoil->ResetPattern();
		}
	}
	bWasReloading = bReloadingNow;

	const FFPSRWeaponStatBlock& Stats = Instance->GetResolvedStats();
	const bool bSpinup = Weapon->BaseStats.bHasSpinup;
	const float CadenceRate = bSpinup ? ComputeSpinupFireRate(Weapon->BaseStats, SpinupElapsed) : Stats.FireRate;
	const float Interval = 1.0f / FMath::Max(CadenceRate, 0.01f);

	// Global run-freeze (§2-2): gate client fire so a held trigger doesn't produce phantom local shots/recoil/cosmetics
	// during the card-selection freeze (the server fire ability already rejects). Spin-up does not advance while paused.
	bool bRunPaused = false;
	if (const AFPSRGameState* GS = GetWorld()->GetGameState<AFPSRGameState>())
	{
		bRunPaused = GS->IsRunPaused();
	}

	const bool bAutoFiring = (bWantsToFire && Stats.FireMode == EFPSRFireMode::FullAuto && CanFire() && !bRunPaused);
	const bool bBurstFiring = (Stats.FireMode == EFPSRFireMode::Burst && BurstShotsRemaining > 0 && CanFire() && !bRunPaused);

	// Spin-up ramp advances ONLY while actively auto-firing. ANY interruption resets it to the minimum: empty mag,
	// reload (manual R or auto), run-freeze, or simply not holding fire. This keeps a trigger held THROUGH a reload
	// from resuming at full speed — the ramp must rebuild from the floor after the gun stops putting rounds downrange.
	// (StopFiring and OnWeaponEquipped also zero it explicitly for release/weapon-swap immediacy.)
	if (bSpinup)
	{
		SpinupElapsed = bAutoFiring ? (SpinupElapsed + DeltaTime) : 0.0f;
	}

	if (bAutoFiring || bBurstFiring)
	{
		TimeSinceLastShot += DeltaTime;
		int32 Safety = 0;
		while (TimeSinceLastShot >= Interval && Safety < 16)
		{
			if (Stats.FireMode == EFPSRFireMode::Burst && BurstShotsRemaining <= 0)
			{
				break;
			}
			FireOneShot();
			if (Stats.FireMode == EFPSRFireMode::Burst)
			{
				BurstShotsRemaining = FMath::Max(0, BurstShotsRemaining - 1);
			}
			TimeSinceLastShot -= Interval;
			++Safety;
		}
	}

	// Melee: repeat attacks while the button is held; FireOneShot self-gates on MeleeAttackDelay.
	if (bWantsToFire && !bRunPaused && Weapon->GetArchetype() == EFPSRWeaponArchetype::Melee)
	{
		FireOneShot();
	}

	// Auto-reload when the magazine empties while the player is still firing.
	MaybeAutoReload();

	// NOTE: the ADS camera FOV interp used to live here. It now belongs to AFPSRCharacter::UpdateCameraFieldOfView,
	// which is the single writer of FieldOfView — it composes this weapon's ADS/scope zoom with camera offsets the
	// weapon has no business knowing about (the slide widening, ADR 0001 invariant 4), and it keeps working with no
	// weapon equipped, which this tick cannot (it returns early above). This component still owns the ADS INTENT
	// (bIsAiming) and the ADS numbers on the stat block; the character reads both.

	// Procedural aim-down-sights weapon placement (owner-local) — the character owns the weapon meshes; drive it from this
	// tick because it solves against the RESOLVED stats of the weapon currently equipped, so it has nothing to do without
	// one. (The FOV above is the opposite case, which is why it moved to the character's Tick.)
	if (AFPSRCharacter* Char = Cast<AFPSRCharacter>(OwnerPawn))
	{
		Char->UpdateAimDownSights(DeltaTime);
		Char->UpdateScopeWeaponVisibility();
		// Gun-motion studio P2 curve consumption (GunMotionTool_Spec.md §4-2/§4-3): same owner-local, post-arms-pose
		// tick site as the two calls above (this component ticks after FirstPersonArms via a tick prerequisite set in
		// BeginPlay) — reading FPGM_P_* off the arms AnimInstance from anywhere earlier would reproduce the one-frame
		// gun-anchor lag this project already fixed elsewhere.
		Char->ApplyWeaponPartCurves();
	}

	// ChargeLaser charge-recoil ramp: spread the shot's up-kick across the charge so the view climbs gradually and the
	//    rise FINISHES at the fire moment (charge complete). The charge duration IS the smoothing (applied directly
	//    here), and it accumulates recovery debt so auto-recovery — gated off while the ramp is active — pulls the view
	//    back down only after the climb finishes.
	if (bChargeSequenceActive)
	{
		const float Dur = FMath::Max(0.0001f, ChargeRecoilDuration);
		const float PrevAlpha = FMath::Clamp(ChargeRecoilElapsed / Dur, 0.0f, 1.0f);
		ChargeRecoilElapsed += DeltaTime;
		const float Alpha = FMath::Clamp(ChargeRecoilElapsed / Dur, 0.0f, 1.0f);
		const float AlphaDelta = Alpha - PrevAlpha;
		if (AlphaDelta > 0.0f)
		{
			const float ApplyPitch = ChargeRecoilTotalPitch * AlphaDelta;
			if (ApplyPitch != 0.0f)
			{
				OwnerPawn->AddControllerPitchInput(-ApplyPitch); // negative = up
				RecoilDebtPitch += ApplyPitch;
			}
			const float ApplyYaw = ChargeRecoilTotalYaw * AlphaDelta;
			if (ApplyYaw != 0.0f)
			{
				OwnerPawn->AddControllerYawInput(ApplyYaw);
			}
		}
		if (Alpha >= 1.0f)
		{
			bChargeSequenceActive = false; // climb complete at the fire moment
		}
	}

	// --- Recoil recovery (ChargeLaser ramp debt): the pattern weapons' uplift/recovery live in the CrystalRecoil
	//     component; this path only services the ChargeLaser charge-ramp, which accumulates RecoilDebtPitch above. ---

	// 1) Player's manual downward compensation pays down the debt (it already moved the camera in
	//    Input_Look) so auto-recovery does not stack on top of it and overshoot below the aim point.
	if (PlayerPitchCompensation > 0.0f && RecoilDebtPitch > 0.0f)
	{
		const float Consumed = FMath::Min(PlayerPitchCompensation, RecoilDebtPitch);
		RecoilDebtPitch -= Consumed;
	}
	PlayerPitchCompensation = 0.0f;

	// 2) Auto-recover the remaining (un-compensated) debt downward when not firing. Gated per weapon: Always = on,
	//    Never = off, Auto = on only for single-shot weapons. (Only the ChargeLaser ramp sets RecoilDebtPitch now.)
	const bool bAutoRecover =
		(Stats.RecoilRecovery == ERecoilRecovery::Always) ||
		(Stats.RecoilRecovery == ERecoilRecovery::Auto && Stats.FireMode == EFPSRFireMode::Single);
	if (bAutoRecover && !bWantsToFire && !bChargeSequenceActive && RecoilDebtPitch > 0.0f)
	{
		const float Recover = FMath::Min(Stats.RecoilRecoveryRate * DeltaTime, RecoilDebtPitch);
		OwnerPawn->AddControllerPitchInput(Recover); // positive = down
		RecoilDebtPitch -= Recover;
	}

#if ENABLE_DRAW_DEBUG
	// Debug scaffolding (replaced by HUD in P3): show ammo for the local player (ammo weapons only). Gated by FPSR.Debug.WeaponDraw.
	if (CVarFPSRWeaponDebugDraw.GetValueOnGameThread() > 0 && GEngine && Weapon->GetArchetype() != EFPSRWeaponArchetype::Melee && Stats.MagSize > 0)
	{
		const int32 Ammo = Inventory->GetCurrentAmmo();
		const int32 Mag = Inventory->GetCurrentMagSize();
		const FString Msg = Inventory->IsReloading()
			? FString::Printf(TEXT("Reloading...  Ammo: %d/%d"), Ammo, Mag)
			: FString::Printf(TEXT("Ammo: %d/%d"), Ammo, Mag);
		GEngine->AddOnScreenDebugMessage((uint64)(UPTRINT)this, 0.0f, FColor::Cyan, Msg);
	}
#endif
}

#if !UE_BUILD_SHIPPING
// ---- Debug: preview the current weapon's recoil spray pattern in front of the local player ----
static FAutoConsoleCommandWithWorldAndArgs GFPSRRecoilPreviewCmd(
	TEXT("FPSR.RecoilPreview"),
	TEXT("Draw the equipped weapon's recoil spray pattern (deterministic, no variance) in front of the local player. Usage: FPSR.RecoilPreview [shots]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}

		int32 Shots = 30;
		if (Args.Num() > 0)
		{
			Shots = FMath::Clamp(FCString::Atoi(*Args[0]), 1, 200);
		}

		APlayerController* PC = World->GetFirstPlayerController();
		APawn* Player = PC ? PC->GetPawn() : nullptr;
		if (!Player)
		{
			return;
		}

		UFPSRWeaponFireComponent* FireComp = Player->FindComponentByClass<UFPSRWeaponFireComponent>();
		UFPSRWeaponInventoryComponent* Inv = FireComp ? FireComp->GetInventory() : nullptr;
		UFPSRWeaponInstance* Instance = Inv ? Inv->GetCurrentInstance() : nullptr;
		if (!Instance)
		{
			return;
		}

		UFPSRWeaponDataAsset* Weapon = Instance->GetSource();
		UCRRecoilPattern* Pattern = Weapon ? Weapon->RecoilPattern : nullptr;
		if (!Pattern)
		{
			UE_LOG(LogFPSR, Warning, TEXT("[Weapon] RecoilPreview: 장착 무기에 RecoilPattern 없음(ChargeLaser/근접/미저작) — 프리뷰할 패턴 없음."));
			return;
		}

		// Camera basis.
		FVector CamLoc = Player->GetActorLocation();
		FRotator CamRot = Player->GetControlRotation();
		if (PC && PC->PlayerCameraManager)
		{
			CamLoc = PC->PlayerCameraManager->GetCameraLocation();
			CamRot = PC->PlayerCameraManager->GetCameraRotation();
		}

		const float Dist = 1000.0f;
		float CumYaw = 0.0f;
		float CumPitch = 0.0f;
		FVector PrevPoint = FVector::ZeroVector;
		bool bHasPrev = false;
		int32 PatternShotIdx = 0;

		for (int32 i = 0; i < Shots; ++i)
		{
			// Project cumulative recoil angles onto a plane Dist units ahead of the camera.
			// Up-kick raises the aim, so it lands HIGHER on the wall -> add pitch.
			const FRotator ShotRot(CamRot.Pitch + CumPitch, CamRot.Yaw + CumYaw, 0.0f);
			const FVector Point = CamLoc + ShotRot.Vector() * Dist;

			DrawDebugPoint(World, Point, 8.0f, FColor::Yellow, false, 6.0f, 0);
			if (bHasPrev)
			{
				DrawDebugLine(World, PrevPoint, Point, FColor::Red, false, 6.0f, 0, 1.5f);
			}
			PrevPoint = Point;
			bHasPrev = true;

			// 실제 CrystalRecoil 패턴의 발당 델타(X=yaw°, Y=up-pitch°, RecoilStrength 1.0 원본 shape).
			const FVector2f Delta = Pattern->ConsumeShot(PatternShotIdx);
			CumYaw += Delta.X;
			CumPitch += Delta.Y;
		}
	}));

// ---- Debug: dump the equipped weapon's base vs resolved stats (verifies weapon-stat modifier cards) ----
static FAutoConsoleCommandWithWorld GFPSRDumpWeaponStatsCmd(
	TEXT("FPSR.DumpWeaponStats"),
	TEXT("Log the local player's current weapon: base stats vs resolved (after modifiers)."),
	FConsoleCommandWithWorldDelegate::CreateStatic([](UWorld* World)
	{
		if (!World)
		{
			return;
		}
		APlayerController* PC = World->GetFirstPlayerController();
		APawn* Player = PC ? PC->GetPawn() : nullptr;
		UFPSRWeaponInventoryComponent* Inv = Player ? Player->FindComponentByClass<UFPSRWeaponInventoryComponent>() : nullptr;
		UFPSRWeaponInstance* Instance = Inv ? Inv->GetCurrentInstance() : nullptr;
		UFPSRWeaponDataAsset* Weapon = Instance ? Instance->GetSource() : nullptr;
		if (!Weapon)
		{
			UE_LOG(LogFPSR, Warning, TEXT("[Weapon] DumpWeaponStats: no equipped weapon"));
			return;
		}

		const FFPSRWeaponStatBlock& Base = Weapon->BaseStats;
		const FFPSRWeaponStatBlock& Res = Instance->GetResolvedStats();
		UE_LOG(LogFPSR, Log, TEXT("[Weapon] %s | MagSize %d->%d | FireRate %.2f->%.2f | RecoilV %.2f->%.2f | Damage %.1f->%.1f | Ammo %d/%d"),
			*Weapon->GetName(),
			Base.MagSize, Res.MagSize,
			Base.FireRate, Res.FireRate,
			Base.RecoilVertical, Res.RecoilVertical,
			Base.Damage, Res.Damage,
			Inv->GetCurrentAmmo(), Res.MagSize);
	}));
#endif // !UE_BUILD_SHIPPING
