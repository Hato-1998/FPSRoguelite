// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AbilitySystem/Abilities/FPSRBossGameplayAbility.h"
#include "GameplayTagContainer.h"
#include "FPSRBossGA_HomingOrbs.generated.h"

class AFPSRBossHomingOrb;

/** BOSS1 pattern 3 — launch a flight of homing orbs at ONE player (`Docs/Specs/BOSS1_AbilityPatternFramework.md`).
 *  Each orb has its own health and can be shot down, which is the whole point: this is the pattern the party can
 *  answer with fire rather than with footwork.
 *
 *  The ability ends as soon as the last orb is away — it does NOT wait for them to resolve. Orbs own their own
 *  lifetime, and holding the ability open would park the boss doing nothing while they fly, which reads as the boss
 *  being broken rather than as a pause. The boss keeps them in its owned-actor list, so they still get the freeze
 *  edge pushed to them and are cleaned up with everything else if it dies. */
UCLASS()
class FPSROGUELITE_API UFPSRBossGA_HomingOrbs : public UFPSRBossGameplayAbility
{
	GENERATED_BODY()

public:
	UFPSRBossGA_HomingOrbs();

protected:
	virtual void ServerBeginExecute() override;
	virtual bool ServerTickExecute(float DeltaSeconds) override;
	virtual void ServerEndExecute() override;

	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Boss|Orbs", meta = (ClampMin = "1", ClampMax = "8"))
	int32 OrbCount = 5;

	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Boss|Orbs", meta = (ClampMin = "1.0"))
	float OrbHealth = 150.0f;

	/** How long each orb steers before it gives up and flies straight. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Boss|Orbs", meta = (ClampMin = "0.1"))
	float TrackSeconds = 8.0f;

	/** Spacing between spawns. 0 forms the whole ring on one frame — staggering reads better. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Boss|Orbs", meta = (ClampMin = "0.0"))
	float SpawnIntervalSeconds = 0.25f;

	/** The orbs form a RING around the boss and hover there before committing (the design's "보스 주위로 투사체가
	 *  만들어지고"). A ring rather than a fan aimed at the target, so the party reads "the boss is loading up" from
	 *  any angle instead of only from where the marked player happens to be standing. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Boss|Orbs", meta = (ClampMin = "0.0"))
	float SpawnRingRadiusCm = 2200.0f;

	/** Height above the boss origin the ring forms at. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Boss|Orbs")
	float SpawnHeightCm = 1500.0f;

	/** How long each orb hovers in the ring before it starts chasing. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Boss|Orbs", meta = (ClampMin = "0.0"))
	float OrbGraceSeconds = 0.8f;

	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Boss|Orbs")
	float Damage = 20.0f;

	/** Blast radius on impact. Every player inside takes it — that is what makes bunching up (or crowding a downed
	 *  teammate the orbs are heading for) the wrong answer. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Boss|Orbs", meta = (ClampMin = "1.0"))
	float BlastRadiusCm = 400.0f;

	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Boss|Orbs", meta = (ClampMin = "0.0"))
	float BlastKnockback = 0.0f;

	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Boss|Orbs")
	FGameplayTag DamageType;

	/** Content-assigned orb Blueprint. Null = nothing is launched (logged once) — no hardcoded asset path here. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Boss|Orbs")
	TSubclassOf<AFPSRBossHomingOrb> OrbClass;

private:
	/** Server: launch orb number Index of OrbCount at the chosen target. */
	void SpawnOrb(class AFPSRBossBase* Boss, int32 Index);

	/** Chosen once at activation — the user's design is "one player is hunted", so re-picking per orb would turn it
	 *  into a spread attack and remove the reason teammates would help. */
	TWeakObjectPtr<APawn> ChosenTarget;

	float SpawnAccumulatorSeconds = 0.0f;
	int32 NumOrbsSpawned = 0;
};
