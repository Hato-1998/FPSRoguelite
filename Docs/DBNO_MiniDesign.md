# DBNO 미니설계 (U9 / Phase 1B) — 사망 모델 = 정식 다운(Down But Not Out)

> **상태: APPROVED (2026-06-29) — §6 추천 디폴트 전부 채택·확정. SSOT `PlayerFeel.md` §2-13 반영 완료.** 이 문서는 `Docs/Archive/gates/U1_PostGate_Fixes.md` 결정("사망 모델 = 정식 DBNO 당겨옴, 서버권위 = Phase 1B, **DBNO 미니설계 선행 필요**")의 선행 산출물이다.
>
> **구현 진행 (브랜치 `phase/p1b-dbno`, 빌드+스모크 검증):**
> - ✅ **증분1**(`b01f76f`): `EFPSRLifeState{Alive,DBNO,Dead}` 상태기계(PlayerState, 복제·OnRep 입력게이트). 순수 리팩토링(거동 무변경).
> - ✅ **증분2**(`1a5e534`): `HandleOutOfHealth`→DBNO + 크롤(`DownedMoveScale`) + 입력게이트 분리(`IsIncapacitatedLocal`/`IsTrulyDeadLocal`, 점프 `CanJumpInternal` override) + 다운 무피해(`ApplyContactDamage`) + B17 타겟제외 + GA/XP 게이트 + 팀와이프(생존0→DBNO→Dead→Defeat).
> - ✅ **증분3**(`4de7569`): `UFPSRReviveComponent`(근접 자동부활·`ReviveProgress` 복제·GAS 체력복구·프리즈 중 정지). **DBNO 서버 로직 완성.**
> - 🔜 **증분4(C++ 범위 밖)**: 부활 게이지 HUD 위젯(`OnReviveProgressChanged`/`GetReviveProgress()` 바인딩) + 다운/관전 노티 = **VibeUE 저작(에디터 필요)**.
> - 🔜 **검증**: PIE 리슨서버 2인(§7) — 사용자 / 후속: 블리드아웃 활성·값 튜닝, 풀 관전 리그(B16) → Codex 머지게이트 → main `--no-ff`.
> 승인 후 → ① SSOT `Docs/SSOT/PlayerFeel.md` §2-13 본문 갱신 → ② C++ 구현(서버권위·복제 = Opus 직접) → ③ 콘텐츠(다운/부활 UI·VFX) → Codex 머지게이트 → main `--no-ff`.
> 출처 규격: PlayerFeel.md §2-13("수동 부활(DBNO)": 체력0→쓰러짐, 아군 근접 반경 체류 시 부활 게이지 충전→가득 차면 부활, 상태기계 Alive/DBNO/Dead, 블리드아웃·전원다운 = 밸런스 후속). 통합 대상: A4(프리즈 사망자 제외)·B16(관전)·B17(어그로 사망자 제외)·B2(클라 Defeat UI).

---

## 0. 스코프 (이 미니설계에 들어가는 것 / 빼는 것)

**IN (Phase 1B 코어):**
- 플레이어 생존 상태기계 `ELifeState { Alive, DBNO, Dead }` (PlayerState, 서버권위·복제).
- 체력 0 → DBNO 전환(즉사 아님). 다운 폰 동작(입력/이동/사격/어빌리티 게이팅).
- **근접 자동 부활**(§2-13): 생존 아군이 다운 폰 반경 내 체류 → 부활 게이지 충전 → 가득 차면 부활(서버권위).
- **팀와이프 경계**: 생존(Alive) 인원 0 → 전원 Dead → `EndRun(Defeat)`. 솔로/전원다운 포함.
- 기존 시임 재정의: `IsAlive()`·`GetLivingPlayerCount`·`AreAllPlayersDead`·`RefreshPauseState`(A4)·B17 타겟선정·`HandleOutOfHealth`.
- 부활 게이지 복제(다운 플레이어 UI) + 다운/부활 HUD 상태(B16 관전 노티 포함).

