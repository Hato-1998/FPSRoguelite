// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h" // FDefaultGameModuleImpl (spec §5 sketches "ModuleInterface.h", but that header alone does not
                                    // declare FDefaultGameModuleImpl — it lives in ModuleManager.h, which also declares IModuleInterface).

/** Primary game module. Owns runtime string-table registration (CSV = source of truth).
 *  Tables are registered with literal LOCTABLE_FROMFILE_GAME calls in the .cpp — the localization
 *  gather commandlet DISCOVERS csv tables by parsing source for that macro, so this must stay
 *  a literal call site (no ini-driven loop). Adding a table = one macro line + one CSV. */
class FFPSRogueliteGameModule : public FDefaultGameModuleImpl
{
public:
	virtual void StartupModule() override;   // LOCTABLE_FROMFILE_GAME x3 (UI / CardEffect / Card)
	virtual void ShutdownModule() override;  // FStringTableRegistry::UnregisterStringTable x3 (대칭 해제)
};
