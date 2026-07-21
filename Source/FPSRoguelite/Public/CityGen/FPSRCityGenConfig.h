// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "FPSRCityGenConfig.generated.h"

class UStaticMesh;
class UFPSRCityGenKit;

/** U22a-A 도시 건물 생성 툴(Content/Python/fpsr_citygen.py)이 읽는 "이 건물에 쓸 에셋" 설정.
 *
 *  워크플로: `1. Collect Modular Meshes`가 규약 통과 메시를 Kit에 모으고 → `2. Fill Config from Kit`으로
 *  여기 후보를 채운 뒤 → 사용자는 **안 쓸 것만 지운다**. 각 카테고리가 배열인 이유는 두 가지다:
 *   ① 여러 개 남기면 파사드처럼 섞어 써서 건물이 단조롭지 않고,
 *   ② 하나만 남기면 그게 고정으로 쓰여 결정적이다.
 *  (단일 값으로 두면 규격 밖 메시를 아무거나 고를 수 있어 건물이 조용히 어긋났다 — 2026-07-21 개편)
 *
 *  에디터 툴 전용(런타임 로직 없음). Python 브릿지 load_config_from_dataasset()가 각 UPROPERTY를
 *  이름으로 읽으므로, 프로퍼티명을 바꾸면 그 매핑도 함께 고칠 것.
 *  조립 규약: 벽 250폭 · 층 300 · 피벗 밑변 왼쪽 +X. */
UCLASS(BlueprintType)
class FPSROGUELITE_API UFPSRCityGenConfig : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** 후보를 가져온 수집 목록(`2. Fill Config from Kit`이 자동 설정). 검증·재채움의 출처. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CityGen")
	TObjectPtr<UFPSRCityGenKit> Kit;

	/** 파사드(창문벽) 후보 — 층 외벽 칸마다 **무작위로 하나씩** 골라 배치. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CityGen|Pieces", meta = (AllowedClasses = "/Script/Engine.StaticMesh"))
	TArray<TObjectPtr<UStaticMesh>> Facades;

	/** 코너 기둥 후보 — 건물마다 하나가 뽑혀 네 모서리에 일관되게 쓰인다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CityGen|Pieces", meta = (AllowedClasses = "/Script/Engine.StaticMesh"))
	TArray<TObjectPtr<UStaticMesh>> Corners;

	/** 문 후보 — 1층 정면 중앙 한 칸을 파사드 대신 대체. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CityGen|Pieces", meta = (AllowedClasses = "/Script/Engine.StaticMesh"))
	TArray<TObjectPtr<UStaticMesh>> Doors;

	/** 옥상 바닥판 후보 — 최상층 위를 격자로 덮는다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CityGen|Pieces", meta = (AllowedClasses = "/Script/Engine.StaticMesh"))
	TArray<TObjectPtr<UStaticMesh>> RoofFloors;

	/** 층 경계 띠 장식 후보 — 각 층 가장자리 4면. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CityGen|Pieces", meta = (AllowedClasses = "/Script/Engine.StaticMesh"))
	TArray<TObjectPtr<UStaticMesh>> CorniceTrims;

	/** 옥상 소품 후보(안테나 등) — RoofPropCount 개수만큼 무작위 위치·무작위 종류. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CityGen|Roof", meta = (AllowedClasses = "/Script/Engine.StaticMesh"))
	TArray<TObjectPtr<UStaticMesh>> RoofProps;

	/** 옥상 소품 배치 개수. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CityGen|Roof", meta = (ClampMin = "0"))
	int32 RoofPropCount = 2;

	/** 가로 칸수(1칸=250). 0 = 사이징 박스 바운드에서 유도. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CityGen|Size", meta = (ClampMin = "0"))
	int32 Width = 0;

	/** 세로 칸수(1칸=250). 0 = 사이징 박스 바운드에서 유도. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CityGen|Size", meta = (ClampMin = "0"))
	int32 Depth = 0;

	/** 층수(1층=300). 0 = 사이징 박스 바운드에서 유도. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CityGen|Size", meta = (ClampMin = "0"))
	int32 Floors = 0;

	/** 상층 셋백(맨 위 2개 층이 한 칸 안쪽으로 들어가는 계단식). 가로·세로가 3칸 초과일 때만 적용. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CityGen|Size")
	bool bSetback = true;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};
