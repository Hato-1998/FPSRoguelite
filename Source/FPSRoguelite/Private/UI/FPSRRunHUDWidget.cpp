// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/FPSRRunHUDWidget.h"
#include "Engine/World.h"
#include "Components/Image.h"
#include "Blueprint/UserWidget.h"
#include "Components/CanvasPanelSlot.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Weapon/FPSRWeaponFireComponent.h"
#include "Weapon/FPSRCrosshairStyleDataAsset.h"
#include "UI/FPSRCrosshairFadeCondition.h"
#include "Hero/FPSRCharacter.h"
#include "Hero/FPSRCharacterMovementComponent.h"
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

	// Center both crosshair layers in their canvas slots (idempotent; independent of the designer's slot setup).
	// The dot sits UNDER the weapon layer: the weapon style is the thing the player reads for dispersion, so it wins
	// any overlap. Sizes are per-layer on purpose — see BaseDotSizePx.
	CenterCrosshairSlot(DotCrosshairImage, 9, BaseDotSizePx);
	CenterCrosshairSlot(CrosshairImage, 10, CrosshairSizePx);

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

	if (!CrosshairImage && !DotCrosshairImage)
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
	// The base dot goes with it: aiming down sights replaces the screen-centre reference with the sight picture.
	const bool bADSVisual = OwningChar ? OwningChar->IsADSFOVActive() : FireComp->IsAiming();
	if (bADSVisual)
	{
		SetLayerVisible(DotCrosshairImage, false);
		SetLayerVisible(CrosshairImage, false);
		return;
	}

	// Base layer first — it is on for every non-ADS state, whatever the weapon is doing.
	UpdateBaseDotLayer();

	// Situational fade (reload etc.). Runs only here, i.e. only on frames where something is actually on screen.
	UpdateCrosshairFade(InDeltaTime);

	// Weapon layer. When a locomotion state takes the gun out of the player's hands, the weapon's crosshair has
	// nothing to describe and drops out — leaving the base dot alone. Reads CanFireInCurrentState() rather than
	// re-deriving the condition, so this stays the SAME judgment source as the fire gate and the holster visual — one
	// predicate, no chance of the HUD disagreeing with what the gun can actually do.
	// ⚠️ That predicate is currently always true: the wall hang was its only false case and it was removed 2026-08-31
	// (ADR 0001). This branch is therefore unreachable today and is kept as the seam, not as live behaviour.
	const UFPSRCharacterMovementComponent* Move = OwningChar ? OwningChar->GetFPSRMovement() : nullptr;
	const bool bWeaponInHand = !Move || Move->CanFireInCurrentState();

	// Per-weapon crosshair material. No style is a NORMAL state (melee / unarmed): the base dot already covers the
	// screen centre, so there is no generic fallback to substitute — the layer simply doesn't draw.
	UMaterialInterface* SourceMat = bWeaponInHand ? FireComp->GetEquippedCrosshairMaterial() : nullptr;
	if (!SourceMat)
	{
		SetLayerVisible(CrosshairImage, false);
		return;
	}
	if (!CrosshairImage)
	{
		return;
	}
	SetLayerVisible(CrosshairImage, true);

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
	const UFPSRGameUserSettings* Settings = UFPSRGameUserSettings::Get();
	if (!Settings)
	{
		return;
	}

	// FillColor + Thickness are per-style material parameters; the SDF crosshairs read them (color tints the
	// shape, thickness scales the line/arm/ring/box/dot weight). Orthogonal to the per-frame Spread update.

	// COLOUR is shared by both layers, from the one setting — that is what makes the base dot the same colour as the
	// weapon crosshair by construction, instead of a second value that could drift out of sync.
	const FLinearColor Color = Settings->GetCrosshairColor();
	for (UMaterialInstanceDynamic* DMI : { ToRawPtr(CrosshairDMI), ToRawPtr(DotCrosshairDMI) })
	{
		if (DMI)
		{
			DMI->SetVectorParameterValue(TEXT("FillColor"), Color);
		}
	}

	// THICKNESS is not pushed at all any more — it is no longer a player setting. Each style's material instance
	// (M_XH_* Thickness parameter) carries its own authored line weight, so leaving the parameter untouched is what
	// lets the designer own it. See UFPSRGameUserSettings for why it stopped being player-facing.
}

