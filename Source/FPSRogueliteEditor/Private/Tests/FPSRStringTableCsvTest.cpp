// Copyright Epic Games, Inc. All Rights Reserved.
#include "Misc/AutomationTest.h"

#include "Internationalization/Text.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/Csv/CsvParser.h"

#if WITH_AUTOMATION_TESTS

namespace
{
	// Anonymous-namespace helpers merge across translation units in a unity build (see the project's
	// test-unity-anon-namespace-collision memory) — "StCsvTest" suffix keeps these names unique to this file.
	struct FFPSRParsedCsvStCsvTest
	{
		TArray<FString> Header;
		TArray<TArray<FString>> Rows; // excludes the header row
	};

	bool LoadAndParseCsvStCsvTest(const FString& InRelativeContentPath, FFPSRParsedCsvStCsvTest& OutParsed, FAutomationTestBase& InTest)
	{
		const FString FullPath = FPaths::ProjectContentDir() / InRelativeContentPath;

		FString FileContents;
		if (!FFileHelper::LoadFileToString(FileContents, *FullPath))
		{
			InTest.AddError(FString::Printf(TEXT("Failed to load CSV '%s'."), *FullPath));
			return false;
		}

		const FCsvParser Parser(FileContents);
		const FCsvParser::FRows& Rows = Parser.GetRows();
		if (Rows.Num() < 1)
		{
			InTest.AddError(FString::Printf(TEXT("CSV '%s' has no header row."), *FullPath));
			return false;
		}

		for (const TCHAR* Cell : Rows[0])
		{
			OutParsed.Header.Add(FString(Cell));
		}

		for (int32 RowIndex = 1; RowIndex < Rows.Num(); ++RowIndex)
		{
			TArray<FString> RowStrings;
			for (const TCHAR* Cell : Rows[RowIndex])
			{
				RowStrings.Add(FString(Cell));
			}
			OutParsed.Rows.Add(MoveTemp(RowStrings));
		}

		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSRStringTableCsvTest, "FPSRoguelite.Editor.Localization.StringTableCsv",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFPSRStringTableCsvTest::RunTest(const FString& Parameters)
{
	// §12-3 검사 ①②③: 3개 CSV 각각 (1) 파싱 성공 + 헤더 일치 (2) Key 유일 (3) en/ja 빈칸은 경고만(실패 아님).
	const TArray<FString> ExpectedHeader = { TEXT("Key"), TEXT("SourceString"), TEXT("en"), TEXT("ja") };
	const TArray<FString> RelativePaths = {
		TEXT("StringTables/ST_UI.csv"),
		TEXT("StringTables/ST_CardEffect.csv"),
		TEXT("StringTables/ST_Card.csv")
	};

	for (const FString& RelativePath : RelativePaths)
	{
		FFPSRParsedCsvStCsvTest Parsed;
		const bool bParsed = LoadAndParseCsvStCsvTest(RelativePath, Parsed, *this);
		TestTrue(FString::Printf(TEXT("%s parses"), *RelativePath), bParsed);
		if (!bParsed)
		{
			continue;
		}

		TestTrue(FString::Printf(TEXT("%s header is Key,SourceString,en,ja"), *RelativePath), Parsed.Header == ExpectedHeader);

		const int32 KeyColumn = Parsed.Header.IndexOfByKey(FString(TEXT("Key")));
		const int32 EnColumn = Parsed.Header.IndexOfByKey(FString(TEXT("en")));
		const int32 JaColumn = Parsed.Header.IndexOfByKey(FString(TEXT("ja")));

		// Key uniqueness within the table.
		TSet<FString> SeenKeys;
		for (const TArray<FString>& Row : Parsed.Rows)
		{
			if (!Row.IsValidIndex(KeyColumn))
			{
				continue;
			}
			bool bAlreadyInSet = false;
			SeenKeys.Add(Row[KeyColumn], &bAlreadyInSet);
			TestFalse(FString::Printf(TEXT("%s: key '%s' is unique"), *RelativePath, *Row[KeyColumn]), bAlreadyInSet);
		}

		// en/ja blanks are reported as warnings only — never fail the test (§12-3-3).
		for (const TArray<FString>& Row : Parsed.Rows)
		{
			if (!Row.IsValidIndex(KeyColumn))
			{
				continue;
			}
			if (Row.IsValidIndex(EnColumn) && Row[EnColumn].IsEmpty())
			{
				AddWarning(FString::Printf(TEXT("%s: key '%s' has an empty 'en' translation."), *RelativePath, *Row[KeyColumn]));
			}
			if (Row.IsValidIndex(JaColumn) && Row[JaColumn].IsEmpty())
			{
				AddWarning(FString::Printf(TEXT("%s: key '%s' has an empty 'ja' translation."), *RelativePath, *Row[KeyColumn]));
			}
		}
	}

	// §12-3 검사 ④: 런타임 문자열 테이블이 시드 키를 소스 문자열로 해석한다(모듈 StartupModule이 에디터 프로세스
	// 시작 시 이미 등록을 마쳤다 — Source/FPSRoguelite/Private/FPSRoguelite.cpp).
	const FText Resolved = FText::FromStringTable(TEXT("UI"), TEXT("Debug.LocSmoke"));
	TestEqual(TEXT("FText::FromStringTable(\"UI\",\"Debug.LocSmoke\") resolves to the seed source string"),
		Resolved.ToString(), FString(TEXT("로컬라이제이션 시드")));

	return true;
}

#endif // WITH_AUTOMATION_TESTS
