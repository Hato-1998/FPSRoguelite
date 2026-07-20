# P0a-0 실행 프롬프트 — 폐루프 디렉터 센서 워킹 스켈레톤

> `/pm` 인입용 실행 프롬프트. 설계 근거 = [`20260720-plan-closed-loop-director.md`](20260720-plan-closed-loop-director.md) §8·§9 · 신호선택 [`20260720-p0-signal-selection.md`](20260720-p0-signal-selection.md).
> **목적 = "값이 맞나"가 아니라 "배관 불변식(생명주기 누수0·프리즈중 미진행·주입→출력 결정성·Front 매핑·enemy/boss 게이팅)"을 골든으로 잠그는 첫 수직 슬라이스.**

## 브랜치/모델
- **main에서 `phase/director-p0a0` 분기**, 검증 후 `--no-ff` 머지(CLAUDE.md §6-7). ⚠️ 현재 `phase/citytool-blockout`(U22 아트)와 분리 — 이 작업은 별도 브랜치.
- 구현 = **Sonnet 위임**(`model="sonnet"`), 검증(빌드/PIE/diff/골든) = **Opus 직접**.
- 서버권위 + Push Model + PIE 2-client 상시(솔로 가정 금지).

## 스코프 IN (이것만)
1. **`UFPSRDirectorSensorSubsystem`** (서버전용 `UWorldSubsystem`, 기존 디렉터처럼 `HasServerAuthority()` 게이트). 복제 0.
2. **`FFPSRPlayerTelemetry`** (고정크기 struct) — P0a-0 필드만. 서버로컬 `TMap<TWeakObjectPtr<AFPSRPlayerState>, FFPSRPlayerTelemetry>`.
3. **생명주기**: StartRun 등록 · EndRun/Logout 명시 정리 · 매 집계 invalid WeakPtr 정리(tombstone 누수 0). 재접속 = 새 baseline(승계 없음).
4. **결정적 시계**: 집계 tick 0.5~1s를 디렉터/월드 델타로 구동하되 **`bRunPaused` 중 미진행**(unpaused-delta). 테스트용 내부 `Advance(dt)`.
5. **신호 5개(P0a-0)**:
   - **HealthPct** — 플레이어 ASC(`UFPSRHealthSet`) 읽기. invalid/lowconf 처리.
   - **IncomingDamageRate[Enemy/Boss]** — 플레이어 피격 초크(`AFPSRCharacter::ApplyContactDamage(float, AActor*, FGameplayTag)`)에 센서 훅. **센서에서 source 분류**(Instigator: `AFPSREnemyBase`계=Enemy·`AFPSRBossBase`=Boss만 카운트; FF/Self/Door/Mission/Env 제외). 구간 EWMA rate. **브릿지 시그니처 불변.**
   - **DownedRecent01 (B3)** — `HandleOutOfHealth`→`SetLifeState(DBNO)` 훅. count + 최근다운시각.
   - **MovementConfinement01 (C1)** — 고정 7필드 anchor confinement(AnchorXY·AnchorStartTime·LastSampleXY/Time·MaxDistFromAnchor·bConfined·ConfinedSince). Deadband 75~100cm / ConfinementR 600~800cm / ReleaseR ×1.25 / Window 20~30s / XY only. 위치 샘플 = 이동패스 재사용 or 집계.
   - **FrontId** — **read-only occupancy 스냅샷 API**(`ComputeOccupancy` 결과를 스폰 allocator **부작용 없이** 추출). per-player 현재 Front.
6. **주입 하네스**(테스트=콘솔 동일 내부함수, `#if !UE_BUILD_SHIPPING`): `InjectDamageTaken(idx,amt,src)`·`InjectDown(idx)`·`SetHealthBand(idx,pct,dur)`·`SampleMove(idx,x,y,z,t)`·`SetFront(idx,fid)`·`StartRun/EndRun/Logout(idx)`·`Advance(t)`·`DumpSnapshot`(CSV/log).

## 스코프 OUT (P0a-1/P0a-2 — 넣지 말 것)
파생/정규화/그룹핑 전부: A1/A4 명중·A5 오버킬·A7 광역·H11 편중·H33 포위·H36 스폰품질·D1 baseline·미션 API·spatial bands·토큰·XP·프리즈 metric·보스 등. **디렉터 소비/액추에이터 없음**(스냅샷 읽기 스텁만 허용). 의미판단(Score) 절대 금지.

## 검증 = 이원 게이트
- **Unit/headless(Automation)**: ① 주입→snapshot 결정성(같은 입력=같은 출력) ② 생명주기 정리(Logout/EndRun 후 tombstone 0) ③ 프리즈 중 집계·confinement 윈도우 미진행 ④ confinement 골든(정지→window후1·deadband 미세이동→유지·반경이탈→reset·confined시 ReleaseR 해제) ⑤ source 게이팅(Enemy/Boss만, FF/Self 제외).
- **PIE/functional 2-client**: 실제 DBNO→DownedRecent · 실제 적 데미지→IncomingDamageRate · FF/자해 제외 확인 · 문 넘어 이동 시 FrontId 매핑·per-player 승계.
- 검증 없이 "완료" 금지. `Result: Succeeded` 빌드 + 골든 통과 + PIE 로그 첨부.

## 제1원리 체크
서버전용·bounded(≤4인)·**per-enemy tick 금지**(전부 이벤트 훅/기존 초크 재사용)·복제 0. 적 hot path 무영향.

## 완료 후
- SSOT(§2-8) "폐루프 디렉터" 항목에 P0a-0 구현상태 반영.
- 다음 = P0a-1(계산기+훅 신호, 이원게이트) → P0a-2(승격) → P1 공간선택.
