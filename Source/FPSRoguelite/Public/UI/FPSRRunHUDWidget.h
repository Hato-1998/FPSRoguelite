// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CommonUserWidget.h"
#include "Core/FPSRGameState.h"
#include "FPSRRunHUDWidget.generated.h"

class UImage;
class UFPSRWeaponFireComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class AFPSRCharacter;

/** Passive run-state HUD base (Game layer). Exposes replicated run state (GameState) to WBP via BlueprintPure
 *  getters and fires OnRunStateUpdated whenever it changes. Event-driven: binds GameState OnRunStateChanged,
 *  no polling. Read-only mirror — input routing stays with the Game-layer widget. (Game.MD §2-2/§2-14).
 *  Pending card picks are NOT surfaced here: level-up immediately opens the card modal, so a count is redundant. */
UCLASS()
class FPSROGUELITE_API UFPSRRunHUDWidget : public UCommonUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	UFUNCTION()
	void HandleRunStateChanged();

	/** WBP refresh hook: fired on construct and whenever run state changes. */
	UFUNCTION(BlueprintImplementableEvent, Category = "FPSR|HUD")
	void OnRunStateUpdated();

	/** W-U2 scope overlay hook: fired on the scoped edge. The content WBP shows/hides its full-screen vignette;
	 *  bVignette requests the edge vignette. The reticle art itself now lives in a per-site scope overlay widget
	 *  (see UpdateScopeOverlay) rather than a texture passed here. */
	UFUNCTION(BlueprintImplementableEvent, Category = "FPSR|HUD")
	void OnScopeStateChanged(bool bScoped, bool bVignette);

	/** BlueprintPure mirror of the owner's scope-active state (for WBP visibility bindings). (W-U2) */
	UFUNCTION(BlueprintPure, Category = "FPSR|HUD")
	bool IsScopeActive() const;

	/** Replicated survival seconds (pauses during freeze / after boss). */
	UFUNCTION(BlueprintPure, Category = "FPSR|HUD")
	float GetRunClockSeconds() const;

	/** Run clock formatted mm:ss. */
	UFUNCTION(BlueprintPure, Category = "FPSR|HUD")
	FText GetRunClockText() const;

	UFUNCTION(BlueprintPure, Category = "FPSR|HUD")
	ERunPhase GetRunPhase() const;

	UFUNCTION(BlueprintPure, Category = "FPSR|HUD")
	bool IsRunPaused() const;

	UFUNCTION(BlueprintPure, Category = "FPSR|HUD")
	int32 GetPartyLevel() const;

	UFUNCTION(BlueprintPure, Category = "FPSR|HUD")
	int32 GetSharedXP() const;

	UFUNCTION(BlueprintPure, Category = "FPSR|HUD")
	int32 GetRequiredXPForNextLevel() const;

	/** Shared XP progress toward the next party level, 0..1. */
	UFUNCTION(BlueprintPure, Category = "FPSR|HUD")
	float GetXPProgress01() const;

	// --- Dynamic crosshair (owner-client cosmetic; §2-14) ---

	/** Centered crosshair image. Bind a UImage named "CrosshairImage" in the WBP (optional — HUD still works without it). */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> CrosshairImage;

	/** Fallback crosshair material instance used when the equipped weapon defines none (set to MI_Crosshair_Default). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Crosshair")
	TSoftObjectPtr<UMaterialInterface> DefaultCrosshairMaterial;

	/** On-screen size (logical px) of the square crosshair image. This is the projection reference: the truthful
	 *  spread cone is mapped into this box, so it must be large enough to fit the widest dispersion without
	 *  clipping (the image is mostly transparent — only the thin SDF shapes draw). NOT a user size setting. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Crosshair")
	float CrosshairSizePx = 600.0f;

	/** Optional floor on the projected spread (UV radius, 0..1) so a very accurate weapon's crosshair is still
	 *  visible at rest. 0 = pure truthful (crosshair tracks the exact cone). Raise if it reads too small. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Crosshair")
	float MinCrosshairSpreadUV = 0.0f;

	/** Upper clamp on the projected spread (UV radius) so an extreme dispersion never draws past the crosshair
	 *  image edge (which would clip the ring/box). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Crosshair")
	float MaxCrosshairSpreadUV = 0.95f;

	// --- W-U2 full-screen scope overlay (owner-local) ---

	/** Fallback full-screen scope-overlay widget used when the active sight doesn't specify its own
	 *  (UFPSRWeaponDataAsset / FFPSRWeaponPartStage Scope.ScopeOverlayWidgetClass) — e.g. the Synty sniper-reticle WBP
	 *  HUD_SciFiSoldier_Reticle_SniperRifle_01. Created lazily + added to the local player's viewport, its visibility
	 *  toggled by scope state. Null = no reticle art (the scope still zooms + hides the weapon). Owner-local: this HUD
	 *  widget lives only on the local player's screen, so the overlay never reaches teammates. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Scope")
	TSoftClassPtr<UUserWidget> ScopeOverlayWidgetClass;

private:
	/** Resolve (and cache) the owning pawn's fire component. */
	UFPSRWeaponFireComponent* ResolveFireComponent();

	/** Resolve (and cache) the owning pawn as an AFPSRCharacter (for ADS/scope visual state). (W-U2) */
	AFPSRCharacter* ResolveOwningCharacter();

	/** Lazily create + viewport-add the scope overlay (once) and toggle its visibility to match bScoped. (W-U2) */
	void UpdateScopeOverlay(bool bScoped);

	/** Re-apply crosshair appearance (color / thickness) when the local player changes it in settings (live). */
	UFUNCTION()
	void HandleCrosshairSettingsChanged();

	/** Push the persisted crosshair color + thickness into the current dynamic material instance. */
	void ApplyCrosshairAppearance();

	/** Project a weapon spread half-angle (deg) to the material's Spread parameter (UV radius, 0..1) so the
	 *  crosshair truthfully bounds the actual dispersion cone (accounts for camera FOV, viewport, image size,
	 *  DPI). Returns 0 if the view context is unavailable. */
	float ComputeSpreadUV(float SpreadHalfAngleDeg) const;

	UPROPERTY(Transient)
	TWeakObjectPtr<UFPSRWeaponFireComponent> CachedFireComp;

	/** Cached owning character (for the reload-aware ADS/scope visual state). (W-U2) */
	TWeakObjectPtr<AFPSRCharacter> CachedOwningChar;

	/** Last scoped state pushed to the WBP, so OnScopeStateChanged fires only on the edge. (W-U2) */
	bool bLastScoped = false;

	/** Last vignette state pushed to the WBP, so OnScopeStateChanged also refires when the active sight's vignette
	 *  flag changes mid-scope (sight evolution). Paired with bLastScoped. */
	bool bLastVignette = false;

	/** The live scope-overlay widget instance (created lazily from ScopeOverlayWidgetClass on first scope-in). (W-U2) */
	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> ScopeOverlayInstance;

	/** Source material currently on the brush; the dynamic instance is rebuilt only when this changes (weapon swap). */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> CurrentSourceMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> CrosshairDMI;
};
