# 세이브 시스템 — EA 운영 요구와의 갭 정의 (2026-08-13)

> **역할**: `Roadmap.md` §7-6 **M0 (d)** 의 세이브 잔여("EA 요구와의 갭 정의만") 산출물. 갭은 전부 **M5** 로 이관한다.
> **범위**: 갭 *정의* 다. 구현·설계 결정은 하지 않는다(M5 착수 시 사용자 결정).
> **왜 지금인가**: (d)의 세이브 항목은 이미 *"버저닝·마이그레이션은 구현돼 있다"* 로 축소된 상태였고, 남은 것이 문서 작업뿐이라 빌드·에디터 없이 닫을 수 있었다.

---

## 0. 한 줄 결론

**인프라는 견고하다. 문제는 저장할 스키마가 아직 없다는 것이다.**
`RunFlow.md` §2-11이 P6에 요구한 저장 정책 6항목 중 **4항목 충족 / 2항목 미충족**이고, 그 위에 **추가 갭 5건**을 찾았다. **최대 갭은 "정책"이 아니라 스키마 공백**이며, 스키마가 확정돼야(→ M3) 나머지가 의미를 갖는다.

---

## 1. 현황 사실표 (실물 근거)

| # | 항목 | 실물 | 근거 |
|---|---|---|---|
| 1 | 세이브 클래스 | `URogueliteSaveGame : USaveGame` | `Public/MetaProgression/RogueliteSaveGame.h` |
| 2 | `CurrentSaveVersion` | `static constexpr int32 = 1` | 같은 파일 |
| 3 | **저장 필드 전수** | **2개뿐** — `int32 SaveVersion` · `int64 Reserved0`(주석: *"NO meta meaning"*). 둘 다 `UPROPERTY(SaveGame)` | 같은 파일 |
| 4 | `MigrateIfNeeded()` | 같으면 false / **더 신형이면 Warning 후 as-is**(역마이그레이션 없음) / `case 0` → `Reserved0=0; SaveVersion=1`, `default` → 무한루프 방지 스냅 | `Private/MetaProgression/RogueliteSaveGame.cpp` |
| 5 | 게이트웨이 | `UFPSRSaveGameSubsystem : UGameInstanceSubsystem`(레벨/심리스 트래블 관통) | `Public/MetaProgression/FPSRSaveGameSubsystem.h` |
| 6 | `EFPSRSaveReason` | `RunEnd` / `Lobby` / `Migration` / `Manual` | 같은 헤더 |
| 7 | **저장 호출 지점 = 3곳(전부 C++)** | ① 마이그레이션 재영속 `RequestSave(Migration)` ② **런 종료** `RequestSave(RunEnd)` ③ 맵 로드 시 pending 재플러시 | `FPSRSaveGameSubsystem.cpp:75` · `FPSRPlayerController.cpp:627` · `FPSRSaveGameSubsystem.cpp:132` |
| 8 | 런 종료 경로 | `FPSRGameMode.cpp:208` `PC->ClientCommitMetaSave(Outcome)`(Client·**Reliable**) → `FPSRPlayerController.cpp:614` `IsLocalController()` 게이트 → `RequestSave(RunEnd)` | — |
| 9 | **`EFPSRSaveReason::Lobby` 호출처** | **0건** — enum만 정의돼 있고 아무도 부르지 않는다 | `grep -rn "EFPSRSaveReason::Lobby" Source/` → 0 |
| 10 | 저장 API | `AsyncSaveGameToSlot`(동기 `SaveGameToSlot`은 **테스트에서만**) | `FPSRSaveGameSubsystem.cpp:98,112` |
| 11 | 로드 API | `DoesSaveGameExist` → `LoadGameFromSlot`, **프라이머리 → 백업 → 신규 default** 3단 | `FPSRSaveGameSubsystem.cpp:46-68` |
| 12 | `DeleteGameInSlot` | **셰이핑 코드 0건** — 자동화 테스트에서만 | `Tests/FPSRSaveGameTest.cpp:33,47` |
| 13 | 슬롯명·유저 인덱스 | **하드코딩 아님** — `UFPSRSaveSettings : UDeveloperSettings(Config=Game)`: `PrimarySlotName="PlayerMeta"` · `BackupSlotName="PlayerMeta_Backup"` · `UserIndex=0` | `Public/Settings/FPSRSaveSettings.h` · `Config/DefaultGame.ini` |
| 14 | 슬롯 개수 | **고정 단일 슬롯 + 백업 1개.** 다중 슬롯·프로필 없음 | 위 + ini 주석 |
| 15 | **손상 복구** | **존재한다** — ① 프라이머리 load 실패 시 Warning + 백업 시도 ② 둘 다 실패 시 신규 default ③ **프라이머리 저장 성공 후에만** 백업 미러(실패한 저장이 last-good 백업을 덮지 않는다) ④ 실패 시 `bSavePending` 유지 → 다음 맵 로드에서 재시도 | `FPSRSaveGameSubsystem.cpp:49-68, 107-119, 125-134` |
| 16 | 체크섬/무결성 | **없음.** 손상 판정 = `LoadGameFromSlot`이 null을 주거나 캐스트 실패할 때뿐 | `FPSRSaveGameSubsystem.cpp:48,57` |
| 17 | 카드ID 리네임 fallback | `ResolveCardId` — 최대 8홉 체인, 사이클 방어. `DeprecatedCardIdRedirects`는 **현재 비어 있음** | `FPSRSaveGameSubsystem.cpp:136-160` |
| 18 | 데디케이티드 서버 | 전 op no-op(per-player local ownership) | `FPSRSaveGameSubsystem.cpp:16-21, 82-85` |
| 19 | **`OnSaveComplete` 구독자** | **0건** — Broadcast만 하고 아무도 듣지 않는다 | `grep -rn "OnSaveComplete" Source/` |
| 20 | **BP/Content 측 참조** | **0건** — `Content/` 전체에 `PlayerMeta`·`RogueliteSaveGame`·`SaveGameToSlot` 문자열 0 | `grep -rla` |
| 21 | **Steam Cloud 코드** | **0.** `UserCloud`/`IOnlineUserCloud`/`RemoteStorage`/`CloudSave` 전 리포 **0건** | `grep -rn … Source/ Config/` → 0 |
| 22 | Steam 설정 실물 | `[OnlineSubsystem] DefaultPlatformService=Steam` · `[OnlineSubsystemSteam] bEnabled=true / SteamDevAppId=480 / bInitServerOnClient=true` | `Config/DefaultEngine.ini` |
| 23 | `.uproject` 플러그인 | `OnlineSubsystem(+Utils/Steam)`·`SteamSockets` enabled. **Cloud 전용 플러그인은 UE에 없다**(Auto-Cloud = 파트너 사이트 설정) | `FPSRoguelite.uproject` |
| 24 | 자동화 테스트 | `FPSRoguelite.Smoke.SaveGame`(`EditorContext｜EngineFilter`) — 5블록 | `Private/Tests/FPSRSaveGameTest.cpp` |

