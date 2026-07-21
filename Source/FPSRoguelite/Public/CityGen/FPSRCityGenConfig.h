// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "FPSRCityGenConfig.generated.h"

class UStaticMesh;

/** U22a-A 도시 건물 생성 툴(Content/Python/fpsr_citygen.py)이 읽는 "에셋 선택" 설정.
 *  프리셋을 대신해 기획자가 종류별 모듈 메시를 직접 고른다(파사드 여러 개·코너·문·바닥/지붕·코니스·옥상 프롭).
 *  에디터 툴 전용(런타임 로직 없음). Python 브릿지 load_config_from_dataasset()가 각 UPROPERTY를 이름으로 읽으므로,
 *  프로퍼티명을 바꾸면 그 매핑도 함께 고칠 것. 조립 규약: 벽 250폭 · 층 300 · 피벗 밑변 왼쪽 +X. */
UCLASS(BlueprintType)
class FPSROGUELITE_API UFPSRCityGenConfig : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** 파사드(창문벽) 후보 — 층 외벽에 무작위로 깔린다. 250폭만 유효(500폭은 툴이 자동 제외). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CityGen|Facade", meta = (AllowedClasses = "/Script/Engine.StaticMesh"))
	TArray<TObjectPtr<UStaticMesh>> Facades;

	/** 코너 트림(모서리 기둥) — 각 층 네 모서리. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CityGen|Pieces", meta = (AllowedClasses = "/Script/Engine.StaticMesh"))
	TObjectPtr<UStaticMesh> Corner;

	/** 문 — 1층 정면 중앙 칸에 파사드 대신 놓인다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CityGen|Pieces", meta = (AllowedClasses = "/Script/Engine.StaticMesh"))
	TObjectPtr<UStaticMesh> Door;

	/** 옥상 슬래브(지붕 바닥) — 최상층 위 125 간격 격자. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CityGen|Pieces", meta = (AllowedClasses = "/Script/Engine.StaticMesh"))
	TObjectPtr<UStaticMesh> RoofFloor;

	/** 코니스 트림 — 각 층 경계(위/아래) 가장자리. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CityGen|Pieces", meta = (AllowedClasses = "/Script/Engine.StaticMesh"))
	TObjectPtr<UStaticMesh> CorniceTrim;

	/** 옥상 소품 후보(안테나·위성 등) — RoofPropCount 개수만큼 무작위 배치. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CityGen|Roof", meta = (AllowedClasses = "/Script/Engine.StaticMesh"))
	TArray<TObjectPtr<UStaticMesh>> RoofProps;

	/** 옥상 소품 배치 개수. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CityGen|Roof", meta = (ClampMin = "0"))
	int32 RoofPropCount = 2;

	/** 가로 칸수(0 = 사이징 박스 바운드에서 유도). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CityGen|Size", meta = (ClampMin = "0"))
	int32 Width = 0;

	/** 세로 칸수(0 = 사이징 박스 바운드에서 유도). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CityGen|Size", meta = (ClampMin = "0"))
	int32 Depth = 0;

	/** 층수(0 = 사이징 박스 바운드에서 유도). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CityGen|Size", meta = (ClampMin = "0"))
	int32 Floors = 0;

	/** 상층 셋백(계단식 후퇴) 사용 여부. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CityGen|Size")
	bool bSetback = true;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};
