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

	// --- Hover Window (ADR 0009 P1 S3 — UFPSRHoverWindowSubsystem's player-centred 3D window). Read at LAUNCH
	//     (dims/interval) or QUERY (edge margin) time, same "designer knob, no recompile" rationale as Separation
	//     above — a PIE edit to the dims takes effect on each slot's own NEXT launch; the margin is live immediately. ---

	/** Window horizontal extent (cells, both X and Y) — a square footprint centred on its owning player. At the
	 *  default 150cm voxel size (FFPSRArenaVoxelData::VoxelCellSizeCm) this is a 96m-wide window. */
	UPROPERTY(Config, EditAnywhere, Category = "Hover Window",
		meta = (DisplayName = "창 가로 셀 수", ClampMin = "8", ClampMax = "192", UIMax = "128"))
	int32 WindowDimXY = 64;

	/** Window vertical extent (cells). Matches the arena voxel bake's own Z band (VoxelBelowOriginCm +
	 *  VoxelAboveOriginCm = 3600cm -> 24 cells at 150cm) by default. */
	UPROPERTY(Config, EditAnywhere, Category = "Hover Window",
		meta = (DisplayName = "창 세로 셀 수", ClampMin = "4", ClampMax = "64", UIMax = "48"))
	int32 WindowDimZ = 24;

	/** Per-tick round-robin cadence (seconds) — UFPSRHoverWindowSubsystem considers exactly ONE of its 4 slots each
	 *  time this timer fires, so one player's window actually re-propagates every 4x this value (~250ms default). */
	UPROPERTY(Config, EditAnywhere, Category = "Hover Window",
		meta = (DisplayName = "창 슬롯 갱신 주기", ClampMin = "0.01", UIMax = "1.0", ForceUnits = "s"))
	float WindowUpdateIntervalSec = 0.0625f;

	/** Cells of margin from the window's own edge a query must stay inside to trust the field there — closer (or
	 *  past the edge) falls back to the 2D surface field instead (ADR 0009 §실패 흐름). */
	UPROPERTY(Config, EditAnywhere, Category = "Hover Window",
		meta = (DisplayName = "창 가장자리 여유(셀)", ClampMin = "0", UIMax = "16"))
	int32 WindowEdgeMarginCells = 4;
};
