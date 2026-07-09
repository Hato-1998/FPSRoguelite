// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/PlayerState.h"
#include "AbilitySystemInterface.h"
#include "Abilities/GameplayAbilityTypes.h"
#include "GameplayTagContainer.h"
#include "Weapon/FPSRWeaponTypes.h"
#include "FPSRPlayerState.generated.h"

class UFPSRAbilitySystemComponent;
class UFPSRHealthSet;
class UFPSRCombatSet;
class UAbilitySystemComponent;
class UFPSRWeaponDataAsset;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnCardPicksChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnRerollChargesChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnLoadoutChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnReadyChanged);

/** Per-player life state (U9 DBNO, Phase 1B). The run keys off IsAlive() == (LifeState == Alive):
 *  - Alive: full participant.
 *  - DBNO (Down But Not Out): downed but revivable — NOT a live participant for the wipe / card-select freeze (A4) /
 *    enemy targeting (B17). The pawn persists (crawl only); an ally's proximity revives it (UFPSRReviveComponent).
 *  - Dead: out of the run (team-wipe, or [후속] bleedout).
 *  Lives on the PlayerState so it survives pawn death and the wipe aggregation is independent of pawn validity. */
UENUM(BlueprintType)
enum class EFPSRLifeState : uint8
{
	Alive,
	DBNO,
	Dead
};

