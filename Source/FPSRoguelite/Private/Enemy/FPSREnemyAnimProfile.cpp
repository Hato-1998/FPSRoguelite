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

void UFPSREnemyAnimProfile_VAT::ApplyAnimState(UMeshComponent* Mesh, EFPSRAnimState State, float MoveSpeedAlpha,
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

	// Map the state to a clip index + playrate. Death holds (playrate 0 handled by the caller/LOD path when frozen);
	// Walk scales with the enemy's speed; Idle/Attack play at unit rate. Clip indices are placeholders until Stage 3.
	float ClipIndex = FPSRVATAnim::ClipIndex_Idle;
	float PlayRate = 1.0f;
	switch (State)
	{
	case EFPSRAnimState::Idle:
		ClipIndex = FPSRVATAnim::ClipIndex_Idle;
		PlayRate = 1.0f;
		break;
	case EFPSRAnimState::Walk:
		ClipIndex = FPSRVATAnim::ClipIndex_Walk;
		PlayRate = FMath::Max(0.1f, MoveSpeedAlpha);
		break;
	case EFPSRAnimState::Attack:
		ClipIndex = FPSRVATAnim::ClipIndex_Attack;
		PlayRate = 1.0f;
		break;
	case EFPSRAnimState::Death:
		ClipIndex = FPSRVATAnim::ClipIndex_Death;
		PlayRate = 1.0f;
		break;
	}

	MID->SetScalarParameterValue(NAME_AnimationIndex, ClipIndex);
	MID->SetScalarParameterValue(NAME_PlayRate, PlayRate);
	MID->SetScalarParameterValue(NAME_Phase, Phase);
}
