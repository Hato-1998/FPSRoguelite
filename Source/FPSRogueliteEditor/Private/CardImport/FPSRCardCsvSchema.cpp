// Copyright Epic Games, Inc. All Rights Reserved.

#include "CardImport/FPSRCardCsvSchema.h"

#include "Serialization/Csv/CsvParser.h"
#include "Misc/DefaultValueHelper.h"

DEFINE_LOG_CATEGORY_STATIC(LogFPSRCardCsvSchema, Log, All);

namespace
{
	// Column order = SSOT CombatWeaponCard.md §2-3-10. Shared internally by ParseCards/ParseCatalog; the
	// exporter (B-3) writes the identical literal order so a round trip never reorders columns.
	const TArray<FString>& GetCardsExpectedHeaderInternal()
	{
		static const TArray<FString> Header = {
			TEXT("CardId"), TEXT("AssetName"), TEXT("Group"), TEXT("Route"), TEXT("OwnerWeapon"), TEXT("Weight"), TEXT("Family"),
			TEXT("DisplayName_ko"), TEXT("DisplayName_en"), TEXT("DisplayName_ja"),
			TEXT("Description_ko"), TEXT("Description_en"), TEXT("Description_ja"),
			TEXT("E1_Attr"), TEXT("E1_Override"), TEXT("E1_Tiers"),
			TEXT("E2_Attr"), TEXT("E2_Override"), TEXT("E2_Tiers"),
			TEXT("E3_Attr"), TEXT("E3_Override"), TEXT("E3_Tiers")
		};
		return Header;
	}

	const TArray<FString>& GetCatalogExpectedHeaderInternal()
	{
		static const TArray<FString> Header = {
			TEXT("AttrId"), TEXT("EffectType"), TEXT("Payload"), TEXT("DefaultOp"), TEXT("DefaultThisWeaponOnly"), TEXT("ShowAsPercent"), TEXT("Notes")
		};
		return Header;
	}

	// CardCatalog.csv EffectType — closed 5-value string enum, 1:1 with the UCardEffect_* subclasses (§5).
	const TArray<FName>& GetValidEffectTypesInternal()
	{
		static const TArray<FName> Types = {
			FName(TEXT("CharGE")), FName(TEXT("CharPassive")), FName(TEXT("WeaponStat")), FName(TEXT("WeaponBehavior")), FName(TEXT("GrantWeapon"))
		};
		return Types;
	}

	// Allowed E*_Override keys mirror the catalog's three Default* columns exactly (§11 미결정 항목 — resolved:
	// "그 외 키 = 오류").
	bool IsValidOverrideKey(const FString& Key)
	{
		return Key == TEXT("Op") || Key == TEXT("ThisWeaponOnly") || Key == TEXT("ShowAsPercent");
	}

	// True for a raw parser row (array of null-terminated cell pointers) whose every cell is empty — i.e. a
	// trailing blank line the CSV writer/editor left behind. Skipped rather than treated as a malformed record.
	bool IsBlankRow(const TArray<const TCHAR*>& RawRow)
	{
		for (const TCHAR* Cell : RawRow)
		{
			if (Cell && *Cell)
			{
				return false;
			}
		}
		return true;
	}

	// Cell-index access that tolerates a row shorter than the header (trailing blank columns Excel/Sheets can
	// drop) — returns an empty string rather than reading out of bounds. TRIMS surrounding whitespace: sheet
	// editing invites stray trailing spaces in ID/enum cells ('weapon.frag.bonusshot ' failed the catalog lookup,
	// 실사고 2026-08-13), and no identifier/number/tiers cell legitimately carries edge whitespace. Display-text
	// columns that may keep intentional edge spaces use GetCellRaw below instead.
	FString GetCell(const TArray<FString>& Cells, int32 Index)
	{
		return Cells.IsValidIndex(Index) ? Cells[Index].TrimStartAndEnd() : FString();
	}

	// Raw (untrimmed) variant for display-text columns (DisplayName_*/Description_* — e.g. a "레벨 : " style
	// trailing-space label is authored intent there).
	FString GetCellRaw(const TArray<FString>& Cells, int32 Index)
	{
		return Cells.IsValidIndex(Index) ? Cells[Index] : FString();
	}

