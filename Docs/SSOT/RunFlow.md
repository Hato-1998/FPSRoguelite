# RunFlow — 런 흐름 / 미션 / 메타 (FPSRoguelite SSOT 분할)

> `Game.md`(SSOT 허브)의 분할 문서. **섹션 번호(§x)는 원본 그대로 보존** — 소스 주석·교차참조 호환.
> 작업 시작 전 허브 `Game.md` + `PROGRESS.md`를 먼저 읽고, 런 루프·XP/레벨업·미션/스케줄·보스·메타 관련 작업 시 본 파일을 연다.
> 담는 섹션: §2-1 핵심 루프 / §2-2 XP·레벨업·프리즈 / §2-7 보스 / §2-8 런 스케줄·미션 / §2-11 메타 프로그레션 / §2-12 난이도 압박.

---

### 2-1. 핵심 게임 루프
```
로비 (메타 강화) → 런(생존 + 레벨업 프리즈 + 시간대별 미션) → 마지막 보스 처치/클리어 → 재화 획득 → 로비
```
- **고정 맵 / 고정 구조** (절차적 생성 PCG 사용 안 함)
  - **맵 다양성 (기획 추가 2026-06-10 · 다중맵 심리스로 갱신 2026-07-03)**: 단일 맵은 로그라이트 재플레이 피로 위험 → **복수의 authored 고정 맵**(각각 Flow-Field 사전계산). PCG 미사용 유지(성능·사전계산 근거 — 동적 생성 시 Flow-Field 이점 소멸).
    - **레벨 구조 = 다중맵 심리스 (피벗 2026-07-03, `Concept.md §1-C-9`)**: 런 시작 시 단일 맵 선택이 아니라, **맵 4방향 끝의 거대한 문을 부수면 다음 맵이 심리스로 스트림-인**(레벨 스트리밍/World Partition, **하드 트래블 아님**). 문 = 테마가 서로 다른 맵들을 잇는 **통일 디자인 요소**(픽션 = 서로 다른 심에 다이빙). 예시 3맵: 사이버펑크 시티 → 숲 → 우주(환경 에셋 `Roadmap.md §8`).
    - **미션/스폰 = 플레이어 점유 맵 기반**: 파티가 여러 맵에 분산되면 **점유된 각 맵에서 독립적으로 미션·스폰이 출현**(상세 §2-8). 맵 간 적 예산 배분은 아래 OPEN에 종속.
    - ⚠️ **#3 다중맵 아키텍처 = OPEN (P0급, 별도 설계 — 이 피벗의 미해결 서브결정)**: (a) **플레이어 분산 허용 vs back-to-back USP 긴장**(분산 페널티 vs 뭉침 유도) (b) **적 예산 = 점유맵 공유 vs per-map**(하드캡 `MaxActiveEnemies`=500 재정의 — per-map이면 예산 붕괴 위험) (c) **RepGraph relevancy + 레벨 스트리밍/World Partition 앞당김**(`Performance.md §5` 다음 레버). 단일 `FPSREnemySpawnSubsystem` · U7 플로우필드(단일)를 다중맵으로 확장하는 설계가 별도로 필요(결정 전까지 단일 맵 기준 유지).
    - **코어 검증은 단일 맵(L_Sandbox)** 유지, 다중맵 = 콘텐츠/P6+ 단계(Synty 파일럿 검증 후 착수 — `Roadmap.md §8`).
- **런 구조 = 생존형 (확정 2026-06-04, P4-A 재설계)**: 라운드제 **폐지**. 런은 끊김 없이 진행되며, **레벨업 시에만 게임이 전역 프리즈**되어 카드를 고른다(§2-2). 시간이 지나면 **미션이 시간대별로 독립 출현**(§2-8)하고, **마지막에 보스 출현 → 이후 시간 무제한·미션 없음, 보스만 처치 = 런 클리어**.
  - **프로덕션 기본**: 보스까지 약 **20분**, 미션 시각·보스 시각은 데이터 드리븐(`UFPSRRunScheduleDataAsset`, 에디터 설정값). 데모/테스트용 압축 스케줄(예: 미션 60/120/180s·보스 300s)을 데이터 교체만으로 운용.
- 시간 경과 → 몬스터 수·종류·HP·공격력 증가 (**시간 스케일링상 이동속도 불변**; 단 스폰 시 개체별 ±10% 이속 편차는 허용 — §2-6)
- **1~4인 협동** (리슨서버 P2P)

