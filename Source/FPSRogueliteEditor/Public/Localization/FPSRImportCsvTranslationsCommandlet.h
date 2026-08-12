// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/GatherTextCommandletBase.h"
#include "FPSRImportCsvTranslationsCommandlet.generated.h"

/** Custom gather step: reads translation columns (column name == culture code, e.g. "en","ja")
 *  from the authoring CSVs and imports them into per-culture archives via FLocTextHelper.
 *  Runs between Gather and Compile in the GatherText step chain ([GatherTextStep{N}] config).
 *  Config keys (own section): CSVFiles (semicolon list, ContentDir-relative), Cultures (comma list). */
UCLASS()
class UFPSRImportCsvTranslationsCommandlet : public UGatherTextCommandletBase
{
	GENERATED_BODY()
public:
	virtual int32 Main(const FString& Params) override; // 반환: 0=성공, 비0=실패(파싱/컬럼/아카이브 오류)

	// GatherText 체인은 phase 순서로 스케줄된다 — 이 스텝은 아카이브 파일이 외부 데이터(여기서는 CSV의 en/ja 컬럼)로
	// 갱신되는 Import phase에서 돈다(엔진 ImportDialogueScriptCommandlet/ImportLocalizedDialogueCommandlet과 동일한
	// phase — Docs/Specs/LOC0_StringTablePipeline.md §11 미결정 항목 참조).
	virtual EGatherTextCommandletPhase GetPhase() const override { return EGatherTextCommandletPhase::Import; }

private:
	/** CSV 1개(전체 경로)를 파싱해 Cultures에 있는 각 문화권 컬럼을 아카이브에 주입한다.
	 *  Namespace는 파일명에서 유도한다(ST_<Namespace>.csv — FPSRoguelite.cpp의 LOCTABLE_FROMFILE_GAME 등록과 1:1). */
	bool ImportCsvForNamespace(FLocTextHelper& InLocTextHelper, const FString& InCsvFilePath, const FString& InNamespace, const TArray<FString>& InCultures);
};
