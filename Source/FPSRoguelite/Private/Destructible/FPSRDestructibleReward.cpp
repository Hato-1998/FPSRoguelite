// Copyright Epic Games, Inc. All Rights Reserved.

#include "Destructible/FPSRDestructibleReward.h"
#include "Destructible/FPSRDestructible.h"
#include "Core/FPSRPlayerState.h"
#include "Core/FPSRGameState.h"
#include "Core/FPSRLogChannels.h"
#include "AbilitySystem/FPSRAbilitySystemComponent.h"
#include "AbilitySystemComponent.h"
#include "GameplayEffect.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"
#include "Run/FPSRStageDirectorSubsystem.h" // no cycle: the stage director (Run/) does not know about rewards (Destructible/)

// ---------------------------------------------------------------------------------------------------------------
// UFPSRDestructibleReward_PlayerEffect — GE applied to every living player within Radius (ADR 0010 D7 "_Heal").
// ---------------------------------------------------------------------------------------------------------------
void UFPSRDestructibleReward_PlayerEffect::Grant(const FFPSRDestructibleRewardContext& Context) const
{
	if (!Effect)
	{
		// One warning for the whole Grant() call, not once per connected player — a misconfigured reward is a
		// content mistake, but it must not spam the log every time this reward pays out.
		UE_LOG(LogFPSR, Warning, TEXT("[Destructible] PlayerEffect reward on %s has no Effect set — no-op."),
			Context.Source ? *Context.Source->GetName() : TEXT("?"));
		return;
	}
	if (!Context.Source || !Context.World)
	{
		return;
	}

	const FVector Origin = Context.Source->GetActorLocation();
	const float RadiusSq = Radius * Radius;

	// World->GetPlayerControllerIterator() (not GameState->PlayerArray) — this reward needs BOTH the pawn's
	// location (radius check) and the PlayerState's ASC, and the controller is the shared path to both (mirrors
	// Pickup/FPSRXPPickup.cpp's collect/magnet scan).
	for (FConstPlayerControllerIterator It = Context.World->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* PC = It->Get();
		APawn* PlayerPawn = PC ? PC->GetPawn() : nullptr;
		if (!PlayerPawn)
		{
			continue;
		}
		const AFPSRPlayerState* PS = PC->GetPlayerState<AFPSRPlayerState>();
		if (!PS || !PS->IsAlive())
		{
			continue; // DBNO/Dead players don't receive break rewards (mirrors the XP pickup collect gate)
		}
		if (Radius > 0.0f && FVector::DistSquared(PlayerPawn->GetActorLocation(), Origin) > RadiusSq)
		{
			continue;
		}
		UFPSRAbilitySystemComponent* ASC = PS->GetFPSRAbilitySystemComponent();
		if (!ASC)
		{
			continue;
		}
		// Same apply path as UCardEffect_CharacterGE::Apply (Card/FPSRCardEffect.cpp). AddSourceObject carries the
		// destructible — the actual cause of this GE for every recipient — rather than the recipient's own PS.
		FGameplayEffectContextHandle Ctx = ASC->MakeEffectContext();
		Ctx.AddSourceObject(Context.Source);
		FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(Effect, 1.0f, Ctx);
		if (Spec.IsValid())
		{
			ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
		}
	}
}

// ---------------------------------------------------------------------------------------------------------------
// UFPSRDestructibleReward_CardPick — grants every living player PickCount pending card pick(s).
// ---------------------------------------------------------------------------------------------------------------
void UFPSRDestructibleReward_CardPick::Grant(const FFPSRDestructibleRewardContext& Context) const
{
	if (!Context.World)
	{
		return;
	}
	AFPSRGameState* GS = Context.World->GetGameState<AFPSRGameState>();
	if (!GS)
	{
		return;
	}

	// GameState->PlayerArray (not the controller iterator) — this reward only needs the PlayerState, matching
	// AFPSRGameState::AddSharedXP's level-up grant loop exactly (Core/FPSRGameState.cpp).
	for (APlayerState* PSBase : GS->PlayerArray)
	{
		AFPSRPlayerState* PS = Cast<AFPSRPlayerState>(PSBase);
		if (!PS || !PS->IsAlive())
		{
			continue; // DBNO/Dead players don't participate in card selection (mirrors AddSharedXP's grant gate)
		}
		for (int32 i = 0; i < PickCount; ++i)
		{
			PS->AddCardPick();
		}
	}

	// Once for the whole grant, NOT once per player — RefreshPauseState recomputes the freeze from every player's
	// pending-pick count, so calling it inside the loop above would re-derive the same freeze state N times.
	GS->RefreshPauseState();
}

// ---------------------------------------------------------------------------------------------------------------
// UFPSRDestructibleReward_StageTransition — the suppressor (ADR 0010 D6/D7). See the header: there is no separate
// suppressor class, only a destructible authored with this one Reward.
// ---------------------------------------------------------------------------------------------------------------
void UFPSRDestructibleReward_StageTransition::Grant(const FFPSRDestructibleRewardContext& Context) const
{
	if (!Context.World)
	{
		return;
	}
	if (UFPSRStageDirectorSubsystem* StageDirector = Context.World->GetSubsystem<UFPSRStageDirectorSubsystem>())
	{
		StageDirector->RequestTransition();
	}
	else
	{
		UE_LOG(LogFPSR, Warning, TEXT("[Destructible] StageTransition reward broke on %s but no UFPSRStageDirectorSubsystem exists in this world."),
			Context.Source ? *Context.Source->GetName() : TEXT("?"));
	}
}
