// Copyright Epic Games, Inc. All Rights Reserved.

#include "Enemy/FPSREnemyAnimProfile.h"

#include "Components/MeshComponent.h"

void UFPSREnemyAnimProfile_VAT_CPD::ApplyAnimState(UMeshComponent* Mesh, EFPSRAnimState State, float PlayRate,
	float Phase, TObjectPtr<UMaterialInstanceDynamic>& CachedMID) const
{
	if (!Mesh)
	{
		return;
	}

	// No MID here (CachedMID is intentionally left untouched / stays null) — CPD is per-primitive-instance data, not
	// a material parameter, so instances sharing the base material stay dynamic-instancing merge candidates (the
	// whole point of this backend, ADR 0007). SetCustomPrimitiveDataFloat write-on-changes (Memcmp-guarded) and
	// pushes a lightweight FScene::UpdateCustomPrimitiveData, not a full render-state recreate, so it is cheap to
	// call on every state transition.
	//
	// The material has no selectable clip-index param (verified in-editor against M_BroBot_VAT / MF_BoneAnimation's
	// GetFrameSwitch) — a clip is a [StartFrame, EndFrame] window, authored per state on this profile.
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
