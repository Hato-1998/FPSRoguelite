# P4-A 사용자 콘텐츠 가이드 — 라운드 스케줄 / 미션 / 런 흐름

> C++ 베이스는 완료(빌드+스모크+Codex 통과). 아래 콘텐츠(DataAsset/BP)를 에디터에서 만들어 연결하면 PIE에서 런 루프가 동작한다. **에셋 경로는 C++에 하드코딩하지 않으므로 전부 BP/DA로 연결**한다.

## 0. 전제
- 빌드된 에디터로 PIE. 기존 `BP_FPSRGameMode`(부모 `FPSRGameMode`, `/Game/Core/`)가 이미 있어야 함(P1 셋업).
- 권장 콘텐츠 폴더: `Content/Run/`(스케줄·미션 DA), `Content/Run/Missions/`(미션 BP).

## 1. 미션 BP — `BP_Mission_HoldZone`
- **부모 클래스**: `FPSRMission_HoldZone` (C++)
- 디테일에서 조정(에디터 노출): `ZoneRadius`(기본 400, 단위 cm 반경), `RequiredHoldSeconds`(기본 30)
- ※ MissionClass에 C++ 클래스(`FPSRMission_HoldZone`)를 직접 지정해도 동작하지만(기본값 400/30), 수치 튜닝 + 아래 시각 메시를 위해 BP 권장.

### 1.1 시각용 존 데칼 추가 (바닥에 반경만큼 칠하기)
판정은 서버 거리체크라 비주얼이 없어도 동작하지만(개발 중엔 `ENABLE_DRAW_DEBUG` 디버그 실린더로만 보임), **플레이어에게 존을 바닥에 원형으로 표시**하려면 BP에 **데칼 컴포넌트**를 붙인다(불규칙 바닥에도 잘 깔리고 시야를 안 가림).

**A. 간단 파란 원형 데칼 머티리얼 만들기** (텍스처 불필요, 노드만)
1. **생성**: Content Browser > Add > Material → `M_HoldZoneDecal`. 더블클릭해 열기.
2. **머티리얼 설정**(좌측 Details, 노드 선택 해제 상태):
   - **Material Domain = `Deferred Decal`**
   - **Decal Blend Mode = `Translucent`** (Domain을 Deferred Decal로 바꾸면 Blend Mode 항목이 Decal Blend Mode로 바뀜)
3. **원형 마스크 노드**(중심에서의 거리로 원을 만든다). ※ 아래 `Dist`/`Alpha`는 **노드 이름이 아니라 중간값 라벨**(설명용):
   - **방법 A(노드 적음, 권장)**: `TextureCoordinate` + `Constant2Vector(0.5,0.5)` → **`Distance`**(A=TexCoord, B=Const2) → 결과 = `Dist`(중심 0, 가장자리 0.5). `Distance`는 입력 2개짜리(두 점 사이 거리).
   - **방법 B**: `TextureCoordinate` − `Constant2Vector(0.5,0.5)` 를 `Subtract`로 빼고 → **`VectorLength`**(입력 1개, 벡터 길이) → `Dist`. (Distance = VectorLength(A−B)와 동일)
   - 이어서: `Constant` = **0.5** → `Subtract`(A=0.5, **B=Dist**) → 원 안은 +, 밖은 −
   - `Multiply` ×**40** (가장자리 선명도) → `Saturate` → `Alpha`(원 안 1, 밖 0)
   - **이 Saturate 출력 → 머티리얼 `Opacity`**
   - ⚠️ 흔한 실수: 거리(길이) 노드를 빼먹으면 위 `Subtract`/`VectorLength` 출력이 어디에도 안 연결됨. `Subtract(0.5, Dist)`의 **B 입력에 반드시 거리값**을 꽂을 것(기본 1.0이 아니라).
4. **파란 색**:
   - `Constant3Vector` = **(0.05, 0.3, 1.0)** (파랑) → (선택)`Multiply` ×**3**(발광 강조) → **`Emissive Color`**
   - (원하면 같은 색을 `Base Color`에도 연결)
