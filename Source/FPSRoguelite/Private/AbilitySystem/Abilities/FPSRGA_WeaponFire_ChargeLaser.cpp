// Copyright Epic Games, Inc. All Rights Reserved.

#include "AbilitySystem/Abilities/FPSRGA_WeaponFire_ChargeLaser.h"
#include "AbilitySystem/Attributes/FPSRCombatSet.h"
#include "Weapon/FPSRWeaponInventoryComponent.h"
#include "Weapon/FPSRWeaponInstance.h"
#include "Weapon/FPSRWeaponFragment.h"
#include "Weapon/FPSRWeaponFireComponent.h"
#include "Combat/FPSRCombatStatics.h"
#include "Enemy/FPSREnemyHealthComponent.h"
#include "Core/FPSRGameState.h"
#include "Core/FPSRPlayerController.h"
#include "Core/FPSRLogChannels.h"

#include "AbilitySystemComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"

UFPSRGA_WeaponFire_ChargeLaser::UFPSRGA_WeaponFire_ChargeLaser()
{
	// Charge release is driven by two ordered Character-channel RPCs (ServerStartChargeLaser then
	// ServerReleaseChargeLaser), not by GAS client→server activation. LocalOnly keeps the client activation
	// purely cosmetic (no auto server activation) so the server can't run the beam ahead of the charge-start
	// stamp on a different channel — the server activates this ability itself from the release RPC.
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalOnly;
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

	// No firing while the run is frozen for card selection (Game.MD §2-2). The charge is left intact (not
	// consumed) — a charge held across a freeze just caps at full alpha, identical to a normal long hold.
	if (const AFPSRGameState* RunState = World->GetGameState<AFPSRGameState>())
	{
		if (RunState->IsRunPaused())
		{
			EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
			return;
		}
	}

	// Resolve weapon stats from the equipped weapon instance (base stats × accumulated modifiers; fallback to defaults).
	float Damage = 10.0f;
	float Range = 10000.0f;
	float SpreadDegrees = 0.0f;
	float ChargeTime = 0.0f;
	float ChargeFullDamageMultiplier = 1.0f;
	UFPSRWeaponInventoryComponent* Inventory = Avatar->FindComponentByClass<UFPSRWeaponInventoryComponent>();
	UFPSRWeaponInstance* Instance = Inventory ? Inventory->GetCurrentInstance() : nullptr;
	const FFPSRWeaponStatBlock* Stats = Instance ? &Instance->GetResolvedStats() : nullptr;
	if (Stats)
	{
		Damage = Stats->Damage;
		Range = Stats->Range;
		SpreadDegrees = Stats->SpreadDegrees;
		ChargeTime = Stats->ChargeTime;
		ChargeFullDamageMultiplier = Stats->ChargeFullDamageMultiplier;
	}

	// Behavior fragments (P4-B-2): build the per-activation context. PreFire runs first; ModifyChargeTime then
	// adjusts the resolved charge duration before the charge alpha is computed.
	FFPSRFireContext FireCtx;
	FireCtx.Avatar = Avatar;
	FireCtx.Controller = Controller;
	FireCtx.World = World;
	FireCtx.Instance = Instance;
	FireCtx.ShotCount = 1;
	FireCtx.bAuthority = Avatar->HasAuthority();

	const TArray<TObjectPtr<UFPSRWeaponFragment>>* Fragments = Instance ? &Instance->GetActiveFragments() : nullptr;
	if (Fragments)
	{
		for (const TObjectPtr<UFPSRWeaponFragment>& Frag : *Fragments)
		{
			if (Frag) { Frag->PreFire(FireCtx); }
		}
		for (const TObjectPtr<UFPSRWeaponFragment>& Frag : *Fragments)
		{
			if (Frag) { Frag->ModifyChargeTime(FireCtx, ChargeTime); }
		}
	}

	// Charge alpha [0,1], measured against this machine's own clock vs the stamped charge-start time
	// (client = local feel, server = authoritative). Consume the charge afterwards so one charge fires once.
	float ChargeAlpha = 0.0f;
	if (UFPSRWeaponFireComponent* FireComp = Avatar->FindComponentByClass<UFPSRWeaponFireComponent>())
	{
		const float Start = FireComp->GetChargeStartWorldTime();
		if (Start < 0.0f)
		{
			ChargeAlpha = 0.0f; // no charge was started on this machine (e.g. a spoofed activation on the server)
		}
		else if (ChargeTime <= 0.0f)
		{
			ChargeAlpha = 1.0f;
		}
		else
		{
			ChargeAlpha = FMath::Clamp((World->GetTimeSeconds() - Start) / ChargeTime, 0.0f, 1.0f);
		}
		FireComp->ResetCharge();
	}

	// Server-authoritative gates: empty mag / reloading / fire-rate. One charged beam costs one magazine round.
	if (Avatar->HasAuthority() && Inventory)
	{
		if (Inventory->IsReloading() || Inventory->GetCurrentAmmo() <= 0)
		{
			EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
			return;
		}
		if (Stats)
		{
			const float MinInterval = 1.0f / FMath::Max(Stats->FireRate, 0.01f);
			if (!Inventory->ServerTryConsumeFireInterval(MinInterval))
			{
				EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
				return;
			}
		}
		Inventory->ConsumeAmmo(1);
	}

	// Charge scales damage from base (alpha 0) up to ChargeFullDamageMultiplier (alpha 1); the global damage
	// multiplier and per-hit crit are applied inside the damage lambda.
	const float ChargeDamageScale = FMath::Lerp(1.0f, ChargeFullDamageMultiplier, ChargeAlpha);

	float DamageMultiplier = 1.0f;
	float CritChance = 0.0f;
	float CritMultiplier = 1.0f;
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		CritChance = ASC->GetNumericAttribute(UFPSRCombatSet::GetGlobalCritChanceAttribute());
		CritMultiplier = ASC->GetNumericAttribute(UFPSRCombatSet::GetGlobalCritMultiplierAttribute());
		DamageMultiplier = ASC->GetNumericAttribute(UFPSRCombatSet::GetGlobalDamageMultiplierAttribute());
	}

	// Trace from the player view point.
	FVector ViewLocation;
	FRotator ViewRotation;
	Controller->GetPlayerViewPoint(ViewLocation, ViewRotation);
	const FVector Start = ViewLocation;
	const FVector BaseDir = (SpreadDegrees > 0.0f)
		? FMath::VRandCone(ViewRotation.Vector(), FMath::DegreesToRadians(SpreadDegrees))
		: ViewRotation.Vector();
	const FVector End = Start + BaseDir * Range;

	// Hit-marker aggregated across pierced enemies — one pulse per activation, strongest outcome (Game.MD §2-14).
	bool bServerHit = false;
	bool bServerCrit = false;
	bool bServerKill = false;

	// Apply charge-scaled damage to one target (server-authoritative); shared across all pierced targets. The beam
	// pierces everything, so a friendly while FF is off simply resolves to 0 and the beam continues.
	auto ApplyDamageToActor = [&](AActor* HitActor) -> void
	{
		if (!HitActor || !FireCtx.bAuthority)
		{
			return;
		}
		float FinalDamage = Damage * ChargeDamageScale * DamageMultiplier;
		bool bCrit = false;
		if (CritChance > 0.0f && FMath::FRand() < CritChance)
		{
			FinalDamage *= CritMultiplier;
			bCrit = true;
		}

		// Per-hit behavior hooks (e.g. bonus/leech) can adjust the damage before it lands.
		if (Fragments)
		{
			for (const TObjectPtr<UFPSRWeaponFragment>& Frag : *Fragments)
			{
				if (Frag) { Frag->OnHitActor(FireCtx, HitActor, FinalDamage); }
			}
		}

		// Beam never self-damages (bAllowSelf=false); ResolveDamage applies the enemy/friendly rules.
		const float Resolved = FPSRCombat::ResolveDamage(Avatar, HitActor, FinalDamage, /*bAllowSelf*/ false, World);
		if (Resolved <= 0.0f)
		{
			return;
		}
		const FPSRCombat::FDamageResult Result = FPSRCombat::ApplyDamage(HitActor, Resolved, Avatar);
		if (Result.bWasEnemy && Result.bApplied)
		{
			bServerHit = true;
			if (Result.bKilled) { bServerKill = true; }
			else if (bCrit) { bServerCrit = true; }
		}
	};

	// Piercing beam: pass through every enemy pawn but stop at the first geometry that blocks the weapon
	// (Visibility) channel. Gather the pawns first, then run a single Visibility trace that ignores them so the
	// wall cutoff matches exactly what blocks a normal hitscan shot (movable cover/doors that block Visibility
	// included), while query-only dynamics that ignore Visibility — e.g. in-flight AFPSRProjectile collision
	// spheres — do NOT truncate the beam.
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(FPSRChargeLaser), false, Avatar);

	TArray<FHitResult> PawnHits;
	FCollisionObjectQueryParams PawnObjParams;
	FPSRCombat::AddDamageablePawnObjectTypes(PawnObjParams); // enemies + players (friendly fire through the beam)
	World->LineTraceMultiByObjectType(PawnHits, Start, End, PawnObjParams, QueryParams);

	FCollisionQueryParams WallParams(SCENE_QUERY_STAT(FPSRChargeLaserWall), false, Avatar);
	for (const FHitResult& PawnHit : PawnHits)
	{
		if (AActor* PawnActor = PawnHit.GetActor()) { WallParams.AddIgnoredActor(PawnActor); }
	}
	FHitResult WallHit;
	const bool bWall = World->LineTraceSingleByChannel(WallHit, Start, End, ECC_Visibility, WallParams);
	const float WallDist = bWall ? WallHit.Distance : Range;

