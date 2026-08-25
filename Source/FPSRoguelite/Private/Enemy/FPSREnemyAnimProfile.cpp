// Copyright Epic Games, Inc. All Rights Reserved.

#include "Enemy/FPSREnemyAnimProfile.h"

#include "Components/MeshComponent.h"
#include "Engine/World.h"

void UFPSREnemyAnimProfile_Proc::ApplyAnimState(UMeshComponent* Mesh, EFPSRAnimState State, float PlayRate,
	float Phase) const
{
	if (!Mesh)
	{
		return;
	}

	// No MID here — CPD is per-primitive-instance data, not a material parameter, so instances sharing the base
	// material stay dynamic-instancing merge candidates (ADR 0007's reasoning, carried over from the deleted VAT_CPD
	// backend this replaces). SetCustomPrimitiveDataFloat write-on-changes (Memcmp-guarded) and pushes a lightweight
	// FScene::UpdateCustomPrimitiveData, not a full render-state recreate, so it is cheap to call on every state
	// transition, and on each ALLOWED restart of a one-shot state (AFPSREnemyBase::SetAnimState's re-entry guard —
	// Attack restarts only once its previous cycle elapsed, Death never restarts).
	const UWorld* World = Mesh->GetWorld();
	Mesh->SetCustomPrimitiveDataFloat(FPSRAnimCPD::CPDSlot_StateId, static_cast<float>(State));
	Mesh->SetCustomPrimitiveDataFloat(FPSRAnimCPD::CPDSlot_EnterTime, World ? World->GetTimeSeconds() : 0.0f);
	Mesh->SetCustomPrimitiveDataFloat(FPSRAnimCPD::CPDSlot_Rate, FMath::Max(0.0f, PlayRate));
	Mesh->SetCustomPrimitiveDataFloat(FPSRAnimCPD::CPDSlot_Phase, Phase);
	// CPDSlot_LastHitTime (slot 4) is deliberately NOT touched here — hit-flash timing is a separate, damage-driven
	// path (AFPSREnemyBase::HandleHealthChangedForHitFlash), not a state-transition concern.
}
