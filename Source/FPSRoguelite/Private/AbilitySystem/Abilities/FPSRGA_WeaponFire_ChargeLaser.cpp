// Copyright Epic Games, Inc. All Rights Reserved.

#include "AbilitySystem/Abilities/FPSRGA_WeaponFire_ChargeLaser.h"
#include "AbilitySystem/Attributes/FPSRCombatSet.h"
#include "Weapon/FPSRWeaponInventoryComponent.h"
#include "Weapon/FPSRWeaponInstance.h"
#include "Weapon/FPSRWeaponFragment.h"
#include "Combat/FPSRCombatStatics.h"
#include "Combat/FPSRWeakpointComponent.h"
#include "Core/FPSRGameState.h"
#include "Core/FPSRPlayerController.h"
#include "Core/FPSRPlayerState.h"

#include "AbilitySystemComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "DrawDebugHelpers.h"
#include "HAL/IConsoleManager.h"

UFPSRGA_WeaponFire_ChargeLaser::UFPSRGA_WeaponFire_ChargeLaser()
{
	// One click runs the WHOLE charge sequence on the server via internal timers (no hold-to-charge, no per-charge
	// RPC handshake). ServerOnly: the owning client's TryActivateAbilityByClass routes to a server activation request
	// with no prediction key — verified against engine InternalTryActivateAbility (ServerOnly + bAllowRemoteActivation
	// -> CallServerTryActivateAbility, AbilitySystemComponent_Abilities.cpp:1633). This keeps the long (>=1s) charge
	// fully server-authoritative with no prediction-key churn. The client's local recoil rides the normal FireOneShot
	// path; the client-side beam VFX is a follow-up (ServerOnly does not replicate activation to the client).
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
	// InstancingPolicy stays InstancedPerActor (base class) — required so each activation owns its own timer state.
}