#if ENABLE_DRAW_DEBUG
	DrawDebugLine(World, Start, Start + BaseDir * FMath::Min(WallDist, Range), FColor::Cyan, false, 0.5f, 0, 2.0f);
#endif

	for (const FHitResult& PawnHit : PawnHits)
	{
		if (PawnHit.Distance > WallDist)
		{
			continue; // behind the wall
		}
		ApplyDamageToActor(PawnHit.GetActor());
	}

	// Server delivers one marker per activation to the owning client — strongest outcome (Kill > Crit > Hit).
	if (FireCtx.bAuthority && bServerHit)
	{
		if (AFPSRPlayerController* OwnerPC = Cast<AFPSRPlayerController>(Controller))
		{
			const EFPSRHitMarkerType MarkerType = bServerKill ? EFPSRHitMarkerType::Kill
				: (bServerCrit ? EFPSRHitMarkerType::Crit : EFPSRHitMarkerType::Hit);
			OwnerPC->ClientNotifyHitMarker(MarkerType);
		}
	}

	// Post-fire hooks (after the beam resolves).
	if (Fragments)
	{
		for (const TObjectPtr<UFPSRWeaponFragment>& Frag : *Fragments)
		{
			if (Frag) { Frag->PostFire(FireCtx); }
		}
	}

	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}