5. **저장**. → §1.1-B에서 BP Decal 컴포넌트의 Decal Material로 지정.

- **링(테두리)만** 원하면 3번 Alpha를 `Saturate((0.5−Dist)×40) × Saturate((Dist−0.42)×40)`로(0.42~0.5 반경 사이만 칠해짐).
- **색 재사용**: `Constant3Vector`를 우클릭 > Convert to Parameter(`ZoneColor`)로 바꾸면 Material Instance에서 미션별 색만 바꿔 재사용.

**B. BP에 데칼 컴포넌트 배치**
- `BP_Mission_HoldZone` 컴포넌트 탭 → **Root**(`USceneComponent`) 하위에 **Decal** 컴포넌트 추가.
- **Decal Material** = 위 A 머티리얼(또는 그 인스턴스).
- **Relative Rotation**: **Pitch = -90** → 데칼이 **바닥으로 투영**(데칼은 로컬 -X 방향으로 투사).
- **Decal Size**(반-크기, half-extent) 디테일:
  - **Y = Z = `ZoneRadius`** → 칠해지는 원 지름 = 2×ZoneRadius = 판정 지름과 일치(기본 400 → Y=Z=400).
  - **X(투영 깊이)** = 256~512 정도(바닥까지 닿게; 너무 크면 벽·천장까지 칠해지니 적당히).
- **위치**: Decal 위치는 Root 기준 (0,0,0) 유지 — 존 중심=액터 원점(스폰포인트 위치, §2.5).

⚠️ **반경 동기화 수동**: `ZoneRadius`를 바꾸면 **Decal Size의 Y/Z도 같은 값**으로 맞춰야 시각=판정 일치(판정은 항상 `ZoneRadius` 사용).

- (선택) 진행도 연동(채워지는 링/색 변화)은 데칼 머티리얼 파라미터를 `GetMissionProgress()`에 바인딩해 표현 가능 — 폴리시는 P4-D. P4-A는 정적 원형 데칼로 충분.

## 2. 미션 DA — `DA_Mission_HoldZone` (`UFPSRMissionDataAsset`)
- **에셋 생성**: Miscellaneous > Data Asset > `FPSRMissionDataAsset`
- 필드:
  - `DisplayName` / `Description` / `ObjectiveText`: 표시용(예 "구역 사수", "30초간 구역을 지켜라") — UI는 P4-D
  - `MissionClass` = `BP_Mission_HoldZone`
  - `TimeLimit`: 0 = 무제한, 또는 예 60(초) — 초과 시 실패 처리
  - `RewardCard`: 무기 모디파이어 보상 카드(weapon-scope). **P4-A는 클리어 시 카운트만 적립**(실제 보상 선택·적용은 P4-B). 테스트로 적립 동작만 보려면 비워둬도 됨(null이면 카드 참조 없이 카운트만 증가)
  - `SpawnPointTag`: 이 미션이 스폰될 수 있는 **미션 스폰포인트 태그**(§2.5). **비우면** 배치된 아무 포인트나 사용(없으면 플레이어 위치 폴백). 미션-지형 적합성을 강제하려면 태그 지정(예 `Mission.Spawn.HoldZone`).

## 2.5 미션 스폰포인트 배치 — `AFPSRMissionSpawnPoint`
미션이 **맵의 디자이너 지정 지점**에 스폰되도록(현재 폴백은 플레이어 위치) 레벨에 스폰포인트를 배치한다.
- **게임플레이 태그 생성**(에디터 Project Settings > GameplayTags 또는 `DefaultGameplayTags.ini`): 예 `Mission.Spawn.HoldZone`, `Mission.Spawn.Platforming` 등 미션 유형별로.
- **레벨에 배치**: Place Actors에서 `FPSR Mission Spawn Point`(또는 클래스 `FPSRMissionSpawnPoint`)를 맵의 원하는 지점에 드래그. 에디터에서 하늘색 화살표로 위치+방향 표시(화살표 방향 = 미션 스폰 회전).
- **디테일 설정**:
  - `MissionTag`: 이 지점이 호스팅할 미션 유형(예 `Mission.Spawn.HoldZone`). 미션 DA의 `SpawnPointTag`와 매칭(같거나 자식 태그)되면 선택 후보.
  - `Weight`: 후보가 여럿일 때 가중 랜덤 비중(0 이하면 제외).
  - `MinPlayerDistance`(cm): >0이면 가장 가까운 플레이어가 이 거리 이상일 때만 후보(플레이어 근처 스폰 방지). 모든 매칭 포인트가 거리 미달이면 **가장 먼 매칭 포인트** 선택(플레이어 위에 스폰 안 함).
  - `bEnabled`: 끄면 삭제 없이 선택 제외.
