# TaskPrompts_Master — 남은 작업 의존성 분해 + 유닛별 실행 프롬프트

> **목적**: 남은 로드맵 전체를 의존성 기준 유닛으로 분해하고, 각 유닛을 **새 세션에 복붙으로 시킬 수 있는 실행 프롬프트**로 제공한다.
> **작성**: 2026-06-13 (전수 분석: SSOT 전 문서 + 코드 grep + Plan 에이전트 적대적 검증 + 사용자 순서 확정).
> **갱신 규칙**: 유닛 완료 시 §B 표의 해당 행에 ✅ 표시 + `PROGRESS.md` 완료 이력 갱신. 설계가 바뀌면 해당 `Docs/SSOT/*.md` 먼저 고치고 이 문서의 프롬프트를 동기화한다.
> **프롬프트 사용법**: §C의 코드블록을 새 세션에 그대로 붙여넣는다. 모든 프롬프트는 플랜모드 우선·HIGH_RISK 승인 원칙(CLAUDE.md)을 전제한다.
> **이 문서를 관리하는 역할** = 프롬프트 매니저(`Docs/ProjectPromptManager.md`, 호출 `/pm`). 계획 작성·완료 검증·DAG 최신화를 그 페르소나가 수행한다.

---

## §A 현황 스냅샷 (2026-06-13)

### 완료 (main 머지, 실측 확인)
- **P0~P4 전부**: 스캐폴드 / 1P 캐릭터(Separated Arms) / 사격감(반동·확산·ADS·탄약) / 적 스웜(풀 500·플로우필드·Significance·스폰포인트) / 공유XP·카드 시스템(CommonUI 카드 UI·리롤·오프닝 시드) / 런 디렉터(시간 미션 스케줄·전역 프리즈) / 미션 6종+PointSet / 무기 7종 DA(Rifle·BurstRifle·Shotgun·Sniper·Bazooka·Grenade·ChargeLaser)+Knife / Fragment 모디파이어 / 게임필(히트마커·피격방향·원거리경고 소비자)
- **P5 FF(친선사격)**: 통합 데미지 헬퍼 `FPSRCombatStatics`, **아군 데미지 = FF ON일 때 50%**(`FriendlyFireDamageScale=0.5`), 자폭=폭발만 풀, 넉백 데미지 독립, FF 카드 4종 콘텐츠 완료
- **ChargeLaser 재설계**: 클릭 1회=자동 차징 시퀀스(ServerOnly GA), DA 적용 완료
- **P6-A 게임플로우 셸**: 메뉴→런→결과→메뉴 루프 + EndRun 프리즈. 현재 트리거는 디버그 `FPSR.EndRun [victory|defeat]` / `FPSR.ReturnToMenu` 뿐
- **운영**: origin 동기화 완료, phase 브랜치 정리 완료, `.uproject` VibeUE `Optional:true` 커밋 완료(`15d4e34`)
- **게임 루프 완성 후(2026-06~) main 머지분**: U2(패배)·U3a(약점)·U11a(로비/세션)·U18 카드 v2·U3+U4(보스 승리 루프)·콘텐츠 1차(3팩·VAT 적·보스 메시·④b 크로스헤어)·**balance/pass2**(룸 점진개방 스폰·문 Chaos 파괴·플로우필드 데이터드리븐 바운드/clearance·스폰존 비활성화 — `d285c69`)·**오디오 설정**(마스터 볼륨 MVP, 4 신규 클래스 — `747a9b2`). 상세=§B 표·`PROGRESS.md`·`git log`.
- **W1 검증(반복 게이트)**: 1차(`full-audit-2026-06-21`, bd44939: P1 0·P2 4·P3 7) + 2차(`full-audit-loop-20260623`, c59d80e: P2 1·P3 1·머지 `411bcf8`) + **3차 완료**(`full-audit-loop2-20260626`, eb6abb2: P1 0·P2 2·P3 4·머지 `b2c55d3`, 2026-06-26). **✅ W1 종결 → 다음=U1 재미게이트(+V2).**

### ✅ 게임 루프 닫기 (완료 2026-06-21 — 승리=보스/패배=전멸 양쪽 배선)
> 갱신 2026-06-20: 패배(U2)·약점(U3a) 완료, 트랙 B 로비/세션/트래블 루프(U11a) main 머지(`b3b364e`) 완료. 승리(보스)는 트랙 A U3→U4에서 닫았다(✅2026-06-21). **U18 카드 v2 완료(2026-06-20)·U3 스캐폴드+U4 콘텐츠 완료(2026-06-21). 다음 차례=W1 전체검증.** 완료 유닛 전체는 §B 표(✅)·`PROGRESS.md`·`git log` 참조.
| 항목 | 근거 | 상태 |
|---|---|---|
| 승리 = 보스 처치 | `FPSRRunDirectorSubsystem::EnterBoss`→`SpawnBoss` / 보스 `OnDeath`→`FPSRGameMode::NotifyBossDefeated`→`EndRun(Victory)` | ✅ 완료(U3 스캐폴드 `0181ed0` + U4 콘텐츠 `content/boss`, 2026-06-21): BossTime→보스 스폰→전무기 처치→VICTORY→로비 복귀(U11a). 약점(U3a)·체력바=U4 콘텐츠 |
| 패배 = 전원 사망 | `AFPSRCharacter::HandleOutOfHealth`→`SetDead`+`NotifyPlayerDefeated`→`AFPSRGameMode::AreAllPlayersDead()`→`EndRun(Defeat)` | ✅ 완료(U2, main 머지 `3506da9`, 2026-06-16) |
| `EndRun` 호출자 | `Source/FPSRoguelite/Private/Core/FPSRGameMode.cpp` (`bRunEnded` 래치) | ✅ 패배=자동(U2 `NotifyPlayerDefeated`) / 승리=자동(U3 `NotifyBossDefeated`→`EndRun(Victory)`) + 디버그 `FPSR.EndRun [victory\|defeat]` |

### 검증 게이트 (U1/V2 = ✅ 2026-06-30 합격, §5 정량만 보류)
- ✅ **§7-5 코어 재미 게이트**(Roadmap) = G1 합격(2026-06-30, 사용자 판정) / ⚠️ **§5 성능 정량 검증**(Insights/NetProfiler, 적 500, 호스트 기준 하드캡 확정, Push→RepGraph→Iris)=**미실시 보류** → 콘텐츠밸런싱/U14 perf 패스로 이월
- ✅ **2-client PIE**: FF(아군 50%·아군 런칭)·ChargeLaser 서버권위 = V2 합격(2026-06-30, 소스 재검증 36항목 + 사전 PIE 통과)

### 사용자 추가 확정 (2026-06-13)
- **Infima Games — Low Poly Animated Modern Guns Pack**을 기본 비주얼 베이스로 적용. `Content/Assets/LowPolyAnimatedModernGuns`(이미 LFS 커밋, 1,935 uasset). 무기 8종(AG14W/HVG7/LRAF9/MAK12/MR22/RC425/SP60/X13)+`_Melee`+1P팔 메시·애니메이션+머즐 이펙트(`PS_*`=레거시 Cascade)+무기/폭발/캐릭터 사운드 포함. 코드에 무기 메시 배선 부재(무기=순수 DataAsset, `FirstPersonArms`는 메시 미할당) → **V0 유닛 신설**
- **순서 확정**: 게임 플로우 완성 먼저(트랙 A ✅U2→✅U3a→**✅U18 카드 재설계**→✅U3→✅U4 · 트랙 B ✅U11a 병렬 완료), 그 후 양 트랙 합류 → W1 전체 검증, 그 후 U1 게이트 → V2 검증. (2026-06-16부터 2-클론 병렬 — §B-3 / U18=2026-06-20 사용자 추가·완료. **U3 스캐폴드+U4 콘텐츠 완료 2026-06-21. 다음=W1 전체검증(양 트랙 합류)**)

### 예정 — 사용자 추가 지시 대기 (2026-06-23)
> 사용자가 W1 후 추가로 지시 예정 — 슬롯만 예약(상세 설계는 지시 시점):
- **콘텐츠 밸런싱**: 무기/카드/적/보스 수치 튜닝. 슬롯 = U1 재미게이트(§7-5) 통과 후(게이트가 밸런스 기준 제공) + 컨설트 D5(G1 계측)·D6(시너지 축). 양산 전 골격 잠금 원칙(§E).
- **멀티플레이 루트**: U11b(멀티 보스-승리 통합 + 2-PC Steam E2E) 기반 + 추가 MP 흐름. U11a(로비/세션/트래블) 위에 얹힘, V2(2-client) 검증과 연계.

### 임시 테스트값 (미원복 — U14에서 원복)
- `DA_RunSchedule.MissionWindows` 윈도우 시간(테스트 압축값) + `BossTime 300s` → 프로덕션 = 미션 300/600/900s·보스 1200s(≈20분 런). 메모리 `p4a-temp-test-values`. **보스 E2E(U11)까지 압축값 유지가 의도**

---

## §B 의존성 DAG (22유닛)

### 실행 순서 (사용자 확정 2026-06-13)

```
[완료] ✅V0 무기 비주얼 · ✅V1 사각 오디오 · ✅V3 크로스헤어 · ✅U16 스핀업 LMG(+LMG 픽스)

[1차 — 게임 플로우 완성. ✅U2 후 2-클론 병렬(2026-06-16 사용자 결정, 충돌점=GameMode 1파일)]
 ┌─ 클론 A (순차, 전투/보스+카드): ✅U3a 약점 → **✅U18 카드 시스템 재설계(목표사양 9조건, U18a/b/c 완료 2026-06-20)** → ✅U3 보스 스캐폴드+승리(2026-06-21) → ✅U4 보스 콘텐츠+PIE(2026-06-21) → ✅W1 1·2·3차 완료(2026-06-21~26, 머지 `b2c55d3`) → **✅U1 재미게이트(+V2) 합격(2026-06-30, §5 정량 보류) → 2차 트랙 해금 → ✅U5 원거리적(2026-07-01, `cd7de43`) → ✅U6 Fragment(2026-07-01, `8d71b1c`) → 2차 잔여(U7/U8/U10/U15/U17) ← 현재**
 └─ 클론 B (병렬, 세션/로비): ✅U11a 로비·세션·트래블 루프 (main 머지 b3b364e, 2026-06-20 — 트랙 B 완료, 잔여 Steam E2E=U11b)
        ↓ (✅ 양 트랙 합류 완료 2026-06-21 — 트랙 B U11a + 트랙 A U3→U4 모두 완료. 승리/패배 양쪽 E2E 닫힘)
W1 전체 프로젝트 정합 검증 (Fable 전수 + Codex 분할) → 교정(fix/ 브랜치)
        ↓
U1 재미 게이트(§7-5)+성능 검증(§5)  →  V2 2-client 정합 검증
        ↓ (게이트 통과 시 해금)
[2차 — 의존 없음, 순서 자유]  ✅U5 원거리 적(2026-07-01) / ✅U6 Fragment 마무리(2026-07-01) / U8 GMS / U10 SaveGame
                              U7 플로우필드 높이 (U1 PIE 관찰 결과로 우선순위 확정)
                              U15 1P 무기 애니메이션 시스템 (U1 손맛 ① 불합격 시 최우선 / 합격 시 폴리시)
        ↓
✅U9 DBNO 수동부활=근접 자동부활 (U2 판정식 교체 — main 머지 `e38dfbe` 2026-06-30, DBNO Phase 1B)
        ↓
✅U11b 멀티 보스-승리 통합 + 2-PC Steam E2E (U11a 위에 U3 보스 OnDeath→로비 복귀 연결·런 리셋·최종 E2E — 사용자 2-PC Steam E2E 확인 2026-07-01)
        ↓
U12 UI/필 후속  →  U13 VFX/오디오 배선 (U8 소프트 선행)
        ↓
U14 임시값 원복 + §8 플레이스홀더 전환 + 폴리시/패키지 빌드
```

### 유닛 표

