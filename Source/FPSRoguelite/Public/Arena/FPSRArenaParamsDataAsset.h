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
	/** 아레나 크기(셀). 100cm 셀 기준 160×160 = 160×160m (ADR 0011 E1). 80×80 화이트박스로 루프·교차 위상이
	 *  확증된 뒤, 랜드마크를 놓을 공간을 위해 올렸다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "아레나|치수", meta = (DisplayName = "아레나 크기(셀)", ClampMin = "16"))
	FIntPoint ArenaSizeCells = FIntPoint(160, 160);

	/** 플로우필드 셀 크기(cm). **160m에서는 사실상 100 고정**이다 — 컴파일 상수(축 ≤256 · 총 ≤40000)에서 역산하면
	 *  80~100cm 뿐이라 "답답하면 50으로 내린다"(0010 D3)는 탈출구가 산술적으로 사라졌다. 남는 레버는 프롭 밀도다. */
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

	/** 열린 셀 100개당 **차단** 미세 프롭 수. 통로를 좁히므로 장식이 아니라 게임플레이 다이얼이다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "아레나|미세 프롭", meta = (DisplayName = "차단 프롭 밀도(/100셀)", ClampMin = "0.0"))
	float BlockingPropsPer100Cells = 1.2f;

	/** 열린 셀 100개당 **통과**(≤45cm) 프롭 수. 통행에 영향이 없어 순수 밀도값이다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "아레나|미세 프롭", meta = (DisplayName = "통과 프롭 밀도(/100셀)", ClampMin = "0.0"))
	float PassablePropsPer100Cells = 6.0f;

	/** 차단 프롭 사이 최소 간격(셀). 적 몸집보다 좁으면 둘이 오목한 주머니를 만들 수 있다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "아레나|미세 프롭", meta = (DisplayName = "차단 프롭 최소 간격(셀)", ClampMin = "1"))
	int32 PropMinSpacingCells = 3;

	/** 통로 여유분을 L1이 얼마까지 먹어도 되는지(비율). 1.0이면 모든 통로가 법적 최소폭까지 깎일 수 있다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "아레나|미세 프롭", meta = (DisplayName = "여유분 소비 상한", ClampMin = "0.0", ClampMax = "1.0"))
	float MaxSlackConsumption = 0.5f;

	/** 콘텐츠가 티어마다 제공하는 메시 변형 수(생성기는 인덱스만 고른다). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "아레나|미세 프롭", meta = (DisplayName = "프롭 변형 수", ClampMin = "1"))
	int32 PropVariantCount = 4;

	/** Copy into the plain contract the generator actually takes. */
	FFPSRArenaGenParams ToGenParams() const;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};
