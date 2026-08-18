// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/Actor.h"
#include "Arena/FPSRArenaTypes.h"
#include "FPSRArenaActor.generated.h"

class UFPSRArenaParamsDataAsset;

/**
 * The arena, as an actor you drop in its own sublevel (ADR 0012).
 *
 * ## It owns logic, not geometry
 *
 * The play space is LEVEL AUTHORING — the walls, floor and props are actors a designer placed, and both the
 * server and every client get them by loading the same .umap. This actor owns only what the level cannot say
 * for itself: the baked obstacle mask, the arena's bounds, and whether it is the live arena right now. ADR 0012
 * axis 2 settled that split after the old arrangement (this actor building its own floor/walls/props as HISM
 * instances at BeginPlay) put a second play space on top of the designer's one.
 *
 * That is also why there is no runtime generator any more: FFPSRArenaGenerator moved to FPSRogueliteEditor and
 * the mask comes from UFPSRArenaBakeDataAsset, baked in the editor from this level's real collision. What the
 * editor shows IS what ships (invariant 3), and mask-vs-collision cannot drift because they are the same thing
 * (invariant 2).
 *
 * ## What replicates, and what does not
 *
 * Only ActiveSeed, and it no longer selects geometry — client and server load the same file, so ADR 0010's
 * warning about a client regenerating different geometry from the server's mask cannot happen at all. The seed
 * is kept for what comes next: picking which traversal-neutral / breakable props a run uses (ADR 0012 「이번
 * 런의 랜덤 선택 결과」), a server decision that gets replicated rather than re-derived.
 *
 * ## Who reads the mask
 *
 * UFPSRFlowFieldSubsystem PULLs it at world begin rather than this actor pushing it. World subsystems'
 * OnWorldBeginPlay runs FIRST (UWorld::BeginPlay -> SubsystemCollection.ForEachSubsystem), and only THEN does
 * GameMode->StartPlay -> ... dispatch every actor's own BeginPlay — so a push from BeginPlay would always be
 * too LATE, not "overwritten a moment later". ActiveSeed is therefore finalized in PostInitializeComponents,
 * which the engine runs earlier still (ULevel::RouteActorInitialize, during UWorld::InitializeActorsForPlay).
 * Verified against UE 5.7; InitializeActorsForPlay walks EVERY level in the world, so an always-loaded arena
 * sublevel is initialised in time for that pull exactly like a persistent-level actor would be.
 *
 * ## One arena, one sublevel (invariant 4)
 *
 * ADR 0012 axis 5: the NEXT arena has to be ready before the current one ends, and the expensive half of that
 * is AddToWorld (5 ms per frame, spread over many frames) which cannot start until bShouldBeVisible is true.
 * So it is paid a whole stage early — UFPSRArenaStreamSubsystem makes arena N+1's sublevel visible as soon as
 * stage N begins, and this actor immediately hides it (SetArenaActive(false)). The swap is then O(1): show,
 * teleport, publish the mask. Invariant 8 (a FIXED damage-dealing window) survives because nothing in the
 * window waits on anyone's disk.
 *
 * An earlier revision parked spare arenas side by side in ONE level, which is why several comments here talk
 * about telling arenas apart by XY. ContainsWorldLocation still does exactly that and is still the spatial
 * membership test every marker, spawn gate and bake hash shares — what changed is that the geometry toggle in
 * SetArenaActive now works on this actor's own ULevel, because with one arena per sublevel that IS the arena.
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

	/** Server: adopt this arena's baked mask, publish it to the flow field if this arena is the live one, and
	 *  replicate the new seed. Called by UFPSRStageDirectorSubsystem on every swap. False means there is no
	 *  usable bake — the caller must ABORT the swap rather than substitute anything (invariant 1). */
	bool ServerRegenerate(int32 NewSeed);

	/** Resolved generator params + world-space grid origin, or false if no params asset is assigned. */
	bool GetGenParams(FFPSRArenaGenParams& OutParams, FVector& OutOrigin) const;

	/** 이 아레나가 베이크된 마스크를 갖고 있는가 (ADR 0012). 참조만이 아니라 내용까지 본다 — 참조는 걸려
	 *  있는데 배열이 빈 에셋이 정확히 "적이 벽을 뚫는" 증상으로 나타나므로, 그 상태를 없음으로 취급한다. */
	bool HasBakedSurface() const;

	/** 베이크 에셋 참조 자체(내용 무관). 에디터 베이커가 여기에 결과를 써 넣고, 검증기가 "참조는 있는데
	 *  안 구워졌다"와 "참조조차 없다"를 구분하는 데 쓴다 — HasBakedSurface() 는 그 둘을 똑같이 false 로
	 *  뭉개므로 지적 문구를 고를 수 없다. */
	class UFPSRArenaBakeDataAsset* GetBakeData() const { return BakeData; }

	/** 베이크된 마스크를 이 액터의 트랜스폼만큼 옮겨 **월드 좌표**로 돌려준다(ADR 0012 불변식 8).
	 *  플로우필드가 마스크를 얻는 유일한 경로다. 실패 시 OutWorld 는 건드리지 않는다. */
	bool GetBakedWorldSurface(FFPSRFlowFieldSurfaceData& OutWorld) const;

	/**
	 * 베이크된 마스크를 이 액터의 `Layout` 으로 채택한다 (ADR 0012). 레이아웃을 얻는 **유일한** 경로다.
	 *
	 * 생성기 출력 대신 베이크를 담지만 **`Layout` 이라는 같은 자리에 넣는 것이 핵심**이다. 진입점 스냅
	 * (`GetPlayerEntryTransforms`)·디버그 그리드·검증기가 전부 `Layout` 을 읽으므로, 베이크를 옆에 따로
	 * 들면 그 셋이 조용히 "레이아웃 없음" 경로를 타면서 스냅도 검사도 사라진다.
	 *
	 * 채택 후 검증기를 돌려 로그에 남긴다 — 런타임 검사는 경보이지 게이트가 아니다(0011 E4).
	 */
	bool AdoptBakedLayout();

	/** 베이크된 마스크를 검증기 입력 형태로 조립한다. 클러스터·프롭·배선은 비운다(베이크엔 그런 개념이
	 *  없고, `bFromBake` 가 검증기에 그 사실을 알린다). 랜드마크는 여전히 레벨 액터에서 모은다(0011 E2). */
	bool BuildValidationLayoutFromBake(FFPSRArenaLayout& Out) const;

	const FFPSRArenaLayout& GetLayout() const { return Layout; }
	bool HasLayout() const { return Layout.IsValid(); }

	int32 GetActiveSeed() const { return ActiveSeed; }

	/** The seed authored on the actor — what the editor tools propose from (ActiveSeed is only meaningful in play). */
	int32 GetInitialSeed() const { return InitialSeed; }

	/** Authored transition order. Public because UFPSRArenaStreamSubsystem has to read it off arenas whose
	 *  sublevel is LOADED BUT NOT YET VISIBLE — those are not in the world, so FindAllInWorld cannot see them and
	 *  the subsystem reads ULevel::Actors directly to know which level to park next. */
	int32 GetStageOrder() const { return StageOrder; }

	/** Height the editor's starting-layout tool gives a proposed blocker. Lives here rather than in the tool so the
	 *  authored number stays in one place — the designer tunes it on the arena and every proposal follows. */
	float GetClusterHeight() const { return ClusterHeight; }

	/** Show or hide this arena — every actor in its own ULevel gets SetActorHiddenInGame + SetActorEnableCollision
	 *  (invariant 4 makes "this level" and "this arena" the same set, and it is the same set the bake hash reads).
	 *  Idempotent; NEVER touches a transform (everything here is Static mobility, and UE forbids moving a Static
	 *  primitive at runtime). Called from BeginPlay with bStartsActive, by UFPSRArenaStreamSubsystem to hide a
	 *  freshly parked arena, and by UFPSRStageDirectorSubsystem on both sides of a swap. */
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

	/** World-space min corner of cell (0,0). The actor sits at the arena CENTRE so placing it reads naturally. */
	FVector ComputeGridOrigin(const FFPSRArenaGenParams& Params) const;

	/** 아레나 파라미터(치수·클러스터 격자·통로 폭). 비어 있으면 아레나를 만들지 않는다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "아레나", meta = (DisplayName = "아레나 파라미터"))
	TObjectPtr<UFPSRArenaParamsDataAsset> ArenaParams;

	/** 이 아레나의 **베이크된 장애물 마스크**(ADR 0012). 에디터 베이커가 레벨 콜리전에서 구워 넣는다.
	 *  설정돼 있으면 플로우필드가 이것만 읽고 절차 생성 경로는 쓰지 않는다 — 불변식 1·3.
	 *  비어 있으면 구 절차 생성 경로로 폴백하며 경고를 남긴다(마이그레이션 중에만 유효한 상태다). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "아레나", meta = (DisplayName = "베이크 데이터"))
	TObjectPtr<class UFPSRArenaBakeDataAsset> BakeData;

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

	/** 클러스터 높이(cm). 시야를 끊는 것이 목적이라 눈높이보다 확실히 높아야 한다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "아레나|화이트박스", meta = (DisplayName = "클러스터 높이(cm)", ClampMin = "60"))
	float ClusterHeight = 400.0f;

	/** The one replicated thing. Clients rebuild everything else from it. */
	UPROPERTY(ReplicatedUsing = OnRep_ActiveSeed)
	int32 ActiveSeed = 0;

	UPROPERTY(VisibleAnywhere, Category = "아레나|화이트박스")
	TObjectPtr<USceneComponent> SceneRoot;