void UFPSRGA_WeaponFire_ChargeLaser::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	APawn* Avatar = Cast<APawn>(ActorInfo->AvatarActor.Get());
	AController* Controller = Avatar ? Avatar->GetController() : nullptr;
	UWorld* World = Avatar ? Avatar->GetWorld() : nullptr;
	if (!Avatar || !Controller || !World)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// InstancedPerActor: a prior activation's timers should already be cleared by EndAbility, but clear defensively
	// before starting a fresh sequence so a stale handle can never fire into this activation.
	World->GetTimerManager().ClearTimer(TickTimerHandle);
	World->GetTimerManager().ClearTimer(FinalTimerHandle);

	// No firing while the run is frozen for card selection (Game.MD §2-2).
	if (const AFPSRGameState* RunState = World->GetGameState<AFPSRGameState>())
	{
		if (RunState->IsRunPaused())
		{
			EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
			return;
		}
	}

	// No firing once the player is dead (U2 defeat wiring) — server-authoritative gate mirroring the input block.
	if (const AFPSRPlayerState* OwnerPS = Avatar ? Avatar->GetPlayerState<AFPSRPlayerState>() : nullptr)
	{
		if (OwnerPS->IsDead())
		{
			EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
			return;
		}
	}

	// Resolve weapon stats (base × modifiers).
	UFPSRWeaponInventoryComponent* Inventory = Avatar->FindComponentByClass<UFPSRWeaponInventoryComponent>();
	UFPSRWeaponInstance* Instance = Inventory ? Inventory->GetCurrentInstance() : nullptr;
	const FFPSRWeaponStatBlock* Stats = Instance ? &Instance->GetResolvedStats() : nullptr;
	if (!Inventory || !Stats)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// Server gates (ServerOnly ability — always authoritative here): empty mag / reloading / fire-rate cadence. One
	// charge sequence costs one magazine round, consumed up-front on the click (the warm-up ticks are not extra ammo).
	if (Inventory->IsReloading() || Inventory->GetCurrentAmmo() <= 0)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}
	const float MinInterval = 1.0f / FMath::Max(Stats->FireRate, 0.01f);
	if (!Inventory->ServerTryConsumeFireInterval(MinInterval))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}
	Inventory->ConsumeAmmo(1);

	// Cache the per-activation values the timer callbacks need (the callbacks have no ActorInfo of their own).
	CachedAvatar = Avatar;
	CachedController = Controller;
	CachedWorld = World;
	CachedInstance = Instance;
	CachedDamage = Stats->Damage;
	CachedTickDamage = Stats->ChargeTickDamage;
	CachedRange = Stats->Range;
	CachedSpread = Stats->SpreadDegrees;

	float ChargeTime = Stats->ChargeTime;
	const float TickInterval = FMath::Max(0.02f, Stats->ChargeTickInterval);

	// Behavior fragments (P4-B-2): PreFire builds the context, then ModifyChargeTime adjusts the charge duration
	// before the timers are armed. The context is cached for the per-hit OnHitActor / PostFire hooks on the payoff shot.
	CachedFireCtx = FFPSRFireContext();
	CachedFireCtx.Avatar = Avatar;
	CachedFireCtx.Controller = Controller;
	CachedFireCtx.World = World;
	CachedFireCtx.Instance = Instance;
	CachedFireCtx.ShotCount = 1;
	CachedFireCtx.bAuthority = true;

	const TArray<TObjectPtr<UFPSRWeaponFragment>>& Fragments = Instance->GetActiveFragments();
	for (const TObjectPtr<UFPSRWeaponFragment>& Frag : Fragments)
	{
		if (Frag) { Frag->PreFire(CachedFireCtx); }
	}
	for (const TObjectPtr<UFPSRWeaponFragment>& Frag : Fragments)
	{
		if (Frag) { Frag->ModifyChargeTime(CachedFireCtx, ChargeTime); }
	}

	// Instant weapon (ChargeTime <= 0): fire the full-power beam immediately, no warm-up.
	if (ChargeTime <= 0.0f)
	{
		DoFinalShot();
		return;
	}

	// Arm the sequence: a looping warm-up beam (only if ChargeTickDamage > 0) and the one-shot payoff beam at
	// ChargeTime. The ability stays ACTIVE for the whole charge, so the server rejects any re-activation until the
	// sequence ends — a re-click mid-charge can't start a second beam (no double fire). CachedChargeEndTime lets a
	// tick that lands on the payoff timestamp bow out (see DoChargeTick).
	CachedChargeEndTime = World->GetTimeSeconds() + ChargeTime;
	if (CachedTickDamage > 0.0f)
	{
		World->GetTimerManager().SetTimer(TickTimerHandle, this, &UFPSRGA_WeaponFire_ChargeLaser::DoChargeTick, TickInterval, true);
	}
	World->GetTimerManager().SetTimer(FinalTimerHandle, this, &UFPSRGA_WeaponFire_ChargeLaser::DoFinalShot, ChargeTime, false);
}

void UFPSRGA_WeaponFire_ChargeLaser::DoChargeTick()
{
	// Skip a warm-up tick that lands at/after the payoff timestamp: when ChargeTime is an exact multiple of
	// ChargeTickInterval the loop tick and DoFinalShot expire on the same frame, and if the tick runs first it would
	// double the payoff with an extra chip beam. The payoff shot (DoFinalShot) covers that instant instead.
	if (UWorld* World = CachedWorld.Get())
	{
		if (World->GetTimeSeconds() >= CachedChargeEndTime - KINDA_SMALL_NUMBER)
		{
			return;
		}
	}
	// Warm-up beam: PURE fixed chip damage (no global multiplier / crit / fragments / marker).
	FireBeam(CachedTickDamage, /*bIsPayoffShot*/ false);
}

void UFPSRGA_WeaponFire_ChargeLaser::DoFinalShot()
{
	// Charge complete: stop the warm-up ticks, fire the full-power payoff beam (global multiplier + crit + marker),
	// end the ability.
	if (UWorld* World = CachedWorld.Get())
	{
		World->GetTimerManager().ClearTimer(TickTimerHandle);
	}
	FireBeam(CachedDamage, /*bIsPayoffShot*/ true);
	EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(), GetCurrentActivationInfo(), true, false);
}

