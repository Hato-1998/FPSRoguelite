// Copyright Epic Games, Inc. All Rights Reserved.

#include "Localization/FPSRImportCsvTranslationsCommandlet.h"

#include "Internationalization/InternationalizationManifest.h"
#include "Internationalization/Text.h"
#include "LocTextHelper.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/Csv/CsvParser.h"

DEFINE_LOG_CATEGORY_STATIC(LogFPSRImportCsvTranslations, Log, All);

int32 UFPSRImportCsvTranslationsCommandlet::Main(const FString& Params)
{
	// Parse command line (Config/Section) — same contract every GatherText step commandlet uses (mirrors engine
	// ImportDialogueScriptCommandlet::Main).
	TArray<FString> Tokens;
	TArray<FString> Switches;
	TMap<FString, FString> ParamVals;
	UCommandlet::ParseCommandLine(*Params, Tokens, Switches, ParamVals);

	FString ConfigPath;
	{
		const FString* ConfigPathParamVal = ParamVals.Find(FString(TEXT("Config")));
		if (!ConfigPathParamVal)
		{
			UE_LOG(LogFPSRImportCsvTranslations, Error, TEXT("No config specified."));
			return -1;
		}
		ConfigPath = *ConfigPathParamVal;
	}

	FString SectionName;
	{
		const FString* SectionNameParamVal = ParamVals.Find(FString(TEXT("Section")));
		if (!SectionNameParamVal)
		{
			UE_LOG(LogFPSRImportCsvTranslations, Error, TEXT("No config section specified."));
			return -1;
		}
		SectionName = *SectionNameParamVal;
	}

	// GetXFromConfig(Section, ...) falls back to [CommonSettings] when the key isn't found in Section (see
	// UGatherTextCommandletBase::GetStringFromConfig) — so reading everything through *SectionName, the same way
	// ImportDialogueScriptCommandlet does, picks up DestinationPath/ManifestName/ArchiveName/NativeCulture/
	// CulturesToGenerate from [CommonSettings] and CSVFiles/Cultures from our own [GatherTextStepN] section.
	FString DestinationPath;
	if (!GetPathFromConfig(*SectionName, TEXT("DestinationPath"), DestinationPath, ConfigPath))
	{
		UE_LOG(LogFPSRImportCsvTranslations, Error, TEXT("No DestinationPath specified."));
		return -1;
	}

	FString ManifestName;
	if (!GetStringFromConfig(*SectionName, TEXT("ManifestName"), ManifestName, ConfigPath))
	{
		UE_LOG(LogFPSRImportCsvTranslations, Error, TEXT("No ManifestName specified."));
		return -1;
	}

	FString ArchiveName;
	if (!GetStringFromConfig(*SectionName, TEXT("ArchiveName"), ArchiveName, ConfigPath))
	{
		UE_LOG(LogFPSRImportCsvTranslations, Error, TEXT("No ArchiveName specified."));
		return -1;
	}

	FString NativeCulture;
	if (!GetStringFromConfig(*SectionName, TEXT("NativeCulture"), NativeCulture, ConfigPath))
	{
		UE_LOG(LogFPSRImportCsvTranslations, Error, TEXT("No NativeCulture specified."));
		return -1;
	}

	TArray<FString> CulturesToGenerate;
	if (GetStringArrayFromConfig(*SectionName, TEXT("CulturesToGenerate"), CulturesToGenerate, ConfigPath) == 0)
	{
		UE_LOG(LogFPSRImportCsvTranslations, Error, TEXT("No CulturesToGenerate specified."));
		return -1;
	}

	// Own-section config (spec §5): CSVFiles is a semicolon list of ContentDir-relative paths, Cultures is a comma
	// list of culture codes whose CSV column should be imported (never the native culture — its text lives in the
	// manifest's SourceString, not an archive translation).
	FString CsvFilesJoined;
	if (!GetStringFromConfig(*SectionName, TEXT("CSVFiles"), CsvFilesJoined, ConfigPath) || CsvFilesJoined.IsEmpty())
	{
		UE_LOG(LogFPSRImportCsvTranslations, Error, TEXT("No CSVFiles specified in [%s]."), *SectionName);
		return -1;
	}
	TArray<FString> CsvFiles;
	CsvFilesJoined.ParseIntoArray(CsvFiles, TEXT(";"), /*InCullEmpty=*/true);

	FString CulturesJoined;
	if (!GetStringFromConfig(*SectionName, TEXT("Cultures"), CulturesJoined, ConfigPath) || CulturesJoined.IsEmpty())
	{
		UE_LOG(LogFPSRImportCsvTranslations, Error, TEXT("No Cultures specified in [%s]."), *SectionName);
		return -1;
	}
	TArray<FString> Cultures;
	CulturesJoined.ParseIntoArray(Cultures, TEXT(","), /*InCullEmpty=*/true);

	// Load the manifest (already written to disk by Game_Gather.ini's GenerateGatherManifest step) and all archives
	// (LoadOrCreate — a culture with no prior archive file gets an empty one created in memory, same as the engine's
	// own import commandlets).
	FLocTextHelper LocTextHelper(DestinationPath, ManifestName, ArchiveName, NativeCulture, CulturesToGenerate, GatherManifestHelper->GetLocFileNotifies(), GatherManifestHelper->GetPlatformSplitMode());
	LocTextHelper.SetCopyrightNotice(GatherManifestHelper->GetCopyrightNotice());
	{
		FText LoadError;
		if (!LocTextHelper.LoadAll(ELocTextHelperLoadFlags::LoadOrCreate, &LoadError))
		{
			UE_LOG(LogFPSRImportCsvTranslations, Error, TEXT("%s"), *LoadError.ToString());
			return -1;
		}
	}

	bool bHadError = false;
	for (const FString& CsvFile : CsvFiles)
	{
		// Namespace = filename minus the "ST_" prefix (ST_UI.csv -> "UI"), matching the
		// LOCTABLE_FROMFILE_GAME(Id, Namespace, ...) registration in FPSRoguelite.cpp 1:1 — the manifest entries
		// this step looks up were gathered under that same namespace by Game_Gather.ini.
		FString Namespace = FPaths::GetBaseFilename(CsvFile);
		Namespace.RemoveFromStart(TEXT("ST_"));

		const FString FullCsvPath = FPaths::ProjectContentDir() / CsvFile;
		if (!ImportCsvForNamespace(LocTextHelper, FullCsvPath, Namespace, Cultures))
		{
			bHadError = true;
		}
	}

	return bHadError ? 1 : 0;
}

