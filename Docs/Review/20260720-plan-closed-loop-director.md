# 플랜 컨설트: 폐루프 "이야기꾼" 디렉터 (2026-07-20)

> `/plan-consult` (PlanConsultLoop v1, FULL). 백엔드(Claude) 초안 → Codex 제2아키텍트/레드팀 3R 적대 수렴.
> **자문 전용** — 코드/에셋 미변경. 채택 시 해당 `Docs/SSOT/` 갱신 후 별도 승인 단계에서 구현(`/pm` 인입 후보).
> 원시 Codex 응답: `Docs/Review/_raw/20260720-14{0943,1338,1854}-closed-loop-director*.md`

---

## 1. Intake
- **Mode: FULL** — 하드트리거 다수: 서버권위/복제 관여 · 대량 적 성능경로(스폰/디렉터 tick) 변경 · phase 경계 흔드는 구조 변경 · 결정축 3개+.
- **plan_type: mixed** — primary=`backend/system`(제2아키텍트/레드팀 렌즈, 끝까지 고정), secondary=`player-facing/content`(체감/연출, R2·R3에서 강제 1회+).
- **라운드 정책: Deep Delta-Gated** — 되돌리기 비용 큼·장기영향·실패모드 다양. 3R 전부 유의미 delta(NO_DELTA 0), divergence 축 8개 소진 후 수렴.

## 2. 결과 플랜 (수렴)

### 핵심 명제 (변경됨)
기존 개루프 디렉터를 **교체하지 않고** 폐루프 계층을 필터로 얹는다. **단, 폐루프의 본체(척추)는 "적 수 조절"이 아니라 "언제·어디로·누구에게 어떤 위협 비트를 던지고 예고하느냐"다.** 밀도 승수는 가장 싼 actuator(보조)일 뿐. (초안은 밀도를 본체로 봤음 → R2·R3에서 뒤집힘.)

### 계층 (수렴 후)
- **감지 — PartyStress/압력 원장 (서버, 0.5~1s 갱신)**: 플레이어별 스트레스 스칼라 = **enemy/boss 데미지만**(FF·자해·낙하·미션피해 제외 = 조작 방지) + 다운 + 저체력. 감쇠 있음, 교전 중 감쇠 정지. **근처 적사망/킬은 pain 아닌 engagement 신호**(감쇠정지·파워추정용, FSM 상승입력 금지). 집계 = **전역 1벌 + per-Front 압력 원장**(LocalStressEWMA·PlayerCount·RecentDowned·LastBeatTime·QuotaWeight, **상태 승계 없음·매틱 재계산**). Front별 FSM 금지(병합/분리 승계=폭탄).
- **완급 — 경량 상태 모델 (서버, 디렉터 unpaused-delta로만 갱신)**: `StressEWMA → IntensityMult` + peak 후 쿨다운 + **최소 휴식 하드플로어**. named 상태(Build/Peak/Ease)는 **텔레그래프·이벤트reserve·로그를 동시 구동할 때만** 유지(출력이 target 하나뿐이면 상태명은 가짜복잡도 → 컷). 휴식=적0 아닌 **트리클 바닥**. FSM 승수는 **physical target + front reserve + 이벤트 게이트에 동시 적용**(target만 낮추면 front reserve가 Relax를 거짓말로 만듦).
- **위협 비트 스케줄러 (척추, 서버)**: `{FrontId, BeatTag, TargetPlayer/Front, Direction/Anchor, LeadTime, StartTime, Duration, SequenceId}`. F4 위협비트 하한(1분후 30~45s·5분후 15~25s) 구동. 고립자엔 **F5 IsolationScore로 스폰편향+타겟편향**(스폰 위치=기존 공간계, "얼마나"=전역 완급). 실제 게임플레이는 서버 Spawn/AI만 실행.
- **밀도 승수 (보조 actuator)**: `UpdateSpawnIntensity()` **한 곳**에서 승수 적용(되돌리기 최소). 기존 `ComputeTargetAliveCount()`(레벨/시간 곡선)이 base, 폐루프가 ×승수. 연속 진폭(적 스탯) 조절 없음.
- **미션 게이트**: `DueButGated` 상태 신설 — 시간 도달해도 FSM 게이트 off면 **fired 처리 금지**(실제 스폰 성공 시점에만 소모). 보스 근접 처리 정책·최대 지연 = 튜닝(사용자 결정 §4).
- **연출 (player-facing, placeholder만)**: **딱 1개** = 방향성 위협 예고 pulse/debug banner("3초 뒤 우측/후방/타겟 압박"). 경로 = 서버 결정 → 서버가 gameplay 실행 → **얇은 Client RPC/notification** → 클라가 U8 `UFPSRGameplayMessageSubsystem` 로컬 pub/sub로 fan-out → HUD/오디오/디버그 소비. **U8은 순수 로컬·복제0·게임로직 금지**이므로 서버가 U8로 브로드 금지; 버스엔 **예고 연출 요청(신규 소형 payload `FFPSRThreatCueMessage`)만**. 클라가 이 메시지를 못 받아도 스폰/데미지/AI/stress/FSM 불변.