	// Validates ONE E*_Tiers cell's syntax ("C:15;R:30;E:60;L:100") — rarity initial in {C,R,E,L}, no duplicate
	// initial, magnitude parses as a float. Does not build FFPSRCardRarityTier (the importer re-parses this same
	// string into runtime tiers — the schema layer stays pure text validation, §6 contract: "Errors 수집, 에셋 무접촉").
	void ValidateTiersSyntax(const FString& Tiers, const FString& FileTag, int32 SourceRow, int32 EffectSlot1Based, TArray<FString>& Errors)
	{
		if (Tiers.IsEmpty())
		{
			return;
		}
		TArray<FString> Entries;
		Tiers.ParseIntoArray(Entries, TEXT(";"), /*InCullEmpty=*/true);

		TSet<TCHAR> SeenInitials;
		for (const FString& Entry : Entries)
		{
			FString InitialStr, MagnitudeStr;
			if (!Entry.Split(TEXT(":"), &InitialStr, &MagnitudeStr))
			{
				Errors.Add(FString::Printf(TEXT("%s:%d E%d_Tiers: '%s' is not <Initial>:<Magnitude>."), *FileTag, SourceRow, EffectSlot1Based, *Entry));
				continue;
			}
			InitialStr.TrimStartAndEndInline();
			MagnitudeStr.TrimStartAndEndInline();

			if (InitialStr.Len() != 1)
			{
				Errors.Add(FString::Printf(TEXT("%s:%d E%d_Tiers: rarity initial '%s' must be exactly one of C/R/E/L."), *FileTag, SourceRow, EffectSlot1Based, *InitialStr));
				continue;
			}
			const TCHAR Initial = FChar::ToUpper(InitialStr[0]);
			if (Initial != TEXT('C') && Initial != TEXT('R') && Initial != TEXT('E') && Initial != TEXT('L'))
			{
				Errors.Add(FString::Printf(TEXT("%s:%d E%d_Tiers: rarity initial '%c' must be one of C/R/E/L."), *FileTag, SourceRow, EffectSlot1Based, Initial));
				continue;
			}
			if (SeenInitials.Contains(Initial))
			{
				Errors.Add(FString::Printf(TEXT("%s:%d E%d_Tiers: rarity '%c' is duplicated."), *FileTag, SourceRow, EffectSlot1Based, Initial));
			}
			SeenInitials.Add(Initial);

			float Magnitude = 0.0f;
			if (!FDefaultValueHelper::ParseFloat(MagnitudeStr, Magnitude))
			{
				Errors.Add(FString::Printf(TEXT("%s:%d E%d_Tiers: magnitude '%s' (rarity %c) is not a number."), *FileTag, SourceRow, EffectSlot1Based, *MagnitudeStr, Initial));
			}
		}
	}

	// Validates ONE E*_Override cell's syntax ("k=v;k=v"), keys restricted to the catalog's Default* set.
	void ValidateOverrideSyntax(const FString& Override, const FString& FileTag, int32 SourceRow, int32 EffectSlot1Based, TArray<FString>& Errors)
	{
		if (Override.IsEmpty())
		{
			return;
		}
		TArray<FString> Pairs;
		Override.ParseIntoArray(Pairs, TEXT(";"), /*InCullEmpty=*/true);
		for (const FString& Pair : Pairs)
		{
			FString Key, Value;
			if (!Pair.Split(TEXT("="), &Key, &Value))
			{
				Errors.Add(FString::Printf(TEXT("%s:%d E%d_Override: '%s' is not key=value."), *FileTag, SourceRow, EffectSlot1Based, *Pair));
				continue;
			}
			Key.TrimStartAndEndInline();
			if (!IsValidOverrideKey(Key))
			{
				Errors.Add(FString::Printf(TEXT("%s:%d E%d_Override: unknown key '%s' (allowed: Op, ThisWeaponOnly, ShowAsPercent)."), *FileTag, SourceRow, EffectSlot1Based, *Key));
			}
		}
	}
}

