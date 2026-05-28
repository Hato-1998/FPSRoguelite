// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/PlayerState.h"
#include "AbilitySystemInterface.h"
#include "FPSRPlayerState.generated.h"

class UFPSRAbilitySystemComponent;
class UFPSRHealthSet;
class UFPSRCombatSet;
class UAbilitySystemComponent;

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

private:
	UPROPERTY(VisibleAnywhere, Category = "FPSR|Abilities")
	TObjectPtr<UFPSRAbilitySystemComponent> AbilitySystemComponent;

	UPROPERTY()
	TObjectPtr<UFPSRHealthSet> HealthSet;

	UPROPERTY()
	TObjectPtr<UFPSRCombatSet> CombatSet;
};