| # | 유닛 | 구분 | 선행 | 차단(블록) | 브랜치 |
|---|---|---|---|---|---|
| V0 ✅ | 무기 비주얼 베이스 적용(Infima 팩) — **완료 2026-06-14** (통합 스태틱 무기+머즐+사운드, PIE 통과) | C++/콘텐츠 | — | U1(손맛 판정) | `phase/p6-weapon-visuals` |
| ✅V1 | 최소 사각 경고 오디오(§2-14 당김 확정분) **완료(main 머지 `da76144`, 2026-06-15)** | C++/콘텐츠 | — | U1(판정 ④) | ~~`phase/p5-blindspot-audio`~~ 정리됨 |
| V3 ✅ | 기본 크로스헤어 HUD(정적 레티클) — **완료 2026-06-15** (십자+점·ADS숨김·히트마커 X 4선+Hit흰/Kill빨강, PreConstruct 변수화, PIE 통과) | 콘텐츠(+디버그 cvar 게이트 C++) | — | U1(손맛 ① 평가에 필수), U12(동적 스프레드 베이스) | `phase/p4d-crosshair` |
| ✅U16 | 스핀업 LMG(연사속도 가속 기관총) — 새 무기 1종 **완료(main 머지 `c0b28a9`, 2026-06-15)** | C++/콘텐츠 | — | U1(손맛 평가 포함), W1(검증 포함) | ~~`phase/p4c-spinup-lmg`~~ 정리됨 |
| ✅U2 | 패배 배선: 간이 전멸 판정→EndRun(Defeat) — **완료(main 머지, 2026-06-16)**: PlayerState `bIsDead`(복제)+GameMode 독립집계+사망 입력/공격 차단+적/XP 시체제외. PIE 통과 | C++ | — | U3, U9, U11a | `phase/p6-defeat-wiring` |
| ✅U3a | 약점 부위 데미지 시스템(헤드샷/디자이너 지정 존) — **완료(main 머지 `89b535b`, 2026-06-16)**: `UFPSRWeakpointComponent`(ECC_FPSRWeakpoint 격리)+`FPSRCombatStatics` 4헬퍼·4경로 배선·per-actor dedup·Weak 히트마커. 빌드+스모크+사용자 PIE 통과 | C++ | U2(순차) | U3/U4(보스 약점), U1(손맛 판정에 포함) | ~~`phase/p6-weakpoint-damage`~~ 정리됨 |
| ✅U18 | **카드 시스템 재설계**(목표 사양 9조건: 3 카드군·멀티효과·무기해금·행동훅·이동속도 + 검증·명명·안정ID) — **완료(U18a/b/c 전부 main `--no-ff` 머지, 2026-06-20)**. 상태이상 처치·상태창=시임 | 설계/C++/콘텐츠 | — | U6(소프트)·D3(상태축 시임)·U10(CardKey 시임). **U3 앞 전체 선행**(사용자결정 2026-06-20=A: U18a/b/c 모두 U3 앞, 분할-연기 없음). 서브유닛 분해 | `phase/card-*` |
| ✅U3 | D4 보스 스캐폴드+승리 배선 — **완료(main `--no-ff` 머지, 2026-06-21)**: `AFPSRBossBase:ACharacter`(EnemyHealthComponent 재사용→신규 데미지코드 0, ECC_Pawn 콜리전함정 회피)+`UFPSRBossDefinitionDataAsset`+전용 `AFPSRBossSpawnPoint`+RunDirector `SpawnBoss`(BossClass ?? C++폴백)+GameMode `NotifyBossDefeated`→EndRun(Victory, EndRun 본문 무수정). 빌드+스모크+Codex(P2교정)+사용자 PIE 통과. 약점/BP_Boss/스폰포인트배치/체력바=U4 | C++ | U2, U3a (시퀀싱: U18 후) | U4, U11b | ~~`phase/p6-boss-scaffold`~~ |
| ✅U4 | 보스 콘텐츠+PIE(콜리전 정합·체력바) — **완료(main `--no-ff` 머지 `content/boss`, 2026-06-21)**: BP_Boss(약점2: 머리x2.5·코어x2.0)+WBP_BossHealthBar(범용 EnemyHealthComponent 바인딩, HCClass로 class핀 우회)+DA_BossDefinition(체력3000)+RunSchedule 배선(BossTime300 유지)+L_Sandbox 스폰포인트(중앙 0,0,200). **신규코드 0**·정적/시각검증+사용자 PIE 통과 | 콘텐츠 | U3 | W1, U11b, U14 | ~~`content/boss`~~ 정리됨 |
| ✅U11a | **D5 세션+로비+트래블 루프**(Play→로비 허브, Steam 초대, 보스 제외 — 클론 B 병렬) — **완료(main 머지 `b3b364e`, 2026-06-20, 별도 클론 FPSRoguelite2)**: `FPSRSessionSubsystem`/`FPSRLobbyGameMode`/`FPSRLobbyPlayerController`/`FPSRLobbyWidget`/`FPSRLoadoutPoolDataAsset` + 로비 콘텐츠(L_Lobby·L_Transition·WBP_Lobby·DA_LoadoutPool) + Seamless 트래블. 머지게이트 P1(런리셋 ASC 베이스라인)·P2(초대 destroy-first·logout·syncDestroy) 교정. 빌드+스모크+PIE 솔로루프 통과. **잔여=2-PC Steam E2E→U11b** | C++/콘텐츠 | U2 + 메뉴↔로비 결정(✅ Play→로비 허브) | U11b | ~~`phase/p7-mp-loop-lobby`~~ 정리됨 |
| ✅W1 | 전체 정합 검증(**반복 게이트**) — 1차(P1:0/P2:4·full-audit-2026-06-21)·2차(P2:1/P3:1·머지 `411bcf8`·full-audit-loop-20260623)·**3차 완료(P1:0/P2:2/P3:4, main `--no-ff` 머지 `b2c55d3`, 2026-06-26)**: balance/pass2+오디오 신규분(52파일/+1940) 자율 Claude×Codex 루프 — net-freq 과호출(적500 핫패스)+설정오버레이 발사/조준 래치 P2 2건 교정, 빌드/스모크/Codex 머지게이트+사용자 PIE 통과. P3 4건 백로그. full-audit-loop2-20260626 | 검증/교정 | U4 | U1, V2 | ~~`fix/w1-loop-20260626`~~ 정리됨 |
| ✅U1 | 재미 게이트(§7-5)+성능 검증(§5) — **합격(사용자 판정 2026-06-30)**: 조건부합격의 MP 차단조건 해소(Phase 1A/1B 머지 `e38dfbe` + PIE 2-client 통과) + G1①~④ 합격(프리즈=B안 baseline-only, ⑤=G2 보류) + V2 소스 재검증 confirmed. **⚠️ §5 적500 정량 측정은 미실시(보류)** → 하드캡 잠정값 유지, 콘텐츠밸런싱/U14 perf 패스로 이월. 시트 `Docs/U1_GateSheet.md` §D. **→ 2차 트랙 해금** | 검증(사용자 PIE) | V0, V1, W1 | 2차 트랙 전체 | (검증·문서) |
| ✅V2 | 2-client(FF·ChargeLaser) — **합격(2026-06-30)**: MP E2E 복제 결함=Phase 1A/1B로 해소·머지(`e38dfbe`). FF(ON 50%/OFF 무피해+관통/런칭 독립)·ChargeLaser(클릭1회 자동차징·빔추적·재클릭무시·무기교체 취소·적/아군/관통) = HEAD 소스 재검증 confirmed(36항목 file:line) + 사전 2-client PIE 통과 | 검증(사용자 PIE)+C++ | W1 | U11b(소프트) | ~~`fix/mp-steam-e2e`~~ 머지됨 |
| ✅U5 | B1 원거리 적 AI+경고 생산자 **+ 콘텐츠**(원거리 적·적투사체·DA_EnemyRoster + 근접↔원거리 베이스 BP 분리·사거리 튜닝) — **완료(2026-07-01)**: 빌드+스모크+Codex P2교정(코드) → 사용자 PIE 통과 → main `--no-ff` 머지(`3c694ed feat` + `f5f0055 chore`). BP_EnemyMeleeBase/BP_EnemyRangedBase 아키타입 베이스 + BP_EnemyProjectile. 엑셀→DataTable 스탯추출=후반 일괄 보류 [[defer-excel-datatable-stats-extraction]] | C+++콘텐츠 | U1 게이트 | — | ~~`phase/p4-ranged-enemy`~~ 머지됨 |
| ✅U6 | A4 Fragment 마무리+AvailableModifiers 확장 — **완료(2026-07-01)**: 근접 fragment 훅(PreFire/OnHitActor/PostFire) + 슬롯상한(MaxFragmentSlots)/제거(RemoveFragment)/교체 안티치트(ServerSelectCardReplacement, 서버가 대상무기 distinct목록 대조→위조 인덱스 거부) + 무기별 UnlockableFeatures 아키타입 매트릭스 배선(commandlet). 빌드+스모크+Codex(P1/P2 교정)+사용자 PIE 통과 → main --no-ff 머지. 교체 UI=후속 시드 | C+++콘텐츠 | U1 게이트 | — | ~~`phase/p4b-fragment-finish`~~ 머지됨 |
| U7 | C1 플로우필드 높이/클리어런스 | C++ | U1 관찰 결과 | — | `phase/p2-flowfield-height` |
| U8 | C2 GMS 재구현 | C++ | U1 게이트 | U13(소프트) | `phase/infra-gms` |
| ✅U9 | D2 DBNO 수동부활 — **완료(main 머지 `e38dfbe`, 2026-06-30, DBNO Phase 1B)**: `EFPSRLifeState`(Alive/DBNO/Dead) 생존상태기계 + `UFPSRReviveComponent`(**근접 자동부활** — hold-interact 아님, ReviveRadius 내 생존 아군이 게이지 충전→부활) + 다운=제자리정지+아군관전 + 팀와이프(부활자 없으면 Dead) + 부활/카드재개 grace(무적+적통과). 빌드+스모크+Codex(P3 1건)+사용자 PIE 2인 통과. 후속(블리드아웃 활성·값튜닝·다운반격)=밸런스 | C++ | U2 | — | ~~`phase/p5-dbno`(=`phase/p1b-dbno`)~~ 머지됨 |
| U10 | D3 메타 SaveGame | C++ | U1 게이트 | 메타 콘텐츠 | `phase/p6-savegame` |
| ✅U11b | 멀티 보스-승리 통합 + 2-PC Steam E2E — **완료(사용자 2-PC Steam E2E 확인 2026-07-01)**: 구성요소 코드 기완비(U3/U4 보스 `OnDeath`→`NotifyBossDefeated`→EndRun(Victory)→U11a 로비복귀·런리셋 ASC 베이스라인) + V2 2-client PIE 통과. 실 2-PC Steam 종단 동작=**사용자 확인**(빌드/스모크로 검증 불가한 MP 게임플레이 게이트, §3.5) | C++/콘텐츠/검증 | ✅U11a·U3·U4·V2 | U14 | (검증·사용자) |
| U12 | UI/필 후속(크로스헤어·히트마커·카드 아이콘·무기별 팔 AnimBP) | C++/콘텐츠 | U1 게이트 | — | `phase/p4d-ui-followups` |
| U15 | 1P 무기 애니메이션 시스템(모듈러 부품+노리쇠+팔↔무기 이중 스켈 동기) | C++/콘텐츠 | U1 (조건부 우선순위) | — | `phase/p6-weapon-anim` |
| U13 | VFX/오디오 배선(폭발·빔·핑/Gibs·풀 사각오디오) | C++/콘텐츠 | U8 소프트 | — | `phase/p7-vfx-audio` |
| U14 | 임시값 원복+§8 전환+폴리시/패키지 빌드 | 혼합 | U4·U11b | (출시) | `phase/p7-polish` |
| U17 | 플레이어 설정 시스템(크로스헤어 크기 등) + 메인메뉴 Settings 진입 | C++/콘텐츠 | V3(크로스헤어 위젯) / 소프트 U1 게이트 | (감도·FOV·오디오 설정 확장) | `phase/settings-system` |

### 장기 백로그 (유닛화 보류 — 기록만)
- **복수 authored 고정 맵**(RunFlow §2-1): 맵 수·해금 방식은 콘텐츠 단계(P6+) 확정. U14 이후.
- **보스 2페이즈 본격화**: U3/U4는 "체력만 박스" 스캐폴드(P7-MultiplayerLoop_Plan 확정). 실제 보스(StateTree 패턴·페이즈 전환)는 별도 유닛으로 재기획.
- **에셋 확장**: V0는 무기/팔 한정. 적 메시(§8 큐브 교체)·환경 에셋은 컨셉 확정 후 별도(팩에 적 메시 없음).

---

## §B-3 작업·검토·머지 프로토콜 (트랙 내 순차 · 2트랙 병렬)

> **트랙 내부는 한 유닛씩 순차**, **두 트랙은 별도 클론에서 병렬**(2026-06-16 사용자 결정 — 클론 A=전투/보스 `U3a→U3→U4`, 클론 B=세션/로비 `U11a`, 충돌점=GameMode 1파일). 각 클론은 메인에서 phase 브랜치를 분기해 작업하고, 검증 통과 후 머지한 다음 같은 트랙의 다음 유닛으로 넘어간다. **단일 클론으로만 작업할 땐 한 유닛씩 순차.** (2026-06-15에 병렬을 한 번 폐지했다가 2026-06-16 2-클론 병렬로 재개 — 이 문구가 최신.)

### 순차 흐름
```
1. main에서 phase/<유닛-브랜치> 분기 (§6-7)
2. 플랜 확정(사용자 승인) → 구현(Haiku 위임)/검증(Opus 직접)
3. 자체 빌드(-WaitMutex) + 헤드리스 스모크 통과   ← 이게 "완료"의 정의. 미검증 머지 금지
4. 사용자 검토: 코드 = diff 또는 Codex 머지게이트(codex-review.ps1 -Base main) / 콘텐츠 = PIE
5. 승인 → --no-ff main 머지 + PROGRESS·TaskPrompts §B ✅ 갱신 + 콘텐츠 동반 커밋 질문 → 브랜치 삭제
6. 다음 유닛으로 (1부터 반복)
```

### 핵심 원칙
- **머지 전 검토**: main에 들어온 뒤 문제를 발견하면 되돌리기 비용이 크다. 빌드+스모크 통과 + 사용자 검토를 머지 **앞**에 둔다.
- **순서**: 의존성을 따른다 — 트랙 A `U3a → U3 → U4`(전투/보스, U2 완료) · 트랙 B `U11a`(세션/로비) 병렬 → 양 트랙 합류 후 `W1` → 게이트(U1) → 2차 트랙.
- **문서/PROGRESS 갱신은 해당 유닛 작업·머지에 포함**(별도 브랜치에 섞지 않음).

---

## §C 유닛별 실행 프롬프트

> 모든 프롬프트 공통 전제(프롬프트에도 내장됨): `Game.md`+`PROGRESS.md` 선행 독해 → 해당 도메인 SSOT만 추가 독해(§0-1 라우팅) / `phase/` 브랜치 분기(§6-7) / 플랜모드 우선·승인 후 진행 / 구현=Haiku 위임·검증=Opus 직접(§6-5) / **완료 = 자체 빌드(`-WaitMutex`)+헤드리스 스모크 통과 후 사용자 검토·머지(§B-3 순차). 미검증 머지 금지** / Codex 머지게이트(`Scripts/codex-review.ps1 -Base main`) / 승인 후 PROGRESS 갱신+`--no-ff` 머지+브랜치 삭제+사용자 콘텐츠 동반 커밋 여부 질문.
> **트랙 내 순차 · 2트랙 병렬**(2026-06-16) — 트랙 A `U3a → U3 → U4`(전투/보스) · 트랙 B `U11a`(세션/로비) 병렬, 합류 후 `W1`. 단일 클론 작업 시엔 한 유닛씩.

### V0 — 무기 비주얼 베이스 적용 (Infima Low Poly Animated Modern Guns)

```
Game.md + PROGRESS.md 먼저 읽어. Docs/TaskPrompts_Master.md의 유닛 V0를 진행한다.
읽을 SSOT: Docs/SSOT/CombatWeaponCard.md §2-4(무기/DataAsset 구조), Docs/SSOT/PlayerFeel.md §2-9(Separated Arms), Docs/SSOT/Workflow.md §6.

[목표] Content/Assets/LowPolyAnimatedModernGuns (Infima 팩, 이미 LFS 커밋됨)를 프로젝트 기본 비주얼 베이스로 적용.
현재 코드엔 무기 메시 배선이 전혀 없다(무기=순수 DataAsset, AFPSRCharacter::FirstPersonArms는 SkeletalMeshComponent만 생성·메시 미할당, FPSRCharacter.cpp:60).

- 브랜치: phase/p6-weapon-visuals 분기 (§6-7)
- 플랜 우선 → 승인 후 Haiku 구현 / Opus 검증(빌드+스모크+diff 자기비판)

[C++ 산출물]
1. UFPSRWeaponDataAsset에 비주얼/사운드 필드 신설(전부 TSoftObjectPtr/TSubclassOf, 기본 null=현재 동작 무회귀):
   1P 무기 SkeletalMesh, 팔 장착 AnimBP 또는 장착/발사 AnimMontage, 발사 사운드, 머즐 플래시 이펙트, 머즐 소켓명(FName — 팩 메시마다 소켓명이 다를 수 있어 DA 데이터화).
2. 장착 배선: 무기 교체(OnWeaponEquipped/OnRep) 시 FirstPersonArms에 무기 메시 컴포넌트 어태치/교체. 1P 전용(OnlyOwnerSee 등 1인칭 표준 설정 검토).
3. 발사 코스메틱 배선: 발사 시 로컬(오너 클라)에서 몽타주/사운드/머즐 플래시 재생. 기존 발사 경로(FireComponent/GA)에 코스메틱 훅만 추가.

[콘텐츠 산출물 — 사용자/VibeUE]
- 팔 메시·AnimBP 할당(BP_FPSRPlayer), 무기 DA 8종(Rifle/BurstRifle/Shotgun/Sniper/Bazooka/Grenade/ChargeLaser/Knife)에 팩 무기 8종(AG14W/HVG7/LRAF9/MAK12/MR22/RC425/SP60/X13 + _Melee) 매핑 — 어떤 팩 무기를 어떤 DA에 쓸지는 사용자에게 질문.

[함정/주의 — 반드시 지켜라]
- 트레이스/탄도/데미지는 기존 카메라 뷰포인트 기준 유지. 머즐 소켓은 코스메틱(플래시/빔 시각 원점) 전용. 게임플레이 수치(DA BaseStats)는 1도 바꾸지 마라.
- 에셋 경로 C++ 하드코딩 금지(ConstructorHelpers 금지 — XP 스피어 전철, Game.md §6-2). 전부 DA/BP 참조.
- 팩 파티클은 PS_*(레거시 Cascade) — UE5.7에서 동작 확인 후, 문제 시 머즐 플래시만 Niagara 전환(전환 범위는 플랜에서 보고).
- Demo/Mannequin 폴더는 사용 금지(데모용).
- 3P 무기 메시(타 플레이어 시점)는 이번 범위 밖 — 같은 DA 필드를 재사용하는 후속(U11 전 권장)으로 PROGRESS에 기록만.
- 멀티 코스메틱(다른 클라에서 보이는 발사 이펙트)은 U13(GMS 이후) 범위 — 이번엔 로컬 1P만.

[검증] 빌드+스모크 → 사용자 PIE: 무기 8종 교체 시 메시/팔 표시, 발사 시 사운드+플래시, 기존 수치 무회귀(반동/탄약/데미지). 머지 전 codex-review.ps1 -Base main.
완료 시: PROGRESS 갱신 + TaskPrompts_Master §B에 ✅ + 사용자 콘텐츠 동반 커밋 질문 + --no-ff 머지.
```

