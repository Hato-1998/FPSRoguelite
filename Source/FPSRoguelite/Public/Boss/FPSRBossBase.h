// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AbilitySystemInterface.h"
#include "Boss/FPSRBossTypes.h"
#include "Combat/FPSRVitals.h"          // FFPSRDamageSpec (by value in FPendingLaserHit)
#include "GameFramework/Character.h"
#include "GameplayAbilitySpecHandle.h"  // FGameplayAbilitySpecHandle members
#include "FPSRBossBase.generated.h"

class UStaticMeshComponent;
class UFPSREnemyHealthComponent;
class UFPSRBossDefinitionDataAsset;
class UFPSRAbilitySystemComponent;
class UFPSRBossGameplayAbility;
class UAnimMontage;

/** Boss (U3/D4 scaffold + BOSS1 attack patterns). A health-only target that closes the run's victory path, now with
 *  a server-authoritative pattern driver on top. Reuses the swarm's UFPSREnemyHealthComponent so EVERY existing
 *  weapon path (hitscan / projectile / charge-laser / melee / explosion) deals damage, crits, friendly-fire and
 *  weakpoints with ZERO new damage code — the combat bridge identifies a damageable enemy by that component, not by
 *  class (FPSRCombatStatics ResolveDamage/ApplyDamage). On death it asks the GameMode to end the run in Victory
 *  (loose coupling, mirroring U2's player-defeat path).
 *
 *  Base class = engine ACharacter (NOT AFPSRCharacter — that is the player; inheriting it would route the boss
 *  through the friendly-fire branch). Movement stays disabled: every BOSS1 pattern is designed around a STATIONARY
 *  central tower (25 m x 50 m at arena centre), and AI navigation / StateTree remain out of scope.
 *
 *  🔴 **Freeze.** §2-2's global level-up freeze is a STATE GATE, not TimeDilation, and the engine runs GE
 *  duration/period and AbilityTasks on the world FTimerManager regardless of it. So every pattern timing here comes
 *  from exactly two places: Tick's DeltaSeconds (which simply never arrives while frozen, because Tick returns
 *  early) and the freeze-paused combat clock (AFPSRGameState::GetCombatClockSeconds, VIT1). There is no
 *  FTimerManager use anywhere in the boss pattern system, and the ASC additionally refuses time-based GEs outright
 *  (UFPSRAbilitySystemComponent::EnableTimeAxisGuard).
 *
 *  Collision: the capsule's object type is ECC_Pawn (mirroring swarm enemies). It MUST NOT be WorldStatic — the
 *  hitscan wall trace would then treat the boss body as a wall and block its own bullets (P7 §6, "most common
 *  trap"). The boss is a pawn but receives no knockback: the knockback dispatch keys on AFPSRCharacter (player)
 *  and AFPSREnemyBase (swarm), so a boss falls through to no-op — intended (bosses aren't punted around).
 *
 *  Spec: `Docs/Specs/BOSS1_AbilityPatternFramework.md`. */
