// Copyright Epic Games, Inc. All Rights Reserved.

#include "AbilitySystem/Abilities/FPSRGameplayAbility.h"
#include "Hero/FPSRCharacterMovementComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/Pawn.h"

UFPSRGameplayAbility::UFPSRGameplayAbility()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
}

bool UFPSRGameplayAbility::IsFirePermittedByMovementState(const APawn* Avatar)
{
	// ACharacter (not the bare APawn) is the cast target because UFPSRCharacterMovementComponent only exists on the
	// character's GetCharacterMovement() — a non-Character pawn (if this project ever adds one) has no CMC at all,
	// which the fail-open null-check below already handles identically to "no gate".
	const ACharacter* AvatarCharacter = Cast<ACharacter>(Avatar);
	const UFPSRCharacterMovementComponent* Move = AvatarCharacter
		? Cast<UFPSRCharacterMovementComponent>(AvatarCharacter->GetCharacterMovement())
		: nullptr;
	return !Move || Move->CanFireInCurrentState();
}
