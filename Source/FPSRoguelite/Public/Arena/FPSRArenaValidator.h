// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Arena/FPSRArenaTypes.h"

/** What a validation pass found. Numbers are kept alongside the messages so the editor can show a summary
 *  without re-deriving anything, and so a regression test can assert on values instead of parsing strings. */
struct FFPSRArenaValidationResult
{
	/** Invariant violations. Non-empty = the arena is broken and must be fixed before it ships. */
	TArray<FString> Errors;
	/** Things that are legal but probably not intended. */
	TArray<FString> Warnings;

	int32 OpenCells = 0;
	/** Size of the largest connected component of open cells. Equal to OpenCells when the arena is one space. */
	int32 LargestComponent = 0;
	/** Orthogonal open<->open adjacencies. >= OpenCells on a connected graph means at least one cycle exists. */
	int32 OpenEdges = 0;
	/** Narrowest corridor any open cell sits in, measured as min(run along X, run along Y). */
	int32 NarrowestCorridor = 0;
	/** Open cells walled on 3+ orthogonal sides — the pocket an enemy gets pushed into and cannot steer out of. */
	int32 PocketCells = 0;
	/** Fraction of open cells that are NOT part of the minimum corridor width, i.e. what L1 has to work with. */
	float SlackFraction = 0.0f;

	bool Passed() const { return Errors.Num() == 0; }
};

/**
 * The arena's invariant checks, in ONE place (ADR 0011 E4).
 *
 * This exists because 0011 flipped how the invariants are guaranteed. While the skeleton was generated, the
 * lattice construction made concave pockets and sub-minimum corridors impossible, and these checks merely
 * confirmed it. Now a person places the blocking volumes, and a person can build all of those failures — so
 * checking stopped being a test and became part of the pipeline.
 *
 * Deliberately shared by three callers rather than reimplemented per site: the automation test, the editor's
 * level validation, and a world-begin log. A rule that lives in two places is a rule that will disagree with
 * itself, and the disagreement surfaces as "the editor said it was fine".
 *
 * The editor is where the verdict matters. A runtime failure arrives after the map is built, with nobody
 * around to fix it — so the runtime call is an alarm, not a gate (0011 실패 흐름 1).
 */
class FPSROGUELITE_API FFPSRArenaValidator
{
public:
	static FFPSRArenaValidationResult Validate(const FFPSRArenaLayout& Layout, const FFPSRArenaGenParams& Params);

	/** One-line summary for logs. */
	static FString Summarize(const FFPSRArenaValidationResult& Result);
};