private:
	/** What an actor's hidden/collision flags were BEFORE this arena first touched them.
	 *
	 *  SetArenaActive(true) restores these rather than forcing "visible + colliding": both AActor::bHidden and
	 *  AActor::bActorEnableCollision are authorable per actor, so a prop a designer deliberately hid (or switched
	 *  collision off on) would otherwise be revealed the first time its arena went live — silently, and only in
	 *  play. Captured lazily on first touch, which is BeginPlay's SetArenaActive for every authored actor, i.e.
	 *  before anything else can have changed them.
	 *
	 *  Bounded by the arena level's authored actor count; nothing spawns into an arena sublevel at runtime (the
	 *  swarm spawns into the persistent level), so this does not grow. */
	struct FFPSRAuthoredActorState
	{
		bool bHidden = false;
		bool bCollisionEnabled = true;
	};
	TMap<TWeakObjectPtr<AActor>, FFPSRAuthoredActorState> AuthoredActorStates;

	/** Local, never replicated, never saved — regenerated from ActiveSeed. */
	FFPSRArenaLayout Layout;

	/** Backs IsArenaActive(). Defaults to false (NOT bStartsActive) on purpose: it means "nobody has toggled this
	 *  arena yet", which is exactly the editor / not-in-PIE state BeginPlay never reaches — that is what lets
	 *  FindActiveInWorld tell "genuinely active" apart from "would become active a moment later" and fall through
	 *  to its bStartsActive tier correctly. Set by SetArenaActive, first called from BeginPlay. */
	bool bIsArenaActive = false;
};
