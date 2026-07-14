// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UWorld;

/**
 * Blockout guardrail validation (slice ⑤). A static, read-only pass over the current editor level that checks the
 * blockout guardrails and reports each finding into the "FPSRBlockout" message log (which it opens on completion):
 *   1) 콜리전     — AStaticMeshActors that block but are NOT ECC_WorldStatic (the flow-field obstacle mask traces
 *                   ECC_WorldStatic, so a non-WorldStatic blocker is invisible → enemies walk through it).
 *   2) 지면(floor) — a WorldStatic ground surface is reachable below the play area (else the grid has no floor anchor).
 *   3) 스폰Z      — AFPSREnemySpawnPoints sit ~floor+100 (capsule half-height 90; a point flush with the floor sinks
 *                   the spawned enemy through it — memory enemy-spawnpoint-z-floor-offset).
 *   4) 볼륨       — exactly one AFPSRFlowFieldBoundsVolume (0 = origin fallback; >1 = single-map ambiguity).
 *   5) 셀예산     — the bounds volume's grid stays within UFPSRFlowFieldComputer's MaxGridDimPerAxis / MaxTotalCells.
 *   6) 중심 클리어 — the bounds box center has no elevated first-hit collider (would mis-anchor the grid-origin floor
 *                   trace — memory flowfield-volume-center-collision-floortrace).
 * Read-only: traces + actor iteration only, never mutates the level.
 */
class FFPSRBlockoutValidator
{
public:
	/** Message-log listing name FMessageLog writes to (lazily created; the editor's MessageLog viewer shows it). */
	static const FName MessageLogName;

	/** Runs every guardrail check against World, reports findings to the FPSRBlockout message log (which it opens),
	 *  and returns the number of warning/error findings (0 = clean). Returns 1 (and logs an error) if World is null. */
	static int32 ValidateLevel(UWorld* World);
};
