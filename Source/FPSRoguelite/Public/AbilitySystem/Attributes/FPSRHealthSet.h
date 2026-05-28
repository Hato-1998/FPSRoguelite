// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AbilitySystemComponent.h"
#include "AttributeSet.h"
#include "FPSRHealthSet.generated.h"

/** Player health attributes (lives on PlayerState ASC). Enemies use UEnemyHealthComponent instead. */
UCLASS()
class FPSROGUELITE_API UFPSRHealthSet : public UAttributeSet
{
	GENERATED_BODY()

public:
	UFPSRHealthSet();

	ATTRIBUTE_ACCESSORS_BASIC(UFPSRHealthSet, Health)
	ATTRIBUTE_ACCESSORS_BASIC(UFPSRHealthSet, MaxHealth)

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	UFUNCTION()
	void OnRep_Health(const FGameplayAttributeData& OldValue);

	UFUNCTION()
	void OnRep_MaxHealth(const FGameplayAttributeData& OldValue);

private:
	UPROPERTY(BlueprintReadOnly, Category = "Health", ReplicatedUsing = OnRep_Health, meta = (AllowPrivateAccess = true))
	FGameplayAttributeData Health;

	UPROPERTY(BlueprintReadOnly, Category = "Health", ReplicatedUsing = OnRep_MaxHealth, meta = (AllowPrivateAccess = true))
	FGameplayAttributeData MaxHealth;
};
