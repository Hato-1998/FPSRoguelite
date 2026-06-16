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

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnCardPicksChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnRerollChargesChanged);

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

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;

	UPROPERTY(BlueprintAssignable, Category = "FPSR|Run")
	FOnCardPicksChanged OnCardPicksChanged;

protected:
	UFUNCTION()
	void OnRep_LifeState();

	UFUNCTION()
	void OnRep_RunRerollCharges();

	UFUNCTION()
	void OnRep_CardPicksPending();

	UFUNCTION()
	void OnRep_AllWeaponsMods();

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
};
