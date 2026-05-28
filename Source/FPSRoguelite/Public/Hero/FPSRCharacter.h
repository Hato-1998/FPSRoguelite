// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/Character.h"
#include "AbilitySystemInterface.h"
#include "FPSRCharacter.generated.h"

class UAbilitySystemComponent;
class UFPSRAbilitySystemComponent;

/** Base player character. Camera and Separated-Arms meshes are configured in a later step. */
UCLASS()
class FPSROGUELITE_API AFPSRCharacter : public ACharacter, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	AFPSRCharacter();

	virtual void PossessedBy(AController* NewController) override;
	virtual void OnRep_PlayerState() override;

	//~IAbilitySystemInterface
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	//~End IAbilitySystemInterface

protected:
	/** Caches the PlayerState ASC and initializes the actor info (server in PossessedBy, client in OnRep_PlayerState). */
	void InitAbilitySystem();

	UPROPERTY()
	TObjectPtr<UFPSRAbilitySystemComponent> AbilitySystemComponent;
};
