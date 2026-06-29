// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UObject;

/**
 * Lightweight flow / branch-point diagnostic logger.
 *
 * Every Event() call does two things:
 *   1. UE_LOG(LogFPSRFlow, ...) — visible in the standard engine log / console.
 *   2. Appends a timestamped line to a dedicated per-launch file under <ProjectDir>/logs/
 *      (in a packaged build that resolves to <PackageDir>/FPSRoguelite/logs/FlowLog_<stamp>.log).
 *
 * Intended for the coarse multiplayer/menu flow (menu clicks, session host/join, invite accept, travel,
 * run start/end, network failures) — NOT per-frame gameplay. Appends per event (reopen-on-write): simple
 * and crash-safe, and these events are infrequent. Thread-safe via an internal critical section.
 */
namespace FPSRFlowLog
{
	/** Log a branch-point event (Tag = short ALL-CAPS domain, e.g. "MENU", "SESSION", "JOIN", "INVITE"). */
	FPSROGUELITE_API void Event(const FString& Tag, const FString& Message);

	/** As above, but prefixes the message with the world's net role ([Server]/[Client]/[Standalone]) derived
	 *  from WorldContext — use from PCs/widgets/subsystems so host and client traces are distinguishable. */
	FPSROGUELITE_API void Event(const UObject* WorldContext, const FString& Tag, const FString& Message);

	/** Absolute path of the active log file (for surfacing to the user / tests). Empty until the first Event. */
	FPSROGUELITE_API FString GetLogFilePath();
}
