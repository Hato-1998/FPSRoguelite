# U1 사후 버그 배치 — MP E2E + 게이트 피드백 26건 (정리/우선순위/담당)

> **출처**: U1 재미게이트(솔로) + 2-client/Steam E2E 멀티 테스트(2026-06-29). 사용자 보고를 시스템·리스크별로 정리.
> **U1 판정 = 조건부 합격** (핵심 루프 동작 확인 / MP 동기화·피드백 결함 다수 → **Phase 1 MP 넷코드 수정 패키지 E2E 검증 후 TaskPrompts §B U1 ✅**).
> **범위 A(5건)** = 이미 작업트리 미커밋(빌드·검증·커밋 대기). **범위 B(21건)** = 신규 (B3 → B3a 차지레이저 노티 / B3b 로비 슬롯 겹침 분리).
> **원칙**: MP/세션/넷드라이버/복제/RPC = **Opus 직접**(Haiku 위임 금지). 코드 전 **엔진 소스 대조**. MP 검증 = **패키지(SteamSockets PIE 미등록)**. 브랜치 = `main→fix/...`, 검증 후 `--no-ff`, Codex 머지게이트.

---

## 🔧 결정 반영 (2026-06-29) — 판단 17건 완료
> 실행 전 사용자 판단 완료. 미해결 설계판단은 추천 디폴트 채택(이견 시 갱신).

- **사망 모델 = 정식 DBNO 당겨옴** (A4·B17·B16·B2 통합, 서버권위 = **Phase 1B**): 죽으면 다운(DBNO)→아군 관전/부활→미부활·팀와이프 시 사망. ⚠️ **스코프 증가**(P5 유닛 앞당김). **DBNO 미니 설계 선행 필요**(다운 상태머신·부활 상호작용·다운 이동·팀와이프→Defeat 경계, DESIGN-FIRST). A4(프리즈 제외)·B17(BFS 시드 제외)는 이 모델의 일부.
- **B13 = 예고형 선딜레이**: 레벨업 직후 **레벨업 VFX/SFX 표시 → N초 후 프리즈+카드UI**. N=디자이너 튜닝값. B안 baseline 프리즈와 무충돌(리드인만 추가, 비동기 전환 아님).
- **B18 = 공중 프리즈 낙하 방지**: 점프/대시/공중 이동 중 프리즈 시 **중력으로 뚝 떨어짐 방지**(속도·중력 서스펜드) + 재개 시 궤적 복원. freeze-gate 완성 버그(수평입력은 이미 정지). [[freeze-gate-client-server-symmetry]].
- **연기**: B8→U7(플로우필드 높이 정식 유닛, 이번엔 최소 GroundSnap) · B20→P7(통합 피드백) · B3a→콘텐츠 저작 유닛(빔/차지 큐).
- **설계 추천 채택**(이견 시 갱신): B4=GameplayMessage+거리게이트 · B3b=ChoosePlayerStart 오버라이드(단상 지오메트리 의존) · B11=월드공간 오클루전+거리스케일 · B9=피치 변조 · B15=눈높이~165cm · B12=MaxHealth 복제+OnRep_Health · B5=독립 코스메틱(소비 WBP) · B14=DrawCards 경로 조사 후 offer 전 IsValid.
- **B7=조사 우선**: 풀링 Activate/Deactivate 복제 + bDead OnRep 클라 토글; 풀링 재설계 필요 시 P2 SCOPE_UNIT 승격.

---

## Phase 1 — MP 넷코드/복제 (서버권위) ★ U1 ✅ 게이팅 크리티컬패스
> 협동이 깨지는 치명 결함. 작업트리 5건 마무리 + 신규 4건. **전부 패키지 2-PC/2계정 Steam E2E로 검증.**