### 단계 분해 (수렴)
| P | 내용 | 게이트 |
|---|---|---|
| **P1 (척추)** | 서버 폐루프 코어: PartyStress(MAX·enemy/boss만) + 전역 완급모델 + per-Front 압력원장 + **위협비트 스케줄러** + F5 스폰편향 배선 + DueButGated + 프리즈 unpaused-delta 게이팅 + 밀도승수(보조, 1곳) + **연출 1개(placeholder 방향예고)** + CSV 계약검증(5시나리오) | 빌드·헤드리스 스모크·PIE 2-client·적500 perf·5시나리오 계약준수 |
| **P2 (체감/연출)** | 위협 게이지(임박도만) + 고립경고 HUD+구조핑 + Relax 음향 + placeholder→실아트/SFX + 비트 라이브러리 확장 | PIE 체감 검증 |
| **P3 (진폭·선택 Fork A)** | `UEnemyScalingProfile` 구현, **GlobalAliveCap 도달 후** 레버, 가시화. **SSOT §2-6 재정의 선행** | — |
| **P4 (성격·선택 Fork B)** | `UFPSRStorytellerProfile` DataAsset(완급 임계/휴식길이/비트가중/변동성), 호스트 옵션. 랜디식 무보정=옵션만·기본 금지 | — |

### 범위 밖 (MVP 제외 — R1/R3 판정)
- **크레딧 경제(초안 계층C) 제거** — 이 리포엔 이미 `TargetAliveCount→FrontId allocator→GlobalAliveCap/SeedReserve/FrontReserve/rear-drain` 예산계가 있어 3번째 "난이도 언어"는 과설계. 정예/스페셜 구성 스케일은 **기존 폴리모픽 `UFPSREnemySpawnRule` 확장**(`FFPSREnemySpawnContext`에 이미 RunClock/PartyLevel 전달됨)으로 해결. 필요 시 **이벤트 전용 소형 예산**으로만 후속.
- **PartyPower/DPS 기반 스탯 판단** — 집계 레이어 없고 `GlobalDamageMultiplier` 하나로 DPS 대표 불가. P1 금지.
- **적 진폭·성격 프로파일** — P3/P4로 이연(선택).

## 3. 수렴 로그 (초안 → 무엇이 바뀌었나)

**R1 (레드팀 — 과설계·실행결함·SSOT):** 초안의 계층 C(크레딧경제)·E(진폭)·성격·PartyPower를 **MVP에서 전부 제거**(PLAN_DELTA). 실행결함 다수 적발·수용: 미션창 소실(→DueButGated), 보스 제외 표현 오해(→FSM은 이벤트/미션 전이만 제외, 보스 중 스웜/램프 무회귀), 프리즈 중 상태 누수(→unpaused-delta만), FrontReserve가 Relax 무력화(→승수를 reserve/게이트에도), **stress FF/자해 오염**(→enemy/boss만), **근처 적사망=engagement≠pain**, 크레딧 API 부정합·freeze burst debt. FALSIFIABLE 5시나리오+서버 CSV 계약검증 신설.

**R2 (발산 — FSM전제/체감/FrontId):** ①4상태 FSM도 과설계 후보 — 상태가 출력 1개(target)만 바꾸면 컷, 텔레그래프/이벤트reserve/로그 구동 시만 유지. ②**핵심 재구성**: 밀도 변화만으론 뱀서 200적 화면에서 invisible → **"적 수 조절=invisible, 위협 비트(방향·타겟·타이밍)=player-facing"**. 진폭0이어도 방향/예고/타겟/타이밍으로 체감 생성. ③**Codex 자기 R1권고(Front별 FSM) 반박** → 병합/분리 승계=폭탄(max오염/avg지움/reset익스플로잇) → **전역 FSM 1벌 + per-Front 압력 원장(승계 없음)**.

