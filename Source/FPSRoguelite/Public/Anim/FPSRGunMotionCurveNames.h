// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Curve-channel names for the v3 gun-motion authoring contract (GunMotionTool_Spec.md §18). Single source of truth
 * for every FPGM_* name so the runtime consumers (UFPSRFirstPersonArmsAnimInstance, AFPSRCharacter) and the future
 * P2 editor tool (§20 — not built yet) never let a literal drift out of sync between them. Runtime module (not the
 * editor module) because the arms AnimInstance and the character both live here and need these at play time; the
 * editor tool will include this header too once it exists.
 *
 * Contract: unit cm/deg. A curve's ABSENCE from the currently-blended pose means "no offset" (0) for every channel
 * here — unlike the older LeftHandIKWeightCurve/RightHandIKWeightCurve convention (absence = 1), because these are
 * additive offset channels and 0 is the neutral element for addition. Each caller reads via
 * UAnimInstance::GetCurveValue and treats a false return as 0, never a hard failure.
 */
namespace FPSRGunMotionCurveNames
{
	// --- Whole-gun offset (§18 row 1-2). CAMERA space — the arms AnimInstance converts to component space via the
	// runtime M = ArmsRot^-1 * CamRot (GunMotionTool_Spec.md §19-1 / §3-2) before publishing GunOffsetLocation/
	// GunOffsetRotation for the ABP's TransformBone(ik_hand_gun) node (§19-2, P2 tool wiring — out of this scope).
	inline const FName Gun_TX(TEXT("FPGM_Gun_TX"));
	inline const FName Gun_TY(TEXT("FPGM_Gun_TY"));
	inline const FName Gun_TZ(TEXT("FPGM_Gun_TZ"));
	inline const FName Gun_RP(TEXT("FPGM_Gun_RP"));
	inline const FName Gun_RY(TEXT("FPGM_Gun_RY"));
	inline const FName Gun_RR(TEXT("FPGM_Gun_RR"));

	// --- Left-hand IK target offset + attach-part rebase blend (§18 row 3-4). GUN space already — same frame as
	// AFPSRCharacter::GetLeftHandGripInGunFrame / GetWeaponPartFrameInGunSpace (ik_hand_gun bone-parent space), so
	// no M conversion is needed here (only the whole-gun channels above cross from camera space).
	inline const FName HL_TX(TEXT("FPGM_HL_TX"));
	inline const FName HL_TY(TEXT("FPGM_HL_TY"));
	inline const FName HL_TZ(TEXT("FPGM_HL_TZ"));
	inline const FName HL_RP(TEXT("FPGM_HL_RP"));
	inline const FName HL_RY(TEXT("FPGM_HL_RY"));
	inline const FName HL_RR(TEXT("FPGM_HL_RR"));
	/** 0 = grip ⊕ offset, 1 = attach-part frame ⊕ offset (§18). Clamped 0..1 by the reader. */
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
	 *  (FPGM_P_<AttachSocket>_TX etc — §18 row 5). Exposed so a future P2 tool can build the same names without
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
	 *  FName from a string per animation frame (GunMotionTool_Spec.md §19-3 perf note). Returns all-NAME_None
	 *  fields when AttachSocket itself is NAME_None (no stable id to key off of). */
	FPSROGUELITE_API FPartCurveNames MakePartCurveNames(FName AttachSocket);
}