**OUT (후속·시임만 예약):**
- **블리드아웃 타이머**(DBNO→Dead 자동) — 시임만 두고 기본 **비활성**(다운은 부활/와이프까지 지속). 값·활성화 = 밸런스 후속.
- 다운/부활 **VFX·SFX·전용 애니**(콘텐츠 유닛). 본 설계는 C++ 베이스 + 최소 플레이스홀더만.
- 풀 관전 카메라 리그(자유 시점/타겟 순환) — 본 설계는 "사망 후 생존 아군 시점 따라가기" 최소 노티만(B16). 정식 관전 = 후속.
- 다운 중 약한 반격 어빌리티(피스톨 등) — 후속 밸런스.

---

## 1. 제1원리 / 기존인프라-우선 (3줄 근거 + 시임 분석)

- **① 제1원리**: 4인 협동 서바이버 = "다운→구출"이 협동 긴장의 핵심 루프. 적 수백 환경에서 다운자가 즉사하면 협동이 성립 안 함 → 다운은 **비타겟·무피해**(B17)로 보호하고 부활 압박을 만든다. 서버권위(원칙2): 생존상태·부활진행·와이프 판정 전부 서버.
- **② Lyra/표준 대비**: Lyra는 DBNO를 GA+게임플레이태그(State.Death.Dying)로 처리. 본 프로젝트는 **플레이어만 GAS**이나 생존상태는 이미 PlayerState 비-GAS bool로 운영 중 → **태그/GA 사망 상태기계는 과설계**. 기존 `AFPSRPlayerState` 패턴(복제 enum + OnRep 입력게이트) 재사용 = native, 원칙4(Lyra 맹종 회피).
- **③ 프로젝트 정합**: 코드베이스가 **DBNO를 명시적 시임으로 이미 예약**(헤더 주석: `IsAlive()` = "U9가 재정의하는 단일 술어", `bIsDead`→`ELifeState` 교체, `GetLivingPlayerCount`/와이프 술어 유지, `OnOutOfHealth` = 훅). 즉 **신규 중앙 클래스 0** — 기존 4개 지점만 확장.

**재사용(신규 생성 아님):**
| 기존 | 역할 | DBNO 변경 |
|---|---|---|
| `AFPSRPlayerState::bIsDead` | 단일 복제 bool | → `ELifeState LifeState` (복제, Push Model, OnRep 입력게이트) |
| `IsAlive()` / `IsDead()` | 와이프·프리즈 술어 | `IsAlive()=LifeState==Alive`(**DBNO=not alive**) / `IsDead()=LifeState==Dead`. 신규 `IsDBNO()` |
| `SetDead(bool)` | 상태 setter | → `SetLifeState(ELifeState)`(idempotent, 서버, MARK_DIRTY) |
| `HandleOutOfHealth()` (FPSRCharacter, 서버) | 체력0 처리 | SetDead(true)→**SetLifeState(DBNO)** + 다운 폰 게이팅. 즉시 와이프 아님 → 와이프 체크는 "생존0?"일 때만 |
| `GetLivingPlayerCount`/`AreAllPlayersDead` | 와이프 집계 | **무변경**(IsAlive() 술어 그대로 — DBNO가 not-alive라 자동 정합) |
| `RefreshPauseState` (A4) | 카드프리즈 게이트 | **무변경**(`!IsAlive()` 제외 → DBNO/Dead 자동 제외) |
| 적 플로우필드 타겟선정 (B17) | 추격 대상 | 대상 필터에 `IsAlive()` 추가(사망/다운자 비타겟) |

**신규(최소):**
- `UFPSRReviveComponent`(또는 PlayerState 서버 로직) — 다운자별 부활 진행 누적/판정. *기존 컴포넌트로 안 되는 이유*: 근접 반경 스캔 + 게이지 누적 + 복제는 생존상태와 분리된 새 책임. PlayerState에 인라인 vs 전용 컴포넌트는 §6 결정.

---

## 2. 상태기계 (ELifeState)