**R3 (스코프 모순 해소·경계):** R1("승수만")↔R2("연출 필요") 충돌 판정 → **P1 척추=위협비트 스케줄러+예고, 밀도승수=보조**. P1/P2 명시 라인 확정(위 표). 연출 0=죽은기능 함정, 통째 연출=과스코프 → **placeholder 예고 1개**가 정답. U8 버스는 재사용하되 **로컬 fan-out 인프라만**(서버 브로드 금지, 게임로직 금지); 신규 `FFPSRThreatCueMessage`. 연출=placeholder(디버그 pulse/beep), 실아트/음악=P2로 격리(콘텐츠 늪 방지).

**기각/보류:** 없음(초안의 과한 계층은 기각이 아니라 **후속 P로 이연**). 크레딧 경제만 "필요 실증 전 제거".

## 4. 미해결 쟁점 · 사용자 결정 필요

### 결정 A — 적 진폭(스탯) 조절을 폐루프 레버로 쓸 것인가 (Fork A)
- **경위**: 초안이 진폭을 계층E로 제안 → R1이 "제1원리(적 싸게)+DDA 신뢰리스크"로 MVP 제외 → P3 선택으로 이연.
- **백엔드 이유**: 캡(200) 도달 후엔 수로 못 늘리니 진폭이 유일 레버. **가시화(뱀서식 히트게이지)면 신뢰리스크 회피 가능**.
- **Codex 이유**: 진폭 DDA는 숨기면 배신감·샌드백 악용. 기본 폐루프 검증 전엔 넣지 말 것.
- **사용자 결정**: ① 진폭을 폐루프 레버로 **쓸지(P3)** 여부, ② 쓰면 **가시화(권장) vs 숨김**, ③ SSOT §2-6은 "시간 HP/공격력 커브"로 예약 — 폐루프(파워/캡 연동)로 재정의할지 = **채택 전 SSOT 수정 필요**. 기준: "게임이 우릴 본다" 체감을 밀도·비트만으로 충분히 주면 진폭은 불필요, 부족하면 가시화 진폭 추가.

### 결정 B — 순수 적응형 하나 vs 성격 선택제 (Fork B)
- **경위**: 초안이 카산드라/피비/랜디식 프로파일 제안 → R1 "밸런스 표면적·운영비, 기본 폐루프 검증 전 과설계" → P4 선택으로 이연.
- **백엔드 이유**: 값만 다른 DataAsset이라 싸게 만듦, 호스트 난이도-flavor 선택 제공.
- **Codex 이유**: 폐루프 검증 전 옵션도 운영비. 랜디식 무보정 랜덤은 4인 와이프 리스크.
- **사용자 결정**: 성격 프로파일을 **아예 안 만들지 vs 폐루프 검증 후 옵션으로**. 기준: 취향·리플레이성 vs 유지보수. (기본=순수 적응형, 성격=후속 옵션·랜디는 기본 금지 권장.)

### 결정 C — 미션창 게이트 정책 (튜닝)
- **경위**: R1 DueButGated 신설 시 파생.
- **쟁점**: FSM 게이트로 미션창을 지연할 때 **최대 지연 시간** + **보스 근접 시 처리**(강제 발화? 명시 skip?). 미해결(양측 "정책·튜닝").
- **사용자 결정**: 지연 상한 값 + 보스 임박 미션 정책. 기준: 미션 밀도 페이싱 vs 스케줄 예측성.

## 5. 검증 상태
- **확인됨 (코드/소스 관찰)**: 기존 개루프 디렉터·스폰·미션·프리즈·FrontId allocator·U8 버스·플레이어 신호 존재 = 4-에이전트 코드조사 + SSOT 직접인용(§2-6/§2-8/§2-12). 레퍼런스 8종 메커니즘 = 워크플로 조사+적대검증(원문 대조).
- **추정 (선례·추론)**: "밀도만으론 invisible / 위협비트가 player-facing" = 레퍼런스 선례(L4D 페이싱·뱀서 웨이브UI 정상) 기반 추론, **PIE 미검증**. per-Front FSM 승계 폭탄 = 구조 추론.
- **검증 필요 (자문전용, 빌드/PIE 미실행)**: 이 플랜은 토론 산출이며 빌드·테스트로 미검증. **P1 착수 = 교체 전 perf 캡처**(U21 정성판정만이라 기준선 없음).
- **반증가능 예측**: 이 플랜이 맞다면 — 5고정시나리오에서 (휴식 하드플로어 위반 0·cap초과 0·gated미션 소실 0·freeze중 상태진행 0·split-front starvation 없음)이 관측되고, placeholder 예고가 켜지면 플레이테스터가 "방향 압박"을 인지한다. **틀렸다면** — 첫 P1 PIE에서 (a) 밀도승수+예고를 켜도 "그냥 스폰 랜덤"으로 읽히거나(→체감채널 부족, P2 연출 조기 필요), (b) 분산파티에서 per-Front 원장만으론 고립전선 방치가 재현(→per-Front FSM 불가피)되는 신호가 첫 체크포인트에서 나온다.

