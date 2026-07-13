# P7 멀티플레이 게임 루프 — 로비→인게임→보스→로비 (플랜 / 세션 핸드오프)

> **상태**: 설계 확정·미구현. 백로그 **D5(세션)+D4(보스 축소)+신규 로비/트래블**을 통합하는 마일스톤.
> **선행**: 무기 6종 + 미션 완료, **P5 FF 머지**(보스도 데미지 받게 데미지 안정화 위에). 패배조건엔 **D2 DBNO** 일부 필요.
> **필독**: `Game.md` 허브 + `PROGRESS.md` + 본 문서. 흐름/세션 도메인이라 `Docs/SSOT/RunFlow.md`(§2-1·2-7·2-8) · `Docs/SSOT/Architecture.md`(§3·4) 참조.
> **모델 정책**: 세션/트래블/서버권위 흐름이라 **Opus 직접 구현/검증**.

---

## 0. 목표 & 확정 결정 (사용자, 2026-06-11)

**루프**: 로비(Steam 초대) → 인게임(전투/미션) → 보스(맵 중앙 박스, 체력만) → 로비 복귀
- **세션 백엔드 = Steam** (개발용 app id 480, 프렌드 초대)
- **보스 트리거 = BossTime 경과**(기존 RunDirector)
- **승리 = 보스 처치 → 로비**, **패배 = 전원 사망(전멸) → 로비** (둘 다 로비 복귀)
- 보스 = "맵 가운데 네모 박스, 체력만, 죽이면 클리어"(최소 — 실보스는 D4 후속)

---

## 1. 아키텍처 (제1원리 3줄)

1. **제1원리**: 세션(OnlineSubsystem 추상)과 매크로 흐름(레벨 ServerTravel)을 분리. **보스=기존 `UFPSREnemyHealthComponent` 재사용** → 모든 무기 데미지/크릿/FF가 그대로 먹힘(신규 데미지 경로 0). 흐름은 서버권위 GameMode 주도.
2. **Lyra/표준**: Lyra `UCommonSessionSubsystem`/CommonUser 플로우를 **경량 참조**(풀 UIManager/GameUIPolicy 미도입, 원칙1). `ServerTravel`/SeamlessTravel = UE 표준 그대로.
3. **프로젝트 정합**: 백로그 D5/D4를 당겨와 통합. 기존 RunDirector Boss 페이즈·`bRunPaused`/RunState 복제 패턴 재사용. Iris 미사용(Push Model 유지).

---

## 2. 선행 조건 / 의존
- **무기 6종 + 미션** 완료(사용자), **P5 FF 머지**(보스 데미지·통합 데미지 위에).
- **D2 DBNO** 일부(전멸 판정 = 전원 Dead/DBNO). 간이판: Health 0 전원 집계로 시작 가능.
- 머지 의존 체인: `fix` → `p5-friendly-fire` → (이 작업) `phase/p7-mp-loop`.

---

## 3. 구성 요소 (파일 단위)

### 3-1. Steam 온라인 서브시스템 설정 (config)
- 플러그인 `OnlineSubsystemSteam` enable(.uproject).
- `Config/DefaultEngine.ini`:
  - `[OnlineSubsystem] DefaultPlatformService=Steam`
  - `[OnlineSubsystemSteam] bEnabled=true`, `SteamDevAppId=480`(개발 공용), `bInitServerOnClient=true`
  - `[/Script/Engine.GameEngine] +NetDriverDefinitions=(DefName="GameNetDriver", DriverClassName="OnlineSubsystemSteam.SteamNetDriver", ...)` (소켓서브시스템 Steam)
- 테스트 = **Steam 클라이언트 실행 상태**에서 패키지 빌드 2-PC(또는 PIE 폴백은 세션 없이 직접 트래블만).
- ⚠️ app id 480 = 공용 스페이스워(테스트 전용). 출시 시 전용 app id.

