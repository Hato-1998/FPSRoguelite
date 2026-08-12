// Copyright Epic Games, Inc. All Rights Reserved.
#include "Misc/AutomationTest.h"

#include "CardImport/FPSRCardCsvSchema.h"

#if WITH_AUTOMATION_TESTS

namespace
{
	// Anonymous-namespace helpers merge across translation units in a unity build (project's
	// test-unity-anon-namespace-collision memory) — "CardCsvSchemaTest" suffix keeps these names unique to this file.
	const FString CardsHeaderCardCsvSchemaTest = TEXT("CardId,AssetName,Group,Route,OwnerWeapon,Weight,Family,DisplayName_ko,DisplayName_en,DisplayName_ja,Description_ko,Description_en,Description_ja,E1_Attr,E1_Override,E1_Tiers,E2_Attr,E2_Override,E2_Tiers,E3_Attr,E3_Override,E3_Tiers");
	const FString CatalogHeaderCardCsvSchemaTest = TEXT("AttrId,EffectType,Payload,DefaultOp,DefaultThisWeaponOnly,ShowAsPercent,Notes");

	// One catalog row wired to char.maxhealth (CharGE) so Cards.csv fixtures can reference a real AttrId.
	FString OneValidCatalogRowCardCsvSchemaTest()
	{
		return CatalogHeaderCardCsvSchemaTest + TEXT("\n") + TEXT("char.maxhealth,CharGE,/Game/Cards/Character/GameplayEffect/GE_Card_MaxHealth.GE_Card_MaxHealth_C,,,,");
	}