## 📌 액션 아이템 (PM 인입 후보 — 실행은 별도 승인)
- **구현 전 SSOT 갱신 대상**: `RunFlow §2-8`(폐루프 디렉터·위협비트·DueButGated) · `Enemy §2-6`(ScalingProfile 재정의·IsolationScore/위협비트 배선·stress 입력규약) · `Enemy §2-12`(난이도 압박=폐루프 컨트롤러) · `PlayerFeel §2-14`(ThreatCue 코스메틱·U8 경계).
- **TaskPrompts_Master 인입 후보**: 유닛 "폐루프 이야기꾼 디렉터" (P1 척추 → P2 연출 → P3 진폭[Fork A] → P4 성격[Fork B]). 의존: 기존 디렉터/스폰/U8/미션 인프라(전부 존재).
- **사용자 결정 3건(A/B/C)** 확정 후 P1 실행 프롬프트 편성.

---

## 7. 목표 기반 재분해 (사용자 목표 확정 · 2026-07-20 후속)

> 위 1~6은 아키텍처 컨설트. 이 §7은 사용자가 낸 **구체 기능 목표 3개**를 서브목표로 쪼개고 코드 검증(3-probe)한 뒤 재구성한 실행 플랜. 컨설트 결론(위)은 P5+의 완급/연출 계층으로 계승.

### 7-1. 코드 검증 결과 (probe 3종, file:line)
- **목표1 공간선택**: 스폰포인트 선택 = **균등 랜덤**. MIN 거리게이트만 있고 **MAX 없음**(`PassesCommonSpawnGates:782-795`). 거리가중·근접폴백·시야게이트는 **제거됨**(2026-06-24/25/29, 사유="단일 포인트가 스폰 굶김" `:1219-1222`) → 재도입 시 **넓은 적격집합 유지 소프트가중**만 허용(하드게이트 금지 = 회귀규약). `IsolationScore`=**코드 0, 문서만**. behind/flank=없음.
- **목표2 신호/구성**: 신호 훅 전부 확인 — 레벨/시간=무료(파티단위), 현재다운=무료; **명중률·초당킬·다운횟수·이동/정지=신규 카운터**(훅: `NotifyFire`+`ApplyDamage`/`HandleDeath(Killer)`/`HandleOutOfHealth:854`/이동패스 `:298-327`). **per-player 스탯 집계객체 없음**(신설). 아키타입=근/원거리 2종만. 로스터 룰 인프라(`GetWeight`)는 준비됐으나 컨텍스트=RunClock+PartyLevel뿐. **스폰포인트 per-biome 로스터 없음**(전역 1개).
- **목표3 미션**: 배치=태그+가중+MIN, **MAX/도달밴드 없음**(폴백이 최원거리=정반대). 트리거=**시간창만**, 행동트리거 없음. 플로우필드 도달가능성 쿼리는 재사용 가능.

### 7-2. 사용자 결정 (검토 답변 2026-07-20)
- **A 공중=재정의**: 비행 아님 → **지상처럼 이동 + 타격점만 상방 + 플로팅 비주얼**(히트프록시 오프셋). **신규 이동코드 불필요=경량**. **방패=방향성 아머**(정면 감쇠·불파괴 / 측후 무방비) = 신규 메커니즘이나 범위 명확.
- **B 캠핑**: 처벌 아님. **비미션 캠핑 = 지형 사기/난이도 낮음의 증거 → 긴장 위해 난이도↑ 신호**. 미션 홀드는 예외.
- **C**: 고립 압박 **상한 필요**(넛지, "합류가 이득").
- **D**: 난이도 판단 = **복합 신호**(단일 금지, 무기특성 정규화).
- **E**: **per-player + 파티를 각각 다른 걸 측정 → 종합 판단**(디렉터=Front별 정합).
- **F 초크**: 도달불가=**버그(고칠 것)**. 좁은 초크 사수=지형이점 → **구성으로 대응**(원거리↑=기존 즉시 / 방패·공중=강화). 공중은 지상이동이라 초크 지형우회 아님 → 카운터는 원거리(초크 밖 사격)+방패(정면탱킹)+공중(상방조준 강제).

