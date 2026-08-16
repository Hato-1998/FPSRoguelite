// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Arena/FPSRArenaTypes.h"
#include "FPSRArenaParamsDataAsset.generated.h"

/**
 * The arena's authoring surface (ADR 0010 invariant 11: "아레나 골격은 코드가 아니라 데이터다").
 *
 * Nothing here may be hardcoded in the generator. The reason is not tidiness — ADR 0010 plans a DECK of
 * arena shapes later, and the only thing standing between us and rewriting the generator at that point is
 * that its inputs already live in data. Every number a designer would want to feel out (size, how many
 * clusters, how tight the corridors are) has to be reachable from here.
 */
UCLASS(BlueprintType)
class FPSROGUELITE_API UFPSRArenaParamsDataAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** 아레나 크기(셀). 100cm 셀 기준 80×80 = 80×80m (ADR 0010 D2). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "아레나|치수", meta = (DisplayName = "아레나 크기(셀)", ClampMin = "16"))
	FIntPoint ArenaSizeCells = FIntPoint(80, 80);

	/** 플로우필드 셀 크기(cm). D3 = 100. 낮추면 미세 프롭 해상도가 오르지만 적 footprint보다 작아지면 흐름이 몸통 안에서 바뀐다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "아레나|치수", meta = (DisplayName = "셀 크기(cm)", ClampMin = "25"))
	float CellSize = 100.0f;

	/** 등반 가능 단차(cm). 단일 평면이라 실제로 오를 일은 없지만, 플로우필드가 슬롯 간 균일성을 검사하므로 엔진 기본과 같아야 한다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "아레나|치수", meta = (DisplayName = "등반 가능 단차(cm)", ClampMin = "1"))
	float ClimbableStepHeight = 45.0f;

	/** 클러스터 배치 격자 후보(가로×세로). 시드가 하나를 고른다. 2×2 = 중앙 교차점, 2×1/1×2 = 8자 동선. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "아레나|골격", meta = (DisplayName = "클러스터 격자 후보"))
	TArray<FIntPoint> SlotGridOptions = { FIntPoint(2, 2), FIntPoint(2, 1), FIntPoint(1, 2), FIntPoint(3, 2), FIntPoint(2, 3) };

	/** 통로 최소 유효 폭(셀). 클러스터를 슬롯에서 이만큼 안으로 넣으므로 인접 클러스터 간격은 이 값의 2배가 된다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "아레나|골격", meta = (DisplayName = "통로 최소 유효 폭(셀)", ClampMin = "1"))
	int32 MinCorridorWidthCells = 3;

	/** 아레나 경계와 클러스터 격자 사이 여백(셀). 바깥 순환로를 만드는 값이다 — 0이면 벽에 붙어 루프가 사라진다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "아레나|골격", meta = (DisplayName = "경계 여백(셀)", ClampMin = "0"))
	int32 BoundaryMarginCells = 3;

	/** 클러스터가 자기 슬롯을 채우는 비율 하한. 축마다 시드로 흔들린다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "아레나|골격", meta = (DisplayName = "클러스터 채움 비율 하한", ClampMin = "0.05", ClampMax = "1.0"))
	float ClusterFillMin = 0.45f;

	/** 클러스터가 자기 슬롯을 채우는 비율 상한. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "아레나|골격", meta = (DisplayName = "클러스터 채움 비율 상한", ClampMin = "0.05", ClampMax = "1.0"))
	float ClusterFillMax = 0.80f;

	/** Copy into the plain contract the generator actually takes. */
	FFPSRArenaGenParams ToGenParams() const;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};
