// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "FPSREnemySpawnPoint.generated.h"

class UArrowComponent;
class USceneComponent;

/** Designer-placed enemy spawn anchor. The enemy spawn subsystem selects UNIFORMLY at random among eligible
 *  points (enabled + min distance + its spawn zone active). Server-only selection; this actor is not replicated
 *  (the spawned enemy actor it anchors is the replicated object). Lightweight: no tick, no collision. A point's
 *  ZoneTag is normally auto-applied by the enclosing AFPSRSpawnRoom at BeginPlay. The enemy spawns at SpawnAnchor's
 *  world location (a child component, default at the actor origin) — move it to spawn INSIDE a structured-spawner's
 *  mesh cavity rather than at the placement gizmo. */
UCLASS()
class FPSROGUELITE_API AFPSREnemySpawnPoint : public AActor
{
	GENERATED_BODY()

public:
	AFPSREnemySpawnPoint();

	/** Spawn-zone (room) tag. A point with NO tag is always eligible; a tagged point is eligible only while its
	 *  zone is active in the spawn subsystem (a room opens -> its zone activates -> its points go live, and stay
	 *  live as more rooms open — accumulating spawn locations). Usually auto-applied by the enclosing
	 *  AFPSRSpawnRoom; a manually set tag is respected (override). (Room spawn system, Enemy.md §2-6.) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Spawn")
	FGameplayTag ZoneTag;

	/** Which map this spawn point belongs to (multimap Tier 0). Unset = the Default single-map (single-map content
	 *  unchanged). The spawn subsystem caches points per MapId and only selects a map's points for that map's allocation;
	 *  an enemy spawned here inherits this MapId. Distinct from ZoneTag (intra-map room gating). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Spawn")
	FGameplayTag MapId;

	/** If > 0, this point is only eligible when the nearest player is at least this far away (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Spawn", meta = (ClampMin = "0.0"))
	float MinPlayerDistance = 0.0f;

	/** Disable to exclude this point from selection without deleting it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Spawn")
	bool bEnabled = true;

	/**
	 * 탈출 경로를 따라가는 동안 적이 **정적 지오메트리를 통과**하는가 (구조형 스포너용, 기본 켬).
	 *
	 * 스포너 메시에 실제 구멍을 뚫어 두는 대신 이것을 쓴다. 메시는 **완전히 막힌 채로** 두므로 플레이어가
	 * 빠지거나 통과하지 않고, 적만 스폰 연출 동안 벽을 지나 나온다. 뚫린 콜리전은 구멍이 적 캡슐보다 좁으면
	 * 조용히 끼는데, 이 방식엔 그 실패 모드가 없다.
	 *
	 * 통과 중에도 적은 **여전히 맞고**(Visibility 채널 그대로) **플레이어를 막는다** — WorldStatic 응답
	 * 하나만 잠시 Ignore 된다. 바닥도 그대로 밟는다: 중력은 캡슐 응답이 아니라 별도 월드 쿼리로 바닥을
	 * 찾는다(`AFPSREnemyBase::ApplyGravity`).
	 *
	 * 끄는 경우: 탈출 경로가 "구조물에서 빼내는" 용도가 아니라 "모퉁이를 돌려보내는" 연출일 때. 그때는
	 * 벽을 통과하면 안 된다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Spawn", meta = (DisplayName = "탈출 중 지오메트리 통과"))
	bool bPhaseThroughWorldWhileExiting = true;

	/**
	 * MovePoint — 스폰 직후 따라갈 탈출 경로 지점들, **액터 로컬 좌표**. 배열 요소마다 뷰포트에 드래그
	 * 핸들이 뜨므로(MakeEditWidget) 디테일 패널에서 `+` 로 늘리고 위치는 뷰포트에서 잡는다.
	 *
	 * `ExitPathRoot` 웨이포인트와 나뉘는 이유 = **맵 배치별 오버라이드**. 웨이포인트는 컴포넌트라 BP 안에서만
	 * 저작되고 그 BP를 놓은 모든 복사본이 같은 경로를 쓴다. 이건 프로퍼티라서 레벨 인스턴스마다 표준
	 * 블루프린트 인스턴스 오버라이드로 저장된다 — **BP 기본값은 비워 두고**, 맵에 놓은 개체에서만 더한다.
	 * (기본값에 1개라도 넣으면 그 BP 의 모든 배치가 스폰마다 탈출 상태 — 아래 통과·분리 무시 — 로 시작한다.)
	 *
	 * ⚠️ 탈출 경로가 있는 동안 적은 `bPhaseThroughWorldWhileExiting`(기본 **켜짐**) 에 따라 WorldStatic 을
	 * 무시하고 웨이포인트를 향해 **직진**하며 플로우필드·분리도 쓰지 않는다. 그러므로 여기에 모퉁이 너머
	 * 지점을 찍으면 적이 벽을 뚫고 나온다 — 열린 바닥에서 "돌려보내는" 용도로 쓸 때는 위 토글을 **꺼라**.
	 * 이 연동은 그 토글의 주석("끄는 경우")과 같은 규칙이다(G2 2026-09-04).
	 *
	 * 로컬 좌표라 스포너를 옮기거나 돌리면 경로도 같이 따라온다. 컴포넌트 웨이포인트 **뒤에 이어 붙는다**
	 * (대체가 아니라 추가 — 기존 구조형 스포너 콘텐츠 무회귀).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Spawn",
		meta = (DisplayName = "MovePoint (탈출 경로)", MakeEditWidget = true))
	TArray<FVector> ExitPathPoints;

	FGameplayTag GetZoneTag() const { return ZoneTag; }
	const FGameplayTag& GetMapId() const { return MapId; }
	float GetMinPlayerDistance() const { return MinPlayerDistance; }
	bool IsEnabled() const { return bEnabled; }
	bool ShouldPhaseThroughWorldWhileExiting() const { return bPhaseThroughWorldWhileExiting; }

	/** Server/setup: assign this point's spawn zone (used by AFPSRSpawnRoom to auto-tag its interior points). */
	void SetZoneTag(const FGameplayTag& InZoneTag) { ZoneTag = InZoneTag; }