> **V0 방향성 결정(2026-06-14 확정)**: 무기 = **통합 Preview 스태틱 메시**(SM_LPAMG_<W>_Preview)를 WeaponMeshStatic1P에 사용. **근거(제1원리+SSOT)**: 이 게임의 손맛 코어는 §2-4-2(절차적 카메라 킥+확산+머즐+사운드)·§2-9(타격감=히트마커/Gibs/핑)로 정의되며 **모듈러 부품·노리쇠 풀 무기 애님은 코어가 아니다**. 따라서 스태틱+팔 애님으로 U1 손맛 게이트 판정이 가능하고, 무거운 무기 애님 투자 여부는 **게이트 결과로 결정**(투기성 선투자 회피). 스켈(WeaponMesh1P) 경로를 코드에 남겨 확장 길을 열어둠.
> **V0 후속(별도 유닛)**: ① 모듈러+노리쇠 풀 무기 애님 = **U15**(U1 게이트 후 조건부). ② 무기별 팔 AnimBP(BlendByEnum 그립) = **U12** 폴리시 편입. ③ 무기별 부착 오프셋(DA 필드) = U15 또는 경량 후속. ④ 범위 밖(기존 유지): 3P 무기 메시(U11 전), 멀티 코스메틱(U13), ChargeLaser 빔 VFX(U13).

### U16 — 스핀업 LMG (연사속도 가속 기관총)

```
Game.md + PROGRESS.md 먼저 읽어. Docs/TaskPrompts_Master.md의 유닛 U16을 진행한다.
읽을 SSOT: Docs/SSOT/CombatWeaponCard.md §2-4(무기/스탯)·§2-4-1(모디파이어/Fragment)·§2-4-2(사격감/반동), Docs/SSOT/Workflow.md §6.

[목표] 새 무기 1종 — **스핀업 LMG**: 발사 시작 시 연사속도가 느리다가 연속 발사할수록 빨라져 일정 시간(램프) 후 최대 연사속도에 도달하는 기관총. 1차 트랙 — 게임 플로우(U2~U4)와 독립이지만 발사 케이던스 코어를 건드리므로 W1 전체 검증과 U1 손맛 게이트의 평가 대상에 포함되어야 한다.

- 브랜치: phase/p4c-spinup-lmg 분기 (§6-7)
- 플랜 우선 → 승인 후 Haiku 구현 / Opus 검증(발사 케이던스 + 서버권위 = 세밀 검증 필수, Opus 직접 교정 가능)

[설계 — 조사로 확정된 배선 지점]
- **새 Archetype 불필요**: 기존 EFPSRWeaponArchetype::FullAuto 사용. 스핀업은 아키타입 속성이 아니라 "연속 발사 경과 시간에 따른 동적 FireRate"다. ChargeLaser처럼 별도 아키타입 만들지 마라.
- **스탯 필드 신설(FFPSRWeaponStatBlock, Public/Weapon/FPSRWeaponTypes.h)**: bool bHasSpinup + SpinupFireRateStart(시작 연사) + SpinupRampTime(최대 도달까지 시간). EditConditionHides=bHasSpinup(ChargeLaser 필드의 Archetype 게이트 패턴과 동일). BaseStats.FireRate = **최대 연사**로 정의(램프 종점).
- **동적 FireRate 계산**: CurrentRate = Lerp(SpinupFireRateStart, FireRate, Clamp(SpinupElapsed/SpinupRampTime, 0..1)). 한 함수로 빼서 클라/서버가 동일 식 사용(결정성).
- **FireComponent(UFPSRWeaponFireComponent) 클라 케이던스 3곳**(조사 기준 라인, 재확인 후):
  ① TickComponent 자동발사 루프의 Interval 계산(현재 1.0/FireRate)을 동적 CurrentRate로 + 발사 홀드 중 SpinupElapsed += DeltaTime.
  ② FireOneShot의 NextFireReadyTime 스탬프를 동적 CurrentRate로.
  ③ StopFiring / OnWeaponEquipped에서 SpinupElapsed 리셋.
- **서버 권위 일관성(★핵심)**: 클라 케이던스(NextFireReadyTime)와 서버 게이트(UFPSRWeaponInventoryComponent::ServerTryConsumeFireInterval의 MinInterval)가 **같은 스핀업 곡선**으로 계산되어야 한다. 히트스캔 GA(FullAuto, 클라 예측 있음)가 발사할 때 동적 MinInterval을 전달하려면 **서버도 스핀업 진행도를 추적**해야 함 — 서버 스핀 상태(마지막 발사 이후 gap으로 증가/리셋 추론, 멤버 1~2개)를 Inventory 또는 WeaponInstance에 두고 결정적 규칙으로 클라와 일치시킬 것.

[플랜에서 결정할 항목 — 지어내지 말고 사용자 질문]
- 멈추면(StopFiring) 스핀: 즉시 리셋 vs 짧은 유예 후 리셋 vs 점진 스핀다운(decay). LMG 손맛엔 점진 감소가 어울리나 복잡도↑ — 권장안 제시 후 결정.
- FireRate 카드(EFPSRWeaponStat::FireRate)가 무엇을 올리나: 최대 연사(BaseStats.FireRate)만 vs 시작 연사도 함께. 기존 GetResolvedStats 해석 경로 재사용 범위 결정.
- 무기 아키타입: 히트스캔 LMG 확정인지(투사체 LMG는 다른 GA). 기본=히트스캔(FullAuto, UFPSRGA_WeaponFire_Hitscan).

[함정/주의]
- **클라/서버 스핀 desync**가 최대 함정(ChargeLaser 재설계가 cross-channel race를 ServerOnly로 피한 교훈, 메모리 freeze-gate-client-server-symmetry). 결정적 동일 곡선 + 서버 게이트 25% 허용오차 안에 들어오는지 검증.
- 반동/확산은 추가 작업 불필요 — FireRate만 변하면 기존 "복구 빚" 반동·블룸이 자동으로 가속 추종(의도된 효과). 단 최고속에서 반동이 과하지 않은지 DA 튜닝.
- 전역 프리즈(§2-2) 중 SpinupElapsed 진행 정지(다른 무기 게이트와 대칭).
- 무기 교체로 스핀 뱅킹/우회 금지(OnWeaponEquipped 리셋 — equip 케이던스 쿨다운 EquipFireCooldown과 정합).
- AllWeapons 카드 제외축 검토: 필요 시 무기 DA의 AllWeaponsStatExclusions에 FireRate 추가(ChargeLaser=RecoilVertical 선례). 사용자 결정.
- 비주얼: 팩 무기 매핑은 V0 체계 재사용(스핀업 LMG에 맞는 팩 무기 1종 — 예 HVG7류 — 사용자 지정). V0 미완이면 메시 없이 기능만 검증 후 비주얼은 V0 머지 후.

[검증] 빌드+스모크 → PIE: 발사 시작 느림→연속 발사로 가속→램프 시간 후 최대 도달, 멈춤 시 결정 규칙대로 / 2-client에서 클라/서버 케이던스 일치(렉/거부 없이) / 카드(FireRate) 상호작용 / 무기 교체 시 스핀 리셋·무크래시 → codex-review.ps1 -Base main → PROGRESS 갱신+✅+사용자 콘텐츠(DA) 동반 커밋 질문+--no-ff 머지.
```

### V1 — 최소 사각 경고 오디오

```
Game.md + PROGRESS.md 먼저 읽어. Docs/TaskPrompts_Master.md의 유닛 V1을 진행한다.
읽을 SSOT: Docs/SSOT/PlayerFeel.md §2-14(게임필 — 사각 오디오 확정분), Docs/SSOT/Performance.md §5-1(Significance 티어), Docs/SSOT/Enemy.md §2-6.

[목표] §2-14 확정(2026-06-10): "최소 사각 경고 오디오(방향성 괴성/경고음)를 P4-D 말~P5로 당겨 코어 루프·재미 게이트(§7-5)와 함께 검증". 재미 게이트 판정 항목 ④(1인칭 사각 위협 체감)의 선행 조건이다. 풀 오디오 폴리시는 U13(P7) — 여기선 최소판만.

- 브랜치: phase/p5-blindspot-audio 분기 (§6-7)
- 플랜 우선 → 승인 후 Haiku 구현 / Opus 검증

[산출물]
- 후방/사각에서 접근하는 적에 대한 방향성(스테레오 패닝/공간화) 경고 사운드. 로컬 전용(기존 UFPSRPlayerFeedbackComponent 패턴 — 비복제·이벤트형 — 재사용 검토).
- 발화 조건: 근접 위협(거리/각도) + 과발화 억제(쿨다운/동시 상한). Significance 티어(§5-1)와 정합 — S2/S3 원거리 적까지 전수 각도 계산 금지.
- 사운드 에셋: Infima 팩 Audio 재사용 가능하면 재사용, 적합한 게 없으면 엔진/플레이스홀더 사운드로 기능 검증(에셋 교체는 U13).

[함정/주의]
- §2-14 확정: 근접/사각 *시각* 위협 표시는 의도적으로 제외됨 — 시각 인디케이터를 부활시키지 마라(사운드로만).
- 전역 프리즈(§2-2) 중 경고음 정지/억제.
- 수백 마리 스웜 — 적 단위 오디오 컴포넌트 상시 부착 금지(로컬 판정 기반 단발 재생 등 싼 구조, 제1원리 3줄 명시).

[검증] 빌드+스모크 → 사용자 PIE(뒤에서 접근 시 방향 인지 체감) → codex-review.ps1 -Base main → PROGRESS 갱신+✅+--no-ff 머지.
```

### V3 — 기본 크로스헤어 HUD (정적 레티클)

```
Game.md + PROGRESS.md 먼저 읽어. Docs/TaskPrompts_Master.md의 유닛 V3를 진행한다.
읽을 SSOT: Docs/SSOT/PlayerFeel.md §2-14(게임필/HUD), Docs/SSOT/CombatWeaponCard.md §2-4-2(확산/ADS), 메모리 [[vibeue-mcp-capabilities]].

[목표] 현재 화면에 조준점이 없어 사격 체감이 어렵다. **기본 정적 크로스헤어(중앙 레티클)**를 Game 레이어 HUD에 추가한다. U1 재미 게이트 손맛 판정 ①(스웜 사격 손맛)의 사실상 전제 — 게이트 전 1차 트랙에서 작업한다. 동적 스프레드(원 확대/축소)는 U12 — 여기선 정적 베이스만.

- 브랜치: phase/p4d-crosshair 분기 (§6-7)
- 주로 콘텐츠(WBP) + 소량 C++(ADS getter BP 노출이 필요한 경우만). VibeUE 또는 사용자 / Opus 직접 권장(콘텐츠 검증 성격)

[배선 — 조사로 확정]
- HUD 인프라 존재: UFPSRGameHUDWidget(UCommonActivatableWidget)가 PlayerController EnsurePrimaryLayout에서 UI.Layer.Game에 push됨(FPSRPlayerController.cpp:75). WBP_GameHUD가 그 BP 구현. 히트마커(WBP_HitMarker)도 같은 Game 레이어 중앙.
- **최소 작업 경로**: WBP_GameHUD에 크로스헤어 위젯을 캔버스 자식으로 중앙 정렬 추가(또는 WBP_BasicCrosshair 신설 후 자식 배치). 신규 코드 없이 콘텐츠만으로 화면 중앙 표시 가능.

[산출물]
1. 정적 크로스헤어 위젯: 중앙 십자/점(플레이스홀더 비주얼 OK — 폴리시는 후속). 히트마커와 중앙 정렬 정합(Z는 히트마커가 위).
2. ADS 시 처리: 조준 중 크로스헤어 숨김 또는 변경. 런타임 상태는 UFPSRWeaponFireComponent::IsAiming()(FPSRWeaponFireComponent.h:36-39) — BP에서 못 읽으면 BlueprintPure 게터/표면화 경로를 소량 C++로 노출(서버 권위 불필요, 로컬 코스메틱).

[함정/주의]
- 동적 스프레드 크로스헤어(원 레티클 확대/축소)는 만들지 마라 — U12 범위. U12가 GetCurrentBloom()(FireComponent.h:32-33)+GetResolvedStats().SpreadDegrees를 읽어 이 베이스 위젯을 확장한다(중복 구현 금지). V3는 정적 + ADS 토글까지만.
- 무기별 다른 크로스헤어(샷건 원 등)도 U12/폴리시 — 여기선 단일 기본형.
- ConstructorHelpers/에셋 경로 하드코딩 금지(§6-2). VibeUE 한계: 위젯바인딩 getter는 컴파일 후(P6-A 교훈).
- 메뉴(Menu 레이어)에선 크로스헤어 비표시 — Game 레이어 전용이라 자동 충족(확인만).

[검증] 빌드+스모크(코드 변경 시) → 사용자 PIE: 크로스헤어 중앙 표시, 발사 시 위치 정합, ADS 시 숨김/변경, 메뉴에선 미표시 → codex-review.ps1 -Base main → PROGRESS 갱신+✅+사용자 콘텐츠 동반 커밋 질문+--no-ff 머지.
```

### U2 — 패배 배선: 간이 전멸 판정 → EndRun(Defeat)

```
Game.md + PROGRESS.md 먼저 읽어. Docs/TaskPrompts_Master.md의 유닛 U2를 진행한다.
읽을 SSOT: Docs/SSOT/RunFlow.md §2-1(런 루프)·§2-2(전역 프리즈), Docs/SSOT/PlayerFeel.md §2-13(DBNO — 이번엔 미구현, 경계 파악용), Docs/SSOT/Workflow.md §6.

[목표] 게임 루프 "진짜 닫기" 1/2 — 패배. 전원 사망 시 AFPSRGameMode::EndRun(Defeat) 자동 호출.
현재: Source/FPSRoguelite/Private/Hero/FPSRCharacter.cpp:472-476 HandleOutOfHealth가 로그만 남김(주의: 경로가 Private/Hero/다, Player/ 아님). EndRun은 Core/FPSRGameMode.cpp:51, 디버그 FPSR.EndRun으로만 트리거됨.

- 브랜치: phase/p6-defeat-wiring 분기 (§6-7)
- 플랜 우선 → 승인 후 Haiku 구현 / Opus 검증

[산출물]
1. 사망 표식: HandleOutOfHealth(서버 OnOutOfHealth 바인딩)에서 플레이어를 Dead 상태로 마킹(간이판 — 입력/공격 차단 + 적 타게팅 제외 정도. 시체 연출/관전은 범위 밖).
2. 전멸 집계: "생존 플레이어 0명 → EndRun(Defeat)". 집계 함수를 GameMode/GameState에 독립 함수로 분리할 것 — 후속 U9(DBNO)가 판정식을 "전원 Dead 또는 DBNO"로 교체만 하면 되는 구조(P7-MultiplayerLoop_Plan §3-6 'Wiped' 판정의 재료가 됨).
3. 솔로(1인) 플레이 = 그 1인 사망 즉시 전멸.

[함정/주의]
- EndRun은 HasAuthority+bRunEnded 래치로 이중 호출 안전 — 그대로 활용하되 호출은 서버에서만.
- EndRunFreeze는 §2-2 bRunPaused 재사용+bRunEnded 래치로 카드선택 완료가 언프리즈 못 하는 구조(P6-A 구현) — 건드리지 마라. 메모리 [[freeze-gate-client-server-symmetry]]: 클라 입력+서버 RPC 양쪽 게이트 대칭 확인.
- Dead 상태에서 적 공격토큰/접촉 데미지·자석 XP 대상에서 제외되는지 확인(죽은 폰에 데미지 반복 금지).
- DBNO/부활/관전은 만들지 마라(U9 범위). 임시 구조 금지 — 집계 함수는 프로덕션 구조로.

[검증] 빌드+스모크 → PIE: 솔로에서 적에게 사망 → DEFEAT 결과창 → ReturnButton 메뉴 복귀 → 재시작 정상. 2-client(가능하면): 1명 사망=게임 계속, 2명 사망=패배. codex-review.ps1 -Base main → PROGRESS 갱신+✅+--no-ff 머지.
```

### U3a — 약점 부위 데미지 시스템 (헤드샷/디자이너 지정 존)