**테스트가 검증하는 것 (5블록)**: ① 신규 세이브 = current version + 중립 default ② v0→current 1회 마이그레이션 + 재호출 no-op ③ 실제 슬롯 직렬화 라운드트립(`Reserved0` 보존 + 버전 보존) ④ 부재 폴백 ⑤ `ResolveCardId` 4케이스.
**검증하지 않는 것**: 백업 미러 · 실제 손상 파일 복구 · 비동기 저장 **실패** 경로 · `bSavePending` 재플러시 · 서브시스템 자체(테스트는 전용 슬롯만 쓴다).

> ℹ️ **`Config/DefaultEngine.ini` 의 Steam Cloud 블록은 "빈 스텁"이 아니다.** Auto-Cloud 글롭을 UE ini가 아니라 **Steamworks 파트너 사이트(App Admin > Cloud)** 에서 잡아야 한다는 것, 슬롯 파일명의 단일 출처가 `[FPSRSaveSettings]`라는 것, AppId 480으로는 구성이 불가하다는 것까지 이미 적혀 있다. **없는 것은 결정과 코드이지 조사가 아니다.**

---

## 2. `RunFlow.md` §2-11 요구 대비

§2-11 "저장 정책(P6)"이 요구한 6항목:

| 요구 | 상태 | 근거 |
|---|---|---|
| SaveData 버전 필드 + 마이그레이션 | ✅ | 사실표 2·4 |
| 슬롯명 규칙 | ✅ | 사실표 13(config 주도, 하드코딩 0) |
| 저장 실패 처리 | ✅ *부분* | 사실표 15 — 재시도·백업 보존은 있으나 **사용자 고지가 없다**(사실표 19) |
| 해금 데이터 삭제/리네임 fallback | ✅ | 사실표 17(주소 지정 seam만 — 언락 리스트 자체는 P0-③) |
| **Steam Cloud 대상** | ❌ | 사실표 21 |
| **런중 vs 로비 저장 구분** | ❌ | 사실표 9 — `Lobby` enum은 있으나 호출처 0 |

