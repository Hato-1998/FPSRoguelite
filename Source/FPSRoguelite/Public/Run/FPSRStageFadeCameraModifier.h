// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Camera/CameraModifier.h"
#include "FPSRStageFadeCameraModifier.generated.h"

class UMaterialInstanceDynamic;

/** Per-PlayerController camera modifier that carries the stage-transition fade material into the view every
 *  frame (Phase B). UFPSRStageFadeSubsystem owns one of these per local player controller (AddNewCameraModifier)
 *  and each Tick just refreshes CurrentAlpha/FadeMID here — the actual per-frame re-application happens through
 *  the engine's own ModifyPostProcess call, not from the subsystem's Tick directly.
 *
 *  Why not APlayerCameraManager::AddCachedPPBlend called straight from the subsystem's Tick: that cache is wiped
 *  by ClearCachedPPBlends() at the TOP of every APlayerCameraManager::ApplyCameraModifiers call (PlayerCameraManager.cpp),
 *  and every real engine caller of AddCachedPPBlend (UCameraModifier::ModifyCamera itself, UCameraShakeBase via
 *  ApplyCameraModifiers' pending-view-target blend) calls it from INSIDE that same function's call stack — i.e.
 *  between the Clear and the render's later read of the cache. A world subsystem's own Tick has no guaranteed
 *  ordering against a given PlayerController's camera update, so calling AddCachedPPBlend directly from there could
 *  land before that frame's Clear (silently discarded) as easily as after it (works, but only by accident of tick
 *  order). Subclassing UCameraModifier and overriding the protected ModifyPostProcess hook — the same hook the
 *  engine's OWN UCameraModifier::ModifyCamera uses to call AddCachedPPBlend (CameraModifier.cpp) — puts this code
 *  inside that guaranteed window instead, camera-agnostic (works whatever actor is the current view target, unlike
 *  writing directly into one specific actor's own camera component). */
UCLASS()
class FPSROGUELITE_API UFPSRStageFadeCameraModifier : public UCameraModifier
{
	GENERATED_BODY()

public:
	/** Refresh what this modifier applies next time the engine calls ModifyPostProcess (this frame or next — a
	 *  one-frame lag here is imperceptible against a fade that runs for several hundred ms). Alpha<=0 clears the
	 *  applied material so ModifyPostProcess adds nothing at all (Phase B spec: alpha==0 => zero PP overhead). */
	void SetFadeState(UMaterialInstanceDynamic* InFadeMID, float InAlpha);

protected:
	//~UCameraModifier
	virtual void ModifyPostProcess(float DeltaTime, float& PostProcessBlendWeight, FPostProcessSettings& PostProcessSettings) override;
	//~End UCameraModifier

private:
	/** Set by SetFadeState; GC-protected so the subsystem's shared MID cannot be collected between refreshes. */
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> FadeMID;

	/** Set by SetFadeState; ModifyPostProcess reads this to decide whether to add FadeMID as a blendable this frame. */
	float CurrentAlpha = 0.0f;
};