### 7-3. 재조정 단계 (목표 접합)
| P | 내용 | 서브목표 |
|---|---|---|
| **P0 센서(토대)** | per-player 집계(명중/킬/다운수/이동·정지) + 파티(레벨/시간) → **복합 난이도지표**(단일 금지) · IsolationScore 구현 · **캠핑판정(비미션·미션홀드 예외)** · 플로우필드 초크/지형이점 쿼리(도달불가=버그) · 서버권위·Front인지 | 1.1·1.3·2.a·2.c-3·3.2 입력 |
| **P1 공간선택(목표1)** | MAX거리 게이트 + 고립 소프트가중(**회귀규약**) + **압박 상한** | 1.2·1.4 |
| **P2 양·구성(목표2 뼈대)** | 밀도승수 + 로스터 컨텍스트 확장 + 상황룰 + 스폰포인트 per-biome 로스터 + **캠핑/초크→원거리↑(기존)·방패/공중↑(있으면)** | 2.a→b·2.c-1·2.c-2 |
| **P3 미션(목표3)** | 미션 도달밴드(플로우필드) + 행동트리거(복합) + 캠핑예외 | 3.1·3.2·3.3 |
| **P4 아키타입(경량화)** | **공중=엘리베이티드 히트박스**(비행코드 아님) · **방패=방향성 아머** + 콘텐츠 | 2.c-4·2.c-5 |
| **P5+** | 완급 FSM + 위협비트 예고 연출 + 진폭 + 성격 (§2~§4 컨설트 결론) | — |

**핵심**: P0~P3만으로 사용자 목표 1·2·3 뼈대 성립. P4(공중/방패)=경량화되어 P2 구성에 편입. P5=그 위 완급/연출.

### 7-4. P4 통합 감사 결과 (Track 2 워크트리 백그라운드 완료 · 2026-07-20)
전 데미지 5경로(hitscan·투사체 직격·투사체 AOE·근접·차지레이저)가 단일 `FPSRCombat::ResolveDamage`→`ApplyDamage`로 수렴, `Hit.GetActor()`로 라우팅 — 이게 양쪽 실현성을 결정. **양쪽 🟢 GREEN, 진짜 BLOCKING 없음.**

**공중(엘리베이티드 히트박스) — 🟢 이동코드 0.** 전 이동/AI/플로우필드/넷컬이 **액터 루트(지상)** 참조 → 루트만 지상이면 이동코드 불필요(확인: 플로우 FootZ=루트, 근접 VertGap≈0 정상발동, 넷relevancy=루트). **함정①(높음)**: 시각메시가 `NoCollision`(`FPSREnemyBase.cpp:47`) → "메시만 올리면" 시각위치 피격 불가. **정답 = 올린 콜라이더 필수**, 최적 매개체 = `UFPSRWeakpointComponent`(mult=1.0, 이미 4정밀경로 배선·`Max(1.0)` 클램프=순수 히트프록시). BP로 메시 오프셋+동높이 Weakpoint → 즉시 동작(DedupePawnHitsByActor로 더블뎀 없음). **함정②(중)**: 지상캡슐 잔존 `ECC_Pawn`→발밑 히트박스("위 조준" 미강제) / AOE는 weakpoint 제외→떠있는 몸통 스플래시 안됨. 엄격강제+AOE일관 원하면 **소규모 채널 분리 1건**(올린 콜라이더=ECC_Pawn, 지상캡슐=새 몸통채널; 파급 3곳 국한).

**방패(방향아머) — 🟢 단일지점 삽입.** 진짜 초크포인트 `ResolveDamage(...Origin)`에 **Origin/Instigator/Target 전부 존재** → 배선0으로 bearing=`(Origin-TargetLoc).Normal2D · TargetForward`. 5경로 자동커버(hitscan=사수/투사체=임팩트/근접=플레이어/AOE=폭발중심 방향). 적이 타깃 향해 회전→forward 신뢰. 순서 자동정확(weakpoint/크리 다음). 삽입=`FPSRCombatStatics.cpp:102-105` swarm분기 1곳, **신규 `UFPSRShieldComponent`**(weakpoint 패턴, subclass 불필요→타 아키타입과 합성가능). **⚠️ 유일 최다버그(BLOCKING급)**: 정면을 **하드블록(0뎀)으로 하면 hitscan이 관통 통과**(`ApplyToTarget` Resolved≤0=false → pierce 미소비 → 뒤 적 명중) + `DamageDealt=0`→마커/라이프스틸/킬크레딧 침묵. **정답 = 정면 90~95% 강DR(잔여>0)** → 총알정지·마커일관·사실상 무적. 콘텐츠규약: 정면아크에 weakpoint 금지(상쇄). AOE near-zero bearing 가드(=정면아님·풀뎀).