void UFPSRGA_WeaponFire_ChargeLaser::FireBeam(float BeamDamage, bool bIsPayoffShot)
{
	APawn* Avatar = CachedAvatar.Get();
	AController* Controller = CachedController.Get();
	UWorld* World = CachedWorld.Get();
	if (!Avatar || !Controller || !World)
	{
		return;
	}

	// No damage while the run is frozen — the charge timers keep running (so timing matches a normal charge), but the
	// beam lands nothing during the pause (mirrors the other fire paths' freeze gate).
	if (const AFPSRGameState* RunState = World->GetGameState<AFPSRGameState>())
	{
		if (RunState->IsRunPaused())
		{
			return;
		}
	}

	// Global damage multiplier + crit apply to the PAYOFF shot only. Warm-up ticks are pure fixed chip damage
	// (multiplier stays 1.0, no crit) so a "+damage" card raises the final beam but never the warm-up ticks.
	float DamageMultiplier = 1.0f;
	float CritChance = 0.0f;
	float CritMultiplier = 1.0f;
	if (bIsPayoffShot)
	{
		if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
		{
			DamageMultiplier = ASC->GetNumericAttribute(UFPSRCombatSet::GetGlobalDamageMultiplierAttribute());
			CritChance = ASC->GetNumericAttribute(UFPSRCombatSet::GetGlobalCritChanceAttribute());
			CritMultiplier = ASC->GetNumericAttribute(UFPSRCombatSet::GetGlobalCritMultiplierAttribute());
		}
	}

	// Re-trace from the CURRENT view point each call so the beam tracks the player's aim throughout the charge.
	FVector ViewLocation;
	FRotator ViewRotation;
	Controller->GetPlayerViewPoint(ViewLocation, ViewRotation);
	const FVector Start = ViewLocation;
	const FVector BaseDir = (CachedSpread > 0.0f)
		? FMath::VRandCone(ViewRotation.Vector(), FMath::DegreesToRadians(CachedSpread))
		: ViewRotation.Vector();
	const FVector End = Start + BaseDir * CachedRange;

	// Hit-marker aggregated across pierced enemies — one pulse for the payoff shot, strongest outcome (Game.MD §2-14).
	bool bServerHit = false;
	bool bServerCrit = false;
	bool bServerWeak = false;
	bool bServerKill = false;

	UFPSRWeaponInstance* Instance = CachedInstance.Get();
	const TArray<TObjectPtr<UFPSRWeaponFragment>>* Fragments = Instance ? &Instance->GetActiveFragments() : nullptr;

	// Apply beam damage to one target (server-authoritative); shared across all pierced targets. The beam pierces
	// everything, so a friendly while FF is off simply resolves to 0 and the beam continues.
	auto ApplyDamageToActor = [&](AActor* HitActor, float WeakpointMult) -> void
	{
		if (!HitActor)
		{
			return;
		}
		float FinalDamage = BeamDamage * DamageMultiplier;
		bool bCrit = false;
		if (bIsPayoffShot && CritChance > 0.0f && FMath::FRand() < CritChance)
		{
			FinalDamage *= CritMultiplier;
			bCrit = true;
		}

		// Per-hit behavior hooks (e.g. bonus/leech) run on the PAYOFF shot only — warm-up ticks are pure chip damage.
		if (bIsPayoffShot && Fragments)
		{
			for (const TObjectPtr<UFPSRWeaponFragment>& Frag : *Fragments)
			{
				if (Frag) { Frag->OnHitActor(CachedFireCtx, HitActor, FinalDamage); }
			}
		}

		FinalDamage *= WeakpointMult;
		// Beam never self-damages (bAllowSelf=false); ResolveDamage applies the enemy/friendly rules.
		const float Resolved = FPSRCombat::ResolveDamage(Avatar, HitActor, FinalDamage, /*bAllowSelf*/ false, World);
		if (Resolved <= 0.0f)
		{
			return;
		}
		const FPSRCombat::FDamageResult Result = FPSRCombat::ApplyDamage(HitActor, Resolved, Avatar);
		if (Result.bWasEnemy && Result.DamageDealt > 0.0f)
		{
			bServerHit = true;
			// OnKill fires on the PAYOFF beam only — warm-up ticks skip every fragment hook (pure chip damage, §2-3-5).
			if (Result.bKilled) { bServerKill = true; if (bIsPayoffShot) { FPSRWeaponHooks::NotifyKill(CachedFireCtx, HitActor); } }
			else if (WeakpointMult > 1.0f) { bServerWeak = true; }
			else if (bCrit) { bServerCrit = true; }
		}
	};

	// Piercing beam: gather pawns (enemies + players, for friendly fire), then a single Visibility trace that ignores
	// them so the wall cutoff matches a normal hitscan shot (movable cover/doors that block Visibility included), while
	// query-only dynamics that ignore Visibility — e.g. in-flight projectile collision spheres — do NOT truncate it.
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(FPSRChargeLaser), false, Avatar);

	TArray<FHitResult> PawnHits;
	FCollisionObjectQueryParams PawnObjParams;
	FPSRCombat::AddDamageablePawnObjectTypes(PawnObjParams);
	FPSRCombat::AddWeakpointObjectType(PawnObjParams);
	World->LineTraceMultiByObjectType(PawnHits, Start, End, PawnObjParams, QueryParams);

	FCollisionQueryParams WallParams(SCENE_QUERY_STAT(FPSRChargeLaserWall), false, Avatar);
	for (const FHitResult& PawnHit : PawnHits)
	{
		if (AActor* PawnActor = PawnHit.GetActor()) { WallParams.AddIgnoredActor(PawnActor); }
	}
	FHitResult WallHit;
	const bool bWall = World->LineTraceSingleByChannel(WallHit, Start, End, ECC_Visibility, WallParams);
	const float WallDist = bWall ? WallHit.Distance : CachedRange;