```
Game.md + PROGRESS.md 먼저 읽어. Docs/TaskPrompts_Master.md의 유닛 U3a를 진행한다.
읽을 SSOT: Docs/SSOT/Enemy.md §2-6·§2-10(데미지 경로/브릿지), Docs/SSOT/CombatWeaponCard.md §2-4-2(크릿/사격감), Docs/SSOT/PlayerFeel.md §2-14(히트마커), Docs/SSOT/Performance.md §5(스웜 비용 예산).

[목표] 적/보스의 **디자이너 지정 부위**(머리, 부패 부위 등)에 명중 시 추가 데미지("헤드샷/약점 사격") 시스템. 보스(U3/U4)가 1차 소비자이며 스웜 적에도 범용 적용 가능해야 한다(DESIGN-FIRST 범용성 원칙 — Enemy 전용으로 만들지 마라).

- 브랜치: phase/p6-weakpoint-damage 분기 (§6-7)
- 플랜 우선 → 승인 후 Haiku 구현 / Opus 검증(데미지 경로 전수라 세밀 검증 필수)

[산출물]
1. 약점 존 정의(디자이너 주도): BP에서 배치/조정 가능한 컴포넌트 기반(예: 약점 콜리전 셰이프 컴포넌트 + 존별 데미지 배수 프로퍼티) — 디자이너가 위치·크기·배수를 에디터에서 지정. 수치 하드코딩 금지(컴포넌트/DA 프로퍼티).
2. 판정 배선: 히트 결과의 컴포넌트 식별로 약점 여부/배수 결정 — **히트스캔(단일/관통)·투사체 직격·ChargeLaser** 경로. 판정/배수 적용은 FPSRCombatStatics 데미지 헬퍼 흐름에 통합(경로별 중복 구현 금지).
3. 적용 대상: AFPSREnemyBase(스웜 — per-BP/DA 옵트인) + U3 보스 베이스가 그대로 상속·소비할 수 있는 구조.
4. 피드백: 약점 명중 구분 — 기존 NotifyHitMarker(Hit/Crit/Kill) 체계에 약점 타입 확장(전용 마커/사운드 훅, 연출 콘텐츠는 보류).

[플랜에서 결정할 항목 — 지어내지 말고 사용자 질문]
- 크릿과의 스택 규칙: 약점 배수 × 크릿(곱연산) vs 배타 vs 약점=크릿 확정 트리거.
- 근접(Melee)/AOE(폭발)에 약점 적용 여부 — 정밀 조준이 아니므로 비적용이 자연스러우나 확정은 질문.
- ChargeLaser 워밍업 틱(고정 소뎀)에 약점 적용 여부 — 페이오프 빔만 권장(틱뎀 고정화 설계와 일관).

[함정/주의]
- **성능(§5)**: 적 500 × 추가 콜리전 프리미티브 = 쿼리 비용 증가. 스웜은 per-DA 옵트인 + 보스/엘리트 우선 적용을 기본 설계로. 전 스웜 적용 시 비용을 측정해 플랜에 명시(제1원리 3줄).
- **이중 히트 방지**: 약점 컴포넌트가 pawn-gather(AddDamageablePawnObjectTypes — ECC_Pawn+ECC_FPSRPlayerPawn) 멀티트레이스에 본체 캡슐과 함께 잡히면 같은 액터가 2회 데미지/관통 카운트 2회 소모될 수 있다 — per-actor 디듀프(가장 우선 히트 컴포넌트 1개 채택, 약점>본체 우선) + 관통 카운트는 액터당 1회.
- **서버 권위**: 약점 판정은 서버 트레이스/임팩트 결과 기준(클라가 "약점 맞았다" 보고하는 구조 금지).
- FF: 플레이어/아군에는 약점 없음(적/보스 전용).
- 약점 미배치 적 = 현행과 완전 동일(무회귀 — 기본값 경로에 분기 비용 최소).
- 콜리전 프로파일: 약점 셰이프는 데미지 쿼리에만 응답(이동/지면 트레이스/적 분리(separation)·플로우필드에 영향 금지).

[검증] 빌드+스모크 → PIE: 약점 명중=추가 데미지+구분 마커 / 본체 명중=현행 동일 / 관통 무기로 약점+본체 겹침 시 1회 판정 / 스웜 다수 스폰 시 프레임 회귀 없음 → codex-review.ps1 -Base main → PROGRESS 갱신+✅+--no-ff 머지. → U3(보스)가 이 인프라를 소비한다.
```

### U18 — 카드 시스템 재설계 (목표 사양 9조건) — ✅ 완료 2026-06-20

> **✅ 완료 (U18a/b/c, main `--no-ff` 머지).** v1 "카드 1=효과 1"(`ECardScope` enum) → **v2 확장성-우선 멀티효과**(폴리모픽 Instanced `UFPSRCardEffect` 서브클래스, enum+switch 폐기 — 새 효과=서브클래스 1파일·중앙 0수정) + 3 카드군[`ECardGroup` Character/Weapon/WeaponUnlock] + 무기해금(`WeaponUnlock` 오퍼·마일스톤 20/30/40·3정 차단) + 행동훅(무기 OnAim/OnFire/OnMiss/OnKill 5경로 공통 브릿지·`bJustKilled`) + 캐릭터 GAS-native 회복(`UFPSRPassiveAbility`) + 이동속도(`MoveSpeedMultiplier`). 상태이상 처치(OnStatusKill)·상태창·속성 elemental 거동=**시임만**(D3/후속).
> - **설계 SSOT** = `Docs/SSOT/CombatWeaponCard.md` §2-3(§2-3-1~9, v2 재작성). **플랜** = `Docs/U18{b,c}_*Plan.md` · `Docs/reviews/plan-U18-card-v2.md`. **실행 프롬프트 원본·상세** = `git log`(머지 `7a85773` U18a · `78b1bb5` U18b · `f02536a` U18c, 페이즈1 `d10a622`, 아카이브 `c72c900`) + `PROGRESS.md` U18 통합 보관 섹션.
> - **후행(비차단)**: U18d 기획자 툴(카드 카탈로그 에디터 유틸, 비런타임). U18b 후속 정리=빈 deprecated `AvailableModifiers` 필드 제거.
> - **다음** = U3 보스 스캐폴드(보스 OnKill 시임 소비).

_(실행 프롬프트 원본은 위 포인터의 git 머지 커밋·`PROGRESS.md`·`Docs/U18*Plan.md`에 보존. 완료되어 §C에서 축약함.)_

### U3 — 보스 스캐폴드(D4) + 승리 배선

```
Game.md + PROGRESS.md 먼저 읽어. Docs/TaskPrompts_Master.md의 유닛 U3(코드 백로그 D4)를 진행한다.
읽을 SSOT: Docs/SSOT/RunFlow.md §2-7(보스)·§2-2(프리즈), Docs/P7-MultiplayerLoop_Plan.md §3-5(보스 박스 설계 — 이 유닛과 통합됨), Docs/SSOT/Enemy.md §2-10(데미지 경로), Docs/SSOT/Workflow.md §6.

[목표] 게임 루프 "진짜 닫기" 2/2 — 승리. 보스 스캐폴드(체력만 있는 보스) + 처치 시 EndRun(Victory).
현재(라인 갱신 2026-06-20): Source/FPSRoguelite/Private/Run/FPSRRunDirectorSubsystem.cpp — EnterBoss()(:335)가 BossTime 도달 시 "boss actor is a P6 stub" 로그(:356)+타임라인 정지만. 디버그 DebugSkipToBoss()(:675)·FPSR.SkipToBoss cmd(:710).

- 브랜치: phase/p6-boss-scaffold 분기 (§6-7)
- 플랜 우선 → 승인 후 Haiku 구현 / Opus 검증
- 선행(전부 ✅ 머지 완료 2026-06-20): U2(전멸 판정·EndRun 배선 패턴 공유) + U3a(약점 부위 인프라 — 보스가 소비) + U18 카드 v2(무기 OnKill 훅 시임을 보스 처치가 소비 — 아래 [U18 시임] 참조)

[산출물]
1. ABossBase(또는 동급): UFPSREnemyHealthComponent 재사용으로 기존 무기 전 경로(히트스캔/투사체/레이저/근접) 데미지가 신규 코드 0으로 먹히게(P7 플랜 §1 확정). 큰 체력, 이동/공격은 스캐폴드 단계 생략 가능(체력만 박스 = 확정 스코프).
2. UFPSRBossDefinitionDataAsset: 체력/스폰 위치 규칙 등 콘텐츠 주도 필드(에셋 경로 하드코딩 금지). StateTree 골격(빈 상태기계)은 백로그 D4 명세대로 클래스만 — 실제 패턴은 장기 백로그.
2-b. U3a 약점 인프라 소비 확인: 보스 베이스에서 약점 컴포넌트 배치가 동작하는지(배치 자체는 U4 콘텐츠) — 신규 약점 코드를 여기서 또 만들지 마라.
3. RunDirector EnterBoss() 연결: 타임라인 정지 지점에서 보스 스폰/활성화(GameMode 설정 클래스 주입 — BP_Enemy 주입 패턴과 동일).
4. 승리 배선: 보스 OnDeath → (느슨한 결합) → GameMode → EndRun(Victory). **U11a가 이미 로비 복귀 시임을 구현했다(2026-06-20 완료)**: EndRun → EndRunFreeze가 GameState `OnRunEnded` broadcast → `AFPSRGameMode::HandlePostRunTravel`(FPSRGameMode.cpp:86, 구독 :82)가 3s 후 ServerTravel(Lobby). 주석에 "victory caller U3 adds (P7 §3-5)"로 이 시임이 명시돼 있다. **∴ U3는 보스 OnDeath→EndRun(Victory) 호출만 하면 로비 복귀가 자동으로 흐른다** — `EndRun`(:131)/`EndRunFreeze` 본문·`OnRunEnded` 구독 수정 금지(U11a 소유, 패배 경로와 공유). 멀티 컨텍스트 보스-승리 통합(보스별 런 리셋)은 U11b.

[함정/주의]
- P7-MultiplayerLoop_Plan §3-5의 AFPSRBossBox를 별도로 만들지 마라 — 이 유닛의 보스 베이스가 그 역할을 대체한다(중복 구현 방지, U11 프롬프트에도 동일 명시됨).
- 콜리전 = "가장 흔한 함정"(P7 플랜 §6): 보스 루트/메시는 ECC_Pawn 오브젝트타입으로 — WorldStatic이면 히트스캔의 Visibility 벽판정에 '벽'으로 잡혀 자기 몸이 탄을 차단한다. pawn-gather 경로는 FPSRCombatStatics::AddDamageablePawnObjectTypes(ECC_Pawn+ECC_FPSRPlayerPawn 양채널)와 정합 확인.
- 보스 페이즈 중에도 레벨업 전역 프리즈(§2-2) 동작 — 보스 로직(스폰 직후 활성화 포함)에 bRunPaused 게이트.
- 보스 비폰이면 넉백 비대상(LaunchCharacter 경로) — 의도된 동작, 버그 아님.
- GAS는 보스에 허용되는 영역(§1)이지만 이번 스캐폴드는 EnemyHealthComponent 확정 — GAS/ASC를 붙이지 마라(확장은 실보스 유닛에서 판단).
- **[U18 시임] 보스 처치 = 무기 OnKill 훅 발화 점검**: 보스가 EnemyHealthComponent + FPSRCombatStatics::ApplyDamage(enemy 브랜치, `bJustKilled` 전이) 경로로 데미지를 받으면 U18c 무기 OnKill 훅(예: 처치 시 재장전 fragment)이 보스 처치에도 발화한다. U18c가 "보스 OnKill 배선=U3"로 남긴 시임 — 보스가 표준 enemy 데미지 경로를 타는지 확인하고, 보스 킬이 OnKill fragment에 반응하는 게 의도인지 점검(코프스 재타격 `bJustKilled` 중복 차단도 보스에 적용되는지 확인). OnStatusKill은 D3까지 빈 시임.

[검증] 빌드+스모크 → PIE: FPSR.SkipToBoss → 보스 스폰 → 전 무기(8종)로 데미지 확인(+약점 U3a 배수) → 처치 → EndRun(Victory) → 결과 표시 → **로비 복귀(U11a HandlePostRunTravel 자동)** + 무기 OnKill fragment가 보스 킬에 반응하는지(U18 시임) 확인. U2와 합쳐 승/패 양쪽 E2E. codex-review.ps1 -Base main → PROGRESS 갱신+✅+--no-ff 머지.
```

### U4 — 보스 콘텐츠 + PIE (전체 게임 플로우 완성 지점)

```
Game.md + PROGRESS.md 먼저 읽어. Docs/TaskPrompts_Master.md의 유닛 U4를 진행한다.
읽을 SSOT: Docs/SSOT/RunFlow.md §2-7, Docs/P7-MultiplayerLoop_Plan.md §3-5·§6, Docs/P6A_GameFlow_UserContent_Guide.md(셸 콘텐츠 전례), 메모리 [[vibeue-mcp-capabilities]].

[목표] U3 코드 베이스 위에 보스 콘텐츠를 만들어 "메뉴→런→미션→보스→승/패→결과→메뉴" 전체 플로우를 PIE로 완성한다. 이 유닛이 끝나면 W1(전체 검증)으로 진입.

- 브랜치: U3 브랜치 동승(코드+콘텐츠 함께 머지) 또는 content/boss 별도 — 착수 시 사용자에게 질문
- VibeUE MCP 사용 가능(에디터 열림+플러그인 활성 시) / Opus 직접 권장(콘텐츠 검증 성격)

[산출물]
- BP_Boss(U3 베이스 상속) + DA_BossDefinition: 체력 등 수치 지정. 메시=플레이스홀더 박스 OK(실보스는 장기 백로그).
- **보스 약점 존 배치(U3a 인프라)**: BP_Boss에 약점 컴포넌트(예: 머리/코어 부위) 위치·크기·배수 지정 — 디자이너 조정 항목.
- L_Sandbox 보스 스폰 위치 지정(맵 중앙 — P7 플랜 확정 "맵 중앙 박스").
- 보스 체력 표시: 디버그 텍스트/단순 ProgressBar로 시작 가능(P7 플랜 §6 허용). 정식 HUD는 U12/U13.

[함정/주의]
- 콜리전 정합 재확인(U3 프롬프트와 동일): 보스 = ECC_Pawn 오브젝트타입. WorldStatic 금지(자기 몸 탄 차단). 적 지면 트레이스는 ECC_WorldStatic만 쿼리하므로 보스 발밑 바닥은 기존 규칙 유지(Mobility=Static+Block, WorldDynamic 금지 — PROGRESS 적 바닥 gotcha).
- 임시 테스트값(BossTime 300s)은 이 단계에서 원복하지 마라 — 빠른 반복 검증용으로 유지(원복은 U14, 메모리 p4a-temp-test-values).
- VibeUE 한계(메모리): 위젯 바인딩 getter는 컴파일 후 / FText는 Conv_StringToText / config는 ini 직접.

[검증 — 전체 플로우 PIE 체크리스트]
① 메뉴→Play→런 시작 ② 미션 발생·클리어·카드 보상 ③ BossTime 도달→보스 스폰 ④ 전 무기(8종)로 보스 데미지 + **약점 부위 명중=추가 데미지·구분 마커 / 본체=기본 데미지** ⑤-a 보스 처치→VICTORY ⑤-b (별도 런) 전원 사망→DEFEAT ⑥ 결과창→메뉴→재시작 무결성(상태 리셋).
완료 시: PROGRESS 갱신+✅ + 사용자 콘텐츠 동반 커밋 질문 + --no-ff 머지. → 다음 = W1 전체 검증.
```

### W1 — 전체 정합 검증 (반복 게이트, 자율 Claude×Codex 토론-교정 루프)

> **1차**(`full-audit-2026-06-21`, bd44939: P1 0·P2 4·P3 7 — 교정 머지) + **2차 루프**(`full-audit-loop-20260623`, c59d80e: P2 1·P3 1 — 대시 프리즈 갭 교정·머지 `411bcf8`) 완료. 코드베이스 매우 양호.
> **이 유닛 = 3차 자율 루프**(사용자 지시 2026-06-26): 2차(c59d80e) 이후 머지된 **balance/pass2(문/Chaos·룸 스폰·플로우필드 데이터드리븐·스폰존 비활성화)+오디오 설정** = **52파일/+1940줄, 5 신규 클러스터 미감사**. Claude×Codex 토론-교정 자율 루프. 사용자는 **최종 결과만** 받고 main 머지만 승인.