→ **결론: P4 두 아키타입 실현성 확정.** 공중=BP 오프셋+Weakpoint(mult1.0), 방패=ResolveDamage 1지점+ShieldComponent+강DR(하드블록 금지). 상세=`_raw` Track2 리포트(감사에이전트 산출).

---

## 8. P0 센서 상세 설계 (Codex 2R 수렴 · 2026-07-20)

> Track 1: P0(센서/토대) 세밀화. Codex R1·R2 수렴. 원시응답 `_raw/20260720-15{2146,2620}-p0-sensor-r*.md`.

### 8-1. 대원칙 = 계측/판정 분리 (계약)
**P0 = 서버권위 런 계측 레이어.** 원시이벤트·롤링카운터·통계변환(rate/EWMA/clip/deadband/z-score)·confidence·lifecycle **만** 소유. **P0는 "고전/압도/캠핑/지형악용/난이도↑↓" 의미판단을 소유하지 않음. P0 출력은 스폰·미션·보상·적스탯·mercy·easing/hardening을 직접 바꾸지 않음.** 난이도 해석·가중·threshold·hysteresis 결정·easing/hardening·choke 대응 = Director(P2+) 소유. → 센서 수정이 난이도 패치가 되는 것을 구조적으로 차단.

### 8-2. 위치·생명주기
신규 **`UFPSRDirectorSensorSubsystem`**(서버전용 UWorldSubsystem). per-player = 서버로컬 `TMap<TWeakObjectPtr<AFPSRPlayerState>, FTelemetry>`. **복제 0**(PlayerState 오염 금지 — CopyProperties/재접속/seamless/RepNotify 리스크). 매 집계 invalid cleanup + StartRun/EndRun/Logout 명시 정리. **재접속=새 baseline**(히스토리 승계 안 함). PlayerState는 읽는 원천만(LifeState/DBNO·ASC health·CurrentMapId).

### 8-3. P0a 출력 (중립명 — Score 금지)
`IncomingDamageRateZ`(enemy/boss만) · `DownedRecent01` · `LowHealthSecondsRate` · `HealthPct` · `KillRateClippedZ`(clip+저가중, last-hit 오염 대비) · `AccuracyRate`(confidence 보조) · `EnemyDamageDealtRate` · `MovementConfinement01`/`bMovementConfined` · `FeatureConfidence`(샘플수) · `FrontId`(점유 스냅샷) · `MissionServing`(미션 API). **금지명**: StressLike/PowerLike/TerrainAdvantageLike/CampingScore/ShouldMercy/ShouldHarden(=판정). z-score는 통계라 허용, 단 "강함" 함의 합성 금지.

### 8-4. 이동 confinement (고정크기 — 자라는 구조 금지)
저장(per-player 고정): `AnchorXY·AnchorStartTime·LastSampleXY/Time·MaxDistFromAnchor·bConfined·ConfinedSince`. 튜닝: Sample 0.5~1s / Deadband 75~100cm / ConfinementR 600~800cm / ReleaseR=×1.25 / Window 20~30s. deadband 무시 → 반경 내 Window 이상 체류=confined → 반경 이탈=앵커/타이머/max reset → confined 시 ReleaseR로 해제(히스테리시스), **XY only**. **미저장**: 방문셀 set·위치히스토리·heatmap·flow cell span·지형score·"캠핑" 최종판정. (큰원/광역순환 회피=P0 문제 아님, P2 Terrain 영역.)

### 8-5. 캠핑 예외 = 미션 소유 API
`AFPSRMissionActor::IsPlayerServingMission(PS, loc, view) const` 가상 + 미션별 override(HoldZone=활성 zone내 / MovingZone=현 point zone / StandStill=생존 전원 / CarryNoHit=carrier만 / 그외=기본 미예외). P0는 결과를 `MissionServing` feature로만 실음(의미판단 X). `GameState.ActiveMission`(HUD 계약)로 판정 금지.

