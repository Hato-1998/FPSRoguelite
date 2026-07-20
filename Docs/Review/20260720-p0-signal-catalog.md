# P0 센서 — 측정 신호 후보 카탈로그 (선택 메뉴)

> 폐루프 디렉터 P0 센서가 잴 수 있는 후보 신호 전량. 사용자가 여기서 선택. Claude 시드(A–G) + Codex 증강(H1–H40) 병합 · 2026-07-20.
> **계약**: P0=측정만(중립명·통계변환·confidence·lifecycle). 의미판단/난이도결정=디렉터(P2). enemy/boss 데미지만. 캠핑예외=미션 소유 API. per-player 히스토리=Front 승계.
> 비용 = 무료파생/cheap/new-counter/expensive · 계층 = P0a(최소)/P0b/P0c · 조작 = L/M/H · 티어 = 🟢코어 🔵권장 ⚪선택 ❌컷

## A. 전투 수행 (per-player)
| # | 신호 | 비용 | 계층 | 조작 | 티어 | 활용 |
|---|---|---|---|---|---|---|
| A1 | 명중률(hits/shots) | cheap(2훅) | P0a | M(벽쏘기↓) | 🟢 | 정밀/무기적합; 압박중 저명중=고전 보조 |
| A2 | 초당킬(clip) | cheap(HandleDeath) | P0a | M(막타) | 🟢 | 처치 효율 |
| A3 | 가한 DPS | cheap(DamageDealt) | P0a | L | 🟢 | 화력 |
| A4 | 약점/헤드샷 적중률 | cheap(Weakpoint경로) | P0a | L | 🟢 | 정밀실력→여유 |
| A5 | 오버킬 비율(죽는적 낭비뎀) | new | P0b | L | 🔵 | 과화력=난이도 낮음 신호 |
| A6 | 평균 TTK | new | — | M | ❌ | 적 HP스케일/타겟전환 오염 → A2/A3로 대표 |
| A7 | 다중처치/광역효율 | new | P0b | L | 🔵 | AOE 빌드 파워 |
| A8 | 재장전빈도·압박중 노출 | new(FireComp) | P0a | L | 🟢 | 취약창 |
| A9 | 무기 교체 빈도 | new(EquipSlot) | P0b | L | ⚪ | 상황대응/허둥 |
| A10 | 크리 적중률 | cheap(CombatSet) | — | L | ❌ | 약점/헤드샷과 강상관(별도 크리빌드 전 중복) |
| A11 | 능력/카드 사용 케이던스 | new | — | L | ❌ | 카드 대부분 패시브=신호품질 낮음 |

## B. 생존/상태 (per-player)
| # | 신호 | 비용 | 계층 | 조작 | 티어 | 활용 |
|---|---|---|---|---|---|---|
| B1 | 체력%(현재) | 무료 | P0a | L | 🟢 | 위기 |
| B2 | 저체력 지속시간 | 무료파생 | — | L | ❌ | B1·피격에서 파생 |
| B3 | 다운수+최근다운시각 | cheap | P0a | L | 🟢 | 실패 |
| B4 | 마지막 피격후 경과 | 무료파생 | P0a | L | 🟢 | 회복/안전 |
| B5 | 니어데스(갔다 회복) | new | — | L | ❌ | 체력%·피격·다운서 파생 |
| B6 | 부활 준/받은 수 | cheap(Revive) | P0b | L | 🔵 | 팀 상호작용 |
| B7 | DBNO 체류시간 | cheap | P0b | L | 🔵 | 고통 |
| B8 | 나 공격중 적수(aggro) | cheap(토큰) | — | M | ❌ | H1/H2 토큰으로 대표(단 H11 편중엔 유지) |
| B9 | 근접범위 내 적수 | cheap(spatial hash) | P0a | L | 🟢 | 밀집 압박 |
| B10 | 최근접 적 거리 | new | P0a | L | 🟢 | 임박 위협 |