```
[복붙: W1 3차 전수검증 — 자율 Claude×Codex 토론-교정 루프. 사용자 중간개입 없이 수렴까지 진행하고, 최종 통합 리포트 1메시지 + main 머지 승인 요청만 한다.]

■ 먼저 읽기: Game.md + PROGRESS.md + Docs/TaskPrompts_Master.md(§A·§B·W1) + Docs/codex-reviews/full-audit-2026-06-21.md(1차) + full-audit-loop-20260623.md(2차). 두 감사가 확정한 수용설계·방법론·보류항목 흡수.

■ 베이스라인(2026-06-26): 현 main HEAD=747a9b2. 2차 감사=c59d80e. **3차 스코프 = `git diff --name-only c59d80e..HEAD -- 'Source/**'` (52파일/+1940줄)** = balance/pass2(`d285c69`)+오디오 설정(`747a9b2`). **1차/2차가 본 것(크로스헤어·VAT·보스메시·카드 v2 코어·기존 5데미지경로)은 재검 제외 — 아래 신규 5클러스터에 집중.**

■ 자율/안전 계약(사용자 승인 2026-06-26):
- 자율: 검증→Codex 토론→P1/P2 교정→빌드/스모크 게이트→재검증 루프를 **중간개입 없이** 수렴까지. 최종에 통합 리포트+적용결과만 1메시지.
- 게이트(절대): ① 교정은 `fix/w1-loop-<날짜>` 브랜치에만(main 직접·머지 자율 금지) ② 각 교정 후 빌드(-WaitMutex)+헤드리스 스모크 그린 필수, 깨지면 즉시 수정/롤백 ③ **main 머지=최종 리포트 후 사용자 승인** ④ force-push·--no-verify·hook 스킵·main 직접커밋 금지 ⑤ P1/P2만 자동교정, P3=백로그(트리비얼+무회귀 확실 시만).
- 모델: 구현=Haiku / 검증·토론판정·보안배선=Opus 직접(§6-5). [[haiku-delegation-security-wiring]] [[plan-codex-comparison-gate]] [[codex-gate-5min-watchdog]]

■ 신규 5클러스터 스코프 (정찰 매핑 완료 — file:line 핫스팟 직행):
① **문 파괴(Door/Chaos)** — `Door/FPSRDoor.{h,cpp}` + `Combat/FPSRCombatStatics.cpp:102-149`(unified ApplyDamage·bCountsAsKill 게이트) + `Weapon/FPSRProjectile.cpp:355-440`(IsHostileTarget). 핫스팟: Durability 150f/DamageStageThresholds 밸런스 정합 / late-joiner `BeginPlay:51-67` 권위無 cosmetic 동기(클라전용 검증) / `MARK_PROPERTY_DIRTY` 매 변경(:106,136) / IsHostileTarget가 HealthComponent로 문 판별(:365)→문 무조건 컴포넌트 보유 확인 / 프리즈 게이트=무기발사 진입에만(문 자체 無). 수용설계: 문=ECC_FPSRPlayerPawn 채널·EnemyHealthComponent(bCountsAsKill=false) 재사용·OnDoorBroken BP이벤트·문 자체 IsRunFrozen 無(무기게이트 의존).
② **룸 스폰+비활성화** — `Enemy/FPSRSpawnRoom.{h,cpp}` + `Enemy/FPSREnemySpawnSubsystem.cpp:149-201,506-617` + `Run/FPSRRunDirectorSubsystem.cpp:166-264` + `RunScheduleDataAsset.h:49-108`. 핫스팟: `ResetSpawnZones`(런시작 누적 누수 방지) / TickDirector 보스페이즈 vs 프리즈 게이트 분리(:463-470 보스 스폰 stall 안 되는지) / 자동태깅 `TActorIterator` O(n) BeginPlay(:54-66) / `SetNetUpdateFrequency` 매 무브틱(:351) 스로틀 / 오프닝시드 dual-activate 엣지(:207-210). 수용설계: Room.bReplicates=false·ActiveSpawnZones 서버전용 비복제·존트리거 ECC_WorldDynamic×ECC_FPSRPlayerPawn·접촉뎀 패스당1회.
③ **플로우필드 데이터드리븐** — `Enemy/FPSRFlowFieldSubsystem.cpp:35-127,302-474` + `Enemy/FPSRFlowFieldBoundsVolume.{h,cpp}`. 핫스팟(perf-critical 적500): FloorZ=PlayerStart 트레이스, 無이면 0.0f 기본(:43-54) / 셀 성장 math 커버리지 손실(:97-108) / FindNearestOpenCell LOS가 player.Z 직접→플랫폼 오판(:250-257) / AgentFootprintRadius 40cm 하드코딩 per-적타입 오버라이드無(:78) / BFS 전그리드 스캔 0.2s×40k셀 프레임예산(§5 Codex perf게이트). 수용설계: 프리즈 중 recompute 스킵(이동 게이트오프)·대각 코너컷 차단·바운드볼륨 옵셔널 발견·고정맵 가정.
④ **오디오 설정(4클래스)** — `Audio/FPSRAudioSubsystem.{h,cpp}` + `Settings/FPSRGameUserSettings.{h,cpp}`·`FPSRAudioSettings.h` + `UI/FPSRSettingsWidget.{h,cpp}` + `Core/FPSRPlayerController.cpp:382-409` + `Hero/FPSRCharacter.cpp:452-460`. 핫스팟: WeakLambda OnDeactivated가 PC 파괴 후 발화 안전(:407) / 슬라이더 OnMouseCaptureEnd+OnControllerCaptureEnd 양쪽 저장(:28-30) / SoundMix 미할당 LoadSynchronous null 무음 no-op(:55-62) / GameUserSettingsClassName FSoftClassPath 해석. 수용설계: 로컬전용 코스메틱(복제無)·설정오버레이 프리즈/사망 중 접근가능(메뉴라 의도)·콘솔 클램프 다운스트림·NoCapture 입력모드.
⑤ **미션존/카드/전투** — `Run/Mission/FPSRMission_HoldZone.cpp`·`FPSRMission_MovingZone.cpp`·`FPSRMissionActor.cpp:99-143` + `Card/FPSRCardSubsystem.cpp:76-335`·`FPSRCardEffect.cpp:43-54`·`FPSRCardDataAsset.cpp:46-140`. 핫스팟: 가중샘플 TotalWeight≈0 엣지 SelectedIndex 기본0(:203-214) / behavior-fragment 판별이 첫 효과만→혼합효과(behavior+magnitude) 오판(:172-186) / ResolveTargetInstance 위조 TargetWeapon 폴백 안티치트(:43-54) / CardFamily 런타임 검증 WITH_EDITOR-only(:85-86,136-140) / MovingZone 포인트캡처 복제 레이스(:70-82). 수용설계: 카드 등급=카드당1회 roll·효과별 magnitude독립(트레이드오프)·멀티효과 CardFamily 필수·클라 인덱스선택 안티치트·존반경 CVar 라이브튜닝·CardSelect 델리게이트 NativeDestruct 정리(2차 교정).

■ 루프(수렴까지): 각 라운드 슬라이스별 — ⓐ Claude(Opus) 검증(정적대조+서브에이전트 증거, 판정 직접) → ⓑ Codex 독립 적대 리뷰(`codex exec` read-only, C:\Users\koras\AppData\Local\OpenAI\Codex\bin\codex.exe -C 리포루트 -s read-only -a never; PS5.1 / 또는 `Scripts/consult-codex.ps1`) "반박·누락 찾기" → ⓒ 토론 수렴(오탐 severity 강등/기각·진짜결함 file:line 확정) → ⓓ P1/P2/P3 분류 → ⓔ P1/P2 교정 `fix/w1-loop-<날짜>`(Haiku)+빌드+스모크 게이트+Opus diff 무회귀 → ⓕ 회귀 재검. **종료=신규 발견 0 라운드 2연속(loop-until-dry) AND 5클러스터 전부 커버 AND 전 P1/P2 그린.** Codex 한도: 영역 분할(①Door+Combat ②Enemy spawn/flowfield ③Run/Mission ④Audio/UI), 5분 무출력=스킵. 적용 교정 일괄 `codex-review.ps1 -Base main`.

■ 수용설계 재보고 금지(1·2차 확정 + 위 5클러스터 수용설계 전부): 관통+ExplosiveRounds 1회폭발 / ChargeLaser 프리즈·Finding A / 보스 EnemyHealthComponent 바인딩·폴백체력 / U18 Instanced 효과·DeprecatedProperty 잔존 / bJustKilled / 보스킬=무기 OnKill(의도) / OnStatusKill·상태창·elemental=빈시임 / VAT 머티 CDO / +위 ①~⑤ 각 수용설계.

■ 최종 산출(사용자에게 이것만):
- 통합 리포트 `Docs/codex-reviews/full-audit-loop2-<날짜>.md`(2차 대비 신규 발견·토론 수렴근거·적용 교정·P3 백로그·잔여).
- `fix/w1-loop-<날짜>` 브랜치(빌드/스모크 그린).
- **사용자에게 1메시지**: "W1 3차 루프 완료 — 발견 N건(P1 x/P2 y/P3 z), 교정 적용·빌드/스모크 그린, main 머지 승인 대기. [핵심 발견·개선 3~5줄]" + PIE 전체플로우(U4 ①~⑥ + 신규: 문 파괴·룸 진행 스폰·오디오 슬라이더) 재실행 의뢰.
- 사용자 승인 → --no-ff main 머지 + PROGRESS·TaskPrompts §B W1 ✅. 통과 후 U1 재미게이트 진입 가능 보고.
```

### U1 — 재미 게이트(§7-5) + 성능 검증(§5) ※ V2와 같은 멀티 세팅에서 동시 진행

```
Game.md + PROGRESS.md + Docs/SSOT/Roadmap.md §7-5 + Docs/SSOT/Performance.md §5 전체를 먼저 읽어. Docs/TaskPrompts_Master.md의 유닛 U1(+동시 진행 V2)을 진행한다. 이 유닛은 코드 작성이 아니라 **판정 게이트**다 — 사용자 PIE 주도, 세션은 측정 세팅·체크리스트·기록을 담당.

[선행 확인] V0(무기 비주얼)·V1(사각 오디오)·W1(전체 검증·교정) 완료 — 손맛 판정은 실제 총기 메시/애님/사운드와 완성된 플로우에서만 유효하다. 미완이면 중단하고 보고.

[판정 항목 — §7-5 ①~⑤ 전부, 누락 금지]
① 스웜 사격 손맛(타격감·반동·학살 쾌감, §2-4-2) ② 카드 선택의 의미(빌드 체감 차이, §2-3 시너지) ③ 프리즈 페이싱 수용성(§2-2 — 비동기 Q/E 하이브리드 전환 필요성 판단) ④ 1인칭 사각 위협 체감(V1 사각 오디오) ⑤ 적 500 체감 성능.

[성능 검증 — §5, 동시 수행]
- Unreal Insights + NetProfiler로 적 500 시나리오 측정. **호스트(리슨서버)=최악 케이스 — 하드캡은 호스트 기준으로 확정**.
- 판정 순서 준수: Push Model(현행) → 부족 시 Replication Graph → 그래도 부족 시 Iris(Beta) 평가. Iris를 1순위로 꺼내지 마라(§3 — 디폴트 OFF).
- 산출물: 측정 수치 + 하드캡 확정값 + RepGraph/Iris 도입 여부 판정 → Docs/SSOT/Performance.md §5 갱신(SSOT 먼저 원칙).

[V2 동시 진행 — 같은 2-client 세팅에서, 단 체크리스트 분리(실패 원인 격리)]
- FF: FPSR.SetFriendlyFire 1 → 아군 사격=50% 데미지(10% 아님 — Roadmap §7-3의 10%는 stale), FF OFF=무피해, 바주카/유탄 아군 런칭(넉백은 데미지 독립).
- ChargeLaser(서버권위, PROGRESS 체크리스트): 클릭1회→자동차징(틱 소뎀 연속)→완료 본뎀1발 / 차징 중 조준이동=빔 추적 / 차징 중 재클릭 무시 / 무기교체 시 시퀀스 취소·무크래시 / 적·아군(FF)·관통 각각 판정.
- 알려진 한계 재발견 시 버그 보고 금지: Finding A(원격 클라 코스메틱 반동/게이트 레이스 — 데미지 무영향, U13에서 해소 예정), 플레이어 넉백 오너클라 스무딩 후속.

[불합격 처리 — §7-5 확정 규칙] 어떤 항목이든 불합격이면 **다음 콘텐츠 진행 전 루프 재설계 우선**: ①→§2-4 사격, ③→§2-2 프리즈(비동기 선택 하이브리드), ⑤→§5 성능 재검토를 트리거. 합격/불합격과 근거를 PROGRESS+해당 SSOT에 기록.

완료 시: 판정 기록 커밋(문서) + TaskPrompts_Master §B에 ✅ → 2차 트랙(U5/U6/U8/U10, U7) 해금 보고.
```

### V2 — (U1에 통합 실행) 2-client 정합 검증

> V2의 체크리스트는 U1 프롬프트에 내장되어 같은 멀티 세팅에서 동시 실행된다(사용자 확정 2026-06-13). 별도 세션으로 분리할 경우에만 U1 프롬프트의 `[V2 동시 진행]` 블록을 독립 프롬프트로 사용.

### U5 — B1 원거리 적 AI + 사전경고 생산자

```
[작업] U5 — B1 원거리 적 AI + 사전경고 생산자 (2차 트랙 / 코드 유닛)

■ 컨텍스트
  U1 재미게이트+V2 = ✅합격(2026-06-30, 커밋 4fc9190) → 2차 트랙 해금됨. U5는 그 첫 유닛.
  검증된 건 "근접 스웜 손맛"이다 — U5는 다음 위협축(원거리)을 검증된 루프 위에 얹는 콘텐츠 확장.

■ 먼저 읽을 것
  Game.md(§0-1 라우팅) + PROGRESS.md(핸드오프 j 최신) + Docs/SSOT/Workflow.md §6(빌드/브랜치/모델정책, 필독)
  + Docs/SSOT/Enemy.md §2-6(원거리 아키타입·공격토큰)·§2-10(투사체)
  + Docs/SSOT/PlayerFeel.md §2-14(원거리 경고 "소비자" — 이미 구현됨)
  + Docs/SSOT/Performance.md §5(복제/풀 예산 — ⚠️ §5 적500 정량 측정은 보류 상태)

■ 브랜치 / 모델 정책
  - main(4fc9190 이후)에서 phase/p4-ranged-enemy 분기 (Workflow §6-7)
  - 플랜 우선 → 사용자 승인 후 구현(Haiku 위임)/검증(Opus 직접). MP·복제·서버권위 RPC는 Opus 직접.
  - 콘텐츠 보류분(적 메시·투사체 VFX)은 만들지 마라 — 로직/베이스만.

■ 산출물
  1. 원거리 적 아키타입: 경량 Pawn 유지(GAS 금지 — 원칙1, 스웜 적엔 ASC 절대 X).
     사거리 내 정지 → 차징(예고) → 발사 사이클. AFPSREnemyBase 확장(별 클래스/서브클래스), 데이터 주도.
  2. 투사체: 기존 AFPSRProjectile + UFPSRProjectileSubsystem 재사용. Team=Enemy 경로
     (Enemy→플레이어 데미지 브릿지 보존) + IsHostileTarget 친화/instigator 차단 그대로(신규 데미지코드 0 지향).
  3. 사전경고 생산자: 차징 시작/종료 시 대상 플레이어 PC의 클라 RPC로
     UFPSRPlayerFeedbackComponent::ReceiveRangedTarget(SourceId, Location, bActive) 호출
     (P4-D 소비자 = id별 TMap 다수소스, 그대로 소비. 현재는 디버그 FPSR.TestRangedWarn만 구동 중).
  4. 공격토큰: 기존 토큰 상한 시스템에 원거리 토큰 통합(§2-6 — 무한 포격 방지).

■ [검증된 재사용 앵커 — 2026-06-30 HEAD 소스 재확인, 그대로 신뢰 가능]
  - 적 베이스/풀: AFPSREnemyBase(경량 Pawn, dormancy 풀링) + UFPSREnemySpawnSubsystem
    (하드캡 MaxActiveEnemies=500, 거리기반 NetUpdateFreq 티어 S0 30/S1 10/S2 5/S3 2Hz·UpdateStride 1/2/4/8
     — 원거리 적도 FPSREnemyBase라 이 티어링/스폰 게이트를 그대로 상속).
  - 투사체/데미지: AFPSRProjectile(IsHostileTarget = team 기반 친화 판정, 플레이어 채널 Overlap) +
    FPSRCombat::ResolveDamage/ApplyDamage/ApplyExplosion(통합 데미지 chokepoint). FF는 player↔player만 관장
    (Enemy→player는 FF와 무관하게 적중) — enemy 투사체는 다른 적을 맞히지 않게 IsHostileTarget로 차단됨.
  - 경고 소비자: UFPSRPlayerFeedbackComponent::ReceiveRangedTarget (FPSR.TestRangedWarn으로 검증 가능).
  - 프리즈: GameState bRunPaused / 전역 프리즈 게이트 + FTimerHandle PauseTimer 패턴(freeze-gate 클라/서버 대칭).

■ 함정 / 주의 (플랜에 명시)
  - 복제 투사체 ≤64 캡 FIFO 강제회수를 "플레이어 AOE와 공유"한다(§5). 원거리 적 다수 시 풀 고갈 →
    발사빈도 상한 / 적 투사체 비복제(코스메틱) 옵션 등 풀 예산 설계를 플랜에 박을 것.
    ⚠️ §5 적500 정량 측정이 보류라 복제 예산은 보수적으로(net-cull 미구현 = 클라당 전 적 복제 상태).
  - 전역 프리즈(§2-2): 차징·발사 사이클 타이머 전부 게이트 + PauseTimer(진행 중 타이머 멈춤).
  - 스폰 비율/아키타입 혼합 = DA/스케줄 데이터 주도(C++ 하드코딩 금지 — 원칙2).
  - ±10% 이속 편차·separation 등 기존 스웜 규칙과 충돌 없는지 확인.
  - 원거리 적도 약점(U3a UFPSRWeakpointComponent)·DBNO 타겟 제외(!Alive) 규칙 정합 확인.

■ 검증
  빌드(-WaitMutex) + 헤드리스 스모크 → PIE: 원거리 적이 차징 시 화면 경고 방향 표시(기존 위젯)·발사·피격,
  FF OFF에서 적탄이 다른 적/아군 오발 없음, 프리즈 중 차징/발사 정지.
  → Scripts/codex-review.ps1 -Base main(머지게이트) → PROGRESS 갱신 + TaskPrompts §B U5 ✅ → main --no-ff 머지.
```