### 8-6. Front 버킷
**per-player 히스토리는 플레이어 따라 승계**(문 넘어도) — front-hop 리셋 익스플로잇 방지. 집계 때 현재 점유로 `PS→FrontId` 계산 → 그 순간 features를 Front 버킷 합산 → 다음 집계 재계산(**Front 버킷만** TTL 소멸 = "승계 없음"은 Front 버킷 한정). Front 매핑 = `ComputeOccupancy`를 **read-only 스냅샷 API**로 추출(스폰 allocator 부작용 호출 금지).

### 8-7. 검증 하네스 (액추에이터 前, 반증가능)
- **주입 API**(테스트+콘솔 동일 내부함수): InjectDamage/InjectKill/SetHealthBand/SampleMove/mission-serving/front/StartRun·EndRun·Logout. 같은 입력→같은 snapshot.
- **콘솔**(`#if !UE_BUILD_SHIPPING`): `FPSR.Telemetry.*`.
- **feature당 골든케이스 ≥3**: Accuracy(0/0=neutral+lowconf·10/5=0.5·min-shot미달=lowconf) / KillRate(정상·burst clip·저가중) / Damage(무피격·지속·down동반) / Confinement(정지·deadband미세이동·반경이탈reset) / MissionServing(같은 confinement라도 serving별도) / Isolation(근접·원거리·front승계) / baseline(갱신·StartRun reset·재접속 새baseline) / lifecycle(Logout 정리).
- **P0 게이트 한계**: "고전/압도를 옳게 읽나"는 P0 게이트 아님 → "고전 시나리오서 damage/down/lowhealth/killrate가 기대방향"까지만. Stress로 묶어 액추에이터 연결 = P2 게이트.

### 8-8. 분리 (과설계 컷)
- **P0a**(최소 계측계약): 서브시스템 + 위 per-player windows + freeze-delta 정지 + 파티 스냅샷(레벨/시간/진척/생존/wipe-risk) + Front 스냅샷 + MovementConfinement + 주입 API/골든 CSV. camping 예외는 미션 API 없으면 HoldZone/StandStill만 임시("불완전" 표기).
- **P0b**: IsolationScore · 개인 baseline 정규화 · 전체 mission-serving API · `ChokeProxy`(EWMA+히스테리시스, 구성대응 참고용) · anti-exploit confidence.
- **P0c/P2**: choke 구성대응 · dominance 기반 구성룰 · 완급/연출.

### 8-9. anti-exploit 하드닝 (수렴)
enemy/boss dmg만 · mercy=wipe-risk 결합+상한(저명중 단독 easing 금지) · kill clip+저가중(DamageDealt 선호) · accuracy=압박중저명중 보조만 · 미세이동 deadband · 미션예외=미션소유 API · 고립압박 상한+고립자 고가치보상 금지 · front-hop=per-player 승계.

---

## 9. P0a 구현 계약 (Codex 2R 수렴 · p0-impl-r1/r2)

### 9-1. 3단 슬라이스 (첫 구현 순서)
- **P0a-0 (워킹 스켈레톤 = 배관 불변식 증명)**: 신호 **5개만** — HealthPct · IncomingDamageRate[Enemy/Boss] · DownedRecent01(B3) · MovementConfinement01(C1) · FrontId. + **배관**: `UFPSRDirectorSensorSubsystem` · StartRun/EndRun/Logout cleanup · WeakPtr invalid cleanup · unpaused **결정적 시계**(`Advance`) · 주입 하네스 · `DumpSnapshot` · 프리즈 게이트 · **read-only Front 스냅샷**. 목적 = 생명주기 누수0·프리즈중 미진행·주입→snapshot 결정성·Front 매핑·enemy/boss 게이팅을 골든으로 잠금. **제외**: 파생/정규화/그룹핑 전부.
- **P0a-1 (계산기+훅 신호)**: A1/A4 명중 · A2킬 · A3DPS · B4 · B9/B10 · C2 · D1 · E1/E3/E5 · F1/F2/F5 · G2 · H1/H2 · H4/H6/H7/H8 · H23 · H25/H27 · H28 · H37. **이원 게이트**(9-4).
- **P0a-2 (승격)**: A5 오버킬 · A7 광역 · H33 포위 · H36 스폰품질 · E4 산개 · H9 duration — **고정크기 증명 + 라이브 훅 반증 케이스** 붙은 뒤 편입.