## C. 공간/이동 (per-player)
| # | 신호 | 비용 | 계층 | 조작 | 티어 | 활용 |
|---|---|---|---|---|---|---|
| C1 | 이동 갇힘도(캠핑, 확정) | cheap(고정크기) | P0a | L(deadband) | 🟢 | 비미션 정체=난이도낮음 신호 |
| C2 | 최근접 아군거리(고립) | cheap | P0a | L | 🟢 | 고립(→IsolationScore P0b) |
| C3 | 파티 중심 거리 | 무료파생 | — | L | ❌ | Front점유+C2로 대표 |
| C4 | 고립 지속시간 | 무료파생 | — | M | ❌ | 대표됨 |
| C5 | 아군 LOS | expensive | — | L | ❌ | 비쌈(트레이스) |
| C6 | 수직/레이어(메자닌) | cheap | P0b | L | ⚪ | 위치 맥락 |
| C7 | 목표방향 진행거리 | cheap | P0c | M | ❌ | 의미판단 가까움 |
| C8 | 백트래킹 감지 | new | P0c | M | ❌ | 의미판단 가까움 |
| C9 | 초크 점유(ChokeProxy) | cheap 저빈도 | P0b | M | 🔵→proxy | 구성대응 참고(easing 핵심입력 금지) |
| C10 | 이동속도/대시 사용 | new | P0b | L | 🔵 | 기동 |
| C11 | 조준 방향/사각 노출 | expensive | — | H | ❌ | 비쌈+조작쉬움+의미판단 |

## D. 진행/빌드 (per-player)
| # | 신호 | 비용 | 계층 | 조작 | 티어 | 활용 |
|---|---|---|---|---|---|---|
| D1 | 개인 화력 baseline | cheap(resolved stats) | P0b | M | 🟢 | 정규화 기준(z-score) |
| D2 | XP 획득율 | cheap(XPPickup) | P0b | L | 🔵 | 성장/픽업 근접성 |
| D3 | 카드픽수/빌드파워 추정 | cheap | P0b | L | 🔵 | 성장 |
| D4 | 리롤 사용 | cheap | P0b | L | 🔵 | (난이도 관련 낮음) |

## E. 파티 집계
| # | 신호 | 비용 | 계층 | 조작 | 티어 | 활용 |
|---|---|---|---|---|---|---|
| E1 | 파티레벨 vs 시간 | 무료 | P0a | L | 🟢 | 진척(기존 곡선) |
| E2 | 파티 평균체력 | 무료파생 | P0b | L | ⚪ | 팀 위기 |
| E3 | 생존수/DBNO수/wipe-risk | 무료 | P0a | L | 🟢 | 팀 위기(mercy 결합) |
| E4 | 산개도(바운딩/분산) | 무료파생 | — | M | ⚪/❌ | Front·C2로 대표 |
| E5 | per-Front/맵 점유 | cheap | P0a | L | 🟢 | 전선별 배분 |
| E6 | 총 파티 DPS 추정 | — | — | — | ❌ | A3 합산 |
| E7 | 집합 피격율 | cheap | P0b | L | ⚪ | 팀 압박 |
| E8 | 마지막 미션/이벤트후 경과 | cheap(timestamp) | P0b | L | 🔵 | 페이싱 |
| E9 | 이번런 미션 성공/실패 이력 | cheap | P0b | M | 🔵 | 난이도 적합 |
| E10 | 보스까지 남은시간 | cheap | P0b | L | 🔵 | 페이싱 |

## F. 교전/페이싱
| # | 신호 | 비용 | 계층 | 조작 | 티어 | 활용 |
|---|---|---|---|---|---|---|
| F1 | 현재 생존 적수 | cheap(디렉터) | P0a | L | 🟢 | 예산 |
| F2 | 최근 처치수(웨이브 소진) | cheap | P0a | L | 🟢 | 진척 |
| F3 | 전투 강도(양방향) | cheap | P0b | L | 🔵 | 열기 |
| F4 | 마지막 비트후 경과 | cheap | — | M | ❌ | 의미판단 냄새→미션/이벤트 timestamp로 |
| F5 | 무교전 시간(소강) | cheap | P0a | L | 🟢 | 소강=밀도↑ 신호 |
| F6 | 교전중 원거리:근거리 비율 | cheap | P0b | L | 🔵 | 위협 종류 |

