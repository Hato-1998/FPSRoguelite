// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FPSRFeedbackTypes.generated.h"

/** Local hit-marker feedback kind (Game.MD §2-14). Hit = pellet landed on an enemy (client-predicted on the
 *  local trace); Crit/Kill are server-confirmed upgrades delivered to the owning client. */
UENUM(BlueprintType)
enum class EFPSRHitMarkerType : uint8
{
	Hit  UMETA(DisplayName = "Hit"),
	Crit UMETA(DisplayName = "Crit"),
	Kill UMETA(DisplayName = "Kill")
};