```
            체력 0 (HandleOutOfHealth, 서버)
   Alive ───────────────────────────────► DBNO
     ▲                                      │
     │  부활 게이지 충전 완료 (아군 근접)        │  생존(Alive) 인원 == 0  (마지막 생존자 다운 / 솔로 다운)
     │  SetLifeState(Alive) + 체력 회복         │  또는 [후속] 블리드아웃 만료
     └──────────────────────────────────────┤
                                            ▼
                                          Dead ──► (생존 아군 있으면) 관전 B16
                                            │
                                            ▼
                              AreAllPlayersDead() → EndRun(Defeat)
```

| 상태 | 입력 | 이동 | 사격/어빌 | 적 타겟(B17) | 피해 | 카메라 |
|---|---|---|---|---|---|---|
| **Alive** | 전체 | 정상 | 가능 | 대상 O | 받음 | 1P 정상 |
| **DBNO** | **없음**(이동·시점 차단) | **정지**(제자리, MaxWalkSpeed 0) | **불가**(StopFiring/CancelAll) | **대상 X** | **무피해**(다운중) | **살아있는 아군 시점 관전**(SetViewTarget) [변경 2026-06-30] |
| **Dead** | 없음 | 없음(폰 정지) | 불가 | 대상 X | — | **생존 아군 시점 따라가기**(B16) |

- **무피해 근거**: 적 수백 환경에서 다운자가 피해를 받으면 부활 전 즉사 → 협동 불성립. 압박은 **부활하지 못함(=와이프 위험)**과 [후속]블리드아웃으로 준다. (이견 시 §6.)

---

## 3. 전환 상세 (서버권위)

### 3-1. Alive → DBNO (`AFPSRCharacter::HandleOutOfHealth`, 서버)
1. idempotent 가드(`LifeState != Alive` → return).
2. `PS->SetLifeState(DBNO)`.
3. `WeaponFire->StopFiring()` + `SetAiming(false)`; `ASC->CancelAllAbilities()` (기존 코드 유지).
4. 이동: `MaxWalkSpeed *= DownedMoveScale`(크롤) **또는** `DisableMovement()`(§6 결정). 점프/대시 차단.
5. **와이프 선체크**: `GM->NotifyPlayerDefeated()` 호출(기존). 단 NotifyPlayerDefeated는 이제 **"생존0이면 와이프"** 로 동작(§3-3) → 마지막 생존자가 다운하면 즉시 와이프, 아니면 DBNO 유지.

### 3-2. DBNO → Alive (부활, `UFPSRReviveComponent` 서버 틱)
- 서버 틱(저주기, e.g. 0.1~0.2s)에서 각 DBNO 폰에 대해:
  - **프리즈 중이면 스킵**(`GS->IsRunPaused()` — 카드프리즈 시 부활 정지, freeze-gate 대칭 [[freeze-gate-client-server-symmetry]]).
  - 반경 `ReviveRadius` 내 **Alive 아군**(자신 제외, IsAlive) 존재 여부 스캔.
  - 있으면 `ReviveProgress += ReviveRate * dt`(아군 N명 가산 옵션 §6), 없으면 `-= ReviveDecayRate*dt`(또는 정지·유지 §6).
  - `ReviveProgress >= 1` → `PS->SetLifeState(Alive)`; 체력 = `ReviveHealthFraction * Max`; 이동/입력 복구; `ReviveProgress=0`.
  - **부활 직후 PostReviveInvuln(기본 5s, 편집가능 `PostReviveInvulnSeconds`)**: `PerformRevive`→`AFPSRCharacter::BeginGraceWindow`(서버, 일반화된 grace 메커니즘). ① 무적 = `ApplyContactDamage` 게이트 `Now < GraceUntil` → 전 데미지 경로(접촉/투사체/히트스캔) 무피해. ② 적 충돌무시 = 캡슐 `ECC_Pawn`(적) 응답 `ECR_Ignore`(통과). 타임아웃(타이머) 시 복원. **근거**: 갓-부활 폰이 자신을 다운시킨 적 무리 한가운데서 깨어나므로 즉시 재다운 방지. 대시 충돌무시창과 같은 채널을 토글하므로 **타임스탬프 기반 공유 헬퍼 `RefreshPawnCollisionResponse()`**로 합성(대시·grace창이 서로 조기복원 안 되게).
  - **카드선택 프리즈 종료 후 grace(기본 3s, 편집가능 `PostFreezeInvulnSeconds`)**: 같은 `BeginGraceWindow` 재사용. `HandleRunStateChanged_Movement`(서버권위)가 `bRunPaused` true→false 전이를 감지해 부여. **근거**: 카드 선택 중 적이 포위한 상태로 재개되면 프리즈 해제 즉시 피해를 입어 카드 화면을 안전하게 빠져나올 수 없음.
  - **부활시키는 사람(생존 아군) HUD 노티**: `AFPSRCharacter::GetReviveTargetProgress()`(BlueprintPure, 클라) — 생존 플레이어가 반경 내 DBNO 아군의 `ReviveProgress`(복제됨) 반환(없으면 −1). 다운된 본인 오버레이는 전체화면 비네트+텍스트+게이지, **부활시키는 사람은 게이지(로딩바)+설명 텍스트만**(비네트 없음). 신규 복제 0.
