// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/Actor.h"
#include "Containers/ArrayView.h" // TConstArrayView (ComputeDamageStage)
#include "FPSRDestructible.generated.h"

class USceneComponent;
class UFPSREnemyHealthComponent;
class UFPSRDestructibleReward;

/** Base class for a destructible world prop (ADR 0010 D7 "파괴 가능 프롭 = 문의 일반화" — extracted from the
 *  original monolithic AFPSRDoor). Carries health (via UFPSREnemyHealthComponent, SetCountsAsKill(false) — takes
 *  damage through the EXISTING combat bridge with zero new damage code, but breaking it fires no kill credit /
 *  on-kill fragments / lifesteal, exactly like the pre-refactor door), progressive damage-stage presentation,
 *  replicated broken state (Push Model), and a polymorphic Instanced Rewards array (ADR 0010 D7) paid out on break.
 *
 *  C++ owns gameplay only (collision/visibility on break, health, replication); the mesh and the broken
 *  presentation stay content (Game.MD §2 — no hardcoded asset paths). This base deliberately creates NO mesh
 *  subobject: which part(s) of a destructible get hidden/collision-disabled on break differs by subclass
 *  (AFPSRDoor hides only its leaf and keeps the frame solid; a plain arena prop hides the whole actor) — see
 *  ApplyBrokenState.
 *
 *  A suppressor (stage-transition trigger) is planned as "a destructible whose one Reward IS a stage transition" —
 *  NOT built in this slice. Rewards is EditAnywhere specifically so that future case needs no new Blueprint. */
UCLASS()
class FPSROGUELITE_API AFPSRDestructible : public AActor
{
	GENERATED_BODY()

public:
	AFPSRDestructible();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintPure, Category = "FPSR|Destructible")
	bool IsBroken() const { return bBroken; }

	/** Test/introspection accessor — the automation test (FPSRoguelite.Destructible.Base) reads this off the CDO to
	 *  confirm a plain AFPSRDestructible authors no reward by default (pure obstacle; opt-in per placed instance). */
	const TArray<TObjectPtr<UFPSRDestructibleReward>>& GetRewards() const { return Rewards; }

	/** Pure, worldless: how many DamageStageThresholds (descending %) HealthPct is at-or-below. Extracted from the
	 *  original inline AFPSRDoor::HandleHealthChanged loop so it is unit-testable without a world (FPSRoguelite.
	 *  Destructible.Base). Order-independent for the COUNT; the 0-based index semantics assume descending order
	 *  (see DamageStageThresholds doc). Empty Thresholds -> always 0. */
	static int32 ComputeDamageStage(TConstArrayView<float> Thresholds, float HealthPct);