### 9-2. 고정크기 교정 (자라는 구조 금지)
- **Fronts = 현재 active만 재빌드**(TTL 누적 금지), 상한 `min(PlayerCount, MaxFronts)`. **SplitInfo = scalar만**(SplitFrontCount·SplitDurationEwma·LargestFrontPlayerCount; front-pair/player-set/history 금지).
- **H33 = OccupancyMask(uint16)+OccupiedBinCount+SampleCountClamped**(배열/set 금지). **기준축 = velocity/anchor/world**(카메라 yaw 금지 = 마우스 회전 조작 차단).
- **H36 p50/p90 = Welford 불가** → **고정 버킷 히스토그램**만, 아니면 `SpawnToEngageMeanEwma`로 격하(→그럼 P0b). engage = pool-slot 최초 이벤트(첫 DamageDealt>0 / 첫 토큰획득 / 첫 피격 중 **택1 고정**), despawn/timeout = **censored 별도 카운트**, **pool index/actor 필드**(TMap<EnemyWeakPtr> 금지).
- 윈도우: rate(A1~A3·킬)=**구간 EWMA**(hits-ewma/shots-ewma, +0/0 neutral·min-shots lowconf) · D1 개인baseline=**Welford** · **H11 = 동일 interval 파티 스냅샷 z/share**(시간 Welford 아님, ≤4인 bounded) · 카운트=단순.

### 9-3. A5 오버킬 = 신규 훅 계산 (코드 충돌 해소)
`FDamageResult.DamageDealt`는 실제 감소량만 보존, **초과분(overkill)은 설계상 버림**(`FPSRCombatStatics.h:29`, `.cpp:143`). → 센서가 **중앙 `ApplyDamage`에서 `bKilled && bWasEnemy`일 때 `max(0, FinalDamage - DamageDealt)`** 기록(corpse re-hit·door·FF 자동 제외).

### 9-4. 검증 = 이원 게이트 (계산기 유닛 + 훅 PIE)
주입이 값을 직접 꽂으면 **계산기(aggregator)만 검증되고 라이브 훅 계약은 미검증** → 이원화 필수.
- **Unit/headless**: `RecordAcceptedFire`·`RecordEnemyDamageApplied`·`RecordPlayerDamageTaken`·source classifier·EWMA/window/Welford 계산기.
- **PIE/functional**: 실제 발사·더미 적·투사체/AOE·적 투사체·DBNO·FF/자해 경로가 **훅까지 도달**하는지.
- 신호별: **A5·incoming = 중앙 브릿지 시임 좋음**(유닛 강). A1(shots=`NotifyFire`, hits=`ApplyDamage`)·A4·A7 = 계산기 유닛 + **라이브 site coverage는 PIE**("모든 GA가 NotifyFire 정확 호출"·per-path weak 판정·grouping). **A7은 `FireSequenceId`를 `FFPSRFireContext`+`FFPSRProjectileParams`에 전파해야 라이브 그룹핑 검증 가능**(현재 없음).

### 9-5. incoming source = 센서 파생 (브릿지 무변경)
`ResolveDamage`(Instigator/Origin)·`AFPSRCharacter::ApplyContactDamage`(Instigator/DamageType)·Projectile(Team/InstigatorActor) 정보 **이미 존재** → 센서 훅에서 분류: Self(instigator==player)·FF(타 `AFPSRCharacter`)·Enemy(`AFPSREnemyBase`계)·Boss(`AFPSRBossBase`)·Door(door actor)·Mission(mission actor/DamageType tag)·Env(null/tag). **브릿지 시그니처 불변.** 변경은 instigator null+tag 빈 케이스만(Mission/Env/Door 구분 불가) → 그때도 우선 = 기존 `FGameplayTag DamageType`를 source tag로 채움, 최후 = `ApplyContactDamage`에 optional enum(`ResolveDamage`는 절대 불변).

### 9-6. 골든케이스 (추가 확정)
freeze(EWMA 미진행·metric은 wall-clock·burst debt 0·AFK만 H25) · H11(균등 z0·집중 high·dead/DBNO/logout 제외·1인 neutral+lowconf) · H36(즉시 engage·never timeout·release 전·pooled reuse reset·freeze 정지·topology 후·front 미승계) · H33(0적 lowconf·일방향 low·wrap-around·spin 가드·vertical/radius 정책) · A5(exact lethal=0·초과=excess·corpse=0·door/FF 제외·boss 포함정책·clamp) · A7(pellet dedupe 액터1회·explosion N·FF/self/door 제외·pierce 정책) · lifecycle(StartRun reset·EndRun clear·Logout clear·seamless 미승계·reconnect 새baseline·FrontId reuse/TTL 누수0).