## G. 환경/맥락
| # | 신호 | 비용 | 계층 | 조작 | 티어 | 활용 |
|---|---|---|---|---|---|---|
| G1 | 플레이어 맵/바이옴 | cheap | P0b | L | ⚪ | (사용자 per-biome 통찰=스폰포인트 측이 주) |
| G2 | 문 개방/맵 확장 상태 | 무료(topology) | P0a | L | 🟢 | 토폴로지 |
| G3 | 활성 스폰존 | cheap | P0b | L | 🔵 | 스폰 맥락 |
| G4 | 지형이점(ChokeProxy) | cheap 저빈도 | P0b | M | ❌→C9 | C9와 동일(proxy) |

## H. Codex 증강 (코드베이스 특유 — 협동/호드/예산)
| # | 신호 | 비용/훅 | 계층 | 조작 | 활용 |
|---|---|---|---|---|---|
| H1 | melee_token_used_ratio/player | cheap:공격패스토큰 | P0a | L | 근접 압력 상한 도달 여부 |
| H2 | ranged_token_held_ratio/player | cheap:held token | P0a | L | 원거리 위협 추가 여지 |
| H3 | attack_token_denial_rate | new | P0b | M(몰기) | 토큰 포화로 지시 무효인지 |
| H4 | enemy_projectile_budget_used_01 | cheap:ProjSubsys | P0a | L | 원거리 적/탄막 여지 |
| H5 | projectile_eviction_count | new | P0b | L | 발사체 캡 체감 누락 |
| H6 | enemy_pool_used_01 / acquire_fail | cheap/new | P0a | L | 스폰 명령 실현가능성 |
| H7 | active_enemy_by_front_count | cheap:FrontId | P0a | L | 전선별 적 배분 |
| H8 | front_budget_used_01 | cheap | P0a | L | 2인+ 전선 고가치 집중 |
| H9 | split_front_count / duration | 무료파생 | P0a | M(장기분산) | 분산 파티 배치 |
| H10 | solo_front_time_rate/player | 무료파생 | P0b | M | 솔로 정찰 vs 그룹 |
| H11 | threat_skew_z/player | cheap:피격/토큰 | P0a | L | 위협 편중 완화 |
| H12 | damage_dealt_share_z/player | cheap | P0a | M(딜중단) | 개인 출력 편차 |
| H13 | kill_credit_share_z/player | cheap | P0a | M | 킬 편중/보상 |
| H14 | revive_distance_to_nearest_ally | cheap:DBNO+위치 | P0a | L | 부활 가능 거리 |
| H15 | revive_attempt/cancel_count | new | P0b | M(취소) | 부활 난도 |
| H16 | revive_pair_pressure_count | cheap | P0b | L | 부활중 주변 스폰 |
| H17 | downed_player_threat_count | cheap | P0b | L | DBNO 주변 압력 |
| H18 | mission_progress_01 | cheap:미션API | P0a | M(방치) | 미션 페이싱 |
| H19 | mission_time_remaining_01 | cheap | P0a | L | 실패/성공 직전 페이싱 |
| H20 | mission_contributor_count | cheap:미션API | P0b | M | 협동 참여도 |
| H21 | mission_distance_to_party_median | cheap | P0b | M | 목표 주변/외곽 스폰 |
| H22 | mission_abandon_time | cheap | P0b | H(방치) | 미션 보상/압박 |
| H23 | xp_pickup_backlog_value | cheap:PickupSubsys | P0a | L | XP 회수 지연/자석 |
| H24 | xp_pickup_age_p90 | cheap | P0b | L | 드랍 과밀/회수실패 |
| H25 | freeze_duration_recent | 무료파생:GameState | P0a | H(AFK) | 레벨업 프리즈 페이싱 |
| H26 | card_pick_latency_z/player | cheap:PlayerState | P0b | H(지연) | 프리즈 UX |
| H27 | run_clock_freeze_ratio | 무료파생 | P0a | L | 전투시간 대비 정지비율 |
| H28 | topology_generation_count/rate | 무료파생:GameState | P0a | L | 문/맵 확장속도 |
| H29 | door_damage_progress_01 | cheap:Door Health | P0b | M | 문 개방 페이싱 |
| H30 | flow_unreachable_enemy_count | cheap 저빈도 | P0b | L | 갇힌 적 예산 회수 |
| H31 | stalled_enemy_count | cheap:이동패스 | P0b | L | 구조형 스폰/탈출경로 문제 |
| H32 | local_density_band_counts | cheap:spatial hash | P0a | L | 0-5/5-15/15-30m 밀도 |
| H33 | encirclement_bin_occupancy_01 | cheap:각도 bin | P0b | M(회전) | 포위·사각 위협 배치 |
| H34 | significance_tier_counts | cheap | P0b | L | relevant/가독성 예산 |
| H35 | spawn_eligible_point_count/front | cheap | P0b | L | 스폰 굶김 감지 |
| H36 | spawn_to_engage_seconds_p50/p90 | new | P0b | M | 스폰 위치 품질 |
| H37 | boss_health_progress_01/phase | cheap | P0a | L | 보스 페이싱 |
| H38 | boss_damage_share_z/player | cheap | P0b | M | 보스 타겟 분배 |
| H39 | server_frame_overrun_rate | expensive | P0c | L | 스폰 상한 안전장치(의미판단 금지) |
| H40 | net_relevant_enemy_count/player | expensive | P0c | L | RepGraph/NetCull 튜닝(hot path 주의) |

