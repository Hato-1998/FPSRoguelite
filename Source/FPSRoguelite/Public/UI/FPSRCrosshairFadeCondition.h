// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "GameplayTagContainer.h"
#include "FPSRCrosshairFadeCondition.generated.h"

class AFPSRCharacter;

/**
 * One "fade the crosshair while X is true" rule, authored inline on the HUD widget
 * (UFPSRRunHUDWidget::CrosshairFadeConditions).
 *
 * Same shape — and the same reason — as UFPSRCardEffect: the set of situations that should dim the crosshair only
 * grows (reload, planting, interacting, downed...), so the TRIGGER must not live in the HUD's tick as a chain of
 * ifs. A NEW situation = one subclass (~15 lines) or a Blueprint child, zero central edits (OCP / extensibility
 * directive). The HUD loops these condition-type-agnostically and never asks what kind each one is.
 *
 * Opacity lives on the condition rather than in a wrapper struct on purpose: this project keeps EditInlineNew
 * Instanced polymorphism as a TArray directly on a UObject and avoids nesting it inside a USTRUCT (that authoring
 * path is unverified here), so the row IS the object.
 *
 * Owner-local cosmetic only — evaluated on the local player's HUD, never replicated, never feeds a gameplay decision.
 */
UCLASS(Abstract, EditInlineNew, DefaultToInstanced, CollapseCategories, Blueprintable)
class FPSROGUELITE_API UFPSRCrosshairFadeCondition : public UObject
{
	GENERATED_BODY()

public:
	/** Crosshair opacity while this condition holds. 1 = unchanged, 0 = invisible. When several conditions are active
	 *  at once the LOWEST wins, so adding a rule can only ever make the crosshair fainter — the result never depends
	 *  on array order. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crosshair Fade", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Opacity = 0.5f;

	/** Whether the always-on base dot fades too, or only the weapon crosshair above it. False keeps the dot solid as a
	 *  fixed aim reference while the weapon layer dims. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crosshair Fade")
	bool bAffectsBaseDot = true;

	/** True while this rule should apply. Character may be null (no local pawn) — return false rather than assuming.
	 *  BlueprintNativeEvent so a designer can author a new condition entirely in Blueprint. */
	UFUNCTION(BlueprintNativeEvent, BlueprintPure, Category = "Crosshair Fade")
	bool IsActive(const AFPSRCharacter* Character) const;
	virtual bool IsActive_Implementation(const AFPSRCharacter* Character) const { return false; }
};

/** Fades while a reload is in flight. Reads the character's replicated weapon state (AFPSRCharacter::IsReloading),
 *  not a montage or an anim flag, so it is true for the whole reload regardless of what is playing. */
UCLASS(meta = (DisplayName = "Reloading"))
class FPSROGUELITE_API UCrosshairFade_Reloading : public UFPSRCrosshairFadeCondition
{
	GENERATED_BODY()

public:
	virtual bool IsActive_Implementation(const AFPSRCharacter* Character) const override;
};

/** Fades while the player's ability system owns Tag. This is the open hook: any system that already tags the player
 *  (abilities, effects, states) can dim the crosshair with no C++ change at all — author a row, pick the tag.
 *
 *  ⚠ The tag has to actually be ON the ASC while the situation lasts (an ability's activation-owned tag, a loose tag,
 *  or a GE-granted tag). A tag that is merely declared in DefaultGameplayTags.ini is never "held" by anyone, so the
 *  row would silently never fire — check with `showdebug abilitysystem` if a rule looks dead. */
UCLASS(meta = (DisplayName = "Gameplay Tag"))
class FPSROGUELITE_API UCrosshairFade_GameplayTag : public UFPSRCrosshairFadeCondition
{
	GENERATED_BODY()

public:
	/** Tag the local player's ASC must hold for this rule to apply. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crosshair Fade")
	FGameplayTag Tag;

	virtual bool IsActive_Implementation(const AFPSRCharacter* Character) const override;
};