void UFPSRRunHUDWidget::CenterCrosshairSlot(UImage* Image, int32 ZOrder, float SizePx) const
{
	if (!Image || SizePx <= 0.0f)
	{
		return;
	}
	if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Image->Slot))
	{
		CanvasSlot->SetAnchors(FAnchors(0.5f, 0.5f));
		CanvasSlot->SetAlignment(FVector2D(0.5f, 0.5f));
		CanvasSlot->SetSize(FVector2D(SizePx, SizePx));
		CanvasSlot->SetPosition(FVector2D::ZeroVector);
		CanvasSlot->SetZOrder(ZOrder);
	}
}

void UFPSRRunHUDWidget::SetLayerVisible(UImage* Image, bool bVisible)
{
	if (Image)
	{
		Image->SetVisibility(bVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
}

void UFPSRRunHUDWidget::UpdateCrosshairFade(float DeltaSeconds)
{
	// Type-agnostic loop: this function never asks what kind of condition it is holding, which is what keeps a new
	// situation from becoming an edit here (see UFPSRCrosshairFadeCondition).
	AFPSRCharacter* OwningChar = ResolveOwningCharacter();
	float TargetWeapon = 1.0f;
	float TargetDot = 1.0f;
	for (const UFPSRCrosshairFadeCondition* Condition : CrosshairFadeConditions)
	{
		if (!Condition || !Condition->IsActive(OwningChar))
		{
			continue;
		}
		// Most restrictive wins, so the outcome is independent of the order the designer listed them in.
		TargetWeapon = FMath::Min(TargetWeapon, Condition->Opacity);
		if (Condition->bAffectsBaseDot)
		{
			TargetDot = FMath::Min(TargetDot, Condition->Opacity);
		}
	}

	if (CrosshairFadeInterpSpeed > 0.0f)
	{
		CurrentWeaponOpacity = FMath::FInterpTo(CurrentWeaponOpacity, TargetWeapon, DeltaSeconds, CrosshairFadeInterpSpeed);
		CurrentDotOpacity = FMath::FInterpTo(CurrentDotOpacity, TargetDot, DeltaSeconds, CrosshairFadeInterpSpeed);
	}
	else
	{
		CurrentWeaponOpacity = TargetWeapon;
		CurrentDotOpacity = TargetDot;
	}

	// Render opacity rather than a material parameter: this works on every crosshair style, including ones authored
	// later, without each material having to expose an opacity input.
	if (CrosshairImage)
	{
		CrosshairImage->SetRenderOpacity(CurrentWeaponOpacity);
	}
	if (DotCrosshairImage)
	{
		DotCrosshairImage->SetRenderOpacity(CurrentDotOpacity);
	}
}

void UFPSRRunHUDWidget::UpdateBaseDotLayer()
{
	if (!DotCrosshairImage)
	{
		return;
	}

	// Built once, not per frame. Unlike the weapon layer — whose material follows whatever is equipped and so has to be
	// re-read every tick — this style is an EditDefaultsOnly asset reference that cannot change after construction,
	// so resolving the soft pointer again each frame would be pure waste.
	if (!DotCrosshairDMI)
	{
		const UFPSRCrosshairStyleDataAsset* Style = BaseDotCrosshairStyle.LoadSynchronous();
		UMaterialInterface* SourceMat = Style ? Style->Material.LoadSynchronous() : nullptr;
		if (!SourceMat)
		{
			// Unauthored style = no dot rather than a wrong dot. Collapsed (not hidden) so it costs no layout either.
			SetLayerVisible(DotCrosshairImage, false);
			return;
		}
		DotCrosshairImage->SetBrushFromMaterial(SourceMat);
		DotCrosshairDMI = DotCrosshairImage->GetDynamicMaterial();
		ApplyCrosshairAppearance();
	}

	// Deliberately no Spread push: dispersion belongs to the weapon layer, and a dot that bloomed with it would stop
	// being the fixed screen-centre reference this layer exists to provide.
	SetLayerVisible(DotCrosshairImage, true);
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
