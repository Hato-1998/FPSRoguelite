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
	float ClipIndex = FPSRVATAnim::ClipIndex_Idle;
	switch (State)
	{
	case EFPSRAnimState::Idle:   ClipIndex = FPSRVATAnim::ClipIndex_Idle;   break;
	case EFPSRAnimState::Walk:   ClipIndex = FPSRVATAnim::ClipIndex_Walk;   break;
	case EFPSRAnimState::Attack: ClipIndex = FPSRVATAnim::ClipIndex_Attack; break;
	case EFPSRAnimState::Death:  ClipIndex = FPSRVATAnim::ClipIndex_Death;  break;
	}

	MID->SetScalarParameterValue(NAME_AnimationIndex, ClipIndex);
	MID->SetScalarParameterValue(NAME_PlayRate, FMath::Max(0.0f, PlayRate));
	MID->SetScalarParameterValue(NAME_Phase, Phase);
}