§2-11의 앞 3줄(누적 재화·업그레이드 트리·해금 / Run 시작 시 영구 스탯 GE 적용 / 통화 1종)은 **전부 미구현(P0-③)** 이다.

---

## 3. 갭 — 요구된 3축

### 3-1. 런중 세이브 정책 ❌

- 저장은 **`EndRun` 1회뿐**이다. 중간 체크포인트도, 로비 진입 저장도 없다.
- `EFPSRSaveReason::Lobby` 는 정의만 되고 **호출처가 0**이다 — 헤더 주석이 스스로 *"런중 vs 로비 저장 구분"* 을 목적으로 든 enum인데 그 구분이 실제로는 없다.
- `Deinitialize()` 는 델리게이트 해제만 하고 **pending 저장을 flush하지 않는다**(`FPSRSaveGameSubsystem.cpp:31-35`) → 비동기 쓰기가 실패한 채 게임을 종료하면 그 저장은 유실된다.
- **결과**: 호스트 크래시·Alt+F4·전원 차단 = 그 런 전부 소실. 30~60분 런이면 EA 리뷰에서 바로 문제가 된다.

**M5 결정 필요**: 체크포인트 지점(미션 클리어? 보스 처치? N분?) · 로비 진입 저장 도입 여부 · 종료 시 동기 flush 여부(비동기 쓰기 중 종료 방어).

### 3-2. Steam Cloud 슬롯 ❌

- 코드 0. `DefaultEngine.ini` 에 **경로·방법은 이미 조사돼 있다**(위 ℹ️).
- **실앱 ID 전환이 선행**이다 — `SteamDevAppId=480`(Spacewar 공용 테스트 앱)으로는 파트너 사이트 Cloud 구성 자체가 불가능하다. → M5의 "Steam 실앱 ID 전환"과 **같은 슬라이스에서 순서를 걸어야 한다**.

**M5 결정 필요**: **Auto-Cloud(파일 글롭) vs `ISteamRemoteStorage` API** · Root/Path 글롭 값 · **충돌 해결 정책**(두 PC에서 오프라인 플레이 후 동기화 시 어느 쪽을 남기는가).

### 3-3. 손상 복구 🟡 부분

**있는 것**: 백업 슬롯 · 3단 폴백 · 실패 시 백업 보존 · 다음 맵 로드 재시도.
**없는 것**:
- **체크섬/무결성 필드** — 손상 판정이 "역직렬화가 실패하는가"뿐이라, **파싱은 되는데 값이 깨진** 부분 손상은 그대로 통과한다.
- **torn-write 방어** — 쓰기 중 전원 차단 시 프라이머리가 반쯤 쓰인 상태로 남을 수 있다(임시 파일 → 원자적 rename 패턴 없음).
- **사용자 고지** — 백업에서 복구했다는 사실도, 신규 default로 리셋됐다는 사실도 `UE_LOG` 로만 남는다.

**M5 결정 필요**: 체크섬 도입 여부(비용 대비) · 원자적 쓰기 패턴 · 복구/리셋 시 UI 고지 문안.

---

## 4. 갭 — 추가로 발견한 5축

### ① ⭐ 스키마 자체가 공백 — **최대 갭**

저장 필드가 `SaveVersion` + 중립 `Reserved0` **2개뿐**이다. `RunFlow.md` §2-11이 약속한 재화·업그레이드 트리·해금은 **전부 P0-③**이다.
그래서 **EA 최대 갭은 "정책"이 아니라 "저장할 게 아직 없다"** 이고, 스키마가 확정돼야 (3-1) 체크포인트 지점, (3-2) Cloud 글롭·충돌 정책, (3-3) 무결성 대상이 정의된다. `MigrateIfNeeded()` 의 `case 0` 은 **실효 마이그레이션을 한 번도 겪지 않았다.**
→ **소관 = M3(메타 프로그레션 실물화).** M5의 4항목은 M3에 선행 의존한다.

### ② ⚠️ `UserIndex=0` + 단일 슬롯의 전제가 틀렸다

`FPSRSaveSettings.h` 주석과 `DefaultGame.ini` 주석이 근거로 든 문장:

> *"계정 시스템 없음 · Steam=머신당 단일 유저"*

**엔진 실물이 이를 반박한다.** `Engine/Source/Runtime/Engine/Public/SaveGameSystem.h:169-171`:

