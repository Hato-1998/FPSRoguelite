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

/** One out-of-view enemy direction for the screen-edge threat indicator (Game.MD §2-14). Local/cosmetic. */
USTRUCT(BlueprintType)
struct FFPSRThreatDir
{
	GENERATED_BODY()

	/** Signed yaw of the threat relative to the camera forward, degrees (-180..180; negative = left). */
	UPROPERTY(BlueprintReadOnly, Category = "FPSR|Feedback")
	float AngleDeg = 0.0f;

	/** Proximity weight 0..1 (1 = at the player, 0 = at the threat radius edge). */
	UPROPERTY(BlueprintReadOnly, Category = "FPSR|Feedback")
	float Severity01 = 0.0f;
};
