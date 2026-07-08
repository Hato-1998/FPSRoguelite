// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/CRRecoilSpreadComponent.h"
#include "FPSRRecoilComponent.generated.h"

/**
 * Project recoil component (P1, CrystalRecoil adapter). Subclasses the plugin's pattern + heat-spread component so the
 * plugin owns the recoil KINEMATICS (uplift / recovery / player-input compensation) and the heat-spread model, while we
 * inject our run-state GATING through the ProcessDelta* hooks — the plugin's tick must not drift the camera during the
 * card-selection freeze (Game.MD §2-2) or while the owner is downed/dead (DBNO, U9).
 *
 * The owning UFPSRWeaponFireComponent drives it (owner-local): SetRecoilPattern on equip, StartShooting on trigger
 * press, ApplyShot per fired round, SetRecoilStrength for ADS scale + recoil-down cards. The plugin applies recoil to
 * the CONTROLLER's control rotation (ApplyInputToController → SetControlRotation), so the server's camera-viewpoint
 * fire trace stays consistent with the on-screen recoil — same authority model as the legacy procedural recoil.
 *
 * ChargeLaser keeps its own bespoke charge-ramp recoil in the fire component (no ApplyShot), so this component stays
 * idle for it. Melee has no recoil pattern.
 */
UCLASS(ClassGroup = (FPSR), meta = (BlueprintSpawnableComponent))
class FPSROGUELITE_API UFPSRRecoilComponent : public UCRRecoilSpreadComponent
{
	GENERATED_BODY()

public:
	/** The plugin's spread subclass narrows ApplyShot() to protected; re-expose it as public so the owning fire
	 *  component can drive each fired round. Virtual dispatch still runs the spread override (recoil uplift + heat). */
	using UCRRecoilSpreadComponent::ApplyShot;

protected:
	/** Skip applying the uplift delta while recoil is suppressed (freeze / downed). Base default returns true. */
	virtual bool ProcessDeltaRecoilRotation(FRotator& DeltaRecoilRotation) override;

	/** Skip applying the recovery delta while recoil is suppressed — otherwise auto-recovery would slowly pull the
	 *  view during the card-selection freeze or while downed. */
	virtual bool ProcessDeltaRecoveryRotation(FRotator& DeltaRecoveryRotation) override;

	/** True when recoil + recovery must be suppressed this frame: the global run is frozen for card selection, or the
	 *  owning player is not alive (DBNO / dead). Mirrors the fire component's own fire gates so the two never disagree. */
	bool IsRecoilSuppressed() const;
};
