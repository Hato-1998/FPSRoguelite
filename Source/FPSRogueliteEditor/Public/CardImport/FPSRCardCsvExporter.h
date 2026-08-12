// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/** Migration-only: existing DA_Card_* uassets -> Cards.csv/CardCatalog.csv (§2-3-10, §5). Run once via the Tools
 *  menu ("카드 CSV 내보내기(마이그레이션)") to seed the authoring sheets from the pre-CSV content; the
 *  round-trip (export -> import -> asset diff 0) is the pipeline's no-regression proof (§12-4). */
namespace FPSRCardCsvExport
{
	/** Existing DA_Card_* (all UFPSRCardDataAsset assets reachable via the AssetRegistry) -> Cards.csv/CardCatalog.csv.
	 *  Text rule: an English FText source string -> the en column + SourceString(ko) gets "[KO-TODO] <en original>";
	 *  an already-Korean source string -> ko column verbatim. Catalog: AttrId candidates are auto-suggested from the
	 *  GE/Fragment/Weapon/Passive each card references (naming: lowercase dot hierarchy). Existing CardFamily tag
	 *  values are preserved in the Family column via GetTagName() (normalized to blank if it equals the derived
	 *  value). Returns false + OutErrors on any failure — no file is written in that case. */
	FPSROGUELITEEDITOR_API bool ExportAll(const FString& OutCardsCsvPath, const FString& OutCatalogCsvPath, TArray<FString>& OutErrors);
}