	TArray<FFPSRCardCatalogRow> ParseOneValidCatalogCardCsvSchemaTest(FAutomationTestBase& Test)
	{
		FFPSRCardCsvParseResult Result;
		const bool bOk = FPSRCardCsv::ParseCatalog(OneValidCatalogRowCardCsvSchemaTest(), Result);
		Test.TestTrue(TEXT("fixture catalog parses cleanly"), bOk);
		Test.TestEqual(TEXT("fixture catalog has 1 row"), Result.Catalog.Num(), 1);
		return Result.Catalog;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSRCardCsvSchemaNormalTest, "FPSRoguelite.Editor.CardCsv.Schema.Normal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFPSRCardCsvSchemaNormalTest::RunTest(const FString& Parameters)
{
	const TArray<FFPSRCardCatalogRow> Catalog = ParseOneValidCatalogCardCsvSchemaTest(*this);

	const FString CardsCsv = CardsHeaderCardCsvSchemaTest + TEXT("\n")
		+ TEXT("Card.MaxHealth,DA_Card_MaxHealth,Character,LevelUpGlobal,,1.0,,체력,Health,体力,설명,Desc,説明,char.maxhealth,,C:15;R:30;E:60;L:100,,,,,,");

	FFPSRCardCsvParseResult Result;
	const bool bOk = FPSRCardCsv::ParseCards(CardsCsv, Catalog, Result);

	TestTrue(TEXT("well-formed Cards.csv parses without error"), bOk);
	TestEqual(TEXT("no errors collected"), Result.Errors.Num(), 0);
	TestEqual(TEXT("one card row parsed"), Result.Cards.Num(), 1);
	if (Result.Cards.Num() == 1)
	{
		const FFPSRCardCsvRow& Row = Result.Cards[0];
		TestEqual(TEXT("CardId"), Row.CardId, FName(TEXT("Card.MaxHealth")));
		TestEqual(TEXT("AssetName"), Row.AssetName, FString(TEXT("DA_Card_MaxHealth")));
		TestTrue(TEXT("Group == Character"), Row.Group == ECardGroup::Character);
		TestTrue(TEXT("Route == LevelUpGlobal"), Row.Route == EFPSRCardRoute::LevelUpGlobal);
		TestEqual(TEXT("Weight parsed"), Row.Weight, 1.0f);
		TestTrue(TEXT("Family blank -> NAME_None (importer derives)"), Row.Family.IsNone());
		TestEqual(TEXT("exactly one effect column used (E1)"), Row.Effects.Num(), 1);
		if (Row.Effects.Num() == 1)
		{
			TestEqual(TEXT("E1 AttrId"), Row.Effects[0].AttrId, FName(TEXT("char.maxhealth")));
			TestEqual(TEXT("E1 Tiers raw string preserved"), Row.Effects[0].Tiers, FString(TEXT("C:15;R:30;E:60;L:100")));
		}
		TestEqual(TEXT("SourceRowIndex is 1-based with header=1"), Row.SourceRowIndex, 2);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSRCardCsvSchemaMultiEffectTest, "FPSRoguelite.Editor.CardCsv.Schema.MultiEffect",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFPSRCardCsvSchemaMultiEffectTest::RunTest(const FString& Parameters)
{
	FFPSRCardCsvParseResult CatalogResult;
	const FString CatalogCsv = CatalogHeaderCardCsvSchemaTest + TEXT("\n")
		+ TEXT("weapon.firerate,WeaponStat,FireRate,PercentMultiply,true,,\n")
		+ TEXT("weapon.magsize,WeaponStat,MagSize,Additive,true,,");
	TestTrue(TEXT("two-row catalog parses"), FPSRCardCsv::ParseCatalog(CatalogCsv, CatalogResult));
	TestEqual(TEXT("catalog has 2 rows"), CatalogResult.Catalog.Num(), 2);

	const FString CardsCsv = CardsHeaderCardCsvSchemaTest + TEXT("\n")
		+ TEXT("Card.FireRateMagTradeoff,DA_Card_FireRate_MagTradeoff,Weapon,LevelUpWeapon,DA_Weapon_Rifle,1.0,Card.Family.RifleTradeoff,연사+/장탄-,Fire+/Mag-,連射+/弾-,,,,")
		+ TEXT("weapon.firerate,,C:0.05;R:0.10,weapon.magsize,Op=Additive;ThisWeaponOnly=true,C:-2;R:-4,,,");

	FFPSRCardCsvParseResult Result;
	const bool bOk = FPSRCardCsv::ParseCards(CardsCsv, CatalogResult.Catalog, Result);

	TestTrue(TEXT("multi-effect card parses without error"), bOk);
	TestEqual(TEXT("no errors"), Result.Errors.Num(), 0);
	TestEqual(TEXT("one card row"), Result.Cards.Num(), 1);
	if (Result.Cards.Num() == 1)
	{
		const FFPSRCardCsvRow& Row = Result.Cards[0];
		TestEqual(TEXT("OwnerWeapon carried through for a weapon route"), Row.OwnerWeapon, FString(TEXT("DA_Weapon_Rifle")));
		TestEqual(TEXT("two effect columns used (E1+E2)"), Row.Effects.Num(), 2);
		if (Row.Effects.Num() == 2)
		{
			TestEqual(TEXT("E1 AttrId"), Row.Effects[0].AttrId, FName(TEXT("weapon.firerate")));
			TestEqual(TEXT("E2 AttrId"), Row.Effects[1].AttrId, FName(TEXT("weapon.magsize")));
			TestEqual(TEXT("E2 Override preserved verbatim"), Row.Effects[1].Override, FString(TEXT("Op=Additive;ThisWeaponOnly=true")));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSRCardCsvSchemaBadHeaderTest, "FPSRoguelite.Editor.CardCsv.Schema.BadHeader",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFPSRCardCsvSchemaBadHeaderTest::RunTest(const FString& Parameters)
{
	// Cards.csv: a column renamed/reordered must fail with a header-mismatch error, not silently misalign columns.
	{
		const FString BadCardsCsv = FString(TEXT("CardId,AssetName,Group,Route,OwnerWeapon,Weight,Family,DisplayName_ko,DisplayName_en,DisplayName_ja,Description_ko,Description_en,Description_ja,E1_Attr,E1_Override,E1_Tiers,E2_Attr,E2_Override,E2_Tiers,E3_Attr,E3_Override\n"))
			+ TEXT("x,y,Character,LevelUpGlobal,,1,,,,,,,,,,,,,,,");
		FFPSRCardCsvParseResult Result;
		const bool bOk = FPSRCardCsv::ParseCards(BadCardsCsv, {}, Result);
		TestFalse(TEXT("Cards.csv missing a trailing column fails"), bOk);
		TestTrue(TEXT("at least one error collected"), Result.Errors.Num() > 0);
		TestEqual(TEXT("no rows parsed on header mismatch"), Result.Cards.Num(), 0);
	}
	// CardCatalog.csv: same contract.
	{
		const FString BadCatalogCsv = FString(TEXT("AttrId,EffectType,Payload,DefaultOp,DefaultThisWeaponOnly,ShowAsPercent\n"))
			+ TEXT("char.maxhealth,CharGE,/Game/x,,,,");
		FFPSRCardCsvParseResult Result;
		const bool bOk = FPSRCardCsv::ParseCatalog(BadCatalogCsv, Result);
		TestFalse(TEXT("CardCatalog.csv missing the Notes column fails"), bOk);
		TestTrue(TEXT("at least one error collected"), Result.Errors.Num() > 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSRCardCsvSchemaDuplicateKeyTest, "FPSRoguelite.Editor.CardCsv.Schema.DuplicateKey",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFPSRCardCsvSchemaDuplicateKeyTest::RunTest(const FString& Parameters)
{
	// Duplicate CardId within Cards.csv.
	{
		const TArray<FFPSRCardCatalogRow> Catalog = ParseOneValidCatalogCardCsvSchemaTest(*this);
		const FString CardsCsv = CardsHeaderCardCsvSchemaTest + TEXT("\n")
			+ TEXT("Card.Dup,DA_Card_A,Character,LevelUpGlobal,,1,,,,,,,,char.maxhealth,,C:1,,,,,,\n")
			+ TEXT("Card.Dup,DA_Card_B,Character,LevelUpGlobal,,1,,,,,,,,char.maxhealth,,C:2,,,,,,");
		FFPSRCardCsvParseResult Result;
		const bool bOk = FPSRCardCsv::ParseCards(CardsCsv, Catalog, Result);
		TestFalse(TEXT("duplicate CardId fails"), bOk);
		TestTrue(TEXT("both rows still collected despite the error (errors don't stop parsing)"), Result.Cards.Num() == 2);
		bool bFoundDupError = false;
		for (const FString& Error : Result.Errors)
		{
			if (Error.Contains(TEXT("duplicated")))
			{
				bFoundDupError = true;
			}
		}
		TestTrue(TEXT("a 'duplicated' error was reported"), bFoundDupError);
	}
	// Duplicate AttrId within CardCatalog.csv.
	{
		const FString CatalogCsv = CatalogHeaderCardCsvSchemaTest + TEXT("\n")
			+ TEXT("char.maxhealth,CharGE,/Game/A,,,,\n")
			+ TEXT("char.maxhealth,CharGE,/Game/B,,,,");
		FFPSRCardCsvParseResult Result;
		const bool bOk = FPSRCardCsv::ParseCatalog(CatalogCsv, Result);
		TestFalse(TEXT("duplicate AttrId fails"), bOk);
		TestEqual(TEXT("both catalog rows still collected"), Result.Catalog.Num(), 2);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSRCardCsvSchemaTiersSyntaxTest, "FPSRoguelite.Editor.CardCsv.Schema.TiersSyntax",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFPSRCardCsvSchemaTiersSyntaxTest::RunTest(const FString& Parameters)
{
	const TArray<FFPSRCardCatalogRow> Catalog = ParseOneValidCatalogCardCsvSchemaTest(*this);

	auto MakeCardsCsv = [](const FString& Tiers)
	{
		return CardsHeaderCardCsvSchemaTest + TEXT("\n")
			+ FString::Printf(TEXT("Card.Test,DA_Card_Test,Character,LevelUpGlobal,,1,,,,,,,,char.maxhealth,,%s,,,,,,"), *Tiers);
	};

	// Invalid rarity initial.
	{
		FFPSRCardCsvParseResult Result;
		const bool bOk = FPSRCardCsv::ParseCards(MakeCardsCsv(TEXT("X:15")), Catalog, Result);
		TestFalse(TEXT("invalid rarity initial 'X' fails"), bOk);
	}
	// Duplicate rarity initial.
	{
		FFPSRCardCsvParseResult Result;
		const bool bOk = FPSRCardCsv::ParseCards(MakeCardsCsv(TEXT("C:15;C:30")), Catalog, Result);
		TestFalse(TEXT("duplicate rarity 'C' fails"), bOk);
	}
	// Non-numeric magnitude.
	{
		FFPSRCardCsvParseResult Result;
		const bool bOk = FPSRCardCsv::ParseCards(MakeCardsCsv(TEXT("C:notanumber")), Catalog, Result);
		TestFalse(TEXT("non-numeric magnitude fails"), bOk);
	}
	// Malformed entry (missing colon).
	{
		FFPSRCardCsvParseResult Result;
		const bool bOk = FPSRCardCsv::ParseCards(MakeCardsCsv(TEXT("C15")), Catalog, Result);
		TestFalse(TEXT("entry without ':' fails"), bOk);
	}
	// Valid multi-tier string succeeds.
	{
		FFPSRCardCsvParseResult Result;
		const bool bOk = FPSRCardCsv::ParseCards(MakeCardsCsv(TEXT("C:15;R:30;E:60;L:100")), Catalog, Result);
		TestTrue(TEXT("well-formed 4-tier string succeeds"), bOk);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSRCardCsvSchemaMissingAttrTest, "FPSRoguelite.Editor.CardCsv.Schema.MissingAttrInCatalog",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFPSRCardCsvSchemaMissingAttrTest::RunTest(const FString& Parameters)
{
	const TArray<FFPSRCardCatalogRow> Catalog = ParseOneValidCatalogCardCsvSchemaTest(*this); // only has char.maxhealth

	const FString CardsCsv = CardsHeaderCardCsvSchemaTest + TEXT("\n")
		+ TEXT("Card.Ghost,DA_Card_Ghost,Character,LevelUpGlobal,,1,,,,,,,,attr.does.not.exist,,C:1,,,,,,");

	FFPSRCardCsvParseResult Result;
	const bool bOk = FPSRCardCsv::ParseCards(CardsCsv, Catalog, Result);
	TestFalse(TEXT("E1_Attr referencing an AttrId absent from CardCatalog.csv fails"), bOk);
	bool bFoundNotFoundError = false;
	for (const FString& Error : Result.Errors)
	{
		if (Error.Contains(TEXT("not found in CardCatalog.csv")))
		{
			bFoundNotFoundError = true;
		}
	}
	TestTrue(TEXT("a 'not found in CardCatalog.csv' error was reported"), bFoundNotFoundError);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSRCardCsvSchemaOwnerWeaponBlankTest, "FPSRoguelite.Editor.CardCsv.Schema.OwnerWeaponBlank",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFPSRCardCsvSchemaOwnerWeaponBlankTest::RunTest(const FString& Parameters)
{
	const TArray<FFPSRCardCatalogRow> Catalog = ParseOneValidCatalogCardCsvSchemaTest(*this);

	// Route=LevelUpWeapon but OwnerWeapon is blank — must fail (§5 interface sketch).
	{
		const FString CardsCsv = CardsHeaderCardCsvSchemaTest + TEXT("\n")
			+ TEXT("Card.NoOwner,DA_Card_NoOwner,Weapon,LevelUpWeapon,,1,,,,,,,,char.maxhealth,,C:1,,,,,,");
		FFPSRCardCsvParseResult Result;
		const bool bOk = FPSRCardCsv::ParseCards(CardsCsv, Catalog, Result);
		TestFalse(TEXT("LevelUpWeapon route with blank OwnerWeapon fails"), bOk);
	}
	// Route=MissionClearWeaponFeature but OwnerWeapon is blank — same rule.
	{
		const FString CardsCsv = CardsHeaderCardCsvSchemaTest + TEXT("\n")
			+ TEXT("Card.NoOwner2,DA_Card_NoOwner2,Weapon,MissionClearWeaponFeature,,1,,,,,,,,char.maxhealth,,C:1,,,,,,");
		FFPSRCardCsvParseResult Result;
		const bool bOk = FPSRCardCsv::ParseCards(CardsCsv, Catalog, Result);
		TestFalse(TEXT("MissionClearWeaponFeature route with blank OwnerWeapon fails"), bOk);
	}
	// Route=LevelUpGlobal (not a weapon route) with blank OwnerWeapon is fine.
	{
		const FString CardsCsv = CardsHeaderCardCsvSchemaTest + TEXT("\n")
			+ TEXT("Card.NoOwnerOk,DA_Card_NoOwnerOk,Character,LevelUpGlobal,,1,,,,,,,,char.maxhealth,,C:1,,,,,,");
		FFPSRCardCsvParseResult Result;
		const bool bOk = FPSRCardCsv::ParseCards(CardsCsv, Catalog, Result);
		TestTrue(TEXT("LevelUpGlobal route with blank OwnerWeapon succeeds"), bOk);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSRCardCsvSchemaZeroEffectsTest, "FPSRoguelite.Editor.CardCsv.Schema.ZeroEffects",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFPSRCardCsvSchemaZeroEffectsTest::RunTest(const FString& Parameters)
{
	const TArray<FFPSRCardCatalogRow> Catalog = ParseOneValidCatalogCardCsvSchemaTest(*this);
	const FString CardsCsv = CardsHeaderCardCsvSchemaTest + TEXT("\n")
		+ TEXT("Card.Empty,DA_Card_Empty,Character,LevelUpGlobal,,1,,,,,,,,,,,,,,,,");

	FFPSRCardCsvParseResult Result;
	const bool bOk = FPSRCardCsv::ParseCards(CardsCsv, Catalog, Result);
	TestFalse(TEXT("a card with all three E*_Attr blank fails (no effects)"), bOk);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSRCardCsvSchemaCatalogEffectTypeTest, "FPSRoguelite.Editor.CardCsv.Schema.CatalogEffectType",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFPSRCardCsvSchemaCatalogEffectTypeTest::RunTest(const FString& Parameters)
{
	// Invalid EffectType string.
	{
		const FString CatalogCsv = CatalogHeaderCardCsvSchemaTest + TEXT("\n")
			+ TEXT("char.maxhealth,NotARealType,/Game/A,,,,");
		FFPSRCardCsvParseResult Result;
		const bool bOk = FPSRCardCsv::ParseCatalog(CatalogCsv, Result);
		TestFalse(TEXT("EffectType outside the closed 5-value set fails"), bOk);
	}
	// All 5 valid EffectType strings succeed.
	{
		const FString CatalogCsv = CatalogHeaderCardCsvSchemaTest + TEXT("\n")
			+ TEXT("a.chargelabel,CharGE,/Game/A,,,,\n")
			+ TEXT("b.charpassive,CharPassive,/Game/B,,,,\n")
			+ TEXT("c.weaponstat,WeaponStat,FireRate,,,,\n")
			+ TEXT("d.weaponbehavior,WeaponBehavior,/Game/D,,,,\n")
			+ TEXT("e.grantweapon,GrantWeapon,/Game/E,,,,");
		FFPSRCardCsvParseResult Result;
		const bool bOk = FPSRCardCsv::ParseCatalog(CatalogCsv, Result);
		TestTrue(TEXT("all 5 closed EffectType values parse cleanly"), bOk);
		TestEqual(TEXT("5 rows parsed"), Result.Catalog.Num(), 5);
	}
	return true;
}

#endif // WITH_AUTOMATION_TESTS
