// Copyright Epic Games, Inc. All Rights Reserved.

#include "Enemy/FPSREnemyAnimProfile.h"

#include "Components/MeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
	// Material scalar parameter names (MID path). ⚠️ PLACEHOLDER — confirm against M_BroBot_VAT / MF_BoneAnimation
	// in-editor (Stage 2). SetScalarParameterValue on an absent name is silently ignored, so a wrong name here is a
	// safe no-op (the driver still builds and runs; the animation just won't visibly change until the names match).
	static const FName NAME_AnimationIndex(TEXT("AnimationIndex"));
	static const FName NAME_PlayRate(TEXT("PlayRate"));
	static const FName NAME_Phase(TEXT("Phase"));

	// State -> baked VAT clip index. Shared by every render backend (MID + CPD) so the Stage-3 content bake has a
	// single edit-point. Clip indices are placeholders until then (see FPSRVATAnimParams.h).
	float ResolveClipIndex(EFPSRAnimState State)
	{
		switch (State)
		{
		case EFPSRAnimState::Idle:   return FPSRVATAnim::ClipIndex_Idle;
		case EFPSRAnimState::Walk:   return FPSRVATAnim::ClipIndex_Walk;
		case EFPSRAnimState::Attack: return FPSRVATAnim::ClipIndex_Attack;
		case EFPSRAnimState::Death:  return FPSRVATAnim::ClipIndex_Death;
		default:                     return FPSRVATAnim::ClipIndex_Idle;
		}
	}
}

void UFPSREnemyAnimProfile_VAT::ApplyAnimState(UMeshComponent* Mesh, EFPSRAnimState State, float PlayRate,
	float Phase, TObjectPtr<UMaterialInstanceDynamic>& CachedMID) const
{
	if (!Mesh)
	{
		return;
	}

	// Lazily create the per-actor MID on first use and reuse it thereafter (the caller only reaches here on a state
	// transition, and only where there is local rendering — see AFPSREnemyBase::SetAnimState's dedicated-server gate).
	// NOTE (Stage 2): 300 unique MIDs break draw-call batching; the CPD path (re-authored master material reading
	// Custom Primitive Data) is the cheaper target. This MID bridge keeps the driver working against today's material.
	UMaterialInstanceDynamic* MID = CachedMID;
	if (!MID)
	{
		MID = Mesh->CreateAndSetMaterialInstanceDynamic(0);
		CachedMID = MID;
	}
	if (!MID)
	{
		return;
	}

	// Clip index comes from the state; PlayRate is supplied by the caller (walk-speed scaled, or 0 to FREEZE the clip
	// for distance LOD). Clip indices are placeholders until the Stage-3 content bake.
	MID->SetScalarParameterValue(NAME_AnimationIndex, ResolveClipIndex(State));
	MID->SetScalarParameterValue(NAME_PlayRate, FMath::Max(0.0f, PlayRate));
	MID->SetScalarParameterValue(NAME_Phase, Phase);
}

void UFPSREnemyAnimProfile_VAT_CPD::ApplyAnimState(UMeshComponent* Mesh, EFPSRAnimState State, float PlayRate,
	float Phase, TObjectPtr<UMaterialInstanceDynamic>& CachedMID) const
{
	if (!Mesh)
	{
		return;
	}

	// No MID here (CachedMID is intentionally left untouched / stays null) — CPD is per-primitive-instance data, not
	// a material parameter, so instances sharing the base material keep sharing the same draw call (the whole point
	// of this backend, Stage 2). SetCustomPrimitiveDataFloat write-on-changes (Memcmp-guarded) and pushes a
	// lightweight FScene::UpdateCustomPrimitiveData, not a full render-state recreate, so it is cheap to call on
	// every state transition exactly like the MID path's SetScalarParameterValue above.
	//
	// The material has no selectable clip-index param (verified in-editor against M_BroBot_VAT / MF_BoneAnimation's
	// GetFrameSwitch) — a clip is a [StartFrame, EndFrame] window, authored per state below rather than resolved from
	// the legacy ClipIndex_* constants (those are the MID path's contract only).
	const FFPSRVATClipRange* ClipRange = &IdleClip;
	switch (State)
	{
	case EFPSRAnimState::Idle:   ClipRange = &IdleClip;   break;
	case EFPSRAnimState::Walk:   ClipRange = &WalkClip;   break;
	case EFPSRAnimState::Attack: ClipRange = &AttackClip; break;
	case EFPSRAnimState::Death:  ClipRange = &DeathClip;  break;
	}

	Mesh->SetCustomPrimitiveDataFloat(FPSRVATAnim::CPDSlot_StartFrame, ClipRange->StartFrame);
	Mesh->SetCustomPrimitiveDataFloat(FPSRVATAnim::CPDSlot_EndFrame, ClipRange->EndFrame);
	Mesh->SetCustomPrimitiveDataFloat(FPSRVATAnim::CPDSlot_PlayRate, FMath::Max(0.0f, PlayRate));
	Mesh->SetCustomPrimitiveDataFloat(FPSRVATAnim::CPDSlot_Phase, Phase);
}
