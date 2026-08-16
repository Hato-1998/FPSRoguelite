// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/Actor.h"
#include "Arena/FPSRArenaTypes.h"
#include "FPSRArenaActor.generated.h"

class UFPSRArenaParamsDataAsset;
class UHierarchicalInstancedStaticMeshComponent;
class UStaticMesh;

/**
 * The arena, as an actor you drop in a level (ADR 0010 D1/D2/D5).
 *
 * ## What replicates, and what does not
 *
 * ONLY the seed. ADR 0010 gives the layout no owner: every machine regenerates it locally from the seed, so
 * the arena costs one replicated int no matter how many clusters it has. That is only sound because the
 * generator is deterministic (invariant 10) — if it ever stopped being, the server's flow field and a
 * client's geometry would disagree and enemies would appear to walk through walls on that client only.
 *
 * ## Who reads it
 *
 * UFPSRFlowFieldSubsystem PULLS the surface data at world begin rather than this actor pushing it. Actor
 * BeginPlay runs before world subsystems' OnWorldBeginPlay, so a push would be overwritten by the subsystem's
 * own bake a moment later. Pull also keeps the fail-fast in one place: if the layout is missing, the
 * subsystem builds NO field rather than quietly falling back to a world-trace bake (invariant 5).
 *
 * The whitebox meshes here are representation only and exist to answer one question — "is the circulation
 * fun?" (invariant 12). Art comes after that verdict, not before.
 */
UCLASS()
class FPSROGUELITE_API AFPSRArenaActor : public AActor
{
	GENERATED_BODY()

public:
	AFPSRArenaActor();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Server: pick a new seed, regenerate, hand the surface to the flow field, and replicate the seed so every
	 *  client rebuilds the same arena locally. False if params are missing/invalid — the caller must not
	 *  substitute anything (invariant 5). */
	bool ServerRegenerate(int32 NewSeed);

	/** Regenerate this machine's local layout from ActiveSeed. Called on the server by ServerRegenerate and on
	 *  clients by OnRep_ActiveSeed; also called by the flow-field subsystem at world begin. */
	bool BuildLocalLayout();

	const FFPSRArenaLayout& GetLayout() const { return Layout; }
	bool HasLayout() const { return Layout.IsValid(); }

	int32 GetActiveSeed() const { return ActiveSeed; }

	/** Find the arena in this world, if any. Null is the normal answer for a legacy authored map. */
	static AFPSRArenaActor* FindInWorld(const UWorld* World);

protected:
	UFUNCTION()
	void OnRep_ActiveSeed();

	/** Rebuild the whitebox instances from Layout. Purely cosmetic + player collision; the swarm never reads it. */
	void RebuildRepresentation();

	/** World-space min corner of cell (0,0). The actor sits at the arena CENTRE so placing it reads naturally. */
	FVector ComputeGridOrigin(const FFPSRArenaGenParams& Params) const;

	/** 아레나 파라미터(치수·클러스터 격자·통로 폭). 비어 있으면 아레나를 만들지 않는다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "아레나", meta = (DisplayName = "아레나 파라미터"))
	TObjectPtr<UFPSRArenaParamsDataAsset> ArenaParams;

	/** 레벨 시작 시드. 런타임에는 서버가 ActiveSeed 를 굴려 덮는다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "아레나", meta = (DisplayName = "시작 시드"))
	int32 InitialSeed = 1;

	/** 화이트박스용 박스 메시. **크기·피벗은 메시 바운드에서 자동으로 읽으므로** 정육면체가 아니어도, 원점이 중심이
	 *  아니어도 된다. C++ 에 에셋 경로를 박지 않으므로 비워 두면 지오메트리가 안 생긴다(디버그 격자 뷰는 그래도 동작). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "아레나|화이트박스", meta = (DisplayName = "박스 메시"))
	TObjectPtr<UStaticMesh> WhiteboxCubeMesh;

	/** 클러스터 높이(cm). 시야를 끊는 것이 목적이라 눈높이보다 확실히 높아야 한다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "아레나|화이트박스", meta = (DisplayName = "클러스터 높이(cm)", ClampMin = "60"))
	float ClusterHeight = 400.0f;

	/** 경계벽 높이(cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "아레나|화이트박스", meta = (DisplayName = "경계벽 높이(cm)", ClampMin = "60"))
	float WallHeight = 500.0f;

	/** 바닥판 두께(cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "아레나|화이트박스", meta = (DisplayName = "바닥판 두께(cm)", ClampMin = "1"))
	float FloorThickness = 50.0f;

	/** The one replicated thing. Clients rebuild everything else from it. */
	UPROPERTY(ReplicatedUsing = OnRep_ActiveSeed)
	int32 ActiveSeed = 0;

	UPROPERTY(VisibleAnywhere, Category = "아레나|화이트박스")
	TObjectPtr<USceneComponent> SceneRoot;

	/** Clusters + boundary walls: what actually stops the player. */
	UPROPERTY(VisibleAnywhere, Category = "아레나|화이트박스")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> BlockingMeshes;

	/** The floor slab. Separate component so it can be non-blocking-to-nothing and differently materialled later. */
	UPROPERTY(VisibleAnywhere, Category = "아레나|화이트박스")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> FloorMeshes;

private:
	/** Local, never replicated, never saved — regenerated from ActiveSeed. */
	FFPSRArenaLayout Layout;
};