UCLASS()
class FPSROGUELITE_API AFPSRBossBase : public ACharacter, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	AFPSRBossBase();

	//~IAbilitySystemInterface
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	//~End IAbilitySystemInterface

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** The boss's non-GAS health component — exposed so the HUD boss bar (B11) can bind OnHealthChanged (now
	 *  client-fired via B12) and read GetHealth()/GetMaxHealth(). */
	UFUNCTION(BlueprintPure, Category = "FPSR|Boss")
	UFPSREnemyHealthComponent* GetHealthComponent() const { return HealthComponent; }

	/** Server: apply a boss definition's tuning (MaxHealth, phase thresholds) to this instance. Called by the run
	 *  director right after spawn. Overrides DefaultMaxHealth. No-op off-authority / null definition. */
	void InitializeFromDefinition(const UFPSRBossDefinitionDataAsset* Definition);

	/** Play a montage on the boss's SKELETAL mesh (the inherited ACharacter Mesh — the boss BP assigns Prime_Helix +
	 *  its AnimBP there; U20 "boss skeletal" seam). The reusable animation entry point for future boss abilities / AI
	 *  and the death anim. No-op when the mesh has no AnimInstance (i.e. before content assigns a skeletal mesh/AnimBP)
	 *  or when Montage is null. Cosmetic. */
	UFUNCTION(BlueprintCallable, Category = "FPSR|Boss")
	void PlayBossMontage(UAnimMontage* Montage, float PlayRate = 1.0f);

	// ---- Phase --------------------------------------------------------------------------------------------------

	/** 1-based. Replicated so the HUD and BP cosmetics can read it.
	 *  🔴 There are deliberately NO Boss.Phase.* loose gameplay tags: the phase COUNT is data (the length of
	 *  UFPSRBossDefinitionDataAsset::PhaseHealthThresholds), and a fixed tag set would put that count back into C++
	 *  as a constant. An int plus each ability's MinPhase is the whole gate. */
	UFUNCTION(BlueprintPure, Category = "FPSR|Boss")
	int32 GetCurrentPhase() const { return CurrentPhase; }

	// ---- Pattern clock ------------------------------------------------------------------------------------------

	/** The freeze-paused clock every pattern times against, in seconds.
	 *  Server: AFPSRGameState::GetCombatClockSeconds() — VIT1's tick-free global clock.
	 *  Client: GetCombatClockSecondsForClients() plus a small visual lead (see MaxClientVisualLeadSeconds).
	 *
	 *  🔴 The clock VALUE is not replicated. Clients derive it from the two freeze anchors the GameState replicates
	 *  on freeze edges only. A per-frame mirror would need an anchor + receipt time on every client and would still
	 *  drift between updates; deriving costs 8 bytes per pause/unpause and is exact at join-in-progress. */
	UFUNCTION(BlueprintPure, Category = "FPSR|Boss")
	float GetPatternClockSeconds() const;

	// ---- Sweeping laser (S2 consumes these; declared here so the header changes once) ------------------------------

	/** Cosmetic beam state. Clients feed OutBaseAngleDeg straight into their beam mesh; the angle is recomputed from
	 *  (start angle, speed, start clock) rather than integrated, so it cannot drift away from the server's. */
	UFUNCTION(BlueprintPure, Category = "FPSR|Boss")
	bool GetBeamState(int32& OutBeamCount, float& OutBaseAngleDeg, bool& bOutWarmup) const;

	/** Server: publish/clear beam state. BeamCount == 0 clears it.
	 *  VisualHeightCm rides along purely so every machine draws the beam at the SAME height the pattern authored —
	 *  the hit test does not use it (that is decided by "were you airborne"), but a debug or Blueprint beam drawn at
	 *  a different height than the authored one would teach players the wrong dodge. */
	void ServerSetBeamState(int32 InBeamCount, float StartAngleDeg, float SpeedDegPerSec, float StartClock,
		float WarmupEndClock, float VisualHeightCm);

	/** Server: true when Pawn was airborne at any point within AirborneGraceSeconds — the "late landing" half of the
	 *  jump window. The "late jump" half is handled by deferring the hit (ServerScheduleLaserHit). */
	bool WasRecentlyAirborne(const APawn* Pawn) const;

	/** Server: book a laser hit to be settled LateJumpGraceSeconds later, cancelled if the target goes airborne in
	 *  the meantime.
	 *  🔴 Why deferred: even with a perfectly synced beam, a client's jump input reaches the server one-way-latency
	 *  late. WasRecentlyAirborne only opens the "already jumped" direction; seeing the "jumped a moment too late"
	 *  direction requires waiting. The list holds at most (beams x survivors) entries. */
	void ServerScheduleLaserHit(APawn* Target, float Damage, const FFPSRDamageSpec& Spec);

	// ---- Delayed blasts (barrage) ---------------------------------------------------------------------------------

	/** Read by client Blueprints every frame (immediate mode — see FFPSRBossBlastMark's doc). */
	UFUNCTION(BlueprintPure, Category = "FPSR|Boss")
	const TArray<FFPSRBossBlastMark>& GetBlastMarks() const { return BlastMarks; }

	/** Server: book a blast. Over MaxConcurrentMarks the OLDEST is fizzled (removed without detonating) and a warning
	 *  is logged once — authored values that pass IsDataValid never reach that path.
	 *
	 *  The damage payload travels WITH the marker instead of being read back at detonation time: the pattern that
	 *  booked it may well have ended (or been cancelled) before the fuse runs out, and a blast that quietly picked
	 *  up some other pattern's numbers would be a miserable thing to track down. Those fields are server-only —
	 *  see FFPSRBossBlastMark. */
	void ServerAddBlastMark(const FVector& Center, float Radius, float FuseSeconds, APawn* TargetPawn,
		float Damage, float KnockbackStrength, const FFPSRDamageSpec& Spec);

	// ---- Owned actors -----------------------------------------------------------------------------------------------

	/** Server: register an actor this boss spawned (homing orbs). One list drives BOTH the freeze-edge push and
	 *  teardown, so a pattern actor never has to poll the freeze state itself. */
	void ServerRegisterPatternActor(AActor* Actor);

	// ---- Pattern stage / target announcement --------------------------------------------------------------------

	/** Server: publish which part of a pattern the boss is in, so Blueprints can play a wind-up and a recovery.
	 *  Called only from UFPSRBossGameplayAbility's stage machine — one publisher, so a new stage cannot be added
	 *  without clients hearing about it. */
	void ServerSetPatternStage(EFPSRBossPatternStage NewStage);

	UFUNCTION(BlueprintPure, Category = "FPSR|Boss")
	EFPSRBossPatternStage GetPatternStage() const { return PatternStage; }

	/** Server: announce (or clear) the player a pattern has singled out. Replicated because the design calls for
	 *  EVERYONE to see who is marked — that is what turns "one player is hunted" into a party problem rather than a
	 *  private one. */
	void ServerSetMarkedPlayer(APawn* Pawn);

	/** 🔴 The mark travels as a PLAYER STATE, not a pawn. Player pawns are not always-relevant, so on a 160 m arena a
	 *  teammate more than the net-cull distance away would receive the announcement as null — the one player who most
	 *  needs to know "it is hunting someone over there" is the one furthest from them. PlayerStates are always
	 *  relevant, so the announcement reaches everyone. */
	UFUNCTION(BlueprintPure, Category = "FPSR|Boss")
	APlayerState* GetMarkedPlayerState() const { return MarkedPlayerState; }

	/** Convenience: the marked pawn IF it is relevant on this machine (null on a client too far to see it, even
	 *  though the mark itself is known). Draw the marker off the PlayerState when this is null. */
	UFUNCTION(BlueprintPure, Category = "FPSR|Boss")
	APawn* GetMarkedPawn() const;

