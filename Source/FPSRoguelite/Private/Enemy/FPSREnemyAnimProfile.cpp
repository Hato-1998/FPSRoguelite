// Copyright Epic Games, Inc. All Rights Reserved.

#include "Enemy/FPSREnemyAnimProfile.h"

#include "Components/MeshComponent.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"

#define LOCTEXT_NAMESPACE "FPSREnemyAnimProfile"

EDataValidationResult UFPSREnemyAnimProfile_VAT_CPD::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	// A reversed range is always an authoring slip (the bake emits Start <= End) — warn here because the runtime
	// clamp corrects it silently into a single held frame, indistinguishable from the un-authored 0..0 default.
	const TPair<FText, const FFPSRVATClipRange*> Clips[] = {
		{ LOCTEXT("ClipIdle", "IdleClip"), &IdleClip },
		{ LOCTEXT("ClipWalk", "WalkClip"), &WalkClip },
		{ LOCTEXT("ClipAttack", "AttackClip"), &AttackClip },
		{ LOCTEXT("ClipDeath", "DeathClip"), &DeathClip },
	};
	for (const TPair<FText, const FFPSRVATClipRange*>& Clip : Clips)
	{
		if (Clip.Value->EndFrame < Clip.Value->StartFrame)
		{
			Context.AddWarning(FText::Format(
				LOCTEXT("ReversedClipRange", "{0} has EndFrame < StartFrame — the runtime will hold a single frame. Swap the values (the bake's DA.Animations[] lists Start <= End)."),
				Clip.Key));
		}
	}

	return Result;
}

#undef LOCTEXT_NAMESPACE
#endif // WITH_EDITOR

void UFPSREnemyAnimProfile_VAT_CPD::ApplyAnimState(UMeshComponent* Mesh, EFPSRAnimState State, float PlayRate,
	float Phase) const
{
	if (!Mesh)
	{
		return;
	}

	// No MID here — CPD is per-primitive-instance data, not a material parameter, so instances sharing the base
	// material stay dynamic-instancing merge candidates (the whole point of this backend, ADR 0007).
	// SetCustomPrimitiveDataFloat write-on-changes (Memcmp-guarded) and pushes a lightweight
	// FScene::UpdateCustomPrimitiveData, not a full render-state recreate, so it is cheap to call on every state
	// transition.
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

	// Defensive clamp: an authoring mistake (EndFrame < StartFrame) must not hand the GPU a reversed frame range to
	// read — silently correct it here rather than trusting the bake data as-is (no log; this is a quiet guard).
	const float StartFrame = ClipRange->StartFrame;
	const float EndFrame = FMath::Max(StartFrame, ClipRange->EndFrame);

	Mesh->SetCustomPrimitiveDataFloat(FPSRVATAnim::CPDSlot_StartFrame, StartFrame);
	Mesh->SetCustomPrimitiveDataFloat(FPSRVATAnim::CPDSlot_EndFrame, EndFrame);
	Mesh->SetCustomPrimitiveDataFloat(FPSRVATAnim::CPDSlot_PlayRate, FMath::Max(0.0f, PlayRate));
	Mesh->SetCustomPrimitiveDataFloat(FPSRVATAnim::CPDSlot_Phase, Phase);
}
