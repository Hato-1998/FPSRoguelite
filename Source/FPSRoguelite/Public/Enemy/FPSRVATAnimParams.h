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

/** C++ <-> VAT-material contract for swarm-enemy animation (U20 domain C).
 *
 *  ⚠️ Stage-2 EDITOR DEPENDENCY: the actual scalar parameter NAMES (in FPSREnemyAnimProfile.cpp) and the per-state
 *  clip indices below are a PLACEHOLDER contract — they cannot be confirmed headlessly and MUST be verified in-editor
 *  against M_BroBot_VAT / MF_BoneAnimation (does it GPU-autoplay a SELECTED index at a SET playrate? what are the real
 *  parameter names? is there a per-clip loop/hold for death?). This header + the profile cpp are the SINGLE edit-point:
 *  once confirmed, fill the real names/indices here and the whole driver picks them up. WriteAnimScalar is no-op-safe
 *  on a wrong name (SetScalarParameterValue on an absent parameter is ignored), so the code builds and runs today.
 *
 *  The whole driver is DORMANT until a designer assigns a UFPSREnemyAnimProfile to an enemy archetype (Stage 3), so
 *  it adds ZERO cost to the current cube/VAT render until content opts in. */
namespace FPSRVATAnim
{
	// VAT sequence indices (which baked clip in DA_*_VAT.AnimSequences[]) per state — LEGACY, the MID path
	// (UFPSREnemyAnimProfile_VAT) only: its material exposes a selectable "AnimationIndex" scalar param. PLACEHOLDER —
	// the content bake (Stage 3) defines the real order; keep in sync with the DA when idle/attack/death sequences are
	// baked. Today only a single walk/jog clip exists at index 0, so every state maps to it (visually a no-op until
	// Stage 3). The CPD path below does NOT use these — see the CPDSlot_StartFrame/EndFrame comment.
	constexpr float ClipIndex_Idle = 0.0f;
	constexpr float ClipIndex_Walk = 0.0f;
	constexpr float ClipIndex_Attack = 0.0f; // TODO Stage 3: real attack clip index
	constexpr float ClipIndex_Death = 0.0f;  // TODO Stage 3: real death clip index

	// CustomPrimitiveData slot indices (CPD path, Stage 2 render-path spike). In-editor verification of M_BroBot_VAT's
	// AnimToTexture playback (MF_BoneAnimation's GetFrameSwitch) found there is NO selectable "AnimationIndex" scalar —
	// a clip is a FRAME RANGE, and playback reads the scalar params StartFrame / EndFrame / Playrate / TimeOffset (the
	// AutoPlay static-switch path). UFPSREnemyAnimProfile_VAT_CPD::ApplyAnimState (FPSREnemyAnimProfile.cpp) writes
	// those four via Mesh->SetCustomPrimitiveDataFloat at the fixed indices below; the CPD-reauthored M_BroBot_VAT
	// reads them back through the SAME scalar params flagged bUseCustomPrimitiveData with PrimitiveDataIndex = these
	// indices (there is no dedicated CPD node — MaterialExpressionScalarParameter.h). This header is the single
	// edit-point for both sides of that contract — change an index here only if the material's nodes move too. Per-
	// state frame ranges are DATA (bake output), authored on UFPSREnemyAnimProfile_VAT_CPD as FFPSRVATClipRange
	// properties, not hardcoded here. The MID path (UFPSREnemyAnimProfile_VAT, the default) is unaffected: it uses the
	// legacy ClipIndex_* constants + named scalar params on a per-actor MID instead of these slots.
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