bool FPSRCardCsv::ParseCards(const FString& CardsCsvText, const TArray<FFPSRCardCatalogRow>& Catalog, FFPSRCardCsvParseResult& InOut)
{
	const int32 ErrorsBefore = InOut.Errors.Num();
	const FString FileTag = TEXT("Cards.csv");

	const FCsvParser Parser(CardsCsvText);
	const FCsvParser::FRows& Rows = Parser.GetRows();
	if (Rows.Num() < 1)
	{
		InOut.Errors.Add(FString::Printf(TEXT("%s:1 header: file is empty or failed to parse."), *FileTag));
		return false;
	}

	const TArray<FString>& Expected = GetCardsExpectedHeaderInternal();
	TArray<FString> ActualHeader;
	ActualHeader.Reserve(Rows[0].Num());
	for (const TCHAR* Cell : Rows[0])
	{
		ActualHeader.Add(FString(Cell));
	}
	if (ActualHeader != Expected)
	{
		InOut.Errors.Add(FString::Printf(TEXT("%s:1 header: expected [%s] but found [%s]."),
			*FileTag, *FString::Join(Expected, TEXT(",")), *FString::Join(ActualHeader, TEXT(","))));
		return false; // column-index parsing below is meaningless against a mismatched header
	}

	TSet<FName> CatalogAttrIds;
	CatalogAttrIds.Reserve(Catalog.Num());
	for (const FFPSRCardCatalogRow& CatalogRow : Catalog)
	{
		CatalogAttrIds.Add(CatalogRow.AttrId);
	}

	TSet<FName> SeenCardIds;
	TSet<FString> SeenAssetNames; // P2-⑤: a duplicate AssetName makes existing-asset resolution (§5 CardId->AssetName
	                              // fallback) ambiguous between rows — same severity as a duplicate CardId.
	for (int32 RowIndex = 1; RowIndex < Rows.Num(); ++RowIndex)
	{
		const TArray<const TCHAR*>& RawRow = Rows[RowIndex];
		if (IsBlankRow(RawRow))
		{
			continue;
		}

		TArray<FString> Cells;
		Cells.Reserve(RawRow.Num());
		for (const TCHAR* C : RawRow)
		{
			Cells.Add(FString(C));
		}

		const int32 SourceRow = RowIndex + 1; // 1-based; header is row 1

		FFPSRCardCsvRow Row;
		Row.SourceRowIndex = SourceRow;

		// CardId — required, unique within the file.
		const FString CardIdStr = GetCell(Cells, 0);
		if (CardIdStr.IsEmpty())
		{
			InOut.Errors.Add(FString::Printf(TEXT("%s:%d CardId: empty — every row needs a unique CardId."), *FileTag, SourceRow));
		}
		else
		{
			Row.CardId = FName(*CardIdStr);
			bool bAlreadySeen = false;
			SeenCardIds.Add(Row.CardId, &bAlreadySeen);
			if (bAlreadySeen)
			{
				InOut.Errors.Add(FString::Printf(TEXT("%s:%d CardId: '%s' is duplicated in this file."), *FileTag, SourceRow, *CardIdStr));
			}
		}

		// AssetName — required (naming-lint target, §2-3-8), unique within the file (P2-⑤).
		Row.AssetName = GetCell(Cells, 1);
		if (Row.AssetName.IsEmpty())
		{
			InOut.Errors.Add(FString::Printf(TEXT("%s:%d AssetName: empty — required (DA_Card_<Group>_<Theme>)."), *FileTag, SourceRow));
		}
		else
		{
			bool bAssetNameAlreadySeen = false;
			SeenAssetNames.Add(Row.AssetName, &bAssetNameAlreadySeen);
			if (bAssetNameAlreadySeen)
			{
				InOut.Errors.Add(FString::Printf(TEXT("%s:%d AssetName: '%s' is duplicated in this file."), *FileTag, SourceRow, *Row.AssetName));
			}
		}

		// Group.
		{
			const FString GroupStr = GetCell(Cells, 2);
			const int64 Value = StaticEnum<ECardGroup>()->GetValueByNameString(GroupStr);
			if (Value == INDEX_NONE)
			{
				InOut.Errors.Add(FString::Printf(TEXT("%s:%d Group: '%s' is not a valid ECardGroup (Character|Weapon|WeaponUnlock)."), *FileTag, SourceRow, *GroupStr));
			}
			else
			{
				Row.Group = static_cast<ECardGroup>(Value);
			}
		}

		// Route (drives whether OwnerWeapon is required below).
		bool bRouteRequiresWeapon = false;
		{
			const FString RouteStr = GetCell(Cells, 3);
			const int64 Value = StaticEnum<EFPSRCardRoute>()->GetValueByNameString(RouteStr);
			if (Value == INDEX_NONE)
			{
				InOut.Errors.Add(FString::Printf(TEXT("%s:%d Route: '%s' is not a valid EFPSRCardRoute."), *FileTag, SourceRow, *RouteStr));
			}
			else
			{
				Row.Route = static_cast<EFPSRCardRoute>(Value);
				bRouteRequiresWeapon = (Row.Route == EFPSRCardRoute::LevelUpWeapon || Row.Route == EFPSRCardRoute::MissionClearWeaponFeature);
			}
		}

		// OwnerWeapon — semicolon-separated weapon-DA-asset-name list; required when Route is a weapon route
		// (§5 interface sketch, C2 2026-08-13 — preserves cards wired into multiple weapons' pools at once).
		{
			const FString OwnerWeaponCell = GetCell(Cells, 4);
			TArray<FString> RawEntries;
			OwnerWeaponCell.ParseIntoArray(RawEntries, TEXT(";"), /*InCullEmpty=*/true);
			for (FString& Entry : RawEntries)
			{
				Entry.TrimStartAndEndInline();
				if (!Entry.IsEmpty())
				{
					Row.OwnerWeapons.Add(Entry);
				}
			}
		}
		if (bRouteRequiresWeapon && Row.OwnerWeapons.Num() == 0)
		{
			InOut.Errors.Add(FString::Printf(TEXT("%s:%d OwnerWeapon: empty but Route requires a weapon (LevelUpWeapon/MissionClearWeaponFeature)."), *FileTag, SourceRow));
		}
		else if (!bRouteRequiresWeapon && Row.OwnerWeapons.Num() > 0)
		{
			// P2-⑧: a central-route (LevelUpGlobal/MissionClearNewWeapon) row with OwnerWeapon filled in is very
			// likely a copy-paste leftover — not an error (the importer's central routes never consult
			// OwnerWeapons), but worth a designer's attention.
			UE_LOG(LogFPSRCardCsvSchema, Warning, TEXT("%s:%d OwnerWeapon: '%s' is set but Route is not a weapon route — ignored by the importer."),
				*FileTag, SourceRow, *FString::Join(Row.OwnerWeapons, TEXT(";")));
		}

		// Weight — blank = default 1.0.
		{
			const FString WeightStr = GetCell(Cells, 5);
			if (WeightStr.IsEmpty())
			{
				Row.Weight = 1.0f;
			}
			else if (!FDefaultValueHelper::ParseFloat(WeightStr, Row.Weight))
			{
				InOut.Errors.Add(FString::Printf(TEXT("%s:%d Weight: '%s' is not a valid number."), *FileTag, SourceRow, *WeightStr));
			}
			else if (Row.Weight <= 0.0f)
			{
				// P2-⑦: a non-positive Weight never wins the weighted draw (UFPSRCardSubsystem::GetEffectiveWeight
				// clamps the final weight to >= 0) — not a parse error, but a card that can never be offered is
				// almost certainly a mistake.
				UE_LOG(LogFPSRCardCsvSchema, Warning, TEXT("%s:%d Weight: '%s' is <= 0 — this card's draw weight will never be positive, so it can never be offered."), *FileTag, SourceRow, *WeightStr);
			}
		}

		// Family — blank = importer derives from E1_Attr (§2-3-2 v3).
		{
			const FString FamilyStr = GetCell(Cells, 6);
			Row.Family = FamilyStr.IsEmpty() ? NAME_None : FName(*FamilyStr);
		}

		// DisplayName / Description (ko,en,ja) — no syntax to validate here (blank tolerated; [KO-TODO] markers
		// are an exporter/authoring convention, not a parser rule).
		Row.DisplayName[0] = GetCellRaw(Cells, 7);
		Row.DisplayName[1] = GetCellRaw(Cells, 8);
		Row.DisplayName[2] = GetCellRaw(Cells, 9);
		Row.Description[0] = GetCellRaw(Cells, 10);
		Row.Description[1] = GetCellRaw(Cells, 11);
		Row.Description[2] = GetCellRaw(Cells, 12);

		// E1..E3 effect column groups — a slot is "used" only when its *_Attr cell is non-empty.
		static constexpr int32 EffectBaseColumn = 13;
		for (int32 EffectSlot = 0; EffectSlot < 3; ++EffectSlot)
		{
			const int32 AttrColumn = EffectBaseColumn + EffectSlot * 3;
			const FString AttrStr = GetCell(Cells, AttrColumn);
			if (AttrStr.IsEmpty())
			{
				continue;
			}

			FFPSRCardCsvRow::FEffectCol Effect;
			Effect.AttrId = FName(*AttrStr);
			Effect.Override = GetCell(Cells, AttrColumn + 1);
			Effect.Tiers = GetCell(Cells, AttrColumn + 2);

			const int32 EffectSlot1Based = EffectSlot + 1;
			if (!CatalogAttrIds.Contains(Effect.AttrId))
			{
				InOut.Errors.Add(FString::Printf(TEXT("%s:%d E%d_Attr: '%s' not found in CardCatalog.csv."), *FileTag, SourceRow, EffectSlot1Based, *AttrStr));
			}
			ValidateOverrideSyntax(Effect.Override, FileTag, SourceRow, EffectSlot1Based, InOut.Errors);
			ValidateTiersSyntax(Effect.Tiers, FileTag, SourceRow, EffectSlot1Based, InOut.Errors);

			Row.Effects.Add(MoveTemp(Effect));
		}

		if (Row.Effects.Num() == 0)
		{
			InOut.Errors.Add(FString::Printf(TEXT("%s:%d Effects: card has no effects (E1_Attr..E3_Attr all blank)."), *FileTag, SourceRow));
		}

		InOut.Cards.Add(MoveTemp(Row));
	}

	return InOut.Errors.Num() == ErrorsBefore;
}

