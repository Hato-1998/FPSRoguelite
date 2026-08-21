// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DeveloperSettings.h"
#include "FPSREnemySwarmSettings.generated.h"

/** Swarm steering tuning (Project Settings -> "FPSR Enemy Swarm"). Values are authored in DefaultGame.ini
 *  [/Script/FPSRoguelite.FPSREnemySwarmSettings], matching UFPSREnemyRenderSettings / UFPSRAudioSettings.
 *
 *  Why this exists: the separation (anti-clumping) numbers lived as constexpr in UFPSREnemySpawnSubsystem, which
 *  made the swarm's personal-space feel a code change instead of a designer knob. First live-fire feedback on the
 *  proto swarm ("enemies jostle shoulder-to-shoulder at the stop ring") is exactly the kind of call that should be
 *  tuned in PIE, not recompiled. Read at USE (every movement pass), so edits apply to enemies already on the field. */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "FPSR Enemy Swarm"))
class FPSROGUELITE_API UFPSREnemySwarmSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** Distance (cm, XY) under which two enemies start pushing apart. This is also the spatial-hash cell size, so
	 *  the neighbour query stays a 3x3 walk whatever the value. Enemy capsules are 40cm radius and do NOT block
	 *  each other (soft separation instead of physics gridlock), so this radius IS the crowd's personal space:
	 *  120 was visibly shoulder-to-shoulder at the player stop ring; 150 opens the gap requested in PIE feedback. */
	UPROPERTY(Config, EditAnywhere, Category = "Separation",
		meta = (DisplayName = "분리 반경", ClampMin = "50.0", UIMax = "400.0", ForceUnits = "cm"))
	float SeparationRadius = 150.0f;

	/** Weight of the separation push against the unit flow direction. The push per neighbour ramps 0..1 from the
	 *  radius edge to contact, so at this default an enemy shoves through the crowd only when genuinely overlapped
	 *  and yields to the flow otherwise. Raising it spreads the swarm wider but makes it look busier. */
	UPROPERTY(Config, EditAnywhere, Category = "Separation",
		meta = (DisplayName = "분리 강도", ClampMin = "0.0", UIMax = "5.0"))
	float SeparationStrength = 1.5f;
};
