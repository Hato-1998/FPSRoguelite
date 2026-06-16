// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/PlayerState.h"
#include "AbilitySystemInterface.h"
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

	/** Per-player life state. Simplified for U2 (defeat wiring): a single replicated bool. U9 (DBNO) replaces
	 *  this with an ELifeState{Alive,DBNO,Dead} state machine — IsAlive() is the single predicate U9 re-defines
	 *  (e.g. DBNO also counts as not-alive for the wipe check). Lives on the PlayerState so it survives pawn
	 *  death/respawn and the wipe aggregation is independent of pawn validity. */
	UFUNCTION(BlueprintPure, Category = "FPSR|Run")
	bool IsDead() const { return bIsDead; }

	/** True while this player is a live participant. The single predicate U9 (DBNO) re-defines. */
	bool IsAlive() const { return !bIsDead; }

	/** Server: mark this player dead/alive. Idempotent. Replicates to all (owning client gates input via OnRep). */
	void SetDead(bool bNewDead);

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

	/** Pending mission-reward picks (granted on mission clear; selected at the freeze, Game.MD §2-8). */
	UFUNCTION(BlueprintPure, Category = "FPSR|Run")
	int32 GetMissionRewardPicksPending() const { return MissionRewardPicksPending; }

	/** Server: add one pending mission-reward pick. */
	void AddMissionRewardPick();

	/** Server: consume one pending mission-reward pick. Returns true if successful. */
	bool ConsumeMissionRewardPick();

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

	/** Server: reset all per-run progression to a fresh-run baseline (called on lobby entry, P7 §3-6). Clears
	 *  life state, pending picks, AllWeapons modifiers and the loadout pick. XP/PartyLevel reset naturally via the
	 *  fresh GameState on each map; weapon inventory resets via pawn respawn. Run on the server (authority). */
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
	bool bIsDead = false;

	UPROPERTY(ReplicatedUsing = OnRep_RunRerollCharges)
	int32 RunRerollCharges = 3;

	UPROPERTY(ReplicatedUsing = OnRep_CardPicksPending)
	int32 CardPicksPending = 0;

	UPROPERTY(ReplicatedUsing = OnRep_CardPicksPending)
	int32 MissionRewardPicksPending = 0;

	UPROPERTY(ReplicatedUsing = OnRep_AllWeaponsMods)
	FFPSRWeaponModContainer AllWeaponsMods;

	/** Lobby loadout pick (the single run weapon). Hard ref — weapon DataAssets are always-loaded primary assets,
	 *  so this replicates cleanly. ReplicatedUsing drives the owning-client lobby UI refresh. */
	UPROPERTY(ReplicatedUsing = OnRep_SelectedWeapon)
	TObjectPtr<UFPSRWeaponDataAsset> SelectedWeapon;
};
