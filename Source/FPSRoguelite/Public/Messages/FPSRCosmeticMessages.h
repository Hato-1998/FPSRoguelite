// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "FPSRCosmeticMessages.generated.h"

/**
 * Cosmetic event payload broadcast on the local GMS (U8) for hit/death cosmetics.
 * Consumers: U13 (VFX / Gibs / pings). Value type — stack allocated, copied by value.
 * Performance.md §5: cosmetics travel on GameplayMessage, NOT replicated actor state.
 * Seam: hit-normal / target-actor / (future U20) enemy-attack fields are added by consumers when needed —
 * intentionally minimal now (over-design 금지).
 */
USTRUCT(BlueprintType)
struct FFPSRCosmeticEventMessage
{
	GENERATED_BODY()

	/** World-space location of the event (impact / death point) — for VFX/Gibs placement. */
	UPROPERTY(BlueprintReadWrite, Category = "FPSR|Cosmetic")
	FVector WorldLocation = FVector::ZeroVector;

	/** Enemy type / archetype tag of the source, for cosmetic selection (empty = unspecified). */
	UPROPERTY(BlueprintReadWrite, Category = "FPSR|Cosmetic")
	FGameplayTag SourceType;

	/** Team of the instigator (project team indices) — for team-aware cosmetics. */
	UPROPERTY(BlueprintReadWrite, Category = "FPSR|Cosmetic")
	uint8 InstigatorTeam = 0;

	/** True if this event was a kill (death cosmetic / Gibs); false for a non-lethal hit. */
	UPROPERTY(BlueprintReadWrite, Category = "FPSR|Cosmetic")
	bool bWasKill = false;
};
