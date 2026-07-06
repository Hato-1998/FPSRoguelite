// Copyright Epic Games, Inc. All Rights Reserved.

#include "Weapon/FPSRWeaponAnimInstance.h"

void UFPSRWeaponAnimInstance::SetFirePartRecoilParams(FName InCurveName, float InDistanceCm, FVector InAxis)
{
	FirePartRecoilCurveName = InCurveName;
	FirePartRecoilDistanceCm = InDistanceCm;
	FirePartRecoilAxis = InAxis;
}

void UFPSRWeaponAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	// Data-driven fire-part recoil: the weapon fire montage (played on WeaponMesh1P by PlayWeaponFireCosmetics) carries
	// the recoil curve; scale the injected axis by curve x distance so the AnimBP's ModifyBone kicks the part back per
	// shot. No curve / zero distance = no-op (offset stays zero) for weapons with no moving fire part.
	if (FirePartRecoilCurveName.IsNone() || FMath::IsNearlyZero(FirePartRecoilDistanceCm))
	{
		FirePartRecoilOffset = FVector::ZeroVector;
		return;
	}

	const float CurveValue = GetCurveValue(FirePartRecoilCurveName);
	FirePartRecoilOffset = FirePartRecoilAxis * (CurveValue * FirePartRecoilDistanceCm);
}
