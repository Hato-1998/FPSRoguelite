// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "FPSRDestructibleReward.generated.h"

class AActor;
class AFPSRDestructible;
class UGameplayEffect;
class UWorld;

/** Server-only context passed to a destructible reward's Grant(). Never replicated — the reward object itself is
 *  an inline Instanced subobject of the destructible actor (it rides the actor across the network; the context
 *  that wraps a single Grant() call never crosses the wire on its own), mirroring FFPSRCardEffectContext (Card/
 *  FPSRCardEffect.h) one layer down the stack: a card effect reacts to a PLAYER's pick, a destructible reward
 *  reacts to an ENVIRONMENT actor breaking. */
struct FFPSRDestructibleRewardContext
{
	/** The destructible that just broke. Rewards needing its location (radius checks) or identity read this. */
	AFPSRDestructible* Source = nullptr;

	/** The actor whose hit finished the destructible off. May be a player, may not be (e.g. an explosion instigated
	 *  by something else) — a reward must not assume this resolves to a player pawn/controller. */
	AActor* Breaker = nullptr;

	UWorld* World = nullptr;
};

/**
 * Polymorphic paid-out-on-break reward (ADR 0010 D7 "파괴 가능 프롭 = 문의 일반화"). A destructible owns an
 * EditAnywhere Instanced array of these; AFPSRDestructible::HandleBrokenAuthority loops them reward-type-agnostically.
 * A NEW reward type = one subclass, zero central edits — the same OCP shape as UFPSRCardEffect (Card/FPSRCardEffect.h),
 * one layer down the stack. A future "stage-transition" reward (the suppressor slice) is just another subclass here;
 * NOT built in this slice (see ADR 0010 D7 — no stub either).
 */
UCLASS(Abstract, EditInlineNew, DefaultToInstanced, CollapseCategories)
class FPSROGUELITE_API UFPSRDestructibleReward : public UObject
{
	GENERATED_BODY()

public:
	/** Server: pay out this reward. Called once per destructible break, from HandleBrokenAuthority, after bBroken has
	 *  already latched — a reward can safely treat the destructible as final/committed without racing the break itself. */
	virtual void Grant(const FFPSRDestructibleRewardContext& Context) const PURE_VIRTUAL(UFPSRDestructibleReward::Grant, );
};

/** Applies a GameplayEffect to every living, connected player within Radius of the broken destructible (0 = no
 *  distance limit — every living player, regardless of distance). ADR 0010 D7's "_Heal" reward IS this: plug in a
 *  heal GE and breaking the prop heals nearby players — the magnitude/behavior lives entirely in the GE asset,
 *  never hardcoded here (Game.MD §2 — no gameplay balance numbers in C++). */
UCLASS(meta = (DisplayName = "Player Effect (Radius)"))
class FPSROGUELITE_API UFPSRDestructibleReward_PlayerEffect : public UFPSRDestructibleReward
{
	GENERATED_BODY()

public:
	/** GE applied to every living player in range. Unset = no-op — a reward with no Effect is a content mistake, not
	 *  a valid "do nothing" configuration, so it is logged once as a warning per Grant() call (not once per player
	 *  the empty-Effect check would otherwise skip). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "FPSR|Destructible", meta = (DisplayName = "적용 효과"))
	TSubclassOf<UGameplayEffect> Effect;

	/** Radius (cm) from the destructible's actor location within which a living player receives Effect. 0 = no
	 *  distance limit (every connected living player receives it). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "FPSR|Destructible", meta = (DisplayName = "적용 반경(cm)", ClampMin = "0.0"))
	float Radius = 0.0f;

	virtual void Grant(const FFPSRDestructibleRewardContext& Context) const override;
};

/** Grants every living, connected player PickCount pending card pick(s) — the same per-player-grant-then-refresh-
 *  once shape as AFPSRGameState::AddSharedXP's level-up grant loop (Core/FPSRGameState.cpp), just triggered by a
 *  broken destructible instead of shared XP crossing a level threshold. */
UCLASS(meta = (DisplayName = "Card Pick"))
class FPSROGUELITE_API UFPSRDestructibleReward_CardPick : public UFPSRDestructibleReward
{
	GENERATED_BODY()

public:
	/** Pending card picks granted to EACH living player. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "FPSR|Destructible", meta = (DisplayName = "카드 픽 지급 수", ClampMin = "1"))
	int32 PickCount = 1;

	virtual void Grant(const FFPSRDestructibleRewardContext& Context) const override;
};

/** The suppressor (억제기), ADR 0010 D6/D7. **A suppressor is not a separate class** — any AFPSRArenaDestructible
 *  authored with exactly this one Reward IS the suppressor; the polymorphic Rewards array (see AFPSRDestructible's
 *  header) is what makes "add a new kind of break trigger" a subclass here instead of a new destructible hierarchy.
 *  Grant() asks the world's UFPSRStageDirectorSubsystem to begin the stage-transition state machine (grace dealing
 *  window -> swap). Multiple suppressors / a multi-kill explosion are safe: the subsystem silently ignores every
 *  request after the first (see UFPSRStageDirectorSubsystem::RequestTransition). */
UCLASS(meta = (DisplayName = "Stage Transition"))
class FPSROGUELITE_API UFPSRDestructibleReward_StageTransition : public UFPSRDestructibleReward
{
	GENERATED_BODY()

public:
	virtual void Grant(const FFPSRDestructibleRewardContext& Context) const override;
};
