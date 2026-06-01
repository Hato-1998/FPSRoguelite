// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AbilitySystemComponent.h"
#include "AttributeSet.h"
#include "FPSRHealthSet.generated.h"

/** Multicast delegate for when the character runs out of health (Health <= 0). */
DECLARE_MULTICAST_DELEGATE(FFPSROutOfHealthSignature);

/** Player health attributes (lives on PlayerState ASC). Enemies use UEnemyHealthComponent instead. */
UCLASS()
class FPSROGUELITE_API UFPSRHealthSet : public UAttributeSet
{
	GENERATED_BODY()

public:
	UFPSRHealthSet();

	ATTRIBUTE_ACCESSORS_BASIC(UFPSRHealthSet, Health)
	ATTRIBUTE_ACCESSORS_BASIC(UFPSRHealthSet, MaxHealth)

	/** Broadcast (server) the first time Health reaches 0. Full DBNO/respawn is P5; this is the hook. */
	mutable FFPSROutOfHealthSignature OnOutOfHealth;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;
	virtual void PreAttributeBaseChange(const FGameplayAttribute& Attribute, float& NewValue) const override;
	virtual void PostAttributeChange(const FGameplayAttribute& Attribute, float OldValue, float NewValue) override;

	UFUNCTION()
	void OnRep_Health(const FGameplayAttributeData& OldValue);

	UFUNCTION()
	void OnRep_MaxHealth(const FGameplayAttributeData& OldValue);

private:
	void ClampAttribute(const FGameplayAttribute& Attribute, float& NewValue) const;

	/** Guard so OnOutOfHealth fires only once until Health is restored above 0. */
	bool bOutOfHealthBroadcast = false;

	UPROPERTY(BlueprintReadOnly, Category = "Health", ReplicatedUsing = OnRep_Health, meta = (AllowPrivateAccess = true))
	FGameplayAttributeData Health;

	UPROPERTY(BlueprintReadOnly, Category = "Health", ReplicatedUsing = OnRep_MaxHealth, meta = (AllowPrivateAccess = true))
	FGameplayAttributeData MaxHealth;
};