#if !UE_BUILD_SHIPPING
	/** Debug (FPSR.BossPattern): force-activate the pattern at Index in GrantedAbilities, bypassing cooldown/gap.
	 *  Without this a slice cannot be verified in PIE without waiting out the authored cooldowns. */
	void DebugForcePattern(int32 Index);

	/** Debug (FPSR.BossPhase): jump straight to a phase (monotonic — cannot go back down). */
	void DebugSetPhase(int32 Phase);
#endif

protected:
	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Server: boss died — end the run in Victory via the GameMode (loose coupling; U2 NotifyPlayerDefeated mirror).
	 *  Also closes every pattern this boss owns: the boss is deliberately NOT destroyed on death (it stays standing
	 *  for the result beat), so without this a corpse would keep shelling the players after the win. */
	UFUNCTION()
	void HandleDeath(AActor* DeadActor, AActor* Killer);

	/** Play the boss death montage (U20). Bound to the health component's OnDeathCosmetic so CLIENTS play it on the
	 *  replicated death edge; also invoked from HandleDeath so the listen-server host / standalone play it. */
	UFUNCTION()
	void HandleDeathCosmetic();

	/** Server: phase check on every applied hit. Bound to the health component's OnHealthChanged. */
	UFUNCTION()
	void HandleHealthChanged(float NewHealth, float MaxHealth);

	UFUNCTION()
	void OnRep_CurrentPhase();

	UFUNCTION()
	void OnRep_BeamState();

	UFUNCTION()
	void OnRep_PatternStage();

	UFUNCTION()
	void OnRep_MarkedPlayer();

	// ---- Cosmetic hooks: presentation is 100% Blueprint (there is no decal/Niagara call anywhere in Source/) -------

	UFUNCTION(BlueprintImplementableEvent, Category = "FPSR|Boss")
	void OnPhaseChangedCosmetic(int32 NewPhase);

	UFUNCTION(BlueprintImplementableEvent, Category = "FPSR|Boss")
	void OnBeamStateChangedCosmetic(int32 InBeamCount, bool bWarmup);

	/** Wind-up / recovery presentation. Fires on clients AND on the authority (the host gets no OnRep). */
	UFUNCTION(BlueprintImplementableEvent, Category = "FPSR|Boss")
	void OnPatternStageChangedCosmetic(EFPSRBossPatternStage NewStage);

	/** Who is being hunted right now (null = nobody). Everyone sees this — that is the point. */
	UFUNCTION(BlueprintImplementableEvent, Category = "FPSR|Boss")
	void OnMarkedPlayerChangedCosmetic(APlayerState* NewMarked);

	/** Optional montage played on the boss skeletal mesh on death (U20). Null = none (null-safe). Content-assigned. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Boss")
	TSoftObjectPtr<UAnimMontage> DeathMontage;

	/** Visible placeholder box (no collision — the capsule handles hits). Designers swap the mesh in the boss BP. */
	UPROPERTY(VisibleAnywhere, Category = "FPSR|Boss")
	TObjectPtr<UStaticMeshComponent> BodyMesh;

	/** Non-GAS health (shared swarm component) — the single reason every weapon path damages the boss for free. */
	UPROPERTY(VisibleAnywhere, Category = "FPSR|Boss")
	TObjectPtr<UFPSREnemyHealthComponent> HealthComponent;

	/** Owner == Avatar == this actor, replication mode Minimal (the boss has no owning client at all) — the same
	 *  ownership shape AFPSREnemyEliteBase uses, and for the same reason. No AttributeSet: health stays in
	 *  HealthComponent so the existing damage bridge keeps working untouched. */
	UPROPERTY(VisibleAnywhere, Category = "FPSR|Boss")
	TObjectPtr<UFPSRAbilitySystemComponent> AbilitySystem;

	/** Max health used when no BossDefinition overrides it (the C++ fallback boss for FPSR.SkipToBoss before any
	 *  boss BP/definition exists). Kept testable so the kill->victory loop is verifiable in a PIE session — the
	 *  real boss's larger health is authored on its DA_BossDefinition (U4). Balance value. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Boss", meta = (ClampMin = "1.0"))
	float DefaultMaxHealth = 1000.0f;

	// ---- Pattern authoring ---------------------------------------------------------------------------------------

	/** Content authors the boss's patterns here; the selector round-robins this order. Granted once in BeginPlay on
	 *  the server (unlike elites, the boss is never pooled, so there is no re-grant cycle). */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Boss|Patterns")
	TArray<TSubclassOf<UFPSRBossGameplayAbility>> GrantedAbilities;

	/** What makes the boss start its next pattern. Patterns do NOT run back to back — after a pattern's recovery the
	 *  boss idles until one of these fires (§14-3).
	 *  🔴 An EMPTY array would mean a boss that never attacks again, with no error anywhere. So an empty array falls
	 *  back to one repeating Elapsed trigger of PatternGapSeconds, and IsDataValid warns about it. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Boss|Patterns")
	TArray<FFPSRBossPatternTrigger> PatternTriggers;

	/** Which pattern to start when a trigger fires. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Boss|Patterns")
	EFPSRBossPatternSelection SelectionPolicy = EFPSRBossPatternSelection::Sequential;

	/** Fallback cadence used only when PatternTriggers is empty (see that field). */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Boss|Patterns", meta = (ClampMin = "0.0"))
	float PatternGapSeconds = 6.0f;

	/** How long after leaving the ground a pawn still counts as airborne (covers a late landing). */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Boss|Patterns", meta = (ClampMin = "0.0"))
	float AirborneGraceSeconds = 0.12f;

	/** How long a laser hit waits before it is settled, so a jump that arrives a moment late still counts. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Boss|Patterns", meta = (ClampMin = "0.0"))
	float LateJumpGraceSeconds = 0.15f;

	/** Hard cap on live blast markers. Authored values that pass IsDataValid stay well under it. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Boss|Patterns", meta = (ClampMin = "1"))
	int32 MaxConcurrentMarks = 32;

	/** Upper bound on how far ahead a client draws the beam to cancel its own view lag. Purely cosmetic — damage is
	 *  100% server-authoritative. 0 disables the compensation. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Boss|Patterns", meta = (ClampMin = "0.0", ClampMax = "0.3"))
	float MaxClientVisualLeadSeconds = 0.15f;

#if !UE_BUILD_SHIPPING
	/** Draw the pattern state as debug shapes when FPSR.BossDebugDraw is on. Runs on EVERY machine — see
	 *  FPSRBoss::IsDebugDrawEnabled for why the client half is the point rather than an extra. */
	void DebugDrawPatterns() const;
