// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Card/FPSRCardTypes.h"

/** One parsed Cards.csv row (schema = SSOT §2-3-10; effects flattened as E1..E3 column groups). */
struct FFPSRCardCsvRow
{
	FName    CardId;                    // 행 키 = 세이브 키 = 로컬라이징 키 접두. 필수·파일 내 유일
	FString  AssetName;                 // DA_Card_<Group>_<Theme> 린트 대상. 필수
	ECardGroup Group = ECardGroup::Character;
	EFPSRCardRoute Route = EFPSRCardRoute::LevelUpGlobal;
	FString  OwnerWeapon;               // 무기 루트(LevelUpWeapon/MissionClearWeaponFeature)일 때 무기 DA 에셋명. 그 외 빈칸
	float    Weight = 1.0f;
	FName    Family;                    // 공란 = E1 AttrId에서 파생(§2-3-2 v3)
	FString  DisplayName[3];            // ko,en,ja (ST_Card.csv 원천 — DA에는 FromStringTable 참조가 들어간다)
	FString  Description[3];
	struct FEffectCol { FName AttrId; FString Override; FString Tiers; };  // Tiers 예: "C:15;R:30;E:60;L:100"
	TArray<FEffectCol> Effects;         // 유효(AttrId 비공란) 컬럼군만, 최대 3
	int32    SourceRowIndex = 0;        // 오류 메시지용(1-base, 헤더=1)
};

/** One parsed CardCatalog.csv row. EffectType은 클로즈드 문자열 enum — 임포터 타입 팩토리의 키. */
struct FFPSRCardCatalogRow
{
	FName   AttrId;                     // 예: char.maxhealth / weapon.firerate / weapon.frag.dot / unlock.smg
	FName   EffectType;                 // CharGE | CharPassive | WeaponStat | WeaponBehavior | GrantWeapon (5종 1:1)
	FString Payload;                    // CharGE/CharPassive=클래스 경로, WeaponBehavior/GrantWeapon=에셋 경로, WeaponStat=EFPSRWeaponStat raw 이름
	FString DefaultOp;                  // WeaponStat 전용: EFPSRWeaponModOp raw 이름(빈칸=PercentMultiply)
	FString DefaultThisWeaponOnly;      // WeaponStat 전용: true/false(빈칸=true)
	FString ShowAsPercent;              // CharGE 전용: true/false(빈칸=false)
	int32   SourceRowIndex = 0;
};

/** 파싱 전용(에셋 무접촉). 모든 오류는 "<파일>:<행> <컬럼>: <내용>" 포맷으로 수집 — 첫 오류에서 멈추지 않는다. */
struct FFPSRCardCsvParseResult { TArray<FFPSRCardCsvRow> Cards; TArray<FFPSRCardCatalogRow> Catalog; TArray<FString> Errors; };

namespace FPSRCardCsv
{
	/** CSV 텍스트 → 구조체. 검사: 헤더 정확 일치, CardId/AttrId 유일, enum 파싱, Tiers 문법(레어도 이니셜 C/R/E/L,
	 *  중복 금지, magnitude=float), E*_Attr가 카탈로그에 존재, 무기 루트인데 OwnerWeapon 공란, 효과 0개 카드. */
	FPSROGUELITEEDITOR_API bool ParseCards(const FString& CardsCsvText, const TArray<FFPSRCardCatalogRow>& Catalog, FFPSRCardCsvParseResult& InOut);
	FPSROGUELITEEDITOR_API bool ParseCatalog(const FString& CatalogCsvText, FFPSRCardCsvParseResult& InOut);
}