- **선택 규칙**: 미션 출현 시 디렉터가 `SpawnPointTag` 매칭 + 활성 + 거리 통과 포인트 중 **Weight 가중 랜덤**. 매칭 포인트가 하나도 없으면 첫 플레이어 위치 폴백(미배치 맵도 동작).
- HoldZone은 스폰포인트 위치 기준 `ZoneRadius` 반경에 존 형성 → 포인트는 **개활지**에 두는 것을 권장.

## 3. 런 스케줄 DA — `DA_RunSchedule` (`UFPSRRunScheduleDataAsset`) — ⚠️ 재설계됨(라운드 폐지)
> 이전 `Rounds[]` 구조는 제거됐습니다. 기존 `DA_RunSchedule`이 있으면 아래 새 필드로 다시 설정하세요(라운드제 → 시간 기반).
- **에셋 생성/수정**: Data Asset > `FPSRRunScheduleDataAsset`
- **필드**:
  - **`MissionEvents`** 배열 — 각: `TriggerTime`(초, 미션 출현 시각), `Mission`(DA). ⚠️ **테스트 압축값**(프로덕션 300/600/900s = 5/10/15분):

    | TriggerTime(초) | Mission |
    |---|---|
    | 60  | DA_Mission_HoldZone |
    | 120 | DA_Mission_HoldZone |
    | 180 | DA_Mission_HoldZone |

  - **`BossTime`** = **300**(초) — 이 시각에 보스 출현(이후 미션·타이머 없음). 프로덕션 ~1200(20분).
  - **`BaseAliveCount`**(기본 40) / **`AliveCountPerMinute`**(기본 30) / **`MaxAliveCount`**(기본 250) — 시간경과 스폰 강도 스케일링.
- 미션은 **해당 TriggerTime 도달 시 1회** 출현, **선택적**. **클리어하면 즉시 전역 프리즈**되어 보상 카드 선택 후 재개.
- 스케줄 미할당 시 C++ 폴백(미션 없음, 보스 300s, 기본 스폰 스케일)로 동작.

## 3.5 적 BP — `BP_Enemy` (XP·메시·스탯 설정) ⭐
적 XP(및 메시/스탯)는 **적 BP에서 설정**한다. 현재 스폰 적 클래스는 GameMode에서 지정하며, 미지정 시 C++ `AFPSREnemyBase`(엔진 큐브 플레이스홀더, XPReward 5)로 폴백한다.
- **BP 생성**: Add > Blueprint Class > 부모 **`FPSREnemyBase`** → `BP_Enemy`.
- **디테일 설정**:
  - `XPReward`(FPSR|Enemy): 이 적 처치 시 드롭하는 XP. **여기서 원하는 값으로 튜닝**(기본 5).
  - (후속) 메시/머티리얼·이동속도·공격력 등도 이 BP에서. 현재 메시는 C++ 큐브 플레이스홀더(§Game.MD 8).
- **GameMode에 연결**(§4): `BP_FPSRGameMode` > `FPSR|Enemy` > **`EnemyClass` = `BP_Enemy`**.
- ※ 적 종류를 여러 개 두거나 시간 스케일링(HP/공격력 커브)은 P4-C/후속. P4-A는 단일 적 클래스 지정까지.

