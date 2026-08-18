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
 * UFPSRFlowFieldSubsystem PULLS the surface data at world begin rather than this actor pushing it. The engine
 * order is the OPPOSITE of what an earlier version of this comment claimed (verified against UE 5.7's
 * World.cpp/GameModeBase.cpp/GameStateBase.cpp/WorldSettings.cpp): world subsystems' OnWorldBeginPlay runs FIRST
 * (UWorld::BeginPlay -> SubsystemCollection.ForEachSubsystem), and only THEN does GameMode->StartPlay ->
 * GameState->HandleBeginPlay -> WorldSettings->NotifyBeginPlay dispatch every actor's OWN BeginPlay. A push from
 * this actor's BeginPlay would therefore always be too LATE for the subsystem's initial pull, not "overwritten a
 * moment later" — the subsystem would have already read whatever this actor had by OnWorldBeginPlay time, or
 * nothing. That is exactly why ActiveSeed is finalized in PostInitializeComponents (see below), which the engine
 * runs even earlier still (ULevel::RouteActorInitialize, during UWorld::InitializeActorsForPlay — strictly before
 * UWorld::BeginPlay() is even called): it is the only actor callback guaranteed to complete before this PULL runs.
 * Pull also keeps the fail-fast in one place: if the layout is missing, the subsystem builds NO field rather than
 * quietly falling back to a world-trace bake (invariant 5).
 *
 * The whitebox meshes here are representation only and exist to answer one question — "is the circulation
 * fun?" (invariant 12). Art comes after that verdict, not before.
 *
 * ## Multiple arenas, one level
 *
 * ADR 0010 D6 as amended 2026-08-17 (user decision): a future stage transition needs the NEXT arena ready before
 * the current one ends, and the chosen shape for that is several AFPSRArenaActor instances placed in the SAME
 * level at XY locations far enough apart that their cell grids never overlap — not one arena loaded from a
 * streaming sublevel. A loaded sublevel cannot be relocated at runtime, and turning its visibility on is not one
 * atomic moment — it resolves over several frames per client as that client's streaming catches up, with no
 * shared deadline; a transition built on either would stop being one atomic swap, so different clients would ack
 * a different-length window — exactly what invariant 8 (a FIXED damage-dealing window) forbids. Parking arenas
 * beside each other in one already-loaded level instead means "swap" is only a transform-free visibility/collision
 * toggle (SetArenaActive) plus a player teleport: no load, no per-client streaming lag, so the window is the same
 * fixed length everywhere. This slice does NOT build the transition state machine itself — only the foundation
 * multiple arenas need: StageOrder/bStartsActive, SetArenaActive, ContainsWorldLocation (spatial ownership of the
 * markers inside an arena's grid — see BuildLayoutForSeed), and FindActiveInWorld/FindAllInWorld.
 */
UCLASS()
class FPSROGUELITE_API AFPSRArenaActor : public AActor
{
	GENERATED_BODY()

public:
	AFPSRArenaActor();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Server: pick a new seed, regenerate, hand the surface to the flow field, and replicate the seed so every
	 *  client rebuilds the same arena locally. False if params are missing/invalid — the caller must not
	 *  substitute anything (invariant 5). */
	bool ServerRegenerate(int32 NewSeed);

	/** Regenerate this machine's local layout from ActiveSeed. Called on the server by ServerRegenerate and on
	 *  clients by OnRep_ActiveSeed; also called by the flow-field subsystem at world begin. */
	bool BuildLocalLayout();

	/** Gather the authored actors and generate a layout for an arbitrary seed WITHOUT touching this actor's state.
	 *  Shared by BuildLocalLayout and by the editor authoring tools, so what the designer previews in the editor
	 *  is produced by the same code the runtime will run — not a second implementation that agrees with it only
	 *  until one of the two is edited. */
	bool BuildLayoutForSeed(int32 Seed, FFPSRArenaLayout& OutLayout) const;

	/** Resolved generator params + world-space grid origin, or false if no params asset is assigned. */
	bool GetGenParams(FFPSRArenaGenParams& OutParams, FVector& OutOrigin) const;

	const FFPSRArenaLayout& GetLayout() const { return Layout; }
	bool HasLayout() const { return Layout.IsValid(); }

	int32 GetActiveSeed() const { return ActiveSeed; }

	/** The seed authored on the actor — what the editor tools propose from (ActiveSeed is only meaningful in play). */
	int32 GetInitialSeed() const { return InitialSeed; }

	/** Height the editor's starting-layout tool gives a proposed blocker. Lives here rather than in the tool so the
	 *  authored number stays in one place — the designer tunes it on the arena and every proposal follows. */
	float GetClusterHeight() const { return ClusterHeight; }

	/** Toggle this arena's whitebox geometry + its own blocker/landmark/destructible markers (spatial membership —
	 *  see BuildLayoutForSeed) on or off: HISM visibility + collision, marker actors hidden + collision-disabled.
	 *  Idempotent; NEVER touches a transform (everything here is Static mobility, and UE forbids moving a Static
	 *  primitive at runtime — see the class comment on why "swap" has to work this way). Called from BeginPlay with
	 *  bStartsActive; the future stage-transition state machine (not built in this slice) calls it again to swap. */
	void SetArenaActive(bool bInActive);

	bool IsArenaActive() const { return bIsArenaActive; }

	/** Whether this arena is AUTHORED to be the live one at level start (as opposed to IsArenaActive(), which is the
	 *  runtime state). Exposed so the editor validator can enforce "exactly one" — the authoring mistake of marking
	 *  two (or none) is invisible at runtime, where FindActiveInWorld just picks one deterministically. */
	bool StartsActive() const { return bStartsActive; }

	/** True if World is inside this arena's cell grid on the XY plane. Z is deliberately NOT checked: the arena is
	 *  a single Z-plane (ADR 0010 D2), and reserve arenas are parked BESIDE the live one rather than stacked below
	 *  it (see the class comment) — so two arenas can share a Z but never overlap in XY, and XY alone is what tells
	 *  them apart. This is the ONE membership test every marker actor and every editor tool uses, so "which arena
	 *  owns this actor" cannot drift between call sites. */
	bool ContainsWorldLocation(const FVector& World) const;

	/** Collect this arena's authored entry points (its own APlayerStart actors), sorted by NAME so every machine
	 *  derives the same order with nothing replicated. No authored starts -> fills Out with a fallback (arena
	 *  centre + 4 cardinal 200 cm offsets) and returns false, so the caller can log the authoring gap instead of
	 *  silently stacking every player on one point. Either way, every returned transform's XY is then snapped onto
	 *  the nearest OPEN cell (Z/rotation untouched) if this machine has a layout — see the .cpp for why. */
	bool GetPlayerEntryTransforms(TArray<FTransform>& Out) const;

	/** The arena to treat as "the live one". Prefers whichever arena actually IsArenaActive(); if nobody has gone
	 *  through BeginPlay yet (editor / not in PIE), falls back to bStartsActive, then to the lowest StageOrder
	 *  (ties broken by name) so the answer is still deterministic. Null only if the world has no arena at all.
	 *  Replaces the old single-arena FindInWorld now that ADR 0010 D6 parks spare arenas in the same level. */
	static AFPSRArenaActor* FindActiveInWorld(const UWorld* World);

	/** Every arena in the world, ordered by StageOrder ascending (ties broken by name) — the order a stage
	 *  transition advances through, and the order FindActiveInWorld's fallback tiers scan in. */
	static void FindAllInWorld(const UWorld* World, TArray<AFPSRArenaActor*>& Out);

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

	/** 전환 순서(작은 값부터 진행). 같은 값이 둘이면 이름 순으로 갈린다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "아레나", meta = (DisplayName = "스테이지 순서"))
	int32 StageOrder = 0;

	/** 레벨 시작 시 이 아레나가 활성 상태인가. **레벨에 하나만 true 여야 한다** — 나머지는 예비 아레나로, 같은
	 *  레벨의 떨어진 자리에서 렌더·콜리전이 꺼진 채 대기한다(2026-08-17 사용자 결정 — 클래스 주석 참고). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "아레나", meta = (DisplayName = "시작 시 활성"))
	bool bStartsActive = true;

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

	/** 차단 미세 프롭 높이(cm). **60 미만으로 못 내린다** — 45~60 은 플로우필드가 못 보는 구간이라
	 *  적이 낀다(ADR 0010 D4). 클램프가 그 구간을 저작에서 아예 막는다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "아레나|화이트박스", meta = (DisplayName = "차단 프롭 높이(cm)", ClampMin = "60"))
	float BlockingPropHeight = 100.0f;

	/** 통과 미세 프롭 높이(cm). **45 초과 못 올린다** — 같은 이유로 반대쪽 가드다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "아레나|화이트박스", meta = (DisplayName = "통과 프롭 높이(cm)", ClampMin = "1", ClampMax = "45"))
	float PassablePropHeight = 30.0f;

	/** 배선 메시(L2, ADR 0010 D8). **비워 두면 배선 지오메트리가 생성되지 않는다** — 아직 아트가 없는 상태가
	 *  정상이며, 경고를 띄우지 않는다(WhiteboxCubeMesh 와 달리 이건 장식일 뿐 위상을 대신 보여주지 않아도 된다).
	 *  위상은 `FPSR.Arena.DebugTraces` 로 확인할 수 있다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "아레나|화이트박스", meta = (DisplayName = "배선 메시"))
	TObjectPtr<UStaticMesh> TraceMesh;

	/** 정션(비아) 메시. 배선이 갈라지는 지점마다 하나씩. 비워 두면 정션 지오메트리가 생성되지 않는다(경고 없음). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "아레나|화이트박스", meta = (DisplayName = "정션(비아) 메시"))
	TObjectPtr<UStaticMesh> TraceJunctionMesh;

	/** 배선 기본 폭(cm). 실제 렌더 폭 = TraceWidthCm + WidthClass * TraceWidthPerClassCm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "아레나|화이트박스", meta = (DisplayName = "배선 폭(cm)", ClampMin = "1"))
	float TraceWidthCm = 40.0f;

	/** 통로폭(WidthClass) 1단당 배선이 굵어지는 정도(cm). **굵은 배선 = 넓은 통로** — ADR 0010 D8 이 말하는
	 *  "밝은 라인이 곧 지나갈 수 있는 곳"을 폭으로도 드러낸다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "아레나|화이트박스", meta = (DisplayName = "통로폭당 배선 가중(cm)", ClampMin = "0"))
	float TraceWidthPerClassCm = 8.0f;

	/** 배선 두께(cm). 0010 D4 「평면 장식」이라 콜리전이 없다 — 두께는 통행에 아무 의미가 없고, 바닥판과의
	 *  z-fighting 을 피하기 위한 값일 뿐이다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "아레나|화이트박스", meta = (DisplayName = "배선 두께(cm)", ClampMin = "0.1"))
	float TraceHeightCm = 2.0f;

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

	/** L2 floor wiring trace instances (ADR 0010 D8). NoCollision — 0010 D4 「평면 장식」, traces affect nothing
	 *  about traversal. */
	UPROPERTY(VisibleAnywhere, Category = "아레나|화이트박스")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> TraceMeshes;

	/** L2 junction (via) instances, one per FFPSRArenaLayout::TraceJunctions entry. Also NoCollision. */
	UPROPERTY(VisibleAnywhere, Category = "아레나|화이트박스")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> JunctionMeshes;

private:
	/** Local, never replicated, never saved — regenerated from ActiveSeed. */
	FFPSRArenaLayout Layout;

	/** Backs IsArenaActive(). Defaults to false (NOT bStartsActive) on purpose: it means "nobody has toggled this
	 *  arena yet", which is exactly the editor / not-in-PIE state BeginPlay never reaches — that is what lets
	 *  FindActiveInWorld tell "genuinely active" apart from "would become active a moment later" and fall through
	 *  to its bStartsActive tier correctly. Set by SetArenaActive, first called from BeginPlay. */
	bool bIsArenaActive = false;
};
