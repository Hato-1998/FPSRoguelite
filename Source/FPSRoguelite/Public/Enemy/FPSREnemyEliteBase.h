// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Enemy/FPSREnemyBase.h"
#include "FPSREnemyEliteBase.generated.h"

/** Elite tier skeleton (ADR 0013: Docs/Architecture/0013-enemy-tier-axis-and-elite-gas.md §「결정」) — the second
 *  class of the enemy tier axis (일반/엘리트) alongside the plain AFPSREnemyBase archetypes. Deliberately EMPTY: it
 *  exists so content/tooling can start targeting a distinct elite TYPE now (BP reparenting, roster entries, the
 *  director sensor's subclass probe), without pulling in anything ADR 0013 defers to its own follow-up actions.
 *
 *  ASC attachment, the elite attribute set, and elite concurrency-cap accounting are ADR 0013 후속 행 3(엘리트 ASC
 *  실부착 + 어트리뷰트 셋 + 엘리트 캡 회계) — NOT this class's job yet. Do not add a virtual IsElite()-style seam, a
 *  dedicated net-cull radius, or dedicated stats here either: FPSRoguelite.Enemy.NetCull assumes a UNIFORM radius
 *  across the whole swarm (ADR 0013 반론 ②), so giving elites their own radius here would break that test's
 *  premise before 후속 행 3 has actually decided how (or whether) elites should diverge it. This class stays a
 *  no-op subclass — same movement/attack/pooling/net-cull behavior as AFPSREnemyBase — until that follow-up lands. */
UCLASS()
class FPSROGUELITE_API AFPSREnemyEliteBase : public AFPSREnemyBase
{
	GENERATED_BODY()

public:
	AFPSREnemyEliteBase();
};