## 4. GameMode BP 연결
- `BP_FPSRGameMode` 디테일 > `FPSR|Run` > **`RunSchedule` = `DA_RunSchedule`** 할당.
- `FPSR|Enemy` > **`EnemyClass` = `BP_Enemy`**(§3.5) 할당 → 적 XP/메시 적용. (미지정 시 큐브 플레이스홀더·XP 5)
- (CardPool은 기존 P3 연결 유지)

## 5. PIE 검증 시나리오 (재설계 — 레벨업 전역 프리즈)
1. **런 시작 = 오프닝 시드 2장**(전원 동결 상태) → 2장 선택하면 동결 해제 → 전투 시작
2. **Combat 스폰 자동**(시간경과 스케일링) — `FPSR.SpawnEnemies` 불필요
3. 적 처치로 **XP 누적 → 레벨업 시 즉시 전역 프리즈**(적·플레이어 정지) → 카드 선택 → 전원 완료 시 재개. (`FPSR.AddXP`로 빠른 트리거)
4. `MissionEvents` 시각 도달 → **미션(존) 출현** → 클리어하면 **즉시 프리즈** → 모디파이어 보상 카드 선택 → 재개. (보상 실적용은 P4-B, weapon-scope는 선택만)
5. **BossTime 도달 → 보스 진입**(미션·타이머 중단, 스폰 정리) — 보스 실물은 P6 스텁. 보스 중에도 레벨업하면 프리즈.

### 런 상태 HUD (데이터 복제됨, WBP 바인딩은 P4-D)
`AFPSRGameState` getter를 HUD 위젯에 바인딩(스타일 위젯은 P4-D):
- `GetRunClockSeconds()`(생존 경과) / `GetRunPhase()`(Combat/Boss) / **`IsRunPaused()`**(프리즈 중) / `GetPartyLevel()` / `GetSharedXP()` / `GetRequiredXPForNextLevel()`
- `AFPSRPlayerState::GetCardPicksPending()` / `GetMissionRewardPicksPending()` (본인 보류 픽)
- 빠른 확인: `FPSR.RunDebug 1` 오버레이(t/boss/alive/mission) + 캐릭터 디버그(Lv/XP/Picks/[Combat|Boss FROZEN]).

### 테스트 편의 콘솔(빠른 반복, shipping 제외)
- `FPSR.AddXP [n]` — 공유 XP 직접 가산 → **레벨업 전역 프리즈 트리거**(핵심 테스트)
- `FPSR.RunTimeScale 10` — 런클럭 10배속(미션/보스 타이밍 빠르게)
- `FPSR.SkipToBoss` — 즉시 보스 진입
- `FPSR.MissionTrigger` / `FPSR.MissionClear` — 다음 미션 즉시 발동 / 활성 미션 강제 클리어(보상 프리즈)
- `FPSR.Pause [0|1]` — 전역 프리즈 수동 토글
- `FPSR.KillAllEnemies` — 활성 적 전부 정리
- `FPSR.RunDebug 1` — 화면에 t/boss/alive/mission/배속 오버레이

## 6. 레벨링 / XP (프로덕션 공식)
- 레벨 요구 XP = `XPBaseRequired + (PartyLevel-1) × XPPerLevel` (기본 100 + (L-1)×50). 둘 다 GameState `EditDefaultsOnly`.
- 적 처치 시 `AFPSREnemyBase::XPReward`(기본 5) 만큼 XP 드롭 → 자석 회수 → 공유 풀 누적. **적 XP는 BP_Enemy에서 튜닝**(§3.5).
- 풀이 요구치 도달 시 **즉시 전역 프리즈**(적·플레이어 정지) → 전원 카드 선택 → 재개.

## 7. ⚠️ 임시 테스트값 (프로덕션 전환 시 원복)
- **미션 시각 60/120/180s · 보스 300s(5분)** — 프로덕션 300/600/900s·보스 1200s(20분). 스케줄 DA에서 조정. 전환 시 별도 보고(메모리 `p4a-temp-test-values`).
