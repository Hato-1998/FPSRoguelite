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
#include "Camera/CameraComponent.h"

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
		Recoil->SetRecoilPattern(Weapon ? Weapon->RecoilPattern : nullptr);
		// Inject the equipped weapon's heat-spread profile (curves + max + cooldown delay). MUST run on swap even to a
		// no-profile weapon: the plugin's SetRecoilPattern IGNORES null, so without an explicit clear the previous
		// weapon's heat curves would bleed into melee / ChargeLaser / no-profile weapons and keep making spread.
		// Runs on the server (EquipSlot) AND clients (OnRep) so both sides' heat model matches the equipped weapon.
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

	// ADS: smoothly interpolate camera FOV toward the aim target (owner-local feel).
	if (!CachedCamera)
	{
		CachedCamera = OwnerPawn->FindComponentByClass<UCameraComponent>();
		if (CachedCamera)
		{
			DefaultFOV = CachedCamera->FieldOfView;
		}
	}
	if (CachedCamera)
	{
		const float TargetFOV = (bIsAiming && Stats.bHasADS) ? Stats.ADSFieldOfView : DefaultFOV;
		CachedCamera->FieldOfView = FMath::FInterpTo(CachedCamera->FieldOfView, TargetFOV, DeltaTime, FMath::Max(0.01f, Stats.ADSInterpSpeed));
	}

	// Procedural aim-down-sights arm offset (owner-local) — the character owns the 1P arms; drive it from this tick
	// (the character's own Tick is debug-only / disabled in shipping). Mirrors the FOV interp above.
	if (AFPSRCharacter* Char = Cast<AFPSRCharacter>(OwnerPawn))
	{
		Char->UpdateAimDownSights(DeltaTime);
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
