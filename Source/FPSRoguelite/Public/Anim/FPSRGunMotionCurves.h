// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Curve-channel names for the gun-motion studio runtime contract (GunMotionTool_Spec.md §2-1). Single source of
 * truth for every FPGM_* literal so the runtime consumers (UFPSRFirstPersonArmsAnimInstance, AFPSRCharacter) never
 * let a name drift out of sync between them (§7 "커브명 리터럴 금지").
 *
 * Contract: unit cm/deg. A curve's ABSENCE from the currently-blended pose means "no offset" (0) for every channel
 * here — unlike the older LeftHandIKWeightCurve/RightHandIKWeightCurve convention (absence = 1), because these are
 * additive offset channels and 0 is the neutral element for addition. Each caller reads via
 * UAnimInstance::GetCurveValue and treats a false return as 0, never a hard failure.
 *
 * There is deliberately NO whole-gun channel here (§0/§1: "총 전체 = hand_r 애디티브 본 트랙(런타임 0)" — the whole-gun
 * motion is baked into a bone track by the studio, not read from a curve at play time).
 */
namespace FPSRGunMotionCurves
{
	// --- Left-hand IK target offset + attach-part rebase blend (§2-1 row 1-2). GUN space already — same frame as
	// AFPSRCharacter::GetLeftHandGripInGunFrame / GetWeaponPartFrameInGunSpace (ik_hand_gun bone-parent space), so no
	// space conversion is needed when composing onto the cached grip.
	inline const FName HL_TX(TEXT("FPGM_HL_TX"));
	inline const FName HL_TY(TEXT("FPGM_HL_TY"));
	inline const FName HL_TZ(TEXT("FPGM_HL_TZ"));
	inline const FName HL_RP(TEXT("FPGM_HL_RP"));
	inline const FName HL_RY(TEXT("FPGM_HL_RY"));
	inline const FName HL_RR(TEXT("FPGM_HL_RR"));
	/** 0 = grip ⊕ offset, 1 = attach-part frame ⊕ offset (§2-1). Clamped 0..1 by the reader. */
	inline const FName HL_Blend(TEXT("FPGM_HL_Blend"));

	// --- Right-hand mirror of the block above.
	inline const FName HR_TX(TEXT("FPGM_HR_TX"));
	inline const FName HR_TY(TEXT("FPGM_HR_TY"));
	inline const FName HR_TZ(TEXT("FPGM_HR_TZ"));
	inline const FName HR_RP(TEXT("FPGM_HR_RP"));
	inline const FName HR_RY(TEXT("FPGM_HR_RY"));
	inline const FName HR_RR(TEXT("FPGM_HR_RR"));
	inline const FName HR_Blend(TEXT("FPGM_HR_Blend"));

	/** Prefix shared by every per-part curve, ahead of the part's stable attach-socket id and the axis suffix
	 *  (FPGM_P_<AttachSocket>_TX etc — §2-1 row 3). Exposed so any future writer builds the same names without
	 *  duplicating the literal. */
	inline const FString PartCurvePrefix(TEXT("FPGM_P_"));

	/** The 6 axis curve names for one attached weapon part (TX/TY/TZ/RP/RY/RR), built once from the part's stable
	 *  attach-socket id (FFPSRWeaponPartAttachment::Socket, e.g. "SOCKET_Mount_3") by MakePartCurveNames below. */
	struct FPSROGUELITE_API FPartCurveNames
	{
		FName TX = NAME_None;
		FName TY = NAME_None;
		FName TZ = NAME_None;
		FName RP = NAME_None;
		FName RY = NAME_None;
		FName RR = NAME_None;
	};

	/** Assemble the 6 FPGM_P_<AttachSocket>_* names for AttachSocket. Call this ONCE per attached part — at equip /
	 *  part-rebuild time (AFPSRCharacter::RebuildPartsFromSelection) — and cache the result; never reassemble an
	 *  FName from a string per animation frame (GunMotionTool_Spec.md §4-2 perf note). Returns all-NAME_None fields
	 *  when AttachSocket itself is NAME_None (no stable id to key off of). */
	FPSROGUELITE_API FPartCurveNames MakePartCurveNames(FName AttachSocket);
}