### 2-2. XP / 레벨업 / 카드 소비 흐름 (재설계 2026-06-04 — 레벨업 전역 프리즈)
- XP는 몬스터 사망 시 **드롭 아이템(범용 Pickup)** → 근접 시 자석 흡수 줍기
- **파티 공유 경험치 풀** — 누가 줍든 하나의 통에 누적, 모두 같은 하단 XP바 UI 공유
- **레벨업 시 전역 프리즈 (확정 2026-06-04, "예전 방식" 복귀 — 기존 "교전 중 프리즈 폐지"를 되돌림)**: 공유 풀이 레벨업 임계 도달 시 **그 즉시 게임이 전역 프리즈**된다 — **적·플레이어 모두 정지**(적은 제자리 동결, 스폰 정지, 플레이어 이동·발사 차단). 연결된 **전원이 본인 카드를 모두 고르면 재개**된다.
  - 프리즈는 전역 `bRunPaused`(서버 권위, `AFPSRGameState`, Replicated)로 표현. **전역 `TimeDilation=0` 미사용** → 스폰·적 이동/공격·플레이어 입력을 **상태로 게이트**(타이머·AbilityTask·RPC·이펙트 부작용 회피). UI·RPC는 프리즈 중 정상 tick.
  - **설계 근거 / 향후 대안 (확정 2026-06-10)**: 프리즈 채택의 제1원리 = ① 빌드 선택이 전투 결과를 좌우하는 로그라이트에서 *선택의 무게*를 보장(실시간 선택은 스웜 압박에 묻힘) ② 서버권위 카드추첨·복제 동기화가 정지 상태에서 가장 단순·안전 ③ 4인 빌드 공유 순간 형성. **레퍼런스(Spell Brigade=실시간)에서 의도적 이탈**. 트레이드오프 = 잦은 레벨업 시 스톱-스타트 페이싱 → **완화책: 후반 레벨업 임계 XP를 가파르게 ↑(프리즈 빈도 억제), 보스전 프리즈 정책은 플레이테스트(§7-5) 후 재검토**. **향후 대안(후속 평가, 현 단계 미구현)**: 경미한 강화는 프리즈 없이 화면 2지선다(**Q/E** 비동기 선택)로 전환하고, 무게 큰 선택(무기/Fragment/다중레벨)만 프리즈 유지하는 **하이브리드**. 현 단계는 전역 프리즈로 진행.
- **레벨업 스택 = 플레이어별 보류 픽**: `SharedXP`/`PartyLevel`은 파티 공유지만, **파티가 1레벨 오를 때마다 연결된 모든 플레이어에게 카드 픽 1개 부여**(`AFPSRPlayerState::CardPicksPending`, Replicated). 다중 레벨업 = 픽 누적 → 프리즈 동안 본인 픽 수만큼 연속 선택(플레이어별 독립, 한 명의 선택이 남의 픽을 소모하지 않음). 늦게 합류한 플레이어는 합류 후 레벨업부터 픽 부여.
- **재개 = 전원 보류 픽 0**(서버 `RefreshPauseState`): 모든 연결 플레이어의 보류 픽(레벨업+미션보상)이 0이 되면 `bRunPaused=false` → 즉시 재개. (별도 준비 입력 없음. AFK 시 전원이 길게 멈추는 점은 감수 — 상태표시는 P7 UX)
- **오프닝 카드 시드**: 런 시작 시 **2장을 즉시 선택**(초반 빌드 시드). 이것도 동일 프리즈 모델(시작 시 프리즈 → 선택 → 재개).
- **미션 보상 프리즈**: 시간대별 미션(§2-8)을 **클리어하면 즉시 프리즈**되어 **무기 모디파이어 보상 카드**(§2-4-1)를 전원이 선택 → 재개. (레벨업 프리즈와 동일 메커니즘, 다른 카드 풀)
- **보스전 중에도 프리즈 동작**: 보스 등장 후 시간제한·미션은 없지만, XP 획득으로 레벨업하면 동일하게 전역 프리즈 후 카드 선택(`bRunPaused`는 페이즈와 독립).