```cpp
virtual FString GetSaveGamePath(const TCHAR* Name)
{
    return FString::Printf(TEXT("%sSaveGames/%s.sav"), *FPaths::ProjectSavedDir(), Name);
}
```

**`UserIndex` 는 경로에 전혀 들어가지 않는다.** `FGenericSaveGameSystem`(Windows 기본 구현)에서 세이브 파일은 `<ProjectSavedDir>/SaveGames/<Slot>.sav` 하나이고, **Steam 계정과 아무 관계가 없다.**
→ 같은 설치본을 쓰는 **공유 PC · Family Sharing · 같은 프로필에서의 Steam 계정 전환** 시 두 사람의 세이브가 **서로 덮인다.**
→ **Cloud를 붙이면 이 충돌이 클라우드까지 전파되므로, 3-2보다 먼저 판정해야 한다.**

**M5 결정 필요**: 단일 슬롯 유지(+ 그 한계를 스토어 페이지에 명시) vs Steam ID 기반 슬롯 접미사 도입.

### ③ 저장 실패가 플레이어에게 보이지 않는다

`OnSaveComplete` 는 **구독자가 0**이다(`FPSRSaveGameSubsystem.cpp:122` 에서 Broadcast만 한다). 델리게이트 주석 자신이 *"UI / systems can react (e.g. a 'saving…' spinner or a save-failure toast)"* 라 용도를 적어 뒀는데 그 UI가 없다.
실패는 `UE_LOG(Error)` 로만 남는다 → EA 리뷰의 **"진행이 사라졌다"** 가 정확히 여기서 나온다.

### ④ 세이브 삭제 / 새 게임 경로 부재

`DeleteGameInSlot` 이 셰이핑 코드에 **0건**이다. EA에서 통상 요구되는 **"진행 초기화"** 가 없고, 손상 세이브를 플레이어가 스스로 치울 수단도 없다(수동으로 `.sav` 를 찾아 지우게 된다).

### ⑤ 런 종료 직후 이탈 클라의 보상 유실

`ClientCommitMetaSave` 는 Reliable RPC **1발**이다. 런 종료와 post-run ServerTravel 사이에 접속이 끊긴 클라이언트에게는 이 RPC가 **도달하지 못한다.**
재시도 훅 `HandlePostLoadMap` 은 **`bSavePending == true` 일 때만** 동작하는데, RPC가 도달하지 않았으면 `RequestSave` 가 호출된 적이 없어 `bSavePending` 이 애초에 `false` 다 → **재시도가 걸리지 않는다.**
→ 리텐션 지표(§1-C-8 D1 35% / D7 15%)에 직결된다. 4인 협동에서 "마지막 순간에 튕겼더니 보상이 없다"는 반복 재현 가능한 시나리오다.

**M5 결정 필요**: 서버가 결과를 잠시 보관했다가 재접속 시 재발급할지 / 클라가 런 중 보상을 낙관적으로 누적해 두고 종료 신호 없이도 저장할지.

---

## 5. M5 이관 요약

| 갭 | 축 | 선행 |
|---|---|---|
| Steam Cloud 슬롯 정책 | 3-2 | **실앱 ID 전환** + 추가 ②(슬롯 네임스페이스 판정) |
| 런중 세이브 정책 | 3-1 | 추가 ①(스키마, M3) |
| 손상 복구 보강 + 저장 실패 고지 | 3-3 · 추가 ③ | — |
| 세이브 삭제 / 새 게임 | 추가 ④ | — |
| `UserIndex`·단일 슬롯 전제 재검토 | 추가 ② | — (Cloud보다 **먼저**) |
| 런 종료 이탈 클라 보상 유실 | 추가 ⑤ | 추가 ①(보상이 스키마에 실려야 의미) |
| ~~저장 스키마 실물화~~ | 추가 ① | **M5 아님 = M3 소관** |

---

## 6. 재현 명령어

```sh
grep -rn "RequestSave\|ClientCommitMetaSave\|OnSaveComplete" Source/ --include=*.cpp --include=*.h
grep -rn "EFPSRSaveReason::Lobby" Source/                    # → 0
grep -rn "DeleteGameInSlot" Source/                          # → 테스트 2건뿐
grep -rn "UserCloud\|IOnlineUserCloud\|RemoteStorage\|CloudSave" Source/ Config/   # → 0
grep -rla "PlayerMeta\|RogueliteSaveGame\|SaveGameToSlot" Content/                 # → 0
sed -n '160,175p' /d/UnrealEngine/UE_5.7/Engine/Source/Runtime/Engine/Public/SaveGameSystem.h   # UserIndex 미사용
```