### U6 — A4 Fragment 마무리

```
[작업] U6 — A4 Fragment 마무리 + AvailableModifiers 확장 (2차 트랙 / 코드 유닛)

■ 컨텍스트
  U5 원거리 적까지 완료(2026-07-01, 머지 cd7de43) → 2차 트랙 진행 중. U6 = 우선순위 #2.
  무기 모디파이어(Fragment) 시스템의 마지막 구멍(근접 GA 훅 + 슬롯 교체 플로우)을 메운다.

■ 먼저 읽을 것
  Game.md(§0-1) + PROGRESS.md(최신 핸드오프) + Docs/SSOT/Workflow.md §6(빌드/브랜치/모델정책, 필독)
  + Docs/SSOT/CombatWeaponCard.md §2-4-1(Fragment 훅 구조)·§2-3(카드)

■ 브랜치 / 모델 정책
  - main(a9457db 이후)에서 phase/p4b-fragment-finish 분기 (Workflow §6-7)
  - 플랜 우선 → 사용자 승인 후 구현(Haiku 위임)/검증(Opus 직접). 서버 교체 로직·안티치트는 Opus 직접.
  - 교체 UI 위젯 콘텐츠는 보류(레이아웃만 후속).

■ 산출물
  1. Melee fragment 훅: UFPSRGA_WeaponMelee에 fragment 훅(PreFire/OnHitActor/PostFire 상당) 배선 —
     형제 발사 GA(Hitscan/Projectile/ChargeLaser) 패턴과 대칭.
  2. Fragment 제거/교체 서버 로직: 슬롯 상한 도달 시 교체 플로우. 기존 안티치트 패턴 준수 —
     서버 발급 offer 캐시 + 인덱스 검증(클라는 인텐트만, 임의 fragment 적용 불가).
  3. 콘텐츠 꼬리: 무기별 AvailableModifiers 확장(현재 Rifle 중심+FF 카드 4종) —
     어떤 무기에 어떤 fragment를 열지 ★사용자에게 질문 후 등록(플랜 단계에서 결정).

■ [재사용 앵커 — grep으로 확인 후 재사용]
  - 형제 GA 훅: 발사 GA들은 fragment/행동 훅 보유(U18c에서 무기 공통 OnAim/OnFire/OnMiss/OnKill 추가). 근접 GA만 미배선.
  - 안티치트 offer 캐시: UFPSRCardSubsystem이 서버 발급 offer 캐시+인덱스 검증 보유([[card-pool-routing]] 패턴). 교체도 동형.
  - ThisWeapon 귀속: FFPSRCardDraw.TargetWeapon 서버 세팅(ResolveTargetInstance가 위조 TargetWeapon 폴백 차단).

■ 함정 / 주의 (stale 정보 주의)
  - PROGRESS 구절 "ModifyChargeTime/OnProjectileSpawn 훅 미완"은 stale — 둘 다 A3a/A3b에서 이미 구현됨.
    재구현하지 마라(grep 확인 후 재사용).
  - MaxStacks 규칙: 훅은 스택마다 적용(MultiShot 2스택=3발), MultiShot은 펠릿당 탄약 소모(잔량 클램프) — 기존 의미 유지.
  - fragment 카드 UI 규칙: 등급 대신 카테고리 라벨(FragmentCategoryText), 수치 빈칸(P4-B-2 확정).
  - 제거/교체는 ThisWeapon 귀속 구조 유지.

■ 검증
  빌드(-WaitMutex) + 헤드리스 스모크 → PIE: 칼에 fragment 적용·발동, 슬롯 초과 시 교체 동작,
  교체 악용(임의 인덱스) 서버 거부 → Scripts/codex-review.ps1 -Base main(머지게이트)
  → PROGRESS 갱신 + TaskPrompts §B U6 ✅ → main --no-ff 머지.
```

### U7 — C1 플로우필드 높이/클리어런스

```
[작업] U7 — C1 플로우필드 높이/클리어런스 (2차 트랙 / 코드 유닛, perf 민감)

■ 컨텍스트
  U5(원거리 적)·U6(Fragment) 완료 → 2차 트랙 진행 중. U7 = 우선순위 #3(HEAD 18db390).
  검증된 스웜 루프의 "이동 품질" 하드닝 — 매 런·모든 적에 영향.

■ [선행 확인 — 이미 충족]
  U1 §5 정량 PIE는 보류됐으나, 대상 결함은 이미 문서화돼 있어 착수 정당함(우선순위 재확인 불요):
   · 좁은 도어웨이 과차단 = balance/pass2 PIE 잔여(PROGRESS)·Performance §5-2 C1 "경계 벽 양쪽 셀 차단 트레이드오프"
   · 멀티레벨/높이(계단·단차) 경로 품질 = §5-2 C1 후속 항목
  → 이 두 개가 U7의 타깃. 추가 관측 필요 없음.

■ 먼저 읽을 것
  Game.md(§0-1) + PROGRESS.md(최신) + Docs/SSOT/Workflow.md §6(필독)
  + Docs/SSOT/Performance.md §5-2(플로우필드 — balance/pass2 데이터드리븐 바운드/clearance 반영분 포함)
  + Docs/SSOT/Enemy.md §2-6

■ 브랜치 / 모델 정책
  - main(18db390 이후)에서 phase/p2-flowfield-height 분기 (Workflow §6-7)
  - 플랜 우선 → 사용자 승인 후 구현(Haiku 위임)/검증(Opus 직접).

■ 산출물
  1. 멀티레벨/높이 인지 BFS 샘플링(계단·단차에서 적 경로 품질).
  2. 셀 클리어런스 인지 프로브: 현재 전셀 오버랩 방식이 경계 벽 양쪽 셀을 차단(좁은 통로 과차단) — 해소.

■ [재사용 앵커 — grep 확인 후 재사용]
  - 플로우필드: UFPSRFlowFieldSubsystem(BFS 라우팅·장애물 마스크) + AFPSRFlowFieldBoundsVolume
    (balance/pass2에서 데이터드리븐 바운드볼륨 + clearance-aware 프로빙 기도입 — 그 위에 높이/좁은통로 해소).
  - 지면 추종: AFPSREnemyBase::ApplyGravity down-trace.

■ 함정 / 주의
  - ApplyGravity down-trace는 의도적으로 ECC_WorldStatic만 쿼리(폰/투사체 위 착지 방지). 바닥 규칙(Mobility=Static+Block) 유지,
    **WorldDynamic 추가 금지**(비행 투사체 위 착지 부작용).
  - 0.2s 간격 멀티소스 BFS 예산(§5-2) 초과 금지 — 높이 샘플링 추가 비용을 측정해 플랜에 명시.
    ⚠️ §5 적500 정량이 보류 상태라 BFS 비용 증가는 특히 보수적으로(적500 핫패스).
  - 그리드/셀 크기(200cm) 변경은 전 맵 영향 — 변경 시 제1원리 3줄 명시.

■ 검증
  빌드(-WaitMutex) + 헤드리스 스모크 → PIE: 계단/단차 추격, 좁은 통로 통과(과차단 해소)
  → Scripts/codex-review.ps1 -Base main(머지게이트) → PROGRESS 갱신 + TaskPrompts §B U7 ✅ → main --no-ff 머지.
```

### U8 — C2 GameplayMessageSubsystem 재구현

```
Game.md + PROGRESS.md 먼저 읽어. Docs/TaskPrompts_Master.md의 유닛 U8(코드 백로그 C2)을 진행한다.
읽을 SSOT: Docs/SSOT/Architecture.md §3·§4(기술 채택/구조), Docs/SSOT/Performance.md §5(코스메틱 계약).

- 브랜치: phase/infra-gms 분기 (§6-7)
- 플랜 우선 → 승인 후 Haiku 구현 / Opus 검증

[산출물]
- GameplayMessageSubsystem(또는 동급 경량 메시지 버스) + Payload struct 정의. GameplayTag 채널 기반 publish/subscribe.
- 기존 GameplayEvent 태그(GameplayEvent.EnemyKilled/LevelUp/MissionComplete/MissionStart/PickupCollected — DefaultGameplayTags.ini에 이미 정의됨) 활용 설계.

[함정/주의]
- §5 계약: "히트/사망 코스메틱은 GameplayMessage/Cue — 복제 액터 상태 아님". U13(VFX/Gibs/핑)이 이 버스의 소비자다 — Payload struct에 U13 요구(위치/적 타입/킬 여부 등)를 미리 반영하되 과설계 금지.
- 수백 적 스케일: 메시지 발행 비용 최소(스택 할당 payload, 구독자 없으면 zero-cost).
- 엔진 플러그인 GameplayMessageRouter(Lyra 발췌) 사용 가능 여부를 먼저 엔진 소스에서 확인(§6-3 — UE5.7에 플러그인 존재 여부 grep) 후 자체 구현과 비교해 플랜에 명시.

[검증] 빌드+스모크+발행/구독 유닛 경로 로그 확인 → codex-review.ps1 -Base main → PROGRESS 갱신+✅+--no-ff 머지.
```

### U9 — D2 DBNO 수동 부활

```
Game.md + PROGRESS.md 먼저 읽어. Docs/TaskPrompts_Master.md의 유닛 U9(코드 백로그 D2)를 진행한다.
읽을 SSOT: Docs/SSOT/PlayerFeel.md §2-13(생존/DBNO), Docs/SSOT/RunFlow.md §2-2, Docs/P7-MultiplayerLoop_Plan.md §3-6(전멸 판정 연계).

- 브랜치: phase/p5-dbno 분기 (§6-7)
- 플랜 우선 → 승인 후 Haiku 구현 / Opus 검증. 다운 애니메이션/부활 UI 콘텐츠는 보류(디버그 표시로 검증).

[산출물]
1. Alive/DBNO/Dead 상태기계(서버권위): 체력 0 → DBNO(다운) → 아군 근접 상호작용 부활 게이지 → 부활 또는 블리드아웃 → Dead.
2. U2의 전멸 집계 판정식 교체: "전원 Dead" → "전원 Dead 또는 DBNO"(P7 플랜 §3-6) — U2가 분리해 둔 집계 함수만 수정.
3. 솔로 플레이: DBNO 없이 즉사 또는 즉시 전멸 — 정책을 플랜에서 사용자에게 질문.

[함정/주의]
- 블리드아웃 시간/전원 다운 처리 세부 밸런스는 문서 미확정 — 지어내지 말고 플랜에서 사용자 질문.
- 전역 프리즈(§2-2) 중 부활 게이지/블리드아웃 진행 정책도 문서 미정 — 사용자 질문 항목(권장: PauseTimer 대칭).
- 플레이어 캡슐=ECC_FPSRPlayerPawn 별도 채널 — DBNO 상태에서 콜리전 변경 시 FF 경로(AddDamageablePawnObjectTypes 양채널 쿼리)가 깨지지 않는지 확인.
- DBNO 중 적 공격토큰 대상 제외 여부(다운 킬 정책)도 질문 항목.

[검증] 빌드+스모크 → 2-client PIE: 다운→아군 부활→복귀 / 전원 다운→DEFEAT / 부활 게이지 서버권위(클라 조작 불가) → codex-review.ps1 -Base main → PROGRESS 갱신+✅+--no-ff 머지.
```

### U10 — D3 메타 SaveGame

```
Game.md + PROGRESS.md 먼저 읽어. Docs/TaskPrompts_Master.md의 유닛 U10(코드 백로그 D3)을 진행한다.
읽을 SSOT: Docs/SSOT/RunFlow.md §2-11(메타 프로그레션·저장 정책), Docs/SSOT/Architecture.md §4.

- 브랜치: phase/p6-savegame 분기 (§6-7)
- 플랜 우선 → 승인 후 Haiku 구현 / Opus 검증. 메타 업그레이드 트리 콘텐츠/UI는 보류(저장 인프라만).

[산출물]
1. URogueliteSaveGame: 버전 필드 + 마이그레이션 경로(§2-11 정책 전항목 준수).
2. SaveManager(UGameInstanceSubsystem): 모든 저장/로드는 이 서브시스템 경유 강제(UI/Actor 직접 SaveGameToSlot 금지). AsyncSaveGameToSlot 사용.
3. 저장 시점 구분: 런 중 vs 로비/메뉴(§2-11). 런 종료(EndRun) 시 재화/기록 영속화 훅.
4. 영구 스탯 적용 경로: 런 시작 시 GE로 ASC 적용(§2-11 — GAS는 플레이어 영역).

[함정/주의]
- Steam Cloud 호환 고려(파일 슬롯 구조) — U11 세션 작업과 충돌 없게.
- 호스트/클라 구분: 메타 저장은 각 로컬 플레이어 소유(서버가 타인 세이브 쓰지 않게).
- 검증 없이 마이그레이션 코드 방치 금지 — 구버전 더미 세이브로 로드 테스트.

[검증] 빌드+스모크+저장/로드/버전 마이그레이션 자동화 경로 → codex-review.ps1 -Base main → PROGRESS 갱신+✅+--no-ff 머지.
```

### U11a — D5 세션 + 로비 + 트래블 루프 (클론 B 병렬, 보스 제외)

> **이 유닛은 2-클론 병렬용이다.** 클론 A가 U3a→U3→U4(전투/보스)를 도는 동안, **별도 클론**에서 세션/로비/레벨트래블 루프만 만든다. 충돌점은 `AFPSRGameMode` 단 1파일 — 아래 [병렬 충돌 회피]를 반드시 지켜라.

