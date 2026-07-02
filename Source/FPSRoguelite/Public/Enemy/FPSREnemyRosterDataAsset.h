// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DataAsset.h"
#include "Templates/SubclassOf.h"
#include "Enemy/FPSREnemyBase.h" // full type: the inline spawn-rule accessors deref TSubclassOf<AFPSREnemyBase> (needs StaticClass)
#include "FPSREnemyRosterDataAsset.generated.h"

/** Run context handed to a spawn rule so it can scale its weight by progression (Game.MD §2-12 — e.g. the ranged
 *  share rises over time / party level). Kept tiny: the spawn director builds it from the GameState each spawn. */
USTRUCT(BlueprintType)
struct FFPSREnemySpawnContext
{
	GENERATED_BODY()

	/** Run-clock seconds since combat began (AFPSRGameState::GetRunClockSeconds). */
	UPROPERTY(BlueprintReadOnly, Category = "Spawn Context")
	float RunClockSeconds = 0.0f;

	/** Current party level (AFPSRGameState::GetPartyLevel). */
	UPROPERTY(BlueprintReadOnly, Category = "Spawn Context")
	int32 PartyLevel = 1;
};

/** One polymorphic enemy-mix rule. Extensibility-first (no central enum/switch): adding a new mix policy is a new
 *  EditInlineNew subclass that overrides GetWeight — the roster and spawn subsystem never change. A rule maps a run
 *  context to (enemy class, selection weight). The MVP ships UFPSREnemySpawnRule_Static (flat weight); future rules
 *  (level-scaled, time-windowed, etc.) plug in here. */
UCLASS(Abstract, EditInlineNew, DefaultToInstanced, BlueprintType)
class FPSROGUELITE_API UFPSREnemySpawnRule : public UObject
{
	GENERATED_BODY()

public:
	/** The enemy class this rule contributes (null = the rule is inert). */
	virtual TSubclassOf<AFPSREnemyBase> GetEnemyClass() const { return nullptr; }

	/** Selection weight in [0, +inf) given the run context. 0 = not eligible right now. */
	virtual float GetWeight(const FFPSREnemySpawnContext& Context) const { return 0.0f; }
};

/** MVP rule: a fixed enemy class with a constant weight (no run-context scaling). */
UCLASS(EditInlineNew, DisplayName = "Static Weight")
class FPSROGUELITE_API UFPSREnemySpawnRule_Static : public UFPSREnemySpawnRule
{
	GENERATED_BODY()

public:
	/** The enemy archetype (BP child of AFPSREnemyBase) this rule spawns. */
	UPROPERTY(EditAnywhere, Category = "Spawn Rule")
	TSubclassOf<AFPSREnemyBase> EnemyClass;

	/** Relative selection weight (higher = spawns more often). */
	UPROPERTY(EditAnywhere, Category = "Spawn Rule", meta = (ClampMin = "0.0"))
	float Weight = 1.0f;

	virtual TSubclassOf<AFPSREnemyBase> GetEnemyClass() const override { return EnemyClass; }
	virtual float GetWeight(const FFPSREnemySpawnContext& Context) const override { return EnemyClass ? Weight : 0.0f; }
};

/** Data-driven enemy roster (Game.MD §2-6 archetype mix). The spawn director picks an enemy class by weighted random
 *  from SpawnRules each spawn — so the melee/ranged mix is authored data, not hardcoded ratios (Game.MD §6-2). EMPTY
 *  (or all rules inert) = the spawn subsystem falls back to its single configured EnemyClass (no regression). */
UCLASS(BlueprintType)
class FPSROGUELITE_API UFPSREnemyRosterDataAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Polymorphic mix rules (instanced so designers pick a rule type per entry in the asset editor). */
	UPROPERTY(EditAnywhere, Instanced, Category = "Roster")
	TArray<TObjectPtr<UFPSREnemySpawnRule>> SpawnRules;

	/** Server: weighted-random pick of an enemy class for the given run context. Returns null when no rule is
	 *  eligible (the caller then falls back to its single configured class). */
	TSubclassOf<AFPSREnemyBase> PickEnemyClass(const FFPSREnemySpawnContext& Context) const;
};