### 3-2. 세션 서브시스템 (D5) — `Core/FPSRSessionSubsystem.{h,cpp}`
- `UFPSRSessionSubsystem : UGameInstanceSubsystem` — `IOnlineSessionPtr` 래핑:
  - `HostSession(int32 MaxPlayers=4, bool bLAN=false)` → CreateSession → 성공 시 `ServerTravel(L_Lobby?listen)`.
  - `FindSessions()` / `JoinSession(Result)` → 성공 시 `ClientTravel`.
  - `DestroySession()` (로비 나가기/종료).
  - **초대**: Steam 오버레이 `ShowInviteUI`(프렌드 초대) + 초대 수락 핸들(`OnSessionUserInviteAccepted`) → JoinSession.
  - 콜백 멀티캐스트 델리게이트(BP 바인딩용): OnHostComplete/OnFindComplete/OnJoinComplete(결과코드).
- (Lyra `UCommonSessionSubsystem` 패턴 경량 참조 — Request 객체까지는 불필요.)

### 3-3. 로비 — 맵 + GameMode + UI
- 콘텐츠 맵 `L_Lobby`.
- `AFPSRLobbyGameMode : AGameModeBase`(`Core/FPSRLobbyGameMode.{h,cpp}`): 로비 페이즈, 플레이어 입장(PostLogin) 집계, 호스트만 시작 게이트. `bUseSeamlessTravel=true`.
- 로비 UI WBP(콘텐츠, C++ 베이스 `UFPSRLobbyWidget`): 플레이어 목록(PlayerState 순회), **초대 버튼**(세션서브시스템 ShowInviteUI), 호스트 **"시작"** 버튼.
- 호스트 시작(서버 RPC) → `GetWorld()->ServerTravel("/Game/Maps/L_Gameplay?listen")`.

### 3-4. 레벨 트래블 흐름
- **Seamless travel**: 모든 GameMode `bUseSeamlessTravel=true` + `Config`에 TransitionMap(빈 맵) 지정 → 트래블 중 연결/세션 유지(재접속 없음).
- 로비→인게임: 호스트 ServerTravel, 클라 자동 동반. PlayerController/PlayerState는 `GetSeamlessTravelActorList`로 유지.
- 인게임→로비: GameMode가 보스클리어/전멸 시 `ServerTravel("/Game/Maps/L_Lobby")`.
- 디버그 `FPSR.TravelLobby` / `FPSR.TravelGame`(흐름 골격 선검증).

### 3-5. 보스 (D4 축소) — `Enemy/FPSRBossBox.{h,cpp}`
- `AFPSRBossBox : AActor`: StaticMesh 박스 + `UFPSREnemyHealthComponent`(고체력 예 10000) + 콜리전(무기 트레이스/투사체에 맞도록 `ECC_Pawn` object type 또는 Visibility 블록 — **기존 무기가 적을 잡는 채널과 동일하게** 설정). `bReplicates`.
- **데미지 = 신규 코드 0**: EnemyHealthComponent가 있으니 기존 Hitscan/Projectile/ChargeLaser/Melee가 그대로 적용. (단 박스는 폰 아니면 넉백 비대상 — OK, static.)
- 사망: `OnDeath` 델리게이트 → GameState `SetRunCleared()` 브로드캐스트.
- 배치/등장: 맵 중앙 레벨 배치(비활성) 또는 RunPhase→Boss 시 중앙 스폰. **BossTime 경과(RunDirector)** 시 활성/스폰.
> ⚠️ 보스를 무기 데미지로 잡으려면 콜리전 채널이 핵심 — 적(EnemyBase)이 맞는 채널/오브젝트타입과 동일하게 맞출 것(Hitscan=Visibility 벽+ECC_Pawn 멀티트레이스, 투사체=ECC_Pawn 오버랩). 박스가 ECC_Pawn 오브젝트타입이면 폭발/관통도 자연 적용.

