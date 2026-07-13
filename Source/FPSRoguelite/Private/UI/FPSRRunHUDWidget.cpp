// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/FPSRRunHUDWidget.h"
#include "Core/FPSRGameState.h"
#include "Engine/World.h"
#include "Components/Image.h"
#include "Blueprint/UserWidget.h"
#include "Components/CanvasPanelSlot.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Weapon/FPSRWeaponFireComponent.h"
#include "Hero/FPSRCharacter.h"
#include "GameFramework/Pawn.h"
#include "Settings/FPSRGameUserSettings.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Engine.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"

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

	// Subscribe for live crosshair appearance updates (color / thickness) from the settings overlay. The values
	// are pushed onto the material instance in NativeTick once it exists (and on every weapon swap).
	if (UFPSRGameUserSettings* Settings = UFPSRGameUserSettings::Get())
	{
		Settings->OnCrosshairSettingsChanged.AddDynamic(this, &UFPSRRunHUDWidget::HandleCrosshairSettingsChanged);
	}
}

void UFPSRRunHUDWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// W-U2 scope overlay. UpdateScopeOverlay runs EVERY tick while relevant (not only on the scoped edge) so a
	// mid-scope sight evolution — e.g. a stat-threshold stage that swaps the sight's ScopeOverlayWidgetClass while the
	// player stays aimed — actually switches the overlay widget instead of leaving the previous one stale. The BP
	// event (OnScopeStateChanged) still fires only on a meaningful change (scoped edge or vignette change) to avoid
	// per-frame Blueprint churn.
	AFPSRCharacter* OwningChar = ResolveOwningCharacter();
	const bool bScoped = OwningChar && OwningChar->IsScopeVisualActive();
	const bool bVignette = bScoped && OwningChar && OwningChar->IsScopeVignetteEnabled();
	if (bScoped != bLastScoped || bVignette != bLastVignette)
	{
		bLastScoped = bScoped;
		bLastVignette = bVignette;
		OnScopeStateChanged(bScoped, bVignette);
	}
	UpdateScopeOverlay(bScoped);

	if (!CrosshairImage)
	{
		return;
	}
	UFPSRWeaponFireComponent* FireComp = ResolveFireComponent();
	if (!FireComp)
	{
		return;
	}

	// Hide the crosshair while the weapon is committing to ADS (reload-aware — it reappears during a reload so the
	// screen isn't left with neither reticle nor crosshair). IsADSFOVActive tracks the FOV-zoom commit itself (not the
	// procedural-sight blend), so an ADS weapon without an AimSocket still hides the crosshair — the crosshair-hide can
	// never desync from the zoom. Iron sights / scope take over. Falls back to raw IsAiming off a character.
	const bool bADSVisual = OwningChar ? OwningChar->IsADSFOVActive() : FireComp->IsAiming();
	if (bADSVisual)
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

	// Rebuild the dynamic instance only on weapon swap (source material change), not every frame; push the
	// player's appearance (color / thickness) onto the fresh instance.
	if (SourceMat != CurrentSourceMaterial)
	{
		CurrentSourceMaterial = SourceMat;
		CrosshairImage->SetBrushFromMaterial(SourceMat);
		CrosshairDMI = CrosshairImage->GetDynamicMaterial();
		ApplyCrosshairAppearance();
	}
	if (!CrosshairDMI)
	{
		return;
	}

	// Truthful spread: dynamic weapons drive the material's Spread (UV radius) from the weapon's ACTUAL current
	// dispersion half-angle (base + bloom, x ADS), projected to screen — so the crosshair bounds where shots go.
	// Static crosshairs (e.g. the melee dot) pin Spread to 0.
	const bool bDynamic = FireComp->GetEquippedCrosshairUsesDynamic();
	float SpreadUV = 0.0f;
	if (bDynamic)
	{
		SpreadUV = ComputeSpreadUV(FireComp->GetCurrentSpreadDegrees());
		SpreadUV = FMath::Clamp(SpreadUV, MinCrosshairSpreadUV, MaxCrosshairSpreadUV);
	}
	CrosshairDMI->SetScalarParameterValue(TEXT("Spread"), SpreadUV);
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

AFPSRCharacter* UFPSRRunHUDWidget::ResolveOwningCharacter()
{
	if (CachedOwningChar.IsValid())
	{
		return CachedOwningChar.Get();
	}
	CachedOwningChar = Cast<AFPSRCharacter>(GetOwningPlayerPawn());
	return CachedOwningChar.Get();
}