| ID | 증상 | 추정 원인 / 시스템 | 담당(파일·SSOT) | 상태 |
|---|---|---|---|---|
| A1 | Steam join 즉시 튕김 | UE5.7 Steam 넷드라이버 SteamSockets 이전 + CoreRedirect 無 → 옛 문자열 무음 실패 → IpNetDriver 폴백 | `DefaultEngine.ini`·`.uproject` | 🔧작업트리 |
| A2 | JoinByCode 후보 0건 | presence 로비인데 `SEARCH_LOBBIES` 없이 전용서버 브라우저 조회 | `FPSRSessionSubsystem` | 🔧작업트리 |
| A3 | 로비 조이너 카메라가 포디움 안 | 리슨서버 클라 폰 빙의가 뷰 탈취 | `FPSRLobbyPlayerController`(+.h) | 🔧작업트리 |
| A4 | 사망자 있으면 레벨업 프리즈 소프트락 | 죽은 팀원이 카드선택 프리즈 게이트에 카운트 | `FPSRGameState`·`FPSRPlayerController`(`!IsAlive`) | 🔧작업트리 |
| A5 | (진단) MP 흐름 추적 | — | `FPSRFlowLog`/`FPSRFlowLogSubsystem` 신규+배선 | 🔧작업트리·별도커밋 |
| B7 | **클라 스웜 적 동기화 실패** — 안보이는데 데미지 입음 / 죽어도 클라에 잔존 ★ | 적 복제(dormancy/relevancy) 또는 풀 액터 재사용 시 클라 미스폰·미파괴 | `FPSREnemySpawnSubsystem`·`FPSREnemyBase`·§5/§2-6 Enemy.md | 신규 |
| B12 | **보스 체력바 클라 미갱신** — 데미지 입는데 풀피 ★ | `EnemyHealthComponent` 체력 복제/`OnHealthChanged`가 클라 UI에 미도달 | `WBP_BossHealthBar`·`UFPSREnemyHealthComponent` 복제 | 신규 |
| B2 | 클라에서 Defeat UI 미표시 | `EndRun(Defeat)` 결과 위젯이 호스트만 표시(클라 경로 누락) | `FPSRGameMode::EndRun`·PC 결과위젯 | 신규 |
| B4 | 아군 사격 소리 안 들림 | 사격 SFX=비복제 코스메틱(§5) → 아군분 멀티캐스트 큐 없음 | `FPSRWeaponFireComponent`·GameplayCue/Message | 신규 |
| B3b | **로비 4인 포디움 슬롯 겹침** — 두 명이 한 자리에 배치 | 로비 플레이어 슬롯 배정/복제가 충돌(PlayerStart·포디움 인덱스 중복) | `FPSRLobbyGameMode`·`FPSRLobbyPlayerController`·로비 콘텐츠(L_Lobby 포디움) | 신규 |

## Phase 2 — 적 AI / 길찾기 / 타겟팅
| ID | 증상 | 추정 원인 / 시스템 | 담당 |
|---|---|---|---|
| B8 | 파이프(수평) 내부 적이 안 떨어짐 | 플로우필드 단일 Z밴드·중력/지면추종 한계(다층) → **U7 높이/클리어런스** | `FPSREnemyBase`·flowfield, U7(`phase/p2-flowfield-height`) |
| B17 | 죽으면 몹이 죽은 사람 어그로 못 풀고 안 옮김 | 타겟 규칙(최근접 플레이어)이 사망자 미제외 | 플로우필드 타겟 선정(사망자 제외) |
| B19 | 스폰포인트 닫힘 = **전 플레이어 통과** 확인 필요(어려우면 닫힘 제거) | `Deactivate`가 1인 통과로 트리거 | `AFPSRSpawnRoom`·`FPSREnemySpawnSubsystem`·Enemy.md §2-6 |

## Phase 3 — 프리즈 / 카드 페이싱 (게이트 ②)
| ID | 증상 | 추정 원인 / 시스템 | 담당 |
|---|---|---|---|
| B13 | 카드선택 **예고형 선딜레이**: 레벨업 VFX/SFX → **N초 후** 프리즈+카드UI | 즉시 프리즈로 맥락 없이 정지 | 레벨업 큐(콘텐츠)+지연 프리즈 트리거(`RefreshPauseState`)·N=디자이너 튜닝 |
| B18 | **공중 프리즈 낙하 방지**: 점프/대시/공중 중 프리즈 시 중력으로 뚝 떨어짐 → 서스펜드 + 재개 시 궤적 복원 | 프리즈가 수평입력만 정지, 중력/수직속도는 계속 | CMC 이동모드/중력 서스펜드(서버권위) [[freeze-gate-client-server-symmetry]] |
| B6 | 카드 후 대기 시 "다른 플레이어를 기다리는 중" 문구 | 다인 동기 프리즈 대기 상태 UI 부재 | 카드 위젯·`RefreshPauseState` 상태 |
| B14 | 카드선택 **Missing 카드** 발견 | 무효 카드(GE-null 등)가 추첨 소모 → **W1 P3 백로그**(`CanApply` GE-null·IsDataValid) | `CardEffect_CharacterGE`·카드 DataAsset |