- `ReviveProgress`는 **복제**(다운 플레이어 + 근접 아군 UI). Push Model, 다운자 채널.

### 3-3. → Dead + 팀와이프 (`AFPSRGameMode`)
- `AreAllPlayersDead()` = `Participants>0 && GetLivingPlayerCount()==0`. **DBNO=not alive**이므로 전원 DBNO/Dead면 true.
- `NotifyPlayerDefeated()`(다운·사망 시 호출): `AreAllPlayersDead()`이면 → 모든 DBNO 플레이어 `SetLifeState(Dead)` → `EndRun(Defeat)`. 아니면 no-op(다운자 유지, 부활 가능).
- **솔로**: 1인 다운 → 생존0 → 즉시 와이프 → Defeat(현행과 동일 체감, 솔로 자가부활 없음).
- **[후속] 블리드아웃**: DBNO 진입 시 타이머 시작, 만료 시 `SetLifeState(Dead)` → NotifyPlayerDefeated 재평가(생존 아군 있으면 관전, 없으면 와이프). 기본 비활성.

---

## 4. 복제 계획 (서버권위 + Push Model)

| 프로퍼티 | 위치 | 복제 | 소비 |
|---|---|---|---|
| `LifeState` (enum) | PlayerState | DOREPLIFETIME_..._FAST, OnRep | OnRep_LifeState: 소유클라 입력게이트(기존 OnRep_bIsDead 대체) + HUD 상태 |
| `ReviveProgress` (float 0..1) | ReviveComponent/PlayerState | Push, 저주기 | 다운 플레이어 부활 게이지 UI + 근접 아군 "부활 중" 프롬프트 |
| (기존) `bDead`→제거 | — | — | bIsDead 참조처 전수 치환 |

- 모든 setter `HasAuthority()` 가드 + `MARK_PROPERTY_DIRTY_FROM_NAME`. enum OnRep는 단일 핸들러로 Alive/DBNO/Dead 분기(기존 OnRep 패턴).
- `ReviveProgress` 저주기 복제(매틱 X) — net 예산(§5, 적500 핫패스와 무관하나 4인 한정이라 경미).

---

## 5. 클래스/파일 변경 (예상)

- `Public/Core/FPSRPlayerState.h` / `.cpp`: `ELifeState` enum, `LifeState` 복제 프로퍼티(+OnRep), `IsAlive/IsDead/IsDBNO`, `SetLifeState`. `bIsDead`/`SetDead` 제거 + 참조처 치환.
- `Private/Hero/FPSRCharacter.cpp`: `HandleOutOfHealth` → DBNO 전환 + 다운 게이팅. 부활 시 복구 경로. 입력 바인딩에 다운 게이트.
- `Private/Core/FPSRGameMode.cpp`: `NotifyPlayerDefeated`/`AreAllPlayersDead` 와이프 시 전원 Dead 승격. (집계 함수 무변경.)
- **신규** `UFPSRReviveComponent`(또는 GameState/PlayerState 서버 틱): 근접 스캔 + 게이지 + 복제.
- 적 타겟선정(B17): 플로우필드 타겟 후보 `IsAlive()` 필터.
- 콘텐츠: 다운 부활 게이지 위젯, "부활 중" 프롬프트, 다운 폰 카메라 높이(B15 연계), 관전 시점(B16 최소).

