// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Interface.h"
#include "FPSRPatternActorInterface.generated.h"

UINTERFACE(MinimalAPI, NotBlueprintable)
class UFPSRPatternActor : public UInterface
{
	GENERATED_BODY()
};

/** Implemented by actors a boss pattern spawns and the boss then owns (BOSS1: the homing orbs).
 *
 *  🔴 The point of the seam is that pattern actors DO NOT poll the freeze state. The boss detects the §2-2 freeze
 *  edge once in its own Tick and pushes it to everything it owns — the same shape UFPSRProjectileSubsystem uses for
 *  in-flight projectiles. N self-polling actors would be N places to get the gate wrong, and the ones that got it
 *  wrong would keep moving (and keep dealing server-authoritative damage) while the party picks a card.
 *
 *  The boss holds these as weak pointers and destroys them on death / EndPlay, so an implementer never has to
 *  manage its own lifetime against the boss's. */
class FPSROGUELITE_API IFPSRPatternActor
{
	GENERATED_BODY()

public:
	/** Server: suspend/resume simulation. Implementations preserve velocity across the pause (stop the movement
	 *  component rather than zeroing it) and must not use FTimerManager for any gameplay timing — world timers keep
	 *  running through the freeze, which is the entire reason this call exists. */
	virtual void SetSimulationPaused(bool bPaused) = 0;
};
