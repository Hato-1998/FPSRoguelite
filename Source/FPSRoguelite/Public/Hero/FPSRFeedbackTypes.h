// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FPSRFeedbackTypes.generated.h"

/** Local hit-marker feedback kind (Game.MD §2-14). Hit = pellet landed on an enemy (client-predicted on the
 *  local trace); Crit/Kill are server-confirmed upgrades delivered to the owning client. Weak = 약점 부위 명중(서버 확정).
 *  ShieldBreak (VIT1) = this hit took a shield from >0 to 0 — appended LAST, never inserted in the middle: this is a
 *  uint8 that content (BP/assets) stores by value, so renumbering would silently reinterpret an already-authored
 *  marker as a different one. FPSRCombat::ResolveHitMarker owns the priority order between these. */
UENUM(BlueprintType)
enum class EFPSRHitMarkerType : uint8
{
	Hit  UMETA(DisplayName = "Hit"),
	Crit UMETA(DisplayName = "Crit"),
	Weak UMETA(DisplayName = "Weak"),
	Kill UMETA(DisplayName = "Kill"),
	ShieldBreak UMETA(DisplayName = "Shield Break")
};
