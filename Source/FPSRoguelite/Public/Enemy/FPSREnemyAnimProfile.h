// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "Enemy/FPSRAnimCPDParams.h"
#include "FPSREnemyAnimProfile.generated.h"

class UMeshComponent;

/** Polymorphic, data-driven selector for HOW a swarm enemy's animation is rendered (U20 domain C). An enemy archetype
 *  assigns a concrete profile (EditInlineNew instanced sub-object on AFPSREnemyBase); the base calls ApplyAnimState on
 *  animation-state transitions only (never per-frame). New render backends = a new subclass, with NO central enum/switch
 *  (mirrors the UFPSREnemySpawnRule extensibility pattern, Enemy.md §2-6).
 *
 *  Null profile (the default) = the whole anim driver is DORMANT: no scalar is written, so the current render is
 *  untouched (zero cost) until content opts an archetype in. */
UCLASS(Abstract, EditInlineNew, DefaultToInstanced, Blueprintable)
class FPSROGUELITE_API UFPSREnemyAnimProfile : public UObject
{
	GENERATED_BODY()

public:
	/** Apply an animation state to the enemy's mesh. Called on every state/playrate-bucket transition (event-driven),
	 *  plus each ALLOWED restart of a one-shot state (Attack/Death) that the plain dedupe would have swallowed. The
	 *  restart rule is NOT "every re-entry" — AFPSREnemyBase::SetAnimState's re-entry guard restarts Attack only once
	 *  its previous cycle has elapsed (the client proximity tell re-asserts Attack on every net update and would
	 *  otherwise rewind it forever) and never restarts Death (terminal — the corpse is on its way out). Never called per-frame — the GPU keeps advancing the material's WPO from time on its own.
	 *  PlayRate is the EXPLICIT rate to write (the caller computes it): a speed-scaled multiplier for a looping state
	 *  (Idle/Walk), or 1/duration for a one-shot state so the material's (Time-EnterTime)*Rate progress reaches 1.0
	 *  exactly at the authored hold/dwell length. Phase (0..1) is a per-actor offset so the swarm doesn't march in
	 *  lockstep. */
	virtual void ApplyAnimState(UMeshComponent* Mesh, EFPSRAnimState State, float PlayRate, float Phase) const {}
};

// NOTE: the per-actor MID backend (UFPSREnemyAnimProfile_VAT) was DELETED per ADR 0007 — it was proven inert (its
// scalar parameter names do not exist on the material) and a per-actor MID takes the mesh out of dynamic-instancing
// merge candidacy (measured: 84 more draw calls @300 vs the CPD path; full merge analysis = VAT-2 V1). A designer
// must not be able to pick a silently-broken backend from the profile dropdown. Contingency lives in ADR 0007.

// NOTE: the skeletal-VAT CPD backend (UFPSREnemyAnimProfile_VAT_CPD) and its FFPSRVATClipRange baked-clip data were
// DELETED when the enemy render path moved from skeletal VAT playback to a PROCEDURAL STATIC MESH (2026-08-24 user
// decision). That backend wrote a [StartFrame, EndFrame] baked-clip-range contract (the old FPSRVATAnimParams.h
// CPDSlot_StartFrame/EndFrame/PlayRate scheme) which no longer has a reader — the new contract is a per-state STATE
// ID + ENTER TIME + RATE (FPSRAnimCPDParams.h, FPSRAnimCPD::CPDSlot_*) that a procedural WPO evaluates directly
// instead of scrubbing a baked texture. Its recorded writes (and any archetype that still had it assigned) would
// silently target CPD slots the new material never reads. A designer must not be able to pick this now-orphaned
// backend from the profile dropdown. See UFPSREnemyAnimProfile_Proc below for the replacement.

/** Procedural-mesh render backend using CustomPrimitiveData (CPD) — a pure translator from the animation-state enum
 *  to the material's state/timing contract (FPSRAnimCPDParams.h). CPD lives on the PRIMITIVE COMPONENT instance, not
 *  on a unique material, so writing it here lets every instance keep sharing the swarm's base material — staying a
 *  dynamic-instancing merge candidate (the same reasoning that ruled out a per-actor MID above). Holds NO data of
 *  its own: a state's DURATION (hence its playback Rate) is the CALLER's concern
 *  (AFPSREnemyBase::AttackAnimHoldSeconds / DeathDwellSeconds) — gameplay data that a future server lifecycle also
 *  needs to read, not a cosmetic-profile property. The material side of this contract (a Custom HLSL WPO reading
 *  these CPD slots) is a later stage — nothing reads these writes yet, which is expected at this point. */
UCLASS(meta = (DisplayName = "Procedural Anim Profile (CPD)"))
class FPSROGUELITE_API UFPSREnemyAnimProfile_Proc : public UFPSREnemyAnimProfile
{
	GENERATED_BODY()

public:
	virtual void ApplyAnimState(UMeshComponent* Mesh, EFPSRAnimState State, float PlayRate, float Phase) const override;
};
