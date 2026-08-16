// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/Actor.h"
#include "FPSRArenaMarkers.generated.h"

class UBoxComponent;
class UStaticMeshComponent;

/**
 * A blocking volume the DESIGNER places (ADR 0011 E2). The arena rasterises its box into blocked cells.
 *
 * Why a dedicated actor rather than "any static mesh in the level": the arena must know exactly which geometry
 * is meant to block the swarm. Inferring it from whatever happens to be in the level is the world-trace bake
 * that 0010 rejected — it cannot tell a wall from a decorative gantry, and it drags the cost and the origin
 * hazards back with it. Declaring intent costs the designer one actor and buys an exact mask.
 *
 * The box is the truth for BOTH the mask and the collision, so the two cannot drift apart. The mesh is only a
 * skin; scale it and you change how it LOOKS, not what the swarm believes.
 */
UCLASS()
class FPSROGUELITE_API AFPSRArenaBlocker : public AActor
{
	GENERATED_BODY()

public:
	AFPSRArenaBlocker();

	/** World-space half extent in XY (the box's own scale is already folded in). */
	FVector2D GetHalfExtentXY() const;

	/** Yaw only — a blocking volume that pitched or rolled would stop being a prism in the XY plane, and the
	 *  arena is single-plane, so there is nothing for the other axes to mean. */
	float GetYawDegrees() const;

	/** Size this blocker from cell-space dimensions. Used by the editor's starting-layout tool so a proposal
	 *  lands as real, hand-editable actors rather than a preview the designer cannot touch. */
	void SetFootprint(const FVector2D& HalfExtentXY, float HeightCm);

protected:
	/** The volume. This is what the arena reads and what the player collides with — one shape, no drift. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "아레나|블로커")
	TObjectPtr<UBoxComponent> Box;

	/** 화이트박스 표시용(선택). 비워 두면 박스 와이어프레임만 보인다 — 판정에는 영향이 없다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "아레나|블로커")
	TObjectPtr<UStaticMeshComponent> DisplayMesh;
};

/**
 * A fixed navigation beacon (ADR 0010 D6, authored per 0011 E2).
 *
 * D6 requires the position and colour to be identical in every stage, which is exactly what procedural
 * placement cannot promise — and the reason the skeleton became authored at all. In a first-person arena with a
 * uniform floor texture this is the only thing that answers "which way am I facing", so burying one under props
 * is a real failure rather than a cosmetic one; hence the reserve radius.
 */
UCLASS()
class FPSROGUELITE_API AFPSRArenaLandmark : public AActor
{
	GENERATED_BODY()

public:
	AFPSRArenaLandmark();

	int32 GetReserveRadiusCells() const { return ReserveRadiusCells; }
	bool IsBlocking() const { return bBlocking; }

	/** Identity + colour are what the player actually navigates by, so they live on the actor rather than being
	 *  derived from anything procedural. */
	FLinearColor GetBeaconColor() const { return BeaconColor; }

protected:
	/** 주변 몇 셀까지 절차 프롭을 금지할지. 0 = 자기 셀만. 파묻히면 랜드마크가 아니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "아레나|랜드마크", meta = (DisplayName = "프롭 금지 반경(셀)", ClampMin = "0"))
	int32 ReserveRadiusCells = 2;

	/** 이 랜드마크가 통행을 막는가(높은 기둥=막음 / 바닥 마크=안 막음). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "아레나|랜드마크", meta = (DisplayName = "통행 차단"))
	bool bBlocking = true;

	/** 멀리서 식별하는 색. 셋은 서로 확실히 달라야 한다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "아레나|랜드마크", meta = (DisplayName = "식별 색"))
	FLinearColor BeaconColor = FLinearColor(0.0f, 0.8f, 1.0f);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "아레나|랜드마크")
	TObjectPtr<UStaticMeshComponent> DisplayMesh;
};
