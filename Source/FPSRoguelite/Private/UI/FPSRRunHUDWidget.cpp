// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/FPSRRunHUDWidget.h"
#include "Core/FPSRGameState.h"
#include "Engine/World.h"
#include "Components/Image.h"
#include "Components/CanvasPanelSlot.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Weapon/FPSRWeaponFireComponent.h"
#include "GameFramework/Pawn.h"
#include "Settings/FPSRGameUserSettings.h"

void UFPSRRunHUDWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (AFPSRGameState* GS = GetWorld() ? GetWorld()->GetGameState<AFPSRGameState>() : nullptr)
	{
		GS->OnRunStateChanged.AddDynamic(this, &UFPSRRunHUDWidget::HandleRunStateChanged);
	}

	OnRunStateUpdated();

	// Center the crosshair image in its canvas slot (idempotent; independent of the designer's slot setup).
	if (CrosshairImage)
	{
		if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(CrosshairImage->Slot))
		{
			CanvasSlot->SetAnchors(FAnchors(0.5f, 0.5f));
			CanvasSlot->SetAlignment(FVector2D(0.5f, 0.5f));
			CanvasSlot->SetSize(FVector2D(CrosshairSizePx, CrosshairSizePx));
			CanvasSlot->SetPosition(FVector2D::ZeroVector);
			CanvasSlot->SetZOrder(10);
		}
	}

	// Apply the persisted crosshair size and subscribe for live updates from the settings overlay.
	if (UFPSRGameUserSettings* Settings = UFPSRGameUserSettings::Get())
	{
		ApplyCrosshairScale(Settings->GetCrosshairScale());
		Settings->OnCrosshairSettingsChanged.AddDynamic(this, &UFPSRRunHUDWidget::HandleCrosshairScaleChanged);
	}
}

void UFPSRRunHUDWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (!CrosshairImage)
	{
		return;
	}
	UFPSRWeaponFireComponent* FireComp = ResolveFireComponent();
	if (!FireComp)
	{
		return;
	}

	// Hide the crosshair while aiming down sights — the iron sights / scope take over.
	if (FireComp->IsAiming())
	{
		CrosshairImage->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}
	CrosshairImage->SetVisibility(ESlateVisibility::HitTestInvisible);

	// Per-weapon crosshair material instance, or the HUD default fallback.
	UMaterialInterface* SourceMat = FireComp->GetEquippedCrosshairMaterial();
	if (!SourceMat)
	{
		SourceMat = DefaultCrosshairMaterial.LoadSynchronous();
	}
	if (!SourceMat)
	{
		return;
	}

	// Rebuild the dynamic instance only on weapon swap (source material change), not every frame.
	if (SourceMat != CurrentSourceMaterial)
	{
		CurrentSourceMaterial = SourceMat;
		CrosshairImage->SetBrushFromMaterial(SourceMat);
		CrosshairDMI = CrosshairImage->GetDynamicMaterial();
	}
	if (!CrosshairDMI)
	{
		return;
	}

	// Static weapons pin Spread to 0; dynamic weapons open by current bloom (tight at rest, widens on fire,
	// recovers when not firing). Clamped so the crosshair never over-spreads past the texture edge.
	const bool bDynamic = FireComp->GetEquippedCrosshairUsesDynamic();
	const float Spread = bDynamic ? FMath::Min(FireComp->GetCurrentBloom() * SpreadToPush, MaxCrosshairSpread) : 0.0f;
	CrosshairDMI->SetScalarParameterValue(TEXT("Spread"), Spread);
}

UFPSRWeaponFireComponent* UFPSRRunHUDWidget::ResolveFireComponent()
{
	if (CachedFireComp.IsValid())
	{
		return CachedFireComp.Get();
	}
	if (APawn* OwningPawn = GetOwningPlayerPawn())
	{
		CachedFireComp = OwningPawn->FindComponentByClass<UFPSRWeaponFireComponent>();
	}
	return CachedFireComp.Get();
}

void UFPSRRunHUDWidget::HandleCrosshairScaleChanged(float NewScale)
{
	ApplyCrosshairScale(NewScale);
}

void UFPSRRunHUDWidget::ApplyCrosshairScale(float Scale)
{
	if (CrosshairImage)
	{
		// Uniform render-transform scale about the default center pivot (0.5,0.5) — orthogonal to the
		// canvas-slot sizing and the per-frame Spread material update.
		CrosshairImage->SetRenderScale(FVector2D(Scale, Scale));
	}
}

void UFPSRRunHUDWidget::NativeDestruct()
{
	if (AFPSRGameState* GS = GetWorld() ? GetWorld()->GetGameState<AFPSRGameState>() : nullptr)
	{
		GS->OnRunStateChanged.RemoveDynamic(this, &UFPSRRunHUDWidget::HandleRunStateChanged);
	}

	if (UFPSRGameUserSettings* Settings = UFPSRGameUserSettings::Get())
	{
		Settings->OnCrosshairSettingsChanged.RemoveDynamic(this, &UFPSRRunHUDWidget::HandleCrosshairScaleChanged);
	}

	Super::NativeDestruct();
}

void UFPSRRunHUDWidget::HandleRunStateChanged()
{
	OnRunStateUpdated();
}

float UFPSRRunHUDWidget::GetRunClockSeconds() const
{
	const AFPSRGameState* GS = GetWorld() ? GetWorld()->GetGameState<AFPSRGameState>() : nullptr;
	return GS ? GS->GetRunClockSeconds() : 0.0f;
}

FText UFPSRRunHUDWidget::GetRunClockText() const
{
	const int32 Total = FMath::Max(0, FMath::FloorToInt(GetRunClockSeconds()));
	const int32 Mins = Total / 60;
	const int32 Secs = Total % 60;
	return FText::FromString(FString::Printf(TEXT("%02d:%02d"), Mins, Secs));
}

ERunPhase UFPSRRunHUDWidget::GetRunPhase() const
{
	const AFPSRGameState* GS = GetWorld() ? GetWorld()->GetGameState<AFPSRGameState>() : nullptr;
	return GS ? GS->GetRunPhase() : ERunPhase::Combat;
}

bool UFPSRRunHUDWidget::IsRunPaused() const
{
	const AFPSRGameState* GS = GetWorld() ? GetWorld()->GetGameState<AFPSRGameState>() : nullptr;
	return GS ? GS->IsRunPaused() : false;
}

int32 UFPSRRunHUDWidget::GetPartyLevel() const
{
	const AFPSRGameState* GS = GetWorld() ? GetWorld()->GetGameState<AFPSRGameState>() : nullptr;
	return GS ? GS->GetPartyLevel() : 0;
}

int32 UFPSRRunHUDWidget::GetSharedXP() const
{
	const AFPSRGameState* GS = GetWorld() ? GetWorld()->GetGameState<AFPSRGameState>() : nullptr;
	return GS ? GS->GetSharedXP() : 0;
}

int32 UFPSRRunHUDWidget::GetRequiredXPForNextLevel() const
{
	const AFPSRGameState* GS = GetWorld() ? GetWorld()->GetGameState<AFPSRGameState>() : nullptr;
	return GS ? GS->GetRequiredXPForNextLevel() : 0;
}

float UFPSRRunHUDWidget::GetXPProgress01() const
{
	const int32 Req = GetRequiredXPForNextLevel();
	return Req > 0 ? FMath::Clamp(static_cast<float>(GetSharedXP()) / static_cast<float>(Req), 0.0f, 1.0f) : 0.0f;
}