### 2-7. 보스
- **Base 구조**: `ABossBase` + `UBossDefinitionDataAsset` + StateTree (소수라 GAS/StateTree OK)
- **출현 = `BossTime` 도달 시**(프로덕션 ~20분, 스케줄 DA, §2-1·§2-8). 보스전부터 **시간 무제한·미션 없음**, 처치 = 클리어. 2페이즈. 스케줄에 보스 시각·정의 추가로 확장. (단 보스 중에도 레벨업 프리즈는 동작 §2-2)
- **구현 상태(U3/U4 스캐폴드, 2026-06-21 머지)**: `AFPSRBossBase : ACharacter`(StateTree/GAS 미장착·정지 box, 재부모화 불요 구조) + `UFPSRBossDefinitionDataAsset`(체력 주도) + 전용 `AFPSRBossSpawnPoint`. **`UFPSREnemyHealthComponent` 재사용**으로 전 무기경로(히트스캔/투사체/차지/근접/폭발)가 데미지·크릿·FF·약점(U3a) 적용(신규 데미지코드 0), 처치→`FPSRGameMode::NotifyBossDefeated`→`EndRun(Victory)`. 콘텐츠=BP_Boss(약점2)+DA(체력3000)+L_Sandbox 스폰포인트+체력바. **실보스(이동·StateTree·GAS 스킬·2페이즈)는 장기 백로그** — 위 Base 구조(StateTree/GAS)는 그 목표 비전.

### 2-8. 런 스케줄 / 미션 (재설계 2026-06-04, P4-A — 시간 기반)
- **런 스케줄 = `UFPSRRunScheduleDataAsset`**(서버 권위, **전부 에디터 설정값**): **시간 기반 미션 이벤트** `TArray<FFPSRMissionEvent>{ float TriggerTime, UFPSRMissionDataAsset* Mission }` + `float BossTime` + 스폰 강도 스케일링(시간경과 `UCurveFloat` 또는 base+rate). 라운드 개념 없음.
- **런 디렉터 = `UFPSRRunDirectorSubsystem`**(서버 권위): 런클럭 누적(프리즈·보스 중 정지) → 미션 이벤트 시각 도달 시 미션 스폰, `BossTime` 도달 시 보스 진입. 스폰 강도를 시간에 따라 구동.
- **미션 = 시간대별 독립 출현**(확정 2026-06-04):
  - 스케줄의 각 이벤트 시각에 **1개** 출현, **선택적**(미클리어 무방). 이전 미션 종료 후 다음(동시 다중 없음).
  - **클리어 시 즉시 전역 프리즈**(§2-2) → 무기 모디파이어 **보상 카드를 전원 선택** → 재개.
  - **확장형 프레임워크**: `AFPSRMissionActor`(서버권위 리플리케이트 Actor 베이스) 서브클래스 + `UFPSRMissionDataAsset`(메타+로직클래스+보상카드) + **디자이너 배치 `AFPSRMissionSpawnPoint`**(태그매칭+가중랜덤). 디렉터가 스폰·생명주기 관리.
  - **미션 카탈로그**(기획, 프레임워크로 수용): 좁은구역 N초 버티기 / 이동 구역 점령 / 도망치는 고체력 몹 처치 / 점프맵 오브 획득 / 피격 없이 구체 N초 소지 / 시야 극제한 버티기 / 전원 제자리 정지 등. 각 종류 = `AFPSRMission_*` 서브클래스(필요 인프라는 서브클래스+콘텐츠로 격리, 코어 불변).
  - **다중맵 점유 기반 출현 (피벗 2026-07-03, §2-1)**: 파티가 여러 맵에 분산되면 **점유된 맵마다 미션·스폰이 독립 출현**(빈 맵 = 미출현), 시간대별 1개 규칙은 **맵별**로 적용. 맵 간 적 예산 배분(공유 vs per-map)은 **#3 다중맵 아키텍처 OPEN**(§2-1)에 종속 — 확정 전까지 단일 맵 스케줄이 코어 검증 기준.