```
Game.md + PROGRESS.md + Docs/P7-MultiplayerLoop_Plan.md **전문**을 먼저 읽어. Docs/TaskPrompts_Master.md의 유닛 U11a를 진행한다.
주의: 이 "P7"은 멀티플레이 루프 플랜 문서의 P7이다 — Roadmap §7-3의 P7(폴리시/빌드)과 무관(명칭 충돌, §D 참조).

[선행 확인] U2(전멸→EndRun(Defeat)) main 머지 완료(✅ 3506da9). 보스(U3/U4)는 선행 아님 — 이 유닛은 보스 없이 로비↔인게임↔로비 루프만 닫는다.

- 브랜치: 별도 클론에서 main(최신) 분기 → phase/p7-mp-loop-lobby (§6-7)
- **Opus 직접 구현**(플랜 §6 확정 — 세션/트래블/서버권위는 Haiku 위임 금지, 메모리 haiku-delegation-security-wiring)

[확정 결정 — 2026-06-16 사용자]
- **메뉴↔로비 = Play→항상 로비 허브**: L_MainMenu의 Play 버튼이 솔로여도 HostSession→L_Lobby로 진입(1인 로비). 로비가 모든 런의 허브. 결과창 복귀처 = **로비**(메인메뉴 아님). 메인메뉴는 Quit/(Join 수락 진입)만 잔존.
- **세션 백엔드 = Steam(app id 480)만**. Null/LAN 폴백 코드 만들지 마라(사용자 확정). dev 검증도 2-PC/2계정 Steam. 헤드리스 스모크는 모듈 로드까지만 보장.

[산출물 — 플랜 §4 순서, 단 ④보스 제외]
① Steam OnlineSubsystem 설정(.uproject 플러그인 + DefaultEngine.ini OSS/NetDriver, 플랜 §3-1) + UFPSRSessionSubsystem(Core/, Host/Find/Join/ShowInviteUI/초대수락, 플랜 §3-2).
② L_Lobby(신규 맵) + AFPSRLobbyGameMode(Core/, PostLogin 집계·호스트 시작 게이트·bUseSeamlessTravel) + 로비 UI(UFPSRLobbyWidget 베이스 + WBP: 플레이어목록/초대버튼/호스트 시작버튼, 플랜 §3-3).
③ Seamless travel 골격(TransitionMap 빈 맵 + 모든 GM bUseSeamlessTravel + GetSeamlessTravelActorList로 PС/PS 유지, 플랜 §3-4). 디버그 FPSR.TravelLobby/TravelGame로 골격 선검증.
④ 메뉴 Play→로비 배선: L_MainMenu Play → SessionSubsystem.HostSession → ServerTravel(L_Lobby?listen). (메뉴 도메인 — 클론 A 무간섭)
⑤ 로비 복귀(패배 경로로 루프 닫기): EndRun(Defeat=U2 ✅, 또는 디버그 FPSR.EndRun victory) → 결과 짧게 표시 → ServerTravel(L_Lobby, seamless). **보스 승리 자동화는 U11b** — U11a는 패배·디버그 트리거로 복귀 검증.
⑥ 런 리셋(로비 입장 시): XP/PartyLevel/카드/무기 인벤토리 + **bIsDead(U2 필드) 전원 false** + 프리즈 상태 풀 초기화(플랜 §3-6). seamless로 PlayerState가 살아오므로 run-state 명시 리셋 필수.
⑦ 콘텐츠(사용자/VibeUE): L_Lobby 맵·로비 UI WBP·TransitionMap.

[병렬 충돌 회피 — ★반드시 지켜라]
- **AFPSRGameMode = 유일한 공유 파일**(클론 A의 U3도 건드림). 충돌을 0으로 만들려면:
  · EndRun **본문을 수정하지 마라**. EndRun 이후 흐름(결과 표시→ServerTravel(lobby))을 **별도 신규 메서드**로 추가(예: HandlePostRunTravel). U3는 "보스 OnDeath→EndRun(Victory) **새 호출자**"만 추가하므로, 양쪽이 EndRun 본문을 안 건드리면 git 머지 충돌 없음.
  · 로비/세션/트래블 신규 로직은 전부 **신규 파일**(SessionSubsystem/LobbyGameMode/LobbyWidget)에 둔다.
- **게임플레이 맵(L_Sandbox)을 편집하지 마라** — 트래블 대상으로만 사용(ServerTravel "…/L_Sandbox?listen"). 보스 배치(U4)가 L_Sandbox를 소유하므로 .umap 바이너리 충돌 방지. U11a가 만드는 맵은 L_Lobby/TransitionMap(신규)뿐.
- **RunDirector 건드리지 마라**(U3의 EnterBoss 소유) — 매크로 상태는 GameState/LobbyGameMode로.
- 머지 순서: 먼저 끝난 트랙이 main 머지 → 나중 트랙이 main 리베이스 후 GameMode만 리졸브(서로 다른 메서드라 경미). [[freeze-gate-client-server-symmetry]]

[함정/주의]
- app id 480(스페이스워)=공용 — 타인 세션 충돌 가능, 소인원 테스트 한정(플랜 §3-1).
- Steam 테스트 = 클라 실행 상태 패키지 빌드 2-PC. PIE 폴백 = 세션 없이 디버그 트래블만 부분 검증(플랜 §4 팁).
- Seamless travel 불안정 시 non-seamless 폴백 플랜 준비(플랜 §6).
- 호스트 마이그레이션 없음 = 호스트 종료=세션 종료(확정) — 구현하지 마라.
- 프리즈(§2-2) 대칭: 로비/트래블 경로에 진행형 타이머 있으면 OnRunStateChanged 구독.

[검증] 단계별 빌드(-WaitMutex)+헤드리스 스모크(ModuleLoads) → 2-PC Steam E2E(메뉴 Play→로비→친구 초대→입장→호스트 시작→인게임→(패배 or 디버그 victory)→로비 복귀→런 리셋 확인→재시작) → codex-review.ps1 -Base main → PROGRESS 갱신+✅+사용자 콘텐츠(맵/UI) 동반 커밋 질문+--no-ff 머지.
완료 후: U11b(보스 승리 자동 복귀 통합)가 U3/U4 머지 후 이 위에 얹힌다.
```

### U11b — 멀티 보스-승리 통합 + 최종 2-PC Steam E2E

```
Game.md + PROGRESS.md + Docs/P7-MultiplayerLoop_Plan.md 먼저 읽어. Docs/TaskPrompts_Master.md의 유닛 U11b를 진행한다.

[선행 확인] U11a(로비/세션/트래블 루프) + U3/U4(보스) main 머지 완료 + V2(2-client) 통과 권장. 미완이면 중단·보고.
이 유닛은 U11a가 닫은 로비 루프 위에 **보스 승리 자동 복귀**만 얹고 전체 멀티 E2E를 마감한다(로비/세션/트래블 재구현 금지 — U11a 소유).

- 브랜치: phase/p7-mp-loop 분기 (§6-7) / Opus 직접 구현

[산출물]
① U3 보스 OnDeath 델리게이트 → GameMode → EndRun(Victory) → (U11a의 HandlePostRunTravel) ServerTravel(L_Lobby). U11a 패배 경로와 동일 시임 재사용(중복 구현 금지).
② 멀티 컨텍스트 런 리셋 보강: 보스 상태/스폰·RunDirector 보스 페이즈가 로비 복귀 시 초기화되는지 확인(U11a 리셋에 보스 항목 추가).
③ 보스 콜리전 정합 멀티 재확인(서버권위 데미지가 클라에서도 일관 — 플랜 §6 "가장 흔한 함정").

[검증] 빌드+스모크 → 최종 2-PC Steam E2E(초대→로비→인게임→미션/카드→보스 등장→**보스 처치=승리→로비 복귀** / 전원 사망=패배→로비 복귀→재시작) → codex-review.ps1 -Base main → PROGRESS 갱신+✅+--no-ff 머지.
```

### U12 — UI/필 후속 묶음

```
Game.md + PROGRESS.md 먼저 읽어. Docs/TaskPrompts_Master.md의 유닛 U12를 진행한다.
읽을 SSOT: Docs/SSOT/PlayerFeel.md §2-14, Docs/SSOT/CombatWeaponCard.md §2-4-1·§2-4-2, Docs/P4-C_WeaponContent_SpecSheet.md §4, 메모리 [[vibeue-mcp-capabilities]].

- 브랜치: phase/p4d-ui-followups 분기 (§6-7)
- 플랜 우선 → 승인 후 Haiku 구현(위젯 C++)/콘텐츠는 VibeUE 또는 사용자 / Opus 검증

[산출물 — 순서 고정]
1. 동적 스프레드 크로스헤어(원 레티클): **V3 기본 크로스헤어를 베이스로 확장**(V3는 정적+ADS 토글 완료). 발사 랜덤(SpreadDegrees+VRandCone)은 이미 구현됨 — **시각만 신규**: GetCurrentBloom()(FireComponent.h:32-33)+GetResolvedStats().SpreadDegrees 읽어 원 크기 변조 + 무기별 크로스헤어(샷건 원 등). 발사 로직 중복 구현 금지(스펙시트 §4). V3 위젯을 새로 만들지 말고 확장.
2. 히트마커 최종 연출 재확인 — 크로스헤어(V3/동적) 작업 **후**(P4-D 이월 순서 고정).
3. 카드 UI 소속 무기 표시: UFPSRWeaponDataAsset.Icon 필드 신설 + UFPSRCardEntryWidget에 FFPSRCardDraw.TargetWeapon 바인딩(아이콘+무기명). TargetWeapon=서버 세팅·클라 위조 불가 구조 유지.

[차징 게이지 HUD — 양자택일을 플랜에서 결정]
차징 게이지는 U13의 "서버 차징 시작/종료 클라 notify"(ChargeLaser Finding A 해소·빔 VFX와 같은 신호)를 쓰는 게 적절하다(PROGRESS 확정 — "같은 신호"). 선택지: (a) notify 배선을 이 유닛으로 당겨 게이지까지 완성 (b) 게이지를 U13으로 이월. 로컬 추정으로 임시 구현하지 마라(U13에서 재배선됨 — 임시 구조 금지).

[함정/주의]
- VibeUE 한계: 위젯바인딩 변수 getter는 컴파일 전 생성 불가(P6-A 교훈 — PreConstruct 파라미터화 회피, per-WBP 베이크), FText=Conv_StringToText.
- fragment 카드=카테고리 라벨 규칙 유지(등급/수치 표시 금지).

[검증] 빌드+스모크 → PIE: 샷건/바주카 크로스헤어 원 확대·축소, 카드 UI 무기 아이콘, (선택 시) 차징 게이지 → codex-review.ps1 -Base main → PROGRESS 갱신+✅+--no-ff 머지.
```

### U13 — VFX/오디오 배선

```
Game.md + PROGRESS.md 먼저 읽어. Docs/TaskPrompts_Master.md의 유닛 U13을 진행한다.
읽을 SSOT: Docs/SSOT/PlayerFeel.md §2-14, Docs/SSOT/Performance.md §5(코스메틱=GMS/Cue 계약), Docs/SSOT/Enemy.md §2-10.

[선행 확인] U8(GMS) 머지 권장(소프트 선행) — 히트/사망 코스메틱은 GMS 경유가 §5 계약. U12에서 차징 게이지 (a)/(b) 어느 쪽을 택했는지 PROGRESS 확인.

- 브랜치: phase/p7-vfx-audio 분기 (§6-7)
- 플랜 우선 → 승인 후 Haiku 구현 / Opus 검증. 에셋: Infima 팩 Effects/Audio 우선 재사용(PS_*=Cascade — UE5.7 동작 확인, 필요 시 Niagara 전환).

[산출물]
1. 투사체 폭발 VFX 배선(로켓/유탄 — 코드 훅, 콘텐츠 가이드 §4 이월).
2. ChargeLaser 빔 VFX + **Finding A 해소**: 서버 차징 시작/종료 클라 notify 신설(빔 VFX와 동일 신호 — PROGRESS 확정 해법). 원격 클라 코스메틱 레이스(반동 램프/게이트)가 이 notify로 정리되는지 확인. 데미지는 이미 서버권위 — 재설계 금지.
3. 핑/Gibs: GMS(U8) 경유 경량 코스메틱(§5 — 복제 액터 상태 금지, Gibs=과도 연산 금지 §2-14).
4. 풀 사각 오디오 폴리시(V1 최소판 위에 에셋/믹스 확장) + 히트스캔 트레이서(비복제 코스메틱).

[함정/주의 — 알려진 수용 설계, "버그"로 고치지 마라]
- 관통(MaxPenetration>1)+ExplosiveRounds = 탄착점 1회만 폭발(설계 수용, Codex P2 문서화됨) — 폭발 VFX도 탄착점 1회만.
- ExplosiveRounds 적 직격 히트마커 2회(직격+스플래시) = 알려진 폴리시.
- ChargeLaser 차징 중 프리즈 = 타이머 계속+데미지만 스킵(의도적 단순화) — PauseTimer로 "고치지" 마라.

[검증] 빌드+스모크 → PIE(+2-client에서 Finding A 해소 확인: 원격 클라 차징 표시/빔 정합) → codex-review.ps1 -Base main → PROGRESS 갱신+✅+--no-ff 머지.
```

### U14 — 임시값 원복 + §8 플레이스홀더 전환 + 폴리시/패키지 빌드

```
Game.md + PROGRESS.md + Docs/SSOT/Roadmap.md §8(플레이스홀더 인벤토리) 먼저 읽어. Docs/TaskPrompts_Master.md의 유닛 U14를 진행한다. 메모리 [[p4a-temp-test-values]] 원복 의무 이행 유닛이다.

[선행 확인] U4(보스)·U11(멀티 E2E) 완료 — 압축 스케줄 값은 E2E 검증까지 쓰고 원복하는 게 확정 운영.

- 브랜치: phase/p7-polish 분기 (§6-7). 원복은 content 커밋으로 분리.
- 플랜 우선 → 승인 후 Haiku 구현 / Opus 검증

[산출물]
1. **임시값 프로덕션 원복(사용자에게 노티 후)**: DA_RunSchedule.MissionWindows 윈도우 시간(테스트 압축값 예 50~120/240~300s) → 프로덕션 미션 300/600/900s, BossTime 300s → 1200s(≈20분 런). 원복 후 풀 길이 런 1회 PIE.
2. **§8 인벤토리 전수 전환**: 발사/근접 DrawDebug → #if !UE_BUILD_SHIPPING 게이트(또는 VFX 대체 확인) / FPSR.* 콘솔 커맨드 shipping 제외 / AFPSRXPPickup의 ConstructorHelpers 스피어 제거(§6-2 하드코딩 금지 위반 잔존 — DA/BP 참조로) / 적 큐브·FP팔 등 잔여 플레이스홀더 메시 상태 점검(교체 에셋 없으면 목록화만).
3. **Roadmap §7-3 P7 폴리시**: CommonUI 폴리시(입력 라우팅/패드), 오디오/이펙트 폴리시 잔여(U13과 분담 — U13=배선, 여기=마감), Insights 최종 측정, README, **패키지 빌드**(Win64 Shipping까지).

[함정/주의]
- 원복 커밋 전 반드시 사용자 노티(메모리 규칙 — 노티·원복 의무).
- shipping 게이트 후 스모크/패키지에서 디버그 의존 코드 컴파일 깨짐 주의(에디터 전용 분리).
- 패키지 빌드에서 LFS 에셋(Infima 팩) 쿠킹 시간/용량 — 미사용 폴더(Demo 등) 쿠킹 제외 설정 검토.

[검증] 빌드+스모크+패키지 빌드 성공+패키지 실행 E2E(메뉴→런→보스→결과) → codex-review.ps1 -Base main → PROGRESS 갱신+✅+--no-ff 머지.
```

### U15 — 1P 무기 애니메이션 시스템 (모듈러 + 노리쇠)

