// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Logging/LogMacros.h"

FPSROGUELITE_API DECLARE_LOG_CATEGORY_EXTERN(LogFPSR, Log, All);

/** Flow/branch-point diagnostics: menu clicks, session host/join, invite accept, travel, run start/end and
 *  network/travel failures. Emitted by FPSRFlowLog::Event (also mirrored to a dedicated logs/ file). */
FPSROGUELITE_API DECLARE_LOG_CATEGORY_EXTERN(LogFPSRFlow, Log, All);
