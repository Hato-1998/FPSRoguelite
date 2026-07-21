// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "FPSRCityGenKit.generated.h"

class UStaticMesh;

/** U22a-A 도시 건물 생성 툴의 **모듈 메시 수집 목록(Kit)** — 손으로 채우지 않는다.
 *  `Tools > FPSR CityGen > 1. Collect Modular Meshes`(Content/Python/fpsr_citygen.py)가
 *  메시 폴더를 훑어 **실제 바운드를 재고** 250폭·300층높이·피벗(밑변 왼쪽) 규약을 통과한 것만
 *  카테고리별로 여기에 채운다. 규격 밖 메시를 고르면 건물이 어긋나므로, 사용자는 이 목록에서
 *  `2. Fill Config from Kit`으로 가져다 쓰고 불필요한 것만 지운다(→ UFPSRCityGenConfig).
 *  재수집해도 Config의 선택은 보존된다(그래서 Kit과 Config를 분리했다 — 사용자 결정 2026-07-21). */
UCLASS(BlueprintType)
class FPSROGUELITE_API UFPSRCityGenKit : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** 창문벽/상점벽 — 층 외벽 칸을 채우는 파사드 후보. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Kit", meta = (AllowedClasses = "/Script/Engine.StaticMesh"))
	TArray<TObjectPtr<UStaticMesh>> Facades;

	/** 모서리 기둥/코너 트림 후보. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Kit", meta = (AllowedClasses = "/Script/Engine.StaticMesh"))
	TArray<TObjectPtr<UStaticMesh>> Corners;

	/** 문이 뚫린 '벽' 후보(문짝 단품이 아니라 벽 한 칸을 대체하는 것). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Kit", meta = (AllowedClasses = "/Script/Engine.StaticMesh"))
	TArray<TObjectPtr<UStaticMesh>> Doors;

	/** 옥상 바닥판(지붕 슬래브) 후보. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Kit", meta = (AllowedClasses = "/Script/Engine.StaticMesh"))
	TArray<TObjectPtr<UStaticMesh>> RoofFloors;

	/** 층 경계 띠 장식(처마/몰딩) 후보. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Kit", meta = (AllowedClasses = "/Script/Engine.StaticMesh"))
	TArray<TObjectPtr<UStaticMesh>> CorniceTrims;

	/** 옥상 소품(안테나·위성접시 등) 후보 — 장식이라 모듈 규격 검사 제외. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Kit", meta = (AllowedClasses = "/Script/Engine.StaticMesh"))
	TArray<TObjectPtr<UStaticMesh>> RoofProps;
};