```
Game.md + PROGRESS.md + Docs/V0_WeaponVisual_UserContent_Guide.md(V0 베이스/스켈 경로) 먼저 읽어. Docs/TaskPrompts_Master.md의 유닛 U15를 진행한다.
읽을 SSOT: Docs/SSOT/PlayerFeel.md §2-9(Separated Arms)·§2-14, Docs/SSOT/CombatWeaponCard.md §2-4-2(사격감).

[목표] V0는 통합 스태틱 메시 + 팔 애님(게이트 검증용)으로 마감했다. 이 유닛은 Infima 팩이 완전 지원하는 **모듈러 SK 무기 + 노리쇠(Bolt) 본 + 팔↔무기 이중 스켈레탈 동기 애님**으로 무기 피델리티를 올린다(A_FP_WEP_*_Fire_Bolt/Reload 등).

[착수 조건 — U1 게이트 결과로 우선순위 확정]
- U1 손맛 판정 ①(사격 손맛)이 **불합격**이면 이 유닛이 §2-4 사격 보강의 일부로 2차 트랙 최우선.
- **합격**(스태틱+팔 애님으로 손맛 충분)이면 피델리티 폴리시로 후순위(U12 이후). PROGRESS에서 U1 판정 확인 후 착수, 미정이면 사용자에게 우선순위 질문.

- 브랜치: phase/p6-weapon-anim 분기 (§6-7)
- 플랜 우선 → 승인 후 Haiku 구현 / Opus 검증

[산출물]
1. SK 무기 경로 활성화: V0가 남겨둔 WeaponMesh1P(스켈) 사용 + 무기 AnimBP(노리쇠/재장전 몽타주).
2. 모듈러 부품 동적 부착: DA 부품 목록(배럴/탄창 등) → 소켓 부착(에셋 경로 하드코딩 금지, DA 데이터).
3. 팔↔무기 이중 스켈 동기: 발사/장전 시 팔 AnimBP와 무기 AnimBP 동기 재생.
4. (선택) 무기별 부착 오프셋 DA 필드 — V0 후속 ③ 흡수.

[함정/주의]
- 게임플레이 무영향: 트레이스/탄도/데미지/케이던스는 V0와 동일하게 카메라 뷰포인트 기준 유지. 애님은 코스메틱 전용(머즐 소켓=활성 메시).
- 재장전 애님 길이와 ReloadTime 스탯 정합(애님이 스탯보다 길면 무기 사용 가능 타이밍 어긋남) — 애님은 ReloadTime에 맞춰 스케일/동기.
- 전역 프리즈(§2-2) 중 몽타주 정지.
- Demo/Mannequin 폴더 사용 금지. 에셋 경로 하드코딩 금지(§6-2).
- 멀티(타 플레이어가 보는 3P 무기 애님)는 별도(U11 전 3P 메시와 함께) — 이번엔 1P만.

[검증] 빌드+스모크 → PIE: 발사 시 노리쇠 왕복, 재장전 애님↔스탯 정합, 무기별 부품 정확 부착, 게임플레이 수치 무회귀 → codex-review.ps1 -Base main → PROGRESS 갱신+✅+사용자 콘텐츠 동반 커밋 질문+--no-ff 머지.
```

### U17 — 플레이어 설정 시스템 (크로스헤어 크기 등) + 메인메뉴 Settings 진입

```
Game.md + PROGRESS.md 먼저 읽어. Docs/TaskPrompts_Master.md의 유닛 U17을 진행한다.
읽을 SSOT: Docs/SSOT/PlayerFeel.md §2-14(HUD/크로스헤어), Docs/SSOT/Architecture.md §3-4(설정/세이브 위치), Docs/SSOT/Workflow.md §6, 메모리 [[vibeue-mcp-capabilities]].

[배경] V3에서 정적 크로스헤어를 추가(WBP_BasicCrosshair, CrosshairRoot Overlay 단일 루트). 사용자 요청: 크로스헤어 크기를 인게임에서 조절·영속. 현재 플레이어 설정 인프라 전무(UGameUserSettings 서브클래스 없음, 옵션 메뉴 없음). FPSRGameFlowSettings는 UDeveloperSettings=프로젝트 dev 설정이라 무관.

[목표] 플레이어 로컬 설정 시스템의 첫 슬라이스 = 크로스헤어 크기. 메인 메뉴 Settings 버튼에서 슬라이더로 조절, 세션 간 영속, 인게임 HUD 실시간 반영. 이후 감도/FOV/오디오로 확장되는 토대.

- 브랜치: phase/settings-system 분기 (§6-7)
- 플랜 우선 → 승인 후 Haiku 구현 / Opus 검증(설정 영속+델리게이트 배선 세밀 검증)

[아키텍처 결정 — 제1원리 3줄(2026-06-15 사용자 확정)]
① 제1원리: 크로스헤어 크기 = 로컬 코스메틱 선호값(영속+인게임조절+비복제) → 엔진표준 UGameUserSettings 서브클래스가 ini 자동영속·로컬·전역접근을 한 번에 충족.
② (참고) Lyra/UE표준: Lyra=SettingsLocal(UGameUserSettings)+SettingsShared(SaveGame/계정). 계정 시스템 없음 → Local 단일 서브클래스만(의도적 단순화).
③ 프로젝트 정합: 향후 감도/FOV/오디오 설정의 공통 토대. UDeveloperSettings(GameFlowSettings)와 역할 분리.

[C++ 산출물]
1. UFPSRGameUserSettings : UGameUserSettings — float CrosshairScale(UPROPERTY Config, Clamp 0.5~2.5, 기본 1.0) + GetCrosshairScale/SetCrosshairScale(Set 시 ApplySettings/SaveSettings) + OnCrosshairSettingsChanged 동적 멀티캐스트 델리게이트(BlueprintAssignable).
2. DefaultEngine.ini에 [/Script/Engine.Engine] GameUserSettingsClassName=/Script/FPSRoguelite.FPSRGameUserSettings 등록(메모리: config는 ini 직접 편집, 라이브 세션 재시작 필요).

[콘텐츠 산출물]
- WBP_BasicCrosshair: Event Construct에서 GetGameUserSettings→Cast→GetCrosshairScale 읽어 CrosshairRoot RenderTransform Scale 적용 + OnCrosshairSettingsChanged 구독(슬라이더 실시간 반영).
- WBP_Settings(신규): 크기 슬라이더(0.5~2.5) → SetCrosshairScale(Apply+Broadcast). CommonActivatableWidget로 만들어 레이어 스택 push/pop(P6-A 메뉴 패턴 재사용).
- 메인 메뉴(WBP_MainMenu): Settings 버튼(WBP_PlayButton/QuitButton 서브클래스 패턴 재사용) → WBP_Settings push, Back으로 pop.

[함정/주의]
- 에셋 경로 C++ 하드코딩 금지. UGameUserSettings 서브클래스는 클래스 등록만 ini, 값은 자동 직렬화.
- 메뉴/HUD 양쪽에서 같은 설정값 읽음 — GetGameUserSettings 싱글톤이라 동기화 자동. 델리게이트는 살아있는 위젯만 구독(HUD는 런 중에만 존재).
- VibeUE 한계: 위젯바인딩 getter는 컴파일 후([[vibeue-mcp-capabilities]]). CommonButton은 서브클래스화 우회.
- 범위 한정: 이번엔 크기만. 색/두께/불투명도·감도/FOV·인게임 일시정지 메뉴는 후속(같은 패턴 확장). 일시정지 메뉴 진입은 U17 범위 밖(사용자: 메인 메뉴 진입 확정).

[검증] 빌드+스모크 → 사용자 PIE: 메인메뉴 Settings→슬라이더로 크로스헤어 크기 실시간 변경→런 진입 후에도 적용→재시작(에디터 재시작) 후 유지→기본값 무회귀 → codex-review.ps1 -Base main → PROGRESS 갱신+✅+사용자 콘텐츠 동반 커밋 질문+--no-ff 머지.
```

---

## §D 문서 정합 메모 (후속 정리 대상 — 발견: 2026-06-13 전수 분석)

| # | 이슈 | 위치 | 내용 |
|---|---|---|---|
| 1 | **"P7" 명칭 충돌** | `Docs/SSOT/Roadmap.md` §7-3 vs `Docs/P7-MultiplayerLoop_Plan.md` | Roadmap의 P7=폴리시/빌드(U14), 플랜 문서의 P7=멀티 루프(U11). 멀티 루프는 Roadmap상 P5 행(4인 협동+세션) 영역. 본 문서는 유닛 번호(U11/U14)로 구분 — Roadmap 개정 시 명칭 정리 권장 |
| 2 | ~~**FF 10% stale**~~ ✅해소(2026-07-01) | `Docs/SSOT/Roadmap.md` §7-3 P5 행 · `Docs/SSOT/Enemy.md` §2-10 | 확정값=50%(`FriendlyFireDamageScale=0.5f`). **기본 ON 설계확정 2026-07-01**(코어 협동, Concept §1-C-6; ⚠️코드 `bFriendlyFireEnabled` 현재 false→ON 전환 후속). 10% 인용 금지 |
| 3 | **Fragment 훅 stale** | `PROGRESS.md` "Fragment 후속(미완)" 구절 | `ModifyChargeTime`/`OnProjectileSpawn` 훅은 A3a/A3b에서 구현 완료 — U6에서 재구현 금지 |
| 4 | **origin 미푸시/.uproject 경고 해소** | `PROGRESS.md` P5·P6-A 절 | 실측(2026-06-13): origin 동기화 완료, `.uproject` VibeUE Optional 커밋(`15d4e34`) 완료 — 경고 stale |
| 5 | ~~**메뉴↔로비 설계 공백**~~ ✅해소(2026-06-16) | P6-A 셸 vs P7 플랜 | 사용자 확정: **Play→항상 로비 허브**(솔로도 1인 로비 경유), 결과창 복귀처=로비, 세션=Steam(480)만. U11a 프롬프트에 내장 |

---

## §E ConsultLoop 결과 인입 (`Docs/Review/` → 백로그)

> **이 프롬프트 매니저는 `Docs/Review/`의 컨설팅 리포트를 읽어** 각 리포트의 `📌 액션 아이템`을 아래 인입 표로 받고, 대상 유닛(§C)·백로그(§B)에 반영한다. 프로토콜=[`Docs/ConsultLoop.md`](ConsultLoop.md), 채널 설명=`Docs/SSOT/Workflow.md` §10.
>
> **인입 규칙**:
> 1. 새 컨설팅 리포트가 `Docs/Review/`에 생기면 그 `📌 액션 아이템`·`🙋 사용자 결정 필요`를 아래 표에 한 줄씩 등재(리포트 링크 + 대상 유닛 + 상태).
> 2. **자문 전용** — 등재만으로 코드/에셋을 바꾸지 않는다. 채택 시 해당 `Docs/SSOT/*.md`를 먼저 갱신(원칙3) 후 대상 유닛 프롬프트(§C)에 반영.
> 3. 유닛에 흡수되면 상태를 `→Uxx 반영`으로, 폐기 시 `보류/기각(사유)`으로 갱신.

| # | 출처 리포트 | 액션/결정 | 종류 | 대상 유닛 | 상태 |
|---|---|---|---|---|---|
| C1 | [Review/20260616-lmg-spinup-feel.md](Review/20260616-lmg-spinup-feel.md) | `UFPSRWeaponFireComponent::GetSpinupAlpha()` BlueprintPure 노출(사운드/크로스헤어/애님 단일 콘텐츠 훅) | 코드(소) | U13 또는 U15 합류 | 신규 |
| C2 | 〃 | 발사 루프 사운드 피치/볼륨 램프 + spin-down/brake 사운드 3종(탄소진/재장전/교체) | 콘텐츠 | U13(오디오) | 신규 |
| C3 | 〃 | 크로스헤어 스핀업 미세 피드백(V3 재사용, Alpha→gap/두께, 실제 Bloom과 일치) | 콘텐츠/UI | U12(UI·필 후속) | 신규 |
| C4 | 〃 | DA 곡선 단계 노출(`SpinupAudioPitch/Volume`, `SpinupCrosshair*`, `SpinupBarrelSpin*`, 선택 `SpinupRecoil/BloomScaleCurve`) — 부담 시 Alpha 노출만으로 1차 충족 | 코드/콘텐츠 | U13/U15 | 신규 |
| C5 | 〃 | **스핀업 곡선 튜닝값 확정**: Start=최대 FireRate 50~65%, RampTime 0.8~1.2s, 최고속 5~7s+ 지속(RampTime ≤ 탄창소모시간 20~25%) | 🙋사용자결정(DA) | U16 후속 튜닝 | 사용자 |
| C6 | 〃 | **사망 경로는 U2에서 해소** — `HandleOutOfHealth`가 `StopFiring()`+`CancelAllAbilities()` 호출(서버권위). **남은 것 = U9 DBNO/부활**: 다운→부활 전이에서도 발사상태·스핀업 리셋 보장(부활 시 stale 케이던스 방지) | 코드(U9 게이트) | U9(DBNO) | ⚠️체크리스트 |
| D1 | [Review/20260616-volumeup-design.md](Review/20260616-volumeup-design.md) | 재미 게이트 §7-5를 **G1(손맛/페이싱/성능, 양산관문)+G2(빌드다양성/시너지)** 2분할 기재 | 문서(SSOT) | Roadmap.md §7-5 | ✅반영(2026-06-16) |
| D2 | 〃 | **EnemyDefinition DataAsset + 시간 가중 로스터 디렉터** 골격(종류/속도/공격타입/HP커브/드랍 + 시간대별 가중풀). 기존 B1(원거리 적) 흡수 | 코드(골격) | 신규 U(적시스템) | ⏸️보스/로비 후(결정1=B) |
| D3 | 〃 | **경량 적 상태이상 서브시스템** C++ 베이스(배치틱/풀리셋/연쇄캡/Significance 피드백). 적500 예산 건드림=HIGH_RISK. Enemy/Combat/Performance SSOT 선갱신 | 코드(골격) | 신규 U(상태계) | ⏸️G1 후·🙋결정대기 |
| D4 | 〃 | **프리즈 하이브리드 최소 프로토**(일반 수치카드=Q/E 비동기, 무게큰선택=프리즈). G1 *전* 선행. RunFlow §2-2 선갱신(설계변경) | 코드+문서 | 신규 U(프리즈) | ⏸️G1 직전·🙋결정대기 |
| D5 | 〃 | **G1 게이트 계측 기준 고정 + 실행**(FPS/적수/프리즈빈도/선택소요시간/이탈감 메모). G1 항목=손맛/페이싱/사각/성능(§7-5) | 검증 마일스톤 | 보스/로비 후 | ⏸️현 로드맵 후(결정1=B) |
| D6 | 〃 | **빌드 시너지 축 정의 패스**(상태이상 4종 + Fragment 물림 매트릭스) — §2-3 자기경고 해소. Combat §2-3 갱신. D3 의존 | 문서/설계 | Combat §2-3 | ⏸️D3 의존 |
| D7 | 〃 | 맵 선택/정의 데이터 골격 + 메타 SaveManager/보스정의 골격 — **현 로드맵 U3/U4/U11a(보스·로비 스파인)와 정합**(결정1=B로 사실상 선행 진행 중) | 코드(골격) | U3/U4/U11a | 진행정합 |
| E1 | [Review/20260629-build-structures.md](Review/20260629-build-structures.md) | **건설 구조물 시스템 = 개념 채택**(사용자 2026-06-29). ⚠️**아이디어 풀로만 보관 — 정식 로드맵(§C 유닛)·SSOT 설계문서 미등재**(사용자 지침). 코어 잠금(U1✅) 후 정식화 검토 | 💡아이디어 | (미등재) | 💡아이디어 풀(파킹) |
| E2 | 〃 | **MVP = 센트리 포탑 + 메디컬 비콘 2종**(비파괴/비차단/충전·시간제) + 미션보상 grant(`EFPSROfferType::StructureGrant`) + 고정슬롯 설치RPC + 폴리모픽 효과. ⭐비차단=적 타겟셋/플로우필드 제외(U7 의존 없음). 미션프레임워크+카드v2 패턴 재사용 | 💡아이디어 | (미등재) | 💡아이디어 풀(파킹) |
| E3 | 〃 | ❌ **XP 타워/리파이너 기각**(사용자 2026-06-29 "XP 타워도 빼자", 밸런스 리스크) / ❌ **바리케이드(차단형) 기각**(사용자 "바리케이트 빼고") | — | — | 🗑️기각 |
