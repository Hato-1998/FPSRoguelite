// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "Enemy/FPSRVATAnimParams.h"
#include "FPSREnemyAnimProfile.generated.h"

class UMeshComponent;
class UMaterialInstanceDynamic;

/** One baked VAT clip's frame range on the AnimToTexture-style material (CPD path, Stage 2). DATA, not code — a
 *  clip is NOT a selectable index (see FPSRVATAnimParams.h CPDSlot_StartFrame/EndFrame comment); it is a
 *  [StartFrame, EndFrame] window the material's GetFrameSwitch autoplays through. Bake output, so authored per
 *  archetype on UFPSREnemyAnimProfile_VAT_CPD rather than hardcoded — today's bake only has one walk/jog clip, so
 *  every field defaults to 0..0 (content assigns the real ranges once more clips are baked). */
USTRUCT(BlueprintType)
struct FFPSRVATClipRange
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "FPSR|Enemy|Anim")
	float StartFrame = 0.0f;

	UPROPERTY(EditAnywhere, Category = "FPSR|Enemy|Anim")
	float EndFrame = 0.0f;
};

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
	/** Apply an animation state to the enemy's mesh. Called ONLY on state/playrate-bucket transitions (event-driven),
	 *  never per-frame — the GPU keeps advancing the VAT frame from time. PlayRate is the EXPLICIT playback rate to
	 *  write (the caller computes it): 1.0 for a normal clip, a speed-scaled value for walk, and 0.0 to FREEZE the
	 *  clip in place (distance LOD — sheds CPU writes AND distant GPU frame advance). Phase (0..1) is a per-actor
	 *  offset so the swarm doesn't march in lockstep. CachedMID is the caller's lazily created MID slot. */
	virtual void ApplyAnimState(UMeshComponent* Mesh, EFPSRAnimState State, float PlayRate, float Phase,
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
	virtual void ApplyAnimState(UMeshComponent* Mesh, EFPSRAnimState State, float PlayRate, float Phase,
		TObjectPtr<UMaterialInstanceDynamic>& CachedMID) const override;
};

/** VAT render backend using CustomPrimitiveData (CPD) instead of a per-actor MID (U20 Stage-2 render-path spike).
 *  CPD lives on the PRIMITIVE COMPONENT instance, not on a unique material, so writing the animation scalars there
 *  (instead of a per-actor MID's scalar parameters) lets every instance keep sharing the swarm's base material —
 *  preserving draw-call batching, which the MID backend explicitly breaks at swarm scale (300 unique MIDs = 300 draw
 *  calls; see UFPSREnemyAnimProfile_VAT::ApplyAnimState). CachedMID is left untouched (stays null): this backend
 *  never creates one. CONTRACT: the assigned material must read Custom Primitive Data at the same fixed slot indices
 *  this writes to — FPSRVATAnim::CPDSlot_StartFrame/EndFrame/PlayRate/Phase (FPSRVATAnimParams.h) — so a re-authored
 *  M_BroBot_VAT (CPD variant) is required; the default MID-authored material will not react to this backend. Unlike
 *  the MID path's selectable clip index, the material has no such param — a clip is a [StartFrame, EndFrame] window,
 *  so the per-state ranges are authored below (FFPSRVATClipRange) instead of the legacy ClipIndex_* constants. */
UCLASS(meta = (DisplayName = "VAT Anim Profile (CPD)"))
class FPSROGUELITE_API UFPSREnemyAnimProfile_VAT_CPD : public UFPSREnemyAnimProfile
{
	GENERATED_BODY()

public:
	virtual void ApplyAnimState(UMeshComponent* Mesh, EFPSRAnimState State, float PlayRate, float Phase,
		TObjectPtr<UMaterialInstanceDynamic>& CachedMID) const override;

	/** Baked frame range for the Idle clip. Defaults to 0..0 (today's bake has only one walk/jog clip) — content
	 *  fills in the real range once more clips exist. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Enemy|Anim")
	FFPSRVATClipRange IdleClip;

	/** Baked frame range for the Walk clip. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Enemy|Anim")
	FFPSRVATClipRange WalkClip;

	/** Baked frame range for the Attack clip. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Enemy|Anim")
	FFPSRVATClipRange AttackClip;

	/** Baked frame range for the Death clip. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Enemy|Anim")
	FFPSRVATClipRange DeathClip;
};
