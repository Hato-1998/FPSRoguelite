// Copyright Epic Games, Inc. All Rights Reserved.

#include "Weapon/FPSRWeaponFireComponent.h"
#include "Weapon/FPSRWeaponInventoryComponent.h"
#include "Weapon/FPSRWeaponInstance.h"
#include "Weapon/FPSRWeaponDataAsset.h"
#include "Weapon/FPSRWeaponTypes.h"
#include "Hero/FPSRCharacter.h"

#include "AbilitySystemComponent.h"
#include "Engine/Engine.h"
#include "AbilitySystemGlobals.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Camera/CameraComponent.h"

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

	bWantsToFire = true;
	TimeSinceLastShot = 0.0f;
	ShotsFiredThisSpray = 0;

	const FFPSRWeaponStatBlock& Stats = Instance->GetResolvedStats();
	if (Stats.FireMode == EFPSRFireMode::Burst)
	{
		BurstShotsRemaining = FMath::Max(1, Stats.BurstCount);
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
	bWantsToFire = false;
	ShotsFiredThisSpray = 0;
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

	const bool bMelee = (Weapon->Archetype == EFPSRWeaponArchetype::Melee);

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

	if (bMelee)
	{
		// Melee has no camera recoil / bloom; just stamp the attack time.
		LastMeleeTime = GetWorld()->GetTimeSeconds();
	}
	else
	{
		// Camera recoil (local feel only), ADS-dependent:
		//  - Hip-fire: weak vertical climb + strong horizontal randomness (scattered, screen stays low).
		//  - ADS: strong vertical climb + low randomness so the deterministic pattern shows (learnable line).
		const bool bADS = bIsAiming && Stats.bHasADS;
		const float VScale = bADS ? Stats.ADSVerticalScale : Stats.HipVerticalScale;
		const float HRandom = bADS ? Stats.ADSHorizontalRandom : Stats.HipHorizontalRandom;

		const FVector2D ShotDelta = ComputeShotRecoilDelta(Stats, ShotsFiredThisSpray);
		if (ShotDelta.Y != 0.0f)
		{
			PendingRisePitch += ShotDelta.Y * VScale;
		}
		if (Stats.RecoilHorizontal != 0.0f)
		{
			const float Variance = FMath::FRandRange(-1.0f, 1.0f) * Stats.RecoilHorizontal * HRandom;
			PendingRiseYaw += ShotDelta.X + Variance; // smoothed in Tick (was instant) to avoid jitter
		}
		++ShotsFiredThisSpray;

		// Bloom grows with each shot.
		CurrentBloom = FMath::Min(CurrentBloom + Stats.BloomPerShot, Stats.MaxBloom);
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

	const FFPSRWeaponStatBlock& Stats = Instance->GetResolvedStats();
	const float Interval = 1.0f / FMath::Max(Stats.FireRate, 0.01f);

	const bool bAutoFiring = (bWantsToFire && Stats.FireMode == EFPSRFireMode::FullAuto && CanFire());
	const bool bBurstFiring = (Stats.FireMode == EFPSRFireMode::Burst && BurstShotsRemaining > 0 && CanFire());

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
	if (bWantsToFire && Weapon->Archetype == EFPSRWeaponArchetype::Melee)
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

	// --- Recoil pitch handling (smoothed rise + debt-aware recovery + player compensation) ---
	// 1) Smoothly apply any pending up-kick (snappy rise), accumulating recovery debt.
	if (PendingRisePitch > 0.0f)
	{
		const float Apply = FMath::Min(Stats.RecoilRiseRate * DeltaTime, PendingRisePitch);
		OwnerPawn->AddControllerPitchInput(-Apply); // negative = up
		PendingRisePitch -= Apply;
		RecoilDebtPitch += Apply;
	}

	// 1b) Smoothly apply pending horizontal recoil (signed; no debt — horizontal is not auto-recovered).
	if (PendingRiseYaw != 0.0f)
	{
		const float ApplyYaw = FMath::Sign(PendingRiseYaw) * FMath::Min(Stats.RecoilRiseRate * DeltaTime, FMath::Abs(PendingRiseYaw));
		OwnerPawn->AddControllerYawInput(ApplyYaw);
		PendingRiseYaw -= ApplyYaw;
	}

	// 2) Player's manual downward compensation pays down the debt (it already moved the camera in
	//    Input_Look) so auto-recovery does not stack on top of it and overshoot below the aim point.
	if (PlayerPitchCompensation > 0.0f && RecoilDebtPitch > 0.0f)
	{
		const float Consumed = FMath::Min(PlayerPitchCompensation, RecoilDebtPitch);
		RecoilDebtPitch -= Consumed;
	}
	PlayerPitchCompensation = 0.0f;

	// 3) Auto-recover the remaining (un-compensated) debt downward when not firing.
	//    Gated per weapon: Always = on, Never = off, Auto = on only for single-shot weapons
	//    (snipers/railguns). Rapid-fire (FullAuto/Burst) does NOT auto-recover — the player pulls
	//    the view back down manually, which feels right for sustained sprays.
	const bool bAutoRecover =
		(Stats.RecoilRecovery == ERecoilRecovery::Always) ||
		(Stats.RecoilRecovery == ERecoilRecovery::Auto && Stats.FireMode == EFPSRFireMode::Single);
	if (bAutoRecover && !bWantsToFire && RecoilDebtPitch > 0.0f)
	{
		const float Recover = FMath::Min(Stats.RecoilRecoveryRate * DeltaTime, RecoilDebtPitch);
		OwnerPawn->AddControllerPitchInput(Recover); // positive = down
		RecoilDebtPitch -= Recover;
	}

	// Bloom recovery.
	if (CurrentBloom > 0.0f)
	{
		CurrentBloom = FMath::Max(0.0f, CurrentBloom - Stats.BloomRecoveryRate * DeltaTime);
	}

#if ENABLE_DRAW_DEBUG
	// Debug scaffolding (replaced by HUD in P3): show ammo for the local player (ammo weapons only).
	if (GEngine && Weapon->Archetype != EFPSRWeaponArchetype::Melee && Stats.MagSize > 0)
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

		const FFPSRWeaponStatBlock& Stats = Instance->GetResolvedStats();

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

			const FVector2D Delta = UFPSRWeaponFireComponent::ComputeShotRecoilDelta(Stats, i);
			CumYaw += Delta.X;
			CumPitch += Delta.Y * Stats.ADSVerticalScale;
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
