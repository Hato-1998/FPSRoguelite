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

	/** WEAPON layer of the crosshair — the equipped weapon's own style, drawn ON TOP of the base dot below. Only
	 *  visible when the weapon actually defines a style: "no style" is a normal state (melee / unarmed), not a case
	 *  to substitute a generic crosshair for. Bind a UImage named "CrosshairImage" in the WBP (optional). */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> CrosshairImage;

	/** BASE layer — a centre dot that is always present regardless of weapon or movement state. The weapon layer
	 *  above ADDS to it rather than replacing it, so a melee weapon shows the dot alone simply because it carries no
	 *  style of its own, and a future melee weapon WITH a style would show dot + style together.
	 *  Bind a UImage named "DotCrosshairImage" in the WBP (optional — HUD still works without it). */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> DotCrosshairImage;

	/** Style used for the always-on base dot (set to DA_XH_Dot). Data, not a literal: the path stays out of C++ so a
	 *  restyle is an asset swap. Its material must expose FillColor/Thickness or the player's colour setting cannot
	 *  reach it — M_XH_Dot does, the old generic M_DynamicCrosshair does NOT (which is why that fallback is gone). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Crosshair")
	TSoftObjectPtr<class UFPSRCrosshairStyleDataAsset> BaseDotCrosshairStyle;

	/** On-screen size (logical px) of the square WEAPON crosshair image. This is the projection reference: the truthful
	 *  spread cone is mapped into this box, so it must be large enough to fit the widest dispersion without
	 *  clipping (the image is mostly transparent — only the thin SDF shapes draw). NOT a user size setting. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Crosshair")
	float CrosshairSizePx = 600.0f;

	/** On-screen size (logical px) of the square BASE DOT image — deliberately its own value, not CrosshairSizePx.
	 *  That one is sized to fit the widest dispersion cone, and the dot has no dispersion to fit: sharing the box made
	 *  the dot render at spread-projection scale, which is far too big. The dot's material draws its shape as a
	 *  fraction of this box, so this is what actually sets the dot's apparent size.
	 *
	 *  Design-time data, NOT a player setting — it stays off UFPSRGameUserSettings so it can never reach the settings
	 *  menu (colour and thickness are the player's; the dot's footprint is the designer's). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Crosshair", meta = (ClampMin = "1.0", UIMin = "8.0", UIMax = "256.0"))
	float BaseDotSizePx = 64.0f;

	/** Situations that dim the crosshair (reload, and anything else added later). Authored inline; each entry carries
	 *  its own opacity. Empty = never fades, which is the old behaviour. Lowest opacity among the active entries wins,
	 *  so the result never depends on the order they are listed in. See UFPSRCrosshairFadeCondition — a new situation
	 *  is a subclass or a Blueprint child, never an edit to this widget. */
	UPROPERTY(EditDefaultsOnly, Instanced, Category = "FPSR|Crosshair")
	TArray<TObjectPtr<class UFPSRCrosshairFadeCondition>> CrosshairFadeConditions;

	/** How fast the crosshair eases toward the target opacity (per second). A hard cut on a reload reads as a glitch,
	 *  so this is interpolated rather than snapped. 0 = snap instantly. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Crosshair", meta = (ClampMin = "0.0"))
	float CrosshairFadeInterpSpeed = 8.0f;

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

	/** Push the persisted crosshair color + thickness into BOTH dynamic material instances (weapon layer + base dot),
	 *  which is what keeps the dot's colour identical to the weapon crosshair's without a second setting. */
	void ApplyCrosshairAppearance();

	/** Force one crosshair layer's canvas slot to screen centre at SizePx, independent of the designer's slot setup.
	 *  Each layer passes its OWN size — the weapon layer needs a box big enough for the projected spread cone, the dot
	 *  only needs to be a dot — while the shared centre anchoring keeps the two aligned. */
	void CenterCrosshairSlot(UImage* Image, int32 ZOrder, float SizePx) const;

	/** Rebuild (only on style change) and show the always-on base dot. Spread is never pushed here — dispersion is a
	 *  weapon-layer concept and the dot has to stay a fixed reference. */
	void UpdateBaseDotLayer();

	/** Shared visibility setter: HitTestInvisible when shown, Collapsed when not. Null-safe (the WBP may bind neither
	 *  image). */
	static void SetLayerVisible(UImage* Image, bool bVisible);

	/** Evaluate CrosshairFadeConditions, ease both layers toward the resulting opacity, and apply it. Called once per
	 *  visible frame; skipped entirely while the crosshair is collapsed (ADS) since nothing is on screen to fade. */
	void UpdateCrosshairFade(float DeltaSeconds);

	/** Current eased opacity per layer, so the fade survives across frames (the target is recomputed every frame but
	 *  the value has to carry). Start fully opaque. */
	float CurrentWeaponOpacity = 1.0f;
	float CurrentDotOpacity = 1.0f;

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

	/** Base dot's dynamic instance. No paired "current source" field like the weapon layer above: the dot's style is
	 *  an EditDefaultsOnly asset reference, so it is resolved and built exactly once and this pointer's existence IS
	 *  the "already built" flag. */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DotCrosshairDMI;
};