#endif

private:
	/** Server: everything this boss owns, released together. Called from HandleDeath, EndPlay and the run-end branch
	 *  of Tick — "nothing of the boss outlives the boss" has to be one function or the list drifts. */
	void ServerReleaseAllPatternState();

	/** Server: settle any deferred laser hits whose grace has elapsed. */
	void ServerSettlePendingLaserHits();

	/** Server: detonate every marker whose fuse has run out. */
	void ServerDetonateDueBlastMarks();

	/** Server: pick and activate the next eligible pattern, if any. */
	void ServerTryActivateNextPattern();

	/** Server: has any trigger fired this tick? Advances each trigger's own latch. */
	bool ServerConsumeAnyTrigger();

	/** Server: push the freeze state to every owned actor once, on the edge. */
	void ServerPushSimulationPaused(bool bPaused);

	UPROPERTY(ReplicatedUsing = OnRep_CurrentPhase)
	int32 CurrentPhase = 1;

	UPROPERTY(Replicated)
	TArray<FFPSRBossBlastMark> BlastMarks;

	/** 🔴 ONE RepNotify property, not six.
	 *  Six properties sharing a single ReplicatedUsing does NOT mean one call: the engine queues RepNotifies per
	 *  PROPERTY and invokes the function once per queued entry, so a beam starting (5-6 fields changing at once)
	 *  fired OnRep_BeamState five or six times on a client while the authority path fired it exactly once. A
	 *  Blueprint spawning a sound or a Niagara burst on that event would stack five on clients and one on the host —
	 *  the very host/client asymmetry this class keeps closing everywhere else.
	 *  Only BeamCount carries the notify; the rest ride the same bunch, so their values have already landed when it
	 *  runs. Beam state is only ever published as a whole (ServerSetBeamState), so there is no partial-update case. */
	UPROPERTY(ReplicatedUsing = OnRep_BeamState)
	int32 BeamCount = 0;

	UPROPERTY(Replicated)
	float BeamStartAngleDeg = 0.0f;

	UPROPERTY(Replicated)
	float BeamSpeedDegPerSec = 0.0f;

	UPROPERTY(Replicated)
	float BeamStartClock = 0.0f;

	UPROPERTY(Replicated)
	float BeamWarmupEndClock = 0.0f;

	UPROPERTY(Replicated)
	float BeamVisualHeightCm = 60.0f;

	UPROPERTY(ReplicatedUsing = OnRep_PatternStage)
	EFPSRBossPatternStage PatternStage = EFPSRBossPatternStage::Finished;

	UPROPERTY(ReplicatedUsing = OnRep_MarkedPlayer)
	TObjectPtr<APlayerState> MarkedPlayerState = nullptr;

	/** Pattern-clock stamp the boss fight began at — the origin for Elapsed triggers. */
	float FightStartClock = -1.0f;

	/** How many patterns have run to completion. The stand-in for "after N attacks" until a basic attack exists. */
	int32 PatternsPerformed = 0;

	/** Cached from the definition asset. Empty = a single-phase boss. */
	TArray<float> PhaseThresholds;

	/** Actors this boss spawned (homing orbs). Weak so a destroyed orb self-clears. */
	TArray<TWeakObjectPtr<AActor>> SpawnedPatternActors;

	struct FPendingLaserHit
	{
		TWeakObjectPtr<APawn> Target;
		float DueClock = 0.0f;
		float Damage = 0.0f;
		/** Carried so the laser keeps its DamageType — without it this pattern alone would lose the per-layer
		 *  defence axis VIT1 opened (FFPSRDamageSpec). */
		FFPSRDamageSpec Spec;
	};
	TArray<FPendingLaserHit> PendingLaserHits;

	/** Last pattern-clock time each pawn was airborne. Pruned with the pending list. */
	TMap<TWeakObjectPtr<APawn>, float> LastAirborneClock;

	TArray<FGameplayAbilitySpecHandle> GrantedAbilityHandles;
	FGameplayAbilitySpecHandle ActivePatternHandle;
	int32 NextPatternIndex = 0;
	bool bWasFrozenLastTick = false;
	bool bPatternStateReleased = false;
};