bool UFPSRImportCsvTranslationsCommandlet::ImportCsvForNamespace(FLocTextHelper& InLocTextHelper, const FString& InCsvFilePath, const FString& InNamespace, const TArray<FString>& InCultures)
{
	FString CsvFileContents;
	if (!FFileHelper::LoadFileToString(CsvFileContents, *InCsvFilePath))
	{
		UE_LOG(LogFPSRImportCsvTranslations, Error, TEXT("Failed to load CSV '%s' for namespace '%s'."), *InCsvFilePath, *InNamespace);
		return false;
	}

	const FCsvParser CsvParser(CsvFileContents);
	const FCsvParser::FRows& Rows = CsvParser.GetRows();
	if (Rows.Num() <= 0)
	{
		UE_LOG(LogFPSRImportCsvTranslations, Error, TEXT("CSV '%s' has no header row."), *InCsvFilePath);
		return false;
	}

	// Locate the reserved "Key" column and one column per requested culture (column header == culture code).
	int32 KeyColumnIndex = INDEX_NONE;
	TMap<FString, int32> CultureColumnIndices;
	{
		const auto& HeaderRow = Rows[0];
		for (int32 ColumnIndex = 0; ColumnIndex < HeaderRow.Num(); ++ColumnIndex)
		{
			const FString ColumnName(HeaderRow[ColumnIndex]);
			if (ColumnName.Equals(TEXT("Key"), ESearchCase::IgnoreCase))
			{
				KeyColumnIndex = ColumnIndex;
			}
			else if (InCultures.Contains(ColumnName))
			{
				CultureColumnIndices.Add(ColumnName, ColumnIndex);
			}
		}
	}

	if (KeyColumnIndex == INDEX_NONE)
	{
		UE_LOG(LogFPSRImportCsvTranslations, Error, TEXT("CSV '%s' is missing the reserved 'Key' column."), *InCsvFilePath);
		return false;
	}

	bool bHadError = false;
	for (const FString& Culture : InCultures)
	{
		if (!CultureColumnIndices.Contains(Culture))
		{
			UE_LOG(LogFPSRImportCsvTranslations, Error, TEXT("CSV '%s' is missing the requested culture column '%s'."), *InCsvFilePath, *Culture);
			bHadError = true;
		}
	}

	// Configuration-drift tripwire (레드팀 P3-4): the manifest namespace comes from the LOCTABLE_FROMFILE_GAME macro's
	// 2nd argument while this commandlet derives it from the CSV filename — if the two ever disagree (or the table's
	// macro registration is missing entirely), every key lands in the "no manifest entry" skip below. A 100% miss is
	// never a per-row authoring problem, so escalate it to a hard error instead of exiting 0 with the culture silently
	// untranslated.
	int32 ValidKeyRowCount = 0;
	int32 ManifestMissCount = 0;
	for (int32 RowIndex = 1; RowIndex < Rows.Num(); ++RowIndex)
	{
		const auto& Row = Rows[RowIndex];
		if (!Row.IsValidIndex(KeyColumnIndex))
		{
			continue;
		}
		const FString Key(Row[KeyColumnIndex]);
		if (Key.IsEmpty())
		{
			continue;
		}
		++ValidKeyRowCount;
		if (!InLocTextHelper.FindSourceText(InNamespace, Key).IsValid())
		{
			++ManifestMissCount;
		}
	}
	if (ValidKeyRowCount > 0 && ManifestMissCount == ValidKeyRowCount)
	{
		UE_LOG(LogFPSRImportCsvTranslations, Error, TEXT("CSV '%s': ALL %d key(s) are missing from the manifest under namespace '%s' — the table's LOCTABLE_FROMFILE_GAME registration is missing/mismatched or Game_Gather.ini did not run. Refusing to exit 0 with this culture silently untranslated."), *InCsvFilePath, ValidKeyRowCount, *InNamespace);
		return false;
	}

	// Import each requested culture's column, one culture at a time (SaveArchive itself is per-culture).
	for (const TPair<FString, int32>& CulturePair : CultureColumnIndices)
	{
		const FString& Culture = CulturePair.Key;
		const int32 CultureColumnIndex = CulturePair.Value;
		bool bHasUpdatedArchive = false;

		for (int32 RowIndex = 1; RowIndex < Rows.Num(); ++RowIndex)
		{
			const auto& Row = Rows[RowIndex];
			if (!Row.IsValidIndex(KeyColumnIndex))
			{
				continue;
			}

			const FString Key(Row[KeyColumnIndex]);
			if (Key.IsEmpty())
			{
				continue;
			}

			if (!Row.IsValidIndex(CultureColumnIndex) || FString(Row[CultureColumnIndex]).IsEmpty())
			{
				// Blank translation cell — leave any existing archive entry untouched rather than importing an
				// empty string over a previously-authored translation (mirrors §12-3's "빈칸=경고, 실패 아님" contract).
				UE_LOG(LogFPSRImportCsvTranslations, Warning, TEXT("CSV '%s': key '%s' has an empty '%s' translation — skipped."), *InCsvFilePath, *Key, *Culture);
				continue;
			}

			const FString Translation(Row[CultureColumnIndex]);

			TSharedPtr<FManifestEntry> ManifestEntry = InLocTextHelper.FindSourceText(InNamespace, Key);
			if (!ManifestEntry.IsValid())
			{
				UE_LOG(LogFPSRImportCsvTranslations, Warning, TEXT("CSV '%s': no manifest entry found for namespace '%s' key '%s' — skipped (did Game_Gather.ini run first?)."), *InCsvFilePath, *InNamespace, *Key);
				continue;
			}

			const FManifestContext* Context = ManifestEntry->FindContextByKey(Key);
			if (!Context)
			{
				UE_LOG(LogFPSRImportCsvTranslations, Warning, TEXT("CSV '%s': manifest entry for namespace '%s' key '%s' has no matching context — skipped."), *InCsvFilePath, *InNamespace, *Key);
				continue;
			}

			FLocItem ExportedSource, ExportedTranslation;
			InLocTextHelper.GetExportText(Culture, InNamespace, Context->Key, Context->KeyMetadataObj, ELocTextExportSourceMethod::NativeText, ManifestEntry->Source, ExportedSource, ExportedTranslation);

			if (!ExportedTranslation.Text.Equals(Translation, ESearchCase::CaseSensitive))
			{
				if (InLocTextHelper.ImportTranslation(Culture, InNamespace, Context->Key, Context->KeyMetadataObj, ExportedSource, FLocItem(Translation), Context->bIsOptional))
				{
					bHasUpdatedArchive = true;
				}
			}
		}

		if (bHasUpdatedArchive)
		{
			FText SaveError;
			if (!InLocTextHelper.SaveArchive(Culture, &SaveError))
			{
				UE_LOG(LogFPSRImportCsvTranslations, Error, TEXT("Failed to save archive for culture '%s': %s"), *Culture, *SaveError.ToString());
				bHadError = true;
			}
		}
	}

	return !bHadError;
}
