// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AbilitySystemComponent.h"
#include "AttributeSet.h"
#include "FPSRCombatSet.generated.h"

/** Global player combat modifiers consumed by the weapon damage calculation. */
UCLASS()
class FPSROGUELITE_API UFPSRCombatSet : public UAttributeSet
{
	GENERATED_BODY()

public:
	UFPSRCombatSet();

	ATTRIBUTE_ACCESSORS_BASIC(UFPSRCombatSet, GlobalCritChance)
	ATTRIBUTE_ACCESSORS_BASIC(UFPSRCombatSet, GlobalCritMultiplier)
	ATTRIBUTE_ACCESSORS_BASIC(UFPSRCombatSet, GlobalDamageMultiplier)
	ATTRIBUTE_ACCESSORS_BASIC(UFPSRCombatSet, Luck)
	ATTRIBUTE_ACCESSORS_BASIC(UFPSRCombatSet, PickupRadius)
	ATTRIBUTE_ACCESSORS_BASIC(UFPSRCombatSet, XPGain)
	ATTRIBUTE_ACCESSORS_BASIC(UFPSRCombatSet, MoveSpeedMultiplier)

	// Reserved attribute axes (extensibility directive §2-3-1): add new player stat axes here — a GE + DataAsset card
	// can target them with zero pipeline edits (e.g. CooldownReduction, Armor, MoveAccel). Define the FGameplayAttributeData
	// field + ATTRIBUTE_ACCESSORS_BASIC + OnRep + Init + DOREPLIFETIME_CONDITION_NOTIFY following the pattern above.

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	UFUNCTION()
	void OnRep_GlobalCritChance(const FGameplayAttributeData& OldValue);

	UFUNCTION()
	void OnRep_GlobalCritMultiplier(const FGameplayAttributeData& OldValue);

	UFUNCTION()
	void OnRep_GlobalDamageMultiplier(const FGameplayAttributeData& OldValue);

	UFUNCTION()
	void OnRep_Luck(const FGameplayAttributeData& OldValue);

	UFUNCTION()
	void OnRep_PickupRadius(const FGameplayAttributeData& OldValue);

	UFUNCTION()
	void OnRep_XPGain(const FGameplayAttributeData& OldValue);

	UFUNCTION()
	void OnRep_MoveSpeedMultiplier(const FGameplayAttributeData& OldValue);

	virtual void PostAttributeChange(const FGameplayAttribute& Attribute, float OldValue, float NewValue) override;

	/** Apply the current MoveSpeedMultiplier to the owning character's movement (server PostAttributeChange + client/sim-proxy OnRep). */
	void ApplyMoveSpeedToOwner();

private:
	UPROPERTY(BlueprintReadOnly, Category = "Combat", ReplicatedUsing = OnRep_GlobalCritChance, meta = (AllowPrivateAccess = true))
	FGameplayAttributeData GlobalCritChance;

	UPROPERTY(BlueprintReadOnly, Category = "Combat", ReplicatedUsing = OnRep_GlobalCritMultiplier, meta = (AllowPrivateAccess = true))
	FGameplayAttributeData GlobalCritMultiplier;

	UPROPERTY(BlueprintReadOnly, Category = "Combat", ReplicatedUsing = OnRep_GlobalDamageMultiplier, meta = (AllowPrivateAccess = true))
	FGameplayAttributeData GlobalDamageMultiplier;

	UPROPERTY(BlueprintReadOnly, Category = "Combat", ReplicatedUsing = OnRep_Luck, meta = (AllowPrivateAccess = true))
	FGameplayAttributeData Luck;

	UPROPERTY(BlueprintReadOnly, Category = "Combat", ReplicatedUsing = OnRep_PickupRadius, meta = (AllowPrivateAccess = true))
	FGameplayAttributeData PickupRadius;

	UPROPERTY(BlueprintReadOnly, Category = "Combat", ReplicatedUsing = OnRep_XPGain, meta = (AllowPrivateAccess = true))
	FGameplayAttributeData XPGain;

	UPROPERTY(BlueprintReadOnly, Category = "Combat", ReplicatedUsing = OnRep_MoveSpeedMultiplier, meta = (AllowPrivateAccess = true))
	FGameplayAttributeData MoveSpeedMultiplier;
};