### 3-6. 매크로 흐름 상태기계 (RunDirector/GameState 확장)
- 상태: `Lobby → InGame(Combat→Boss) → Cleared/Wiped → (travel) Lobby`.
- **승**: 보스 `OnDeath` → `Cleared` → 결과 짧게 표시 → `ServerTravel(Lobby)`.
- **패**: 전원 사망(전멸) → `Wiped` → `ServerTravel(Lobby)`. 전멸 판정 = 모든 플레이어 Dead/DBNO 집계(D2 연계; 간이=Health 0 전원).
- 로비 복귀 시 **런 상태 리셋**(SharedXP/PartyLevel/카드/무기 인벤토리 초기화) — 새 런.

---

## 4. 구현 순서 (권장)
1. **Steam 설정(3-1) + 세션 서브시스템(3-2)**: 호스트/조인/초대 → 2-PC(Steam) 또는 2-client 로비 입장 확인.
2. **L_Lobby + LobbyGameMode + 로비 UI(3-3)**: 목록/시작/초대.
3. **Seamless ServerTravel 골격(3-4)**: 보스 없이 디버그 cmd로 로비↔인게임↔로비 왕복(연결 유지).
4. **AFPSRBossBox(3-5) + BossTime 트리거 + OnDeath→Cleared→travel**.
5. **전멸 판정→Wiped→travel(3-6)** + 로비 복귀 런 리셋.
6. **콘텐츠(사용자)**: L_Lobby/L_Gameplay 맵, 로비 UI WBP, 보스 박스 메시, GameMode 설정.
7. 각 단계 빌드+스모크. 최종 **2-PC Steam E2E**.

> 단계 분리 팁: 3번(트래블 골격)은 세션 없이도 PIE/단일에서 검증 가능 → 세션(1)과 병행 가능.

---

## 5. 검증
- **2-PC Steam**(또는 세션 빼고 PIE 멀티 폴백으로 트래블/보스/흐름만):
  - 초대 → 로비 입장 → 호스트 시작 → 인게임 → (BossTime) 보스 등장 → 처치 → **로비 복귀**.
  - 전원 사망 → **로비 복귀**(패배).
  - Seamless travel 중 연결/세션/플레이어 유지.
  - 보스 데미지 = 서버권위, 전 무기(+FF/넉백) 적용.
- 빌드 + 헤드리스 스모크(세션/트래블은 헤드리스 한계 → 2-PC 수동).

---

## 6. 미결 세부 (착수 시)
- **Seamless vs non-seamless**: seamless=연결유지(권장)지만 TransitionMap·ActorList 관리 필요. 불안정 시 non-seamless 폴백.
- **로비 재입장 런 리셋 범위**: XP/레벨/카드/무기 전부 초기화(새 런) 확정.
- **호스트 마이그레이션 없음**: 호스트 종료=세션 종료(단순화). 클라는 로비/메인으로.
- **보스 콜리전 채널**: 무기별 트레이스/오버랩 채널과 정합(3-5 경고) — 가장 흔한 함정.
- **보스 등장 연출/HUD**: 체력바 등 최소(박스 위 체력 텍스트 디버그로 시작).
- **app id 480**: 공용 테스트. 동시 다수 세션 충돌 가능 → 실테스트는 인원 적게.

---

## 7. 새 세션 재개 프롬프트 (복붙)
```
Game.md + PROGRESS.md + Docs/P7-MultiplayerLoop_Plan.md 먼저 읽어.
phase/p7-mp-loop 브랜치 분기해 멀티플레이 게임 루프(로비→인게임→보스→로비) 구현.
- 선행: 무기/미션 완료 + P5 FF 머지 확인. 플랜 §4 순서대로.
- 확정값: 세션=Steam(app id 480), 보스=BossTime 트리거+중앙 박스(EnemyHealthComponent 재사용),
  승=보스킬·패=전멸 둘 다 로비 복귀, seamless travel.
- 세션/트래블/서버권위라 Opus 직접 구현/검증. 단계마다 빌드+스모크, 최종 2-PC Steam은 사용자.
- 콘텐츠(맵 L_Lobby/L_Gameplay, 로비 UI WBP, 보스 박스 메시)는 사용자 — 코드 베이스만 완성.
```