---

## 6. 사용자 결정 필요 (추천 디폴트 = 굵게)

1. **다운 이동/카메라 [변경 2026-06-30, PIE 후]**: ~~크롤(이동 30%)~~ → **제자리 정지 + 살아있는 아군 시점 관전, 부활 = 쓰러진 자리 기상**. (사용자 결정: 크롤 폐기.) 구현: 다운 시 이동·시점 입력 차단(`Input_Move*`/`Input_Look` 게이트 `IsTrulyDeadLocal`→`IsIncapacitatedLocal`) + `ApplyDownedLocomotion(true)`=MaxWalkSpeed 0 / 서버가 가까운 Alive 아군으로 `SetViewTargetWithBlend`(원격=ClientSetViewTarget 복제), `UFPSRReviveComponent::UpdateDownedSpectate` 틱 유지·재선정, `PerformRevive`→`RestoreOwnView`(본인 폰). 폰 무이동이라 부활 위치=다운 위치 자동. **B16 관전을 DBNO로 당겨옴**(원격 아군 1P SetViewTarget의 3P 바디 가시성은 PIE 후 평가, 흉하면 3인칭 관전 카메라로 v2).
2. **다운 중 피해**: **무피해(비타겟+접촉피해 0)** vs 약한 피해/유한HP. (적 수백 → 무피해 강력 추천. 압박은 와이프 위험으로.)
3. **블리드아웃**: **시임만, 기본 비활성**(부활/와이프까지 다운 지속) vs 지금부터 타이머(예 30s). (§2-13 "밸런스 후속" → 비활성 추천.)
4. **부활 게이지**: **아군 1명+면 충전(인원수 무가산), 미충전 시 천천히 감소, 반경≈300, 충전≈3s, 회복=50%HP** — 전부 튜닝값. 인원수 가산/감소-vs-유지 = 밸런스 후속.
5. **관전(B16)**: **사망(Dead, 와이프 아님) 시 생존 아군 1인 시점 자동 추적**(최소) vs 풀 관전 리그(후속).
6. **부활 컴포넌트 위치**: **전용 `UFPSRReviveComponent`(플레이어 폰/PS 부착)** vs GameState 중앙 틱 루프. (확장성-우선 [[extensibility-first-designer-tooling]] → 전용 컴포넌트 추천: 책임 분리·디자이너 튜닝 노출 용이.)

---

## 7. 테스트 계획 (패키지 2-PC E2E + PIE)
- [ ] 2인: A 다운 → B가 근접 → 게이지 충전 → 부활(체력 50%) / B 이탈 시 게이지 정지·감소. 양 클라 게이지 동기.
- [ ] 2인 전원 다운 → 즉시 Defeat(전원 Dead) / 한쪽 부활 성공 시 런 지속.
- [ ] 솔로 다운 → 즉시 Defeat(현행 동일).
- [ ] 다운자: 적이 타겟 안 함(B17)·접촉피해 0·사격 불가·크롤만.
- [ ] 카드 프리즈 중 다운/부활 정지(freeze-gate 대칭, A4: DBNO가 프리즈 게이트 미카운트).
- [ ] 사망(와이프 아님, 블리드아웃 활성 시) → 생존 아군 관전(B16).
- [ ] 빌드 Succeeded + 헤드리스 스모크 Success + Codex 머지게이트.

## 8. 기록처
- 승인 시 → `Docs/SSOT/PlayerFeel.md` §2-13 본문 갱신 / `PROGRESS.md` 핸드오프 / `TaskPrompts_Master.md` U9. 본 문서는 설계 잠금 후 참조용 유지.
