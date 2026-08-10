// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Abilities/GameplayAbility.h"
#include "FPSRGameplayAbility.generated.h"

class APawn;

/** Project base GameplayAbility. Common helpers/policies live here. */
UCLASS(Abstract)
class FPSROGUELITE_API UFPSRGameplayAbility : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UFPSRGameplayAbility();

protected:
	/** True when the avatar's CURRENT locomotion state permits firing (server-authoritative source =
	 *  UFPSRCharacterMovementComponent::CanFireInCurrentState(), ADR 0001 single owner of locomotion state — e.g.
	 *  wall-hang, both hands on the wall). LocalPredicted abilities run ActivateAbility on the predicting owning
	 *  client AND the server, so calling this from inside ActivateAbility (outside any HasAuthority branch) is what
	 *  makes it the REAL gate: the FireComponent-side CanFire()/FireOneShot() checks are cosmetic-only prediction that
	 *  can diverge from the server without this. Fail-open (true) when the avatar has no CMC of this type, so an
	 *  avatar type that doesn't use this project's movement component is never silently blocked by a state it can't
	 *  even enter. */
	static bool IsFirePermittedByMovementState(const APawn* Avatar);
};