	/**
	 * Append this point's authored exit-path waypoints (world space) to Out, in attach order — the route OUT of a
	 * structured spawner, ending at the hand-off point to flow-field player-chase. No waypoints = no path (the
	 * enemy chases immediately). (C1)
	 *
	 * Three authoring places. (1) and (2) are alternatives — (2) is the fallback used only when (1) is empty —
	 * and (3) is always APPENDED after whichever of them answered:
	 *   1. Scene components attached to the **UChildActorComponent that spawned this point**, i.e. authored in the
	 *      SPAWNER Blueprint. Use this when one mesh has several holes that exit in DIFFERENT directions — each
	 *      ChildActorComponent carries its own route, dragged in that BP's viewport.
	 *   2. Scene components under this actor's own **ExitPathRoot**. Used by a directly-placed spawn point, and as
	 *      the shared default a structured spawner inherits when a hole has no route of its own (identical routes
	 *      differing only by rotation are covered by rotating the ChildActorComponent).
	 *   3. The **ExitPathPoints** array (actor-local). Both of the above are COMPONENTS, so they are authored in a
	 *      Blueprint and every placement of that Blueprint shares the one route; this is a property, so it is
	 *      overridden per level instance — for a DIRECTLY-PLACED point (or a point subclass) this is how ONE
	 *      placement's route is extended in a map. It does NOT give per-placement routes to a point spawned through
	 *      a ChildActorComponent (path 1): there the property lives in the ChildActorTemplate, which is
	 *      VisibleDefaultsOnly and cannot be overridden per placement.
	 */
	void GetExitPathWorldPoints(TArray<FVector>& Out) const;

	/** World location where the enemy actually spawns = SpawnAnchor's world location (falls back to the actor origin
	 *  if SpawnAnchor is somehow null). Lets a structured-spawner BP place the spawn point INSIDE its mesh cavity
	 *  while the actor origin stays the placement/orientation gizmo. */
	FVector GetSpawnLocation() const;

private:
	/** The enemy spawn position (a child Scene component, default at the actor origin). Move it in a structured-spawner
	 *  BP so enemies appear inside the pipe/box cavity instead of at the placement gizmo. Distinct from ExitPathRoot's
	 *  waypoints, which are the route OUT after spawning. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy Spawn", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> SpawnAnchor;

	/** Container for the authored exit-path waypoints — its direct child scene components are the waypoints (attach
	 *  order = order). Separate from the actor root so a structured-spawner BP can also add a pipe/box mesh under the
	 *  root without those components being mistaken for waypoints. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy Spawn", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> ExitPathRoot;

#if WITH_EDITORONLY_DATA
	/** Editor-only direction arrow so designers can see placement + facing (enemy spawn rotation). */
	UPROPERTY()
	TObjectPtr<UArrowComponent> EditorArrow;
#endif
};
