// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FFPSRCardCsvParseResult;

/** Result of one ImportAll() pass. Unchanged = touched-but-not-mutated (no Modify/MarkDirty called) — the
 *  observable proof the import was idempotent (§12-4: a second consecutive run must be all-Unchanged). */
struct FFPSRCardImportResult
{
	int32 CreatedCount = 0, UpdatedCount = 0, UnchangedCount = 0;
	/** Pool/weapon membership array entries removed this run — declarative-sync removals (a card no longer in
	 *  Cards.csv for that route) plus invalid-card cleanup (§ red-team gate P2-③). Surfaced in the Tools menu
	 *  summary so a designer notices an unexpectedly large drop in wired-up content. */
	int32 RemovedMembershipCount = 0;
	TArray<FString> Errors; // parsing + resolution + validation-gate errors, all of them
	bool Succeeded() const { return Errors.Num() == 0; }
};

/** Editor-only CSV -> DA_Card_* importer core (§2-3-10, §5). Loads Content/Authoring/{Cards,CardCatalog}.csv,
 *  parses them (FPSRCardCsv::Parse*), creates/updates card DataAssets in place, regenerates ST_Card.csv, and runs
 *  the existing validation gate (IsDataValid + FPSRCardPoolValidator) on every touched asset before any save. */
namespace FPSRCardCsvImport
{
	/** Content/Authoring/*.csv -> load -> parse -> DA create/update -> ST_Card.csv regenerate -> validation gate
	 *  (IsDataValid + FPSRCardPoolValidator). Idempotent contract: when the resulting state already matches the
	 *  target, Modify()/MarkDirty() are never called (a second consecutive run reports 0 dirty packages).
	 *  bSaveAssets: the commandlet passes true (SavePackage all the way through); the editor menu also passes true
	 *  (successful assets are saved; assets that failed the validation gate are left dirty and unsaved). */
	FPSROGUELITEEDITOR_API FFPSRCardImportResult ImportAll(bool bSaveAssets);
}
