// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AbilitySystemComponent.h"
#include "AttributeSet.h"
#include "FPSRHealthSet.generated.h"

/** Multicast delegate for when the character runs out of health (Health <= 0). */
DECLARE_MULTICAST_DELEGATE(FFPSROutOfHealthSignature);

/** VIT1: fires (server) the instant Shield transitions from >0 to 0 — the player-warning source for requirement 5.
 *  Plain (non-dynamic) delegate, matching FFPSROutOfHealthSignature exactly: only C++ binds to it. */
DECLARE_MULTICAST_DELEGATE(FFPSRShieldBrokenSignature);

/** Player health attributes (lives on PlayerState ASC). Enemies use UEnemyHealthComponent instead. */
UCLASS()
class FPSROGUELITE_API UFPSRHealthSet : public UAttributeSet
{
	GENERATED_BODY()

public:
	UFPSRHealthSet();

	ATTRIBUTE_ACCESSORS_BASIC(UFPSRHealthSet, Health)
	ATTRIBUTE_ACCESSORS_BASIC(UFPSRHealthSet, MaxHealth)
	ATTRIBUTE_ACCESSORS_BASIC(UFPSRHealthSet, Shield)
	ATTRIBUTE_ACCESSORS_BASIC(UFPSRHealthSet, MaxShield)
	/** VIT1 §9 layer-mitigation multiplier — smaller is tougher, 1 = no card bonus. Composes with the vitals
	 *  profile's per-DamageType coefficient (FMitigation.ShieldDefense = profile x this attribute); a card that
	 *  reads "+20% shield defense" is a GE that sets this to 0.8. */
	ATTRIBUTE_ACCESSORS_BASIC(UFPSRHealthSet, ShieldDefense)
	ATTRIBUTE_ACCESSORS_BASIC(UFPSRHealthSet, HealthDefense)

	/** Broadcast (server) the first time Health reaches 0. Full DBNO/respawn is P5; this is the hook. */
	mutable FFPSROutOfHealthSignature OnOutOfHealth;

	/** VIT1 requirement 5: broadcast (server) the instant Shield transitions from >0 to 0. */
	mutable FFPSRShieldBrokenSignature OnShieldBroken;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;
	virtual void PreAttributeBaseChange(const FGameplayAttribute& Attribute, float& NewValue) const override;
	virtual void PostAttributeChange(const FGameplayAttribute& Attribute, float OldValue, float NewValue) override;

	UFUNCTION()
	void OnRep_Health(const FGameplayAttributeData& OldValue);

	UFUNCTION()
	void OnRep_MaxHealth(const FGameplayAttributeData& OldValue);

	UFUNCTION()
	void OnRep_Shield(const FGameplayAttributeData& OldValue);

	UFUNCTION()
	void OnRep_MaxShield(const FGameplayAttributeData& OldValue);

	UFUNCTION()
	void OnRep_ShieldDefense(const FGameplayAttributeData& OldValue);

	UFUNCTION()
	void OnRep_HealthDefense(const FGameplayAttributeData& OldValue);

private:
	void ClampAttribute(const FGameplayAttribute& Attribute, float& NewValue) const;

	/** Guard so OnOutOfHealth fires only once until Health is restored above 0. */
	bool bOutOfHealthBroadcast = false;

	/** Guard so OnShieldBroken fires only once per break (mirrors bOutOfHealthBroadcast) until Shield is refilled
	 *  above 0 again — without it, a regen driver tick landing exactly on 0 (or a second 0-damage hit while already
	 *  broken) could re-fire the warning. */
	bool bShieldBrokenBroadcast = false;

	UPROPERTY(BlueprintReadOnly, Category = "Health", ReplicatedUsing = OnRep_Health, meta = (AllowPrivateAccess = true))
	FGameplayAttributeData Health;

	UPROPERTY(BlueprintReadOnly, Category = "Health", ReplicatedUsing = OnRep_MaxHealth, meta = (AllowPrivateAccess = true))
	FGameplayAttributeData MaxHealth;

	UPROPERTY(BlueprintReadOnly, Category = "Health", ReplicatedUsing = OnRep_Shield, meta = (AllowPrivateAccess = true))
	FGameplayAttributeData Shield;

	UPROPERTY(BlueprintReadOnly, Category = "Health", ReplicatedUsing = OnRep_MaxShield, meta = (AllowPrivateAccess = true))
	FGameplayAttributeData MaxShield;

	UPROPERTY(BlueprintReadOnly, Category = "Health", ReplicatedUsing = OnRep_ShieldDefense, meta = (AllowPrivateAccess = true))
	FGameplayAttributeData ShieldDefense;

	UPROPERTY(BlueprintReadOnly, Category = "Health", ReplicatedUsing = OnRep_HealthDefense, meta = (AllowPrivateAccess = true))
	FGameplayAttributeData HealthDefense;
};
