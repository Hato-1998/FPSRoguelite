// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Arena/FPSRArenaTypes.h"
#include "FPSRArenaPropSetDataAsset.generated.h"

/**
 * One authored prop group — "커패시터 뱅크", "저항 어레이", "DIP 칩 + 핀" and so on.
 *
 * This exists because the first version of L1 scattered props cell by cell and the result read as noise. Even
 * coverage is not the same thing as looking placed: a real board has parts in groups with empty substrate
 * between them, and no per-cell random process produces that. So a person authors what a plausible group looks
 * like, once, and the generator only decides where groups go and which way they face.
 *
 * A set is authored in CELL space, relative to its anchor. That keeps it resolution-independent of world units
 * and, more importantly, means the safety check the generator runs on a candidate position is the same
 * cell-run check everything else uses.
 *
 * ⚠️ Tier is per ENTRY, not per set. That is what keeps ADR 0010 D4's height band enforceable: a group can mix
 * a blocking capacitor with passable traces around it, and each entry lands on one side of the 45/60 band.
 */
UCLASS(BlueprintType)
class FPSROGUELITE_API UFPSRArenaPropSetDataAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** 이 세트를 이루는 프롭들. 셀 오프셋은 앵커 기준 상대 좌표다(0,0 = 앵커 셀). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "프롭 세트", meta = (DisplayName = "구성 프롭"))
	TArray<FFPSRArenaPropSetEntry> Entries;

	/** 다른 세트 대비 뽑힐 상대 가중치. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "프롭 세트", meta = (DisplayName = "가중치", ClampMin = "1"))
	int32 Weight = 1;

	/** 시드가 세트 전체를 90도 단위로 돌려도 되는지. 방향성이 있는 부품(커넥터 등)이면 끄면 된다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "프롭 세트", meta = (DisplayName = "90도 회전 허용"))
	bool bAllowRotation = true;

	/** Flatten into the plain struct the generator consumes. */
	FFPSRArenaPropSet ToPropSet() const;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};
