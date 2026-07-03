// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "Enemy/FPSRVATAnimParams.h"
#include "FPSREnemyAnimProfile.generated.h"

class UMeshComponent;
class UMaterialInstanceDynamic;

/** Polymorphic, data-driven selector for HOW a swarm enemy's animation is rendered (U20 domain C). An enemy archetype
 *  assigns a concrete profile (EditInlineNew instanced sub-object on AFPSREnemyBase); the base calls ApplyAnimState on
 *  animation-state transitions only (never per-frame). New render backends = a new subclass, with NO central enum/switch
 *  (mirrors the UFPSREnemySpawnRule extensibility pattern, Enemy.md §2-6).
 *
 *  Null profile (the default) = the whole anim driver is DORMANT: no MID is created and no scalar is written, so the
 *  current cube/VAT render is untouched (zero cost) until content opts an archetype in. */
UCLASS(Abstract, EditInlineNew, DefaultToInstanced, Blueprintable)
class FPSROGUELITE_API UFPSREnemyAnimProfile : public UObject
{
	GENERATED_BODY()

public:
	/** Apply an animation state to the enemy's mesh. Called ONLY on state/speed-bucket transitions (event-driven), never
	 *  per-frame — the GPU keeps advancing the VAT frame from time. MoveSpeedAlpha (~0..1) scales the walk playrate;
	 *  Phase (0..1) is a per-actor offset so the swarm doesn't march in lockstep. CachedMID is the caller's lazily
	 *  created MID slot (a concrete profile creates it here on first use and reuses it). */
	virtual void ApplyAnimState(UMeshComponent* Mesh, EFPSRAnimState State, float MoveSpeedAlpha, float Phase,
		TObjectPtr<UMaterialInstanceDynamic>& CachedMID) const {}
};

/** VAT (Vertex Animation Texture) render backend — the swarm default (U20). Drives the master material's animation
 *  scalars (clip index / playrate / phase) on a per-actor MID so the GPU self-plays the selected clip. Event-driven:
 *  written only on transitions. The MID path is the Stage-0/1 bridge; Stage 2 may flip to CustomPrimitiveData (which
 *  preserves draw-call batching) once M_BroBot_VAT is re-authored — the AFPSREnemyBase driver is agnostic to which. */
UCLASS(meta = (DisplayName = "VAT Anim Profile"))
class FPSROGUELITE_API UFPSREnemyAnimProfile_VAT : public UFPSREnemyAnimProfile
{
	GENERATED_BODY()

public:
	virtual void ApplyAnimState(UMeshComponent* Mesh, EFPSRAnimState State, float MoveSpeedAlpha, float Phase,
		TObjectPtr<UMaterialInstanceDynamic>& CachedMID) const override;
};
