// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Enemy/FPSRFlowFieldComputer.h" // FFPSRFlowFieldSurfaceData
#include "FPSRArenaBakeDataAsset.generated.h"

/**
 * One arena's baked obstacle mask (ADR 0012).
 *
 * ADR 0012 invariant 1: the mask is baked IN THE EDITOR from the level's collision and stored here; the
 * runtime only loads it. Nothing in the shipping path traces the world or generates geometry. What made the
 * editor bake acceptable where ADR 0010 rejected a world trace is that all three of 0010's objections
 * (thousands of downtraces per stage transition, the silent CellSize coarsen, the spare arena poisoning the
 * PlayerStart anchor) are objections to doing it AT RUNTIME.
 *
 * Why a plain UDataAsset and not UPrimaryDataAsset like UFPSRArenaParamsDataAsset: this asset is a hard
 * dependency of the arena actor that references it and is never looked up by id or enumerated. A primary
 * asset type can be pulled in wholesale by an Asset Manager scan rule, and each of these carries ~300 KB of
 * bulk arrays — several arenas' worth of mask loaded at startup for no reason. The params asset IS enumerated
 * (a designer picks one), so the difference is deliberate rather than an inconsistency.
 *
 * COORDINATE SYSTEM: Surface is ARENA-LOCAL (invariant 8) — the arena actor's own transform is NOT folded in.
 * Never hand Surface straight to BuildFromSurfaceData; go through BuildWorldSurface, which is the only place
 * the conversion lives. Baking world coordinates would pin the arena to one position forever, which kills
 * both reusing one arena .umap at several LevelTransforms and simply nudging a parked arena later.
 */
UCLASS(BlueprintType)
class FPSROGUELITE_API UFPSRArenaBakeDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	/** 구워진 표면 그래프. **아레나 로컬 좌표**다 — 런타임은 `BuildWorldSurface` 로만 읽는다. */
	UPROPERTY(VisibleAnywhere, Category = "아레나|베이크", meta = (DisplayName = "표면 데이터(로컬)"))
	FFPSRFlowFieldSurfaceData Surface;

	/** 베이크 대상 액터 집합의 해시. 현재 레벨에서 다시 계산한 값과 다르면 **스테일**이다 — ADR 0012
	 *  5겹 검사가 전부 이 한 값을 본다. 비어 있으면 "아직 안 구움"이지 "일치"가 아니다. */
	UPROPERTY(VisibleAnywhere, Category = "아레나|베이크", meta = (DisplayName = "소스 해시"))
	FString SourceHash;

	/** 어느 레벨에서 구웠는지. 검증기가 재해시할 대상을 찾는 데 쓴다. */
	UPROPERTY(VisibleAnywhere, Category = "아레나|베이크", meta = (DisplayName = "소스 레벨"))
	FSoftObjectPath SourceLevel;

	/** 그 레벨 안의 어느 아레나 액터인지. 한 레벨에 아레나는 하나여야 하지만(불변식 4), 그 불변식이
	 *  깨졌을 때 조용히 엉뚱한 마스크를 쓰는 대신 검증기가 짚을 수 있어야 한다. */
	UPROPERTY(VisibleAnywhere, Category = "아레나|베이크", meta = (DisplayName = "소스 아레나"))
	FName SourceArenaName;

	/** 베이크에 들어간 액터 수. 사람이 "이게 그럴듯한 값인가"를 1초 만에 판단하는 용도 — 0 이나 1 이면
	 *  콜리전 설정을 잘못 건드린 것이다. */
	UPROPERTY(VisibleAnywhere, Category = "아레나|베이크", meta = (DisplayName = "소스 액터 수"))
	int32 SourceActorCount = 0;

	/** 마지막 베이크 시각(UTC). 스테일 판정은 해시가 하고, 이건 사람이 읽는 값이다. */
	UPROPERTY(VisibleAnywhere, Category = "아레나|베이크", meta = (DisplayName = "베이크 시각(UTC)"))
	FDateTime BakedAtUtc = FDateTime(0);

	/** 열린 셀 수 / 전체 셀 수 — 검증기가 이미 세는 값을 요약으로 남긴 것. 벌크 배열은 디테일 패널에
	 *  띄울 수 없으므로(51,200 엔트리) 여기가 유일한 눈으로 보는 창이다. */
	UPROPERTY(VisibleAnywhere, Category = "아레나|베이크", meta = (DisplayName = "열린 셀"))
	int32 OpenCells = 0;

	/** 굽힌 데이터가 실제로 들어 있는가. 배열 길이까지 본다 — 필드에 UPROPERTY 를 빠뜨려 저장이
	 *  통째로 날아간 경우(ADR 0012 「감수하기로 한 것」) 치수만 보면 멀쩡해 보이기 때문이다. */
	bool IsBaked() const;

	/**
	 * 로컬 좌표 Surface 에 아레나 트랜스폼을 적용해 **월드 좌표** 사본을 만든다. 런타임이 마스크를 읽는
	 * 유일한 경로다.
	 *
	 * 평행이동만 허용한다. 셀 격자는 축정렬 AABB 이고(`AFPSRArenaActor::ContainsWorldLocation` 이 XY
	 * min/max 로만 판정한다) 회전을 먹이면 격자 인덱싱 전체가 조용히 어긋난다 — 회전이 섞인 트랜스폼은
	 * 보정하지 않고 **거부한다**. 스케일도 같은 이유로 거부한다(CellSize 와 이중 정의가 된다).
	 *
	 * @return 성공 여부. 실패 시 OutWorld 는 건드리지 않는다.
	 */
	bool BuildWorldSurface(const FTransform& ArenaTransform, FFPSRFlowFieldSurfaceData& OutWorld) const;

	/** 월드 좌표 베이크 결과를 아레나 로컬로 접어 이 에셋에 넣는다(에디터 베이커 전용).
	 *  `BuildWorldSurface` 의 정확한 역연산 — 둘이 같은 파일에 있어야 한쪽만 고쳐지지 않는다. */
	static bool LocalizeSurface(const FFPSRFlowFieldSurfaceData& World, const FTransform& ArenaTransform,
		FFPSRFlowFieldSurfaceData& OutLocal);

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif

private:
	/** 두 변환의 공통 부분: 트랜스폼을 검사해 평행이동 성분만 뽑는다. */
	static bool ExtractPlanarOffset(const FTransform& ArenaTransform, FVector& OutOffset, FString& OutError);

	/** GridOrigin + CellFloorZ 에 Z 오프셋을 적용한 사본을 만든다. MAX_flt(=표면 없음) 센티넬은
	 *  절대 이동시키지 않는다 — 더하는 순간 유한값이 되어 "없던 바닥"이 생긴다. */
	static void OffsetSurface(const FFPSRFlowFieldSurfaceData& In, const FVector& Offset,
		FFPSRFlowFieldSurfaceData& Out);
};
