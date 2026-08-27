// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/** Swarm-enemy animation state (procedural-mesh domain C). Cosmetic only — NEVER replicated (Performance §5: enemy
 *  replication = Transform only). On the authority (standalone / listen-server host) the server batch movement/attack
 *  pass derives it; on clients it is derived from the replicated transform (PostNetReceiveLocationAndRotation). */
enum class EFPSRAnimState : uint8
{
	Idle,
	Walk,
	Attack,
	Death,
};

/** C++ <-> material contract for swarm-enemy animation (U20 domain C). The enemy render path moved from skeletal VAT
 *  playback to a PROCEDURAL STATIC MESH driven by a Custom HLSL World Position Offset (2026-08-24 user decision), so
 *  this header is now a pure STATE + TIMING contract — not a baked-clip frame-range contract (the old StartFrame/
 *  EndFrame/PlayRate/TimeOffset scheme this replaces is gone along with the skeletal track it described). The
 *  material side of this contract is a later stage — nothing reads these slots yet, which is expected here.
 *
 *  The whole driver is DORMANT until a designer assigns a UFPSREnemyAnimProfile to an enemy archetype, so it adds
 *  ZERO cost to any archetype that has not opted in. */
namespace FPSRAnimCPD
{
	// CustomPrimitiveData slot indices. UFPSREnemyAnimProfile_Proc::ApplyAnimState (FPSREnemyAnimProfile.cpp) writes
	// slots 0-3 via Mesh->SetCustomPrimitiveDataFloat on every state/playrate-bucket transition, plus each ALLOWED
	// restart of a one-shot state (AFPSREnemyBase::SetAnimState's re-entry guard: Attack restarts only once its
	// previous cycle elapsed, Death never restarts); AFPSREnemyBase::HandleHealthChangedForHitFlash writes
	// slot 4 directly on a damage edge. This header is the single edit-point for both sides of the contract — change
	// an index here only if the material's WPO reads move too.
	//
	// CONTRACT for the untouched (never-written) primitive: an actor with no AnimProfile assigned — or one not yet
	// reached by its first SetAnimState call — leaves slots 0-2 at their CPD zero-init default. That decodes as
	// State=Idle (0), EnterTime=0, Rate=0. By the time such a primitive is ever sampled, GameTime is typically large,
	// so (Time - EnterTime) is large too — the material's progress formula (Time - EnterTime) * Rate MUST rely on
	// Rate=0 to zero that product out regardless, so the default renders as a static held Idle pose, never a huge or
	// wrapping progress value. This is the "slots 0/1/2 default 0 = Idle / loop / tolerant of a large (T-0)" contract.
	constexpr int32 CPDSlot_StateId     = 0; // float: Idle=0, Walk=1, Attack=2, Death=3 — EFPSRAnimState's own order
	constexpr int32 CPDSlot_EnterTime   = 1; // world seconds this state was entered — same clock as material Time (GameTime)
	constexpr int32 CPDSlot_Rate        = 2; // loop states: playback-speed multiplier. One-shot states: 1/duration, so the
	                                          // material's (Time-EnterTime)*Rate progress reaches 1.0 exactly at the hold/dwell length
	constexpr int32 CPDSlot_Phase       = 3; // 0..1 per-instance phase offset (material param: InstancePhase) — de-lockstep
	constexpr int32 CPDSlot_LastHitTime = 4; // world seconds of the last HP-decreasing hit this life; 0 = never hit yet

	// Reserved / unused by any current backend. The engine fixes the CPD payload at 9 float4s per primitive regardless
	// of how many a project uses (FCustomPrimitiveData::NumCustomPrimitiveDataFloat4s = 9 in SceneTypes.h, which must
	// match NUM_CUSTOM_PRIMITIVE_DATA in SceneData.ush; its sibling NumCustomPrimitiveDataFloats = 36 is the flat
	// float count) — i.e. 36 float slots are already reserved whether or not this header uses them, so there is no
	// scarcity pressure motivating a cramped allocation here; slots 5+ remain free for a future cosmetic without
	// renumbering anything above.

	// Speed-bucket quantization for the walk playrate: the state driver writes a new playrate only when the enemy's
	// speed crosses a bucket boundary (not every frame), so a swarm mostly walking issues ~0 writes/frame at steady
	// state. Alpha (CurrentMoveSpeed-relative) is quantized into this many buckets.
	constexpr int32 SpeedBucketCount = 4;
}
