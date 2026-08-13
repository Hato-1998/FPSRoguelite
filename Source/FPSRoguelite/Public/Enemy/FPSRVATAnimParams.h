// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/** Swarm-enemy animation state (VAT-driven, U20 domain C). Cosmetic only — NEVER replicated (Performance §5: enemy
 *  replication = Transform only). On the authority (standalone / listen-server host) the server batch movement/attack
 *  pass derives it; on clients it is derived from the replicated transform (PostNetReceiveLocationAndRotation). */
enum class EFPSRAnimState : uint8
{
	Idle,
	Walk,
	Attack,
	Death,
};

/** C++ <-> VAT-material contract for swarm-enemy animation (U20 domain C, adopted render path = ADR 0007).
 *
 *  The whole driver is DORMANT until a designer assigns a UFPSREnemyAnimProfile to an enemy archetype, so it adds
 *  ZERO cost to any archetype that has not opted in. */
namespace FPSRVATAnim
{
	// CustomPrimitiveData slot indices (the adopted CPD render path, ADR 0007). In-editor verification of
	// M_BroBot_VAT's AnimToTexture playback (MF_BoneAnimation's GetFrameSwitch) found there is NO selectable
	// "AnimationIndex" scalar — a clip is a FRAME RANGE, and playback reads the scalar params StartFrame / EndFrame /
	// Playrate / TimeOffset (the AutoPlay static-switch path). UFPSREnemyAnimProfile_VAT_CPD::ApplyAnimState
	// (FPSREnemyAnimProfile.cpp) writes those four via Mesh->SetCustomPrimitiveDataFloat at the fixed indices below;
	// the CPD-reauthored material variant (M_BroBot_VAT_CPD) reads them back through the SAME scalar params flagged
	// bUseCustomPrimitiveData with PrimitiveDataIndex = these indices (there is no dedicated CPD node —
	// MaterialExpressionScalarParameter.h). This header is the single edit-point for both sides of that contract —
	// change an index here only if the material's params move too. Per-state frame ranges are DATA (bake output),
	// authored on UFPSREnemyAnimProfile_VAT_CPD as FFPSRVATClipRange properties, not hardcoded here.
	constexpr int32 CPDSlot_StartFrame = 0; // material param: StartFrame
	constexpr int32 CPDSlot_EndFrame   = 1; // material param: EndFrame
	constexpr int32 CPDSlot_PlayRate   = 2; // material param: Playrate
	constexpr int32 CPDSlot_Phase      = 3; // material param: TimeOffset (seconds) — per-actor de-lockstep offset

	// Reserved / unused by any current backend — kept clear for a future hit-flash cosmetic (a short emissive pulse
	// driven per-primitive without a MID) so it does not collide with a later Stage-3 slot allocation.
	constexpr int32 CPDSlot_HitFlash_Reserved = 4;

	// Anim distance LOD: beyond this squared radius the animation FREEZES (playrate 0 / no further param writes) to
	// shed CPU param writes and distant GPU frame-advance. MIRRORS UFPSREnemySpawnSubsystem::TierS1RadiusSq (the S1
	// boundary, Performance §5-1). Kept as a documented mirror to avoid a cross-header dependency for one constant.
	constexpr float AnimFreezeRadiusSq = 3500.0f * 3500.0f;

	// Speed-bucket quantization for the walk playrate: the state driver writes a new playrate only when the enemy's
	// speed crosses a bucket boundary (not every frame), so a swarm mostly walking issues ~0 writes/frame at steady
	// state. Alpha (CurrentMoveSpeed-relative) is quantized into this many buckets.
	constexpr int32 SpeedBucketCount = 4;
}