bool FPSRCardCsv::ParseCatalog(const FString& CatalogCsvText, FFPSRCardCsvParseResult& InOut)
{
	const int32 ErrorsBefore = InOut.Errors.Num();
	const FString FileTag = TEXT("CardCatalog.csv");

	const FCsvParser Parser(CatalogCsvText);
	const FCsvParser::FRows& Rows = Parser.GetRows();
	if (Rows.Num() < 1)
	{
		InOut.Errors.Add(FString::Printf(TEXT("%s:1 header: file is empty or failed to parse."), *FileTag));
		return false;
	}

	const TArray<FString>& Expected = GetCatalogExpectedHeaderInternal();
	TArray<FString> ActualHeader;
	ActualHeader.Reserve(Rows[0].Num());
	for (const TCHAR* Cell : Rows[0])
	{
		ActualHeader.Add(FString(Cell));
	}
	if (ActualHeader != Expected)
	{
		InOut.Errors.Add(FString::Printf(TEXT("%s:1 header: expected [%s] but found [%s]."),
			*FileTag, *FString::Join(Expected, TEXT(",")), *FString::Join(ActualHeader, TEXT(","))));
		return false;
	}

	TSet<FName> SeenAttrIds;
	for (int32 RowIndex = 1; RowIndex < Rows.Num(); ++RowIndex)
	{
		const TArray<const TCHAR*>& RawRow = Rows[RowIndex];
		if (IsBlankRow(RawRow))
		{
			continue;
		}

		TArray<FString> Cells;
		Cells.Reserve(RawRow.Num());
		for (const TCHAR* C : RawRow)
		{
			Cells.Add(FString(C));
		}

		const int32 SourceRow = RowIndex + 1;
		FFPSRCardCatalogRow Row;
		Row.SourceRowIndex = SourceRow;

		const FString AttrIdStr = GetCell(Cells, 0);
		if (AttrIdStr.IsEmpty())
		{
			InOut.Errors.Add(FString::Printf(TEXT("%s:%d AttrId: empty — every row needs a unique AttrId."), *FileTag, SourceRow));
		}
		else
		{
			Row.AttrId = FName(*AttrIdStr);
			bool bAlreadySeen = false;
			SeenAttrIds.Add(Row.AttrId, &bAlreadySeen);
			if (bAlreadySeen)
			{
				InOut.Errors.Add(FString::Printf(TEXT("%s:%d AttrId: '%s' is duplicated in this file."), *FileTag, SourceRow, *AttrIdStr));
			}
		}

		const FString EffectTypeStr = GetCell(Cells, 1);
		Row.EffectType = FName(*EffectTypeStr);
		if (!GetValidEffectTypesInternal().Contains(Row.EffectType))
		{
			InOut.Errors.Add(FString::Printf(TEXT("%s:%d EffectType: '%s' is not one of CharGE|CharPassive|WeaponStat|WeaponBehavior|GrantWeapon."), *FileTag, SourceRow, *EffectTypeStr));
		}

		Row.Payload = GetCell(Cells, 2);
		Row.DefaultOp = GetCell(Cells, 3);
		Row.DefaultThisWeaponOnly = GetCell(Cells, 4);
		Row.ShowAsPercent = GetCell(Cells, 5);
		// Cells[6] = Notes — author-facing commentary only, not carried into the parsed struct.

		InOut.Catalog.Add(MoveTemp(Row));
	}

	return InOut.Errors.Num() == ErrorsBefore;
}