/** PlayerState owns the AbilitySystemComponent and global attribute sets (co-op / revive friendly). */
UCLASS()
class FPSROGUELITE_API AFPSRPlayerState : public APlayerState, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	AFPSRPlayerState();

	//~IAbilitySystemInterface
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	//~End IAbilitySystemInterface

	UFPSRAbilitySystemComponent* GetFPSRAbilitySystemComponent() const { return AbilitySystemComponent; }
	UFPSRHealthSet* GetHealthSet() const { return HealthSet; }
	UFPSRCombatSet* GetCombatSet() const { return CombatSet; }

	/** Current life state (U9 DBNO state machine). */
	UFUNCTION(BlueprintPure, Category = "FPSR|Run")
	EFPSRLifeState GetLifeState() const { return LifeState; }

	/** True while this player is a LIVE participant (Alive only). The single predicate the run uses for the wipe
	 *  check, the card-select freeze gate (A4) and enemy targeting (B17) — DBNO and Dead both count as not-alive. */
	UFUNCTION(BlueprintPure, Category = "FPSR|Run")
	bool IsAlive() const { return LifeState == EFPSRLifeState::Alive; }

	/** True only when truly OUT of the run (team-wipe / bleedout) — NOT while DBNO (downed-but-revivable). */
	UFUNCTION(BlueprintPure, Category = "FPSR|Run")
	bool IsDead() const { return LifeState == EFPSRLifeState::Dead; }

	/** True while downed (Down But Not Out) — revivable, pawn persists (crawl only). */
	UFUNCTION(BlueprintPure, Category = "FPSR|Run")
	bool IsDBNO() const { return LifeState == EFPSRLifeState::DBNO; }

	/** Server: set the life state. Idempotent. Replicates to all (owning client gates input via OnRep_LifeState). */
	void SetLifeState(EFPSRLifeState NewState);

	UFUNCTION(BlueprintPure, Category = "FPSR|Run")
	int32 GetRunRerollCharges() const { return RunRerollCharges; }

	UPROPERTY(BlueprintAssignable, Category = "FPSR|Run")
	FOnRerollChargesChanged OnRerollChargesChanged;

	/** Server: consume one reroll charge. Returns true if successful. */
	bool ConsumeRerollCharge();

	/** Server: reset reroll charges to default. */
	void ResetRerollCharges();

	/** Server: set reroll charges to a specific value. */
	void SetRerollCharges(int32 NewCharges);

	UFUNCTION(BlueprintPure, Category = "FPSR|Run")
	int32 GetCardPicksPending() const { return CardPicksPending; }

	/** Server: add one pending card pick (granted when party levels up). */
	void AddCardPick();

	/** Server: consume one pending card pick. Returns true if successful. */
	bool ConsumeCardPick();

	/** Pending weapon-unlock picks (granted on mission clear + level 20/30/40 milestones; selected at the freeze). */
	UFUNCTION(BlueprintPure, Category = "FPSR|Run")
	int32 GetWeaponUnlockPicksPending() const { return WeaponUnlockPicksPending; }

	/** Server: add one pending weapon-unlock pick. */
	void AddWeaponUnlockPick();

	/** Server: consume one pending weapon-unlock pick. Returns true if successful. */
	bool ConsumeWeaponUnlockPick();

	/** AllWeapons-scope stat modifiers (apply to every owned weapon). Lives on the PlayerState so it is
	 *  character-wide and survives pawn respawn, consistent with the run state (CardPicksPending). */
	const FFPSRWeaponModContainer& GetAllWeaponsMods() const { return AllWeaponsMods; }

	/** Server: append an AllWeapons stat modifier (from an AllWeapons-scope card). Marks the owning pawn's
	 *  weapon instances' resolved-stat caches dirty. */
	void AddAllWeaponsModifier(const FFPSRWeaponStatMod& Mod);

	/** The weapon this player picked in the lobby (P7 §3-8). Read by AFPSRCharacter::PossessedBy to grant the
	 *  single run weapon; null = fall back to the character BP's default loadout (e.g. debug straight-to-gameplay).
	 *  Survives the lobby->gameplay seamless travel via CopyProperties. */
	UFUNCTION(BlueprintPure, Category = "FPSR|Loadout")
	UFPSRWeaponDataAsset* GetSelectedWeapon() const { return SelectedWeapon; }

	/** Server: set the lobby-chosen weapon (validated against the loadout pool by ServerSelectLoadoutWeapon). */
	void SetSelectedWeapon(UFPSRWeaponDataAsset* Weapon);

	/** Lobby ready state (U11a). The host-only "Start" gate is replaced by a per-player ready: the server starts
	 *  the run once every participant is ready (AFPSRLobbyGameMode::NotifyReadyChanged). Lives on the PlayerState so
	 *  the lobby UI / podium can read it for all players, and it resets to false on each lobby (re)entry. */
	UFUNCTION(BlueprintPure, Category = "FPSR|Lobby")
	bool IsReady() const { return bReady; }

	/** Server: set the lobby ready state. Idempotent. SetReady(true) is rejected unless a loadout weapon is chosen
	 *  (no ready-with-empty-hands). Replicates to all so every client's lobby list reflects each player's state. */
	void SetReady(bool bNewReady);

	/** Owning client: this player's ready state changed (drives the local ready button) — also broadcast on the
	 *  listen-server host directly from SetReady since the host gets no OnRep. */
	UPROPERTY(BlueprintAssignable, Category = "FPSR|Lobby")
	FOnReadyChanged OnReadyChanged;

	/** Lobby podium seat index (0..NumPodiumSlots-1), server-assigned on lobby entry so each co-op player occupies a
	 *  distinct podium slot (B3b — replaces the engine's random ChoosePlayerStart that let two players share a spot).
	 *  INDEX_NONE until assigned. Replicated so clients can map seat->podium for per-seat cosmetics; placement itself
	 *  is server-only (AFPSRLobbyGameMode::ChoosePlayerStart). Lobby-only — reset each lobby entry, not carried into the run. */
	UFUNCTION(BlueprintPure, Category = "FPSR|Lobby")
	int32 GetLobbySeatIndex() const { return LobbySeatIndex; }

	/** Server: set this player's lobby podium seat (B3b). Idempotent; replicates to all. */
	void SetLobbySeatIndex(int32 NewSeat);

	/** The map this player currently occupies (multimap Tier 0 — COMMITTED occupancy). Server-authoritative; replicated to
	 *  all for UI / late-join. This is the low-churn "settled" occupancy the allocator counts + the combat cross-map gate
	 *  reads — assigned only when the player has definitively settled in a map (not the per-frame boundary AABB). Grace /
	 *  instantaneous occupancy is a separate SERVER-ONLY notion in the stream/allocator subsystem (never replicated). An
	 *  unset tag = the Default single-map field (single-map play is unchanged). */
	UFUNCTION(BlueprintPure, Category = "FPSR|Run")
	FGameplayTag GetCurrentMapId() const { return CurrentMapId; }

	/** Server: set the committed occupancy map. Idempotent (low-churn); replicates to all. */
	void SetCurrentMapId(const FGameplayTag& NewMapId);

	// --- U (P-F) topology late-join ack (server-only, NOT replicated) — a client must confirm the current flow-field
	//     topology generation before the swarm targets / the allocator counts it, so a late joiner (or a client mid door-
	//     open) doesn't act on a topology it hasn't seen. Host/standalone owning controller is local authority = the server
	//     itself, so it is instantly satisfied. Single-map: generation stays 0, so the gen-0 ack clears the gate at once. ---

	/** Server: stamp the topology generation this player entered the current run's topology at (first sighting; idempotent
	 *  — only sets when unset). Starts the fail-open clock. Server-only. */
	void MarkTopologyJoin(int32 Gen, float Now);

	/** Server: record the client's acknowledgement of topology generation Gen (monotone max — an ack never regresses). */
	void SetAckedTopologyGeneration(int32 Gen);

	/** Server: has this player acknowledged the generation it joined at? The gate the allocator/movement use to seal a
	 *  late joiner out until it confirms the current topology. Not-yet-marked = fail-closed; else acked, or a logged
	 *  fail-open past the timeout (a lost ack RPC must not softlock). Server-only. */
	bool HasAckedJoinTopology(float Now) const;

	/** Server: reset the topology ack state for a fresh / same-world re-run (StartRun / lobby entry). */
	void ResetTopologyAck();

	/** U (P-F) pure decision (unit-testable, headless via FPSRoguelite.Allocator): given the owning-controller local-
	 *  authority flag, the acked/join generations, the join time, now, and the fail-open timeout — is the ack satisfied?
	 *  local authority -> true; unmarked (JoinGen < 0) -> false (leading-edge seal); else AckedGen >= JoinGen OR timed out. */
	static bool IsTopologyAckSatisfied(bool bLocalAuthority, int32 AckedGen, int32 JoinGen, float JoinTime, float Now, float Timeout);

	/** Server: track a passive ability granted by a character-passive card (U18c), so the run-reset can clear it.
	 *  bIsDamageEventListener bumps the DealtDamage listener count (drives the cheap ApplyDamage event-send gate so
	 *  players without such a passive pay nothing on the hot damage path). Idempotent per handle is the caller's job. */
	void AddCardGrantedAbility(FGameplayAbilitySpecHandle Handle, bool bIsDamageEventListener);

	/** Server: true if any granted passive listens for GameplayEvent.Player.DealtDamage (lifesteal etc.). Gates the
	 *  per-hit event send in FPSRCombat::ApplyDamage — 0-cost for players who never picked such a card. */
	bool HasDamageEventListeners() const { return DamageEventListenerCount > 0; }

	/** Server: reset all per-run progression to a fresh-run baseline (called on lobby entry, P7 §3-6). Clears
	 *  life state, pending picks, AllWeapons modifiers, the loadout pick, and card-granted passive abilities. XP/
	 *  PartyLevel reset naturally via the fresh GameState on each map; weapon inventory resets via pawn respawn.
	 *  Run on the server (authority). */
	void ResetRunState();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;

	/** Carry seamless-travel-persistent fields (the loadout pick) to the new PlayerState when traveling
	 *  lobby->gameplay. Run-progression fields are intentionally NOT carried (reset on lobby entry anyway). */
	virtual void CopyProperties(APlayerState* PlayerState) override;

	UPROPERTY(BlueprintAssignable, Category = "FPSR|Run")
	FOnCardPicksChanged OnCardPicksChanged;

	/** Owning client: the loadout selection replicated/changed — lobby UI refreshes its highlight. */
	UPROPERTY(BlueprintAssignable, Category = "FPSR|Loadout")
	FOnLoadoutChanged OnLoadoutChanged;