#if ENABLE_DRAW_DEBUG
	// Payoff beam = bright cyan, longer-lived; warm-up tick = dim blue, brief. Gated by FPSR.Debug.WeaponDraw.
	if (const IConsoleVariable* CVarWeaponDraw = IConsoleManager::Get().FindConsoleVariable(TEXT("FPSR.Debug.WeaponDraw")))
	{
		if (CVarWeaponDraw->GetInt() > 0)
		{
			DrawDebugLine(World, Start, Start + BaseDir * FMath::Min(WallDist, CachedRange),
				bIsPayoffShot ? FColor::Cyan : FColor::Blue, false, bIsPayoffShot ? 0.5f : 0.08f, 0, bIsPayoffShot ? 2.0f : 1.0f);
		}
	}
#endif

	TArray<FPSRCombat::FResolvedHit> ResolvedHits;
	FPSRCombat::DedupePawnHitsByActor(PawnHits, ResolvedHits);
	for (const FPSRCombat::FResolvedHit& Entry : ResolvedHits)
	{
		if (Entry.Distance > WallDist)
		{
			break; // behind the wall (distance-sorted)
		}
		ApplyDamageToActor(Entry.Actor, Entry.WeakpointMultiplier);
	}

	// Hit-marker + post-fire hooks fire only on the payoff shot (warm-up ticks are silent to avoid marker/hook spam).
	if (bIsPayoffShot)
	{
		if (bServerHit)
		{
			if (AFPSRPlayerController* OwnerPC = Cast<AFPSRPlayerController>(Controller))
			{
				const EFPSRHitMarkerType MarkerType = bServerKill ? EFPSRHitMarkerType::Kill
					: (bServerWeak ? EFPSRHitMarkerType::Weak
					: (bServerCrit ? EFPSRHitMarkerType::Crit : EFPSRHitMarkerType::Hit));
				OwnerPC->ClientNotifyHitMarker(MarkerType);
			}
		}

		// OnFire / OnMiss triggers (payoff only — warm-up ticks are silent; ServerOnly ability so authority is implicit).
		FPSRWeaponHooks::NotifyFire(CachedFireCtx);
		if (!bServerHit)
		{
			FPSRWeaponHooks::NotifyMiss(CachedFireCtx);
		}

		if (Fragments)
		{
			for (const TObjectPtr<UFPSRWeaponFragment>& Frag : *Fragments)
			{
				if (Frag) { Frag->PostFire(CachedFireCtx); }
			}
		}
	}
}

void UFPSRGA_WeaponFire_ChargeLaser::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	// Clear the charge timers on every end path — a weapon-swap / death cancel (ASC cancels the ability) must not
	// leave a timer that fires DoChargeTick/DoFinalShot into a stale or destroyed avatar.
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(TickTimerHandle);
		World->GetTimerManager().ClearTimer(FinalTimerHandle);
	}
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
