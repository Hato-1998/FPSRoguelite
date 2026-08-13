// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "Enemy/FPSRVATAnimParams.h"
#include "FPSREnemyAnimProfile.generated.h"

class UMeshComponent;

/** One baked VAT clip's frame range on the AnimToTexture-style material (CPD path, Stage 2). DATA, not code — a
 *  clip is NOT a selectable index (see FPSRVATAnimParams.h CPDSlot_StartFrame/EndFrame comment); it is a
 *  [StartFrame, EndFrame] window the material's GetFrameSwitch autoplays through. Bake output, so authored per
 *  archetype on UFPSREnemyAnimProfile_VAT_CPD rather than hardcoded — today's bake only has one walk/jog clip, so
 *  every field defaults to 0..0 (content assigns the real ranges once more clips are baked). */
USTRUCT(BlueprintType)
struct FFPSRVATClipRange
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "FPSR|Enemy|Anim", meta = (ClampMin = "0.0"))
	float StartFrame = 0.0f;

	UPROPERTY(EditAnywhere, Category = "FPSR|Enemy|Anim", meta = (ClampMin = "0.0"))
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
	 *  offset so the swarm doesn't march in lockstep. */
	virtual void ApplyAnimState(UMeshComponent* Mesh, EFPSRAnimState State, float PlayRate, float Phase) const {}
};

// NOTE: the per-actor MID backend (UFPSREnemyAnimProfile_VAT) was DELETED per ADR 0007 — it was proven inert (its
// scalar parameter names do not exist on the material) and a per-actor MID takes the mesh out of dynamic-instancing
// merge candidacy (measured: 84 more draw calls @300 vs the CPD path; full merge analysis = VAT-2 V1). A designer
// must not be able to pick a silently-broken backend from the profile dropdown. Contingency lives in ADR 0007.

/** VAT render backend using CustomPrimitiveData (CPD) — the adopted swarm render path (ADR 0007). CPD lives on the
 *  PRIMITIVE COMPONENT instance, not on a unique material, so writing the animation scalars there lets every
 *  instance keep sharing the swarm's base material — staying a dynamic-instancing merge candidate, which a per-actor
 *  MID forfeits (measured 84-draw-call delta @300; complete-merge verification deferred to VAT-2 V1). No backend
 *  creates a MID any more (ADR 0007) — CPD is the only render path. CONTRACT: the assigned material must read Custom
 *  Primitive Data at the same fixed slot indices this writes to — FPSRVATAnim::CPDSlot_StartFrame/EndFrame/PlayRate/
 *  Phase (FPSRVATAnimParams.h) — so the CPD-reauthored material variant (M_BroBot_VAT_CPD) is required; a plain
 *  scalar-param material will not react to this backend. The material has no selectable clip-index param — a clip is
 *  a [StartFrame, EndFrame] window, so the per-state ranges are authored below (FFPSRVATClipRange). */
UCLASS(meta = (DisplayName = "VAT Anim Profile (CPD)"))
class FPSROGUELITE_API UFPSREnemyAnimProfile_VAT_CPD : public UFPSREnemyAnimProfile
{
	GENERATED_BODY()

public:
	virtual void ApplyAnimState(UMeshComponent* Mesh, EFPSRAnimState State, float PlayRate, float Phase) const override;

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

#if WITH_EDITOR
	/** Editor validation: a reversed clip range (EndFrame < StartFrame) is an authoring mistake — the runtime clamp
	 *  silently pins it to a single frame, which looks identical to the un-authored 0..0 default and is therefore
	 *  undiagnosable in-game. Warn at authoring time instead (the runtime clamp stays as the last line of defense). */
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};
