// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FPSRFeedbackTypes.generated.h"

/** Local hit-marker feedback kind (Game.MD §2-14). Hit = pellet landed on an enemy (client-predicted on the
 *  local trace); Crit/Kill are server-confirmed upgrades delivered to the owning client. Weak = 약점 부위 명중(서버 확정). */
UENUM(BlueprintType)
enum class EFPSRHitMarkerType : uint8
{
	Hit  UMETA(DisplayName = "Hit"),
	Crit UMETA(DisplayName = "Crit"),
	Weak UMETA(DisplayName = "Weak"),
	Kill UMETA(DisplayName = "Kill")
};