- 난이도 곡선은 `UCurveFloat`로 분리(후속)
- **구현 상태(P4-A)**: 디렉터(시간 미션+보스타임) + 미션 프레임워크 + 레퍼런스 미션 1종(`AFPSRMission_HoldZone`) + 스폰포인트 + 레벨업/미션보상 전역 프리즈 + 오프닝시드. 미션 보상의 **무기 모디파이어 실적용(weapon-scope `ApplyCard`)은 P4-B**(P4-A는 프리즈+선택 흐름까지).
- **구현 상태(P4-B-3, main 머지 2026-06-11)**: 미션 종류 **6종** + 공용 PointSet + 시간 윈도우 스케줄러. 빌드+스모크+Codex 머지게이트(P2 교정)+PIE 통과.
  - **6종**: `AFPSRMission_StandStill`(전원 정지 N초) / `AFPSRMission_MovingZone`(PointSet 점 순차 점령) / `AFPSRMission_CollectOrbs`(PointSet 각 점 오브 스폰·수집) / `AFPSRMission_CarryNoHit`(오브 소지 무피격 N초, 캐리어 Health 폴링) / `AFPSRMission_DefeatFleeing`(독립 고체력 도망 타깃 `AFPSRMissionFleeTarget` 처치) / `AFPSRMission_LimitedVision`(시야 극제한 N초 버티기). 베이스 `AFPSRMissionActor::Tick`이 `bRunPaused`(프리즈) 중 진행/시간제한 게이트(전 미션 일괄). 공유 인프라 `AFPSRMissionOrb`(서버 오버랩, `EndPlay` 정리). 디버그 `FPSR.MissionTrigger [windowIndex] [poolIndex]`.
  - **공용 `AFPSRMissionPointSet`**(비복제 서버권위, 스폰포인트 패턴): 자식 Scene/Billboard 컴포넌트=월드 점 목록(attach 순서). 미션이 소비 방식 결정(MovingZone=순차 순회 / CollectOrbs=각 점 스폰). 디렉터가 미션 **CDO 가상함수**(`UsesPointSet`/`AssignPointSet`)로 일반 선택·주입(미션별 cast 없음), `SelectMissionPointSet`(태그매칭+가중랜덤, 개수 무관, 0개면 미션 폴백). 점 좌표=콘텐츠.
  - **LimitedVision 시야 제한**: 서버권위 복제 `AFPSRGameState::bVisionRestricted`(`bRunPaused` 미러) → 각 로컬 클라가 `OnRunStateChanged` 구독해 자기 `FirstPersonCamera` PostProcess 적용(`VisionRestrictionMaterial` 블렌더블 / 미할당 시 내장 비네트 폴백, save·restore). cosmetic·게임플레이 중립. 미션 종료/`EndPlay` 어느 경로든 복구. 바인딩=로컬게이트 없이·적용만 `IsLocallyControlled`(+`NotifyControllerChanged` 늦possession 재적용).
- **스케줄 = 시간 윈도우 + 미션 풀 랜덤(2026-06-11)**: `UFPSRRunScheduleDataAsset.MissionWindows`(`FFPSRMissionWindow{MinTime, MaxTime, MissionPool[]}`). 윈도우마다 런 시작 시 `[MinTime,MaxTime]` 내 **랜덤 트리거 시각 롤**(서버권위, 런마다 변주) + 발화 시 풀 **균등 랜덤 1개** 스폰. 윈도우별 풀로 시간대 제한(예: 초반 풀에서 HoldZone 제외). 빈 풀=스킵. 기존 `{TriggerTime, Mission}` 단건 폐지.
- **보스 실물**은 후속(P6).
- **테스트 스케줄**: DataAsset 교체로 압축(예: 윈도우 50~120/240~300s·보스 300s). ⚠️ 압축값은 임시(프로덕션 전환 시 원복).

### 2-11. 메타 프로그레션
- `URogueliteSaveGame`(USaveGame) — 누적 재화, 업그레이드 트리 상태, 캐릭터/무기 해금
- `UGameplayStatics::AsyncSaveGameToSlot`
- Run 시작 시 영구 스탯을 GE로 캐릭터 ASC에 적용
- 통화 1종, Steam 클라우드 세이브 고려
- **저장 정책(P6)**: SaveData 버전 필드+마이그레이션, 슬롯명 규칙, Steam Cloud 대상, 저장 실패 처리, 해금 데이터 삭제/리네임 fallback, 런중 vs 로비 저장 구분. **`UGameInstanceSubsystem` SaveManager 경유**(UI/Actor가 SaveGame 직접 접근 금지)

### 2-12. 난이도 압박 수단 (이속 불변 유지)
스폰 밀도↑ / 원거리 적 비율↑ / 특수 적 패턴 / 미션 목표 압박 / 보스 phase pressure.
