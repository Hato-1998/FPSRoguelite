// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CommonUserWidget.h"
#include "Core/FPSRGameState.h"
#include "FPSRRunHUDWidget.generated.h"

class UImage;
class UFPSRWeaponFireComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;

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

	/** On-screen size (px) of the square crosshair image. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Crosshair")
	float CrosshairSizePx = 96.0f;

	/** Maps the weapon's current bloom (degrees) to the material's Spread (UV-push). Bloom is 0 at rest and grows
	 *  while firing, so the crosshair is tight at rest and widens on fire (then recovers). Tune for feel. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Crosshair")
	float SpreadToPush = 0.25f;

	/** Upper clamp on the material Spread so the crosshair never over-spreads past the texture edge. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Crosshair")
	float MaxCrosshairSpread = 0.18f;

private:
	/** Resolve (and cache) the owning pawn's fire component. */
	UFPSRWeaponFireComponent* ResolveFireComponent();

	/** Re-apply the crosshair size when the local player changes it in settings (live). */
	UFUNCTION()
	void HandleCrosshairScaleChanged(float NewScale);

	/** Apply a uniform render-transform scale to the crosshair image (about the default center pivot). */
	void ApplyCrosshairScale(float Scale);

	UPROPERTY(Transient)
	TWeakObjectPtr<UFPSRWeaponFireComponent> CachedFireComp;

	/** Source material currently on the brush; the dynamic instance is rebuilt only when this changes (weapon swap). */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> CurrentSourceMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> CrosshairDMI;
};