## Phase 4 — UI / HUD / 피드백 (게임필 ①③)
| ID | 증상 | 추정 원인 / 시스템 | 담당 |
|---|---|---|---|
| B20 | **데미지 플로팅 텍스트** 필요(적 피격 숫자 N초 후 소멸) | 피드백 부재(손맛 ①) | 신규 UI(메시지→위젯풀)·PlayerFeel.md §2-13 |
| B11 | 적/보스 HP바 거리 무관 동일크기·벽 뚫림 → 상단 고정 UI 검토 | 월드공간 빌보드 스케일·오클루전 미처리 | 적 HP바 WBP·§2-13 |
| B10 | 미션 시작 시 상단 배너 "(제목) 미션 시작" | 미션 출현 노티 부재 | RunDirector 미션 이벤트→HUD |
| B5 | 피격 방향 노티(어디서 맞았는지) | `FPSR.TestDamageDir`는 있으나 실데미지 배선 미흡 | `FPSRPlayerFeedbackComponent`·§2-14 |
| B9 | 사각 사운드 **Z축(높이) 부재**(위에 있어도 구분 불가) | V1 사각 오디오가 수평면만 | `FPSRBlindspotAudioComponent`·§2-14 |
| B16 | 죽으면 아군 **관전**(부활 전까지) → **정식 DBNO 채택**(다운/관전/부활, Phase 1B) | DBNO 미구현 — P5 유닛 앞당김 | PC/GameMode 사망처리, **DBNO 미니설계 선행** |

## Phase 5 — VFX / 카메라 / 비주얼
| ID | 증상 | 추정 원인 / 시스템 | 담당 |
|---|---|---|---|
| B1 | 바주카/그레네이드 **머즐 화염 안 지워짐** | 머즐 Niagara 자동소멸/detach 누락 | 투사체/무기 머즐 FX(`FPSRProjectile`·콘텐츠) |
| B15 | 1P 카메라 시야 높이 ↔ 아군 캐릭터 키 불일치(1P가 너무 높음) | 카메라 소켓 높이 ≠ 메시 eye height | `FPSRCharacter` 카메라·메시 |
| B3a | **차지레이저 발사 시퀀스 노티 부재** — 쏘고 있는지·누구를 맞췄는지 불명(V2 ChargeLaser) | 차징·빔 VFX/사운드·피격 피드백 부재(서버권위 시퀀스는 동작하나 시각/청각 노티 없음) | `FPSRGA_WeaponFire_ChargeLaser`·콘텐츠 Niagara/큐 · B20(피격 숫자)·히트마커 연계 |

---

## 죽음 처리 교차 스레드 (A4·B16·B17·B2)
사망 관련 결함이 여러 Phase에 걸침: 소프트락 제외(A4 ✓작업트리) → 관전(B16) → 어그로 재타겟(B17) → 클라 Defeat UI(B2). **일관된 "사망 상태" 모델**(서버권위 `bIsDead` 기반)로 묶어 처리 권장.

## 검증 (FlowLog `logs/FlowLog_*.log` 교차확인)
- [ ] 빌드 Succeeded + 헤드리스 스모크 `Result={Success}`
- [ ] **Phase 1 패키지 2-PC E2E**: host→join(코드/초대)→로비(카메라 정상·**4인 슬롯 분리 B3b**)→ready→travel→양 클라 진입 / 클라 적 가시·사망 정리(B7) / 보스 체력바 클라 갱신(B12) / 클라 Defeat UI(B2) / 아군 사격음(B4) / 사망자 프리즈 소프트락 X(A4)
- [ ] **Phase 1 통과 = TaskPrompts §B U1 ✅ 확정** (조건부 해제)

## 기록처
- `PROGRESS.md`(핸드오프) · `Docs/U1_GateSheet.md`(§D 조건부 합격) · `Docs/TaskPrompts_Master.md`(§B U1/V2) · SteamSockets 근본원인+SEARCH_LOBBIES → `Docs/P7-MultiplayerLoop_Plan.md` · 사각 Z축/플로팅텍스트 등 SSOT(§2-13/§2-14)
</content>
</invoke>