void UFPSRRunHUDWidget::UpdateScopeOverlay(bool bScoped)
{
	// 원하는 오버레이 클래스: 활성 사이트가 지정한 위젯 우선, 없으면 이 HUD의 폴백(ScopeOverlayWidgetClass).
	TSubclassOf<UUserWidget> DesiredClass = nullptr;
	if (bScoped)
	{
		if (AFPSRCharacter* Char = ResolveOwningCharacter())
		{
			DesiredClass = Char->GetActiveScopeOverlayWidgetClass();
		}
		if (!DesiredClass)
		{
			DesiredClass = ScopeOverlayWidgetClass.LoadSynchronous();
		}
	}
	// 스코프 해제됐거나 원하는 클래스가 현재 인스턴스와 다르면(사이트 교체) 기존 인스턴스 파괴.
	if (ScopeOverlayInstance && (!bScoped || ScopeOverlayInstance->GetClass() != DesiredClass))
	{
		ScopeOverlayInstance->RemoveFromParent();
		ScopeOverlayInstance = nullptr;
	}
	// 필요 시 원하는 클래스로 생성.
	if (bScoped && DesiredClass && !ScopeOverlayInstance)
	{
		ScopeOverlayInstance = CreateWidget<UUserWidget>(GetOwningPlayer(), DesiredClass);
		if (ScopeOverlayInstance)
		{
			ScopeOverlayInstance->AddToViewport(5); // HUD 컨텐츠 위, 모달 UI 아래
		}
	}
	if (ScopeOverlayInstance)
	{
		ScopeOverlayInstance->SetVisibility(bScoped ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
}

bool UFPSRRunHUDWidget::IsScopeActive() const
{
	const AFPSRCharacter* Char = Cast<AFPSRCharacter>(GetOwningPlayerPawn());
	return Char && Char->IsScopeVisualActive();
}

void UFPSRRunHUDWidget::HandleCrosshairSettingsChanged()
{
	ApplyCrosshairAppearance();
}

void UFPSRRunHUDWidget::ApplyCrosshairAppearance()
{
	if (!CrosshairDMI)
	{
		return;
	}
	if (const UFPSRGameUserSettings* Settings = UFPSRGameUserSettings::Get())
	{
		// FillColor + Thickness are per-style material parameters; the SDF crosshairs read them (color tints the
		// shape, thickness scales the line/arm/ring/box/dot weight). Orthogonal to the per-frame Spread update.
		CrosshairDMI->SetVectorParameterValue(TEXT("FillColor"), Settings->GetCrosshairColor());
		CrosshairDMI->SetScalarParameterValue(TEXT("Thickness"), Settings->GetCrosshairThickness());
	}
}

float UFPSRRunHUDWidget::ComputeSpreadUV(float SpreadHalfAngleDeg) const
{
	const APlayerController* PC = GetOwningPlayer();
	if (!PC || !PC->PlayerCameraManager || !GEngine || !GEngine->GameViewport || CrosshairSizePx <= 0.0f)
	{
		return 0.0f;
	}
	const float FovDeg = PC->PlayerCameraManager->GetFOVAngle();
	if (FovDeg <= 1.0f)
	{
		return 0.0f;
	}
	FVector2D ViewportPx(0.0f, 0.0f);
	GEngine->GameViewport->GetViewportSize(ViewportPx);
	const float Dpi = UWidgetLayoutLibrary::GetViewportScale(this);
	if (ViewportPx.X <= 0.0f || Dpi <= 0.0f)
	{
		return 0.0f;
	}
	// Spread as a fraction of the viewport half-width (angular, so distance-independent), then rescaled from the
	// viewport into the crosshair image's own [-1,1] space. DPI cancels because both the viewport width and the
	// image size are taken in logical (DPI-independent) units.
	const float AngleRad = FMath::DegreesToRadians(FMath::Clamp(SpreadHalfAngleDeg, 0.0f, 60.0f));
	const float HalfFovRad = FMath::DegreesToRadians(FovDeg * 0.5f);
	const float FracHalfWidth = FMath::Tan(AngleRad) / FMath::Max(FMath::Tan(HalfFovRad), 1e-4f);
	const float ViewportWidthLogical = ViewportPx.X / Dpi;
	return FracHalfWidth * ViewportWidthLogical / CrosshairSizePx;
}

void UFPSRRunHUDWidget::NativeDestruct()
{
	if (AFPSRGameState* GS = GetWorld() ? GetWorld()->GetGameState<AFPSRGameState>() : nullptr)
	{
		GS->OnRunStateChanged.RemoveDynamic(this, &UFPSRRunHUDWidget::HandleRunStateChanged);
	}

	if (UFPSRGameUserSettings* Settings = UFPSRGameUserSettings::Get())
	{
		Settings->OnCrosshairSettingsChanged.RemoveDynamic(this, &UFPSRRunHUDWidget::HandleCrosshairSettingsChanged);
	}

	if (ScopeOverlayInstance)
	{
		ScopeOverlayInstance->RemoveFromParent();
		ScopeOverlayInstance = nullptr;
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