## CUT / 대표 묶음 (강상관 → 하나로 대표)
- 명중/정밀: **A1+A4** 남기고 A10 컷(크리=약점과 상관).
- 화력: **A3+A2(또는 F2)** 남기고 A6·E6 컷(TTK 오염/E6=합산).
- 생존: **B1+B3+B4** 남기고 B2·B5 컷(파생).
- 근접압력: **H1/H2+B9/B10** 남기고 B8 컷(토큰 대표; 편중은 H11).
- 고립/산개: **H9+C2** 남기고 C3·C4·E4 컷.
- 미션/시간: **H18/H19+E8** 남기고 F4 컷(의미판단 냄새).
- 공간 고급: C5·C7·C8·C11·G4 = **컷 또는 P0c**(비쌈/조작/의미판단; 잘못 넣으면 "센서"가 새 AI 비용).
- 빌드: **D3+D4** 남기고 A11 컷.
- 협동위험: FF/자해/아군피해 신호 = **전면 컷**(계약 위반).

## 티어링 (Codex 권고)
- **🟢 코어(반드시, ~22–35)**: A1,A2,A3,A4,A8 · B1,B3,B4,B9,B10 · C1,C2 · D1 · E1,E3,E5 · F1,F2,F5 · G2 · H1,H2,H4,H6,H7,H8,H11,H18,H19,H23,H25,H27,H28,H32,H37
- **🔵 권장**: A5,A7 · B6,B7 · C9(proxy),C10 · D2,D3,D4 · E8,E9,E10 · F3,F6 · G3 · H3,H5,H9,H10,H12,H14,H15,H16,H17,H20,H21,H24,H29,H30,H31,H33,H34,H35,H36,H38
- **⚪ 선택(세밀조절)**: A9 · C6 · E2,E4,E7 · G1 · H13,H22,H26,H39,H40
- **❌ 컷**: A6,A10,A11 · B2,B5,B8 · C3,C4,C5,C7,C8,C11 · E6 · F4 · G4 · FF/자해류

## 제1원리 플래그
코어는 대부분 기존 서버 훅·GameState·SpawnSubsystem·ProjectileSubsystem·FrontId·spatial hash 파생 = per-actor 비용 안 키움. **C5/C9/C11/G4/H40류(LOS·초크·조준·net-relevant)는 잘못 넣으면 "센서"가 아니라 새 AI/트레이스 비용** → P0c 또는 컷.
