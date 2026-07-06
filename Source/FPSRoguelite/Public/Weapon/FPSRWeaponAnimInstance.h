// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "FPSRWeaponAnimInstance.generated.h"

/**
 * Owner-local 1P weapon-mesh AnimInstance base. Turns a per-shot anim curve (played by the weapon's fire montage on
 * WeaponMesh1P) into a data-driven "fire part" recoil offset that the weapon AnimBP's ModifyBone consumes — so the
 * bolt / charging-handle kickback is authored once per weapon in the DataAsset (curve x distance x axis) instead of
 * being hardcoded into each AnimBP. Purely cosmetic; no replication (the fire montage that drives the curve already
 * plays owner-locally and on spectators via the fire-cosmetics path).
 *
 * Wiring: AFPSRCharacter::RefreshFirstPersonWeaponVisual injects the DataAsset params on equip (SetFirePartRecoilParams).
 * The weapon AnimBP (reparented to this class) binds a ModifyBone Translation pin to FirePartRecoilOffset. Distance 0 /
 * no curve = zero offset = no-op for weapons with no moving fire part.
 */
UCLASS()
class FPSROGUELITE_API UFPSRWeaponAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	/** Per-shot recoil translation (weapon component space) for the fire part (bolt / charging handle). Recomputed each
	 *  frame from the injected curve x distance x axis; bind a ModifyBone Translation pin to this in the weapon AnimBP. */
	UPROPERTY(BlueprintReadOnly, Category = "FPSR|Weapon")
	FVector FirePartRecoilOffset = FVector::ZeroVector;

	/** Inject the equipped weapon's fire-part recoil params (owner/spectator client, on equip). Curve None or Distance 0
	 *  disables the effect (offset stays zero). Axis is the component-space direction the part travels at the curve peak. */
	UFUNCTION(BlueprintCallable, Category = "FPSR|Weapon")
	void SetFirePartRecoilParams(FName InCurveName, float InDistanceCm, FVector InAxis);

protected:
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

private:
	/** Injected params (RefreshFirstPersonWeaponVisual). CurveName None or DistanceCm ~0 = disabled. */
	FName FirePartRecoilCurveName = NAME_None;
	float FirePartRecoilDistanceCm = 0.0f;
	FVector FirePartRecoilAxis = FVector::ZeroVector;
};
