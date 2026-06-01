// Copyright Epic Games, Inc. All Rights Reserved.

#include "Weapon/FPSRWeaponFireComponent.h"
#include "Weapon/FPSRWeaponInventoryComponent.h"
#include "Weapon/FPSRWeaponDataAsset.h"
#include "Weapon/FPSRWeaponTypes.h"

#include "AbilitySystemComponent.h"
#include "Engine/Engine.h"
#include "AbilitySystemGlobals.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"

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

void UFPSRWeaponFireComponent::StartFiring()
{
	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (!OwnerPawn || !OwnerPawn->IsLocallyControlled())
	{
		return;
	}

	UFPSRWeaponInventoryComponent* Inventory = GetInventory();
	UFPSRWeaponDataAsset* Weapon = Inventory ? Inventory->GetCurrentWeapon() : nullptr;
	if (!Weapon)
	{
		return;
	}

	bWantsToFire = true;
	TimeSinceLastShot = 0.0f;
	ShotsFiredThisSpray = 0;

	if (Weapon->BaseStats.FireMode == EFPSRFireMode::Burst)
	{
		BurstShotsRemaining = FMath::Max(1, Weapon->BaseStats.BurstCount);
	}

	// Immediate first shot on press.
	FireOneShot();
	if (Weapon->BaseStats.FireMode == EFPSRFireMode::Burst && BurstShotsRemaining > 0)
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
	UFPSRWeaponDataAsset* Weapon = Inventory ? Inventory->GetCurrentWeapon() : nullptr;
	if (!Weapon)
	{
		return;
	}

	// Block firing on empty magazine or during reload (no auto-reload; player presses R).
	if (Inventory->IsReloading() || Inventory->GetCurrentAmmo() <= 0)
	{
		return;
	}

	const FFPSRWeaponStatBlock& Stats = Weapon->BaseStats;

	// Activate the weapon's fire ability (trace + damage; predicted + server-authoritative).
	if (Weapon->FireAbility)
	{
		if (UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(OwnerPawn))
		{
			ASC->TryActivateAbilityByClass(Weapon->FireAbility);
		}
	}

	// Camera recoil (local feel only). Vertical is applied smoothly in Tick via PendingRisePitch;
	// horizontal follows a deterministic pattern + small random variance (Apex-style, not pure jitter).
	const FVector2D ShotDelta = ComputeShotRecoilDelta(Stats, ShotsFiredThisSpray);
	if (ShotDelta.Y != 0.0f)
	{
		PendingRisePitch += ShotDelta.Y;
	}
	if (Stats.RecoilHorizontal != 0.0f)
	{
		const float Variance = FMath::FRandRange(-1.0f, 1.0f) * Stats.RecoilHorizontal * Stats.RecoilHorizontalVariance;
		OwnerPawn->AddControllerYawInput(ShotDelta.X + Variance);
	}
	++ShotsFiredThisSpray;

	// Bloom grows with each shot.
	CurrentBloom = FMath::Min(CurrentBloom + Stats.BloomPerShot, Stats.MaxBloom);
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
	UFPSRWeaponDataAsset* Weapon = Inventory ? Inventory->GetCurrentWeapon() : nullptr;
	if (!Weapon)
	{
		return;
	}

	const FFPSRWeaponStatBlock& Stats = Weapon->BaseStats;
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

	// --- Recoil pitch handling (smoothed rise + debt-aware recovery + player compensation) ---
	// 1) Smoothly apply any pending up-kick (snappy rise), accumulating recovery debt.
	if (PendingRisePitch > 0.0f)
	{
		const float Apply = FMath::Min(Stats.RecoilRiseRate * DeltaTime, PendingRisePitch);
		OwnerPawn->AddControllerPitchInput(-Apply); // negative = up
		PendingRisePitch -= Apply;
		RecoilDebtPitch += Apply;
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
	// Debug scaffolding (replaced by HUD in P3): show ammo for the local player.
	if (GEngine)
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
		UFPSRWeaponDataAsset* Weapon = Inv ? Inv->GetCurrentWeapon() : nullptr;
		if (!Weapon)
		{
			return;
		}

		const FFPSRWeaponStatBlock& Stats = Weapon->BaseStats;

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
			CumPitch += Delta.Y;
		}
	}));
