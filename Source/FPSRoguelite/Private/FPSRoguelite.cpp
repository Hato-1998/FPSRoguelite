// Copyright Epic Games, Inc. All Rights Reserved.

#include "FPSRoguelite.h"
#include "Modules/ModuleManager.h"
#include "Internationalization/StringTableRegistry.h"

// CSV = source of truth for runtime strings (Docs/Specs/LOC0_StringTablePipeline.md §3-1/§9). These three calls
// MUST stay literal LOCTABLE_FROMFILE_GAME call sites — GatherTextFromSourceCommandlet discovers string-table CSVs
// by parsing source text for that exact macro token (see the engine's FStringTableFromFileMacroDescriptor,
// GatherTextFromSourceCommandlet.cpp:498), not by walking an ini-driven list. Adding a table later = one macro
// line here + one CSV under Content/StringTables — never a data-driven loop over a config array (§9).
void FFPSRogueliteGameModule::StartupModule()
{
	LOCTABLE_FROMFILE_GAME("UI", "UI", "StringTables/ST_UI.csv");
	LOCTABLE_FROMFILE_GAME("CardEffect", "CardEffect", "StringTables/ST_CardEffect.csv");
	LOCTABLE_FROMFILE_GAME("Card", "Card", "StringTables/ST_Card.csv");
}

// Symmetric unregister — matches StartupModule 1:1 (§8 lifecycle). UnregisterStringTable is a safe no-op for any
// table that failed to register above (e.g. a missing CSV), so this never needs to special-case a partial startup.
void FFPSRogueliteGameModule::ShutdownModule()
{
	FStringTableRegistry::Get().UnregisterStringTable("UI");
	FStringTableRegistry::Get().UnregisterStringTable("CardEffect");
	FStringTableRegistry::Get().UnregisterStringTable("Card");
}

IMPLEMENT_PRIMARY_GAME_MODULE( FFPSRogueliteGameModule, FPSRoguelite, "FPSRoguelite" );