protected:
	UFUNCTION()
	void OnRep_LifeState();

	UFUNCTION()
	void OnRep_RunRerollCharges();

	UFUNCTION()
	void OnRep_CardPicksPending();

	UFUNCTION()
	void OnRep_AllWeaponsMods();

	UFUNCTION()
	void OnRep_SelectedWeapon();

	UFUNCTION()
	void OnRep_Ready();

private:
	UPROPERTY(VisibleAnywhere, Category = "FPSR|Abilities")
	TObjectPtr<UFPSRAbilitySystemComponent> AbilitySystemComponent;

	UPROPERTY()
	TObjectPtr<UFPSRHealthSet> HealthSet;

	UPROPERTY()
	TObjectPtr<UFPSRCombatSet> CombatSet;

	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Run")
	int32 DefaultRerollCharges = 3;

	UPROPERTY(ReplicatedUsing = OnRep_LifeState)
	EFPSRLifeState LifeState = EFPSRLifeState::Alive;

	UPROPERTY(ReplicatedUsing = OnRep_RunRerollCharges)
	int32 RunRerollCharges = 3;

	UPROPERTY(ReplicatedUsing = OnRep_CardPicksPending)
	int32 CardPicksPending = 0;

	UPROPERTY(ReplicatedUsing = OnRep_CardPicksPending)
	int32 WeaponUnlockPicksPending = 0;

	UPROPERTY(ReplicatedUsing = OnRep_AllWeaponsMods)
	FFPSRWeaponModContainer AllWeaponsMods;

	/** Lobby loadout pick (the single run weapon). Hard ref — weapon DataAssets are always-loaded primary assets,
	 *  so this replicates cleanly. ReplicatedUsing drives the owning-client lobby UI refresh. */
	UPROPERTY(ReplicatedUsing = OnRep_SelectedWeapon)
	TObjectPtr<UFPSRWeaponDataAsset> SelectedWeapon;

	/** Lobby ready flag (U11a). Replicated to all so every client's lobby list/podium shows each player's state. */
	UPROPERTY(ReplicatedUsing = OnRep_Ready)
	bool bReady = false;

	/** Lobby podium seat (B3b). See GetLobbySeatIndex. Plain Replicated (no OnRep — placement is server-side; the
	 *  replication only lets clients map seat->podium for future per-seat UI). */
	UPROPERTY(Replicated)
	int32 LobbySeatIndex = INDEX_NONE;

	/** Committed occupancy map (multimap Tier 0). See GetCurrentMapId. Plain Replicated (no OnRep — the allocator reads
	 *  the authoritative server value; the replication lets client UI / late-joiners show which map each player is on).
	 *  Low-churn: only set on a settled map change, not per boundary AABB frame. */
	UPROPERTY(Replicated)
	FGameplayTag CurrentMapId;

	/** Server-only: passive ability specs granted by character-passive cards this run (U18c). Not replicated —
	 *  ability specs are server ASC state. Cleared (ClearAbility) on ResetRunState so they never leak to the next run
	 *  (the ASC survives lobby<->run seamless travel, like the run GEs cleared alongside). */
	TArray<FGameplayAbilitySpecHandle> CardGrantedAbilityHandles;

	/** Server-only: how many granted passives listen for the DealtDamage event (lifesteal). Gates the hot-path event
	 *  send. Reset to 0 on ResetRunState. */
	int32 DamageEventListenerCount = 0;

	// --- U (P-F) topology ack state (server-only, NOT replicated — the client's confirmation rides the ServerAckTopology
	//     RPC; only the server reads these in the allocator/movement gates). ---

	/** Fail-open timeout (seconds): if a client's ack never lands within this, the player participates anyway (RPC loss /
	 *  softlock guard) with a one-time diagnostic log. */
	static constexpr float TopologyAckTimeoutSeconds = 5.0f;

	/** The topology generation this player joined the current run at (-1 = not yet marked / fail-closed). */
	int32 JoinTopologyGeneration = -1;

	/** The highest topology generation this player's client has acked (-1 = none; monotone). */
	int32 AckedTopologyGeneration = -1;

	/** World time the player was first marked (the fail-open clock start). */
	float TopologyJoinTime = 0.0f;

	/** One-shot latch so the fail-open diagnostic logs at most once per player (mutable — HasAckedJoinTopology is const). */
	mutable bool bLoggedTopologyFailOpen = false;
};