protected:
	virtual void BeginPlay() override;

	/** Rewards paid out when this destructible breaks (ADR 0010 D7). EditAnywhere — NOT EditDefaultsOnly — is what
	 *  lets a designer author a DIFFERENT reward on each PLACED instance without a new Blueprint subclass; that is
	 *  what will let a future "stage transition" reward (the suppressor slice) be just one entry in this array on
	 *  one specific placed actor, no new BP. Polymorphic Instanced, the same shape as UFPSRCardEffect's Effects
	 *  array one layer up (Card/FPSRCardDataAsset.h). */
	UPROPERTY(EditAnywhere, Instanced, Category = "FPSR|Destructible", meta = (DisplayName = "파괴 보상"))
	TArray<TObjectPtr<UFPSRDestructibleReward>> Rewards;

	/** Server: this destructible's health reached zero — mark broken (replicated), run the authority-only reaction
	 *  (HandleBrokenAuthority — Rewards; AFPSRDoor also opens the flow-field seam + streams in its target map
	 *  there), apply the broken presentation state, then fire the BP presentation. Bound to
	 *  UFPSREnemyHealthComponent::OnDeath. */
	UFUNCTION()
	void HandleBroken(AActor* DeadActor, AActor* Killer);

	UFUNCTION()
	void OnRep_Broken();

	/** Server: HealthComponent reported a (post-clamp) health change — advance the damage stage and fire the BP
	 *  presentation for any newly-crossed thresholds. Bound to UFPSREnemyHealthComponent::OnHealthChanged. */
	UFUNCTION()
	void HandleHealthChanged(float NewHealth, float MaxHealth);

	UFUNCTION()
	void OnRep_DamageStage(uint8 OldStage);

	/** Authority-only reaction to breaking, running BEFORE the broken presentation/collision state applies — a
	 *  subclass override may need collision or component state that ApplyBrokenState is about to tear down (see
	 *  AFPSRDoor's flow-field seam notify, which must read the door leaf's still-live collision). Base = pay out
	 *  every entry in Rewards (null entries skipped — an author left an empty Instanced slot). A subclass override
	 *  that adds its own authority-only work should call Super::HandleBrokenAuthority(Breaker) to still pay out
	 *  Rewards. Breaker is the actor whose hit finished this off (may be null / may not be a player) and is passed
	 *  through rather than stashed on the actor — it is only meaningful for the duration of this call. */
	virtual void HandleBrokenAuthority(AActor* Breaker);

	/** Disable collision + hide. Base = the WHOLE actor (SetActorEnableCollision(false) + SetActorHiddenInGame(true))
	 *  — appropriate for a destructible that IS its own visual (a crate, a prop). A subclass whose broken state
	 *  should only affect PART of itself (AFPSRDoor hides the leaf but keeps the frame solid/visible) overrides this
	 *  and does NOT call Super (Super would hide the parts it needs to keep). Runs on BOTH the server break path
	 *  (HandleBroken) and the client OnRep (OnRep_Broken) — the same call from both sides is what keeps a late
	 *  joiner and an already-connected client in the same visual state. */
	virtual void ApplyBrokenState();

	/** Fire the "broken" BP presentation. Base = OnDestructibleBroken(). AFPSRDoor overrides to fire OnDoorBroken()
	 *  INSTEAD (it does not call Super) so BP_Door's existing event binding is the only thing that fires and
	 *  nothing double-fires. Runs on both the server break path and the client OnRep. */
	virtual void FireBrokenPresentation();

	/** Fire the "damage stage crossed" BP presentation for one stage. Base = OnDestructibleDamageStage(...).
	 *  AFPSRDoor overrides to fire OnDoorDamageStage(...) instead, same non-additive override shape as
	 *  FireBrokenPresentation. Called by the private FireDamageStages loop below, once per crossed threshold. */
	virtual void FireDamageStagePresentation(int32 StageIndex, float HealthPct, float Threshold);

	/** Broken presentation (anim open / Chaos shatter / VFX) — BP-implemented so the visual is content, never a
	 *  C++ rebuild. Base event; AFPSRDoor fires its own OnDoorBroken instead (see FireBrokenPresentation). */
	UFUNCTION(BlueprintImplementableEvent, Category = "FPSR|Destructible")
	void OnDestructibleBroken();

	/** Progressive-damage presentation (crack / shake / tint), fired once per crossed threshold. StageIndex is
	 *  0-based into DamageStageThresholds (0 = first/highest threshold). Base event; AFPSRDoor fires
	 *  OnDoorDamageStage instead (see FireDamageStagePresentation). */
	UFUNCTION(BlueprintImplementableEvent, Category = "FPSR|Destructible")
	void OnDestructibleDamageStage(int32 StageIndex, float HealthPct, float Threshold);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FPSR|Destructible")
	TObjectPtr<UFPSREnemyHealthComponent> HealthComponent;

	/** Destructible durability (HP), applied to HealthComponent at BeginPlay. Tune per placed instance / BP (~
	 *  rifle N shots). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Destructible", meta = (ClampMin = "1.0"))
	float Durability = 150.0f;

	/** Remaining-health fractions (1..0) at which OnDestructibleDamageStage fires, in DESCENDING order. Default
	 *  {0.75, 0.5, 0.25, 0.05} = crack feedback at 75/50/25/5%. Designer-tunable per instance/BP (add/remove freely). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Destructible")
	TArray<float> DamageStageThresholds = {0.75f, 0.5f, 0.25f, 0.05f};

	UPROPERTY(ReplicatedUsing = OnRep_Broken)
	bool bBroken = false;

	/** Count of DamageStageThresholds crossed so far (server-computed, replicated so clients fire the same stages). */
	UPROPERTY(ReplicatedUsing = OnRep_DamageStage)
	uint8 DamageStage = 0;

private:
	/** Fire the damage-stage BP presentation for each stage in [FromStage, ToStage). CurrentPct >= 0 reports the
	 *  real percent (server); CurrentPct < 0 reports each stage's own threshold (client OnRep, which lacks exact
	 *  health). Dispatches through the virtual FireDamageStagePresentation so a subclass fires its own event. */
	void FireDamageStages(int32 FromStage, int32 ToStage, float CurrentPct);
};
