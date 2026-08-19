# 작업 이력 (WorkLog) — 완료된 것만

> `PROGRESS.md`는 **지금 진행 중인 것만** 담는다. 끝난 작업은 여기로 내려온다.
> 커밋 단위 상세는 `git log --oneline`, 구조 결정의 근거는 `Docs/Architecture/*.md`,
> 증상→원인→해결은 `Docs/Troubleshooting.md`.
>
> **아래는 위에서부터 최신순.** 각 항목은 완료 당시의 기록을 그대로 보존한다(요약하지 않음 —
> 여기 적힌 실측치·기각안·함정이 나중에 같은 논의를 반복하지 않게 막아 준다).

---

## 🧾 총알이 억제기·문을 그냥 통과하던 것 — 파괴물 전용 콜리전 채널 (2026-08-19, `phase/arena-spawnpoints`, `d49998f2`)
> 보드 행 = *"파괴물 전용 콜리전 채널 신설 — 투사체가 억제기/문을 관통하는 결함 수정"*(M0 · 하이 · S · 추천모델 Fable, 수행 Opus).
> 조사 중 드러난 사실 하나 = 이 행은 기존 *"PIE 검증 — 스테이지 전환·그레이스 창·파괴물"*(M0 · 하이) 행의
> **미등록 선행조건**이었다. 그 행의 검증 항목 6개가 전부 "억제기를 총으로 부숴 전환을 트리거"를 전제로 하는데,
> 억제기가 안 부서지니 6개 다 실행 자체가 불가능했다. 선행 관계를 보드에 등록했다.

증상 = 억제기(BP_Inhibitor)를 쏘면 부서지지 않고, **반대편에 있는 적이 맞는다.**
사용자 최초 진단은 "기본 Cube 인데 충돌처리가 안 된 것 같다"였다.

### 🪤 콜리전은 멀쩡했다 — 총알 쪽이 파괴물을 "벽"에서 빼고 있었다
에디터 월드에서 억제기를 관통하는 Visibility 라인트레이스를 직접 쏴 봤다:
`bBlockingHit=True`, 거리 650, 컴포넌트 `BP_Inhibitor_C_1.DestructibleMesh`. **콜리전은 살아 있다.**
BP CDO와 배치 인스턴스 둘 다 QueryOnly / 전 채널 Block / Cube 메시 정상. 즉 에셋 쪽엔 아무 문제가 없었다.

진범은 **"데미지를 받으려고 빌려 쓴 오브젝트 채널"** 이었다. `AFPSRArenaDestructible`/`AFPSRDoor` 의 메시는
오브젝트 타입을 `ECC_FPSRPlayerPawn` 으로 쓴다 — 무기의 폰 오브젝트 쿼리에 걸려서 **새 코드 없이 데미지를
받으려는** 설계다(`FPSRArenaDestructible.h` 주석에 그 전제가 그대로 적혀 있었다: *"gathered by every weapon
object-query"*). 그리고 오버랩 이벤트는 꺼 둔다(`SetGenerateOverlapEvents(false)`).

**전 무기가 히트스캔이던 시절엔 이 조합이 성립했다.** 오브젝트 타입 라인트레이스는 씬 쿼리라
오버랩 이벤트 플래그를 아예 보지 않기 때문이다. 전 무기가 투사체로 바뀌면서 두 경로가 동시에 죽었다.

| 경로 | 왜 죽었나 |
|---|---|
| 블로킹 히트 (`OnSphereHit`) | `AFPSRProjectile::Activate` 가 `ECC_FPSRPlayerPawn` 에 **Overlap** 응답만 준다. 총알을 멈추던 건 `ECC_WorldStatic` 하나뿐이었다 |
| 오버랩 이벤트 (`OnSphereOverlap`) | 엔진이 **양쪽 컴포넌트 모두** 플래그가 있어야 보낸다 (`PrimitiveComponent.cpp:2972` — *"Both components must set GetGenerateOverlapEvents()"*). 파괴물이 꺼 놨다 |

→ 블록도 없고 오버랩도 없음 = **완전 관통.** 게임 내 **모든** 파괴물이 총으로 파괴 불가였고,
ADR 0010 D6/D7 의 "억제기 파괴 → 스테이지 전환" 루프 전체가 죽어 있었다. 문이 그나마 엄폐물로 보인 건
`FrameMesh` 가 `ECC_WorldStatic` 이라 **문틀**이 막아 줬기 때문이고, 문짝 자체는 못 부쉈다.

### 이미 알고 있었는데 우회해 둔 흔적이 있었다
`FPSRRangedEnemyBase::HasLineOfSight` 주석: *"적 투사체는 그 채널을 오버랩하고 AFPSRCharacter 만 적으로
치므로 문을 그냥 통과한다"*. **같은 결함을 알고도 고치는 대신 LOS 게이트로 덮어 둔 것.**
이번 수정이 그 우회의 근본 원인을 없앤다(LOS 게이트는 더 싼 1차 방어로 남긴다).

### 수정 — 파괴물을 pawn 채널에서 떼어 자기 채널로
`ECC_FPSRDestructible = ECC_GameTraceChannel3` 신설. **`DefaultResponse=Block`** 이라 기존 콜리전
프로파일을 하나도 건드리지 않고 전원(플레이어 캡슐·적 캡슐·트레이스)이 파괴물을 고체로 취급한다.

오브젝트 타입을 옮겼으므로 **파괴물을 찾아야 하는 쿼리를 전수 조사해 전부 새 채널을 추가**했다 —
히트스캔 / 차지레이저 / 근접 / `ApplyExplosion`(바주카 스플래시로 억제기가 부서지는 경로) /
**적 원거리 LOS**(빠뜨리면 적이 문·억제기 너머로 쏜다). 이걸 빠뜨리면 관통 버그가 다른 경로로 재발한다.

라인트레이스 두 경로(히트스캔·차지레이저)에는 별도 결함이 하나 더 있었다: 벽 거리를 구할 때
**폰 쿼리에 걸린 액터를 전부 무시 목록에 넣는데**, 파괴물도 거기 걸리니 벽 컷오프가 파괴물 너머로 흘러
뒤쪽 적까지 데미지가 샜다. `FPSRCombat::IsDestructibleGeometry` 로 파괴물만 무시 목록에서 제외했다.
같은 이유로 `WallDist` 비교에 `KINDA_SMALL_NUMBER` 톨러런스를 줬다 — **파괴물 자기 항목이 정확히
`WallDist` 에 걸리므로**, 맨 `>` 로는 float 잡음에 자기 데미지가 조용히 누락될 수 있고 그 실패는
"안 부서진다"로만 보인다.

관통(`ProjectilePierce`)은 파괴물에 **적용하지 않는다.** 관통은 대(對)폰 스탯이고 파괴물은 벽 그 자체라,
저격총(`Pierce=2`)도 억제기 앞에서 멈춘다.

### 검증
- UBT `-DisableUnity` 풀빌드 `Result: Succeeded` (에러 0 / 경고 0, 376초).
- **대조군**: 수정 전 `BP_Inhibitor.DestructibleMesh` objtype = `ECC_PLAYER_PAWN(14)`,
  수정 후 CDO·배치 인스턴스 **둘 다** `ECC_DESTRUCTIBLE(16)`. 채널 등록과 **BP 오버라이드 부재**를
  한 번에 확인한 것 — 네이티브 컴포넌트라 BP 가 `BodyInstance` 를 오버라이드해 저장했으면 C++ 변경이
  안 먹는데, 값이 16 으로 나온 것이 그 반증이다.
- PIE: 억제기가 파괴되고, 반대편 적이 더는 맞지 않음(사용자 확인).

### 남은 것
`Durability` 기본 50 은 라이플 5발(0.5초)이라 여전히 낮다. ADR 0010 에서 억제기 체력은 "일찍 부수려 할수록
비싸다"의 **비용 축**인데 50 은 사실상 0 이다. 밸런스 값이라 사용자 결정 사항으로 남긴다.

---

## 🧾 총구 화염이 안 꺼지던 것 — 발사마다 남는 immortal 파티클 (2026-08-19, `phase/arena-spawnpoints`, `6ffcfff0`)
> 보드 행 = *"총구 화염 immortal 파티클 누수 — PS_Gunshot_Repeating 발사마다 PSC 잔존"*(M2 · 하이 · S · Opus).
> **§6-9 (8) M0 개시순서 예외 ③으로 당겨 썼다** — 남은 M0 Exit Criteria ①이 *"적 300/500 정량 성능
> 베이스라인 실측"*인데, 사격이 들어간 어떤 측정도 이 누수에 오염된다. 먼저 막지 않으면 그 수치 자체가
> 성립하지 않는다. M0 EC ① 측정은 이제 이 오염원에서 자유롭다(근거 = 이 커밋).

증상 = 라이플을 쏘면 총구 화염과 연기가 사라지지 않고 계속 나온다. 단서는 로그 한 줄이었다:
`LogParticles: ... spawned potentially immortal particle system! ... (PS_Gunshot_Repeating)` — PIE 한 세션에 **85건**.

### 🪤 immortal 경고 ≠ 누수. 게이트가 둘이고, 갈리는 지점은 SpawnRate다
엔진 판정이 두 군데로 나뉘어 있고, **경고를 내는 쪽과 실제로 죽이는 쪽이 다르다.**

| 게이트 | 조건 | 결과 |
|---|---|---|
| `UParticleSystem::bIsImmortal` (`ParticleSystem.cpp:236-268`) | `EmitterLoops==0 && EmitterDuration==0` | **경고만** 낸다 (`GameplayStatics.cpp:1378`) |
| `FParticleEmitterInstance::CheckEmitterFinished` (`ParticleEmitterInstances.cpp:881`) | `ActiveParticles==0` && 마지막 버스트 지남 && `GetMaximumSpawnRate()==0` | `bEmitterIsDone` → `bAutoDeactivate`(기본 true) → 완료 → `bAutoDestroy` |

T3D 덤프 실측(둘 다 에미터 = `FX_MusketFire` + `FX_MusketSmoke`):

| | `PS_Gunshot_Repeating` | `PS_Gunshot_Single` (수정 전) |
|---|---|---|
| EmitterLoops / EmitterDuration | 0 / 0.0 | 0 / 0.0 |
| **SpawnRate** | **8/초** | **0** (버스트 전용) |

→ **둘 다 immortal 플래그가 켜져 있었다.** 차이는 SpawnRate 하나뿐인데, 그것 때문에 `_Repeating`은
`ActiveParticles`가 0이 되는 순간이 없어 `bEmitterIsDone`이 영원히 안 서고 → 자동 비활성화가 안 걸리고 →
`bAutoDestroy`가 발동하지 않는다. **총 한 발 = `UParticleSystemComponent` 하나가 폰에 영구 잔존**, 각자
초당 화염 8 + 연기 8을 계속 뿜는다. `_Single`은 우연히 정리될 뿐, 누가 SpawnRate를 올리면 즉시 같은 지뢰가 된다.

**여기서 성급히 결론냈으면 틀렸다.** "이름이 `_Repeating`이니 `_Single`로 바꾸면 끝"이 첫 가설이었는데,
`_Single`도 immortal이라 그것만으로는 **빨간 온스크린 경고가 그대로 남고** 잠재 지뢰도 남는다.
`_Single`을 유한화(`Loops=1`/`Duration=0.1`)해야 비로소 닫힌다.

### 배정 자체가 틀렸다
팩이 의도한 `_Repeating`의 용법 = *격발 유지 동안 한 번 Activate → 놓으면 Deactivate*. 그런데 이 프로젝트는
`PlayWeaponFireCosmetics`/`MulticastFireCosmetics`에서 **발사마다 `SpawnEmitterAttached`로 스폰하고 버린다**
(`bAutoDestroy=true`). 루프형 에셋을 일회성 경로에 꽂은 것. Rifle/SMG/LMG/ChargeLaser 4종이 그랬고,
Bazooka/Shotgun/Sniper 3종은 처음부터 `_Single`이라 멀쩡했다.

### 수정
- **무기 DA** — Rifle/SMG/LMG → `PS_Gunshot_Single`. **ChargeLaser는 None**(화약 총기가 아니므로 —
  사용자 결정, 레이저 VFX는 M2 *"VFX 전투 4종"* 행에서 별도 저작).
- **`PS_Gunshot_Single`** 두 에미터 Required 모듈 — `EmitterLoops 0→1`, `EmitterDuration 0.0→0.1`.
  버스트는 시간 0이라 그대로 터지고, Duration은 *스폰 창*만 제한하므로 연기는 제 수명(0.5~1.5초)을 다 산다.
- **`UFPSRWeaponDataAsset::IsDataValid`** — `MuzzleFlash`가 `IsImmortal()`이면 ERROR. 엔진은 이 상황을
  `Log` 한 줄로만 알려주므로 데이터 단계에서 막는다. `MuzzleFlash`는 이 프로젝트의 **유일한**
  `UParticleSystem` 프로퍼티라(스폰 지점도 위 두 곳뿐) 이 검사 하나로 노출면 전부가 덮인다.

### 기각·보류
- **`_Repeating`을 살리는 안**(격발 시작/종료를 코스메틱에 노출해 Activate/Deactivate로 구동) — 기각.
  발사마다 스폰하는 현 구조에서는 불가능하고, 별도 상태 배선이 필요하다. LMG의 "연속으로 타오르는 총구
  화염"을 원하면 그때 별건으로 세운다.
- **PSC 풀링(`EPSCPoolMethod::AutoRelease`)** — 보류. 발사당 UObject 생성/등록 제거(4인 × ~10발/초 ≈
  초당 40개)로 제1원리(액터당 비용 최소화)에 맞지만, **이번 버그와 무관하고** 풀 재사용 상태라는 별도
  리스크면이 있어 커밋에 섞지 않았다. 백로그 후보.
- **검증을 Warning으로 낮추는 안** — 기각. 쿠킹/커맨드릿 검증을 통과해버려 다시 새어나간다.

### 검증 (전부 실측, 대조군 포함)
- UBT `Result: Succeeded`, 컴파일 에러 0, 602초.
- **검증기 대조군** — 고치기 전 무기 DA 전수 검증에서 `MuzzleFlashImmortal` **정확히 3건**
  (SMG/LMG/ChargeLaser, 원인 에셋 `PS_Gunshot_Repeating`), 이미 고쳤던 Rifle + 나머지 6종 미발생.
  수정 후 9종 재검증 **0건**. → "에러 0"이 *통과*인지 *검사가 안 도는 것*인지 구분됐다.
- `PS_Gunshot_Single` T3D 재덤프 — 두 Required 모듈 모두 `Loops=1` / `Duration=0.1` (수정 전 `0` / `0.0`).
- PIE(`L_Arena`) — `potentially immortal` **0건**(수정 전 85건), 사격 중단 후 화염·연기 소멸.

### 🔓 조사 기법 — protected 프로퍼티는 T3D로 읽는다
Cascade는 Python에 **클래스 자체가 없고**(`unreal.ParticleModuleRequired` = AttributeError),
`UParticleSystem::Emitters`는 protected라 `get_editor_property`가 거부한다. 그래서 위 표의 값들을 읽을
방법이 없어 보였는데, **`unreal.Exporter.run_asset_export_task`로 T3D를 뽑으면 전부 평문으로 나온다**
(C++ 리플렉션이라 Python 노출 여부와 무관). 수정 전/후 덤프 대조로 값이 실제로 들어갔는지 판정하는 데도 썼다.
⚠️ T3D는 **CDO 기본값과 다른 것만** 쓴다 — 항목이 안 보이면 *없다*가 아니라 *기본값이다*
(`EmitterLoops` 미출력 = 기본값 0 = 무한 반복. 이걸 모르면 정반대로 읽는다).

---

## 🧾 M0 EC ② 닫힘 — BP 인라인 Text 이관 + 고아 위젯 정리 (2026-08-19, `phase/m0-ec2-editor`, 머지 `1c646085`)
> 보드 행 = *"EC ② 잔여 — BP 그래프 핀 3곳 + 고아 WBP 2 (에디터)"*(M0 · 로우 · S · Opus).
> **§7-6 M0 EC ② 가 이 행으로 종료됐다.** 같은 날 닫힌 C++ 축(`c485b68e`)과 합쳐 하드코딩 UI 문자열 = 0.
> M0 잔여 Exit = **EC ① 뿐**(성능 정량 베이스라인 — VAT-3 차단의 연쇄).

### 착수 전 열거가 틀렸다 — 3곳이 아니라 7건
보드·SSOT 모두 *"BP 그래프 핀 3곳"* 이라 적고 있었으나, 실물은 **위젯 트리 Text 3 + 그래프 핀 4**였다.
둘은 **편집 수단이 다르다** — 이걸 먼저 갈라 재지 않으면 되는 것까지 사람 손으로 하게 된다.

| 위치 | 대상 | 키 | 수단 |
|---|---|---|---|
| 위젯 트리 Text | `WBP_DownedOverlay.DownedText` · `.ReviverText` · `WBP_MissionBanner.BannerText` | `HUD.Downed.WaitingForAlly` · `HUD.Downed.AllyReviving` · `HUD.Mission.BannerLabel` | **Python 가능** |
| 그래프 핀 | `WBP_Result` `SetText(In Text)`×2 · `WBP_DownedOverlay` `Select Text(A/B)` | `Menu.Result.Victory` · `Menu.Result.Defeat` · `HUD.Downed.WaitingForAlly` · `HUD.Downed.Reviving` | **에디터 UI 전용** |

`WBP_RunHUD` 의 Select `Combat`/`Boss` 는 **대상 아님 확정** — `Combat` 은 바이너리에 부재하고, `Boss` 단일 히트는 기존 키 `HUD.Boss.NameLabel` 문자열의 부분일치였다(2026-08-13 메모의 *"현재 바이너리에서 안 나온다"* 가 옳았다).

### 🪤 Python 이 닿는 경계 (다음 세션이 제일 먼저 알아야 할 것)
- **닿는다** — 위젯 트리 Text 프로퍼티. `WidgetTree` 는 `get_editor_property` 로는 못 받지만
  **서브오브젝트 경로로는 로드된다**: `unreal.load_object(None, "<pkg>.<asset>:WidgetTree.<WidgetName>")`.
  위젯 이름은 uasset 이름테이블에서 뽑아 후보를 시도하면 잡힌다.
  값 주입 = `unreal.TextLibrary.text_from_string_table("UI", key)` → `set_editor_property("text", …)`.
- **안 닿는다** — 그래프 핀 기본값. `EdGraphNode::Pins` 가 **protected** 라 노드 객체는 잡혀도 핀이 안 나온다.
  `EdGraph.Nodes` 도 protected. `manage_asset` 도 에셋 단위 작업(search/find/open/save/duplicate/move/delete)만 제공한다.
- 그래서 **7건 중 3건은 자동, 4건은 사람 손**이었다.

### 🪤 `To Text (String)` 은 오답이다 (실사고)
중간 시도로 그래프 핀에 `To Text (String)` 노드를 놓고 `In String` 에 키 문자열을 적는 형태가 나왔다.
그건 `Conv_StringToText` 라 **문자열을 글자 그대로 FText 로 바꿀 뿐 테이블 조회를 하지 않는다** —
컴파일하면 화면에 `승리` 대신 **`Menu.Result.Victory`** 가 그대로 뜬다.
→ 검증 지표로 **바이너리에 `Conv_StringToText` 0건**을 쓰면 확실하게 잡힌다(실제로 이걸로 확인했다).

### 왜 핀 참조인가 (Advanced 노드를 기각한 근거)
`KismetTextLibrary.h:316` 이 직접 권고한다 — *"문자열 테이블 참조는 텍스트 프로퍼티나 **핀에 직접** 설정하는 쪽을 선호하라, 훨씬 견고하다"*.
`Make Text from String Table (Advanced)`(`TextFromStringTable`)는 조회 실패 시 **dummy text 를 돌려주는 조용한 실패**라 키 오타가 런타임까지 간다. 핀 참조는 에디터가 검증한다.

### 🔑 `UStringTable` 에셋 0개인데 UMG String Table 참조가 성립하는 이유
테이블 id `UI` 를 `LOCTABLE_FROMFILE_GAME` 이 모듈 시작 시 등록하기 때문이다 —
실측 `StringTableLibrary.get_registered_string_tables()` = `["UI","CardEffect","Card"]`.
그리고 **CSV sync 직후 에디터 재시작 없이 새 키 6개가 즉시 LIVE** 였다(런타임 CSV 로더가 다시 읽는다).
⚠️ 키를 꽂기 전 `is_registered_table_entry` 로 등록을 확인할 것 — 미등록 키는 **빈 문자열 회귀**로 조용히 끝난다.

### 고아 위젯 2개는 에디터가 아니라 파일 삭제로
`WBP_PlayButton`·`WBP_ReturnButton`. 삭제 전 **`Content` 10개 루트의 `.uasset`/`.umap` 전수 + `Source` + `Config` 를 ASCII·UTF-16 양쪽**으로 세어 참조 0을 확인했다(인코딩 가정 하나로 "참조 없음"을 단정하면 삭제 사고가 난다).
리다이렉터는 rename/move 에서 생기고 delete 에서는 안 생기므로 잔재도 없다.

### ⚠️ 구조적 발견 — EC ② 의 "기계적 정의"는 절반만 성립한다
`Config/Localization/Game_Gather.ini` 에 **`GatherTextFromSource` 단계만 있고 패키지(에셋) 수집 단계가 없다.**
그래서 EC ② 가 근거로 삼은 *"검사 대상 = `Game.manifest` 수집분"* 은 **C++ 에만 성립**하고, 조항이 명시한
*"BP 위젯의 인라인 Text 프로퍼티"* 는 **원리적으로 탐지되지 않는다.** 이번 BP 축은 수동 열거 + 바이너리 실측으로 닫았고,
새 BP 리터럴을 막을 자동 검사는 없다. → **보드 백로그 행 등록**(켜면 "표시되는 것만 센다"로 제외 합의한 디자인타임 플레이스홀더 ~10건과 보류 중인 `WBP_Lobby` 15건이 전부 올라오므로, 켜는 것 자체가 별도 설계 판단이다).

### 검증
바이너리 실측(대상 리터럴 7건 전부 ASCII·UTF-16 양쪽 **0** · 키 결선 확인 · `Conv_StringToText` **0** · `WBP_Result` 65,092 → 60,692 바이트) ·
6키 `get_table_entry_source_string` 실값 반환 = dummy 아님 · 3위젯 재컴파일 + `generated_class` 생성 ·
gather **manifest 94 → 100**(`UI` 25 → 31, 6키 전부 수집, C++ LOCTEXT 네임스페이스 0 유지) · **6키 × 3언어 18/18 번역 주입**(아카이브 직접 파싱) ·
헤드리스 스모크 **5/5 Success** · 레드팀 게이트 **미적용**(§6-6-1 — 콘텐츠·BP배선 갈래).

### 커밋
`9fde733a` 고아 WBP 2 삭제 · `76354ff5` ST_UI 6키 + 트리 3건 · `f4bdc351` 그래프 핀 4건 · `42c669f7` gather 산출물 · `1c646085` 머지

---

## 🧾 M0 EC ② — C++ 하드코딩 문자열 잔여 5 → 0 (2026-08-19, `phase/m0-ec2-cpp-loctext`, 머지 `c485b68e`)
> 보드 행 = *"EC ② 잔여 — C++ LOCTEXT 5건 + Map_CyberCity 주석 2줄 (빌드 동반)"*(M0 · 로우 · S · Opus).
> **EC ②의 C++ 축이 닫혔다.** 남은 EC ② 잔여 = 에디터 행(BP 그래프 핀 3곳 + 고아 WBP 2)뿐.

### 판정을 먼저 기계화했다
착수 전에 `Content/Localization/Game/Game.manifest` 를 네임스페이스별로 세어 기준선을 박았다 —
**총 98엔트리 = `Card` 52 / `CardEffect` 16 / `UI` 25 / `FPSRBossDefinition` 4 / `FPSRCardEffect` 1.**
앞 셋은 StringTable 경유(정상)고 **뒤 둘이 C++ LOCTEXT 잔여 5건**이다. 그래서 EC ②는
*"뒤 두 네임스페이스가 소멸하고 `CardEffect` 가 17이 되면 닫힌다"* 는 **대조 가능한 명제**가 됐다.
최종 = **94 = `Card` 52 / `CardEffect` 17 / `UI` 25**, 두 네임스페이스는 manifest·ko/en/ja archive
**전부에서 소멸**. 신규 키 수집·번역 주입도 직접 파싱해 확인(ko `무기` / en `Weapon` / ja `武器`).

### ① Boss `GetDescription()` 4건 — 이관이 아니라 **가드**가 답이었다
헤더가 *"One-line summary for designer tooling / catalog"* 라 **에디터 도구용이라 선언해 놓고**
`#if WITH_EDITOR` 밖에 있어 쿠킹 빌드에 실리고 gather에 잡혔다. 같은 파일의 `IsDataValid` 는
제대로 가드돼 있어 **대조군이 파일 안에 있었다**. 에디터 전용 문자열은 번역 대상이 아니므로
StringTable로 옮기는 게 아니라 **수집 대상에서 빼는 것**이 옳다 → CSV·gather 불필요.
🪤 **`UFUNCTION(BlueprintPure)` 를 가드로 감싸면 BP 호출자가 있을 때 쿡 빌드가 깨진다** —
선확인 필수였고 실측 = `Source/` 0 · 프로젝트 소유 BP 디렉터리 15곳 0 · `Content/Python`·`Tools` 0
= 호출자 전무. 엔진 선례 = `PrimitiveComponent.h:1487` `GetEditorMaterial`.

### ② `FPSRCardEffect.cpp:356` 1건 — 시트가 먼저다
`LOCTEXT("UnknownWeapon","Weapon")` 폴백. 바로 다음 줄은 이미 `LOCTABLE("CardEffect","Fmt.UnlockWeapon")`
인데 **인자만 리터럴**로 남아 있었고, 이 함수는 `FPSRCardEntryWidget.cpp:82` 가 소비하는
카드 선택 UI 경로다(에디터 전용이 아니라 진짜 노출).
🪤 **코드부터 고치면 안 된다.** `Localization.md` §L-5(*"시트=마스터, 단방향, 양방향 금지"*)·§50
(*"편집은 항상 구글 시트에서"*)이라 리포 CSV는 **빌드 스냅샷**이고, 손편집하면 다음 sync에 덮인다.
키 없이 코드만 바꾸면 **카드 UI에 빈 문자열 회귀**가 난다. 순서 = 사용자가 시트 `ST_CardEffect` 에
`Fallback.UnknownWeapon,무기,Weapon,武器` 행 추가 → `sync-authoring-csv.ps1 -SheetName ST_CardEffect`
(정확히 +1행, 다른 시트 무접촉) → 코드 1줄 교체 → 재-gather.

### ③ 곁다리 — 삭제된 맵을 예시로 든 주석 2줄
`FPSRFlowFieldBoundsVolume.h:31` · `FPSRFlowFieldSubsystem.h:22` → `L_Map1_City`(`dca19b4e` 이후 현행).

### 🪤 이번에 검증이 거짓 통과할 뻔했다 (둘 다 `Docs/Troubleshooting.md` 로 내림)
1. **`G10` — 헤드리스 스모크가 exit 0인데 아무것도 안 돌았다.** PS 5.1에서 `-abslog=$log` 를
   따옴표 없이 넘기면 `UnrealEditor-Cmd` 가 아예 뜨지 않는데 `$LASTEXITCODE` 는 **0**이고 stdout도
   비어 있고 `Saved/Logs/` 에도 새 파일이 없다. **로그 부재는 실패가 아니라 "안 돌았다"** 이고 둘은
   구분해야 한다. 해결 = `Scripts/validate-data.ps1` 처럼 `=` 든 인자를 통째로 따옴표 + `-log`,
   그리고 판정은 로그의 `Result={Success}` 와 `N tests performed` 로.
2. **`H7` — `Game.manifest` 는 UTF-16LE.** 첫 grep이 대상 5키에 대해 에러 없이 **조용히 0건**을 냈다.
   그대로 믿었으면 *"이미 정리됨"* 으로 오판했을 자리다. UTF-8 변환 후 JSON 파싱이 정답.

### 검증
`Build.bat FPSRogueliteEditor Win64 Development -MaxParallelActions=2` 2회 = **Succeeded**(실에러·경고 0).
UHT가 `-WarningsAsErrors` 로 통과 = `#if WITH_EDITOR` 안 `UFUNCTION` 배치 정상.
헤드리스 스모크 **5/5 Success**(`Smoke.ModuleLoads`·`SaveGame`·`GameplayMessage`·`CrosshairSettings`
+ `Editor.Localization.StringTableCsv`). 레드팀 게이트 = **미적용**(§6-6-1 — 코어 갈래 T1~T5 아님).

### 병렬 트랙 메모
`FPSRFlowFieldSubsystem.h` 는 `phase/arena-topology`(당시 미머지 26커밋)도 +25줄 수정 중이었다.
이 트랙의 헌크는 L22 주석, arena 쪽은 L4·L73+ 라 auto-merge가 확실해 그대로 진행했다.

---

## 🧾 M0 기준선 정정 — 비-빌드/에디터 트랙 일괄 (2026-08-13, `docs/m0-baseline-reconcile`)
> 사용자 지시 = *"M0 작업보드 중 빌드·에디터 관련을 제외한 작업을 전부 처리"*. 보드 M0 활성 14건 중 **5건 착수 / 9건 미착수 유지**(빌드·에디터·사용자 PIE 의존).
> **닫힌 것 = EC ③·EC ④ + M0 (c) 표기 정정 + M0 (d) 세이브 잔여.** 코드 변경 0(문서·감사·보드만) → 빌드 불요, `git diff` 자기비판 경로(§6-7 3).

### 왜 이 셋이 빌드 없이 닫히는가
- **EC ③(`/Engine` 참조 감사)** — "쿡에서 탈락하는가"는 **엔진의 제외 조건 + 프로젝트 설정으로 정적 판정이 가능**했다. UE 5.7의 엔진 콘텐츠 쿡 제외는 `/Engine/Editor*`·`/Engine/VREditor` **두 접두사뿐**(`CookSavePackage.cpp:292`)이고, 게이트 `bSkipEditorContent` 가 `BaseGame.ini:113`에서 `false`이며 프로젝트가 오버라이드하지 않는다 → **현재 탈락 0건**. EC ③의 요구는 "감사 결과 문서화 + 쿡 탈락분 M2 등록"이므로 이걸로 충족된다.
- **EC ④(완료 표기 재대조)** — 전부 문서 수정.
- **M0 (d) 세이브 잔여** — 이미 "갭 *정의* 만" 으로 축소돼 있었다.

### 산출
- **정본 2건 신설**: [`Docs/Review/EngineRefCookAudit_20260813.md`](Review/EngineRefCookAudit_20260813.md) · [`Docs/Review/SaveSystem_EAGap_20260813.md`](Review/SaveSystem_EAGap_20260813.md) (둘 다 재현 명령어 수록)
- **정정 12건**: §7-3 4행 · §7-6 M0 (c)·EC ① 문구 · §8 7행 · 도메인 SSOT 5파일

### 🔎 재대조가 잡은 것 (실물 근거)
1. **§7-3 P2 "Significance"** — `USignificanceManager` 는 **미사용**(플러그인만 enable, `Source/` 참조 0). `Enemy.md`·`Architecture.md` 는 2026-08-11에 이미 정정됐는데 **이 표만 남아 있었다** = EC ④가 잡아야 할 정확히 그 드리프트.
2. **§7-3 P3 "정비시간 RunPhase"** — 그런 Phase가 없다. `ERunPhase` = `Combat`·`Boss` **2개뿐**. P4-A가 전역 프리즈로 흡수·폐지(2026-06-04)했는데 P3 행에 남았다.
3. **§7-3 P4-D "핑"** — `Source/` 에 `Ping` 참조 **0**. `PlayerFeel.md §2-14` 가 이미 *"핑/Gibs는 후속"* 이라 반박하고 있었다.
4. **§7-3 P8 "SMG 제거"** — 반대다. **SMG는 추가**됐고 제거된 것은 **점사총**이다(`3adc945` 커밋 원문 · `DA_Weapon_SMG` 실존).
5. **§7-6 M0 (c) "로컬라이제이션 0 / `Config/Localization` 부재"** — **사실과 반대.** 파이프라인이 이미 서 있고 Phase A 이관도 끝났다. 그대로 두면 **Exit 판정이 (c)를 미착수로 오판한다.**
6. **EC ① 문구 "인스턴싱/VAT가 적용된 상태"** — ADR 0007이 인스턴싱을 **기각**했으므로 문자 그대로는 **영원히 닫히지 않는 EC**였다 → "확정된 렌더 경로(CPD)"로 교정.
7. **§8 9행 중 7행이 스테일** — 해소 3(`FPSR.SpawnEnemies`·Input Warning·`LogUIActionRouter`) · 내용 스테일 4(적 메시는 큐브가 아니라 VAT BroBot이고 전환계획이 기각된 방안 / `ConstructorHelpers` 0건 / Infima 없음·Blu 3P 적용 완료 / 현행 맵 칸이 아직 `Map_CyberCity`) · 절반 해소 1.
8. 🆕 **`/Engine` 감사 신규 발견** — `BP_MissionPointSet` 의 Billboard `Sprite` → `/Engine/EditorResources/Spawn_Point`. `UBillboardComponent::Sprite` 는 `WITH_EDITORONLY_DATA` **밖**의 순수 런타임 프로퍼티이고(`BillboardComponent.h:23-24`) 이 BP는 `bIsEditorOnly` 미설정. **대조군 = 프로젝트 자체 `FPSRFlowFieldBoundsVolume.cpp:28` 은 제대로 설정한다.** → M2 등록.
9. 🆕 **세이브 `UserIndex=0` 전제 오류** — 주석이 *"Steam=머신당 단일 유저"* 라 단정하나, 엔진 `SaveGameSystem.h:169-171` 의 `GetSaveGamePath()` 는 **`UserIndex` 를 경로에 넣지 않는다**. 공유 PC·계정 전환 시 세이브가 상호 덮인다 → **Cloud를 붙이기 전에 판정할 것**.
10. **Phase A "전수 이관 완료"가 잔여를 과소 기록** — 실측 16건 중 `WBP_Lobby` 10 + C++ 5가 누락돼 있었다(아래 Phase A 항목의 정정 블록).

### 🪤 함정
- **`Content/` 감사는 LFS 실체 확인이 선행 조건이다.** 포인터만 받은 클론에서는 grep이 **조용히 0건**을 내 "깨끗하다"는 거짓 결론이 나온다. `git lfs ls-files | awk '$2=="-"'` 가 0인지 먼저 본다(이번엔 4,314/4,314 실체).
- **`strings` 금지, `grep -a -o` 사용** — 이름 테이블을 놓쳐 오판한다(기록된 실사고).
- **`grep -P` 는 이 환경에서 에러 종료**(`-P supports only unibyte and UTF-8 locales`). `2>/dev/null` 과 함께 쓰면 **깨끗한 거짓 0건**이 나온다.
- **`DirectoriesToNeverCook` 는 존재하지 않는 키다**(엔진·프로젝트 전체 0건). 이걸로 쿡 제외를 설명하면 틀린다.
- **이름에 속지 말 것** — `EngineDebugMaterials`·`MapTemplates`·`OpenWorldTemplate` 은 어떤 제외 규칙에도 안 걸린다(정상 쿡 대상).
- **서브에이전트 결과는 액면 그대로 믿지 않는다** — 감사 에이전트가 §8을 **8행이라 보고했으나 실제로는 9행**이었고(`적 추격 = 단순 스티어링` 누락), 그 행도 해소 대상이었다. 주요 수치·주장은 전부 메인 세션이 재실행해 재현했다.

### 이번 범위에서 제외 (보드 후속 행)
C++ 5건 LOCTEXT 정리(컴파일 필요) · 코드 주석 2줄의 `Map_CyberCity` 인용(헤더 변경 = 대규모 재컴파일) · WBP 인라인 Text 11 + BP 그래프 핀 + 고아 WBP 2(위젯 에디터) · `localization-gather.ps1` 재실행(커맨드렛).
**M0 잔여 EC = ①(성능 실측 — VAT 체인 대기) · ②(UI 문자열 16건 + 판정 경계 1건 결정대기).**

## 🤖 VAT-1 완료 — 스웜 렌더 경로 대조 실험 → CPD(경로 B) 채택 (2026-08-13, `phase/vat-renderpath-spike`)
> M0 **(a″) 분할 조각 1/4 완료** + 사실상 조각 2의 구현 본체까지 이 스파이크 안에서 끝남(VAT-2는 "정식화·잔여 검증"으로 스코프 교체, 사용자 승인).
> 정본: 결정 = `Docs/Architecture/0007-enemy-swarm-render-path-cpd.md` · 실측 = `Performance.md §5` · 컨설트 = `Docs/Review/20260812-plan-vat1-swarm-render-path.md`(Codex 4R, 기각 0).

- **경위**: 초안은 "ISM 채택"이었으나 컨설트 R2가 전제를 뒤집음("개별 컴포넌트=지배항"은 미측정 주장, 진범 후보=per-actor MID) → A(현행)/B(MID 폐기+CPD)/C(ISM, 조건부) 대조 실험으로 재편 → **B 전 게이트 통과·전 지표 A 우세 → 채택, C 미개봉**.
- **실측(Development 패키지·고정 시나리오)**: B@300 = 평균 5.48ms(182fps)·P95 7.99ms·스웜 렌더 합 2.05ms(예산 4ms) / A@300 = 6.48ms·2.68ms / B@500 = 7.11ms(141fps)·3.03ms. 사용자 실플레이 2판 육안 = 애니 정상·정상 작동.
- **스파이크 중 발견(중요)**: ①AnimToTexture에 "AnimationIndex" 파라미터 없음 — 클립=프레임 구간, 종전 MID 드라이버는 이름 불일치로 **무동작**이었다 → CPD 계약 4슬롯(Start/End/PlayRate/Phase)으로 확정 ②A가 애니된 이유 = 머티리얼 AutoPlay 기본값(상태 제어는 B만 가능) ③멜리 BP stencil=1 vs 랭드 0 콘텐츠 드리프트(별도 정리 대상).
- **낳은 수정**: `fix(hero)` grace 창 연장-만(ratchet) — 프리즈 해제 3s grace가 진행 중인 긴 grace(부활/디버그)를 단축하던 실버그(`3c8fb399`). 디버그 커맨드 `FPSR.Invuln`·`FPSR.SpawnEnemies [count] [radius]` 신설.
- **측정 인프라(재사용 — (b) 베이스라인이 그대로 씀)**: `Scripts/measure_swarm_render.ps1` + `Scripts/analyze_swarm_csv.py`. 함정 원장: 부트 캡처(-csvCaptureFrames)=RHI 초기화 전 어설션 즉사 / 아카이브 최상위 exe=부트스트랩(자식 게임 잔존→인스턴스 4개 누적 오염) / 근접 링 스폰=적이 카메라 근평면 뒤로 뭉쳐 "적 0마리 장면" 측정(CustomDepth 0.01ms가 단서) / UE CSV=끝 헤더+컬럼 증가+EVENTS 문자열 / **런처 엔진은 Test 구성 미지원** → Development로 측정(보수적).
- **커밋**: `08ebe184`(예산 확정) `5b40c2c7`(컨설트) `280c04f3`(CPD 백엔드) `1e1625b1`(CPD 콘텐츠) `426deefa`·`3c8fb399`·`766d3ecb`·`bd78e0ef`(측정 도구·grace).

## 🌍 Phase A — UI 문자열 전수 이관 완료 (2026-08-13, `phase/loc-ui-migration` → 머지 `f1b7e314`, 정합복원 `60a68015`)
> 보드 행 "문자열 외부화 파이프라인 + 기존 UI 전수 이관" **완료 마킹**. C++ 런타임 이관(A-1 `df62d400`) + 출처 풀 라벨(A-1b `d9b76d3c`) + WBP 23개 인벤토리(A-2 `5a3691a4`) + 사용자 에디터 작업(A-4 `3e23a33a`: 키 바인딩 15곳·버튼 WBP_Button_Base 통합·SourcePoolText·BonusShot 리네임). 사용자 PIE ko/en/ja 순회 정상.

### 🪤 함정
1. **CommonButtonBase는 루트 버튼 슬롯을 강제 Fill로 리셋**(`CommonButtonBase.cpp:495-497` — 컴파일마다 정렬 원복). 정렬은 콘텐츠 안쪽(Overlay 래핑)에서.
2. **WBP 상속 = 자식 트리가 부모 트리를 통째 대체**(부분 오버라이드 불가). 자식에 부모와 동명(FName 대소문자 무시) 위젯 변수 잔존 시 Internal Compiler Error("property already exists"). 공유 버튼 = Base 트리 + 인스턴스가 노출 프로퍼티(StringTable 키)만 지정.
3. **에디터 PIE 게임 텍스트 언어 = '미리보기 게임 언어'**(culture= 콘솔은 에디터 UI만) + **번역은 각 워킹트리에서 gather를 돌려야 LocRes가 생긴다**(브랜치에 키만 있고 gather 안 돌리면 어느 언어로도 안 바뀜).
4. **worktree 브랜치가 오래되면 에디터 저장이 구버전 데이터 기반** — 머지에서 무기 DA/LocRes 충돌은 main 채택 + **임포터 재실행이 참조·멤버십·family를 CSV 기준으로 복원**(선언적 동기화가 머지 수리 도구가 됨). CardFamily 태그→FName 무음 드롭도 재임포트가 그물.
5. **시딩 전 시트에 full sync 금지** — 시드 2행뿐인 ST_UI/ST_CardEffect 시트가 25행 스냅샷을 롤백(실사고, git 복원). 시트별 -SheetName 지정 습관.
6. 리네임은 시트 셀 연쇄(CardId/AssetName/AttrId/Payload) — 오타 2회 왕복 실측. 리네임 시 관련 셀 목록을 한 번에 안내할 것.

### 남은 것 (후속 행 등재)
BP 그래프 핀 리터럴 3곳 / ST_UI·ST_CardEffect 시트 시딩(사용자) + Cards 시트 bounsshot 오타 / LoadoutEntry 배선 확인 / 고아 WBP 2개 정리.

> 🔁 **정정 2026-08-13 (M0 EC ④ 재대조, `docs/m0-baseline-reconcile`) — 위 "남은 것"이 잔여를 과소 기록했다.**
> 제목이 "**전수** 이관 완료"이고 보드 행이 완료 마킹된 상태라, 이 갭을 그대로 두면 **M0 EC ② 판정이 위양성으로 통과한다.** 실측 잔여는 **16건**이다.
>
> **① 누락돼 있던 것 — `WBP_Lobby` 10건.** 위 기록은 "LoadoutEntry 배선 확인" 1건만 적었으나 실제로는 **11건**이다.
> 판정 방법(양방향 증거): `ST_UI.csv` 의 키 24개를 하나씩 `Content/**/*.uasset` 과 `Source/` 양쪽에서 역추적하면, **양쪽 모두 참조 0인 키가 정확히 11개** 나온다 —
> `Widget.Lobby.WeaponSlot0`~`7`(8) · `Widget.Lobby.InviteButton` · `Widget.Lobby.JoinButton` · `Widget.LoadoutEntry.NameLabel`.
> 나머지 13개는 전부 WBP 바이너리 또는 C++ 에서 참조된다. **"시트에 행은 심었는데 위젯 바인딩을 안 했다"** 가 확정된다.
> ℹ️ 단 `WBP_Lobby` 무기명 8슬롯은 `Docs/Review/WBP_TextInventory_20260813.md` §3-2가 *"무기명을 고유명사로 유지할지 기획 확인 필요"* 라 적어 **의도적 보류일 가능성**이 있다 — 그렇다면 보류 사유가 여기 기록되지 않은 것이 문제다. → 보드 `결정대기`.
>
> **② 누락돼 있던 것 — C++ 5건.** gather manifest 가 수집까지 하고 있는데(= 프로젝트 스스로 검사 대상이라 선언한 것) 기록되지 않았다.
> · `FPSRBossDefinitionDataAsset.cpp` 의 `GetDescription()` **4건**(`DefaultBoss`/`AtSpawnPoint`/`AtFallback`/`BossDescFmt`) — **실질은 가드 누락**이다. 헤더 주석이 *"designer tooling / catalog"* 라 선언해 놓고 `#if WITH_EDITOR` **밖**에 있어 쿠킹 빌드에 실린다. 호출자는 C++·BP 통틀어 0(사실상 dead code)이고, 같은 파일의 `IsDataValid` 는 제대로 가드돼 있다 → **가드 라인을 위로 올리면 4건이 한 번에 소거된다.**
> · `FPSRCardEffect.cpp:356` `LOCTEXT("UnknownWeapon", "Weapon")` **1건** — 이건 **진짜 UI 노출**이다(`UCardEffect_GrantWeapon::GetDescription` → 카드 선택 위젯). 같은 줄의 `Fmt.UnlockWeapon` 은 이미 LOCTABLE인데 인자만 리터럴로 남았다.
>
> **③ 검사 대상의 정의는 이미 기계화돼 있었다** — `Config/Localization/Game_Gather.ini` 가 `SearchDirectoryPaths=Source/FPSRoguelite/`(에디터 모듈 배제) + `ShouldGatherFromEditorOnlyData=false`(`#if WITH_EDITOR` 스킵)라, **`Game.manifest` 수집분 = EC ② 검사 대상**이다. 에디터 모듈 `FPSRogueliteEditor` 의 LOCTEXT 383건과 런타임 `#if WITH_EDITOR` 내부 ~108건은 **대상이 아니다**(수집 제외가 의도).
>
> **잔여 16건 = C++ 5(빌드 동반) + WBP 인라인 Text 11(위젯 에디터) + BP 그래프 핀 ~4곳 + 고아 WBP 2.** 전부 빌드 또는 에디터를 요구하므로 이번 문서 트랙에서는 처리하지 않고 보드 후속 행으로 등재했다.
> 🪤 **재현 시 주의**: 이 환경의 `grep -P` 는 `-P supports only unibyte and UTF-8 locales` 로 **에러 종료**한다(`LC_ALL=C` 로도 해소 안 됨). `2>/dev/null` 을 걸어 두면 "한글 0건"이라는 **깨끗한 거짓 결과**가 나온다 — `.uasset` 내 CJK 탐지에는 `grep -P` 를 쓰지 말 것.

## 🎲 CARDDRAW v4 — 추첨 배제 규칙 (family+레어도 쌍) (2026-08-13, `phase/card-draw-exclusion-v4` → 머지 `3d5418cc`)
> 사용자 확정 판정표: All레어+All레전더리 ✅ / All레어+All레어 ❌ / All레어+This레어 ✅. 배제 키 = (family, 굴린 레어도) 쌍, WeaponStat 자동 family에 `.all`/`.this` 스코프 접미(전체/개별 = 다른 카드), 같은 카드가 레어도만 달리해 2장 제시 가능(동일 카드 포인터 차단 폐지 — 단 같은 카드+같은 레어도는 family 유무 무관 무조건 배제 = 레드팀 P2 가드). family 키는 대상 무기 미구분(SSOT 명기, 사용자 확인 대기 항목). 명세+원장 = `Docs/Specs/CARDDRAW_FamilyRarityExclusion.md`. 6장(FireRate·MagSize·RecoilVertical All/This) family 재파생, 멱등 재확인. 후속: PoolValidator ThinOffer 휴리스틱 v3 잔존(과잉 경고 가능).

## 🃏 CARDCSV — 카드 CSV 저작 파이프라인 (2026-08-13, `phase/card-csv-pipeline` → 머지 `da180388`)
> 보드 행 "카드 시스템 외부 엑셀 데이터 파이프라인 개편"의 코드 전량. 저작 사슬 = 구글 시트(Cards/CardCatalog) → sync → `Content/Authoring/*.csv` → 임포터(Tools 메뉴/`-run=FPSRImportCards`) → `DA_Card_*`(파생물). 명세+레드팀 원장 = `Docs/Specs/CARDCSV_ImporterPipeline.md`, SSOT = `CombatWeaponCard.md` §2-3-10 신설.

### 결과
- 30장 역추출→재임포트 **멱등 증명**(2회차 unchanged=30, dirty 0) = 무회귀 기준선. 다중 무기 풀 동시 소속 7장은 OwnerWeapon 세미콜론 리스트로 보존(단일 컬럼이었으면 붕괴 — C2 에이전트가 잡아 머지 보류 걸었던 건).
- CardFamily = FGameplayTag → **FName + E1 속성 ID 자동 파생**(공란 기본). `Card.Family.*` 태그 제거. **의도된 거동 변화 1건**: FireRate·RecoilVertical의 전체무기/개별무기 쌍이 같은 제시에 동시 등장 안 함(같은 속성=한 제시 1장 — §2-3-2 v3 사용자 확정 의미론. 원복 = 시트 Family 셀에 다른 값).
- 레드팀 P1 0 / P2 7(6건 수정 + 1건 의도 판정) / P3 9(5건 수정, 4건 후속). 주요 수정: Description 공란 11장의 `<MISSING STRING TABLE ENTRY>` 노출 차단(GetEmpty 규칙), 부분실패 3종(오류 카드 저장·풀 dangling·SavePackages exit 0 삼킴), AssetName 중복 하이재킹, Effect_<i> NewObject Fatal 경로.
- 부수 발견·치유: `DA_CardModifiers_SniperScope`의 CardId가 BurstFire 복붙 중복(기존 콘텐츠 버그).

### 🪤 함정
1. **`-DisableUnity` 게이트 빌드가 10분을 넘기면** 셸 타임아웃으로 끊겨도 UBT 자식은 계속 돈다 → 재실행이 `ConflictingInstance` 뮤텍스 충돌. UBT 종료는 `Win32_Process`의 CommandLine으로 감시(PS5.1 `Get-Process`는 CommandLine 미노출).
2. 임포터 계약 설계 시 **"파생 텍스트 키 생성 규칙"과 "에셋에 기록하는 참조 규칙"은 반드시 1:1 대칭**이어야 한다 — 한쪽만 공란을 스킵하면 미존재 키 참조가 태어난다(P2-①).

### 남은 것 (보드 행 유지 사유)
- Cards/CardCatalog **시트 시딩**(리포 CSV → 시트, 1회) → 이후 시트=마스터. ko 재저작([KO-TODO] 소거)·en/ja 번역 = 시트에서.
- PIE 사용자 스모크(§12-7): 카드 3장 제시·빈 설명 11장 플레이스홀더 없음·같은 속성 쌍 배제·해금 오퍼.
- P3 후속 4건(익스포터 메뉴 가드 등) + 머지 체크리스트: 타 브랜치에 미재저장 카드 uasset 있으면 CardFamily 태그→FName 무음 드롭(재검증 그물 필수).

## 🌐 LOC0 — StringTable CSV 파이프라인 공통 기반 (2026-08-12, `phase/loc-foundation-stringtable` → 머지 `6f92dbdb`, origin 통합 `5bb52a02`)
> 보드 행 "문자열 외부화 파이프라인 + 기존 UI 전수 이관"(하이)의 **Phase 0**. 이 위에서 Phase A(UI 전수 이관, `phase/loc-ui-migration`)와 Phase B(카드 CSV 개편, `phase/card-csv-pipeline`)가 병렬 분기한다. 명세·레드팀 원장 = `Docs/Specs/LOC0_StringTablePipeline.md`, SSOT = `Docs/SSOT/Localization.md`(신설).

### 결과
- **런타임 문자열 소스 = `Content/StringTables/*.csv` 3개**(UI/CardEffect/Card), `LOCTABLE_FROMFILE_GAME` 리터럴 3건으로 등록(게임 모듈 `FFPSRogueliteGameModule` 신설 — gather가 소스의 매크로 리터럴을 파싱해 CSV를 발견하므로 ini 데이터드리븐 등록 금지가 구조 제약).
- **ko 네이티브 + en/ja 타깃**: 커스텀 gather 스텝 `UFPSRImportCsvTranslationsCommandlet`이 CSV의 en/ja 컬럼을 문화권 아카이브에 주입 → `Scripts/localization-gather.ps1` 원버튼으로 gather→주입→LocRes 컴파일 왕복(멱등 확인).
- **저작 마스터 = 구글 시트**(공유폴더 `FPS로그라이크/시트/`) → `Scripts/sync-authoring-csv.ps1` 무인증 export URL pull(헤더 검증·실패 시 스냅샷 보존·provenance = `Config/AuthoringSheets.manifest.json`). 리포 CSV = 빌드 스냅샷(단방향).
- 검증: 빌드 3회 그린(-DisableUnity 포함) · 자동테스트 `FPSRoguelite.Editor.Localization.StringTableCsv` Success · sync 정상+부정 테스트 · 패키징 스모크(pak 내 CSV 3종 + ko/en/ja LocRes + ICU 스테이징 확인) · 레드팀 게이트 P1 0/P2 2(전건 수정)/P3 6(5건 수정, 1건 후속).

### 🪤 함정 (Troubleshooting에도 올릴 것)
1. **PS5.1 + BOM 없는 한글 .ps1 = 파서 에러**(CP949 오독). 스크립트는 UTF-8 **BOM** 필수.
2. **PS5.1 `Invoke-WebRequest`의 `$Response.Content` 문자열은 charset 미지정 응답을 Latin-1로 디코드** → UTF-8 본문 이중 인코딩 파손. 원시 바이트(`RawContentStream`)로 받고 그대로 저장할 것.
3. **`Internal_LocTableFromFile`은 ImportStrings 실패에도 빈 테이블을 무조건 등록** — 리로드류 유틸은 엔진 파일워처처럼 기존 테이블에 in-place `ImportStrings`(검증 후 클리어 = 깨진 CSV에 기존 문자열 보존)로.
4. **`CulturesToStage`만으론 패키지에서 문화권 활성화 불가** — ICU 데이터는 `InternationalizationPreset`이 결정(BaseGame 기본 English → `EFIGSCJK`로 덮음). 스테이징 확인만으론 못 잡는 무음 결함.
5. **Localization 대시보드 Gather/Compile 클릭 = 수제 `Config/Localization/Game_*.ini` 덮어씀** — 실행은 반드시 스크립트로.
6. `Content/StringTables/` 안에 두는 모든 파일은 UFS 스테이징으로 pak에 실린다(provenance manifest를 Config/로 뺀 이유).

### 남긴 것
- PIE 사용자 스모크(culture=en/ja 시드 전환 + ja 폰트 글리프 확인) 대기. 패키지 기본 문화권(DefaultCulture, 현 en) = 제품 결정 대기.
- P3 후속 1건: `Game_ImportCsvTranslations.ini CSVFiles` ↔ 테스트 목록 자동 대조.

## 🏙️ 인게임 맵 교체 완료 — Synthwave City Kit → `L_Map1_City` (2026-08-12, `content/map1-synthwave`)
> M0 **(a′) 완료**. 프롭 밀도까지 이 작업 단위 안에서 확정했으므로, (b) 베이스라인의 선행 중 맵 쪽은 닫혔다.
> **남은 (b) 선행은 2건**이다 — ①**(a″) 적 인스턴싱/VAT** ②**[결정] 목표 프레임 예산 수치 + 측정 빌드 구성**(현재 *결정대기*).
> ⚠️ §7-6 (b) 본문은 *"(a′)·(a″) 완료 후에 잰다"* 라고만 적어 ②를 전제로 명시하지 않지만, **보드 `선행작업` 릴레이션은 3건으로 더 보수적으로 걸려 있다.**
> 목표 프레임값이 없으면 실측해도 합/불을 못 적으므로 릴레이션 쪽이 맞다고 본다 — 둘을 일치시킬지는 사용자 판단.

### 결과
`Map_CyberCity` 폐기 → `L_Map1_City` 신규. 배치·검증·PIE 전부 통과(**"레벨 검증" 0건**, PIE 스모크 이상 없음).

### 🪤 P2 실측치가 통째로 무효가 됐다 — 재측정이 필수였다
2026-08-12 오전에 확정했던 값(*7×9 = 315×405m · 윗면 Z=-50 · 셀 158×203 = 32,074, `CellSizeOverride` 불요*)은
사용자 레벨 편집으로 **전부 폐기**됐다. 지형이 커지고 올라갔다:

| | 이전(무효) | 확정 |
|---|---|---|
| 주 지면 | 7×9 = 63타일 · 315×405m · 윗면 **Z=-50** | **10×11 = 110타일 · 450×495m · 윗면 Z=200** · 구멍 없음 |
| 층 | 단층 | **2단** — 주 지면 Z=200 + **고가 지면 Z≈800**(면적의 20%) |
| 셀(200cm) | 158×203 = 32,074 ✅ | 225×248 = **55,800 ❌ 상한 40,000 초과** |

→ **`CellSizeOverride = 250` 을 명시**해 181×200 = **36,200 / 40,000** 으로 맞췄다.
런타임은 죽지 않고 [셀을 자동으로 키우지만](../Source/FPSRoguelite/Private/Enemy/FPSRFlowFieldComputer.cpp#L964) **검증기가 Error로 잡으므로** 명시가 맞다.
지면이 -50 → 200 으로 올라가면서 **가드레일 #6에 새 제약**이 생겼다: `지면Z ≤ 볼륨Min.Z + 150` 이라 **Min.Z ≥ 50**. (Min.Z = 100 으로 배치)

### 확정 배치값
- **볼륨** 1개 — 위치 `(0, 2250, 850)` · Box Extent `(22600, 24900, 750)` · 회전 0(축정렬) · `CellSizeOverride = 250`
  · MapId 미설정 · 월드박스 X `-22600..22600` Y `-22650..27150` Z `100..1600`
- **PlayerStart** `(0, 0, 299)` — 지면 199 위 100cm. 이 액터가 [격자원점 Z를 정한다](../Source/FPSRoguelite/Private/Enemy/FPSRFlowFieldSubsystem.cpp#L44)(단일맵은 `DetectFloorZForVolume` 이 아니라 `DetectFloorZ` = 첫 PlayerStart 아래 트레이스) → **격자원점 Z = 199**
- **적 스폰포인트 18개** — 주 지면 15 · 고가 지면 3. 전부 지면+100
- **천장 안개판 2장** — `Fog` Z=6505 · `Fog2` Z=13216, 600×600m, `MI_CeilingFog`

### 🪤 층 간 이동 — 계단 9개 중 2개는 적이 못 쓴다
단차 600cm는 오를 수 있는 최대(60)의 10배라 계단·다리로만 연결된다. 셀 250cm 기준 **셀당 상승량** 실측:

| 메쉬 | 개수 | 상승 / 수평런 | 셀당 | 판정 |
|---|--:|---|--:|---|
| `SM_stairs_02` | 7 | 685 / 4305 | **39.8cm** | ✅ 통과 |
| `SM_bridge` | 4 | 170 / 1200 | 35.4cm | ✅ |
| `SM_stairs` | 2 | 685 / **1670** | **102.6cm** | ❌ 벽으로 인식 |

셀을 200으로 낮춰도 82cm라 **셀 크기 문제가 아니라 계단 형상 문제**다. 적을 올리려면 수평 런을 늘려야 한다(플레이어는 CMC라 그대로 오른다).

### 스폰 템포 — 우려가 수치로 확인됨
스폰 선택은 [**균등랜덤**이고 거리 가중치는 2026-06-25에 제거됐다](../Source/FPSRoguelite/Private/Enemy/FPSREnemySpawnSubsystem.cpp#L1242).
플레이어 근처 스폰(front spawning)은 [멀티맵 전용](../Source/FPSRoguelite/Private/Enemy/FPSREnemySpawnSubsystem.cpp#L813)이라 단일맵인 이 맵에선 **작동하지 않는다**.

> PlayerStart까지 거리 최소 18m · **중앙 240m** · 최대 342m → 적 250cm/s 기준 **중앙 96초 · 최대 137초**

코어 재미 게이트가 "30초 루프"인데 절반의 적이 3루프 뒤에 온다. **스폰포인트를 늘려도 안 바뀐다**(균등랜덤이라 점 개수와 평균거리는 무관).
바꾸려면 ①`ZoneTag`+`AFPSRSpawnRoom` 으로 구역 활성화 ②아레나 축소 ③적 속도 — 셋 중 하나다.
PIE 스모크에서는 이상 없었으므로 **현 상태 유지**, 템포 문제가 실제로 드러나면 그때 손댄다(사용자 결정 2026-08-12의 연장).

### 룩 — 밝기의 주범은 광원이 아니라 이미시브였다
`DirectionalLight` Intensity = **1.0**(거의 꺼진 값)인데도 화면이 밝다. 실제 출처는 키트의 이미시브 머티리얼:
`MI_neon_` **29.22**(색 B=2.0 → 실효 58) · `MI_synthwave_wall`/`_wall5` **24.51** · `MI_neon_6` 21.28 · `MI_sun` 9.50.
그리고 이 레벨엔 **PostProcessVolume·SkyLight가 아예 없었고**, `DefaultEngine.ini`의 `[/Script/Engine.RendererSettings]`는 **2줄뿐**(`r.AllowStaticLighting=False`·`r.CustomDepth=3`)이라 노출·블룸이 전부 엔진 기본값이었다. → 사용자가 PostProcessVolume 배치.

**천장 안개** = `M_CeilingFog` / `MI_CeilingFog` 신규(`/Game/Materials/Fog/`). Unlit·Translucent·양면, 포스트프로세스가 아니라 **메쉬 머티리얼**.
Opacity = `Fog Opacity` × lerp(`Opacity Facing`, `Opacity Grazing`, Fresnel(`Grazing Exponent`)) × DepthFade(`Depth Fade Distance`).
`ExponentialHeightFog` 로는 불가능하다 — 높이 안개는 **아래로 짙어지고 위로 옅어져** 방향이 반대다.
SRS의 `BP_SRS_Fog`(`M_Fog`, `Fog Height`/`Fog Half Density Height` 보유)도 후보였지만 **높이 방향을 뒤집을 수 있는지 확인 못 했다**(머티리얼 그래프가 Python에 안 열림). 나중에 필요하면 그쪽을 먼저 시험할 것.

### 🚨 가드레일 사각지대 — 천장 판 콜리전
안개판은 처음에 `QUERY_AND_PHYSICS` + **WorldStatic** 이었다. 두 가지를 기록해 둔다:
1. **플로우필드는 격자원점 Z + 2000(=2200)에서 아래로 프로브**한다. 판이 그보다 **위**(6505/13216)면 무해하지만, **아래**였다면 모든 칸의 바닥이 천장으로 잡혀 맵 전체 길찾기가 깨진다.
2. **"레벨 검증"이 이걸 못 잡는다.** 가드레일 #1은 *"막는데 WorldStatic이 아닌 것"* 만 보는데 이 판은 WorldStatic이라 **통과해 버린다.** 판정식의 사각지대다.
→ 천장/장식 판은 **NoCollision** 이 정답(검증기도 `NoCollision`은 건너뛴다). 최종 상태는 콜리전·그림자 모두 꺼짐.

### 🪤 도구 쪽 함정 3건
- **`HitResult` 필드가 Python에 안 열린다** — `impact_point`·`hit_actor` 전부 `get_editor_property` 실패, `break_hit_result`도 미노출. 트레이스는 **맞았나/아니냐(HitResult vs None)만** 읽힌다. 표면 Z는 **끝점 Z를 이분 탐색**해서 구했다(16회 = cm 정밀도).
- **`delete_asset` 이 모달을 띄워 에디터를 멈춘다** — 임시 프로브 머티리얼을 지우려다 게임 스레드가 막혔고 에디터 재시작으로 이어졌다(크래시 아님, 유실 없음). 스크래치 에셋 정리는 **파일로** 할 것.
- **기존 액터에만 기즈모가 안 뜨던 건** 사용자가 해결. Game View·선택·화면내 여부·모드 전부 정상이었고, 새로 만든 큐브는 정상 동작했다 — 즉 **액터별 속성** 문제였다. `bLockLocation` 은 protected UPROPERTY라 Python으로 못 읽는다(`.umap` 이름 테이블에 없으면 잠긴 액터가 없다는 뜻).

### 남은 것
- **`SM_stairs` 2개** — 적이 못 오르는 계단. 장식으로 둘지 런을 늘릴지 미정.
- **고가 지면 스폰 3개** — PIE에서 이상 없었으나, 스웜이 커졌을 때 계단 병목이 생기는지는 미확인.
- 스폰 템포(중앙 96초) — 위 참조.

---

## ⚡ 적 인스턴싱/VAT를 M2 → M0로 이동 (2026-08-11, `docs/instancing-to-m0`)
> 사용자 결정. 아래 시딩 항목에서 "M0 베이스라인 결과에 따라 당겨야 할 수 있다"고 표시해 둔 건을 **재검토 없이 바로 당기는 쪽으로** 확정.

### 왜 (제1원리)
적 200-300을 싸게 굴리는 것이 제1원리(`Game.md §1`)인데, **액터당 렌더 비용의 지배항이 아직 손도 안 댄 상태**다 — 실측상 인스턴싱 **0**(적마다 개별 `UStaticMeshComponent`), `SignificanceManager` **미사용**.
그 위에서 잰 값은 "우리가 유지할 아키텍처의 성능"이 아니라 **버릴 아키텍처의 성능**이다. 베이스라인으로 쓸 수 없다.

**미뤘을 때의 대가**: M2에서 VFX를 다 만든 뒤 예산이 안 나오면 → 인스턴싱 도입 → 예산 재산정 + **이미 만든 VFX 재작업**. 맵 교체와 **같은 부류의 무효화**다.

### 🪤 이번에도 같은 함정을 밟을 뻔했다
보드 행만 옮기고 끝냈으면 **§7-6 M0 정본에 없는 항목이 M0에 배정된 상태**가 된다 — 레드팀 A-P2가 맵 교체에서 잡아낸 바로 그 실패다. 그래서 **같은 커밋에서 정본을 함께 고쳤다**: `§7-6 M0 (a″)` 신설 · 내부 순서 강제 문구를 `(a′)+(a″) → (b)`로 · **EC ①을 "유지할 아키텍처의 수치여야 한다"로** 재작성 · §8 인벤토리의 전환 계획을 M0로 · M2 성능 재측정 항목에 이동 사실 명기.

### 순서
`인게임 맵 교체` ─┐
`U14 perf` → `[결정] 프레임예산·빌드구성` ─┼→ **`성능 정량 베이스라인 실측`**
`적 인스턴싱/VAT` ─┘
셋 다 *측정 대상 자체를 바꾸는* 작업이라 베이스라인의 선행이다. 보드 릴레이션으로 걸었다.

### ⚠️ 남는 마찰
이 행은 **`XL`이라 §6-9 (8)상 클레임 금지**다 — `S`~`L`로 분할해야 착수할 수 있고, **M0 Exit이 그 분할에 걸린다.** 분할 후보를 행 본문에 적어 뒀다: ①ISM/HISM 전환 ②VAT 파이프라인 ③LOD·거리밴드 재정합 ④셀셰이딩(SRS Custom Depth) 정합 실측.

---

## 🌱 마일스톤별 작업 55행 시딩 + 사용자 결정 4건 반영 (2026-08-11, 보드 전용)
> 아래 두 항목(체계 수립 → 레드팀)의 **완결편**. git 변경은 이 로그뿐이고 실체는 전부 Notion 보드에 있다.

### 왜
체계를 세운 직후 **M1·M5·M6에 배정된 행이 0건**이었다. 그건 버그가 아니라 *이 체계가 드러내려던 공백*이었고 — 상태이상 축도, Steam 실앱 전환도, 텔레메트리도 **보드에 존재한 적이 없었다**. 이번에 그 공백을 행으로 채웠다.

### 시딩 결과 (신규 55행)
M0 **5** · M1 **6** · M2 **10** · M3 **5** · M4 **10** · M5 **10** · M6 **5** · 결정대기 **4**.
- **M0은 레드팀 정정본** — 세이브 버저닝(이미 구현)이 빠지고 **`/Engine` 에디터 에셋 쿡 생존 감사**와 **프레임 예산·측정 빌드 구성 결정**이 들어갔다.
- **선행 릴레이션 체인을 실제로 걸었다**: `U14 perf` → `[결정] 프레임 예산·빌드 구성` → `성능 정량 베이스라인`, 그리고 `인게임 맵 교체` → `베이스라인`·`적 그림자 LOD`. §7-6 (1)이 "순서 = 선행작업"이라 선언해 놓고 정작 하나도 안 걸었던 것을 해소.
- **`XL` 6행**(적 인스턴싱/VAT · 실보스 · 맵2 · 맵3 · 카드물량 · 1.0물량 · EA공약)은 §6-9 (8)에 따라 **클레임 금지 — 분할 후 착수**.

### 산정 근거 (물량표 초안, 결정대기 행 본문에 보존)
런 ~20분 × 목표 ~90런 = 30시간(§1-C-8 일치)에서 역산. 무기 8→10→14 · 상태축 0→3→4 · 카드 30→45→70 · 적 아키타입 2→5→8 · 협동유도 스페셜 0→2→4 · 보스 1→2→4 · 맵 1→2→3.
- 🪤 **카드 수치는 근거가 약하다** — "런당 픽 수 × 3지선다"로 잡았는데 **런당 레벨업 횟수가 SSOT에 없다.** M1 플레이테스트 실측 후 교정할 것. 스스로 약한 곳을 표시해 둔다.
- 🪤 **보스 "현재 1"을 물량 1로 세면 안 된다** — 이동·스킬 없는 정지 box다(`RunFlow §2-7`).

### 사용자 결정 반영 4건
①`정리 대상 4건` ②항 ↔ 단독 행 `ABP_FPArms 리다이렉터 fixup` **병합**(단독 행이 유일 소유자, 체크리스트에서 제외 표기) ②맵 교체 행 = **`대기` 유지**(차단 아님 — 사용자가 아직 임포트를 안 한 것) ③`U14 perf` 백로그 → **하이 승격**(M0 EC ①의 유일한 실행체가 자기 슬라이스 최하위에 있던 문제) ④산정안 승인.

### 🪤 이번에 부딪힌 도구 함정
`notion-create-pages`에 **10행을 넘거나 본문이 길면 JSON이 잘려 파싱 실패**한다(체감 한계 ~8KB). 마일스톤 단위로 쪼개 호출할 것. 또 `영역` multi_select는 오타를 **전량 거부**하므로(`캐릭터·1인칭팔`의 `1` 누락으로 배치 전체 실패) 값을 정확히 쓸 것.

---

## 🛡️ 마일스톤 체계 레드팀 게이트 — 지적 23건 전량 수용 (2026-08-11, `docs/milestone-redteam-fixes`)
> 바로 아래 항목(`docs/release-milestones`)의 **머지 후 레드팀**. §6-6-1 원장. **기각 0건 · P1 0건**(리뷰어가 비-코드 diff라 상한을 P2로 잡음) → 머지 되돌림 불요, 정정 커밋으로 처리.

### 절차 이탈 (감추지 않고 기록)
§6-6-1은 레드팀을 **머지 직전**에 걸라고 규정한다. 사용자 요청이 머지 뒤에 와서 **순서가 어긋났다**(`d178aaec` 머지 → 리뷰). push 전이라 원격 영향은 없었다. 문서 갈래는 §6-7상 게이트 비필수라 규칙 위반은 아니지만, **코어 갈래였다면 되돌림 사유**였다.

### 구성
축을 나눠 Fable 서브에이전트 2개(§6-6-1 "되돌리기 비용이 큰 변경 = 최대 2, 서로 다른 공격 축"). **A = 설계·SSOT 정합** / **B = 보드 운영·스키마·백필**. Notion 변경은 diff에 안 나타나므로 **사실 목록으로 프롬프트에 실어** 조용한 축소를 막았다. 스폰 프롬프트에 설계 변호·"왜 이렇게 했는지"는 싣지 않았다.

### 🚨 내 실측이 틀린 것 2건 (가장 중요)
- **`WarningSound` 슬롯이 "비어 있다"는 거짓이었다.** `BP_FPSRPlayer.uasset`에 `WarningSound` + `/Engine/VREditor/Sounds/UI/Camera_Shutter`(SoundWave)가 실재한다(`da761449`에서 지정). 내 확인은 `strings`로 했는데 **못 잡았고**, `grep -a`로는 잡힌다.
  - 🪤 **교훈**: uasset 실측에 `strings`를 쓰지 말 것 — 이름 테이블을 놓친다. `grep -a -o` 를 쓴다.
  - 이 오기록은 같은 파일 §7-5 G1의 "③ 사각(V1) 합격"(소리가 나야 가능한 사용자 실플레이 판정)과 **정면 모순**을 만들었다. 리뷰어가 그 모순으로 거짓을 찾아냈다.
  - **실제 리스크는 "무음"이 아니라 "에디터 전용 에셋이라 패키지 쿡에서 탈락할 수 있음"** → M0 (d)를 이걸로 교체.
- **"EA라서 세이브 마이그레이션이 앞당겨진다"도 거짓이었다.** `URogueliteSaveGame::CurrentSaveVersion`/`SaveVersion`/`MigrateIfNeeded()` + `FPSRoguelite.Smoke.SaveGame` **자동 테스트**가 P6에 이미 있다(`RunFlow.md §2-11`). M0 (d)와 그 Exit Criteria ③은 **착수 0으로 이미 참**이라 게이트로서 아무것도 막지 못했다.
  - 🪤 **교훈**: "전수 실측했다"고 쓴 착수 전 목록에 **세이브 항목이 아예 없었다.** 오디오·VFX처럼 *없을 것 같은 것*만 확인하고 *있을 것 같은 것*은 안 봤다 — 가짜 미완료는 가짜 완료와 같은 비용이다.

### 그 밖의 P2 (전량 수용)
- **맵 교체가 M0 정본에 없었다** — 순서 근거를 WorkLog에만 적고 §7-6 M0 범위엔 안 넣어서, 정본대로 실행하면 구 맵에서 베이스라인을 재고 그 뒤 맵을 갈아 **무효화하는 경로**가 열려 있었다. → (a′)로 정본에 명시 + 선행 강제. 추가로 보드의 **"Synthwave City Kit"은 SSOT에 기록된 적 없는 결정**(§8은 Synty Cyber City) → (5)-5 결정대기로.
- **성능 베이스라인의 측정 빌드 구성 미지정** — `Performance.md:38`이 *"Push Model 전제는 패키지 빌드에선 성립하지 않는다, 예산 계산에서 빼고 세지 말 것"*(미해결 = `OpenIssues_Network.md` N-1)이라 명시하는데, PIE 실측만 하면 M2~M5의 "베이스라인 대비" 판정이 **전부 §5가 금지한 비교** 위에 선다. 목표 프레임 예산 **수치 자체가 어느 SSOT에도 없다**(§5는 캡/빈도 표). → (5)-6 결정대기 신설.
- **"우선순위 = 마일스톤 안에서의 순서" 규칙을 집행할 수단이 없었다** — 세션이 행을 고르는 유일 경로인 §6-9 (2) 전수 조회 SQL에 `마일스톤`이 없어서, **M2를 M0보다 먼저 집는 게 기본 경로**였다. 조회 정책의 근거는 "2단계 조회 = 호출 예산"이었는데 **같은 SQL에 열만 늘리는 건 호출 증가 0**이라 그 근거가 이 누락을 정당화하지 못한다. → SELECT에 `마일스톤`·`크기` 추가.
- **(8)이 자기모순이었다** — 신규 행에 `마일스톤`을 채우라면서(relation = 상대 행 page ID) 그 ID를 알 유일한 경로인 마일스톤 DB 조회를 **스스로 금지**했고, M0~M6 **행 ID가 git 어디에도 없었다.** 리뷰어가 실제로 이 벽을 쳤다(ID만 반환돼 M0인지 M2인지 확인 불가). → **(8)에 page ID 7개 정적 기록**(마일스톤은 불변 집합이라 1회로 끝난다).
- **에이전트 정의가 신설 규칙의 반대를 지시하고 있었다** — `pm-board.md:104` *"릴레이션은 `선행작업`만 쓴다"*, 신규 행 지침은 `추천모델`만 요구, `board.md`는 마일스톤을 **0회** 언급. → 세 곳 다 갱신 + 감사에 **D8(마일스톤 미배정)** 신설.
- **"순서 = `선행작업`"이 확정 전제인데 백필이 선행을 하나도 안 걸었다** — 맵 교체 → 성능 실측 의존이 프로즈로만 존재. → 보드에서 릴레이션으로 연결(아래).
- **§6-7 브랜치·검증 라이프사이클이 "P 단계"에만 걸려 있었다** — §7-6이 P를 종료한 뒤엔 **M 작업에 머지 전 검증 게이트가 형식상 비적용**. → 단위를 "작업 단위"로 확대.
- **M6 EC ③ "1.0 물량표"를 만드는 절차가 없어 M6가 정의상 닫히지 않았다** → 물량표는 **하나**이고 `EA`/`1.0` 두 컬럼을 갖는 것으로 통일.
- **우선순위 의미를 바꾸고 기존 20행을 재검토하지 않았다** — 실례: 「U14 perf 측정항목」이 **백로그**(= 신 의미로 "M0 안에서 맨 뒤")인데 **M0 EC ①의 유일한 실행체**라 M0 종료가 자기 슬라이스 최하위 행에 봉쇄된다. 승격은 사용자 전용이라 제안만 남긴다.

### 백필 오배정 (B 지적, 사용자 결정 대기)
- **이중 배정 확정** — 「정리 대상 4건」②항이 `ABP_FPArms` ObjectRedirector 정리인데, 동일 작업의 단독 행이 따로 M2에 있다. 한 작업이 M0·M2에 동시 존재.
- 「슬라이드 SlopeTimeScale 유닛테스트」→M0 = 정본의 닫힌 목록에 없는 **다섯 번째 예외**를 조용히 만든 것.
- 「적 재스폰 체력바 잔존」→M2 = 오디오·VFX 슬라이스 밖의 **복제 소비자 버그**. M2에 잠그면 M1 G2 실플레이 판정을 오염시킨 채 판정한다. → **개시 규칙 예외 ③**의 표본 사례로 §7-6에 명문화.
- 「정리 대상 4건」→M0 = 본문 ③항이 LPAMG 트랙(M5 라이선스 영역) 종속, ④항은 사용자 결정 미확정 — 첫 슬라이스 배정과 본문의 착수 불가 조건이 충돌.

### P3 (전량 수용)
§7-6 헤더 `§6-9-1` 오참조(→(8)) · Roadmap 헤더 "담는 섹션"·`Game.md` 라우팅에 §7-6 누락 · (1) 폴리시 결정문↔M2 문면 모순(→"레인의 1회성 부트스트랩"으로 명시) · 자동검사 대상·판정 주체 미정의(→정의 + "판정 = 사용자") · M0 감사 범위가 §7-3 한정(→§7-5·§8·도메인 SSOT로 확대; **실재하지 않는 `L_Sandbox`를 §8과 `Game.md §9`가 인용**하던 것 정정) · M4 "G1 재판정"이 §7-5에 없는 절차(→§7-5에 재판정 조항 신설) · `크기` M/L 경계 중첩(→S=1/M=2~3/L=4+ 상호배타, **XL은 클레임 금지·분할 후 착수**) · 마일스톤 `상태` 갱신 트리거 전무(→(4) 마감에 훅) · WorkLog 산술 불일치.

### 산술 정정 (B-P3)
아래 항목이 "활성 18"과 "활성 20행 백필"을 같은 문서에 적었다. 실제 = **착수 중 파생 2행 신설**(「보드 이상행 정리」·「§7-3 오디오 표기」) + 클레임 행 1개 → **보드 62 → 65행**, 백필 대상 = 기존 활성 18 + 파생 2 = **20**. 클레임 행(메타작업)은 미배정으로 남겼다.

### 리뷰어가 "안전"으로 확인해 준 것 (지적 0건이 통과 근거가 아니므로 기록)
`/Game` 사운드 에셋 0 · 무기 DA `FireSound` 전부 빈 슬롯 · 자작 Niagara 1개(나머지 NS_*는 구매팩) · `Config/Localization` 부재 · `SteamDevAppId=480` · 인게임 맵 1개 · 리텐션 계측 0 · P7의 CommonUI ✅는 실물 정합(같은 류 오기록 아님) · §1-C-8/§1-C-6 인용 정확 · M1&lt;M4·M2&lt;M4 순서 근거는 §7-5 원문과 정합 · 제1원리(`Game.md §1`) 위반 서술 없음 · Exit Criteria 비복제 구조는 보드 실물과 일치 · 완료 38행 소급 미배정은 규칙과 실행 일치 · 훅 가드는 마일스톤 DB에도 동일 적용.

---

## 🎯 출시 마일스톤 체계 수립 (EA→정식) + 보드 마일스톤 축 도입 (2026-08-11, `docs/release-milestones`)
> 사용자 지시. **보드 스키마 변경 + 신규 DB 동반.** 정본 = `Docs/SSOT/Roadmap.md` §7-6 / 운영 규칙 = `Workflow.md` §6-9 (8).

### 왜
보드는 잘 돌지만 **지금 인지된 작업만 산재**해 있었다. "출시까지 무엇이 남았나"를 셀 축이 없어서,
**빠진 축이 안 보이는 게 아니라 셀 수 없었다.** 마일스톤 = 그 축.

### 착수 전 실측 (마일스톤을 잘못된 완료 상태 위에 쌓지 않으려고 전수 확인)
- 코드는 두텁다 — 게임 모듈 21 서브디렉터리 + 에디터 모듈 4. 무기 DA 8종 · 카드/프래그먼트 42에셋 · 미션 BP 6종.
- 🚨 **오디오 = 실질 0** — `Content/Audio/`에 `SC_Master`·`SMix_Master` **2개뿐이고 사운드 웨이브/큐가 프로젝트 전체에 0개**다.
  배관(`FPSRAudioSubsystem` · `FPSRBlindspotAudioComponent::WarningSound` · `FPSRWeaponDataAsset::FireSound` 슬롯)만 있고 **꽂을 소리가 없다.**
  → §7-3의 `P7 오디오 MVP ✅`는 **오기록**이었다. 같은 커밋에서 정정(배관 완료 / 사운드 에셋 0).
- 🚨 **VFX = 실질 0** (자작 Niagara `NS_JumpPad` 1개) · **로컬라이제이션 = 0**(`Config/Localization` 부재) ·
  **Steam = `SteamDevAppId=480`**(Spacewar 공용 테스트 앱) · **인게임 맵 1개** · **텔레메트리 0**.
- 보드 62행 = 완료 38 / 대기 15 / 검증중 3 / 보류 4 / 폐기 2 / **진행중 0**(착수 경합 없는 시점).

### 사용자 결정 4건 (이 체계의 전제)
1. **기간 산정 = 달력 날짜 없음** — 마일스톤 = Exit Criteria / 행 = 상대크기 `S·M·L·XL` / 순서 = `선행작업`.
   ⚠️ 요청 원문이 "기간 산정 안 함"과 "기간을 계산해서"로 **자기모순**이라 착수 전에 물어 해소했다.
2. **절단 = 수직 슬라이스** + UI·오디오·VFX·성능·접근성은 **전 마일스톤 관통 폴리시 레인**.
   기각안 = 기능축 절단(UI/사운드/VFX 각각 마일스톤). 근거: 각 마일스톤 끝에 **플레이 가능한 게임이 없어** G1/G2 게이트가 판정 불가.
3. **출시 형태 = EA → 정식** — 동결선 2회. **Steam 실앱 ID·세이브 마이그레이션·텔레메트리가 EA 진입으로 앞당겨진다**(정식 1회면 미룰 수 있던 셋).
4. **보드 반영 = 별도 마일스톤 DB + 릴레이션**, 단 🔒 **일반 세션은 조회하지 않는다**(사용자 지시 시에만).
   → 세션 시작 SQL 불변 = 우려했던 "조회 2단계 + 무료 플랜 호출 예산 증가"가 사라졌다.

### 체인 (M0→M6)
`M0` 기준선 정정(⚠️ 유일한 비-수직슬라이스, 예외 근거 명시) → `M1` 재미 축 확정(G2) → `M2` 감각 완성 →
`M3` 런 루프 완결 → `M4` 콘텐츠 양산 → `M5` EA 진입(🔒동결1) → `M6` 정식 1.0(🔒동결2).

**순서에 박은 근거 3개** (뒤집으면 재작업이 나는 지점):
- **M1(G2) < M4** — 시너지 축이 정해지기 전에 만든 카드·무기는 축이 바뀌면 전부 재설계 대상.
- **M2 < M4** — 오디오/VFX 파이프라인이 서야 이후 콘텐츠가 *소리·이펙트를 달고 태어난다*. 뒤로 미루면 전 콘텐츠 소급 패스.
- **맵 교체 ∈ M0** — 인게임 맵을 갈면 **플로우필드 베이크·스폰포인트·미션존·성능 베이스라인이 전부 무효**가 된다.
  → 유지할 맵 위에서 M0 성능 실측을 해야 한다. (그래서 "콘텐츠"인데 M0)
- 🪤 **M1의 함정**: 상태이상은 **보이지 않으면 판정할 수 없다** → M1에 플레이스홀더 상태 VFX·오디오 큐를 **필수 포함**.
  빼면 §7-5가 G1/G2를 쪼갠 바로 그 실패("손맛이 죽은 건지 축이 없어서인지 분리 불가")를 반복한다.

### 사용자가 빠뜨렸던 축 (예시로 든 UI/사운드/이펙트/게임루프 **바깥**)
문자열 외부화 시점 · 성능 재측정 트리거 · **출시 사무(Steam 등록·심사 리드타임)** · **에셋 라이선스 감사**(Synty/SRS/PWAS/LPAMG/Paragon —
Fab 미이관 리스크는 §8에 이미 기록돼 있었다) · **텔레메트리**(§1-C-8이 D1 35%/D7 15%를 확정해 놓고 계측 수단이 0) · 데모/EA 여부.

### 실행
- Notion **"마일스톤" DB 신설**(`collection://7934d49b-…`) — `코드`/`상태`/`성격`/`동결선`/`요약`/`작업`(듀얼).
  마일스톤 페이지 본문은 **Roadmap §7-6 포인터만** — Exit Criteria를 복제하지 않았다(이중 SSOT 금지).
- 작업 보드에 **`마일스톤`(relation, 듀얼 자동)** + **`크기`(S/M/L/XL)** 2열 추가. 기존 14열은 무변.
- **활성 20행 백필** — M0:7 · M2:8 · M3:3 · M4:2 · 미배정 1(이 행 자체 = 마일스톤을 정의하는 메타작업이라 상위에 둔다).
  **완료 38행은 소급 배정하지 않았다**(수작업 38회로 얻는 게 과거 그래프뿐).
- ⚠️ **M1·M5·M6은 배정된 행이 0건** — 아직 그 작업들이 보드에 없다는 뜻이고, **이게 정확히 이 체계가 드러내려던 공백**이다.

### 남긴 결정대기 4건 (§7-6 (5))
콘텐츠 물량표(M4 선결 — 없으면 M4 Exit Criteria가 성립 안 함) · EA 진입 최소 콘텐츠 기준선 ·
EA 로드맵 공약 범위(공개하면 되돌리기 어렵다) · 텔레메트리 수집항목·보관정책.

---

## 🗑️ 보드에 `폐기` 상태 신설 + 완료로 우회했던 건 이관 (2026-08-11, `docs/board-discard-status`)
> 사용자 결정. 커밋 `981a982e`(§6-9 규칙) · `ae5a9d48`(에이전트 감사 정합). **보드 스키마 변경 동반.**

### 왜
`폐기`가 없어서 **"트랙을 접었다"를 `완료`로 넣고 사유를 본문에 적는 우회**를 반복했다. 그대로 두면
**"완료 = 실제로 한 것"으로 셀 수 없다.** 사용자 승인으로 `상태` select 에 `폐기`를 추가했다
(기존 7개 옵션 = 대기/진행중/검증중/결정대기/차단/보류/완료 를 **색까지 그대로 보존**한 것을 반환 스키마로 확인).

### 판정 기준을 먼저 못박고 옮겼다 (§6-9 (5))
"폐기처럼 보이는 것"이 많아서, 기준 없이 옮기면 **완료를 폐기로 잘못 내리게 된다.**
- **완료** = 맡은 일을 **실제로 해냈다.** ⚠️ 헷갈리는 두 경우를 명시: **범위 조정**으로 남은 일을 신규 행으로 뺀 것도 완료
  (맡은 부분은 끝났다) · **조사만 하고 "문제 없음"으로 닫은 것**도 완료(결론을 냈다).
- **폐기** = **산출물 없이 접었거나**, 대상이 사라져 **할 수 없게 됐다**.
- 폐기 행도 **보드에 남긴다** — 지우면 "왜 안 했는지"가 사라져 같은 걸 다시 착수한다.
  되살릴 땐 **그 행을 재사용하지 말고 새 행**으로(접힌 뒤 코드가 달라져 스펙을 다시 써야 한다).

### 🚨 전수 감사 결과 — 내가 알던 "폐기 선례" 3건 중 **진짜는 1건뿐이었다**
완료 40건 전수 조회 → 완료커밋이 비었거나 설명문인 10건 + 제목에 폐기 신호가 있는 4건을 **본문까지 열어** 확인.
- ✅ **이관 1건** = 「[이동] 슬라이드 SlopeTimeScale 경계 유닛테스트」. 대상 브랜치·심볼이 부재해 **쓸 대상 코드 자체가 없다.**
- ❌ **`Blu_FP_Arms`는 폐기가 아니다** — 제목의 "폐기"는 **대상 에셋의 운명**이지 이 행의 운명이 아니다.
  참조 0 실측 후 4에셋을 **실제로 지웠고 커밋(`7beef8fc`)이 있다** → 완료.
- ❌ **「PM 보드 담당 서브에이전트 세팅」은 옮기면 보드가 거짓이 된다.** 본문이 스스로 *"완료가 아니라 폐기다"* 라고
  적어 두었지만 **git 이 그걸 반박한다**: `CLAUDE.md` 핵심원칙 3 · `Workflow.md` §6-9 (7) 이 도입 후 **한 번도 롤백되지 않고**
  여전히 "보드 I/O = `/board`(`pm-board`)"를 현행으로 명시하고, `.claude/agents/pm-board.md` 는 **오늘자 커밋으로도 유지보수**됐다.
  결정타 = **이 감사 자체를 그 에이전트가 수행했다.** 당시의 "필수화됐으니 전담 에이전트는 불필요" 판단이 **결과적으로 틀렸다**
  (필수화가 오히려 보드 I/O 빈도를 늘려 전담이 **더** 필요해졌다). → 완료 유지 + **행 본문에 정정 절을 append**.
- 🪤 **교훈**: 폐기 판정을 **제목·자기선언으로 하면 안 된다.** 두 건 다 "폐기"라고 적혀 있었는데 실측은 반대였다.
  근거는 **커밋이 있느냐 + 그 산출물이 지금 쓰이느냐**다.

### 보류했던 2건 — 사용자 결정으로 처리
- **「4b 로코모션 잔여 — 8방향 시작·정지 전환 + `Split_Jumps` 108 리타게팅」 = 행 삭제**(사용자 결정).
  폐기가 아니라 **완료 근거가 없던** 케이스다(본문이 "완료커밋 미기재 — git 전체 로그를 훑었으나 이 행의 작업을 담은
  커밋을 못 찾았다"고 자백). 행이 사라지면 추적이 통째로 없어지므로 **내용을 여기 남긴다**:
  남은 것으로 적혀 있던 항목 = **8방향 시작·정지 전환 클립 40개+** · **`Split_Jumps` 108 리타게팅**.
  인접 커밋 `d719e6c0`(Rifle_01 426개 Blu 리타게팅)은 본문에 "Split_Jumps 108(보류)"라 적혀 있어 **재료를 들여온 커밋**이지
  완료 증거가 아니었다. 나중에 이 애니가 없어서 막히면 **여기가 출발점**이다.
  - 🪤 **Notion MCP 에는 행 삭제 도구가 없다**(create/update/move/duplicate/fetch 뿐 — 페이지 휴지통 API 미노출).
    삭제는 Notion UI 조작으로만 가능해 **사용자에게 넘겼다**. 앞으로 "행을 지운다"는 요청은 항상 이 제약을 먼저 말할 것.
- **「총 모션 클립 저작 파이프라인 + 에디터 탭 툴」 = `폐기`로 이관 + 생존분 명기**(사용자 결정).
  폐기된 것은 **"커스텀 저작 툴"이라는 트랙**이다(4세대·±12,000줄·저작 결과물 0). 다만 순수 폐기가 아니라
  **산출물이 일부 살아남은 하이브리드**라, 행 본문에 표로 못박았다 — **"폐기됐으니 지워도 되는 것"으로 오해하면 현행 코드가 깨진다.**
  - 🟢 **트리 실측으로 생존 확인**: P2 커브 런타임(`75db21f7` — `Anim/FPSRGunMotionCurves.*`·`FPSRGunMotionStudioData.*`,
    **`FPSRFirstPersonArmsAnimInstance`가 매 프레임 소비 중**) · 상태 포즈 DA 스키마 `FFPSRWeaponStatePose`
    (**이후 `HolsterPose`·`ReloadPose`가 이 스키마 위에 얹혔다**) · gun-anchor IK 구조 · A16/A17 규약.
  - 🟡 **ImGui 플러그인은 남겨만 뒀다** — `.uproject`엔 enable 이지만 `Build.cs` 의존 0(쓰는 코드 없음). 정리 대상이 되면 그 근거가 이 줄이다.
  - `완료커밋` 은 비우지 않고 **`[폐기 = 저작 툴]` 접두 + 30커밋 체인 보존** — 생존 산출물이 어느 커밋에서 나왔는지가 사라지면 안 된다.

### 🔄 그리고 폐기한 지 몇 시간 만에 전제가 도착했다 (같은 날)
푸시 직전 `git fetch` 에서 **origin/main 이 3커밋 앞서** 있었고, 그중 `6626ca5e feat(movement): 슬로프 슬라이딩
지속시간 회복 — 내리막 타이머 되감기` 가 **오늘 폐기한 유닛테스트 행의 전제 그 자체**였다.
- 실측: `SlideSlopeTimeRecoveryCap` 이 이제 실재하고(`FPSRCharacterMovementComponent.h:328`), 공식이
  `FMath::Max(-SlideSlopeTimeRecoveryCap, 1.0f - (SlopeAlignment * SlopeTimeInfluence))`(cpp:785)로 바뀌어
  **scale 이 음수가 된다**(감속 → 동결 → **되감기**). 폐기 사유였던 "`scale < -cap` 케이스의 대상 코드가 없다"가 사라졌다.
- **폐기 판단 자체는 당시 코드 기준으로 옳았다** — 되돌리지 않고 그 행은 폐기로 두고, §6-9 (5) 규칙대로 **신규 행**을 열었다.
  판단 과정(브랜치·심볼 부재 실측)이 기록으로서 값이 있고, 그게 규칙을 "재사용 금지"로 쓴 이유이기도 하다.
- 🪤 **머지 전 `git fetch` 를 안 했으면 push 가 거부됐거나 남의 커밋 위에 얹혔다.** 파일은 겹치지 않아
  머지는 깨끗했지만(이동 컴포넌트·테스트맵·ADR 0001 vs 내 SSOT·무기·카드), **머지 후 빌드를 다시 돌려
  `Result: Succeeded` 를 확인하고 푸시**했다 — 파일이 안 겹친다고 컴파일이 보장되지는 않는다.

---

## 🔩 소규모 하드닝 3건 — 위조 Gen · 콘솔 Reserve · ADS 무음 폴백 (2026-08-11, `fix/small-hardening-batch`)
> 머지 `ee15b5fb`. 보드 대기 16건 중 **"한두 번의 체크로 끝나는 것"만** 골라 처리(사용자 요청).
> 커밋 3개 = `77a3835b`(위조 Gen) · `f5abfa90`(Reserve + 소유 헬퍼) · `7f0daf5d`(ADS 경고).

### 🪤 보드 행 본문이 내 구현보다 정확했다 — 음수 한 글자
`Reserve` 상한을 `FMath::Min(Count, Candidates.Num())`로 잡았는데, **보드 행은 `FMath::Max(Count, 0)`까지 요구**하고 있었다.
확인해 보니 그게 맞았다 — 엔진 `Array.h`의 `TArray::Reserve`는 **음수를 `OnInvalidArrayNum`으로 보낸다**(치명 처리).
콘솔은 `FCString::Atoi` 결과를 검증 없이 넘기므로 **`FPSR.DrawCards -1`은 "아무것도 안 뽑기"가 아니라 프로세스를 내린다.**
큰 값만 막고 끝냈으면 **정반대 방향의 같은 크래시**를 그대로 남길 뻔했다.
→ 교훈: 보드 행이 지정한 식을 "대충 같은 뜻"으로 줄여 쓰지 말 것. 왜 그 형태인지 먼저 확인.
- 같은 도메인 전수 grep: 외부 입력 기반 `Reserve`/`SetNum`은 이것 하나뿐(나머지 1건은 DataAsset 배열 길이).

### 위조 Gen 하나로 늦은참여 게이트가 영구히 열려 있었다 (`77a3835b`)
`ServerAckTopology(Gen)`이 클라 값을 그대로 넘기고, `SetAckedTopologyGeneration`은 `FMath::Max`로 **단조 증가**만 한다.
즉 **`INT32_MAX`를 한 번만 보내면 그 값이 영원히 남아**, 이후 문이 열려 `JoinGen`이 올라가도 `AckedGen >= JoinGen`이 계속 참이다.
- **한 곳에서만 클램프**했다 — `HasAuthority` 직후. 여기서 막으면 PlayerState 경로와 `PendingAckTopologyGeneration`
  **버퍼 경로가 같이 덮인다**(두 군데 중복 검사 불필요).
- 하한 0이 정상 클라를 막지 않는 근거도 코드로 확인: `JoinTopologyGeneration >= 0`이라 0으로 깎여도 gen 0 조인만 만족한다.
- **성격 = 위생 조치지 보안 수정이 아니다**(보드 결정 문구 그대로). 단일맵에선 이 게이트가 사실상 no-op이라,
  주석에 **"멀티맵(`bUnifiedExtent`) 활성화 시 재평가"**를 명시했다 — 자가 충족이 실제 이득이 되는 건 그때부터다.

### 소유 판정을 세 함수에 복붙하지 않았다 (`f5abfa90`)
`GetOwnedWeapons()`는 `BlueprintPure`라 시그니처를 못 바꾼다 → `HasOwnedWeapon()`/`HasAnyOwnedWeapon()` **추가**만 하고
스칼라 질문 2곳만 교체(배열을 실제로 쓰는 나머지 2곳은 손대지 않음).
- ⚠️ **"맨손은 소유가 아니다"(`bExcludeFromProgression`) 규칙을 세 함수에 흩지 않았다** — 파일 로컬 `ResolveOwnedWeapon()`
  하나로 뽑고 셋 다 그걸 쓴다. 이 규칙이 갈라지면 맨손이 **카드풀 시드·"무기 보유" 검사·로비 시작무기**로 새는데,
  세 곳이 제각각 새면 증상이 달라 추적이 어렵다.

### ADS "되긴 하는데 안 맞는" 상태를 말하게 했다 (`7f0daf5d`)
조준 소켓이 비었거나 메시에 없으면 `bAiming`만 조용히 false가 되는데, **FOV 줌은 소켓과 무관하게 걸린다** → 화면상 ADS가
작동하는 것처럼 보인다. 이번 ADS 트랙 최초 오진단의 원인이 이 무음이었다.
- **넣은 위치가 이 작업의 전부**였다: `RefreshWeaponPartComponents()` **직후**. `CachedAimSocket`은 그 앞에서 캐시되지만
  **`CachedAimComponent`는 파츠 리빌드가 정한다** → 앞에 넣으면 직전 무기의 낡은 값이나 리셋된 null을 본다
  (항상 뜨거나 항상 안 뜨는 경고가 된다). 매 프레임 게이트에는 넣지 않았다(핫패스).
- **판정을 소비처와 동일하게** 맞췄다(`CachedAimComponent` → 없으면 `ActiveWeaponMesh` = `UpdateAimDownSights`의 `WeaponCarrier`).
  런타임이 보는 것과 다른 걸 검사하면 경고 자체가 거짓이 된다.
- `RefreshEquippedWeaponVisual`은 **모든 머신에서 돈다**(헤더 주석). 이 캐시를 읽는 건 오너 로컬 전용 함수뿐이라
  `IsLocallyControlled()`로 게이트 — 안 그러면 같은 DA 오설정을 접속 머신 수만큼 중복 보고한다.
- ⚠️ 이 보드 행은 **닫지 않았다.** 나머지 절반(무기 DA 5종 조준 소켓 **값 저작**)은 사용자 작업이다.
  이제 그 5종을 장착하면 어느 DA가 비었는지 PIE 로그가 이름으로 알려 준다.

### 사용자 판단으로 종결된 것 2건
- **`Workflow.md §6-6` 빌드 대상 클론 표기** — 새 행을 만드는 대신 **즉시 정정**(사용자 결정). `FPSRoguelite2` → **`FPSRoguelite`**
  (커밋 `ffdb02c3`, 머지 `e8ee944c`). 지목만 바꾸면 다음 사람이 또 "왜 이 클론이지"를 물으므로, 그 위에 **기본 규칙
  ("코드를 고친 그 클론에서 빌드한다")** 을 명시했다 — 다른 클론을 빌드하면 내 변경이 안 들어간 바이너리를 검증하게 된다.
- **[이동] 슬라이드 SlopeTimeScale 경계 유닛테스트 = 폐기**(사용자 결정). 착수 불가가 두 세션 연속 확인됐다 —
  대상 브랜치 `fix/slope-slide-duration`이 origin·로컬 모두에 없고 `SlideSlopeTimeRecoveryCap` 심볼도 0건이며,
  main의 공식은 **음수가 될 수 없어** 행이 요구한 클램프·되감기 케이스의 **대상 코드 자체가 부재**하다.
  브랜치가 도착하면 **이 행을 재사용하지 말고 새 행으로** 연다(그때의 코드가 달라 스펙을 다시 써야 한다).

### 손대지 않기로 판정한 것 (대기 유지)
- **정리 대상 4건** — 2항목이 "LPAMG 트랙 확정 후"·"폴더명 결정 필요"로 **사용자 결정 대기**, 삭제분이 git으로 복구 불가라 임의 착수 비권장.
- **어셈블러 잔여 2건** — 둘 다 **원인 미상 + 이미 우회 적용**. 재현이 없어 지금 능동적으로 끝낼 게 없다(관찰 대기).
- **ABP_FPArms 리다이렉터 fixup** — 행 본문이 에디터 우클릭 조작을 지정 = 사용자 작업 영역.
- **U14 perf 측정 항목 등록 / 적 재스폰 체력바 잔존** — 전자는 "측정 후 결정"으로 명시 이연, 후자는 원인 미확정(조사 동반).

---

## 🧹 보드 3행 처리 — 잔여 머신 경로 · SSOT 드리프트 13건 · CachedRecoil 약참조 (2026-08-11)
> 브랜치 2개: `docs/ssot-drift-and-path-neutralize`(보드 2행, 머지 `898992ea`) ·
> `fix/cachedrecoil-weakptr`(보드 1행, 머지 `18480710`).
> 세 건 다 원천은 `Docs/Refactor_20260806_Report.md`이고 **방향은 2026-08-07 사용자 결정으로 이미 확정**돼 있었다.

### ⚠️ 착수 전에 걸린 것 — 로컬 main이 7커밋 뒤처져 있었다
`main 33e636b3` vs `origin/main d3bd147a`. 뒤처진 커밋에 **`152a4152`(§6-1·§6-6 경로 머신 중립화)** 가 들어 있었는데
**작업 1이 바로 그 커밋의 후속**이라, 안 받고 시작했으면 규약 원문 없이 제 나름의 형식을 만들 뻔했다.
`git pull --ff-only`로 받고 시작(뒤처진 7커밋이 만지는 파일은 이번 대상과 안 겹침).

### 1. 잔여 하드코딩 머신 경로 — 3건 중 1건은 고칠 게 아니라 죽은 문서였다 (`18924c36`)
보드 행이 판단을 **의도적으로 보류**해 뒀다: *"재개 프롬프트라 머신 고유 경로가 오히려 정확할 수 있다.
일괄 중립화 전에 각 문서가 아직 살아있는 작업인지 먼저 판단하라 — 죽은 문서면 Archive 이동이 맞다."*
3개 문서를 읽고 **인바운드 참조를 전수 조사**한 결과 판정이 갈렸다.
- **`AssetReplacement_Synty_ResumePrompt.md` = 죽음.** `U22_AssetReplacement_Prompt.md:3`·`Roadmap.md:83`·
  `TaskPrompts_Master.md:141` **세 곳이 이미 "폐기본 — 읽지 말 것"** 이라 지정하고 있었다 →
  `Docs/Archive/prompts/`로 이동. **내용은 무수정**(역사 기록은 경로까지 그대로 보존).
- 나머지 2건은 **살아있음**(U22a 트랙은 폐기가 아니라 ⏸️일시정지) → `152a4152` 규약대로 **값이 아니라 해석 규칙**으로 치환.
  `<엔진루트>`(§6-1 레지스트리 해석) · `git rev-parse --show-toplevel` · "클론이 여럿인 머신에서만 빌드 클론을 고른다".
- 🪤 **아카이브 이동은 "파일 옮기고 끝"이 아니었다** — 인바운드 참조가 **5곳**이었고, 그중 **`Concept.md:94`는
  이 폐기본을 아직 살아있는 참조로 지목**하고 있었다(Roadmap은 이미 폐기 표시). 드리프트 1건이 여기서 추가로 나왔다(D14).
  → 두 보드 행을 **한 브랜치에 묶은 이유**가 이것이다(둘 다 `Concept.md`를 만짐).
- 보드 행이 "손대지 말 것"으로 못박은 2건(`WorkLog.md`·`Troubleshooting.md`의 클론명)은 **과거 기록/증상 예시**라 그대로 뒀다.

### 2. SSOT 드리프트 13건 — D7만 코드가 아니라 설계를 바꿨다 (`d835c17a`)
근거표 = `Docs/ProjectStructure_Report.md §8`. **리포트가 2026-08-06 스냅샷이라 12건 전부 현재 코드로 다시 대조**했고(전건 유효),
감사 중 D14가 추가돼 13건이 됐다. §8에 해소 배너를 달아 다음 사람이 다시 훑지 않게 했다.

- 🔄 **D7 FF(아군 오사) = 기본 OFF 확정.** 이건 **2026-07-01의 "기본 ON = 코어 협동 정체성" 설계를 사용자가 뒤집은 것**이다.
  코드(`bFriendlyFireEnabled=false`)는 손대지 않았고 **문서가 코드 쪽으로 왔다.** 문서에 *뒤집혔다는 사실 + 원 논거*를 함께 남겼다 —
  원 논거("사선 관리가 협동 긴장·실력 축", "공개매칭 미고려라 트롤 안전장치 불요")는 **ON 토글의 설계 의도로는 지금도 유효**하기 때문.
  전파 = `Enemy §2-10` · `Concept §1-C-6`(+요약 3곳) · `Roadmap` P5 · `CombatWeaponCard` 협동카드. "⚠️코드 후속 flip 필요"는 전부 철회.
- **나머지는 문서를 코드에 맞췄다.** 없는 것으로 확인된 심볼: `USignificanceManager`(플러그인만 켜짐 — 실제는 손수 짠 거리밴드) ·
  인스턴싱(적마다 개별 SMC) · `UActorPool`(풀링은 스폰 서브시스템 **인라인** — 기능은 있고 이름만 없던 것) ·
  `UEnemyScalingProfile`(P5 이연) · `CrosshairScale` · `UHeroDataAsset` · `UFPSRShieldComponent`/공중 아키타입 ·
  `HealthRegen` · `Performance/` 폴더 · 보스의 StateTree·ASC.
- **반대로, "미구현"이라 적혀 있는데 실은 되어 있던 것**: `PickupRadius`·`XPGain`·`MoveSpeed`(각 14/13/48 hits).
  드리프트는 한 방향으로만 나지 않는다 — 목록을 믿지 말고 양쪽을 다 재 볼 것.
- **D5는 "빠뜨린 기능"이 아니라 의도였다.** 크로스헤어 크기 설정이 없는 이유가 `FPSRGameUserSettings.h`에 적혀 있다 —
  *"크로스헤어는 정직하다: 퍼짐 = 무기의 실제 탄퍼짐이 화면에 투영된 것."* 그래서 겉모습(색)만 설정이다.
  이걸 모르고 "미구현이네" 하고 만들면 설계를 깨는 쪽이라, 근거 문장을 문서에 인용해 뒀다.
- **D6은 문서가 현재 트랙과 정반대였다** — "1P 전용 팔 폐기"라 적혀 있는데 코드엔 `FirstPersonArms`가 있고 **ADR 0006이 진행 트랙**이다
  (ADR 0002를 대체). 폐기된 건 `Blu_FP_Arms`(자동컷 메시)지 1P 팔 경로가 아니다. `PlayerFeel.md` 머리말에 ADR 0006 우선 표시 추가.
- **합류분 3개**: ① `Performance §5` 적 복제 계약을 **실제 3프로퍼티**(`Health`·`MaxHealth`·`bDead`)로 정정 —
  `bDead`는 `Health<=0`에서 파생 가능하지만 **풀 재사용의 `true→false` 엣지 감지**용이라 유지(비트 하나보다 엣지 정확성이 먼저 —
  놓친 전이 = 유령 시체·체력바 잔존). ② **크로스헤어 2층 구조 등재**(무기 크로스헤어 + **독립 크기**의 기준 도트 + 폴리모픽 페이드조건) —
  어느 SSOT에도 없던 신규 구조였다. ③ `Performance §5`의 "복제 = Push Model" 전제에 **`OpenIssues_Network.md` N-1 상호참조**
  (출시 빌드에선 Push Model이 빠져 전제가 성립하지 않는다).
- 🪤 **자기비판에서 잡은 것**: 표 바로 뒤에 정정 blockquote를 끼웠더니 **마크다운 표가 두 동강 나서** 뒤쪽 6줄이 표로 렌더되지 않았다.
  문서 작업이라 빌드가 안 잡아 준다 — **표 안쪽에 블록 요소를 넣지 말 것**(정정 주는 표 **다음**에).

### 3. CachedRecoil 약참조 전환 — 엔진이 기계적 치환을 막아 뒀다 (`7b65c110`)
`TObjectPtr<UFPSRRecoilComponent> CachedRecoil` 이 **`UPROPERTY()` 밖**이라 GC가 추적하지 않았다.
오늘 안전했던 건 같은 액터의 컴포넌트 배열이 우연히 살려주기 때문이지 계약이 아니다.
**강참조가 아니라 약참조가 정답인 이유** = 이 클래스가 만든 게 아니라 `FindComponentByClass`로 **캐시한 것**이고,
`GetCurrentSpreadDegrees`에 **이미 null 폴백이 있었다**(작성자가 null 가능성을 본 것).
- 🚨 **`TWeakObjectPtr`는 bool 변환이 삭제돼 있다.** `WeakObjectPtrTemplates.h:211` —
  `explicit operator bool() const = delete;` 이고 바로 위 주석이 이유를 적고 있다: *"use Get() once in a function."*
  그래서 `if (!CachedRecoil)` · `if (CachedRecoil && ..)` · `return CachedRecoil;` 이 **전부 컴파일되지 않는다.**
  타입만 바꾸는 1줄 작업으로 보이지만 실제로는 **두 함수를 엔진이 지정한 형태(함수당 `Get()` 1회 → 로컬 raw 포인터)로 재작성**해야 한다.
  이 함정을 헤더 주석에 남겼다.
- **동작 변화 없음**: 컴포넌트가 살아 있으면 종전과 동일, 파괴되면 종전엔 댕글링이던 것이 null로 떨어져 **기존 폴백 경로**를 탄다.
- 검증 = UBT `FPSRogueliteEditor Win64 Development` **`Result: Succeeded`** + **경고 0건**(대상 cpp를 강제 재컴파일해 로그 전문 검색 —
  tail만 보면 컴파일 중 경고를 놓친다) + 헤드리스 스모크 `FPSRoguelite.Smoke.ModuleLoads` **`Result={Success}`**.

### 범위 밖 발견 — 고치지 않고 보고만
**`Workflow.md §6-6`의 "D:/E: 머신의 현 코드 빌드 대상 = `FPSRoguelite2`"가 스테일로 보인다.**
실측 `FPSRoguelite2` HEAD = `2114637c`(한참 낡음)이고 최근 세션들은 전부 이 클론에서 빌드했다. 이번 스코프 밖이라 손대지 않았다.

---

## 🛰️ 시뮬레이티드 프록시가 슬라이드·벽매달림을 로컬로 재결정하고 있었다 (2026-08-10, `fix/slide-proxy-sim-and-bonus`)
> 보드 4행("검증·테스트")을 한 번에 처리해 달라는 요청에서 시작했는데, **4행 중 검증 작업은 하나도 없었다.**
> 2건은 실제 버그 수정, 1건은 착수 불가, 1건은 사용자 작업 영역. 전제 정정 후 2건만 진행.
> 출처는 전부 `fix/slope-slide-duration` 머지 전 레드팀 리뷰(2026-08-10)다.

### 원 보드 행은 "왜곡 여부 검증 필요"였으나 확정 결함이었다
프록시가 진입 임펄스를 복제 속도에 재적용한다는 의심을 엔진 소스로 끝까지 확인한 결과 **전부 성립**했다.
1. `SimulateMovement`(engine `CharacterMovementComponent.cpp:2159`)가 `UpdateCharacterStateBeforeMovement`를 **:2226에서 호출**
2. **UE 5.7 `ACharacter::OnRep_IsCrouched`가 프록시에서 `bWantsToCrouch = true`를 직접 세팅**한다 — 구버전과 달라진 지점이고, 이게 성립 조건이었다. `bWantsToCrouch` 자체는 복제 프로퍼티가 아니라(헤더 `UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly)`) "복제 안 되니 안전하다"고 넘기기 쉽다
3. `CanEnterSlide()`의 나머지 항도 프록시에서 전부 통과 — `Velocity`는 `ReplicatedMovement`로 오고, `SlideCooldownRemaining`은 프록시 로컬에서 0
4. **결정타 = engine :2263 `MoveSmooth(Velocity, ...)`** — 프록시가 그 속도를 실제로 적분한다

즉 ×1.5 진입 임펄스뿐 아니라 곡선 감쇠·슬로프 보너스·헤딩 조향까지 매 프레임 프록시에서 돌며 복제 위치와 싸우고 있었다.
엔진의 `bZeroReplicatedGroundVelocity` 완화책(:2241)은 *정지 상태*만 커버해서 하필 슬라이드 진입 속도 조건을 만족하는 케이스엔 안 걸린다.

### 벽 매달림도 같은 결함 — 그리고 더 심각했다 (범위 밖이었으나 같은 함수·같은 근본 원인)
`StartWallHang`이 프록시에서 `SetMovementMode(MOVE_Custom, CMOVE_WallHang)`를 로컬 실행하고, `WallHangElapsed`가 로컬 누적돼
**서버가 아직 매달림이라 말하는 동안 프록시만 먼저 떨어진다.** 게이트 하나로 함께 막았다.

### ⚠️ 1차 게이트는 절반짜리였다 — 레드팀이 잡아냈다
`UpdateCharacterStateBeforeMovement`만 막고 **`PhysCustom`을 놓쳤다.**
**engine `MoveSmooth`는 `MovementMode == MOVE_Custom`이면 프록시에서도 `PhysCustom(deltaTime, 0)`으로 직행하고 곧장 return 한다(`:6808-6813`).**
복제된 `CMOVE_WallHang`에 들어간 프록시는 매 프레임 `PhysCustom`을 도는데, `WallNormal`은 프록시에 의도적으로 전달되지 않아
(헤더 `GetWallYawForDisplay` — "Deliberately NOT fed back into WallNormal") **ZeroVector**다. 글라이드·벽스틱·좌우 투영이 전부
영벡터 기준으로 계산되고, 슬립 착지 시 `StopWallHang → SetMovementMode(MOVE_Falling)`이 프록시에서 실행된다 — 1차 커밋이 막았다고
주장한 바로 그 실패 클래스가 다른 경로로 남아 있었던 것.
게다가 **1차 게이트가 `UpdateWallHang`을 막으면서 `WallHangElapsed`가 프록시에서 0에 동결**돼
`GetWallEntryMomentumAlpha()`가 계속 최대값이 되고, `Lerp`가 매 프레임 진입 속도를 다시 써 넣어 넷 교정을 즉시 되돌리는
**회귀까지 동반**했다. → `PhysCustom`의 `CMOVE_WallHang` 경로에 동일한 `ROLE_SimulatedProxy` 조기 반환 추가.

### 무곡선 슬라이드 슬로프 보너스 이중 가산 — 2차 시도 만에 제대로 고쳤다
무곡선 분기의 `TargetSpeed = Velocity.Size2D()`는 **직전 프레임이 써 넣은 값이라 누적 보너스를 이미 품고 있는데**,
최종 단계에서 누적 보너스를 통째로 다시 더하고 있었다(매 프레임 전체 재가산 → 초선형 가속).
- **1차 시도(틀림)**: 클램프 **이후** 차분을 증분으로 썼다. `SlideSlopeSpeedBonus`는 슬로프가 더한 양의 **총계(gross) 카운터**인데
  브레이킹이 동시에 속도를 빼내므로, **실속도가 상한 근처가 아닐 때 누적기가 먼저 포화**한다 → 그 순간부터 증분 0 →
  **내리막 중간에 슬로프 가속만 조용히 사라진다.** 커밋 메시지의 "포화하면 상한이 그대로 성립"이라는 자기 정당화가
  **결과 상한과 총획득량 제한을 혼동**한 것이었고, 후자는 `SlopeAccelerationScale` 계약("The **result** is still bounded by
  SlideMaxSpeed")에 없다.
- **2차(채택)**: 무곡선 분기는 **클램프 전 원시 증분**을 적분. 누적기 클램프는 곡선 분기 전용으로 남긴다.
  곡선 분기는 매 프레임 `SlideEntrySpeed`에서 재구축돼 이력이 없으므로 **누적 전체 가산이 맞다** — 두 분기의 요구가 다르다.
  후진 캡 `Min`이 누적 이득을 잘라도 원시 증분이 계속 더해져 회복된다(1차 방식은 영구 소실이었다).
- 현재 콘텐츠는 곡선을 쓰므로 **PIE로는 안 잡히는 결함**이다. 곡선 미할당 무기·상태에서만 재현된다.

### 동반 수정 — `SlideBlend` 소스 교체
게이트를 넣으면 프록시의 `bIsSliding`이 항상 false가 되어 **원격 슬라이드 포즈 가중치가 0으로 굳는다.**
→ `AdvanceStanceBlends`의 소스를 `bIsSliding` → **`IsSlidingForDisplay()`**(권한·로컬조종은 정확값, 프록시는 복제된 `bSlidingVisual`)로 교체.
헤더에 "GetSlideBlend는 프록시에서 신뢰 불가"로 문서화돼 있던 제약이 이로써 해소됐다.
⚠️ 단 **프록시 쪽은 넷 업데이트 스냅샷 기준**이라 업데이트 사이에 시작·종료한 슬라이드(점프캔슬 2프레임)는 여전히 안 잡힌다 —
그게 `GetSlideVisualSerial()`이 존재하는 이유다. 1차에 이 한계를 헤더에서 무단 삭제했다가 레드팀 지적으로 복원했다.

### 검증
UBT Development 빌드 exit 0 · 헤드리스 스모크 `FPSRoguelite.Smoke.ModuleLoads` `Result={Success}` (둘 다 3커밋 반영 상태 측정).
레드팀 게이트(§6-6-1) 1회 — **P1 0건 / P2 3건 / P3 1건, 4건 전부 수용·수정, 기각 0건.**
머지 판정 = Fable(§6-5-2 모드 B, 코어 갈래 T3 복제 경로) → **통과**. 지적 원장 전문은 머지 커밋 `f2f5a1ec`에 있다(명세 파일 없는 유닛).

### 같이 요청됐으나 하지 않은 것
- **[이동] 슬라이드 SlopeTimeScale 경계 유닛테스트 — 착수 불가로 보류(상태 `대기` 유지).**
  대상 브랜치 `fix/slope-slide-duration`이 **origin·로컬 모두에 없고**(`git branch -a`), `SlideSlopeTimeRecoveryCap` 심볼도 소스 전역 0건이다.
  main의 공식은 `FMath::Max(0.0f, 1.0f - (SlopeAlignment * SlopeTimeInfluence))`로 **음수가 될 수 없어**
  보드가 요구한 `scale < -cap` 클램프·되감기·`SlideElapsed` 0-플로어 케이스는 **대상 코드 자체가 부재**하다.
  ⚠️ **같은 리뷰에서 나온 행들의 인용 라인번호가 main 대비 3줄씩 밀려 있다**(795/804 → 792/801) — 미머지 브랜치 스냅샷 기준이라는 방증.
  브랜치가 origin에 도착하면 원래 스펙대로 작성한다.
- **ABP_Blu_Body 죽은 상태머신 정리 — 건너뜀.** 행 자체가 "AnimBP 그래프 편집 = 사용자 작업 영역, Claude는 조회·진단·가이드까지"로 못박고 있다.

### 후속으로 남긴 것 (전부 비차단)
- `CMOVE_WallHang` 프록시는 넷 업데이트 사이 로컬 전진이 없다 — 원격 벽매달림 충실도가 `ReplicatedMovement` 빈도 + 메시 스무딩에 전적으로 의존.
  글라이드가 저속이라 수용 가능하고 코드 주석에 의도로 문서화. 뚝뚝 끊겨 보이면 **NetUpdateFrequency 튜닝 사안이지 코드 결함이 아니다.**
- **곡선 분기는 슬로프 보너스가 후진 배수 뒤에 가산돼 후진 내리막이 후진 캡을 넘어 `SlideMaxSpeed`까지 갈 수 있다.**
  main에도 동일한 기존 의미론이라 이번 머지의 결격 사유는 아니나 **디자인 확인 사안**이다.
- `OnMovementModeChanged`가 프록시에서도 실행되며 `JumpCurrentCount`·`bWallHangConsumed`를 프록시 로컬로 쓴다(현재 무해, 권한 가드 부재).
- 폰이 Autonomous → Simulated로 전환되면(관전/빙의 해제) 게이트 때문에 stale `bIsSliding = true`가 그 머신에 잔존할 수 있다(로컬 독자 한정, 표시 경로 무관).

---

## 🔄 장전 = 임시 "내렸다 올리기" 포즈 + 벽 매달림 장전 차단 (2026-08-10, `phase/fparms-reload-sequencer`)
> **사용자 PIE 실측 확인 완료.** 원래 이 브랜치는 "1인칭 장전 애니 Sequencer 편집"으로 시작했는데,
> 조사 중 **배선 결함**이 나왔고 이어 사용자가 **애니 편집 자체를 접기로** 결정해 산출물이 바뀌었다.

### 🚨 착수 조사에서 나온 것 — 장전 몽타주가 다른 스켈레톤을 가리키고 있었다
`DA_Weapon_Rifle.ArmsReloadMontage` = **PWAS 데모 폴더의 `AM_FP_RifleReload`(`S_Mannequin`, 161본)** 인데
실제 팔은 `SKEL_LPAMG_Character`. 총 고정 처리까지 끝낸 프로젝트 자체 몽타주 `AM_FP_Rifle_Reload`
(`dc4b13bd`)는 **참조 0건**으로 떠 있었다. **이름이 언더바 하나 차이**라 눈으로는 구분이 안 된다.
- **UE 5.7은 이걸 막지 않는다** — `Montage_PlayInternal`이 스켈레톤 **널만** 검사하고(`AnimInstance.cpp:2396`),
  "wrong Skeleton" 경고는 널일 때만 뜬다. **에러도 경고도 없이 어긋난 본으로 재생된다.** → `Troubleshooting.md` **A19**.
- 사용자 증상 신고가 정확히 "움직이긴 하는데 이상하다"였다. 사용자가 DA를 고쳐 배선은 복구.

### 사용자 결정 — 애니 편집을 접고 포즈로 대체
배선을 고친 뒤에도 남는 문제가 **팔 메시/스키닝 수준**이라(모델링부터 재작업) 정식 장전 애니는 보류.
대신 **"장전 동안 팔·무기를 화면 밖으로 내렸다가 끝나면 올린다"** 로 대체.
- ⚠️ **홀스터를 재사용하지 않는다** — 사용자 명시("홀스터는 예시"). 홀스터 값을 건드리지 않고 장전만
  따로 튜닝할 수 있어야 하고, 나중에 애니가 들어오면 장전 쪽만 걷어낼 수 있어야 한다.
  → **자기 알파·자기 값·자기 전환시간을 가진 독립 포즈**로 신설.
- 무기 DA에 🚧 **`ReloadPose` + `ReloadBlendDuration`**(기본 0.12s). **스위치가 곧 값이다** — Offset/Tilt가
  전부 0이면 비활성. 별도 bool을 두지 않은 이유 = 방아쇠가 "장전 중인가" 하나뿐(`bForceHolsteredPose`가
  bool인 건 홀스터 포즈가 이미 벽 매달림에 쓰이고 있어 **두 번째** 방아쇠가 필요해서였다).
- ⚠️ **공간 규약은 홀스터와 같다(카메라 공간, 팔 전체)** — 슬라이드/공중의 총-공간이 아니다. 총만 내리면
  어깨가 카메라에 고정돼 손 IK가 총을 놓고 팔만 화면에 남는다(홀스터에서 이미 실증). 패널 저작 불가.
- 저작값: 라이플 `Offset.Z = −40`(홀스터와 동일 — 화면 밖이 검증된 값) · 「재장전 몽타주(1인칭 팔)」 비움.
  `IsDataValid`에 **둘 다 채우면 경고**(화면 밖에서 안 보이는 몽타주가 도는 셈) 추가.

### 벽 매달림 = 장전 취소 + 장전 입력 차단 (사용자 요구)
포즈가 겹쳐 쌓이는 문제를 없애는 김에 사용자가 규칙 자체를 정했다. **술어를 새로 만들지 않고
`CanFireInCurrentState()`를 그대로 읽는다** — "양손이 벽에 있다"가 발사 게이트가 닫히는 이유와 **동일**하고,
사유가 하나면 술어도 하나여야 드리프트하지 않는다(이름이 `Fire`인 건 남은 부채로 기록).
- **서버 권위** = `UFPSRWeaponInventoryComponent::StartReload` (프리즈 게이트 옆). **여기가 단일 초크포인트**라
  수동 R키·ReloadOnKill 카드·무기교체 재개 **3경로 전부** 한 번에 덮인다.
- **클라 입력** = `Input_Reload` (거부될 RPC를 안 보냄 — `Input_EquipSlot`/`ServerEquipSlot`과 같은 대칭).
- **진행 중 취소** = `OnMovementModeChanged`에서 `CancelReload()`. 내부 authority 검사가 있어 **한 호출부로
  모든 머신에서 옳다**(서버가 취소 → 오너는 `OnRep_Reloading`으로 수신). 시뮬 프록시에서도 도는데 무해.
- **재개는 없다** — 벽에서 내려온 뒤 R을 다시 눌러야 한다(`PendingReloadSlot`은 호출 전에 이미 비워짐).
  요청("취소")과 일치. 알려진 비용: 벽에 매달린 동안 지연 발사체가 킬을 내면 ReloadOnKill이 1회 무효.

### 곁다리로 확인해 둔 것 (다음 사람이 다시 재지 않게)
- **캐시는 전용 bool** `bCachedHasReloadPose`로 뒀다. `CachedHipMotion`은 무기가 있을 때만 대입되고
  **해제 시 안 비워져서**, 방아쇠를 거기서 읽으면 무기를 내려놓은 뒤에도 낡은 요청이 알파를 끌어내린다
  (`bCachedForceHolsteredPose`가 이미 밟은 함정).
- 장전은 `RefreshWeaponVisibility`를 부르지 않는다 — 홀스터의 지연-숨김은 `bHideForWall`일 때만 동작하고,
  장전은 메시를 숨기는 사유가 아니다(포즈로만 화면을 비운다).
- **원본 클립 실측**(편집을 접었어도 값은 유효): `FP_Rifle_Reload_GunLocked` = 2.5s · **600fps · 1501키** ·
  애디티브(base `A_FP_Rifle_Pose`) · 본 트랙 79. 왼손 최저 −30.3cm@0.80s / 왼쪽 최대 +31.8cm /
  오버슛 +2.65cm@1.90s / **끝 0.40초가 완전 무변화**(클립의 16%). `hand_r`은 전 구간 고정(총 고정 처리 유효).
  무기 `ReloadTime=1.5s`라 2.5s 클립은 **1.67배로 압축 재생**된다.
- **축 규약**(LPAMG 팔): `thigh_l`=+X / `thigh_r`=−X 로 확정 → **+X=왼쪽 · +Y=앞 · +Z=위**.
- Persona 프리뷰는 애디티브 기준 포즈를 **제대로 깐다**(`UDebugSkelMeshComponent`+`UAnimPreviewInstance`).
  "떠 있는 팔뚝"은 팔만 있는 메시라서지 프리뷰 결함이 아니다 — 어셈블러 주석의 근거 그대로.
- 🚨 **`Edit With FK Control Rig`의 부작용**: `Driving_<애니이름>` **레벨 시퀀스를 애니와 같은 폴더에 생성**하고
  **현재 레벨에 `SkeletalMeshActor`를 스폰**한다. 게다가 시퀀서 디스플레이 레이트를 클립 그대로(**600fps**)
  잡아 스크럽이 사실상 불가능하다. 레벨 에디터가 열려 있고 PIE가 꺼져 있어야 동작한다(아니면 조용히 중단).
  → **빈 레벨에서 할 것.** 이번에 실제로 `TestWorld.umap`이 더러워져 되돌렸다.
- 총 고정(`dc4b13bd`) 파생 스크립트는 **커밋되어 있지 않다** — GunLocked를 재생성하려면 A16 공식으로 다시 써야 한다.

---

## 🤜 아이들 미저작 무기 = 항상 수납 포즈 (맨손 임시 조치) (2026-08-10, `phase/melee-force-holster`)
> 커밋 `2ae814a2`(기능) → `6e121012`(맨손 경로 결함). **사용자 PIE 실측 확인 완료.**

**요구**: 3번 슬롯 = 근접 칸. **처음엔 맨손으로 시작하고 근접 무기를 얻으면 교체**되는 형태인데,
맨손은 1인칭 아이들이 미저작이라 팔이 엉뚱한 포즈로 화면에 뜬다 → **홀스터 포즈를 디폴트로 써서 팔을 화면 밖에 둔다.**

- 무기 DA 프로파일에 **`bForceHolsteredPose`**(「🚧 항상 수납 포즈(임시 — 아이들 미저작 무기)」) 신설.
  켜면 그 무기를 든 동안 홀스터 알파가 1을 향한다. **아이들 저작 후엔 체크만 끄면 되고 코드는 안 건드린다.**
- ⚠️ **이 프로젝트 원칙을 의도적으로 어긴다** — "수납이면 못 쏜다"(홀스터 판정 = 발사 게이트 = 한 술어)를 깨고
  **수납해 둔 채 근접 공격은 되게** 둔다(사용자 결정 = 2번 안). 기각안: ①공격도 차단 → 근접 무기를 못 씀
  ③공격 시에만 팔 올림 → 아이들 저작되면 버려질 배선. 임시성을 **필드명 🚧 + DA 주석 + 코드 주석 3곳**에 명시.
- 캐시는 **전용 bool**로 둔다: `CachedHipMotion`은 무기가 있을 때만 대입되고 **해제 시 비워지지 않아**,
  거기서 읽으면 무기를 내려놓은 뒤에도 낡은 요청이 홀스터 알파를 계속 끌어내린다.
- **부수 개선**: 장착 시 홀스터 알파 스냅을 **수납 방향으로만** 적용 → 지상 스왑이 "내려갔다 올라오는" 연출로 바뀐다.
  이전에 범위 밖으로 미뤄둔 "무기 스왑 홀스터 모션"이 **별도 배선 없이** 해결됐다. 벽/공중 스왑의 팝 방지는 그대로.

### 🚨 맨손일 때 팔 포즈 계산이 통째로 건너뛰고 있었다
**증상**: 맨손에 "항상 수납"을 켜고 **홀스터 오프셋을 아무리 키워도** 팔이 안 내려가고 손끝이 화면에 남는다.

**원인 = 값이 아니라 실행 경로.** `UpdateAimDownSights`의 첫 게이트가 `if (!ActiveWeaponMesh) return;` 인데
**맨손은 보여줄 무기 메시가 없어 `ActiveWeaponMesh == nullptr`** 이다. 함수 전체가 조기 반환되면서 홀스터 레이어도
같이 건너뛰어졌고, 팔은 **직전 무기 기준으로 계산된 마지막 상대 트랜스폼에 얼어붙어** 있었다(내려가다 만 게 아니라
애초에 아무도 안 쓰던 상태).
→ 게이트를 **"무기 메시도 없고 팔도 없을 때"만 반환**하도록 변경. 팔은 그 자체로 움직일 대상이라 무기 유무와 무관하게
솔브 대상이 될 수 있어야 한다. 무기 의존 구간이 맨손에서 안전한지 전수 확인(ADS = AimComp null → bAiming false로 꺼짐 /
힙 = `bCachedHasHipMotion` 게이트 / 무기 직접 조작 분기 = 전부 `!bSolvingArms` 뒤라 진입 불가)하고 근거를 주석에 남겼다.

> 💡 **이 세션에서 두 번 반복된 패턴**: "값을 바꿔도 화면이 안 변한다"는 값의 문제가 아니라 **그 값을 읽는 코드가
> 아예 안 도는 것**이었다(여기 + ADS 조준 소켓 무음 폴백). 값을 더 키워보기 전에 실행 경로부터 확인할 것.

---

## 🎯 홀스터 팔-공간 전환 + ADS 조준 정렬이 안 걸리던 원인 (2026-08-10, `phase/fparms-holster-armspace` · `phase/fix-ads-sway`)
> 위 홀스터 트랙의 직접 후속 2건. 커밋 `23c00056`(팔-공간) → `0c729e6f`(머지) · `5f954de1`(ADS). **둘 다 사용자 PIE 실측 확인 완료.**

### 홀스터만 팔-공간으로 되돌림 — "수납 시 팔까지 화면 밖으로"
사용자 요구: 홀스터 상태에서 **무기뿐 아니라 손·팔도 화면에 아무것도 안 남게.** 단 **팔 가시성을 껐다 켜는 코드는 만들지 않는다**
— 나중에 1인칭 홀스터 애니메이션이 들어오면 그 장치를 다시 걷어내야 하므로, **포즈로 화면 밖까지 보내는 선에서 끝낸다**(사용자 결정).

**총-공간 홀스터로는 불가능하다**: 어깨가 카메라에 고정돼 있어 **팔이 뻗는 거리를 넘기는 순간 손 IK가 총을 놓는다**.
화면 밖까지(30~60cm) 내리면 "총만 나가고 팔은 아래로 뻗은 채 잔류"가 된다. 팔을 움직이면 총이 팔에 붙어 있어 같이 빠지므로,
**홀스터 레이어만** `UpdateAimDownSights`(카메라 공간)로 복귀. 슬라이드·공중은 총-공간 유지(디테일 패널 저작 루프 보존).
- 부수 정정: 홀스터 지연 게이트를 `bWeaponAttachIsGunAnchor` → `bFirstPersonSplitActive`로 되돌림(팔은 gun-anchor와 무관하게 내려간다).
- ⚠️ **같은 struct 안에서 공간 규약이 갈린다** — 슬라이드/공중 = 총-공간(패널 숫자와 1:1), 홀스터 = 카메라 공간(팔 전체).
  DA 주석에 이유까지 명시했다. 홀스터 값은 패널로 저작 불가(팔 트랜스폼은 매 프레임 덮어써진다) → "값 넣고 실행해 확인" 방식.

### 🔍 ADS 조준 정렬이 안 걸리던 원인 = 코드가 아니라 DA 값 하나
**증상**: 조준하면 **FOV 줌은 되는데 총이 조준경 위치로 안 옮겨진다.** ADR 0002 실측상 이 총 조준경은 화면 중앙에서
**+29.7°/−27.1°** 벗어나 있어, 위치 보정이 없으면 조준해도 조준경이 화면 밖에 남는다.

**기각한 가설 4개(같은 조사 반복 방지)**:
1. ~~조준 소켓 미저작~~ — 라이플 DA `SOCKET_Aim` 정상, **기본 레드닷(`SM_Wep_Mod_Reddot_01`)과 진화 스코프(`SM_Wep_Mod_Scope_09`) 둘 다 소켓 실존**.
2. ~~파츠 조립 실패~~ — 진화 단계 데이터까지 정상.
3. ~~입력 미바인딩~~ — `IA_ADS` 정상. ⚠️ **처음 "미바인딩"으로 읽힌 건 언리얼이 폐기한 `mappings` 속성을 읽은 탓**
   (실제 데이터는 `DefaultKeyMappings`, 16개 매핑). 폐기 속성은 **부분적으로 낡은 값을 조용히 반환한다** — 오판 유발.
4. ~~홀스터 트랙 회귀~~ — ADS 솔브 코드는 트랙 전후 무변경(게이트 한 줄만 추가).

**실제 원인**: 라이플 DA의 `ADSPositionBobScale` = **1.0**(최댓값). 이 값은 조준위치↔힙위치를 Lerp하는데,
1.0이면 결과가 **힙위치 그대로** → 위치 보정이 통째로 소멸(각도만 정렬). FOV 줌은 이 값과 무관해 정상 → "ADS가 반쯤 되는" 착시.

**더 근본적 결함**: 그 값의 문서화된 동작("힙 쪽으로 섞으면 애니메이션 흔들림이 되살아난다")이 **1인칭 팔 구성에서는 거짓**이다.
힙 기준 = `ArmsHipRelativeTransform` = **BeginPlay 스냅샷 상수** → 되살릴 움직임 자체가 없고, 섞으면 흔들림이 아니라
**고정된 어긋남**만 생긴다. 팔 없던 시절(애니메이션되는 손에서 힙을 매 프레임 재읽기)에만 참이던 값 → **제거**(근거는 코드 주석에 보존).

**같이 처리(커밋 `5f954de1`)**:
- `ADSSwayIdleScale`(정지 시 비율, 기본 0.35) 신설 — 조준 흔들림이 **이동 중에만** 살아 있어 정지 시 화면이 얼어붙던 문제.
  게이트가 아니라 스케일이라 출발/정지 순간 단차가 없다.
- `ADSSwayFreeHorizontal/VerticalCm`(기본 0=끔) 신설 — ⚠️ **조준경 축 회전으로는 좌우/상하 평행이동이 원리적으로 불가능하다**
  (강체는 한 점을 고정한 채 평행이동할 수 없다 — "총만 흔들고 조준경은 화면에 박기"는 **회전으로만** 가능).
  조준점이 같이 흔들려도 된다고 판단할 때만 켜는 옵트인. 사용자 원칙(**조준 = 플레이어 입력의 것, 연출이 움직이면 안 됨**)은 기본값에서 유지.

**미처리(별도 행)**: 무기 DA 5종(바주카·차지레이저·LMG·샷건·스나이퍼)이 **조준 소켓이 빈 채 ADS만 켜져 있다** → 붙이는 순간 같은 증상 잠복.
그리고 이 실패는 **경고 한 줄 없이 조용하다** — "ADS 켜짐인데 조준 소켓 미해결" 경고 추가가 필요(오늘 원인 규명이 오래 걸린 직접 원인).

---

## 🔫 1인칭 뷰모델 — 홀스터/드로우 + 상태별(슬라이드/공중) 모션 + 벽 매달림 발사 게이트 (2026-08-10, `phase/fparms-holster-statemotion`)
> 보드 행 = "1인칭 무기 뷰모델 애니메이션 — 수납/꺼내기(홀스터) + 상태별(슬라이딩/공중) 무기 모션 분리"(하이).
> 커밋 `0ab4d732`(1차 구현) → `23a237e4`(적용 대상 재설계). **사용자 PIE 실측 확인 완료.**

**요구(2026-08-07 사용자 지시)**: ①무기 못 쓰는 상태(벽 매달림)에서 즉시 소멸 대신 6시로 수납/꺼내기
②걷기 밥이 슬라이딩·공중에도 그대로 재생되던 것을 상태별로 분리(슬라이드=틸트 로우 포즈·사격 가능, 공중=살짝 +Z·밥 없음).

### 스키마는 이미 있었다 — 이번 작업 = 그 **소비** 구현
`FFPSRWeaponStatePose`(Offset/Tilt) + `SlidePose`/`SlideBobScale(0)`/`AirbornePose`/`AirborneBobScale(1)`/
`HolsterPose`/`HolsterDuration(0.25)` 는 총모션 명세 §6에서 이미 `FPSRWeaponDataAsset.h`에 정의돼 있었고 소비처가 0건이었다.
`CachedHipMotion`이 같은 struct라 상태 포즈까지 이미 캐시된다 — 추가 캐시 불필요. 신규 필드는 `AirborneBlendDuration` 1개뿐
(공중만 `IsFalling()`이라는 **이진 소스**라 스무딩 시간이 데이터로 필요하다. 슬라이드=`GetSlideBlend`, 홀스터=`HolsterDuration`은 이미 있음).

### 🔁 적용 대상 재설계 — 팔(카메라 공간) → 총(부착 기준 성분 덧셈 델타)
1차 구현(`0ab4d732`)은 상태 포즈를 **팔**에 카메라 공간으로 합성했다. **사용자 지적으로 전제가 틀렸음이 드러났다**:
- 사용자의 저작 루프 = **PIE 디테일 패널에서 WeaponMesh 트랜스폼을 직접 만져 목표 포즈를 잡고 그 숫자를 DA로 옮기는** 방식.
- 팔에 합성하면 ① WeaponMesh 패널 수치가 **영영 안 바뀐다**(팔에만 쓰고, 총의 상대 트랜스폼은 부착 시 1회만 쓰인다)
  ② 회전 중심이 다르다(팔 원점 = 카메라 근처) → 사용자가 저작한 포즈가 **재현되지 않는다**.
→ `23a237e4`에서 **총의 부착 기준값에 대한 성분 덧셈 델타**로 전환. `Final = Base + Σw·(Offset/Tilt)`, Scale 불가침.
**성분 덧셈을 쓴 이유 = 패널 3개 숫자와 1:1 대응**(쿼터니언 합성은 숫자가 예측 불가라 저작 루프가 닫히지 않는다).
절대값(고정 수치) 안은 기각 — 상태끼리 블렌드가 안 되고 무기마다 base가 달라 재사용 0.

**실증**: DA `슬라이드 포즈 → 회전(틸트)`에 **Roll −13 / Pitch −55 / Yaw +30** 입력 → 슬라이드 중 패널이 **−30/−55/105**로 읽힘
(기준 −17/0/75 + 델타). 사용자 확인 "원하는대로야".

### 같이 잡은 함정 3개 (이게 없으면 눈에 보이는 버그)
1. **손 IK 추종** — 그립 타깃은 부착 시점 상수라 총이 움직이면 낡는다. 특히 **왼손(포어그립, 총 원점에서 멀다)** 이 크게 어긋난다.
   `CachedWeaponStatePoseDelta = Base⁻¹·Final` 을 그립·파츠 프레임 리더에 합성. ⚠️ `ComputeGripInGunFrame`이 마지막에 스케일을
   1로 정규화하고 base는 `WeaponAttachScale`(0.85)을 물고 있어 어긋날 것처럼 보이지만, **역행렬에서 스케일이 정확히 상쇄**돼
   순수 강체 델타가 된다(검산 완료 — `Bs⁻¹·Fs`의 스케일 = (1/S)·S = 1, 이동 성분도 S가 소거).
2. **캐시 오염** — `GetWeaponRootPlacementInGunFrame`이 총의 **라이브** 상대 트랜스폼을 읽고 있었다. 델타가 실린 채
   그립/파츠 캐시가 재계산되면(**재장전 중 파츠 재빌드가 현실적 트리거**) 그 순간의 상태 포즈가 캐시에 **영구히 구워진다**.
   건-앵커 경로에선 `CachedWeaponAttachRelative`(부착 기준)를 읽도록 교체.
3. **1프레임 지연** — 팔 애님이 먼저 도는 틱 순서라 델타를 `UpdateAimDownSights`(WeaponFire 틱)에서 계산하면 손 IK가 1프레임 늦는다
   (이 프로젝트가 이미 한 번 잡은 건-앵커 랙과 같은 부류). 알파 진행+델타+총 write를 `AFPSRCharacter::Tick`으로 옮기고
   `FirstPersonArms->AddTickPrerequisiteActor(this)` 추가 → 체인 **CMC → Character → Arms → WeaponFire**.
   ADS 사이트 글루는 팔 포즈가 확정된 뒤라야 맞으므로 WeaponFire 틱에 **잔류**.

### 홀스터 가시성 — 단일 소유자 유지
`RefreshWeaponVisibility`가 계속 `SetVisibility`·`bWeaponHidden` 래치의 유일한 주인이고, 지연은 `bHideForWall` 입력 1식으로만 들어간다.
가드 4개: 이미 벽 숨김 결정 + **owner-local**(프록시는 알파가 안 돌아 자동 배제 = **리모트 3P는 기존 즉시 숨김 유지** — 벽 클립은 전 머신
맨손이라 지연 표시는 손 옆에 총이 뜨는 버그다) + 스플릿 활성 + `bCachedHasHolsterPose`(**미저작 무기 = 레거시 즉시 숨김, 회귀 0**).
하강 완료(알파 1.0 상승 엣지)에서만 재평가 1회 호출 — 드로우 방향은 `OnMovementModeChanged`가 이미 즉시 재평가하므로 대칭 호출 불요.

### 부수 처리 — 호출자 0건이던 게이트 2개 배선
`CanFireInCurrentState()`·`GetSpreadMultiplier()`가 **호출자 0건**이었다 → **벽 매달림 중 발사가 코드상 전혀 안 막혀 있었다**(무기만 숨김).
- 클라 UX 게이트: `CanFire()`(자동/버스트 루프 정지) + `FireOneShot()` 상단(**코스메틱·GA 발동보다 앞** — 막힌 샷의 총구화염/반동 누출 방지).
  밀리도 포함(벽 = 양손 사용 중).
- **서버 권위 게이트**: 신규 `UFPSRGameplayAbility::IsFirePermittedByMovementState()`를 발사 GA 4종의 기존 게이트 블록
  (`IsRunPaused`/`IsAlive` 옆, **`ServerTryConsumeFireInterval` 이전**)에 추가. `HasAuthority` 분기 **밖** — LocalPredicted는 예측 클라+서버
  양쪽에서 돌고 서버 실행이 권위가 된다. `CanActivateAbility` 오버라이드는 기각(패시브 상속 + 기존 관례 = ActivateAbility 내 조기 EndAbility).
- 확산 배율: `ComputeSpreadDegrees`에 `StateSpreadMultiplier` 추가 + 호출자 3곳(HUD/Hitscan/Projectile) 배선 → 크로스헤어와 실제 원뿔이 동조.
- HUD의 `!IsOnWall()` 직독을 `CanFireInCurrentState()`로 통일 — "쏠 수 있나"와 "총이 손에 있나"가 **한 술어**를 공유한다.

### 남은 것 / 알려진 잔여
- 슬라이드/공중 가중 `(1-CurrentADSAlpha)`는 **1프레임 스테일**(ADS 솔브가 뒤에 돌아 구조상 불가피). ADS 전환 중 포즈 비침 정도만 영향.
- **홀스터 포즈를 크게 저작하면 손 IK가 끝까지 뻗어 어색해질 수 있다** — 그 경우 홀스터만 팔-공간으로 되돌리는 선택지(슬라이드/공중은 총-공간 유지).
- DA 저작은 **슬라이드만 완료**. 공중 밥 제거는 `AirborneBobScale=0`을 넣어야 나타난다(기본 1). 홀스터/공중 포즈 값 미저작.
- 검증 = `-NoXGE -DisableUnity` 풀빌드 2회 Succeeded(경고 0) + 사용자 PIE 실측.

---

## 🧾 보드 5건 종결 — 검증 완료 3건 + 범위 조정 1건 + 폐기 1건 (2026-08-10, `main`)
> 사용자 확인분 정리. **코드·에셋 변경 0** — 보드 상태와 이 기록만 바뀐다.

- **검증 완료(3건)**
  - **4b 로코모션 잔여**(8방향 시작·정지 전환 + `Split_Jumps` 108 리타게팅) — ⚠️ **완료커밋을 특정하지 못했다.** `8방향`·`Split_Jump`·`전환`·`리타게` 키워드와 경로(`Content/**/*Split*`) 양쪽으로 전체 로그를 훑었으나 매칭 없음, 작업 트리도 clean. 인접한 `d719e6c0`(Rifle_01 426개 Blu 리타게팅)은 본문에 "**Split_Jumps 108(보류)**"라 적혀 있어 **재료를 들여온 커밋**이지 완료 증거가 아니다. 해시를 임의로 채우지 않고 보드 `완료커밋`을 비워 두었다.
  - **4c 슬라이드·벽 포즈 재저작** — `43730c9e`(8포즈 Blu 반영) → `d9eb9143`(미사용 6개 삭제, `Blu_Wall_TopOut` 포함 = `wall_topout` f0/f12 판단의 결과) → `50d0d686`(쓰는 2개만 재반영, 본문에 "슬라이드 저자세 사용자 재저작" 기록). `cc92d790`·`26786758`은 `PROGRESS.md`만 고친 경위 커밋이라 근거에서 제외.
  - **양손 IK — 무기 기준(`ik_hand_gun`) 구조 전환** — `727a4d7b → ccd4c477 → 6a26dbfc → e36889e4`(`phase/fparms-gunanchor-ik`) → **`d494b64e`**(merge→main, 푸시). 후행 행 "1인칭 팔 트랙 PIE 검증"이 이미 완료·머지된 것이 근거 — 이 행의 산출물을 전제로 한 PIE 검증이 통과했다.
- **범위 조정 종결(1건)** — **근접 3번 슬롯 손맛 세트 + PIE 10항목**: 남은 항목(맨손 몽타주·사운드·VFX, 칼 배정, PIE 10항목)은 **검증 단계가 아니라 추가 작업**이라는 사용자 판단으로 이 행을 닫았다. 해당 작업은 **나중에 별도 신규 행으로 개설**한다. 슬롯 인프라 자체는 `b44b26f7`에 이미 들어가 있고, 그 커밋이 오히려 이 잔여를 만든 쪽이다.
- **폐기(1건)** — **PM 보드 담당 서브에이전트 세팅(`pm-board` + `/board`)**: 각 세션이 작업 **전·후로 보드를 갱신하도록 필수화**(CLAUDE.md 핵심원칙 3 하드 게이트)되면서 전담 에이전트를 따로 둘 이유가 사라졌다. 구현 자체는 이미 머지돼 있었고, 이 행은 그 트랙을 **더 잇지 않는다**는 종결이다.
- ⚠️ **보드 상태 select에 `폐기`가 없다**(축 = 대기/진행중/검증중/결정대기/차단/보류/완료, `Docs/SSOT/Workflow.md` §6-9 (5)). 그래서 폐기 건도 `완료`로 넣고 사유를 행 본문에 적었다 — **선례 동일**(`Blu_FP_Arms` 폐기, `7beef8fc`). 폐기가 반복되면 상태 옵션 추가를 검토할 만하다(스키마 변경 = 사용자 결정).

---

## 🗑️ `Blu_FP_Arms` 폐기 완료 — 참조 0 실측 후 삭제 (2026-08-10, `main`)
> 2026-08-03 제작한 **Blu 스켈레톤 자동컷 1인칭 팔**. ADR 0003 §전환표에서 "폐기 대상"으로 지정됐고,
> 이후 ADR 0006(구매 리그 LPAMG 리타깃)이 확정되면서 이 메시를 쓸 경로가 완전히 사라졌다.
> 원 지시 = `Docs/FirstPersonArms_PWAS_ResumePrompt.md` 7번 항목("참조 0 확인 후 폐기").

- **삭제한 것(4개)** = `Content/Characters/Blu/SkeletalMeshes/Blu_FP_Arms/` 전체
  — `SkeletalMeshes/Blu_FP_Arms.uasset` · `SkeletalMeshes/Blu_FP_Arms_PhysicsAsset.uasset` · `Materials/Body.uasset` · `Textures/Blu_UV_Body_BaseColor.uasset`.
- **참조 0 판정 근거(실측)**: `Content` 하위 **전 `.uasset`/`.umap`을 바이트 스캔**(ASCII + UTF-16 양쪽, 자기 폴더 제외) → **일치 0건**.
  네 에셋 모두 패키지 경로에 `Blu_FP_Arms` 가 들어가므로 이 한 번의 스캔이 4개 전부를 덮는다.
  `Source/`·`Config/` 텍스트 검색도 0건 — 남은 언급은 **문서 3개뿐**(`Troubleshooting.md`, `FirstPersonArms_PWAS_ResumePrompt.md`, ADR 0003).
- ⚠️ **에디터를 끈 상태에서 파일로 지웠다** — 메모리 [[no-force-delete-after-pie]] 규약(스크래치 에셋 삭제는 에디터 종료 후 파일로). 삭제 전 `UnrealEditor` 프로세스 0개 확인.
- 되돌리려면 `git checkout d494b64e -- Content/Characters/Blu/SkeletalMeshes/Blu_FP_Arms` — **git 히스토리에 그대로 남아 있다.**
- ⚠️ 메모리 [[fparms-own-skeleton-track]] 의 "**에셋은 보존**"은 **자체 스켈레톤 트랙(ADR 0004/0005) 산출물** 이야기다. 이 건은 그보다 앞선 **Blu 스켈레톤 자동컷 메시**로, 처음부터 폐기 대상으로 지정돼 있던 별개 자산이다.

---

## 🎯 1인칭 조준 pitch ↔ 3인칭 프록시 시선각 정합 (2026-08-08, `fix/aimoffset-remoteviewpitch`)
> 증상→원인→해결 = [`Docs/Troubleshooting.md`](Troubleshooting.md) **A18** · 보드 행 = "1인칭 조준(카메라 pitch) ↔ 3인칭 프록시 머리/시선 각도 불일치".
> 커밋 `e884fed4`(계측 CVar+클램프+오염 가드) → ABL 콘텐츠 보정 커밋(본 머지에 포함).

**원인** = 복제가 아니라 콘텐츠: AO 샘플 패밀리 전체(센터 포함)가 카메라 수평 대비 아래로 시프트 저작.
샘플 간 상대 델타는 `AnimPose` 실측 45.0°/89.9°로 라벨 그대로 → 패밀리 내부 정확, 절대 기준만 시프트.
**해결** = `ABL_Blu_W2_Rifle` AO 적용 뒤 `Transform (Modify) Bone`(chest, Add to Existing, Component, **Roll −20** 캘리브레이션 확정).

**기각/반증 기록** (같은 논의 반복 방지):
- ~~CalcCamera 서버 자기참조~~ — 엔진 5.7 소스로 반증(`PlayerCameraManager.cpp:788`: 기본
  `bUseClientSideCameraUpdates=true`에서 서버는 원격 PC의 `DoUpdateCamera`를 안 돌림; 서버 캐시 =
  `ServerUpdateCamera` RPC). `FPSRCharacter.cpp` 무변경.
- ~~AO 에셋 범위 부족(±90 미만)~~ — 실사용 AO(`AO_Stand_Aim`/`AO_Crouch_Aim`)는 ±90 풀그리드 정상.
- ~~ABP 배선 오류~~ — T3D 익스포트로 X←AimYaw·Y←AimPitch·MeshSpaceAdditive Alpha=1 확인.

**남긴 인프라**: `FPSR.Debug.AimSync` CVar(1=온스크린 pitch 소스 분해, 2=+0.5s 로그), AimPitch ±90
계약 클램프, 액터 pitch 오염 1회성 경고(엔진 `IsNearlyZero` 가드 우회 감지).
**잔여**: 미참조 AO 에셋 4종 정리(별도 태스크 칩, `Blu_AO_Rifle`은 샘플 빈 껍데기).

---

## 🧾 리팩토링 결정대기 전 항목 종결 — 사용자 결정 8건 + 후속 6행 (2026-08-07, `docs/rpc-failure-policy`)
> 원천 = [`Docs/Refactor_20260806_Report.md`](Refactor_20260806_Report.md) §6·§7. **코드 변경 0** — 결정은 전부 보드 새 행으로만.
> 착수 전 메모리 `refactor-report-stale-premises` 지침대로 **전제 8건을 현재 코드와 재대조**(스테일 0건 추가 확인).
> 보드: 결정대기 행 8개 결정 append 후 완료 · **신설 6행** · 카드 Reserve 행 스코프 합류.

| 원 항목 | 사용자 결정 | 후속 |
|---|---|---|
| §7-1 Push Model 출시빌드 OFF | **C안 — U14 실측 후 결정**(복제 표면 12클래스로 작아 폴백 실비용 추정 불가, 소스 엔진 전환을 추정 위에서 지불 안 함) | U14 측정 행(백로그) + §5 미결 표시 |
| §7-3 ServerAckTopology | **A안 — Gen 클램프 1줄**(위생조치) + 멀티맵 재평가 주석. C(킥)는 §4-3 규약으로 배제 | 새 행(로우) — E안 착수 전 선처리 권장 |
| §7-5 CachedRecoil | **`TWeakObjectPtr` 전환**(소유 아닌 캐시 + 기존 null 폴백 = 약참조가 원 의도) | 새 행(로우) |
| §7-6 적 `bDead` 복제 | **유지 + `Performance.md §5` 계약을 실제(3프로퍼티)로 정정**(엣지 감지 정확성 > 1비트) | 문서 정정 행 합류. **파생 신고**: 재스폰 시 체력바 잔존 버그 → 백로그 행 신설 |
| §7-7 MsgSubsystem 배열 복사 | **보류 — U14 프로파일 후 결정**(재진입 안전장치, 실구독자 수 모른 채 재설계 = 추측 최적화) | U14 측정 행에 포함 |
| §7-8 GetOwnedWeapons | **헬퍼 2개 추가 승인**(추가형, BP 무사) | 「카드 Reserve」 행에 합류(같은 파일 1회 빌드) |
| §6 매직넘버 | **3분기**: ⚡핫패스=**기동 시 1회 캐시 승격**·🟡비핫패스 16개(TimeoutSeconds 누락분 포함) **일괄 승격**·`SpawnGroundHalfHeight`=**런타임 조회 전환**. 🔒잠금 4개는 승격 금지 유지. (핫패스·일괄은 에이전트 권고(유지/개별)를 사용자가 기각한 결정) | 새 행(미듐·**코어 갈래 Fable**) — **이름 승인 게이트 필수**(승격 후 개명 = BP 오버라이드 소실) |
| §7-9 드리프트 12건 | 전 건 방향 확정 — **D7 FF = 설계 재검토, "기본 OFF"로 문서 정정**(컨셉의 FF 기본 ON 설계를 뒤집는 사용자 결정). 나머지 10건 문서→코드, D12 일치 기록 | SSOT 일괄 정정 행(미듐) |

### 같이 알아 둘 것
- **파일 겹침 순서**: `FPSRPlayerState`를 클램프(1줄)·E안·매직넘버(TimeoutSeconds) 세 행이 만진다 → **클램프 → E안 → 매직넘버** 순 권장, 각 행에 메모.
- §7-2(로비 강제이동)는 앞서 **E안 결정완료**(전제 절반 오류 정정 포함), §7-4(WithValidation)는 **§4-3 미채택 확정** — 이 세션 재토론 없음.
- 이 머지의 코드 diff = §4-3 신설 커밋(`c7a7b5a0`)뿐. 문서 갈래라 §6-7 규정대로 레드팀 게이트 대신 diff 자기비판으로 검증.
---

## 🤲 양손 IK 구조 전환 + 그립 실저작 — 무기 기준(ik_hand_gun) (2026-08-07, `phase/fparms-gunanchor-ik`)
> 함정·판정법 = [`Docs/Troubleshooting.md`](Troubleshooting.md) **A12·A13·A14** · 보드 행 = "양손 IK — 무기 기준(ik_hand_gun) 구조 전환".
> 커밋 `727a4d7b`(C++) → `ccd4c477`(라이브 씸) → `6a26dbfc`(콘텐츠). ⚠️ **PIE 2인 교차검증·main 머지 미완**(후행 행).

**사용자 결정**: 손이 총을 끌고 다니는 구조 → **총(1인칭 구도로 저작한 위치)이 기준, 양손이 IK 로 붙는** 구조.
근거 실측 = 저작된 총 위치가 hand_r 손끝 밖 21.15cm(손 길이 18cm) — 애니 손이 총을 못 쥐는 상태였다.

- **구조**: 무기를 hand_r 소켓 대신 **ik_hand_gun 뼈**(팔 FK 체인 밖)에 앵커 — 손 IK 가 총을 못 끈다.
  그립 타깃 = **총-공간 상수**(포즈 무개입 정적 합성, 장착 시점 캐시) — 월드 이펙터의 1프레임 지연 구조 소멸.
  ABP: CopyBone(hand_r→ik_hand_gun) + TransformBone×2 + TwoBoneIK×2(Bone Space + Take Rotation).
- **내부 레드팀 첫 실전**: P1 급 0·지적 8건 전건 수용. 그중 소켓 실재+부모본 등식 2단 게이트가
  **당일 실사용자 오기입**(DA 부착 소켓 칸에 `SOCKET_RightHand ` 공백 포함 오타)을 폴백+경고로 잡았다 —
  구게이트였으면 무기가 무음으로 팔 원점에 부착됐을 사고.
- **저작 결과**: SOCKET_Weapon (-1.30, 26.78, 5.11)/(p0,y75,r-17) · 왼손 실도달(잔여 0.00cm) ·
  손가락 좌우 완성(오른검지 곧게, 왼손 점증 그라데이션 -22~-50, 왼엄지 총열 정렬) ·
  여성형 스케일(전완 0.88·손 0.9, 총 크기는 CopyBone Copy Scale 해제로 보호, 총 위치는 소켓 역보정 0.000000cm).
- **재장전 무수정 확정**: 애디티브(차분)라 Idle 손가락 작업이 자동 유지 — 기준 포즈를 고치면 오히려 오염.
- **에디터 라이브 씸**: 소켓 프로퍼티 변경 → 자동 재부착+캐시 재계산(WITH_EDITOR) — 소켓 튜닝이 PIE 재시작
  없이 즉시 보인다. 저작 루프를 이것이 살렸다.
- 잔여: 왼엄지 미세 형상(사용자, `Scripts/tune_left_thumb.py` 2모드) · PIE 2인 · main `--no-ff` 머지.

---

## 🕸️ 적대 검증 체계 개편 — Codex 페르소나·호출범위 + 머지 게이트 내부 이관 (2026-08-07, `refactor/character`)
> 본문 = **[`Docs/ConsultLoop.md`](ConsultLoop.md) §0-1/§1/§3-1** · **[`Docs/SSOT/Workflow.md`](SSOT/Workflow.md) §6-6-1** ·
> 프라이머 2종 = [`Docs/CodexRedTeamPersona.md`](CodexRedTeamPersona.md)(토론) · [`Docs/InternalRedTeamReview.md`](InternalRedTeamReview.md)(게이트).
> 커밋 `ea888754` → `4dd8393c` → `f81d786e`. 게임 코드 변경 0. **보드 행** = "적대 검증 체계 개편".

**사용자 결정 3차**: ① Codex 페르소나를 백엔드×클라 역할분업 → **웹/앱 적대 레드팀 단일**로 교체 ② 적대 검증 호출을 **코어/리팩토링/설계·구조 사안에만** ③ **최종 결정권 = Claude**(단 종료 사유 + 기각 원장 필수) ④ 머지 게이트는 **외부 Codex → 내부 Fable 레드팀**, 리뷰 하네스도 내부로 이관.

판정 기준을 새로 만들지 않고 §6-5-2 `T1~T5` 트리거를 인용했다 — **같은 개념의 정의가 둘이면 반드시 드리프트하기 때문**(그러고도 계약을 4벌로 갈라 놓아 레드팀에 잡혔다, 아래).

### 실측으로 확정된 것
| # | 사실 | 근거 |
|---|---|---|
| 1 | 🚨 **`codex review`에 적대 페르소나를 넣을 방법이 없다** | ①인자: `--base`/`--uncommitted`/`--commit` 전부 `cannot be used with '[PROMPT]'`로 거부(`--help`의 `[PROMPT]` 표기가 오해를 부른다 — "스테일 의심"은 내 오판이었고 기존 기록이 맞았다) ②`AGENTS.md` 안내: **대조군**(실제 리팩토링 커밋) 리뷰에서 자체 형식 유지, 출력 계약 0건 → **게이트를 내부로 옮긴 직접 원인** |
| 2 | ✅ **내부 Fable 레드팀 첫 가동 성공** | `general-purpose` + `model=fable` + 백그라운드 · 약 14분 · 157k 토큰 · 도구 18회 · **출력 계약 100% 준수**. 느리니 폴링 금지 |
| 3 | 🚨 **빈 출력 = 통과 아님** | 인자 에러·인증 실패면 stdout이 비고 exit 0처럼 보인다. 빈 리뷰 파일을 통과로 오독한 실사고 후 래퍼에 가드(경고+exit 1+미저장), stub codex로 실증 |
| 4 | 🚨 **BOM 없는 `.ps1`은 PS5.1이 ANSI로 읽어 한글이 깨진다** | 기존 결함. 콘솔 메시지뿐이면 미관 문제지만 프롬프트로 나가는 문자열이면 치명 → 래퍼 2개에 UTF-8 BOM |

### 레드팀이 자기 도입 diff에서 잡은 것 (지적 8 + 범위 밖 3, **기각 0건**)
- **[P2] 화이트리스트가 자기무효화 규칙이었다** — "설계 근거를 안 넘긴다"를 성립 조건으로 잡았는데, 근거는 diff·리포·**서브에이전트에 자동 주입되는 `CLAUDE.md`/`AGENTS.md`/`MEMORY.md`** 에 이미 있어 차단 불가(레드팀이 **자기 컨텍스트를 근거로 실증**). 즉 게이트는 정의상 항상 불성립이었다. → 성립 조건을 **①스폰 프롬프트에 변호 없음 ②새어 든 근거는 "안건으로 취급" ③별도 인스턴스**로 재정의.
- **[P2] 게이트 계약이 도입 시점부터 4벌** — SSOT는 4항목인데 파생 3곳이 제각각 축소. → §6-6-1 포인터로 치환.
- **[P2] `git add -A`가 다른 세션의 미커밋 작업을 쓸어담았다** — `4dd8393c`가 존재하지 않는 §6-9를 가리키는 헤더를 포함. **리뷰 대상 diff ≠ 머지 diff**가 되는 게이트 무결성 문제 → §6-6-1에 규칙화(명시 경로 스테이징). 메모리 `shared-worktree-branch-collision` 재현.
- P3 5건: 게이트 실행 실패 처분 미정의(워치독을 토론 전용으로 좁히며 누락) · "외부 목소리 0개" 과장(`GameConfirm.md` 존속) · `ToolingBacklog`가 삭제된 스크립트 위에 설계 · 심각도가 비-코드 diff 미커버(상한 P2 매핑 추가) · 템플릿 "12개 항목" vs §13.

### 남은 것
- 게이트를 **문서 diff에만** 걸어봤다. 런타임 코드에서도 같은 밀도로 잡는지는 첫 코어 유닛에서 확인.
- 외부 모델 교차검증은 **토론 채널(`/consult`·`/plan-consult`)의 Codex만** 남았다 — 여기서까지 빼면 우리가 직접 부를 수 있는 외부 검증이 0이 된다.
- 파생 발견: 카드 뽑기 무제한 `Reserve`(`FPSRCardSubsystem.cpp:164`) → 보드 별도 행(로우).

---

## 🧭 모델 정책 2계층화 — 코어/구조 작업 = Fable 주도 (2026-08-07, `refactor/character`)
> 본문 = **[`Docs/SSOT/Workflow.md`](SSOT/Workflow.md) §6-5-2** · 명세 양식 = **[`Docs/Specs/_TEMPLATE.md`](Specs/_TEMPLATE.md)**. 코드 변경 0.

**사용자 결정**: 코어·구조설계·리팩토링 작업은 **Opus가 아니라 Fable이 플랜·지시·검증**을 맡는다.
Fable이 C++ 플랜과 헤더 수준 인터페이스까지 만들고, Sonnet이 그 명세대로 구현한다.

| 갈래 | 플랜·설계 | 구현 | 검증·판정 |
|---|---|---|---|
| 코어 / 구조설계 / 리팩토링 | **Fable** | Sonnet 위임 | **Fable** |
| 그 외 | Opus | Sonnet 위임 | Opus 직접 |

파이프라인 = **C0** 조사 → **C1** Fable 설계·명세(`Docs/Specs/`에 커밋) → **C2** Sonnet 축자 구현 → **C3** Fable 판정.
트리거 5개(신규 서브시스템 / 다중 클래스 구조변경·리팩토링 / 복제·수명주기·GAS·성능 경로 / 확장 스키마 / 코어 도메인 헤더) 중 하나라도 걸리면 코어 갈래.

### 착수 전에 잡은 것 3가지
| # | 문제 | 처리 |
|---|---|---|
| 1 | **정책이 3곳에서 서로 다른 말을 하고 있었다** — `Workflow.md §6-5`는 "구현=Sonnet/검증=Opus"인데 `CLAUDE.md`·`AGENTS.md` 원칙3은 아직 **"구현=Haiku 위임"**(2026-07-02 Sonnet 전환 때 갱신 누락) | 진입점 4곳(+`editor-bridge.md`)을 새 2갈래 문구로 통일 |
| 2 | **"Fable이 검증"은 서브에이전트로는 성립하지 않는다** — 사용자와 직접 대화 못 해 플랜 승인 왕복 불가 + 빌드 로그·diff를 통째로 넘겨야 해서 판정이 비싸고 부정확 | **모드 A(기본) = 세션 자체를 Fable로 시작**, 모드 B(폴백) = Opus 세션에서 Fable 서브에이전트에 판정 권한만 부여 |
| 3 | **비용은 "적용 범위"가 아니라 "세션 전체"에 붙는다** — Fable 출력 100만 토큰당 $50(Opus 5의 2배, Sonnet 5의 약 3.3배). 모드 A는 grep 한 방까지 그 단가 | 가드 명문화: Fable 세션 안에서도 **조사·census는 `model="sonnet"` 서브에이전트로**, Fable 본체는 설계·판정 전용 |

### 프롬프트 규칙이 모델마다 정반대라는 점 (§6-5-2 (4))
- **Fable에게** — 단계별 처방("먼저 X 하고 그다음 Y") **금지**. 구형 모델용 스캐폴딩은 Fable 출력 품질을 오히려 떨어뜨린다. 목표·제약·성공 기준만 주고 컨텍스트를 두껍게. 고정 3줄(도구결과 대조 보고 / 범위 밖 정리 금지 / 턴 종료 전 마지막 문단이 계획이면 지금 실행)을 붙인다.
- **Sonnet에게** — 정반대로 **축자적·명시적**. "전부 적용"류는 대상을 열거. 명세 이탈 금지, 갭 발견 시 중단·보고.

`Docs/Specs/` 명세 12항목에는 **복제표(Push Model `MARK_PROPERTY_DIRTY` 지점 포함)·수명주기/델리게이트 해제 대칭·데이터드리븐 경계·성능 예산(적 200~300)**을 필수로 넣었다 — 헤더만 넘기면 이 프로젝트 고유의 함정이 명세에 안 실린다.

---

## 🧹 기계적 리팩토링 1회차 + 전체 구조 리포트 (2026-08-06, `refactor/mechanical-cleanup`)
> 산출물 = **[`ProjectStructure_Report.md`](ProjectStructure_Report.md)**(전체 구조) ·
> **[`Refactor_20260806_Report.md`](Refactor_20260806_Report.md)**(결과 + **사용자 결정 필요 9건**).
> 커밋 `5f496e4f` · 베이스라인 태그 `refactor-baseline-20260806`.

외부 프롬프트(`REFACTOR_LOOP_PROMPT.md`, PHASE 0~6)를 기준으로 착수했으나
**실측에서 프롬프트의 전제 3개가 무너졌다.** 이 코드베이스는 겨냥된 축에서 이미 깨끗했다.

| 프롬프트 전제 | 실측 |
|---|---|
| 헤더가 무거울 것 | 헤더 146개에 include **총 417줄**, 그중 **115개가 3줄 이하**. 상위 include는 부모 UCLASS·`FGameplayTagContainer`(둘 다 전방선언 불가) |
| 값 복사가 널려 있을 것 | 컨테이너 값 전달 **0건**, 범위 for 값복사 **0건** (240개 중 223개가 이미 `const auto&`) |
| dirty 마킹이 빠져 있을 것 | 선언 **54** ↔ 등록 **54** 불일치 0, write 사이트 68곳 중 누락 1곳(그나마 복제 등록 전 실행이라 결함 아님) |

**수정한 것(18파일, 동작 변경 0)**: 중복 include 12건(`.cpp`↔자기 `.h` 양쪽에 있던 것만) ·
샷건 펠릿 루프의 `TArray` 2개 호이스팅 · `Reserve()` 11건(상한이 루프 헤더 `.Num()` 으로 확정된 곳만).
적 계열은 **손댈 게 없었다** — 이미 전부 `TInlineAllocator`/`Reserve` 가 걸려 있다.

### 🪤 호이스팅에 있던 함정 (실제로 걸릴 뻔함)
`FPSRCombat::DedupePawnHitsByActor` 는 `OutHits` 를 **비우지 않고 `Add` 만 한다**.
배열만 루프 밖으로 올리고 `Reset()` 을 빼먹으면 **펠릿마다 앞 펠릿의 대상이 다시 맞아 샷건 데미지가
조용히 중복된다.** 두 배열 모두 사용 지점에서 명시적으로 `Reset()` 하고 이유를 주석에 남겼다.

### 📏 빌드 실측 — 프롬프트 추정이 크게 빗나갔다
`-DisableUnity` **풀빌드 = 66.99초**(384 액션, 프로젝트 `.cpp` **145/146 개별 TU**, 유니티 소스 블롭 0, 에러/경고 0).
프롬프트는 이걸 "회당 20~60분"으로 가정해 검증 주기를 나눴는데 그럴 필요가 없었다.
⚠️ **일반 빌드로는 include 제거를 검증했다고 말할 수 없다** — 에디터 모듈이 유니티 블롭으로 묶여 컴파일된다.
앞으로 include 를 건드리면 `-DisableUnity` 를 쓸 것(67초면 싸다).

### 🔴 조사 중 새로 나온 것 2건 (수정 안 함 — 판단 필요)
1. **패키지 빌드에서 Push Model 이 통째로 꺼진다.** `TargetRules.bWithPushModel` 기본값이
   `(Type == TargetType.Editor)`(`TargetRules.cs:1420`)인데 `FPSRogueliteTarget` 은 `TargetType.Game` 이고
   오버라이드가 없다. 에디터/PIE 에서만 켜져 있다. 켜려면 `[RequiresUniqueBuildEnvironment]` 때문에
   **소스 엔진 빌드 전환**이 필요(현재 Installed Build).
2. **아무 클라이언트나 파티 전원을 로비로 강제 이동시킬 수 있다.**
   `ServerRequestReturnToLobby` 에 런 상태 게이트도 호스트 전용 게이트도 없다.

문서↔코드 드리프트 **12건**도 목록화했다(문서는 안 고침 — 건마다 문서를 코드에 맞출지
코드를 문서에 맞출지가 다르다).

---

## 🦾 1인칭 팔을 **구매 리그(LPAMG)** 로 갈아탐 + PWAS 리타게팅 (2026-08-05, `refactor/character`)
> 구조 근거 = **[ADR 0006](Architecture/0006-first-person-arms-purchased-rig-retargeted.md)** (0004·0005 대체).

**자체 스켈레톤 트랙을 접었다.** 사용자 실플레이 판정: *"모든 걸 다 해봤는데 내 기준에 도달하지 못했어.
모델링은 근본부터 수정할게."* 잠깐 **"1인칭은 총만 보인다"** 로 갔다가, 사용자가 LPAMG 팩의 1인칭 팔을
임포트하면서 뒤집혔다.

| | |
|---|---|
| 팔 | `SK_LPAMG_Arms_Base_Smooth` · `SKEL_LPAMG_Character` **79본, UE4 마네킹 규약** · 소켓 92개 |
| 애니 | PWAS(`S_Mannequin`, UE5) → `RTG_PWAS_to_LPAMG` → `Anims_LPAMG/FP_Rifle_{Idle,ADS,Reload}` |
| 배선 | `ABP_FP_Base`(부모 `FPSRFirstPersonArmsAnimInstance`) → `BP_FPSRPlayer.FirstPersonArms` (사용자 작업) |

**두 스켈레톤 차이 실측** — 이름 규약은 같고 계층만 다르다. 마네킹에만 있는 본 14개(메타카팔 8 ·
`lowerarm/upperarm_twist_02` 4 · `spine_04/05`), 이름은 같은데 **부모가 다른 본 10개**(`clavicle` =
spine_05 vs spine_03, 손가락 `01` = 메타카팔 경유 여부). 그래서 그냥 못 틀고 리타게팅이 필요했다.
**`SOCKET_Weapon` 이 이 팔에 이미 있어** 무기 부착 규약은 그대로 맞았다.

### 🩺 리타게팅이 **정적 포즈만 찍던 사고** — 3단 원인
증상: Idle/ADS/Reload 셋이 소수점까지 같은 값(−56.65, −0.34, 111.68). 서로 다른 두 애니의 **압축 DDC
해시까지 동일**. 그런데 **어느 단계에서도 에러가 안 났다** — 리그 생성·op·매핑 호출·내보내기·압축까지
전부 "성공".

1. UE 5.7 리타게터는 **op 스택**이고 **체인 매핑이 op 안에 산다** → `auto_map_chains()` 를 op 이름 없이
   부르면 안 붙는다
2. op 이름을 주고 `set_source_chain()` 을 직접 걸어도 0/15 — `ChainMapping->HasChain(Target)` 이
   false 면 조용히 `continue`(`IKRetargeterController.cpp:1022`)
3. 그 false 의 진짜 이유: **FK Chains op 의 `IKRig Asset` 슬롯이 None**. op 이 타깃 리그를 모르니
   체인 목록이 0줄이고, 0줄이라 UI 의 `Auto-Map Chains` 도 회색이었다.
   `GetTargetIKRigForOp` 는 **게터만** 노출돼 있어 스크립트로 못 채운다 → 사용자가 에디터에서 지정

**대조군이 진단을 갈랐다.** "안 변한다"는 관측만으로는 *측정 도구가 고장난 경우*와 구분되지 않는다.
원본 재장전을 같은 도구로 먼저 재서(t=0 −11.32 → t=1.0 −9.64) 도구가 멀쩡함을 확인하고서야 단정했다.

**내 첫 게이트가 이 사고를 못 잡았다** — 트랙 79개·핵심 본 12/12 가 다 있는데 값만 정적이었다.
"있다" 검사는 "맞다" 검사가 아니다. 값 게이트(`lpamg_verify_pose_values.py`)와 매핑 게이트를 추가했다.

**최종 검증**: Idle(−23.07, 28.43, 149.70) · ADS(−12.72, 24.57, 154.64) · Reload@1.0s(−21.07, 29.45,
147.98). Idle→ADS 변화 방향이 원본과 **세 축 모두 일치**. 재장전도 시점마다 원본과 같은 방향으로 변한다.

### 🧹 미사용 에셋 8,117개 정리 — **4,988MB → 1,609MB**
판정은 문자열 검색이 아니라 **에셋 레지스트리 의존성 폐포**로 했다(루트 = 맵 + 우리가 만든 폴더).
지운 범위(사용자 승인): ModularSciFiStation 289 · LPAMG 2,261 · Rifle_01 926 · PolygonCyberCity 1,537 ·
PolygonScifi 871 · PolygonMilitary 2,228 · Blu 중복본 31 · _SyntyPilot 5.
안 건드림: Synty · Characters(Blu) · StylizedRenderingSystem · PWAS · PolygonParticleFX.

- **"폴더만 남긴다"로 자르면 안 된다** — 무기 메시는 머티리얼·텍스처를 다른 폴더에서 물고 있어서,
  명시 보관도 **의존성 폐포까지** 확장했다. 그래서 PolygonMilitary 는 도달 57 이 아니라 **363개**가 남는다
- **도시 빌드 툴은 `/Game/PolygonCyberCity` 를 폴더째 스캔한다**(`get_assets_by_path`) — 레지스트리
  판정으로는 "미사용"이지만 지우면 팔레트가 108개로 준다. 사용자가 알고 승인했다
- 검증: 재스캔 후 남은 4,295 패키지 전수 검사 → **이번 정리가 만든 끊어진 참조 0**. 남은 39건은 정리
  전부터 있던 것. 검증이 `Rifle_01/Character/Mesh/SK_Mannequin` 끊김을 잡아냈고, 참조자가
  `IK_UE4Mannequin`·`RTG_UE4Man_to_Blu` 라 되살렸다 — LPAMG 가 UE4 마네킹 규약이라 앞으로 쓸 리그다

### 🔧 무기 어셈블러에 1인칭 팔 프리뷰 추가
BP 뷰포트로는 총이 안 보인다(실측: `WeaponMesh` 비어 있음 · 파츠 7개 런타임 생성 ·
`WeaponMeshStatic` 에는 옛 PWAS 데모총 `SM_M4` 잔재). 그래서 그립을 원점에 뜬 총으로 판정하고 있었다.

기존 인프라 위에 얹었다 — `BakeSockets` 가 *"바디가 identity 가 아니어도 정합"* 하도록 이미 짜여 있어
조립품을 손에 얹어도 기존 베이크가 산다. **기즈모도 새로 안 만들었다**(기존 `전체 이동` 재사용),
**저장 대상만** 갈랐다: `조립→저장`=무기 바디 / `손 위치 저장`=팔 메시. 순수 추가 323줄.

- 🚨 **스케일은 소켓에 굽지 않는다** — 런타임이 `SnapToTargetNotIncludingScale` 후
  `SetRelativeScale3D(WeaponAttachScale)` 를 따로 걸어서, 소켓에도 넣으면 0.85가 두 번 곱해진다
- 🪤 소켓이 없으면 **만들지 않고 실패**한다(어느 뼈에 달지는 추측 대상이 아니다)
- 🪤 프리뷰 포즈는 **정지 포즈**여야 한다(매 틱 따라가면 기즈모 편집이 매 틱 덮인다)

### 🪤 커맨드렛 함정 3종 (전부 실측 → `Docs/Troubleshooting.md` D7~D9)
- `LevelEditorSubsystem.is_in_play_in_editor()` → **커맨드렛에서 즉사**(`GUnrealEd` 없음). 파이썬 예외가
  아니라 프로세스 종료라 try/except 로 못 막는다 → 부르기 전에 커맨드렛인지 가른다
- `IKRetargetBatchOperation.duplicate_and_retarget()` → **Slate assertion**. 계산은 끝나고 압축까지
  갔는데 **저장 직전에 죽어** 결과가 안 남는다 → 정식 에디터(`-ExecutePythonScript`)로 분리
- 파이썬 커맨드렛은 `sys.argv` 로 인자를 못 받는다 → `SystemLibrary.get_command_line()` 에서 읽는다

**커밋**: `31fee185`(정리) · `6cac5d62`(도구) · `45de6b54`(리타게팅) · `0c68e02c`(이름) ·
`bcc62d1b`(리타게팅 수정) · `50e6f300`(어셈블러 팔 프리뷰)

---

## 🎥 슬라이드 중 화각 +5도 — 카메라 FOV를 **단일 기록자**로 (2026-08-05, `refactor/character`)
> 구조 근거 = **[ADR 0001 「카메라 FOV의 단일 기록자」](Architecture/0001-player-movement-state-ownership.md)**.

**요청의 전제를 먼저 뒤집었다.** "FOV를 5 빼서 주변을 더 보이게"는 서로 반대다 — UE `FieldOfView`는 화각(도)이라
값이 **커야** 넓어진다. 사용자 확인 후 **+5(넓게)** 로 확정. 슬라이드·스프린트에서 화각을 넓히는 건 Apex/타이탄폴/CoD의
속도감 연출 방식이기도 하다.

**얹으려던 자리가 잘못돼 있었다.** FOV를 쓰는 코드가 무기 발사 컴포넌트 틱 안이라, 거기 슬라이드를 넣으면
무기가 `IsSliding()`을 묻게 된다(불변식 4 위반). `AFPSRCharacter::UpdateCameraFieldOfView()`로 옮겨
**FieldOfView 기록자를 하나로** 만들었다. 부수 효과로 **맨손일 때 FOV가 갱신 안 되던 문제**가 사라졌다
(옛 틱은 장착 무기가 없으면 먼저 `return`했다).

| 값 | 기본 | 뜻 |
|---|---|---|
| `SlideFieldOfViewOffset` | `+5.0` | 슬라이드 중 기준 FOV에 더할 각도(양수=넓게) |
| `SlideFieldOfViewInterpSpeed` | `8.0` | 보정치가 들고 나는 속도 |
| `BaseFieldOfViewRange` | `(60, 130)` | 나중 FOV 슬라이더가 읽을 허용 범위 |

**조준 중엔 보정이 빠진다** — `ResolveADSTargetFOV`가 힙 목표값을 ADS/스코프 FOV로 **대체**한다. 더했다면
FOV 30도짜리 스코프에서 5도가 17% 배율 변화로 보인다. **기준값이 바뀌어도 따라온다** — 보정을 절대값이 아니라
`BaseFieldOfView` 위의 차이값으로 표현했고, 설정 메뉴는 `SetBaseFieldOfView()` 하나만 부르면 된다.

**부드러움은 2단**: 보정치가 한 번(8), 카메라 FOV가 다시 한 번(무기 `ADSInterpSpeed`=14) 지수 보간 → 시작·끝이
모두 완만한 S자(체감 ~0.2초). 슬라이드 도중 조준을 켜고 꺼도 목표값만 바뀌어 끊기는 지점이 없다.

## 🖐️ 1인칭 팔 = **자체 스켈레톤(안 A′)** + 아이들/ADS 포즈 (2026-08-04, `refactor/character`)
> 결정 근거·게이트 실측치 = **[ADR 0004](Architecture/0004-first-person-arms-own-skeleton.md)** · 함정 = `Troubleshooting.md` **A7~A10**.

**방향을 뒤집었다** — 메시를 마네킹 뼈에 맞춰 구부리던 것을, **뼈를 메시에 맞추는** 쪽으로.
`S_Mannequin` 을 공유하는 한 UE 는 ref pose 도 공유하므로 메시가 늘어날 수밖에 없었고, 그 대가가 손 왜곡(손바닥 +9~21% · 손가락 −7~10%)이었다. PWAS 포즈를 그대로 재생하지 않기로 한 순간 그 제약을 지킬 이유가 없어졌다.

**사용자 결정 = 안 A′** — 손은 NEON-V 그대로, 팔은 도달 55.02cm·어깨 간격을 유지하되 상완/전완 **비율만** NEON-V.
예상 못 한 이득: 총 길이를 고정한 채 비율만 옮기니 두 마디가 **같은 배율**(×1.325)로 늘어났다. 옛 마네킹 타깃은 ×1.396/×1.259 **불균일**이라 전완 테이퍼를 조용히 뭉개고 있었다.

### 구현 결정 3개
1. **뼈는 평행이동만.** 방향·roll 은 Manny 그대로. 두 리그의 뼈 로컬 축은 전혀 다르지만(검지 7~29° · 엄지 100~107°) **축은 변형에 관여하지 않는다** — LBS 는 월드 트랜스폼과 웨이트로만 결정된다. 축을 바꾸면 견본 포즈 1:1 전이와 리타게팅만 잃는다. `use_connect` 는 먼저 해제(연결 본은 형제까지 끌고 간다).
2. **손은 통째 강체 이식.** `hand.*` 를 회전만(스케일 1) 포즈하고 **손가락은 아예 포즈하지 않는다**. 기각안 = "웨이트 전이 먼저, 손은 나중에 되돌리기" — 손바닥 피부는 `hand_r` 에 물려 있는데 A′ 에서 손목이 안 움직이므로 **손바닥이 늘어난 채 남는다**. 지적받은 바로 그 부위가 안 고쳐진다.
3. **웨이트 공여 메시를 먼저 우리 rest 에 맞춘다.** `POLYINTERP_NEAREST` 는 의미를 모르고 가까운 표면을 집으므로, 손바닥이 1.3~1.6cm 짧아진 만큼 손가락 뿌리가 공여의 손바닥 중간을 집는다.

### 게이트 (전부 대조군과 나란히)
| | 자체(A′) | 대조군 |
|---|---|---|
| 손 강체잔차 vs 원본 NEON-V | **0.0000cm** | 옛 마네킹 컨폼 **1.0973cm** |
| 손 표면거리 vs 원본 | **0.0004cm** | 0.5567cm |
| UE rest 위치 vs Blender 스펙 | **0.00016cm** | 같은 메시 두 번 0.00000 |
| UE rest 로컬 회전 vs `S_Mannequin` | **0.00089°** | — |
| 손가락 교차 오염 >0.30 정점 | 104 | 공여 메시 1598 (우리가 더 깨끗) |
| 손목 90° 비틀기 95% 변형 | 0.1236 | 옛것 0.1234 (동등·최대는 개선) |
| 주먹 손바닥 95% 변형 | 0.357 | 옛것 0.528 |
| 포즈 왕복 로컬 회전 65본 | **0.0066°** | — |

> 🔑 **가장 값어치 있던 발견**: 그동안의 "본 잔차 0/65" 는 **FBX 왕복을 증명한 적이 없었다.** 스켈레톤을 공유하면 `get_socket_transform` 이 메시가 아니라 ref pose 를 돌려준다(단위 100배 사고를 못 잡은 것도 같은 이유). 자체 스켈레톤이 되어서야 쟀고 — 회전 0.00089° → **FBX 경로는 뼈를 보존한다**.
>
> 🪤 **틀렸다가 고친 것 3개**: ①손을 겹칠 때 뼈 로컬 축으로 프레임을 맞춰 9cm 오차(→Kabsch 강체정합으로 교체) ②UE 에서 잰 소켓 값을 Blender 축으로 분해(→**뼈 로컬도 Y 부호가 뒤집힌다**, 계산을 UE 공간으로 통일) ③표면거리 게이트를 총 거리로 걸어 헛나감(→**길이방향/반지름방향을 갈라** 길이방향만 게이트. 팔이 가는 건 매핑을 안 깬다).

**스크립트**: Blender `NeonV_scripts/fp_arms_{rebuild,reweight}.py` 수정 · `fp_arms_{verify_own,dump_spec,socket_probe,pose_transfer}.py` 신규 · UE `Scripts/fparms_{import_newskel,socket_physics,export_ref_poses,import_anims,verify}.py` 신규(**`Saved/` 는 gitignore 라 clean 한 번에 날아간다** — 추적되는 `Scripts/` 로 둔다).
**남은 것** = 배선·PIE(사용자). `PROGRESS.md` 참조.

---

## 🛝 4c 슬라이드·벽 애니 = ✅구조 수리(앵커·루프·wall_slip) / **⏳포즈는 사용자 재저작 대기** (2026-07-31, `refactor/character`)
> **저작 레시피·함정 전부 = 메모리 `blender-locomotion-anim-authoring`** (헤드리스 AnimSequence 저작 API·glTF 왕복·IK 미러폴 플립 등 하드-won). 새 세션은 그것부터 읽을 것.

### 🚨 2026-07-31 정정 — 아래 "8포즈 저작 ✅완료"는 사실이 아니었다
계측해 보니 **8개 중 4개가 깨진 채 UE로 넘어갔다.** 앞 세션이 완료로 적었을 뿐 확인한 적이 없다.
1. **"저자세 동기화"가 멀쩡한 클립 3개를 망가뜨렸다.** `slide_low_reference.json`을 **`slide_loop` f0에서 캡처**했는데 그 프레임이 *공중에 뜬 앉은 자세*(발이 지면 **위 50cm**)였다. 그 포즈가 `slide_enter` f9 · `slide_exit_crouch` f0 · `slide_exit_stand` f0으로 퍼졌다 — 동기화 **전** 백업(19:41)에는 세 곳 다 −26.0(진짜 슬라이드 포즈)으로 정상이었다. `slide_exit_crouch`는 끝 포즈까지 −19.5 → **−41.6**으로 악화되고 길이도 12 → 15로 바뀌었다.
2. **발이 바닥을 뚫는 건 원래부터 미수정.** 저작 가이드가 *"foot.L을 위로 젖혀 발바닥이 바닥에 닿게"* 라고 지적했던 그대로 남아 있었다. 발목 높이는 멀쩡했고(z 5~7) **발이 발끝-아래로 회전**된 게 원인 — 오른발은 자기 발목보다 **33cm 아래**를 향하고 있었다.
3. **`wall_slip`은 1프레임.** 백업 **3개 전부** 1프레임 = 나중에 깨진 게 아니라 **처음부터 저작이 안 됐다**(스펙은 0..24 루프).
4. `slide_loop` f0은 어느 버전에서도 공중 포즈였다 → **루프가 닫힌 적이 없다**(f0 ≠ f15).
> **교훈**: 포즈 참조 JSON을 뜰 때 **그 프레임이 멀쩡한지 먼저 재라.** 그리고 애니는 숫자만으로 완료 판정하지 말 것 — 바닥면 넣은 렌더 한 장이 넷 다 잡아냈다.

**✅ 완료(2026-07-31, 구조만):** 도구 = Blender repo `NeonV_scripts/anim_{probe_state,diag_render,ground_feet,build_wall_slip,sheet_render,export,add_ingame_refs}.py`. 작업 전 백업 = `NeonV_locomotion_preslidefix2_backup.blend`.
- **A. 앵커 복구** — `slide_enter`·`slide_exit_crouch`·`slide_exit_stand`를 19:41 백업에서 통째로 되살림(재저작 아님) + `slide_loop` f0 := f15로 **루프 닫음**. 앵커 4곳 −26.02로 일치 확인.
- **B. 발 접지 = ❌ 되돌림(내 버그).** 발목을 돌려 sole 벡터를 수평으로 보냈더니 **오른발이 뒤를 향했다**(dy −33.4 = 발끝이 발목 뒤). 원인: 원본 오른발이 거의 수직(dz −33.0)이라 **수평 성분이 +5.4cm(뒤쪽) 노이즈뿐**이었는데 그걸 발 길이만큼 증폭했다. 왼발은 수평 성분이 뚜렷해(−21.7) 정상이었다. → 슬라이드 4클립을 사용자 원본 발로 되돌림(관통은 다시 −26~−29.5). **도구는 고쳐 뒀다** — 수평 성분이 발 길이의 35% 미만이거나 다리 방향과 반대면 **다리 방향(엉덩이→발목)으로 폴백**하고 로그를 남긴다.
  > **교훈**: 벡터를 "수평으로 눕힌다"는 연산은 그 벡터가 거의 수직일 때 **방향이 노이즈**다. 크기만 보지 말고 방향의 신뢰도를 게이트할 것.
- **C. `wall_slip` 재저작** — 프레시 IK 금지(팔·다리 플립 전례)라 **검증된 포즈만 섞었다**: 팔 = `wall_climb` 양 극단(좌우 교대) / 다리 = rest 쪽으로 0.8 신전 + 0.15 교대(스크래핑) / 나머지 = `wall_hang`. hips는 안 건드림(in-place).
- **검증**: 앵커 4곳 일치 · `wall_slip` 루프 닫힘 오차 **0.0000cm** · 팔 교대 ±26/28cm · 다리 12~17cm 신전 · 벽 3종(hang/climb/topout) 무변경 대조 확인.
- **D. 인게임 충돌 형상을 .blend에 참조로 넣음** (사용자 요청) — `REF_InGame` 컬렉션에 `REF_Wall_InGame`(y=**−0.386**)·`REF_Floor_InGame`(z=0)·`REF_Capsule_InGame`(r 0.386, 높이 1.84). 전부 와이어·`hide_select`·`hide_render`. **export 3중 차단**: 이름 `REF_`/`FP_` 필터(`anim_export.py`) + hide_select + 실제 export 후 glb에 문자열 **0건** 확인.

### 🚨🚨 2026-08-02 최중요 정정 — Blender 1cm ≠ UE 1cm (`CharacterMesh0` scale **0.8806**)
리그는 Blender에서 **키 184.0cm**인데 UE는 `CharacterMesh0`를 **0.8806배**로 넣어 162cm를 만든다(Blender repo `Docs/HANDOFF_NEONV_FPARMS_RESULT.md`, 2026-07-25 "신장 162cm 통일"). **게임 값을 Blender로 옮길 땐 0.8806으로 나눠야 한다.** 교차검증: 캡슐 162cm(UE) ÷ 0.8806 = 184cm = 리그 키와 정확히 일치.
- 이걸 놓쳐 **두 번 틀렸다**: ①벽을 34cm에 놓음(정답 **38.6**) ②1인칭 눈높이를 150.7cm에 놓음(정답 **171.1** — 20cm 낮아 카메라가 옷깃 속에 파묻혔고, 거기서 나온 "몸이 시야를 45~60% 덮는다"는 **가짜 결론**이었다).
- **함정의 근원**: ADR 0002의 *"head 본은 발에서 155.1cm"* 는 **스케일 적용 전 스켈레톤 값**이라 Blender와 그냥 일치한다. 그래서 스케일 불일치가 안 잡혔다 → **게임 월드 값과 스켈레톤 값을 섞어 쓰지 말 것.**

**✅ 올바른 벽(38.6cm) 기준 재계측 — 사용자 저작본은 정확하다**

| 클립 | 벽 기준 |
|---|---|
| `wall_hang` | 0.3cm 앞 (거의 정확히 닿음) |
| `wall_slip` | 0.5cm 안 |
| `wall_climb` f0/f15 | 0.0 / 0.1cm |
| `wall_topout` f0 / f12 | 20.3 / 39.9cm 안 (**미수정**. f12는 벽을 넘어가는 동작이라 의도적일 수 있음 — 판단 필요) |

**✅ 1인칭 팔 메시 = 3인칭 애니와 완전 호환(실증)** — `shoulder.L/R` 이하만 남겨 자른 팔이 3인칭 애니를 **오차 0.000000cm**로 따라온다(5액션 7프레임). UE 측은 **UE 5.5+ 정식 기능**: 팔 `FirstPersonPrimitiveType=FirstPerson` / 바디 `WorldSpaceRepresentation`(그림자 담당, `bOwnerNoSee`·`bCastHiddenShadow` 엔진이 자동 설정) + `LeaderPoseComponent`. **ADR 0002가 걱정한 그림자 문제는 엔진이 해결했다.**
- ⚠️ **다만 가림 해소 효과는 작다**(올바른 눈높이 재계측): 수평 시선 **0%**, 아래 40° 33.2%→24.1%, 아래 60° 57.7%→**49.3%**(8%p). 내려다볼 때 보이는 게 대부분 팔이라서다. → 팔 메시의 가치는 가림이 아니라 **"내 다리·몸이 안 보이는 Apex식 룩"** 취향 선택.
- ⚠️ 자동 컷(주 가중치 기준)은 엣지루프가 아니라 **너덜한 경계**를 만든다 → 실제 저작은 손으로. 팔 실루엣은 거의 전부 `Jacket`(소매), `Body`는 손·손가락만.

**📁 Blender repo 정리 완료(2026-08-02, 185MB→76MB)** — 작업 파일 **2개로 분리**: `NeonV_locomotion.blend`(3P 애니, **액션 8개의 진실원천**) · `NeonV_fp_arms.blend`(1인칭 팔). 둘 다 아마추어+액션을 갖지만 **액션 편집은 locomotion에서만**(안 그러면 조용히 갈라진다). `[완성본]/` = 원본 리그·`NeonV_work.blend`·`Textures/`(⚠️`Blu - *.blend`가 텍스처를 상대경로로 참조해 같은 폴더여야 함). 백업·중간산물 68개 삭제.

**⏳ 사용자 재저작 대기 (포즈 확정권자)**
1. **슬라이드 저자세** — 사용자 판정 *"전혀 앉아있지도 않다"*. 골반 57.8cm(서기 97 = 60%)라 런지에 가깝다. 재저작 후 알려주시면 관통 재계측 + 렌더 확인.
2. **`wall_topout`** — f0(20.3cm)은 아직 벽에 매달린 시점이라 당겨야 하고, f12는 넘어가는 동작이라 판단 필요.
3. **1인칭 팔 컷** — 자동 컷은 출발점일 뿐, `Jacket` 어깨 엣지루프로 손 컷 필요.

- **(이력) Blender 8포즈 저작** (`C:\Users\koras\Desktop\작업\개발작업\블랜더\NeonV_locomotion.blend` — **별도 Blender repo**). 벽 4(hang/slip/climb/topout)·슬라이드 4(enter/loop/exit_crouch/exit_stand). wall_hang·slide_loop = 사용자 확정 기준. 쿼터니언 스핀 비파괴 정리. 조정 가이드 3종(그 repo `Docs/`).
- **UE 반영** = glTF 왕복(export→커맨드렛 임포트→새 스켈레톤 8애님, 포즈 정확·본명 underscore=Blu 호환) → **트랙 복사로 Blu 스켈레톤 재생성**. 최종(미추적, 이 커밋): `Content/Characters/Blu/Anims/W2_Rifle/Blu_W2_Slide_{Enter,Loop,Exit_Crouch,Exit_Stand}` + `Content/Characters/Blu/Anims/Blu_Wall_{Slip,Hang,Climb,TopOut}`. 108본·포즈검증(slide_loop foot z59·head z114=Blender 일치)·테스트폴더 정리.

**✅ 상체 그래프트 완료** (3a-1, 미커밋) — 슬라이드 4클립의 **상체 98본**(spine 아래 전부: 척추·가슴·목·머리·어깨·팔·손·손가락 + 머리카락/얼굴/Rope)을 `Blu_W2_Crouch_Aim_Idle_IPC` 프레임 0으로 덮어씀. 하체 10본은 손 안 댐. 벽 4개는 맨손 유지. 스크립트 = `Scripts/slide_upper_graft.py` + `_verify.py`(**멱등**, 되돌리기 = `git checkout -- .../Blu_W2_Slide_*`).
- **방식 결정 근거(실측)**: 조준 아이들은 60프레임 동안 손이 0.30cm만 움직임 → 프레임 0 한 장으로 충분 / 슬라이드 골반이 조준 자세와 **11.5°** 차이뿐 → 로컬 트랙 그대로 복사해도 총 방향 오차 **7.7°** → **스파인 역보정 불필요**.
- **검증(별도 프로세스 재로드)**: 하체 드리프트 **0.0000cm**(재압축에도 안 흔들림) · 상체 = 소스와 완전 일치 · 양손 간격 20.72cm 유지 · 발 높이 무변화 · `upper_arm_R` 81.6°(T자 해소). 포즈 그림 = `Saved/NeonV/anim/pose_preview.svg`(`Scripts/anim_stickfigure_svg.py` — **에디터 없이 포즈 눈으로 확인하는 도구**).
- **함정 2개 기록**: ① `AO_Crouch_Aim`의 애디티브 **기준 포즈는 `Blu_W2_Crouch_Aim_Point_Center`**다(아래 AimOffset 표의 "미리보기 베이스"는 *다른 필드*). 다만 두 포즈는 **0.02cm·0.0° 동일**이라 결과는 같음 — 스크립트는 이름이 아니라 **포즈를 비교**해서 게이트한다. ② `get_bone_track_names()`는 `unreal.Name`이고 스켈레톤과 **대소문자가 다르다**(`lower_leg_r` vs `lower_leg_R`). `str()`로 바꿔 비교하면 다리 본이 상체로 새어 들어간다 → 비교는 전부 소문자로.

**✅ UE 재반영 완료 (2026-08-03)** — 에디터 종료 상태 헤드리스 커맨드렛. **실제 쓰는 2개만** 반영: `Blu_W2_Slide_Loop`(15프레임) · `Blu_Wall_Slip`(1프레임).
- **슬라이드 저자세 = 앉아서 미끄러지는 자세로 재저작**(사용자). 저작본이 바닥에서 **39.6cm 떠 있어** 골반 순수 이동으로 내림 — 자세 모양은 무변경. 전 프레임 접지 **−0.00cm**.
  - **내린 양의 교차검증**: 슬라이드 중엔 캡슐이 앉은 크기(반높이 36.8 + 앉은 눈높이 29.5 = 바닥에서 **66.3cm**)이고, 66.3 ÷ 0.8806 = 75.3cm인데 내린 뒤 머리 뼈가 **75.5cm** — 0.2cm 차이로 카메라가 정확히 머리에 온다.
- **파이프라인**: `anim_export.py`(glTF, `REF_`·`FP_` 제외) → 커맨드렛 임포트 → `ue_copy_to_blu.py`로 트랙 복사 → `slide_upper_graft.py`. 검증 = Blender 대비 **최대 0.38cm**, 그래프트 별도 프로세스 재검증 **ALLPASS**(하체 드리프트 0.0000 · 상체 소스 일치 · 양손 간격 20.72cm · `upper_arm_R` 81.6°).
- 🪤 **커맨드렛이 exit 3으로 죽지만 작업은 끝나 있다** — 임포트/저장 완료 후 **콘텐츠 브라우저가 결과를 UI에 표시하려다** `Assertion failed: CurrentApplication.IsValid()`(Slate 없음). 로그에서 `CP ALLDONE`/`ALLPASS` 같은 완료 마커를 확인할 것이지 종료코드로 판단하지 말 것.
- ✅ **미사용 6개 삭제**(`Slide_Enter`·`Slide_Exit_Crouch`·`Slide_Exit_Stand`·`Wall_Hang`·`Wall_Climb`·`Wall_TopOut`, 사용자 결정 2026-08-03). 지우기 전 **참조 0개 확인**(참조가 하나라도 있으면 전체 중단하게 짜서 돌림). 깨진 소스에서 나온 것들이라 남겨두면 나중에 신뢰하게 된다. 되살리려면 `git checkout 50d0d686 -- <경로>`. **이제 UE의 4c 애니는 `Blu_W2_Slide_Loop`·`Blu_Wall_Slip` 둘뿐이다.**
- ~~exit 끝점 정합~~ **소멸** — exit 클립 자체가 없어져 상태기계가 슬라이드 포즈 → 앉기 아이들로 바로 블렌드한다.
4. ~~**`BS_Wall_Vertical`** 1D BlendSpace~~ **폐기**(사용자 결정 2026-08-02) — **벽 관련은 `wall_slip` 애니 하나로 통일**한다. 매달리기·등반·미끄러짐·**턱 넘기까지 전부** 이 하나(사용자 확정 2026-08-03). 최대 1.5초(`WallHangMaxDuration`)짜리 동작이라 감수.
   - **따라서 `wall_hang`·`wall_climb`·`wall_topout` 3개는 미사용**이 된다. `.blend`에는 남겨 두되(비용 0, 참고·폴백용) UE로는 `wall_slip` 하나만 반영한다.
   - **턱 넘기가 자세로는 안 보인다** — 넘어가는 건 코드(`WallTopBoostForward/Up`)가 캐릭터를 앞·위로 밀어서 일어나고, 벽을 잃으면 `MOVE_Falling`으로 빠지므로 **그 순간의 그림은 공중/착지 애니가 받는다.** 별도 상태가 원래 없으므로 상태기계는 오히려 단순해진다.
   - ⚠️ **`wall_slip`은 몸이 벽에 대해 86° 옆으로 선 포즈**다(어깨선 실측 +86.2°, 나머지 벽 3포즈는 0°). 이건 **의도된 것**(사용자 확정 2026-08-02).

### ✅ 3b 코드 완료 (2026-08-03) — 빌드 4회 Succeeded(UHT `-WarningsAsErrors` 포함) · Codex 게이트 2회
**플랜 게이트에서 4건, diff 게이트에서 3건을 잡았고 전부 반영했다.** 특히 앞의 둘은 그대로 짰으면 조용히 깨졌을 것들이다.
1. **슬라이드가 프록시에서도 보인다** — `bIsSliding`은 `PrepMoveFor`가 되감으므로 복제 대상으로 부적합. **별도 서버 사본** `bSlidingVisual` + `SlideVisualSerial`(진입마다 +1)을 CMC에 두고 Push Model·`COND_SkipOwner`로 복제. 읽기는 `IsSlidingForDisplay()`(오너·서버는 정확한 로컬값, 프록시는 사본).
   - ⚠️ **serial이 필요한 이유**: 점프 캔슬처럼 net update 사이에 시작·종료가 다 끝나는 슬라이드는 bool만으론 **동료 화면에 아예 안 뜬다.** serial이 바뀌면 `SlideVisualMinDuration`(0.2s)만큼 붙잡아 보이게 한다.
   - ⚠️ **시각 사본 쓰기는 authority 전용**(Codex diff 지적) — 오너가 쓰면 보정 되감기가 과거 슬라이드를 재실행해 serial을 다시 올리고, 취소된 슬라이드가 0.2초 재생된다. AnimInstance의 hold 소비도 **프록시 전용**(리슨서버 자기 화면 꼬리 방지).
2. **벽 정렬** — CMC에 `GetWallYawForDisplay()`(정확한 법선이 있으면 그것, 없으면 1바이트 양자화본)·`GetWallSideSign()`(진입 시 래치)·`GetWallPoseSideAngle()`. AnimInstance가 `RootYawOffset`을 벽 기준으로 계산.
   - 🚨 **분기를 moving/falling 리셋보다 앞에 둬야 한다**(Codex 플랜 지적, 코드로 확인). 벽에 매달려도 미끄러짐 속도 때문에 `bIsMoving`이 true라서, 뒤에 두면 **매 프레임 지워지면서 코드는 멀쩡해 보인다.**
   - **side angle은 CMC 단일 소스** — sign을 래치한 쪽과 목표각을 계산하는 쪽이 같은 수를 봐야 한다(양쪽에 두면 어긋나서 반대 어깨를 고른다).
   - ⚠️ 모드는 이동 패킷, yaw/sign은 프로퍼티라 **원자적으로 못 온다** → 진입 프레임에 같이 더티 처리해 간극을 1업데이트로 제한.
3. **무기 가시성 = 단일 컴포저** `RefreshWeaponVisibility(bForce)` — `스코프(오너로컬) || 벽(모든 머신) || 무기없음`. 훅은 `AFPSRCharacter::OnMovementModeChanged`(CMC가 무기 렌더를 만지면 ADR 0001 "질의만" 경계가 흐려짐). `BeginPlay`·장착 갱신에서 멱등 호출, **장착 갱신은 `bForce=true`**(메시 재부착이 컴포넌트를 다시 보이게 만드는데 래치는 "숨김"이라 그냥 부르면 early-out).

### ✅ 3c 배선 완료 (2026-08-03) — VibeUE `AnimGraphService` 로 전부 프로그래매틱
**컴퓨터 제어(마우스)는 안 썼다.** 그래프를 마우스로 끄는 건 API보다 몇 배 느리고 좌표가 어긋나면 엉뚱한 핀에 붙는데, `AnimGraphService`에 `add_state`·`set_state_animation`·`set_transition_rule_from_bool(invert)`·`set_transition_blend`·`set_transition_priority`·**`validate_state_machine`**이 다 있어 결정적이고 검증까지 API로 된다.
- **죽은 상태머신 제거** — `BlueprintEditorLibrary.remove_graph`가 답이었다(`BlueprintService.delete_function`은 애님 그래프에 **안 먹는다**, False 반환). 죽은 `Locomotion` 하나를 지우니 **자식까지 딸려가 그래프 60 → 23(고아 37개 제거)**. 살아있는 `Locomotion2`는 무손상(validate 통과).
  - 죽은 그래프의 정체 = `AnimGraph.AnimGraphNode_StateMachine_0.Locomotion` — AnimGraph의 **노드 배열에서는 빠졌지만 outer로 매달려** 살아 있던 것. 그래서 `list_state_machines`에는 안 잡히고 `list_graphs`에는 잡혔다.
- **`Slide` 스테이트** = `Blu_W2_Slide_Loop`(루프). `Locomote →(bIsSliding)→ Slide` · `Slide →(!bIsSliding)→ Locomote` · `Slide →(bIsAirborne)→ Airborne` · `Slide →(bIsDowned)→ Down`.
  - ⚠️ **`Slide → Airborne` 우선순위 0**(나머지 1) — 점프 캔슬은 `bIsSliding` false와 `bIsAirborne` true가 **같은 프레임**에 오므로 `→Locomote`와 동시 성립한다. 공중이 이겨야 한다.
- **`Wall` 스테이트** = `Blu_Wall_Slip`(1프레임 정지 포즈). `Airborne →(bIsOnWall)→ Wall` · `Wall →(!bIsOnWall)→ Airborne` · `Wall →(bIsDowned)→ Down`.
  - 벽 진입이 **공중 전용이 사양**이라 `Locomote → Wall` 직행은 만들지 않았다. `Locomote → Airborne`(bIsAirborne, 벽 포함) → `Airborne → Wall`로 한 프레임 거쳐 간다.
  - 벽에서 손을 놓으면 `MOVE_Falling`이 되어 `Wall → Airborne`이 받고, 착지는 기존 `Airborne → Locomote`가 받는다 → `Wall → Locomote` 직행 불필요.
- **`RotateRootBone` + `Get RootYawOffset`은 이미 AnimGraph에 연결돼 있었다**(4a-2 콘텐츠 작업 산출물) — 3b가 계산만 벽 기준으로 바꿨으므로 배선 추가 불요.
- **검증**: 컴파일 **0 에러·0 워닝** · `validate_state_machine` = 7 states / 20 transitions / 에러 0 · 저장 후 재조회로 스테이트·그래프 수 재확인.

### ✅ 2인 PIE 결과 (사용자, 2026-08-03)
| 항목 | 1차 | 클램프 수정 + 애님 수정 후 |
|---|---|---|
| ① 동료 화면에 슬라이드가 보이는가 | ✅ 정상 | — |
| ② 점프 캔슬 슬라이드도 보이는가 | ✅ 정상 (serial + 최소 표시시간이 일한다) | — |
| ③ 벽에서 몸이 벽 기준으로 서는가 | ❌ 실패 — 들어오는 각도에 따라 몸 방향이 달라진다 | ✅ **사용자 확인 완료** |
| ④ 벽에서 총이 사라지는가 | ✅ 정상 | — |
| ⑤ 로코모션·ADS 포즈 (콜드 로드) | ❌ T자 | ✅ **사용자 확인 완료** |

### ✅ ③ 벽 클램프 제거 완료 (C++, 2026-08-03)
벽 분기가 `RootYawOffset`을 `±RootYawOffsetMax`(90)로 자르는데 목표각이 **86°**였다:

| 시선 | 필요값 | 클램프 시 |
|---|---|---|
| 벽 정면 | 86 | 86 ✅ |
| 30° 비껴서 | 116 | **90으로 잘림 → 26° 어긋남** |
| 벽 따라 옆 | 176 | **90으로 잘림 → 86° 어긋남** |

클램프의 존재 이유는 *"상체가 크로스헤어를 따라잡을 여유를 남긴다"* 인데 **벽에서는 애초에 사격이 불가능**하다(`CanFireInCurrentState()`가 벽에서만 false, 주석이 *"both hands are on the wall"*). 지킬 예산이 없는데 자르고 있었다. → `UpdateRootYawOffset`의 `if (bIsOnWall)`에서 클램프 삭제, `Desired` 그대로 사용.
- 🚨 **클램프를 빼면 새 버그가 생긴다 — 같이 막았다.** `FInterpConstantTo`는 각도를 각도로 모른다. 클램프가 있을 땐 값이 ±90에 갇혀 문제가 없었지만, 원 전체를 쓰게 되면 목표가 **±180을 넘길 때마다 몸이 반대로 한 바퀴 돈다**(179 → −179 = 실제 2°인데 358° 이동). `FRotator::NormalizeAxis`로 **최단호 스텝**으로 바꿨다. 속도 상수도 `RootYawOffsetMax*2`(이제 벽과 무관) → `180.0f`로 — 기본값에선 1200°/s로 수치 동일, 의미만 "최악의 경우 `WallAlignBlendDuration` 안에 정렬"로 정확해진다.
- 부수(수용): 벽에서 상체가 시선을 안 따라간다(못 쏘므로 영향 0) · 벽을 놓는 순간 오프셋 스냅 폭이 최대 90 → 176°가 될 수 있으나 같은 프레임에 낙하 포즈로 갈아치워져 가려진다.

### 🚨🚨 IDLE T자 = **버그 두 개가 서로를 가리고 있었다** (2026-08-03 해결)
> 앞 세션이 적은 *"4a-2가 미완성이라 애니가 하나도 없다"* 는 **오진이었다.** BlendSpace 4개도 레이어 그래프도 이미 다 만들어져 있었다(`e47909d5`). 근거였던 `get_used_anim_sequences = []` 는 그 함수가 **AnimSequence만 세기 때문**이고, 살아있는 경로의 포즈 소스는 전부 **BlendSpace**라 원래 안 잡힌다. **빈 목록은 "비었다"의 증거가 아니다.**

**두 버그가 겹쳐 있었다. 하나만 고치면 증상이 그대로라 서로를 가렸다.**

**버그 1 — `Save Cached Pose` 가지가 평가되지 않았다.**
`AnimGraph`의 `LinkedAnimLayer(GetWeaponLocomotionPose) → Save cached pose 'WeaponLocomotion'` → `Locomote` 상태의 `Use cached pose`. 이 경로로는 **무엇을 물려도 레퍼런스 포즈**가 나왔다(ABL 레이어도, 본체 자기 스텁에 넣은 시퀀스 플레이어도). 캐시 쌍을 지원 API로 **새로 만들어 봐도 동일**.
- → **캐시를 걷어내고 `Locomote` 안에 `Linked Anim Layer` 노드를 직결**해서 해결. `ApplyWeaponAimOffset`(정상 동작하던 쪽)과 똑같은 모양이 된다.
- 실증: 직결 후 런타임에 `unlink_anim_class_layers` 하니 본체 폴백 스텁 포즈가 **오차 0.2°로 정확히** 나왔다. 캐시 경유일 땐 그것조차 안 나왔다.

**버그 2 — BlendSpace 4개의 구워진 그리드가 디스크에 없었다.** ← 진짜 T자의 원인
`BS_W2_{Stand,Crouch}_{Aim,Relaxed}`는 헤드리스로 만들어져 **샘플 데이터만 있고 내부 그리드(삼각분할)가 통째로 비어 있었다.** 그래서 로드할 때마다 조용히 레퍼런스 포즈를 냈다.
- **"에디터에서 열면 고쳐지고 재시작하면 돌아온다"** 가 결정적 단서였다 — 여는 순간 메모리에서만 구워지고 저장이 안 된다.
- 고치는 법 = **다시 굽기 + 저장**. 축 Max를 바꿨다 되돌려 `PostEditChangeProperty`를 태우고 `save_asset`. 단순히 열기만 하면 dirty가 안 되어 Ctrl+S가 아무것도 안 한다.
- 증거: 저장 후 파일이 **13.9KB → 39.0KB**(Stand), **11.0KB → 22.5KB**(Crouch)로 커졌다. 비어 있던 게 그만큼이다.
- 같은 시기 헤드리스 AimOffset 4개(`/Game/Rifle_01/AimOffset/*`)는 **콜드 로드에서 정상 동작 실측** — 이 배치만 안 구워진 채 저장됐다. **앞으로 헤드리스로 만든 BlendSpace는 콜드 로드로 검증할 것.**

> **🔑 진단 도구**: 애님 문제는 그래프를 읽으려 하지 말고 **PIE 중인 AnimInstance를 파이썬으로 직접 읽어라** — `unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()` → `GameplayStatics.get_all_actors_of_class(gw, unreal.Character)` → `mesh.get_anim_instance()` → `get_editor_property(...)`.
> **포즈 판정은 본 로컬 회전 숫자로 한다**: `get_socket_transform(bone, RTS_COMPONENT)`를 부모 대비로 환산(`MathLibrary.make_relative_transform`)해 `AnimationLibrary.get_bone_pose_for_frame(clip, bone, 0, False)`와 대조. "어느 클립이 재생 중인가"를 각도 거리로 가른다 — 이번에 상태기계가 `Locomote`에 있음을 다른 6개 상태 클립과의 거리(최소 224.7°)로 배제 증명했다.
> ⚠️ **PIE 중에는 `AnimGraphService`·`BlueprintService`가 죽는다**(`Failed to load AnimBlueprint`). 그래프 조회는 PIE 끄고.
> ⚠️ **`BlueprintService.get_nodes_in_graph` + `get_node_details`는 애님 그래프에서도 핀·연결을 다 준다**(앞 세션의 "서비스 API가 핀을 안 준다"는 `get_connections`·`get_graph_definition`에만 해당). 연결 추적은 이걸로.

### 💥 에디터 크래시 1회 — `create_node_by_key`로 애님 노드 만들지 말 것
`BlueprintService.create_node_by_key(..., "NODE AnimGraphNode_LinkedAnimLayer", ...)`가 `Interface=None, Layer=""` 인 **빈 껍데기**를 만들었고 다음 틱에 **하드 크래시**(assert 없이 로그가 끊긴다). 더 나쁜 건 **저장한 적 없는 그 노드가 디스크에 남아** 재시작 후에도 존재했다는 것.
- → **애님 노드는 `AnimGraphService`의 타입별 `add_*`만 쓴다**(`add_sequence_player`·`add_save_cached_pose`·`add_use_cached_pose` 등, 초기화된 노드를 만든다).
- → **사용자 지시(2026-08-03): BP·애님그래프 노드 편집은 사용자가 직접 한다.** Claude는 조회·진단·단계 가이드까지.

## 🖐 1인칭 팔 메시 = **채택 확정** (사용자 결정 2026-08-03) — P1 코드 완료 / ⏳P2 Blender 대기
**방식 = B(별도 팔 메시)**, 사용자 결정: *"재킷 소매 때문이라도 아예 다른 걸 써야"*. ADR 0002가 적어둔 안(같은 메시 + 머티리얼 슬롯 분리)은 소매를 몸통에서 떼는 손 작업이 결국 같은데 **바디 메시까지 재임포트**해야 해서 기각. 대신 팔 에셋을 계속 유지하는 비용을 받는다(의상·스킨 바뀌면 재추출).
**P2 플레이스홀더는 재킷을 아예 빼고 맨팔만**(사용자 수정).

### ✅ P1 UE 배선 완료 (빌드 Succeeded, UHT `-WarningsAsErrors` 포함 0에러·0워닝)
`AFPSRCharacter`에 `FirstPersonArms`(바디에 attach → 스케일 0.8806 상속, `OnlyOwnerSee`) + `FirstPersonArmsMesh`(소프트 참조, **BP에서 지정**) + 단일 컴포저 `RefreshFirstPersonRendering()`. 훅 = `BeginPlay`(리슨서버 호스트) + `NotifyControllerChanged`(클라 + 빙의 해제. `PossessedBy`가 이걸 부르므로 별도 훅 불요).
- **메시가 비어 있으면 분리를 아예 안 켠다** — `WorldSpaceRepresentation`은 엔진이 `bOwnerNoSee=true`를 걸어버리므로, 팔 없이 켜면 오너 화면이 빈다. 왼손 IK가 그립 없을 때 꺼지는 것과 같은 규칙.
- 🚨 **무기는 태깅하지 않는다**(설계 결정, 엔진 소스 근거). `PrimitiveSceneProxy.cpp:620` — `bIsFirstPerson`이면 `bCastDynamicShadow/Static/Volumetric/Far`가 **전부 강제 off**. 그리고 렌더러는 FirstPerson 프리미티브를 뷰별로 거르지 않는다(오너 전용은 `bOnlyOwnerSee`의 별개 일). 공용 무기 1벌(ADR 0002)을 태깅하면 **동료 화면에서도 총 그림자가 사라진다.**
- **ADS 정렬 리스크는 엔진 소스로 닫았다** — FP 패스의 별도 FOV·스케일은 `UCameraComponent::bEnableFirstPersonFieldOfView`/`bEnableFirstPersonScale`로 켜는 옵션이고 **둘 다 기본 false**(`CameraComponent.cpp:97-98`), 꺼져 있으면 `FirstPersonFOV = FOV` / `FirstPersonScale = 1.0`(`:475-476`)이라 **태깅이 렌더 위치를 안 바꾼다** → `UpdateAimDownSights`의 월드 정렬 그대로 유효. **이 둘은 건드리지 않는 게 결정사항**이고, 총이 벽에 파고드는 걸 없애려 켜게 되면 ADS 정렬을 다시 재야 한다.

### ✅ P2 팔 메시 완료 — 저작·검증·임포트 (2026-08-03)
`Blu_FP_Arms` = `/Game/Characters/Blu/SkeletalMeshes/Blu_FP_Arms/SkeletalMeshes/`. **재킷 제거 후 맨팔만**(사용자 결정 — 소매가 팔 실루엣의 거의 전부라 별도 메시가 맞다).
- **검증(헤드리스 Blender 계측)**: 구멍 0 · 논매니폴드 0 · 미웨이트 버텍스 0 · 느슨한 지오메트리 0 · 본 108 · 머티리얼 `Body` 하나 · 바운드 x±72.4 z136~147(=팔만). GLB `Saved/NeonV/fp_arms/Blu_FP_Arms.glb`.
- 🚨 **export는 반드시 glTF(GLB)** — `NeonV_scripts/fp_arms_export.py` 첫 줄 경고: *"FBX 는 쓰지 않는다 — 뼈가 미터/메시가 cm 로 잡혀 회전 시 팔이 원뿔로 붕괴한다. rest 포즈만 보면 멀쩡해 보인다."* ([[glb-import-crash-use-fbx]] 메모리는 *에디터 자동임포트* 사례라 여기 해당 없음)
- 🚨 **본 이름 변환은 export 세션 안에서만** 한다. Blender는 점(`upper_arm.L`), UE Blu 스켈레톤은 언더바(`upper_arm_L`) — 점→언더바 치환이 **108개 전수 일치**(`.uasset` 이름테이블 grep 대조). `.blend`에서 바꾸면 `NeonV_locomotion.blend`(액션 진실원천)와 조용히 갈라진다.
- **export 대상 정리 필요**: 이 씬에 카메라 10 · 라이트 12 · `REF_*` 메시 3이 있는데 `fp_arms_export.py`는 전체 선택이라 다 딸려간다. `glTF_not_exported` 컬렉션으로 옮기거나 export 스크립트에서 제외할 것.
- 💥 **스켈레톤 사고 1회(복구됨)** — 임포트 소스를 잘못 잡고(마네킹 1인칭 팔) Skeleton에 Blu를 지정했더니 **Blu 스켈레톤이 108본 → 65본(마네킹)으로 통째로 교체**됐다. 541개 에셋이 참조하는 에셋이다. "Skeleton Conflicts" 창은 **소스 파일이 틀렸다**는 신호였는데 무시하고 Done을 눌러 발생. 복구 = 에디터 닫고 `git checkout`(바이트 단위 복원 확인). 상세·재발방지 = 메모리 [[ue-import-overwrites-target-skeleton]].

### ✅ P3 해결 — 애님 그래프는 무죄였다, 1인칭 팔이 만든 회귀 (2026-08-03)
**증상**: 로코모션 완전 정지. 원인은 **뼈 갱신(Evaluate)이 아예 안 돌고 있었던 것**이고, 애님 그래프·레이어 링크·BlendSpace는 전부 정상이었다.
1. `ACharacter` 생성자가 바디 메시를 `AlwaysTickPose`로 둔다(`Character.cpp:124`, **엔진 기본값**).
2. P1/P2가 바디를 `WorldSpaceRepresentation`으로 태그 → 오너 화면 일반 패스에서 안 그려짐 → **`bRecentlyRendered=false`**(실측).
3. `ShouldUpdateTransform()`은 정확히 그것만 본다 — `SkinnedMeshComponent.cpp:1617`: `bRecentlyRendered || option==AlwaysTickPoseAndRefreshBones` → **false → `RefreshBoneTransforms` 스킵.**
4. `AlwaysTickPose`라 **그래프 Update는 계속 돈다** → Speed 1400·Direction·bIsAiming이 메인/레이어 양쪽에 멀쩡히 도착한다. **평가만 안 돈다.**
5. 뼈가 마지막 평가 결과(스폰 직후 Speed=0 아이들)에 고정. 1인칭 팔은 `LeaderPoseComponent`로 바디 뼈를 쓰므로 같이 언다.

**수정** = `AFPSRCharacter` 생성자 1줄 — `GetMesh()->VisibilityBasedAnimTickOption = AlwaysTickPoseAndRefreshBones`. 무조건(사용자 결정 2026-08-03): 플레이어는 최대 4명이라 예산과 무관하고, 적은 자기 설정을 유지한다.

**검증(실측, 달리는 중)**: 본 Z 스팬 **0.00cm(3024프레임) → foot 37.3/46.4 · hand_r 94.9 · upper_arm_l 89.5cm(300프레임)**. 바디↔팔 수치 완전 일치 · `WorldSpaceRepresentation`·`recently_rendered=false` 유지(분리는 살아 있음) · 빌드 `Result: Succeeded` · 스모크 `Result={Success}` · BP CDO 상속 확인.

> 🪤 **함정 3개 — 앞 세션 오진의 정체**
> 1. **"값이 정상"은 애니가 도는 증거가 아니다.** `AlwaysTickPose`는 Update만 돌린다. Speed/bIsAiming 프로브가 전부 초록이어도 포즈는 얼어 있을 수 있다 → **판정은 뼈 좌표의 시간 변화(span)로, 그리고 반드시 움직이는 상태에서.**
> 2. **폴백 스텁 클립과 BlendSpace의 Speed=0 샘플이 같은 애셋이면 둘을 구분할 수 없다.** 여기선 둘 다 `Blu_W2_Stand_Relaxed_Idle_IPC`였고, 그래서 "스텁이 재생 중"이라는 오진이 나왔다. 앉은 채 idle로 잰 것이 결정적 실수.
> 3. **`LinkedAnimLayer`는 그래프 위치와 무관하다.** `LinkedAnimLayerNodeProperties`는 생성 클래스의 모든 해당 프로퍼티를 훑어 담는다(`AnimBlueprintGeneratedClass.cpp:498-511`). "상태기계 안이라 안 붙는다"는 메커니즘은 엔진에 없다 — 그 가설로 노드를 옮기지 말 것.

<details><summary>(이력) 앞 세션이 남긴 오진 기록</summary>
**1인칭 팔 자체는 완전히 정상이다**(실측: `LeaderPoseComponent`=CharacterMesh0 · 팔=`FirstPerson` · 바디=`WorldSpaceRepresentation` · 바디↔팔 본 오차 **0.00cm** · 프록시 무영향 · ADS 조준소켓 좌우 `yaw 0.0°`). **바디가 잘못된 포즈를 재생하는 게 문제**이고 이건 1인칭 팔과 무관한 별건이다.

**실측**: `bIsAiming=True` · `StanceBlend=1.0`(완전히 앉음) · `Speed=0` 인데 나오는 포즈가 `Stand_Relaxed_Idle`과 **0.1°**, `Crouch_Aim_Idle`과 **1029.8°**. 조준·스탠스·속도 **어떤 입력에도 반응하지 않고 늘 한 클립**이며, 그 클립이 `ABP_Blu_Body`의 **폴백 스텁**에 넣어둔 것이다. 값 전달은 정상(메인·레이어 양쪽 `bIsAiming=True`, `StanceBlend=1.0` 확인).

**기각된 가설 3개(전부 실측으로)**: ①`BlendListByBool` 반전 — 엔진 소스가 `BlendPose_0`=True로 못박음(`AnimNode_BlendListByBool.cpp:12`), 그래프도 Aim이 0번에 물려 있다 ②BlendSpace 그리드 — 오늘 구워 저장 완료, 그리고 스텁 클립이 나오는 건 블렌드스페이스로 설명 불가 ③`SetLeaderPoseComponent`가 링크를 깼다 — 런타임에서 `unlink→link` 재실행해도 그대로.

**남은 가설(다음 세션이 검증할 것)**: **`LinkAnimClassLayers`가 상태기계 상태 안의 레이어 노드를 대상으로 안 잡는다.** 지금 두 노드의 유일한 차이가 위치다 — `ApplyWeaponAimOffset`은 루트 `AnimGraph`에 있어 **붙고**, `GetWeaponLocomotionPose`는 2026-08-03 아침에 `Locomote` 상태 안으로 옮겨서 **안 붙는다**.
- **검증법**: 그 노드를 루트 `AnimGraph`로 되돌려 Slot에 직결(상태기계 우회)하고 PIE. 조준/앉기에 포즈가 반응하면 가설 확정.
- 확정 시 재설계 방향: 로코모션 포즈를 **상태기계 밖**에서 만들고, 상태기계는 Slide/Wall/Air/Down/Turn 같은 특수 상태만 덮어쓰는 구조(`Blend Poses by bool`로 합성). 캐시포즈 경유는 이미 죽은 걸 확인했으므로 되돌리지 말 것.
- ⚠️ 아침에 "정상 작동"으로 확인된 뒤 바뀐 건 **폴백 스텁 클립 교체 + 그때의 BP 재컴파일**뿐이다. 재컴파일이 노드 목록을 다시 만들며 갈렸을 가능성이 있으니, 검증 시 **컴파일 직후/재시작 후를 각각** 재볼 것.

**진단 레시피**: 판정은 라이브 본 로컬 회전을 후보 클립들과 **각도 거리**로 비교(가장 가까운 것이 재생 중인 클립).
</details>

### 🔧 (근거) 루트 요를 벽 법선에 맞추는 이유 (사용자 승인 2026-08-02)
**지금은 `bUseControllerRotationYaw = true`라 캡슐(=메시) 방향 = 플레이어 시선이고, `StartWallHang`도 `PhysCustom`도 캐릭터를 벽 쪽으로 돌리지 않는다**(`WallNormal`을 저장만 함). 그래서 어떤 각도로 저작하든 인게임에서 벽과의 관계가 매번 달라진다 → **루트 요를 벽 법선에 정렬**해야 포즈에 박힌 각도가 "벽 기준 상대 각도"로 확정된다. 그러면 왼쪽/오른쪽/뒤쪽 어느 벽이든 동일하게 나오고 **미러 버전은 불필요**.
- ⚠️ **`RootYawOffsetMax = 90` 예산 주의**: 86° 포즈면 *몸이 향한 쪽*을 볼 때 0°, **벽 정면을 볼 때 86°**(90 중 86 사용, 빠듯), *벽 따라 반대쪽*을 볼 때 176° → **클램프**되어 86° 어긋난 채 굳는다. 즉 "편한 방향"이 한쪽으로 쏠린다. 거슬리면 **그때 미러본을 추가**(포즈 좌우 반전은 스크립트로 가능, 재저작 불요) — 미러의 근거는 "반대편 벽"이 아니라 **"반대쪽을 볼 때"** 다.

### 🖐 1인칭 팔 메시 = 선택 사항 (호환은 실증 완료, 채택 여부 미정)
위 "1인칭 팔 메시" 절 참조. **효과가 8%p뿐**이라 가림 해소용으로는 값어치가 낮고, 채택하면 **Apex식 룩**(내려다봐도 내 다리·몸이 안 보임)을 얻는 대신 **팔 메시 에셋 하나를 계속 유지**해야 한다(의상·스킨 바뀔 때마다 재추출). 현재 `NeonV_fp_arms.blend`에 자동 컷 결과가 출발점으로 들어 있다.
그 뒤 **3b 코드**(CMC 3 + AnimInstance 4 + 무기가시성, 빌드필요) · **3c `ABP_Blu_Body` 상태기계 배선**(에디터+수동).

**⏳ 사용자 확인 대기(Blender)**: 발목 교정이 큰 편이라(오른발 **81°**) 눈으로 볼 것 — 원본이 그만큼 처박혀 있었다는 뜻이고 렌더상 신발은 정상으로 얹혔다. `wall_slip`은 **내가 만든 구성**이므로(검증된 포즈 조합) 포즈 확정권자 확인 필요.

## 🎯 True First Person 전환 = ✅A·B·C 코드 + ✅3단계 애니 완료 / **4단계 AnimBP 인계** (2026-07-30, `refactor/character`)
> **설계 = [ADR 0002](Docs/Architecture/0002-true-first-person-shared-animation.md)** — 새 세션은 **ADR 0002 전문을 먼저 읽을 것**(불변식 10개 + 실측 + 기각안이 전부 거기 있다). 아래는 진행 상태만.
> 목표 = 1인칭·3인칭을 **3P 애니 팩 한 벌**(`Content/Rifle_01`)로 덮기. 1P 전용 팔 + PWAS 폐기.

### 완료 (커밋 5개)
| 커밋 | 내용 |
|---|---|
| `ab9fede3` | ADR 0002 작성 — 축 4개 결정(시각 회전 분리 / 눈 앵커 / ADS 혼합 / 관전자 소유 리그) + 불변식 10 |
| `f678ba10` | Rifle_01 팩(310MB, LFS) + IK Rig 2개 + Retargeter + 리타게팅 4종. **무기 스케일 85% 확정** |
| `ffe45a21` | **A단계** — 무기 DA 1P/3P 필드 통합, `WeaponAttachScale`·`LeftHandSocket` 추가, 3P 블록 삭제. 빌드 통과 |
| `11d8db05` | ADR 갱신 — 1인칭 실측으로 **ADS 정렬 방식 재결정**(아래) |
| `0a05baa6` | **B+C단계** — 무기 컴포넌트 단일화 + 머리 숨김 + ADS 무기-to-카메라 + 왼손 IK 이음매. 빌드 3회 통과 · Codex 1건 반영 |
| `a4ad0ddd` | 대응 콘텐츠 — `BP_FPSRPlayer` 재저장(고아 컴포넌트 정리 + `Blu_ABP_Unarmed` 배정), `DA_Weapon_Rifle` 저작(스케일·왼손소켓·PWAS 제거) |
| `258501be` | **축 2 눈 앵커 = 고정 항** — 카메라를 가슴 앞으로(`FirstPersonCameraOffset`) + 반지름 클램프. ADR 축 2 공식 2곳 수정 |
| `76b272c0` | "팔만 보이게"(Apex식) 조사 — 지금 데이터로는 불가, 4단계 후 재판단 |
| `d719e6c0` | **3단계** — Rifle_01 **426개** Blu 리타게팅(106MB LFS). 재발방지 검증 통과 |
| `9edf528d` | **무기 85% 결정의 구멍** — 전량 실측으로 그립 이동안(B) 부활 |
| `a7fab2e5` | 에임오프셋 포즈 48개를 Mesh Space 애디티브로 선처리 |
| `324972df` | **AimOffset 4개 도입 + 대각 좌표 16개 정정**(내가 준 좌표표가 틀렸다 — 아래) |
| `7d5fe569` | **4단계 선행** — 조준 상태 복제(Push Model + `COND_SkipOwner`) + 프리즈 래치 결함 수정. 빌드 3회 통과 · Codex 4회 · ✅PIE |
| `1d41ddb7` | **4a-1 코드** — `UFPSRCharacterAnimInstance` + 무기별 애니 레이어 링크(무기 DA `BodyAnimLayerClass`). 빌드 2회 · Codex 4회 |

### ✅ PIE 1차 결과 (사용자)
- **정상**: 앞뒤좌우 시야 · 머리 숨김 · ADS(조준경이 화면 중앙)
- **문제**: **아래를 볼 때** 자기 몸을 관통해서 보임 → 이번 커밋에서 수정
- **판정 불가**: 힙에서 총이 손에 붙었는지 — 팩 힙 포즈가 총을 화면 밖(오른손 세로 −66.5°, 화면 아래 한계 약 −29°)에 두므로 **1인칭으로는 원래 안 보인다.** PIE에서 **F8(Eject)** 로 밖에서 봐야 확인된다

### ⚠️ 알아야 할 반전 하나 (B+C가 이걸 구현했다)
**축 3의 "ADS = 애니 포즈 + 잔여 오차 보정"은 폐기됐다.** 실제 눈 위치에서 재보니 조준경이 **가로 +29.7° / 세로 −27.1°** 로 ADS 화면(±27.5°/±16°) **밖**이다. 보정이 아니라 재배치가 필요하다.
→ **ADS 중 무기를 카메라 기준으로 재배치**(사용자 안 채택). 기존 `UpdateAimDownSights` 역산 수식을 그대로 쓰고 **대상만 팔 → 무기**로 바꿨다. 원격은 변화 없음(`hand_R` + 조준 애니). **두 손 모두 ADS 화면 밖이라 손 숨김 불필요.**
→ 불변식 4에 예외 명시됨(ADS 중 무기 부착점만 로컬이 다름). **예외를 두 번째로 늘리려 하면 불변식 4 자체를 다시 볼 것.**

### B+C단계에서 실제로 한 것 (`FPSRCharacter.{h,cpp}` 중심 + 무기 4파일)
1. **컴포넌트 단일화** — `FirstPersonArms`·`WeaponMesh1P`·`WeaponMeshStatic1P` → `WeaponMesh`/`WeaponMeshStatic` 2개. 부착 = `GetMesh()`의 `SOCKET_Weapon`, **`SnapToTargetNotIncludingScale` + `SetRelativeScale3D(WeaponAttachScale)`**(스케일 규칙이 KeepWorld라 부착만으론 크기가 안 붙는다 — 명시 호출이 필요). 그림자 끄기 삭제(이제 월드 오브젝트). `RefreshFirstPersonWeaponVisual` → **`RefreshEquippedWeaponVisual`로 개명**(더는 1인칭 전용이 아님)
2. **가시성** — 바디 `SetOwnerNoSee` · 파츠 `SetOnlyOwnerSee` 제거. 신규 `UpdateFirstPersonBodyVisibility()`(캐릭터 Tick)가 **뷰 타겟 == this 일 때만** `HideBoneByName`. 본 이름 = `HeadBoneName`(EditDefaultsOnly, 기본 `head`) + **없는 본이면 경고 로그**(엔진은 조용히 무시한다 — 불변식 9의 함정을 실제로 막음)
3. **`UpdateAimDownSights`** — 상태 기계 전부 보존, 대상만 무기로. 힙 기준이 "BeginPlay에 캡처한 팔 자세" → **매 프레임 읽는 그립 소켓**으로 바뀌었고, 모든 프레임 계산을 **스케일 제거(RigidFrame)** 후에 한다(0.85가 소켓 오프셋까지 줄여서 조준경이 덜 나가는 것 방지). 마지막 쓰기 1줄 = `SetWorldLocationAndRotation`(부모가 움직이는 손이라 상대 쓰기는 손 흔들림이 다시 섞인다). **㉰-b(hand_R+양팔 IK)로 갈아타는 건 이 1줄의 대상만 바꾸면 된다**
4. **왼손 IK 이음매** — `CachedLeftHandSocket`/`CachedLeftHandComponent`(파츠 해석은 AimSocket과 동일 형태) + `GetLeftHandGripTransform(FTransform& Out) → bool`(BlueprintPure, **월드 공간**, 소켓 없으면 false = 불펍 null-safe)
5. **곁다리(단일화의 직접 결과)** — 몽타주가 전부 바디 1벌로 합쳐짐(장착·발사·재장전, 소유자/원격 동일). `MulticastFireCosmetics`의 **총구 화염·노리쇠 몽타주 관전자 전용 게이트 해제** — "1P 무기가 원격에 안 보여서 아군 머릿속에 화염이 뜬다"는 이유가 사라졌다(이제 아군 사격이 보인다)

### 축 2(눈 앵커) = 고정 항만 구현. 동적 항은 4단계 이후
증상은 "아래를 볼 때 몸 관통"이었고 원인은 카메라가 `(0,0,BaseEyeHeight)` = **목 높이 몸통 중심축**이라 절두체가 거리 0의 자기 가슴을 향한 것. 상세·기각안 = **[ADR 0002 "눈 앵커 실측 — 축 2 공식 수정"](Docs/Architecture/0002-true-first-person-shared-animation.md)**. 요지 2개:
1. **감쇠 하나로는 안 된다** — 감쇠는 *흔들림*용인데 필요한 건 *정지 상태에도* 유지되는 고정 보정. 항을 둘로 쪼갰다(`FirstPersonCameraOffset` 신설, 기본 `(10,0,0)`, BP 클래스 기본값에서 튜닝). **동적 본 추종은 미구현** — 바디 AnimBP가 로코모션 전용이라 따라갈 머리 움직임이 없고, 동적 항이 곧 본 읽기 틱순서/URO 의존을 끌고 온다
2. **클램프는 캡슐 봉쇄가 아니라 반지름 봉쇄** — 문자 그대로 캡슐에 가두면 **기존 앉기 블렌드가 깨진다**(블렌드는 일부러 시야를 캡슐 위에 붙들어 둔다). 벽 너머 사격을 막는 건 높이가 아니라 반지름이므로 **횡 성분만** 잡고 Z는 스탠스 시스템에 맡긴다. **불변식 2 문구도 정정**(원문은 한 번도 참인 적이 없었다)

부수: 사격 원점이 앞으로 10cm 이동(근접 체감 사거리 +10cm) · 클램프 발동 시 개발빌드 상단에 `CAM CLAMPED Xcm` 표시.

### ⏸️ 보류 — "에이펙스처럼 팔만 보이게"는 4단계 후 재판단 (사용자 결정)
카메라 수정 후 PIE에서 몸이 **바깥면으로** 보이는 건 해결됐지만, 사용자가 레퍼런스(Apex)처럼 **손·팔 외 전부 숨김**을 원함. 조사 결과 **지금 데이터로는 불가** — 상세 = **[ADR 0002 "에이펙스처럼 팔만 보이게"](Docs/Architecture/0002-true-first-person-shared-animation.md)**. 3줄 요약:
1. **뼈로는 구조적 불가** — `RebuildVisibilityArray`가 자식을 같이 숨기는데 **팔이 `chest`의 자식**이다. 지울 수 있는 건 `neck`·`upper_leg_L/R`뿐이고 다리만 지우면 더 어색해진다
2. **슬롯으로도 불가**(6개 Isolate 실측) — `Body`(맨살)도 `Jacket_Trim`(소매 있음)도 **팔과 몸통을 한 슬롯에** 담는다
3. **저작해도 걸림돌 2개** — 어깨 **단면이 뚫려 보이고**(캡 필요), **숨긴 섹션은 그림자도 안 만든다**(`SkeletalMeshSceneProxy.cpp:232`) → 자기 그림자가 팔만 남는다. 그림자를 살리려면 **같은 메시를 컴포넌트 2개**로(그림자 전용 바디 + 카메라용 팔). 1P 전용 모델을 만드는 건 아니므로 비목표 1은 안 깨진다

**보류 이유** = 지금 팔이 옆구리에 내려가 있어(`Blu_ABP_Unarmed`) 몸통이 시야를 다 덮는다. 4단계에서 팔이 가슴 앞으로 올라온 뒤 다시 보고 저작 여부 결정 → 헛된 Blender 작업 방지.

### ✅ 4단계 선행 = 조준 상태 복제 완료 (`bIsAiming` → Push Model + `COND_SkipOwner`)
`IsAiming()`이 **이제 모든 머신에서 유효하다** — 4단계 AnimBP가 이걸로 조준 포즈를 몰아도 원격 플레이어의 몸이 조준한다. 설계 근거·감수사항·트립와이어 전문 = **[ADR 0002 "조준 비트 복제 — 4단계 선행"](Docs/Architecture/0002-true-first-person-shared-animation.md)**. 요지 4개:
1. **새 변수 없이 `bIsAiming` 자체를 복제**했다(별 변수 + mux 안 기각 — mux는 SkipOwner가 이미 하는 일이고 진실원천만 2개가 된다). 안전 근거 = 행동 소비자 전수조사에서 **프록시에서 반응하는 코드가 0**(`UpdateAimDownSights`는 이중 게이트, FOV는 무기 tick, GA는 서버·오너, HUD는 로컬).
2. 대신 **쓰기를 구조적으로 차단** — `SetAiming`이 권위·로컬컨트롤 아닌 호출을 거부한다. 가상의 위험이 아니었다: **`AFPSRPlayerState::OnRep_LifeState`가 "Owning client"라고 적힌 채 프록시에서도 돌고 있었다**(그 가드도 같이 넣음).
3. **같이 고친 잠재 결함** — 프리즈 클리어 경로가 *이미 프리즈가 켜진 뒤* `SetAiming(false)`를 보내는데 서버가 그걸 거부하고 있었다 → 서버 조준이 true로 래치. 복제 전엔 확산만 틀렸지만 복제 후엔 **원격 바디가 조준 포즈로 굳는다**. 수정 = 게이트를 방향성으로(**조준 ON만 막고 해제는 항상 통과** — 리포 선례 `Input_CrouchReleased`). `OnAim` 훅 게이트는 W1 P3-3 보장 때문에 **원래대로 보존**.
4. **감수** = 서버가 조준 ON을 거부하면 오너 교정이 안 간다(SkipOwner). 최대 1 RTT이고 **그 낡음을 만든 OnRep이 그대로 클리어를 실행한다**. `COND_None`은 빠른 탭에서 오너 ADS가 깜빡여서 기각.

**✅ PIE 확인 완료**(사용자, 2026-07-30) — 오너 ADS 감각 회귀 없음, 방어 로그(`component replication is DISABLED`) 미발생 = `BP_FPSRPlayer`에 오버라이드 없음. 조준 **포즈**는 4a-2 이후에 보인다.

### ✅ 3단계 완료 — Rifle_01 426개 Blu 리타게팅 (`d719e6c0`)
`Content/Characters/Blu/Anims/W2_Rifle/` **426개**(106MB, LFS) = In-Place 346 + Aim_Offset 48 + Holster_Reload_Fire 32. 제외 = Root_Motion 348(ADR 확정)·Split_Jumps 108(보류)·Legacy 11. 기존 97개(`Blu_MF/MM_Rifle_*`)는 접두사가 달라 **무손상**, `_Spike` 4개는 정식본이 생겨 삭제.
- **`216e7cca` 재발 방지 검증 통과** — 그때 걸린 지점이 리그가 아니라 리타게터 op였으므로 op를 직접 읽었다. FK Chains 20/20 매핑(손가락 10 + 쇄골 2 포함). 소스↔출력 본별 대조 **30/31 일치**, 나머지 1개는 척추 3→2 압축이고 `spine_02+spine_03 = Blu chest`로 합이 정확
- 안 넘어온 것 = `ik_*`(Blu에 IK 본 없음 — **왼손 IK가 필요한 바로 그 이유**)·`*_twist_*`(트위스트 본 없음). 둘 다 정상
- 리타게터 2개 용도: **`RTG_Manny_to_Blu`=Blu 팩 97개 / `RTG_UE4Man_to_Blu`=Rifle_01**. 헷갈리지 말 것

### 🚨 무기 85%가 전량에서는 통과 못 한다 (3단계 중 발견)
**ADR의 85%는 애니 4개를, 그것도 사실상 프레임 0(정지 포즈)에서 잰 값이었다.** 186개 양손 파지 조준 애니를 전 프레임으로 재니 **54개가 음수, 최악 −3.11cm**. 같은 애니가 프레임0 **+2.42** ↔ 전체 **−2.34**. 계측은 ADR 자신의 수치(리치 41.54·간격 18.92·발 15.1)로 교정 확인됨. 상세 = [ADR 0002 "무기 85% 결정의 구멍"](Docs/Architecture/0002-true-first-person-shared-animation.md).
→ ADR이 *"85%면 충분하니 불필요"* 로 기각했던 **안 B(그립을 매거진웰로 이동)가 되살아난다.** 스케일만으로 덮으면 ~73%(장난감), **그립 이동은 그것만으로 최악을 덮는다**(요구간격 −3.4cm@85%). **4단계에서 왼손 IK를 붙인 직후 판단** — `SOCKET_LeftHand` 위치는 콘텐츠 결정이다.

### ✅ AimOffset 4개 완료 (`324972df`) — 위치가 예상과 다르니 주의
**`Content/Rifle_01/AimOffset/`** 에 있다(`W2_Rifle/` 아님):

| 에셋 | 재료 | 샘플 | 미리보기 베이스 |
|---|---|---|---|
| `AO_Stand_Aim` | `Blu_W2_Stand_Aim_Point_*` | 17 | `Blu_W2_Stand_Aim_Idle_IPC` |
| `AO_Crouch_Aim` | `Blu_W2_Crouch_Aim_Point_*` | 13 | `Blu_W2_Crouch_Aim_Idle_IPC` |
| `AO_Stand_Relaxed_Look` | `Blu_W2_Stand_Relaxed_Look_*` | 9 | `Blu_W2_Stand_Relaxed_Idle_IPC` |
| `AO_Crouch_Look` | `Blu_W2_Crouch_Look_*` | 9 | `Blu_W2_Crouch_Idle_IPC` |

전부 Blu 스켈레톤 · 축 Yaw/Pitch −90..90 grid 4 · 전 샘플 Mesh Space 애디티브(`a7fab2e5`에서 선처리) · `interpolate_using_grid=False`(삼각분할 직접 보간).

> ⚠️ **좌표 규칙 — 이름의 숫자는 Yaw가 아니다.** 앞 세션이 명명 규칙을 *추론*해 틀린 표를 줬고, 팩 포즈를 실측해 정정했다(머리 시선축의 방위·고도를 `Center` 대비로 측정).
> - `L45/L90/R45/R90` → **숫자 = Yaw**, Pitch 0 (실측 dYaw −45.0/−90.0/+45.0/+89.9)
> - `U45/U90/D45/D90` → **숫자 = Pitch**, Yaw 0 (실측 dPitch +51.7/+103.3)
> - `LU45·LD45·RU45·RD45` → **Yaw는 항상 ±90**, 숫자 = Pitch (실측 dYaw −91.2/+88.6, dPitch ±44.8)
> - `LU90·LD90·RU90·RD90` → (±90, ±90)
>
> **에디터에서 그리드 위 미리보기는 `Ctrl`을 눌러야 움직인다**(엔진 상태바 문구 그대로). 안 움직인다고 에셋 문제로 오판하지 말 것.

### 🎯 4단계 = 4a/4b/4c로 쪼갬 (사용자 결정) — **4a 코드 완료 / 4a 콘텐츠가 다음**
**4a = 코어**(아래) · **4b** = 8방향 시작·정지 전환(클립 40개+) + Split_Jumps 108 리타게팅 · **4c** = 슬라이드.
구조 = **애니 레이어 인터페이스**(사용자 결정): 바디 ABP 1개 + 무기별 레이어, 무기 DA `BodyAnimLayerClass`.
설계 전문 = **[ADR 0002 "바디 AnimBP 4a"](Docs/Architecture/0002-true-first-person-shared-animation.md)**.

#### ✅ 4a-1 코드 완료 (`1d41ddb7`)
`UFPSRCharacterAnimInstance`(신규 `Hero/`)가 이동·조준·생존 상태를 프레임당 한 번 계산해 발행하고, 그래프는 그것만 읽는다. 알아야 할 4개:
1. **데이터 흐름은 메인 → 레이어 푸시**(플랜의 "주입 후 당겨 읽기"를 뒤집었다). `TickAnimInstances`가 **링크된 인스턴스를 메인보다 먼저** 업데이트하므로 당겨 읽으면 레이어 상태기계가 지난 프레임 값으로 전이한다. 엔진 훅 `PreUpdateLinkedInstances`에서 계산·푸시하고 `NativeUpdateAnimation`은 프레임 가드 폴백. 덕분에 "레이어 함수가 같은 group이 아니면 한쪽만 주입" 함정도 소멸
2. **`RootYawOffsetMax` = AimOffset yaw 범위(±90)에 묶임** — 하체가 상체보다 더 밀리면 상쇄가 클램프되어 조준이 크로스헤어를 못 따라온다. 넓히려면 **AO를 먼저 더 넓게 저작**
3. **오프셋 소비는 코드가 한다**(`TurnInPlaceRateDegPerSec`) — 팩 회전 클립은 In-Place 변환본이라 루트모션이 없다. 클립은 시각만
4. **`bIsSliding`/`SlideBlend`는 노출조차 안 했다** — 4c이고, 그 값이 시뮬레이티드 프록시에서 맞는지 미검증(이 그래프는 모든 머신에서 돈다). **슬라이드 착수 시 그것부터 확인**

#### ⏳ 4a-2 콘텐츠 = 다음 (에디터 필요)
**노드 순서 3제약을 지킬 것**(바꾸면 조용히 반쯤 동작한다 — ADR에 근거 있음):
`Layer.로코모션 → Slot('DefaultSlot') → Layer.에임오프셋 → RotateRootBone → TwoBoneIK(hand_L, **World**) → Output`
1. **BlendSpace 4개** 신규(Stand/Crouch × Aim/Relaxed, Direction × Speed). 재료 = `Walk_Aim`·`Jog_Aim`·`CrouchWalk_Aim` 8방향
2. **라이플 레이어 ABP** — 상태기계 Idle / Move / TurnInPlace / Air. 인터페이스 함수 **2개**(`GetWeaponLocomotionPose` + `ApplyWeaponAimOffset`) — 단일 함수면 에임오프셋이 Slot 앞에 갇혀 몽타주가 조준 pitch를 죽인다. Idle은 `StanceBlend`(0~1 **연속**)로 블렌드, bool 스냅 금지
3. **Air 임시 브릿지** — 하체 `Blu_MM_Jump`/`Fall_Loop`/`Land`(Blu 자체, **이미 3분할**) + spine 이상은 라이플 조준 아이들(맨손 낙하 위에 라이플 AO만 얹으면 애디티브 기준 포즈가 달라 어깨·오른손이 틀어진다). `bIsOnWall`도 여기로
4. **베이스 ABP** — `UFPSRCharacterAnimInstance` 파생. `TwoBoneIK`는 `bAllowStretching=false` + **JointTarget(팔꿈치 폴) 필수**(없으면 elbow flip)
5. **재장전·장착 몽타주에 `LeftHandIKWeight` 커브 0 저작** — 몽타주 이름 문자열 판정 금지
6. `BP_FPSRPlayer`의 AnimBP 교체 + `DA_Weapon_Rifle`에 레이어 클래스(피커가 `UFPSRCharacterAnimInstance` 파생만 보여준다)

**PIE 최우선 = 부호 검증**(좌/우 ±90 제자리회전). 틀리면 **AO와 이동 블렌드가 동시에** 반대로 보정된다.

4a 직후 판단 3개: **오너 ADS 왼팔 과신전 여부 → 85% vs 그립 이동**(알파로 덮지 말고 **계측**할 것 — 덮으면 판단 근거가 사라진다) · **힙 총 가시성**(F8 Eject) · **"팔만 보이게" 저작 여부**(위 ⏸)

### 블로커 / 주의
- **빌드 1회 ≈ 10분**(증분은 15~25초). `-NoXGE` 사용.
- 🚨 **에디터가 떠 있으면 빌드하지 말 것 — "실패한다"가 아니라 "성공해 버리고 에디터를 죽인다".** 이 줄의 원문은 *"Live Coding 락으로 실패"* 였는데 **틀렸다**(2026-07-30 실증: 락이 안 걸리고 `Result: Succeeded` 후 에디터 크래시, 미저장 작업 유실). 빌드 전에 **프로세스를 실제로 확인**할 것:
  `Get-Process | Where-Object { $_.ProcessName -match 'UnrealEditor' }` → 비어 있어야 빌드
- **모듈이 둘이다.** `Source/FPSRoguelite`(게임) + `Source/FPSRogueliteEditor`(무기 조립 툴). grep은 `Source/` 전체에 걸 것 — A단계 1차 빌드가 이걸 놓쳐 실패했다
- **VibeUE Python에서 레벨 전환 API(`new_level`/`load_level`) 호출 금지** — 에디터 즉사(2회 실증). 열린 레벨에 스폰하고 끝나면 지운다. 자동 저장이 꺼져 있을 수 있으니 에셋은 `save_asset` 명시 호출
- `unreal.Rotator(a,b,c)` = **(roll, pitch, yaw)** 순. 이 세션에서 두 번 틀렸다
- **VibeUE `execute_python_code`는 30초 타임아웃**이다(2026-07-30 실측). 넘으면 툴은 에러를 주지만 **에디터 쪽 작업은 계속 진행돼 결과가 남는다** — 재시도 전에 반드시 현재 상태를 조회할 것. 배치는 30초 안에 끝나는 크기로 쪼개고(리타게팅 0.23초/개 → 80개가 20초), **이미 만들어진 건 건너뛰게** 짜면 크래시·타임아웃에도 이어서 갈 수 있다
- **Bash 툴의 작업 디렉터리는 호출 간에 유지된다.** 이 세션에서 `cd Content/Rifle_01/Animation` 한 뒤 몇 턴 뒤 `find Content/...`가 "No such directory"로 나와 파일이 사라진 줄 알았다. 상대경로 쓰기 전에 `cd /e/Git_Project/FPSRoguelite &&` 를 붙일 것
- **본 애니 계측 API 함정**: `AnimationLibrary.get_bone_pose_for_frame(a,b,f,X)`의 4번째 인자는 `extract_root_motion`이며 **로컬 공간**을 돌려준다(컴포넌트 공간 아님). 컴포넌트 공간이 필요하면 `find_bone_path_to_root`로 체인을 합성해야 한다. 또 `get_animation_track_names`는 **애니되는 본을 다 나열하지 않는다**(`neck`·`spine`·`head`가 목록에 없는데 움직였다) — 트랙 목록으로 "안 넘어왔다"를 판정하면 오진한다
- 본 가시성(`hide_bone_by_name`) 변경 직후 같은 프레임에 렌더하면 **갱신 전 화면**이 나온다
- `Weapon_B`(불펍) 핸드가드 12종에는 `SOCKET_LeftHand`가 없다 → 왼손 IK는 **null-safe로 꺼지게** 만들 것

### 미커밋 콘텐츠
**없다 — 2026-07-30 인계 시점 작업 트리 완전 클린.** 이 세션의 콘텐츠(리타게팅 426 · 애디티브 48 · AimOffset 4)는 사용자 확인을 거쳐 전부 커밋됨.

### 남은 사용자 콘텐츠 작업
- **PWAS 정리 (아직 안 됨, 순서 중요)**: `DA_Weapon_SMG`를 열고 저장 → `Content/ProceduralWeaponAnimationSystem` 삭제 → `Content/Character/FPArms` 삭제. SMG가 유일하게 아직 PWAS(`ABP_FPChar`)를 가리키는데, 그 필드는 A단계에서 클래스에서 사라져 **로드 시 이미 버려진다**(동작 문제 없음). 다만 `.uasset` 안의 참조 기록은 재저장해야 지워진다. `FPArms` 9개는 바깥에서 참조 0건 확인됨(Characters·Weapons·Maps·UI·Cards·Actors·Game·Blockout·Mission 스캔)
- **무기 DA 나머지 8개**: `WeaponAttachScale`·`LeftHandSocket`은 `DA_Weapon_Rifle`만 채워졌다. 나머지는 해당 무기를 쓸 때 채우면 됨(A단계 CoreRedirects 3줄이 메시·파츠 값을 지켜주고 있다 — `Config/DefaultEngine.ini:123`)
- (참고) 이전 세션의 `Content/Maps` churn은 `a4ad0ddd`에서 커밋되어 정리됨

### (이력) 무기 DA 초기 작업 메모
- 무기 DA 9개에 `WeaponAttachScale`(라이플 0.85)·`LeftHandSocket`(`SOCKET_LeftHand`) 채우기 + PWAS 참조 비우기(`DA_Weapon_Rifle`/`SMG`의 `WeaponAnimInstanceClass`·`ReloadMontage`) → 그 뒤 `Content/ProceduralWeaponAnimationSystem` 폴더 삭제 (순서 중요: DA 먼저)
  - ⚠ **PWAS 참조 비우기가 B+C 이후 더 급해졌다**: `ReloadMontage`(PWAS 팔 스켈레톤용)가 이제 **Blu 바디 메시에서 재생**된다 → 스켈레톤 불일치로 재생 거부 + 경고 로그(크래시는 아님). `WeaponAnimInstanceClass`(PWAS 팔 ABP)도 무기 메시에 얹혀 같은 형태로 실패한다
- **에디터 첫 실행 시 예상되는 것**: `BP_FPSRPlayer`에 `FirstPersonArms`·`WeaponMesh1P`·`WeaponMeshStatic1P` **고아 컴포넌트 기록**이 남아 경고가 뜬다(A단계의 `WeaponMesh3P`와 같은 형태 — 무해, BP 재저장하면 정리됨). 새 `WeaponMesh`/`WeaponMeshStatic`은 C++ 기본값으로 시작하니 **BP에서 손으로 맞췄던 옛 상대 트랜스폼은 사라진다**(의도 — 정렬 주체가 이제 `SOCKET_Weapon`이다). 폐기 에셋 `Content/Character/FPArms`(+`SK_FP_Manny_Simple`)는 참조가 0이 되므로 정리 대상

## 🧹 BP 그래프 정리 = ✅완료 + **에디터 툴화** (2026-07-30, `refactor/character`) — **✅사용자 육안 확인 완료 2026-08-10**
> 요청 = `Docs/BPGraphLayout_ResumePrompt.md` → 이후 사용자 추가 요청(게터 복제·reroute·툴화).
> **로직·배선·값은 안 바뀐다.** 엔진 = `Content/Python/fpsr_bp_layout.py`.

### 🛠 앞으로는 에디터에서: **Tools > FPSR BP 노드 정리**
콘텐츠 브라우저에서 BP나 폴더를 고르고 메뉴를 누른다. 되돌리기(Ctrl+Z) 되고, **저장은 안 하니 확인 후 Ctrl+S**.
1. 선택한 블루프린트 정리(배치 + 게터 복제) / 2. 배치만 / 3. 수치만 보기 / 4. reroute까지(실험적)
- 그래프는 자동 인식(AnimBP 상태머신 포함). 외부 팩(`Polygon*`·`Synty`·`Rifle_01` 등)은 자동 제외.
- ⚠️ VibeUE 플러그인의 `BlueprintService`에 의존한다(`.uproject`에 Optional로 등록됨). 없으면 메뉴만 조용히 실패.

### 2차 — 게터 복제 (사용자 추가 요청)
- 여러 곳으로 뻗던 순수 변수 게터를 **소비 노드마다 하나씩**으로 쪼갬(29개 추가). 커밋 `391ab3a4`.
  순수 게터는 UE가 컴파일 때 어차피 소비처마다 다시 평가하므로 **의미·성능 동일**.
- **교차 146 → 111.** 큰 것 = `ABL_Blu_W2_Rifle`의 Ground·ApplyWeaponAimOffset·GetWeaponLocomotionPose 각각 **8 → 0**
  (`Get Speed`/`Direction`/`AimYaw`/`AimPitch`가 4갈래씩 뻗던 것).
- 검증 = **논리적 연결**(knot 관통해 접고 게터는 변수명으로 치환) 전 그래프 동일 · 재실행 시 무변화(멱등).
- 한계: 함수의 **로컬 변수** 게터는 복제 불가(`add_get_variable_node`가 멤버 변수만 만든다). `WBP_Lobby/RefreshPlayer` 1건.

### reroute(knot) = ❌ 채택 안 함 (사용자 판정 "애매")
기구는 됨(`create_node_by_key(bp, graph, "NODE K2Node_Knot", x, y)`, 핀 = `InputPin`/`OutputPin`).
하지만 파일럿에서 **4번 중 3번이 품질 게이트에 걸려 되돌아갔다** — 교차를 크게 늘린다(BP_LobbyDisplayPawn 4→15).
메뉴 4번에 실험적으로 남겨둠. **사용자가 손으로 놓은 reroute는 어떤 경우에도 보존**된다.

### ⚠️ 측정 기준 정정 (내가 두 번 틀렸다)
"노드 위를 지나가는 선"은 **63/761 = 8.3%**(상태머신 제외)가 맞다.
처음 보고한 38%는 선을 노드 **원점**끼리 이어 잰 값, 그다음 12%는 오염된 에디터 상태에서 잰 값이라 둘 다 틀렸다.
지금은 **출발 노드 오른쪽 가장자리의 출력핀 → 도착 노드 왼쪽 가장자리의 입력핀**으로 잰다(`endpoints()`).

### 1차 — 배치만 (좌표)
> 커밋 `ef11412e` UI/HUD → `5b9e0231` 문서.

- **작업 전 프롬프트의 전제 하나가 틀렸다** — 엔진 `BlueprintService.auto_layout_graph`는 **쓸 수 없다.**
  노드 많은 그래프에서 성공을 반환하면서 **0개 이동**(RunHUD 48노드·Lobby 77노드·BP_Door 29노드·DownedOverlay 45노드),
  작동하는 그래프에선 교차를 **되레 늘린다**(BossHUDBar 1→20, ApplyWeaponAimOffset 13→25).
  → 계층 배치를 직접 구현했다(약연결 덩어리 세로 분리 + 최장경로 계층화 + 게터를 소비 노드 핀 높이로 + barycenter 스윕).
- **결과(잰 값)**: 정리한 33개 그래프 합계 **교차 237→98 · 노드 겹침 97→0 · 역방향 선 61→8**.
  큰 것: WBP_Lobby EventGraph 22→1, WBP_RunHUD 41→11, ApplyBand 52→16, WBP_DownedOverlay 39→8.
  나머지 17개 그래프는 이미 깔끔해 **원본 유지**(품질 게이트 탈락).
- **안전**: 그래프마다 배선 지문(노드 구성 + 간선 전체 목록)을 전후 대조해 다르면 되돌린다. **되돌린 건 0건.**
  작업 후 39개 그래프 노드/간선 수를 작업 전 실측치와 재대조 → **불일치 0**. 전체 재실행 시 전부 keep-original(멱등).
- **컴파일은 부르지 않았다.** 좌표는 컴파일 결과에 안 들어가고, 자식 WBP를 품은 위젯을 프로그래매틱으로
  컴파일하면 재인스턴싱 중 에디터가 죽은 전례가 있다(2026-06-24). 저장은 `save_packages`(비모달).
- **⚠️ 별건 발견 — `ABP_Blu_Body`에 죽은 상태머신이 있다.** 출력에 연결된 건 `Locomotion2`인데
  이전 `Locomotion`(19노드)이 어디에도 연결되지 않은 채 남아 있고, 딸린 `Transition` 그래프가 **42개**,
  `Ground`가 **3개**로 불어나 있다. 4a 저작 스크립트가 두 번 돈 흔적. **배선을 바꾸는 일이라 손대지 않았다** — 별도 승인·별도 커밋 대상.
- ~~**남은 확인**: 에디터에서 `WBP_Lobby`·`WBP_RunHUD`·`BP_Door`·`ABL_Blu_W2_Rifle`을 열어 눈으로 보기 + 애님은 PIE로 로코모션 동일한지.~~ → **✅ 사용자 확인 완료(2026-08-10) — 이상 없음. 보드 행 완료 처리.**

## 🌬️ 에어 스트레이프 = ✅완료 (2026-07-29, `refactor/character`) — **✅사용자 PIE 확인 완료 2026-08-10**
> 사용자 요청 = "체공 상태 조작이 거의 안 된다" → 대화 중 목표가 **에어 스트레이프**(FPS 유저 기술)로 특정됨.
> 시나리오 = 슬라이드 → 점프 → 공중에서 **W 떼고 D 유지 + 마우스 오른쪽** → **속도를 보존한 채** 코너를 돈다.
- **처음 세운 "공중 가속을 올린다" 안은 폐기했다.** 그걸론 성립하지 않는다 — 엔진 `CalcVelocity`는 **속도 벡터 전체를 입력 쪽으로 가속하고 총합을 클램프**해서, 진행 방향과 어긋나는 입력이 **속도를 깎는다**(`CharacterMovementComponent.cpp:3863-3865`). 가속을 아무리 올려도 "속도를 잃지 않고 돈다"가 안 나온다. **알고리즘 자체가 다르다.**
- **채택 = Quake 계열 공중 가속 모델**. `AddSpeed = WishSpeed − dot(횡속도, WishDir)` 만큼만 입력 방향으로 더한다. **수직 입력이면 크기는 그대로 방향만 회전**(|v+εp|≈|v|)하고 예산이 가득 남아 계속 돌아간다. **진행 방향 입력이면 예산이 0** → W를 눌러도 공중 가속이 없다. **이 비대칭이 기술의 전부.**
- **확장 지점 = `CalcVelocity()`의 `IsFalling()` 분기**(기존 슬라이드 분기와 같은 자리·같은 방식) + `GetFallingLateralAcceleration()` 오버라이드. 낙하 진입 시 **Z는 이미 0**이고 중력은 뒤에 따로 적용돼 횡성분만 다루면 된다.
  - ⚠️ `GetFallingLateralAcceleration()`이 넘기는 벡터는 **방향 전달용만이 아니다** — 벽 충돌 후 `LimitAirControl()`이 그 **크기를 재가속 값으로 그대로 쓴다**(엔진 5033·5095). 원시 `Acceleration`(=MaxAcceleration 2048)을 넘기면 벽 스치기 후 가속이 2.5배로 튄다 → 우리 값(`AirStrafeAcceleration`)을 곱해 넘긴다. (Codex 지적)
  - ⚠️ `CalcVelocity`는 **한 프레임에 여러 번** 불린다(서브스텝 + 충돌 후 재호출, 엔진 5018·5024). `AddSpeed`가 그 방향 총속도를 묶어 **반복 호출에 수렴**한다 — 프레임당 1회를 가정하는 코드 금지.
- **수치**(전부 `FPSR|Movement|Air`, EditDefaultsOnly): `AirStrafeWishSpeed` **300** · `AirStrafeAcceleration` 2000 · `AirStrafeMaxSpeed` 1400.
  - `AirStrafeWishSpeed` = **난이도 다이얼**(사용자 결정 = **대중적 난이도**). 곧 "마우스 안 돌리고 방향키만 유지했을 때 도는 각도"다 — 속도 600 기준 120≈11°(정통 Quake) / **300≈30°(채택)** / 400≈42°(거의 자동). 더 돌리려면 마우스를 같이 돌리면 된다 — **잘하면 더 잘 되지만 못해도 된다.**
  - Quake 원식은 가속 항이 `wishspeed`에 묶여 있으나 **절대값(cm/s²)으로 분리**했다 — 원식대로면 WishSpeed를 만질 때마다 가속 반응성이 같이 흔들려 두 다이얼이 독립 튜닝되지 않는다.
- **`WallJumpMaxSpeed` 1200 → 1400**(사용자 결정) → `SlideMaxSpeed`·`AirStrafeMaxSpeed`와 통일 = **최고 이동속도 상한이 1400 하나**. ⚠️ C++ 기본값이라 **BP에 오버라이드가 있으면 BP가 이긴다** — PIE에서 실제 1400 확인 필요.
- **상한 규칙**: `Ceiling = max(현재 횡속도, AirStrafeMaxSpeed)` — 들고 온 속도를 **클램프가 깎지 않는다**. 단 **진행 방향과 반대되는 입력은 깎는다**(벡터 덧셈이므로) — 그게 공중 브레이크다. "저절로 사라지지 않고 플레이어만 버릴 수 있다"가 정확한 표현(Codex가 초안의 과장 문구를 잡아냄).
- **부수 효과(의도)**: 공중에서 **`MaxWalkSpeed` 상한이 사라진다**(낙하 중 `GetMaxSpeed()` 미사용) — 이 기능의 전제. 덕분에 `GetMaxSpeed()`의 **뒷걸음 감속 공중 누수**(지상 게이트 누락, 공중 후진 450)도 공중에선 무의미해졌다(지상은 그대로). **엔진 공중 노브 3개 무동작**: `AirControl`·`AirControlBoostMultiplier`·`AirControlBoostVelocityThreshold`(디테일 패널엔 계속 보임).
- **예측**: `Acceleration`·`Velocity`·상수만 읽음 → **`FSavedMove_FPSR` 추가 상태 0개**.
- **검증**: 빌드 `Result: Succeeded`×2(UHT `-WarningsAsErrors` 포함) + `git diff` 재검토(2파일, 의도 외 변경 0). Codex 게이트 플랜 2회·diff 1회(결함 5건 반영). **✅ PIE 사용자 확인 완료(2026-08-10)** — `WallJumpMaxSpeed` 1400 BP 오버라이드 건 포함해 이상 없음. 보드 행 완료 처리(커밋 `6c81c332`).

## 🔪 근접 전용 3번 슬롯 + 무기별 이동속도 = ✅코드 완료 / ⏳콘텐츠·PIE 대기 (2026-07-29, `refactor/character`)
> 사용자 요청 = "근접무기를 다른 무기와 섞지 말고 3번 슬롯 전용으로. 근접무기가 없어도 3번을 누르면 맨손 상태로 클릭 가능하고, 이동속도를 더 준다(600→700, 슬라이드 900→1000)."
> 플랜 = Codex 적대 검토 4라운드 수렴본(원문 `Docs/Review/_raw/20260729-09*-melee-slot3*.md`). 설계 근거는 `Docs/SSOT/CombatWeaponCard.md §2-4` · `Docs/SSOT/PlayerFeel.md §2-13` · ADR 0001 "걷기 속도 상한의 단일 기록자" 절.

- **핵심 판단 ① 맨손 = 빈 슬롯이 아니라 무기 DataAsset**(`DA_Weapon_Unarmed`, Archetype=Melee, 메시 없음). 슬롯이 애초에 안 비므로 `EquipSlot`·`RefreshEquippedAbility`·`FireOneShot`·`RefreshFirstPersonWeaponVisual`에 "무기 없음" 분기가 **한 줄도 안 들어갔다**. 공격은 기존 `UFPSRGA_WeaponMelee` 재사용.
- **핵심 판단 ② 슬롯 규칙 = 데이터**. `FFPSRWeaponSlotDefinition{AcceptedArchetypes, DefaultWeapon}` 배열(`SlotDefinitions`, 기본 3칸·마지막 칸이 `Melee` 전용 선언). **명시 목록=전역 전용 / 빈 목록=나머지 담당 / 슬롯 순서 무관.** `EWeaponSlotKind` enum 대안은 기각(아키타입→kind 매핑=새 아키타입마다 중앙 switch 수정).
- **핵심 판단 ③ 슬라이드 값은 저작하지 않는다**. 진입 임펄스가 `min(max(V, min(V×1.5, SlideMaxEntrySpeed)), SlideMaxSpeed)`로 **그 순간 속도 V에서 파생** → `SlideMaxEntrySpeed`를 **900→1000 한 값만** 올리면 걷기 600=900(무변화)·700=1000. 무기 DA엔 `WalkSpeed` 하나만 둔다. 부수효과로 "3번 누르자마자 앉으면 가끔 슬라이드가 덜 나감"(장착 복제 타이밍 의존) 버그 클래스가 원천 소멸.
  - ⚠️ **영향 구간 = V ∈ (600, 1000)**: 속도 카드·내리막 모멘텀·벽점프 착지로 이 구간에서 슬라이드하면 진입속도가 오른다(V=650이면 900→975). 되돌리려면 `SlideMaxEntrySpeed` 한 값만 900으로.
- **`MaxWalkSpeed` 기록자 4개 → 1개**(`UFPSRCharacterMovementComponent::RefreshWalkSpeedCap`). 층 = 저작 기본값(캐릭터 `BaseWalkSpeed`, **프로퍼티는 안 옮김** — BP값·hip sway 기준·GE 참조 보존) × 장비 `WalkSpeed`(0=기본) × 카드 `MoveSpeedMultiplier` × 다운(`DownedWalkSpeed`). **기존 버그 동반 수정**: DBNO 중 속도 카드가 들어오면 `MaxWalkSpeed`가 0에서 되살아나 쓰러진 플레이어가 움직이던 문제.
- **장비 속도는 예측 안 함**(서버 `EquipSlot` + 클라 `OnRep_CurrentSlotIndex`/`OnRep_Slots`에서만). 클라 선적용은 서버가 장착을 거절했을 때(프리즈/다운) 아무것도 복제되지 않아 클라가 빠른 속도에 고착되는 구멍이 있다. 남는 건 걷기 상한의 1 RTT 지연뿐이고, 상한은 가속 램프가 접근하는 값이라 체감 없음으로 판정.
- **막은 함정 4건**(Codex 검토 산물): ①`ServerSeedDefaultSlots`는 **null 칸만** 채움(멱등) — 재빙의/seamless travel이 칼을 맨손으로 되돌리지 못한다 ②`SetSlotWeapon` 단일 지점에서 `RemoveReplicatedSubObject` → 교체된 맨손 인스턴스가 등록 목록에 안 남는다 ③무기 해금 카드 후보를 **아키타입별로** 판정(`HasFreeSlotFor`) — 원거리 칸만 꽉 찬 상태에서 라이플 카드가 떠서 뽑아도 `AddWeapon`이 `INDEX_NONE`을 돌려 카드가 증발하던 경로 ④`FPSRLoadoutPoolValidator`가 맨손(`bExcludeFromProgression`)의 로비 시작무기 등재를 에러 처리.
- **구현 중 추가 발견**: 3번(맨손)을 **든 채로** 근접 해금 카드를 먹으면 슬롯 번호가 안 바뀌어 `EquipSlot`이 early-return → 발사 GA·1P 메시·이동속도·반동 프로필이 맨손인 채 남는다. `SetSlotWeapon`에 "장착 중인 칸의 내용물 교체" 분기 추가.
- **검증**: 빌드 `Result: Succeeded`×3(UHT `-WarningsAsErrors` 포함, 링크까지) + `git diff` 재검토(13파일, 의도 외 변경 0, 디버그 로그 0 — 추가된 `UE_LOG`는 슬롯 수 오저작용 에러 1건).
- **✅ 콘텐츠 완료(헤드리스 커맨드렛, 에디터 미기동)**: ①**`DA_Weapon_Unarmed` 신규**(`/Game/Weapons/DataTable/`, Archetype=Melee / FireAbility=`FPSRGA_WeaponMelee`(칼과 동일) / 메시 없음 / Damage 25·MeleeRadius 130·MeleeAttackDelay 0.6 = 칼(90/175/0.5)보다 약하고 느리게 / FireRate 8(검증 통과용 무의미값) / `WalkSpeed=700` / `bExcludeFromProgression=true`) ②`DA_Weapon_Knife.WalkSpeed=700` ③`BP_FPSRPlayer` → `WeaponInventory.SlotDefinitions[2].DefaultWeapon = DA_Weapon_Unarmed`.
  - **사전 확인 2건 통과**: BP `SlideEnterSpeedMultiplier=1.5`(슬라이드 표 유효) / BP CharacterMovement `MaxWalkSpeed=600`으로 `BaseWalkSpeed`와 동일 → **이관할 BP 오버라이드 없음**.
  - **검증**: `Scripts/validate-data.ps1` = **0 error**(36 warning은 전부 기존 "anchor 도달 불가", 칼도 동일) + 에디터 검증기 직접 호출로 `DA_Weapon_Unarmed`·`DA_Weapon_Knife`·`DA_LoadoutPool`·`BP_FPSRPlayer`·`WeaponInventory 컴포넌트` 전부 `VALID`(신규 슬롯 검증 규칙 오탐 0).
  - **함정 1건**: `FFPSRWeaponSlotDefinition` 멤버를 `EditDefaultsOnly`로 두면 파이썬이 루즈 struct를 "인스턴스"로 보고 `DisableEditOnInstance` 필드 쓰기를 거부한다 → 멤버만 `EditAnywhere`로(컨테이너 `SlotDefinitions`가 `EditDefaultsOnly`라 편집 가능 범위는 불변).
- **⏳ 남은 콘텐츠 = 맨손 손맛 세트**(실제 에셋 필요, 코드 0): 1P 팔 공격 몽타주(`FireMontage`) + 휘두름/히트 사운드(`FireSound`) + 명중 VFX. **현재 칼도 이 3개가 전부 비어 있다** — 즉 근접 공격은 지금 데미지만 들어가고 화면·소리 반응이 0이라 "무기 없음 버그"로 읽힌다.
- **⏳ PIE(사용자, 2인 `L_Lobby`)**: 3번=팔만 보임 / 맨손 클릭 공격 / `SPEED` 700 / 슬라이드 피크 1000 / 1번 복귀 600·900 / 3번 든 채 속도카드=700×배수 / DBNO 완전정지 / 근접 해금이 맨손만 대체 / 원거리 2칸 찬 뒤 원거리 카드 미출현 / 클라 창 3번 연타 러버밴딩 없음.
- **후속 인입 후보**: HUD 3번 슬롯 아이콘(주먹/칼 — 없으면 빈 칸인지 맨손인지 안 읽힘) · 맨손 vs 칼 속도 차등(밸런스: 맨손이 제일 빠르면 "총 버리고 도망"이 최적해가 될 수 있음) · 필요 시 엄격 예측.

## 🧱 플레이어 이동 구조 재설계 = ✅ADR 0001 확정 + 대시 폐기 (2026-07-28, `refactor/character`)
> 구조 재설계 트랙의 **첫 결정**. 참조 = 사용자 제공 Apex 사격훈련장 영상(달리기→슬라이드→벽 매달리기/등반→공중→사격→ADS→장전)을 60fps 프레임·탄약 카운터로 실측 분해. 결정 기록 = **`Docs/Architecture/0001-player-movement-state-ownership.md`**(이 리포 첫 ADR, 인덱스 `Docs/Architecture/README.md`).
- **채택**: 이동 상태의 주인 = **신규 `UFPSRCharacterMovementComponent`**(아직 미구현). GAS 는 쿨다운·무적·카드 수치만. 사격/애님BP/HUD 는 `CanFireInCurrentState()`·`GetSpreadMultiplier()`·`IsSliding()`·`IsOnWall()` 로 **질의만** 한다.
- **불변식 9개** 확정(문서 §불변식). 핵심 = ①상태 주인 하나 ②클라 예측+서버 확인(서버 응답 대기형 이동 금지) ③수치는 GAS 어트리뷰트로 복제 ④사격은 상태를 모름 ⑤확산 주인은 heat 하나 ⑥애니메이션이 상태를 소유하지 않음 ⑦탈출 불가 상태 금지 ⑧프리즈/DBNO는 진행 중인 이동도 정지 ⑨수치는 데이터에.
- **기각**: GAS 어빌리티가 상태 전이 결정(서버 보정 되감기 때 어빌리티는 안 되감겨 상태 주인이 둘로 갈라짐) · 현행 대시식 서버 RPC 확장(ping 46ms 러버밴딩).
- **비목표**(사용자 확정): 월런 없음 · 움직이는 벽 매달리기 없음(바로 떨어짐) · **카드 확장은 수치·조건까지만**(새 이동 상태 추가 금지 — 이단점프/공중대시는 전부 수치·조건 범위).
- **미결(구현 시점 연기)**: 슬라이드를 걷기모드 확장 vs `MOVE_Custom` — `저렴` 등급이고 외부 인터페이스가 축과 무관해 나중에 뒤집어도 파급 0.
- **⚠️ 대시 폐기 완료**(사용자 결정 "나중에 불변식에 맞춰 재제작"): C++ 전용 코드 전체 + `Ability.Movement.Dash` 태그(참조 0건) 제거. **`RefreshPawnCollisionResponse` 는 함수 유지·`bDashing` 항만 제거** — `bGrace`(부활/프리즈 무적)·`bDowned`(DBNO 통과)가 계속 쓰므로 함수째 지웠다면 둘 다 깨졌음. `HandleRunStateChanged_Movement` 도 함수·`StopMovementImmediately()` 유지(낙하 중 프리즈 + 향후 슬라이드). `IA_Dash`+`IMC_Default` 키 매핑은 **유지**(재사용 예정, 현재 무동작). 기획은 유효 = `Docs/SSOT/PlayerFeel.md §2-13` 에 폐기사유·재제작 방침 기록.
- **검증**: 빌드 `Result: Succeeded`(FPSRogueliteEditor Win64 Development, 링크까지) + `git diff` 재검토(변경 9파일, 의도 외 변경 0). **⏳ 남은 = PIE 사용자 확인 — 부활 후 무적/DBNO 통과가 그대로 동작하는지**(대시 제거가 공유 헬퍼를 건드렸으므로).
- **✅ 1·2단계 구현 완료(2026-07-28)** = 신규 **`UFPSRCharacterMovementComponent`**(예측 배관 `FSavedMove_FPSR` + 앉기 + **슬라이드**) + 캐릭터 배선. **축 1 해소 = 혼합**(슬라이드는 `MOVE_Walking` 유지·속도/마찰 훅만 오버라이드 / 벽 매달리기는 예정대로 `MOVE_Custom`) — `PhysCustom()`이 빈 함수라 커스텀 모드로 가면 바닥추종·계단·경사·벽미끄러짐을 전부 재구현해야 하기 때문.
  - **커스텀 네트워크 슬롯 0개**(ADR 예상 1개에서 정정): 진입 의도 = 엔진 `bWantsToCrouch` 재사용, 파생 상태 5종은 `FSavedMove_FPSR` **로컬 재생용**(전송 0) → 스톡 CMC 대비 대역폭 증가 없음.
  - **엔진 함정 3건 발견·처리**: ①`CanCrouchInCurrentState()`가 `IsFalling()` 허용 → 공중 앉기/슬라이드 됨(지면 전용으로 좁힘, 부수효과로 앉은 채 점프 시 자동 기립) ②`PhysWalking`이 `GroundFriction`(8)×`BrakingFrictionFactor`(2)=**실효 16**을 브레이킹에 적용 → 900→250이 **0.08초**에 끝나 슬라이드가 순간정지로 보임(슬라이드 마찰 0으로 대체=등감속 경로) ③`CanAttemptJump()`의 `!bWantsToCrouch`가 **슬라이드 중 점프를 원천 차단**(크라우치 항만 제외).
  - **슬라이드 사양**: 진입=달리기+앉기입력+최소속도 450(×1.5 부스트) / 종료=키해제·250미달·1.6s상한·공중이탈·프리즈·DBNO / **모든 종료에 쿨다운 0.8s**(무한 슬라이드 방지) / 슬라이드 중 **사격 가능**(확산×1.3)·**점프 취소 가능**(속도 유지).
  - **속도 곡선 = 절대값**(사용자 결정): `/Game/Config/Character/Curve_GroundSpeed`(X=초,Y=0→600) · `Curve_SlideSpeed`(X=초,Y=900→300, **진입속도로 상한**). 헤드리스 생성(CSV 임포트 경로 — `UCurveFloat::FloatCurve`는 파이썬 미노출) + 값 읽어서 검증. 카드로 MaxWalkSpeed 상승 시 `SpeedCurveReferenceSpeed` 대비 자동 스케일. **미할당 시 등가속/등감속 폴백**.
  - **디버그**: 우측 상단 `SPEED`/`STATE`(+슬라이드 쿨다운) — 엔진 DebugDrawService(좌상단 `AddOnScreenDebugMessage`는 기존 HP/런 디버그가 점유). 토글 `FPSR.Movement.Debug 0`.
  - **검증**: 빌드 `Result: Succeeded`(UHT `-WarningsAsErrors` 포함) + 커브 값 읽기 대조(`CURVE OK`×2). ⏳ **PIE = 사용자**(예측 정합성은 원리상 헤드리스로 검증 불가 — 2-client 필요). 작업 맵 = **`Content/Maps/TestWorld.umap`**(리팩토링 기간 작업 맵, 사용자 결정).
- **✅ 2단계 후속 전부 완료(2026-07-28, 사용자 PIE 확인 완료)** — 아래 ⑨ 핸드오프 참조.
- **✅ 3단계 = 벽 매달리기 코드 완료(2026-07-28)** — `CMOVE_WallHang`(`MOVE_Custom`) + `PhysCustom()` 직접 구현. **상태 1개·출구 3개**(W 유지→등반 / W 해제→미끄러진 뒤 낙하 / 점프→벽 법선 방향으로 튕김) + 안전 출구 4개(벽 소실·시간 상한·프리즈/DBNO·바닥 착지). `IsOnWall()`은 **무브먼트 모드에서 파생**(별도 복제 bool 없음 — 엔진이 이미 이동 패킷에 싣고 보정 시 복원). 빌드 `Result: Succeeded`. **✅ PIE 사용자 확인 완료(2026-07-29)** — 진입 모멘텀·조준 벽점프·자세 전환 블렌딩까지 전부 이상 없음. 아래 ⑩ 핸드오프 참조.
- **다음** = GAS 어트리뷰트 연결(카드→이동 수치, 불변식 3) / `GetSpreadMultiplier()`의 heat 시스템 소비 배선 / 대시 재제작. **무기 DataAsset을 `Content/Config/Weapon/`으로 이동 = 무기 작업 착수 시점에**(사용자 결정 2026-07-28, 에디터에서 이동+Fix Up Redirectors 필요).

## ⌨️ 키 바인딩 2개씩 + 재바인딩 배관 = ✅완료 (2026-07-29, `refactor/character`)
> 사용자 요청 = "각 액션에 키 2개씩(예: 점프 Space + 마우스 휠 위), **추후 설정에서 재바인딩도 필요**".
> 사용자 결정 = **배관만** 이번에 (실제 2번 키·설정 UI는 나중).
- **코드 변경 0건.** 한 액션에 키 여러 개 = Enhanced Input 기본 기능이고, 재바인딩도 엔진
  `UEnhancedInputUserSettings`(UE 5.3+)가 전부 제공한다(`MapPlayerKey`/`SaveSettings` 전부 BP 호출 가능 →
  **설정 UI는 UMG만으로** 가능). 커스텀 시스템 만들 필요 없음.
- **핵심 발견**: **1번키/2번키 슬롯은 IMC 행 순서로 자동 배정**된다(같은 매핑 이름이 나올 때마다 슬롯 +1 —
  `EnhancedInputUserSettings.cpp` 1708·1747·1762행). 단 **매핑 이름(`PlayerMappableKeySettings`)이 없는 행은
  등록에서 통째로 스킵**(1680행, `IsPlayerMappable()`=설정객체 유무). 프로젝트엔 이름이 0개였고
  `bEnableUserSettings`도 꺼져 있어 **재바인딩 시스템이 아예 안 돌고 있었다.**
- **한 일**: ① `Config/DefaultInput.ini` 에 `[/Script/EnhancedInput.EnhancedInputDeveloperSettings] bEnableUserSettings=True`
  ② **버튼 액션 9종은 IA 에셋에** 이름 부여(`Jump`/`Crouch`/`Fire`/`ADS`/`Reload`/`EquipSlot1~3`/`Menu`) —
  그 액션의 모든 IMC 행이 물려받아 행 순서대로 슬롯이 된다 ③ **이동(축) 4행은 IMC 행마다** 따로
  (`MoveForward`/`MoveBackward`/`MoveLeft`/`MoveRight`) — W/S가 같은 `IA_MoveForward`+Negate라 IA에 붙이면
  **S가 "앞으로 가기의 2번 키"로 잡힌다**. ④ `IA_Look`(시점=재바인딩 대상 아님)·`IA_Dash`(폐기, 무동작) 제외.
- **⚠️ `Name`은 세이브 키다** — 배포 후 바꾸면 플레이어 저장 바인딩이 고아가 된다. 표시명은 나중에 바꿔도 안전.
- **검증(헤드리스, 슬롯까지 실측)**: 16행 중 **14행 매핑 가능**(Look·Dash만 제외), 이름 13종,
  **`Jump`가 슬롯 2개** — 사용자가 에디터에서 넣어둔 휠 행이 자동으로 슬롯 2가 되면서 **배관 전체가 끝까지 증명**됐다.
- **⚠️ 남은 것 = 그 휠 행의 키 종류**: `MouseWheelAxis`(축 키)로 들어가 있다. `IA_Jump`는 Boolean이고
  `FInputActionValue::IsNonZero()`가 크기만 보므로 **휠을 아래로 굴려도 점프한다**. "휠 위"만 원하면
  `MouseScrollUp`으로 바꿔야 한다(1행). **사용자에게 보고함 — 이번엔 그대로 유지**(바꾸려면 그 한 행만).
- 저장 위치 = 엔진 기본 `Saved/SaveGames/`(SaveGame). 기존 `UFPSRGameUserSettings`(오디오·크로스헤어,
  `GameUserSettings.ini`)와 **별도 경로** — 합치지 않음(이득 없음).
- **✅ PIE 사용자 확인 완료(2026-07-29)** — 기존 조작 전부 이상 없음.
- **✅ 보드 행 완료 처리(2026-08-10)** — 남아 있던 "휠 행 키 종류" 결정은 **`MouseWheelAxis` 그대로 유지**로 종결(사용자). 커밋 `0353cd4a`.

## ⑩ 핸드오프 (2026-07-29, `refactor/character`) — **벽 매달리기(3단계) + 자세 전환 블렌딩 · ✅PIE 확인 완료**

> 빌드 `Result: Succeeded`(UHT `-WarningsAsErrors` 포함, 링크까지). **✅ PIE 사용자 확인 완료(2026-07-29) — 이상 없음. 전부 커밋·푸시됨.**
> **다음 = GAS 어트리뷰트 연결(카드→이동 수치, 불변식 3) / `GetSpreadMultiplier()` heat 소비 배선 / 대시 재제작 / 애니메이션 배선.**
> ⚠️ 애니메이션 배선 시 결정 필요 = 원격 플레이어의 `IsSliding()`/`GetSlideBlend()` 신뢰 불가(슬라이드 미복제 + `bWantsToCrouch` 부재).
> ✅ 벽 매달리기 진입 = **공중 전용이 사양**(사용자 확정 2026-07-29). 체공 상태에서만 붙는다 — 지상 진입은 열지 않는다(벽 보고 W만 눌러도 붙는 문제 회피 + "점프해야 붙는다"로 규칙이 하나로 끝남). ADR §"진입은 공중 전용이 사양이다" 참조.

### 구현 요약
- **`CMOVE_WallHang`**(`EFPSRCustomMovementMode`) + `PhysCustom()` = 엔진 `PhysFlying` 구조를 그대로 따름(중력 없음·`CalcVelocity` 미호출 → 입력이 벽에서 밀어내지 못함). `IsOnWall()`은 **무브먼트 모드에서 파생**.
- **새 예측 상태 4종**(`FSavedMove_FPSR`, 전부 로컬 재생용 = **전송 0**): `WallHangElapsed` · `WallSlipElapsed` · `bWallHangConsumed` · `WallNormal`. `CanCombineWith`는 **매달리는 동안 무브 조합을 아예 금지**(엔진 `FSavedMove_Character::CanCombineWith`가 packed movement mode를 비교하지 않음 — 엔진 소스 확인).
- **트레이스 = 프레임당 1회**. 진입 프로브는 **가슴 높이**(무릎 높이 상자를 안 잡게), 유지 프로브는 **발 근처**(발이 벽 꼭대기를 넘는 순간 미스 = 등반 완료 판정이 공짜, 벽 파괴도 같은 미스 = **불변식 7 자동 충족**).
- **턱 넘김 부스트**(`WallTopBoostForward/Up`) = 등반 중 벽을 잃으면 앞·위로 가속. **없으면 기능이 성립 안 함**(다 올라가도 캡슐이 벽 위로 못 올라타 도로 떨어짐). 사용자 승인 후 추가.
- **재부착 = 공중 체공 1회당 1번**(`bWallHangConsumed`, 착지 시 해제). 쿨다운만 두면 "등반 1.5s(≈390cm) → 쿨다운 0.5s 낙하(≈120cm)"가 순증이라 임의 높이 벽을 무한 사다리로 오를 수 있다.
- **✅ 자세 전환 블렌딩(2026-07-29, 2차 PIE 피드백)** — 앉기↔슬라이드↔서기가 "뚝뚝 끊긴다" → **카메라 눈높이 · 걷기 속도 상한 · 애님BP용 블렌드 값**이 **하나의 시계**(`StanceBlendDuration` 0.15s)로 움직인다. **상태 전환 자체(캡슐·`bIsCrouched`·슬라이드 진입)는 즉시 유지**(사용자 결정 — 조작 반응 희생 없음).
  - **주인 = 무브먼트 컴포넌트**: `GetStanceBlend()`(0서기↔1앉기) · `GetSlideBlend()` · `GetStanceTransitionProgress()`. 갱신은 `UpdateCharacterStateBeforeMovement` 한 곳 — 엔진이 `PerformMovement`(소유클라·서버) **와 `SimulateMovement`(원격 프록시) 양쪽에서** 부르므로 모든 역할이 커버된다.
  - **등속 블렌딩**(`FInterpConstantTo`): 완전 전환 = 정확히 Duration, **중간에 뒤집으면 간 거리만큼만** 되돌아온다(0.05s 끊으면 복귀도 0.05s). 시간 고정이면 연타 시 흐물거린다.
  - **카메라 = "직전 프레임 위치를 붙잡고 이완"**. `StanceBlend`로 눈높이를 직접 계산하지 **않는** 이유 = 캡슐 리사이즈 기준점이 상황마다 다르다(지상=발 고정 / 공중=중심 고정 / **원격 프록시는 캡슐이 아예 안 움직임** — 엔진에서 그 `MoveComponent`가 `!bClientSimulation` 안). 튄 거리를 **재서** 흡수하면 원인과 무관하게 맞고 DBNO 관전에서도 맞다. 진행도만 `StanceBlend`에서 받는다 → 셋이 같은 프레임에 끝난다.
  - **속도**: 앉을 때 600→300이 **0.03초(2프레임)** 였다(실효 마찰 8×2=16 + 감속 2048). 이제 0.15초. 단순히 `Lerp(MaxWalkSpeed, MaxWalkSpeedCrouched, StanceBlend)`로 하면 **`FindCurveTimeForSpeed`의 정규화 기준이 같이 흔들려 일어설 때 300→150으로 오히려 더 떨어진다** → 커브 로직은 그대로 두고 **결과값을 이완**(`StanceSpeedFrom`). 일어설 때 기존 커브 램프(300→600 이어서 가속)가 그대로 살아있다.
  - **`AFPSRCharacter` 액터 틱 상시화**(종전 디버그 빌드 전용) + `AddTickPrerequisiteComponent(무브먼트)` — 카메라 참조 위치가 한 무브먼트 스텝 낡지 않게. 플레이어 폰 4개라 적 200~300 예산과 무관.
  - **예측**: `StanceBlend`가 속도 상한에 쓰이므로 `FSavedMove_FPSR`에 저장(+`StanceBlendStart`·`StanceSpeedFrom`·`SlideBlend`). **전환 중 무브 조합 금지** — 엔진의 `MaxSpeedThresholdCombine`(10)이 60fps에선 우연히 막아주지만(프레임당 33cm/s) 고프레임에선 통과한다(Codex 지적).
  - ⚠️ **원격 플레이어의 `IsSliding()`/`GetSlideBlend()`는 신뢰 불가** — 슬라이드 상태가 복제되지 않고 `bWantsToCrouch`가 프록시에 없다. **애니메이션 배선 시점에 결정 필요**(복제 비트 / 커스텀 플래그 / 애님BP 추정). `GetStanceBlend()`는 복제되는 `bIsCrouched` 기반이라 프록시에서도 정확.
- **진입 모멘텀 + 조준되는 벽점프**(1차 PIE 피드백 반영, 사용자 결정): ①붙는 순간 속도를 `WallEntryMomentumDuration`(0.25s) 동안 살린다 — 벽면을 따라 **실제로 미끄러지고**(진입 속도의 벽면 성분), 그 안에 점프하면 **진입 속도 전체 크기**가 벽점프에 얹힌다(정면 충돌도 보상됨, `WallJumpMaxSpeed` 1200으로 상한). 종전엔 진입 즉시 속도를 0으로 지워 `WallLateralSpeed`(120)+`WallStickSpeed`(60)=**정확히 134**로 죽었다. ②벽점프 방향 = **시선 + 벽 법선 반반**(`WallJumpAimBlend` 0.5, 0=벽수직·1=시선). 벽을 마주본 채 점프해도 다시 처박히지 않도록 `WallJumpMinOutward`(0.35)로 **바깥 성분 최소 보장**. ③`WallJumpUpSpeed`(기본 0=일반 점프 높이) 추가. 디버그 STATE에 남은 모멘텀 `+숫자` 표시.

### 엔진 함정 / 결정 (전부 근거 있음)
1. **`CanAttemptJump()`가 `IsMovingOnGround() || IsFalling()`** — 매달리기는 둘 다 아니라 **벽점프가 원천 봉쇄**. `|| IsOnWall()` 추가.
2. **`JumpIsAllowedInternal()`의 점프 예산** — `JumpMaxCount=1`이면 점프해서 벽에 붙는 순간 예산이 0이라 벽점프 불가. 벽 진입 시 `JumpCurrentCount = Min(현재, MaxCount-1)`로 **딱 한 번치만 환급**(0으로 리셋하면 이단점프 카드에서 공중 점프가 순증 — Codex 지적). ⚠️ 환급이 **진입 시점**인 이유 = `CheckJumpInput`이 `DoJump` **전에** `CanJump()`(예산)를 본다. 부작용 = 벽점프 안 하고 미끄러져도 점프 1회가 남는다(공중 1회로 상한).
3. **`ProcessLanded()`를 직접 부르면 안 된다** — `SetPostLandedPhysics`가 `if (IsFalling())`로 감싸여 있어 `MOVE_Custom`에서 부르면 **모드 전환이 통째로 스킵**돼 벽 모드에 갇힌다. 바닥 착지는 `MOVE_Falling`으로 넘겨 다음 틱의 `PhysFalling`이 정상 착지시킨다(`Landed` 알림만 1프레임 늦음).
4. **`SlideAlongSurface`가 `FHitResult&`를 덮어쓴다** — 두 번째 스윕 결과로 바뀌므로 바닥 판정은 **호출 전에** 읽어야 한다(아니면 아래로 미끄러져 바닥에 닿는 걸 놓친다).
5. **벽이 사라진 프레임의 점프** — 재프로브 실패 시 `Super::DoJump`로 흘려보내면 "없는 벽에서 수직 점프"가 승인된다. `StopWallHang(); return false;`로 그 프레임엔 점프를 만들지 않는다(Codex 지적).

### 남은 관찰 사항
- ✅ **진입 = 공중 전용이 사양으로 확정**(2026-07-29). 지상/슬라이드 진입은 열지 않는다.
- 프리즈(카드 선택) 중 벽에 매달려 있으면 **손을 놓고 떨어진다**(공중에 있던 플레이어가 프리즈 중에도 중력으로 떨어지는 기존 동작과 동일). 벽에 **핀으로 고정**해두는 쪽이 나으면 별도 처리가 필요하다.
- 가슴 프로브는 맞고 발 프로브는 빗나가는 지형(가슴 높이 난간)은 붙자마자 턱 넘김으로 넘어간다 — 결과가 "난간을 타 넘음"이라 자연스럽긴 하나 의도한 건 아니다.
- 수치 16개(`FPSR|Movement|Wall`)는 **뼈대 값**이다. 감각 조정은 BP 디폴트에서.

## ⑨ 핸드오프 (2026-07-28, `refactor/character`) — 슬라이드/앉기/점프 완료

> 컨텍스트 소진으로 세션 인계. **코드+콘텐츠 모두 커밋·푸시됨**(커브·BP·맵은 이번 작업 산출물이라 동반 커밋 — 사용자 승인). 빌드 `Result: Succeeded`, PIE 사용자 확인 완료.

### 이번 세션에서 한 일
**엔진 함정 6건 발견·수정**(전부 PIE 없이는 안 보이는 것들):
1. `ACharacter::CanJumpInternal_Implementation` = `!IsCrouched() && JumpIsAllowedInternal()` — 크라우치 차단이 `CanAttemptJump` 말고 **여기에도** 있어 앉기/슬라이드 중 점프가 원천 불가. **두 곳 다** 풀어야 했다.
2. 슬라이드 중 `GetMaxAcceleration()=0` 으로 막았더니 `Acceleration = GetMaxAcceleration() * InputVector` 라 **입력 방향 벡터까지 0** → WASD 조향이 무시되고 시선만 따라감. 가속 차단 위치를 `CalcVelocity`(Super 미호출)로 옮겨 해결.
3. `PhysWalking` 이 `GroundFriction`(8)×`BrakingFrictionFactor`(2)=**실효 16** 을 브레이킹에 넘겨 900→250이 **0.08초** — 슬라이드가 순간정지로 보임.
4. `CanCrouchInCurrentState()` 가 `IsFalling()` 도 허용 → 공중 앉기/슬라이드 가능했음.
5. `SlideMaxDuration`(1.6s)이 커브 길이보다 짧아 커브 하강 구간이 **실행된 적 없음**. 종료 후 크라우치 걷기 상한(`MaxWalkSpeedCrouched`=300)이 "커브가 300으로 급락"처럼 보였다. → 커브 있으면 **커브 길이 = 지속시간**.
6. 진입 부스트 상한이 `SlideMaxSpeed`(1400) 하나뿐이라 **점프-슬라이드 반복으로 900→1350→1400 계단식 누적**. → `SlideMaxEntrySpeed`(900) 분리, 단 "올려주기만 하고 깎지 않음"(내리막 관성 보존).

**기능**: 슬라이드 조향(WASD / 무입력 시 시선 추종, `SlideTurnRateDegrees`) · **경사 슬라이드**(법선 수평성분=내리막방향·크기=sin θ → 내리막 가속 + 커브 시간 지연, 오르막 반대) · 뒷걸음 감속 0.75(걷기=입력방향 / 슬라이드=**헤딩** 기준) · 앉기 속도 커브 + **자세 전환 시 커브 시간 재매핑**(300→600 이어서 가속) · `MaxFallSpeed`(기본 0=비활성) · `AirControl` 0.05→**0.4**.

**구조**: 커브를 **정규화(0~1)로 전환** — `GetSpeedCurveScale()`·기준속도 중복·빠른진입 분기가 전부 소멸. 곱하는 기준 = 서기 `MaxWalkSpeed` / 앉기 `MaxWalkSpeedCrouched` / 슬라이드 **진입속도**. 무브먼트 프로퍼티 전부 **`FPSR|Movement|{Ground,Slide,Air,Spread}`** 로 묶음.

### 주의 / 블로커 (⑩에도 그대로 유효)
- **Live Coding 락**: 에디터가 켜져 있으면 빌드가 `Unable to build while Live Coding is active` 로 실패한다(코드 문제 아님). 빌드 전 에디터 종료.
- **디버그 = `SPEED`/`STATE` 2줄**(우측 상단). ⑩에서 사용자 지시대로 `JUMP`/`GATE` 2줄 삭제 완료. 토글 `FPSR.Movement.Debug 0`.
- 커브 생성 시 **시작·끝 2키만** 만들 것(사용자 지시). 중간 키를 채우면 편집 불가 상태가 된다.
- `MaxFallSpeed` 는 기본 0(비활성) 유지 — 사용자가 값 미정.
- **미완**: 불변식 3(GAS 어트리뷰트로 이동 수치 변경) · `GetSpreadMultiplier()` 를 heat 시스템이 실제 소비하는 배선(현재 값만 제공, 소비자 없음) · 대시 재제작.

## 🔀 브랜치 전면 통합 = ✅완료 (2026-07-27) — 구조 재설계 착수
> **사용자 결정**: 가장 기초 구조부터 다시 설계한다(**플레이어 캐릭터**부터). 흩어져 있던 작업 브랜치를 전부 `main` 으로 모으고, 재설계 브랜치 `refactor/character` 를 분기한다.
> - 통합(`--no-ff`, 4건) = `feat/neonv-fp-arms`(NEON-V 1인칭 팔·LPAMG 팩 제거) · `tooling/vibeue-proxy` · `phase/director-p0a0`(`fix/dbno-ff-hardening` 포함) · `phase/u22a-environment`(U22a 환경·CityGen 툴). `pilot/s1-cybercity-sector` 는 이미 main 에 포함돼 있어 제외.
> - **아래 항목들의 "미머지" 표기는 이 통합으로 전부 해소**됐다. 각 항목에 남은 것은 **PIE 사용자 검증**뿐이다.
> - 아키텍처 논의·결정 기록 방법론 = 스킬 `architecture-review`(결정문서는 `Docs/Architecture/NNNN-*.md`).

## 🧭 폐루프 디렉터 P0a-0 센서 = ✅코드+헤드리스 골든 완료 · **PIE 2-client 사용자검증 대기** (2026-07-20, `phase/director-p0a0` → main 통합 2026-07-27)
> 폐루프 "이야기꾼" 디렉터의 **첫 수직 슬라이스**(센서 워킹 스켈레톤). 목적 = "값이 맞나"가 아니라 **배관 불변식(생명주기 누수0·프리즈중 미진행·주입→출력 결정성·Front 매핑·enemy/boss 게이팅)**을 골든으로 잠그는 것. 설계 잠금 = `Docs/Review/20260720-plan-closed-loop-director.md §8·§9` + `20260720-p0a0-exec-prompt.md`. SSOT = `RunFlow §2-8-2`.
- **구현(Opus 직접 — 서버권위·프리즈대칭·생명주기·훅 = 위임 카브아웃)**: 신규 `UFPSRDirectorSensorSubsystem`(서버전용 `UWorldSubsystem`, `HasServerAuthority()` 게이트, **복제 0**) + `FFPSRPlayerTelemetry`(고정크기 plain struct, 서버로컬 `TMap<TWeakObjectPtr<AFPSRPlayerState>,…>`). **신호 5개만**: HealthPct · IncomingDamageRate[Enemy]/[Boss] · DownedRecent01(B3) · MovementConfinement01(C1, 7필드 앵커) · FrontId. 순수 헬퍼(`ClassifyDamageSource`·`AdvanceConfinement`·`StepEwmaRate`·`ComputeDownedRecent01`·`ShouldAdvance`·`PruneInvalidTelemetry`) = 단일 진실원, 월드리스 골든으로 잠금.
- **훅(각 1~2줄, 브릿지 시그니처 불변)**: `Hero/FPSRCharacter.cpp` — `ApplyContactDamage`(수락 히트 커밋 직후→IncomingRate, **source=Instigator 파생** enemy/boss만) + `HandleOutOfHealth`(→DownedRecent, 다운당 1회). `Core/FPSRGameMode.cpp` — `BeginPlay`(StartRun)·`Logout`(즉시 정리, PS 유효 시점). **FrontId = read-only `PS->GetCurrentMapId()` 점유 스냅샷**(`ComputeOccupancy` 무호출 → 액추에이터와 정합, 부작용 0).
- **결정적 시계·프리즈**: 0.5s `FTimerManager` 타이머, `GS->IsRunPaused()` 중 early-return(unpaused fixed-step → SensorClock·EWMA·confinement 윈도우 미진행). 테스트는 `Advance(dt)` 직접 호출. 주입 하네스 = `FPSR.Telemetry.*`(`#if !UE_BUILD_SHIPPING`) + `DumpSnapshot`(CSV `logs/TelemetrySnapshot.csv`). **의미판단(Score) 없음 · per-enemy tick 0 · 파생/정규화/그룹핑 전부 P0a-1↑**.
- **검증(Opus 직접)**: 빌드 `Result: Succeeded`(에디터 닫고 풀빌드, 새 UCLASS, UHT `-WarningsAsErrors` 통과) · 헤드리스 골든 `FPSRoguelite.Telemetry.*` 6종(SourceGating·Confinement·RateDowned·Determinism·FreezeNoProgress·Lifecycle) + `Smoke.ModuleLoads` = **7/7 Success, Queue Empty**. (Lifecycle 초판이 `NewObject<UObject>()` ensure로 Fail → 구체 UObject `UFPSRFlowFieldComputer` 키로 교정 후 통과.) **Codex 머지게이트 = codex CLI PATH 부재 → 규칙대로 패스, Opus 자체 적대검토로 대체(FrontId 정합·tombstone 구조검증 등 반영).**
- **⏳ 남은 = PIE 2-client 라이브훅 게이트(사용자 스모크, 이원게이트의 나머지 반)**: 실제 DBNO→DownedRecent · 실제 적 데미지→IncomingRate · FF/자해 제외 · 문 넘어 FrontId 매핑·per-player 승계. 서버 콘솔 `FPSR.Telemetry.Dump`로 스냅샷/CSV 확인. **통과 시 `--no-ff` main 머지 → 다음 = P0a-1(계산기+훅 신호, 이원게이트)**. (레시피 = `L_Lobby` ▶Play 2-client, §⑧ PIE 레시피 재사용.)

> 🔀 **2026-07-21 핸드오프 ② (U22a-A 건물 툴 = 실행 검증까지 ✅완료 · EUW 패널 가동 · Bake 리스크 해소)**:
> - **커밋**: `be36b78d`(결함 8건) → `779c8bf5`(창문 규칙+편집도구) → `7a6ffe44`(EUW 셸+문서) → `c47c6852`(EUW 자동배선 불가 확정) → `6f7abb15`(초보자 가이드) → `06bcb584`(팔레트 명칭 정정) → `d3a68609`(EUW 배선, 사용자 저작). **상세 = `Docs/U22a-A_BuildingTool_ResumePrompt.md`(3차 갱신)**.
> - ✅ **첫 실행 결함 8건 수정**: 회색 덩어리=SizingBox가 건물을 감쌈(→Preview 중 숨김) · 항아리 모양=셋백이 반 칸(125) 들여 격자 어긋남(→**셋백 기본 OFF**) · 지붕 타일 간격 125 고정이라 250 타일이 **4겹 중첩·875 돌출**(→메시 실측 크기로 산출, 36→9장) · 코니스 검증이 진짜 코니스를 탈락시킴(`Ceiling_Trim`의 `minZ=261`은 "층 천장"이라 정상 → **코니스를 코너와 분리**, 3→8종) · 반층 기둥(150) 채택(→높이 300 조건, 13→8종) · 위성 분해 부품/45도 코너 조각 혼입(→`_arm_`·`_dish_`·`_wing_`·`_45_` 제외).
> - ✅ **창문 배치 규칙**(사용자 판정 "층마다 뒤죽박죽이라 건물로 안 보임"): **수직 일관**(칸 위치를 키로 고정 → 같은 자리는 위로 쭉 같은 창문, `facade_mode`=column/floor/building/random) + **지상층 분리**(`Base_` 붙은 벽=1층용, 도어도 전부 `Base_` 계열). 부수 발견: `ground` 플래그를 **정면에만** 넘겨 1층인데 옆·뒷면은 상층 벽을 쓰고 있었음 → 네 면 전달.
> - ✅ **편집 도구 3종**(사용자 결정: **한 채씩 놓아가며 거리 조성** → 다듬기가 핵심): 생성 시 부모 태그에 `CityGenSize:WxDxF`·`CityGenSeed:N` 기록 → `reroll_selected()`(같은 자리·크기, 조합만 새로) / `change_floors(±1)`(**시드 유지**라 창문 그대로, 높이만) / `cycle_piece_mesh(±1)`(조각을 같은 카테고리 다음 후보로). `_build_one`이 공유 Random이 아니라 **자기 시드**를 받게 리팩터한 것이 토대.
> - ✅ **EUW 패널 가동**: `/Game/Tools/CityGen/EUW_CityGen` — 셸은 AI 생성, **버튼 배치·배선은 사용자가 수작업**(정상 동작 확인). 🔴 **Python 자동 배선은 불가 확정**(재시도 금지): `load_object(bp,"WidgetTree")`로 트리와 위젯 객체는 만들 수 있으나 **`WidgetTree.RootWidget`이 읽기/쓰기 모두 protected**·`SetRootWidget` UFUNCTION 부재라 루트 지정 불가 + 노드 생성 API도 없음. 초보자 가이드 = **`Docs/CityGen_EUW_Setup_Guide.md`**(EUW 편집 중엔 팔레트가 `Editor Utility Button`을 대신 보여줌 — 일반 `Button` 상속이라 동일).
> - ✅ **Bake(ISM) 콜리전 = 최대 리스크 해소**(실측): 플로우필드와 **동일 쿼리**(`FPSRFlowFieldComputer.cpp:1006` `ECC_WorldStatic` ObjectType)로 벽 관통 트레이스를 Bake 전/후 같은 좌표에 3발 → **전후 모두 블로킹 O, 충돌 지점 y=-11 동일**. ISM 14개 전부 `BlockAll`·`ECR_BLOCK`·`QUERY_AND_PHYSICS`·`ECC_WORLD_STATIC`, **인스턴스 합계 66 = 조각 66(누락 0)**. 드로우콜 66→14(4.7배, ISM 수는 **메시 종류 수**에 비례해 건물이 클수록 이득). 남은 것 = PIE 최종 확인.
> - **사용자 결정 3건**: ①거리 조성 = **한 채씩**(블록 일괄 생성 코드는 넣되 **기본 OFF**, 켜면 맞닿는 면 벽/코니스/코너를 층 단위 생략) ②**간판은 사용자가 직접 배치**(자동배치 코드 넣었다가 되돌림. 짝 규약만 문서에 남김: `sign`이 `backing` 안쪽 12.5 묻힘) ③뷰포트가 밝은 회색인 건 조명이 아니라 **Unlit 모드**.
> - **다음**: 사용자가 툴로 거리 조성 → 게임플레이 레이어 이식(룸·미션·문) → D(런 완주 → 쿡 → main 머지). ⚠️ `Map_CyberCity`는 여전히 **게임플레이 레이어 0**(미션스폰 0 → PIE 런이 보스까지 안 감).

> 🔀 **2026-07-21 핸드오프 ① (U22a-A 건물 생성 툴 = "에셋 선택 + 미리보기", Phase 1 ✅완료)**:
> - **방향(사용자 결정 2026-07-21)**: 프리셋 폐기 → **사용자가 종류별 에셋을 직접 고르는 툴**(창문벽 여러개·코너기둥·문·바닥/지붕·코니스·옥상프롭). 설정 그릇 = **C++ DataAsset**(사용자 결정: "C++ 작업·재빌드 코스트를 크게 여기지 말고 지금 단계에 가장 적합한 걸").
> - ✅ **Phase 1 완료·커밋 `40679a86`**: ① C++ **`UFPSRCityGenConfig`**(`UPrimaryDataAsset`, `Source/FPSRoguelite/{Public,Private}/CityGen/`) — `Facades[]`/`Corner`/`Door`/`RoofFloor`/`CorniceTrim`/`RoofProps[]`/`RoofPropCount`/`Width`/`Depth`/`Floors`/`bSetback`, **각 필드가 곧 내장 에셋 피커(썸네일)** = "보여주고 고르는" UI를 공짜로 획득. ② `fpsr_citygen.py` **config 기반 리팩터**(`generate_from_config` + `load_config_from_dataasset`[이름 정규화 견고화] + `preview`/`confirm`/`clear` + 메뉴 6종), `DEFAULT_CONFIG` 폴백이라 **빈 설정으로도 미리보기 동작**. **빌드 `Result: Succeeded`**(FPSRogueliteEditor Win64 Development).
> - 🔴 **왜 C++로 갔나(재조사 방지)**: `UDataAsset`/`UPrimaryDataAsset`은 **Blueprintable이 아니어서** BP로 DataAsset 하위클래스를 만들 수 없다 — VibeUE `create_blueprint`가 DataAsset 부모에서 계속 행/실패한 진짜 원인. 프로젝트에 blueprintable DataAsset 베이스도 없음 → **C++이 유일한 정공법**.
> - 🔴 **하드 교훈(재발 방지)**: **VibeUE MCP는 세션 도중 에디터를 껐다 켜면 그 세션에 다시 안 붙는다**(실측: 빌드하려 종료 → 빌드 성공 → 재기동했으나 도구 전부 소실, ToolSearch no match). → **C++ 빌드가 끼는 에디터 작업은 반드시 2세션으로 계획**(세션A=코드+빌드+커밋+재개프롬프트 / 세션B=에디터 켠 채 시작해 저작).
> - ✅ **2차 개편·커밋 `ec54e5a3` — 모듈 수집기 + Kit/Config 분리**(사용자 지적: "에셋 피커가 다른 메시도 다 보여줘서 규격 다른 걸 고르면 건물이 안 만들어진다 → 모듈러인 것만 수집해두고 그중에서만 고르게"): ① **`UFPSRCityGenKit`**(신규 C++) = 자동 수집 목록, **`UFPSRCityGenConfig`** = 단일값 4개를 **배열로** + `Kit` 참조. **분리 이유 = 재수집해도 사용자 프루닝이 안 날아가게.** ② Python `_measure`/`_classify`/`_validate` + `collect_modular_meshes`(스캔→실측→분류→검증→Kit 기록, **채택/탈락 사유 로그**) + `fill_config_from_kit` + 생성 시 풀 재검증(**무음 드롭 폐지**). 메뉴 8종. ③ **검증 기준은 카테고리별로 다름**: 파사드/도어=1칸(250)+피벗 엄격 / **코너·코니스=한 칸 내+바닥안착만**(250 배수 규칙 걸면 얇은 장식이라 기본값조차 전멸 — 내가 위임 결과에서 잡아낸 결함) / 루프바닥=125 배수 / 옥상소품=무검사. 500폭은 `멀티칸 미지원` 사유로 제외(구 260 무음가드 대체). ④ **빌드 `Result: Succeeded`**(2회차). 🔴 **실행 검증은 아직 0** — 이 메시들의 실제 치수/피벗을 한 번도 잰 적이 없어, **첫 Collect 로그가 최초 실측**이고 기준 조정은 그걸 보고 하는 게 순서.
> - **남은 것 = `Docs/U22a-A_BuildingTool_ResumePrompt.md` §2**: ① **`DA_CityGenKit`(클래스 `FPSRCityGenKit`) 생성 — 아직 없음, 꼭 만들 것** + `DA_CityGenConfig`는 생성됨(`055b0733`). 둘 다 **`/Game/Tools/CityGen/`**(툴 에셋은 `/Game/Tools/` 아래로 모으는 규칙 — 사용자 결정 2026-07-21, 폴더 난립 정리). 경로가 달라도 `find_kit_asset()`/`find_config_asset()`이 클래스로 폴백 검색함 ② 스모크 테스트(Open Config→Place Box→Preview→Confirm/Bake) ③ **Phase 2 = EUW 플로팅 패널**(DetailsView(DA)+버튼 4개; 컨테이너 위젯 프로그래매틱 저작 = 크래시/손상 위험 주의). **새 세션은 반드시 에디터 켠 상태로 시작.**

> 🔀 **2026-07-20 핸드오프 (이 브랜치 `phase/u22a-environment` = U22a-A 착수: PCG 도시 밀도)**:
> - **방향 재설정(사용자 결정 2026-07-20)**: U22a = **환경(건물) 먼저 → 지형 확정 후 게임플레이 레이어 이식 → D**. 화이트박스 지형은 **최종 아님**(건물이 걷는 공간을 바꿈). **A 생성 방식 = UE PCG**(Python 스크립트/툴버튼 검토 후 채택). 건물 소스 = `Content/PolygonScifi/`(187M·883 에셋, 사이버펑크 시티 킷, **미커밋**).
> - 🔴 **Map_CyberCity에 게임플레이 레이어 0개**(실측: 룸/미션스폰/문/스폰존 전부 0, 적스폰28·플로우필드4·SRS20만) = 리네임된 `L_GameFloor`. **지금 PIE 런은 보스까지 안 감**(미션스폰 지점 0 → `FPSRRunDirectorSubsystem.cpp:631`의 `TActorIterator<AFPSRMissionSpawnPoint>`). **정상·미결** — 이식은 **A(환경) 완료 후**. ⚠️ 종전 핸드오프가 A-3 step2 "게임플레이 레이어 이식"을 건너뛴 상태였음(이 세션 실측 확인, "D 대기"는 오판이었음).
> - ✅ **step 0 완료(이 세션, 에디터 닫고)**: `main`→`u22a` 머지(`db31d6a9` — 도시툴 C++ 10파일+M_BlockoutGhost+config+director/SSOT 문서, **소스=main+주석2줄**) + **PCG 플러그인 활성**(`312e5df9`, `.uproject`). → u22a 빌드/재빌드 안전(툴 소스 포함). **검증 빌드 `Result: Succeeded`**(`FPSRogueliteEditor Win64 Development`, 0 에러, 신선 바이너리 → 새 세션 에디터 재빌드 불요).
> - **재개 = `Docs/U22a-A_PCG_ResumePrompt.md`** — 새 세션을 **에디터 연결 상태로** 시작해 PCG 그래프 저작: perf 베이스라인 → 맵 설계(2계층: 스카이라인 배경 + 아레나 구조) → PCGGraph(StaticMeshSpawner=ISM+WorldStatic 콜리전+스트리트 마스크) → 생성 → **플로우필드/검증기/육안 검증**. 최대 리스크 **R1 = PCG ISM 콜리전 ↔ 런타임 플로우필드 다운트레이스 정합**.
> - 도시 빌드 툴(수동 배치)= `bf17ab38` main 완료(`Docs/CityBuildTool_Design.md`). **PCG와 별개**(수동 터치업 병용 가능).

## 🗄️ (이전) 현재 상태 (2026-07-20 · 도시 빌드 툴 ✅완료·main 머지 — U22a 최신 상황은 위 2026-07-21 핸드오프 ② 참조)

### 🏙️ 도시 빌드 툴 = ✅완료·main 머지 (2026-07-20, `phase/citytool-blockout`→main `bf17ab38`)
> Trinity Building Editor(Steam) 참조. 목적 = U22a A-4 밀도 저작 가속. **기존 블록아웃 툴 확장**(새 모듈 X, 설계=`Docs/CityBuildTool_Design.md`). Tools > FPSR > 블록아웃 툴.
- **P1a** 회전(`[`/`]`)+공용 스폰헬퍼 `FFPSRBlockoutSpawn`(3중복 제거) · **P1b** 팔레트 카테고리 필터(전체/구조/장식)+그리드단위 **250 확정**(에디터 GetBounds: Base Floor 250, Block 1500=6×250).
- **R1** 경량 BP 프리팹(`HarvestBlueprintFromActors`, **서브레벨 X**) — 종전 Packed Level Actor(프리팹마다 .umap 생성)는 사용자 피드백으로 폐기.
- **R2** Minecraft 면/그리드 스냅 — 커서 레이로 가리키는 면에 flush 부착 + 바운딩박스 그리드 타일링(Synty 코너피벗 조각도 딱딱 붙음).
- **R3** 카드 선택 시 자동 배치모드 진입 + **자석(근접) 스냅**(면 근처만 가도 스냅, `SnapRadius`) + **반투명 고스트**(`M_BlockoutGhost`, config `GhostMaterial`).
- 검증: 빌드×6·스모크×3·**사용자 육안 "정상 작동"**·Codex 머지게이트 P2 2건 교정(자석 높은벽 상하오붙임·BP프리팹 제외). **P4 재질패널=보류**(지속성 문제+스웜슈터 저가치, 사용자 결정).
- 곁다리: **Riot Client(8558)↔UE ZenServer(8558) 포트충돌** 크래시 해결 → `[Zen.AutoLaunch] DesiredPort=8560`(`DefaultEngine.ini`, 커밋 `4e02017a`). 롤 켜둔 채 UE 실행 가능.

### 🔜 다음 세션 = D 먼저, 그다음 A (사용자 결정 2026-07-20)
- **D (U22a 마무리)**: `phase/u22a-environment` 체크아웃 → **PIE로 Map_CyberCity 런 1회 완주**(미션 스폰·룸개방·보스) → 통과 시 **L_Sandbox 삭제**(⚠️ 삭제 전 `L_MainMenu.umap`의 L_Sandbox 참조 1건 확인) → 패키지 쿡 1회 → **u22a → main `--no-ff` 머지**. 상세=`Docs/U22_AssetReplacement_Prompt.md §3` A-6/A-7 + 아래 §⑩.
- **A (도시 밀도 저작)**: main(툴+맵)에서 **절차적 건물 생성기**를 만들어 CyberCity를 빠르게 채운다(사용자 착안: "건물=모듈 조각 패턴이라 AI 자동생성 가능"). 방법 = ①스크립트 생성기(파라미터: 가로×세로칸·층수·창간격·지붕종류·시드변형) ②툴에 "건물 생성" 버튼 내장 ③UE PCG로 소품 스캐터. 수동 스냅툴은 미세조정 병행.

### 🔴 U22a 환경 — D 대기 (phase/u22a-environment, 이 세션에 여기까지)
- ✅ 맵 리네임 L_GameFloor→**Map_CyberCity**(리다이렉터 삭제) · RunMap 전환 · MapsToCook 사문3건 삭제 · 코드주석2건 · A-6 헤드리스 검증 3종(빌드·스모크·앵커커맨드릿) 통과 · SRS 액터 4종 확인.
- ✅ 네온글로우 룩(미확정, 롤백가능): SRS 셀(Shadow Color 어둠·하드밴딩·해칭·시안림) · PP(bloom_threshold -1→1·잉크블랙·채도) · DL 하드섀도우. **A-4 밀도 저작 후 최종 룩 판단**(사용자).
- ⏳ **D 잔여**: PIE 런 완주(사용자) · L_Sandbox 삭제 · 패키지 쿡 · 육안 게이트.

## 🗄️ (이전) 현재 상태 (2026-07-19 · W2-B 정확성 감사 ✅완료·PIE 2인 검증 통과 · W2-C 메뉴/세션 권위 ✅완료)

### ⑧ W2-B 런타임 정확성 전수 감사 = ✅수정·검증 완료, **머지 전 사용자 PIE 대기** (2026-07-18, `phase/w2b-correctness`)
> W2-A(코드 품질)의 짝인 **정확성축**. 서버권위·복제/Push Model·MP 엣지·프리즈 게이트·생명주기·레이스·안티치트만 대상(성능 U25 / 스타일 W2-A / 콘텐츠·밸런스 제외). **전부 4인 협동+리슨서버 기준.**
> 방법 = 6 도메인 파인더(Sonnet) → **도메인별 적대 검증 2렌즈(Opus, refute-by-default)** → Opus 직접 소스 교차검증 → Codex 머지게이트 3라운드. 에이전트 12개(§6-5-1 상한 준수).
- **감사**: 원시 findings 14 → **적대검증 생존 9 / REJECTED 5**. 문서 = `Docs/codex-reviews/w2b-correctness-20260718.md`(gitignore).
- **🚨 P1 2건 = 근본원인 1개** (`93c4e3b5`): **런 맵 `AFPSRGameMode`에 `Logout` 오버라이드가 없었음.** 와이프 판정(`NotifyPlayerDefeated`)과 카드 프리즈(`GameState::RefreshPauseState`)가 **둘 다 pull 방식**인데 접속 종료가 어느 쪽도 트리거하지 않음 → ① 마지막 생존자 이탈 시 `EndRun(Defeat)` 영원히 미선언 ② 픽 대기자 이탈 시 프리즈 영구 고착. ②는 **자기봉인 데드락**(프리즈가 디렉터·스폰·XP를 멈춰 재계산 이벤트 자체가 발생 불가) + `FPSR.Pause`가 시핑 제외라 **출시 빌드 복구 불가**. 수정 = 로비 GM과 동일한 **다음 틱 지연** 재평가(`Super::Logout`이 `CleanupPlayerState`보다 먼저 돌아 즉시 재평가하면 이탈자를 아직 셈).
- **P2 2건** (`93c4e3b5`): ① `ServerTryConsumeFireInterval`이 다음 허용시각을 **도착시각에 재앵커**해 관용 25%가 매 발 누적 → 지속 간격 `0.75×`로 수렴 = **무기 4계열 전부 영구 +33% 연사**(수정 = `FMath::Max(Now, Next) + MinInterval`, 관용은 1회성으로 유지). ② `EquipSlot` **동일 슬롯 재입력**이 부분탄창 재장전을 취소하고 재개 안 함(재개는 탄약 0일 때만) + `CurrentSlotIndex`가 자기 값으로 쓰여 **OnRep 미발화 → 원격 클라만 쿨다운 미적용 = 유령 발사**(호스트엔 없는 비대칭).
- **P3 2건** (`93c4e3b5`, 각 1줄): `FPSREnemyBase::HandleDeath`가 `HandleDeathCosmetic` 직접 호출(보스와 동일 — 호스트는 OnRep이 없어 사망 상태 미적용, **Stage-3 death-dwell 착수 시 호스트만 회귀하는 것 사전 차단**) · `OnRep_LifeState` 이동잠금 조건을 `!= Alive`로 통일(DBNO→Dead 비대칭 제거).
- **Codex 머지게이트가 내 수정의 후속 결함 2건 적발**(3라운드): ① 부활 시 프리즈 재계산 누락 → `PerformRevive`에 `RefreshPauseState` 추가(`04c6d10b`) ② 다운된 플레이어의 **stale 카드 오퍼가 남아 전투 중 수락 가능** → `AFPSRPlayerController::WithdrawActiveOffer` 신설(`192dc793`). **픽은 소모하지 않아 부활 시 재제시 = 손실 없음**(`RequestCardOffer`는 뽑을 때 소모하지 않고 `ApplyCard`가 적용 시 소모). 3라운드 = "no discrete, actionable correctness issues" **통과**.
- **REJECTED 5건**(적대 검증이 반박 성공, 억지 버그 제조 없음): 적 풀 이중반납(호출처 4곳 전부 구조적 불가) · AliveCount 누수(서브시스템 없는데 ActiveEnemies에 있는 상태 = 논리적 구성 불가) · **2층 타깃팅**(흐름장은 `SampleFlowDirection(MapId, 위치)`라 타깃 좌표를 아예 안 씀 = 인과 자체가 틀림) · 부활/와이프 레이스(게이지 1.0과 `PerformRevive`가 같은 문장 블록 동기 호출이라 대기 창 없음) · 투사체 포인터(반환값 역참조처 0).
- **이월**(수정 안 함): 투사체 cap eviction의 `NotifyMiss` 우회(P3, 해제경로 통합 필요·도달난이도 매우 높음) · 적 발사체 `InstigatorActor` stale(P3, 피격방향 표시기 좌표만 = 코스메틱) · **런 중 재접속 진행도 복원(P3 = 버그 아닌 미구현 기능 — 복원 구현은 식별자 스푸핑 안티치트 표면을 새로 만듦, 사용자 설계 결정 필요)** · `ReleaseEnemy` 1줄 방어 하드닝.
- **검증**: 빌드 `Result: Succeeded` ×3 · 스모크 `ModuleLoads Result={Success}` ×3 · Codex 게이트 3라운드 통과 · Opus 직접 소스 교차검증(P1/P2 전 항목).
- **✅ 사용자 PIE 2인 검증 통과(2026-07-19)**: ① 카드 프리즈 중 클라 이탈 → 나머지 프리즈 해제 ✅ ② 마지막 생존자 이탈 → Defeat 선언 ✅. **P1 수정이 실측으로 확인됨** → 머지 조건 충족.
  - **PIE 2인 테스트 레시피(재사용)**: 메뉴에서 시작하면 안 됨(아래 §⑨) → **`L_Lobby`를 열고 ▶ Play**(PIE_ListenServer·2 clients) → 준비 → 런 시작(로비 GM `bUseSeamlessTravel=true`라 클라 동반) → 게임 맵에서 2인.
  - **이탈 시뮬레이션 = 클라 창 콘솔(`~`)에 엔진 내장 `disconnect`**. PIE에서 ESC는 플레이 종료라 설정 오버레이를 못 여는 반면, `disconnect`는 그 창 연결만 끊어 서버 `Logout`을 정상 발화시킨다(`UnrealEngine.cpp:15130-15155` = `SetClientTravel("?closed")`). 프로세스를 안 죽여 호스트 화면을 계속 관찰 가능.
- **부수 산출물**: ESC 설정 오버레이에 **게임 종료 버튼**(`dfcbe833`) — 오버레이는 프리즈·다운 중에도 열리므로 패키지 빌드에서 클라가 아무 때나 이탈하는 수단. UMG는 기존 `WBP_QuitButton` 인스턴스 재사용(신규 에셋 0). 패키지 = `Packaged/26_7_19_BuildTest_1/`(BUILD_INFO에 T1~T4 절차).

### ⑨ 메뉴/세션 경로 서버권위 결함 = ✅**수정 완료·main 머지** (2026-07-19, `phase/w2c-session-authority`)
> 아래 3건 + 같은 결함 클래스인 GameFlow 트래블 가드까지 처리. **셋 다 "권위 검사 없이 서버 전용 동작 실행"** 이라는 한 가지 결함 클래스.
- **⑨-1 수정**: 소유권 추적 `bLocalSessionIsHosted` 도입(create-complete set / join-complete·destroy-complete clear) → 우리가 만들지 않은 세션이 등록돼 있으면 호스팅 거부. **넷모드 가드만으론 부족** — `JoinSession` 성공과 `ClientTravel` 완료 사이 창은 아직 클라가 아니라서, 타이밍이 아니라 **소유권**으로 판정해야 한다.
- **⑨-2 수정**: `HostSession` **최상단**에 `NM_Client` 차단(스테일 파괴 분기보다 **앞** → 고아 세션·오염된 로비코드 미발생) + `OnHostComplete.Broadcast(false)`(기존 실패 경로 5곳과 동일 계약, 안 하면 UI 영구 대기) + `ServerTravel` 직전 이중 잠금.
- **⑨-3 수정**: `AFPSRMenuGameMode`에 `bUseSeamlessTravel = true` → **PIE에서 메뉴부터 정상 2인 흐름 가능**(종전엔 `L_Lobby` 시작으로 우회해야 했음). 배포 무영향.
- **+ `GameFlowSubsystem::StartRun/ReturnToMenu`** 트래블 권위 가드(헤더의 authority-only 약속을 코드로 강제).
- **Codex가 내 수정의 버그를 잡음**: destroy **실패** 시에도 소유권 플래그를 지워 다음 호스팅이 자기 세션을 남의 것으로 오인 → **영구 잠금**. `bWasSuccessful`일 때만 해제로 교정(`28d08a98`).
- **검증**: 빌드 `Result: Succeeded` ×2 · 스모크 **4/4 Success** ×2 · Codex 게이트 2라운드(마지막 = "No discrete correctness, replication, or build issues").
- **✅ 사용자 실측(2026-07-19)**: **Play 정상 작동 확인** — ⑨-3(메뉴 GM seamless) 반영 후 메뉴 흐름이 회귀 없이 동작. **정직 표기**: 아래 둘은 이번 확인에 **포함되지 않았다** → ① 클라 창에서 Play를 눌렀을 때 크래시 대신 무시되는지(기대 로그 `HostSession BLOCKED (client)`) ② **⑨-1 세션 자폭은 PIE로 검증 불가**(NULL OSS라 조인 분기 자체에 도달 안 함) → **Steam 2-PC 검증 시 확인할 것**. 코드·Codex 리뷰 검증까지는 완료.
- **비버그로 확정**: PIE 코드 조인 실패(0 candidate)는 버그 아님 — Steam 미접속 → OSS가 NULL 폴백 → NULL의 `FindSessions`는 LAN 브로드캐스트 전용(`OnlineSessionInterfaceNull.cpp:513-582`). `Docs/P7-U11a_UserContent_Guide.md §7.1/§7.2`에 기존 문서화.
- **남은 별건 백로그**: `NAME_GameSession` 프로세스 전역 상수 1개를 PIE 두 창이 공유(`FPSRSessionSubsystem.cpp:19`) — 이번 크래시 원인은 아님(클라 단독 클릭으로도 재현), 별건.

<details><summary>발각 경위 (접은 이력)</summary>

### 🔴 (발각 시점 기록) 메뉴/세션 경로 서버권위 결함 3건 (2026-07-19)
> W2-B PIE 검증 중 **PIE 메인메뉴에서 Play를 누르면 에디터가 하드 크래시**하는 것을 계기로 발각. W2-B 감사 범위가 "런 중 게임플레이"라 메뉴/세션 경로를 안 본 **범위의 빈틈**. 조사 = 4에이전트 워크플로(적대 반박 + 권위 census + 테스트경로) + Opus 직접 엔진소스 대조. **W2-B 브랜치와 무관**(`git diff main --stat` = 메뉴/세션/로비 파일 0개, 세 GameMode는 전부 `AGameModeBase`에서 독립 파생).
- **⑨-1 `HostSession` 세션 자폭 (가장 심각·배포 영향)**: `HostSession`은 진입 즉시 같은 이름의 기존 세션을 파괴하는데(`FPSRSessionSubsystem.cpp:78-92`), **조인 경로도 같은 이름으로 등록**한다(`:282`, 상수 `:19` = `NAME_GameSession`). → Steam으로 조인해 로비에 있는 클라가 어떤 경로로든 `HostSession`에 들어오면 **자기가 참가 중인 세션을 스스로 파괴**. PIE에서 안 보인 이유 = NULL OSS라 조인 자체가 없어 `GetNamedSession`이 null.
- **⑨-2 클라에서 Play → 하드 크래시**: `HandlePlayClicked`(`FPSRMainMenuWidget.cpp:48-59`)·`HandleCreateSessionComplete`(`FPSRSessionSubsystem.cpp:152-158`) 둘 다 넷모드 검사 0 → 클라 월드에 `ServerTravel("...?listen")`. 엔진(`World.cpp:9389-9406`)은 `GetAuthGameMode()==nullptr`이라 검사를 건너뛰고 `NextURL`만 세팅 → `EditorEngine.cpp:2142`가 넷모드 구분 없이 모든 PIE 컨텍스트를 틱 → `LoadMap`이 클라 자기 넷드라이버를 파괴하고 `?listen`으로 이미 점유된 포트에 바인드 시도. **재현 3/3**(FlowLog 3개가 전부 `[Client] ServerTravel` 줄에서 종료, 서버 경로는 4/4 정상 = 반례 0). 콜스택은 확보 못 함(덤프 없음·UE 로그 버퍼 유실).
  - **수정 시 함정 3개**(검증자 지적): ⓐ 판정은 **`NM_Client` 차단**이어야 함 — "Standalone만 허용"으로 짜면 **PIE 서버 창(메뉴가 `NM_ListenServer`)까지 막혀** 테스트 불가(`FPSRFlowLog.cpp:49-64` 태그 매핑으로 실측). ⓑ 가드는 **`HostSession` 진입 최상단**(세션 파괴 분기 `:78`보다 **앞**) — `ServerTravel` 직전에 넣으면 고아 세션·오염된 로비코드가 남고 ⑨-1도 못 막음. ⓒ 조기 반환 시 **`OnHostComplete.Broadcast(false)` 필수**(기존 실패 경로 5곳이 전부 지킴, 안 하면 UI 영구 대기).
- **⑨-3 `AFPSRMenuGameMode`만 `bUseSeamlessTravel` 누락**: 런(`FPSRGameMode.cpp:51`)·로비(`FPSRLobbyGameMode.cpp:26`)는 `true`인데 메뉴 GM 생성자엔 없음 → 메뉴→로비 트래블이 non-seamless라 **접속된 클라를 끊어버림**(실측 `[NETFAIL] Host closed the connection` → 클라가 `Standalone`으로 메뉴 복귀 → 별개 로비 2개 생성). **배포에서는 무해**(각자 메뉴는 Standalone이라 끊길 연결이 없고 타인은 Steam 조인). **PIE 2인 테스트만 막는다** → 위 레시피(`L_Lobby` 시작)로 우회 중. `TransitionMap`은 이미 설정됨(`DefaultEngine.ini:6`).
- **비버그로 확정**: PIE에서 코드 조인 실패(0 candidate)는 **버그 아님** — Steam이 안 붙어 OSS가 NULL로 폴백되고(로그 `:338-341`) NULL의 `FindSessions`는 LAN 브로드캐스트 전용(`OnlineSessionInterfaceNull.cpp:513-582`). `Docs/P7-U11a_UserContent_Guide.md §7.1/§7.2`에 이미 문서화돼 있었음.
- **별건 백로그**: `NAME_GameSession` 프로세스 전역 상수 1개를 PIE 두 창이 공유(`:19`) · `GameFlowSubsystem::StartRun/ReturnToMenu`가 헤더엔 "authority-only"라 적고 본문엔 가드 없음(`FPSRGameFlowSubsystem.cpp:8-27`/`:29-48`, 둘 다 BlueprintCallable).

</details>

### ⑦ W2-A 코드 품질·기술부채 그라인딩 = ✅완료·main 머지 (2026-07-18)

> **U21 파일럿 게이트 통과**(사용자 판정 2026-07-18): S1 단차 walkability ✅ · S3 셀 아웃라인×VAT 정합 ✅ · S4 성능 실측 ✅("다음 유닛 넘어갈 만한 퍼포먼스"). 아트 방향 = 사전확정 셀/툰 피벗(메모리 `synty-anime-cel-art-pivot`)이 **파일럿으로 검증됨** → U22(전체교체) 게이트 해제. **✅하위결정 2건 확정(2026-07-18)**: ① 무기 백본 = **Synty Military 전환**(→ 나중에 SF 무기로 리스킨/변환) ② 캐릭터 애님 = **3인칭 Blu · 1인칭 PWAS**(손저작 AnimBP 대체 → U15 1P·U19 3P 손저작 계획은 이 파이프라인에 흡수, U20 적 VAT만 별도로 U22 적교체 후 베이크).
> **추가 수정 = SRS 아웃라인 거리 헤이즈**(아래 §⑥). 코드 미커밋 0. 작업트리 = **사용자 콘텐츠만 미커밋**(아래 §미커밋 콘텐츠 — SRS 수정은 `L_GameFloor.umap` 인스턴스에 포함).

### ⑦ W2-A 코드 품질·기술부채 그라인딩 = ✅완료·main 머지 (2026-07-18)
> U21 완료 후 "전체 프로젝트 그라인딩"의 **코드축** — U22와 무관하게 살아남는 C++ 로직/구조만 대상(콘텐츠·임시값[U14]·성능[U25]·정확성버그[W2-B] 제외). 6 도메인 감사(파인더 Sonnet) → Opus 검증 → Codex 적대 토론 → 재작업.
- **감사**: 런타임 223 + 에디터 35 = 258 파일 전수. 원시 findings 28 → **P1 없음, 전부 P2/P3**(그라인딩 성격 확인). 문서 = `Docs/codex-reviews/w2a-code-quality-20260718.md`(gitignore).
- **수정 20건**(`0a8cc495` + cook fix `4a6c7b1b`): ① 주석 드리프트 9(적/플로우필드/미션/투사체/무기타입/시작무기 코드-모순) ② §6-2 하드코딩 3(XP젬/적/보스 placeholder 메시 `ConstructorHelpers`→config `FPSRPlaceholderVisualSettings` BeginPlay fallback) ③ dead code 2(`ServerRequestReturnToMenu` RPC·`ContentBrowser` 에디터 의존) ④ UI.Layer 태그 SSOT 네이티브화(`FPSRUITags`, 11사이트, ini 중복 제거) ⑤ `CopyProperties` Push Model dirty-mark(seamless travel 복제 정합) ⑥ 데이터드리븐 4(투사체 머즐오프셋 DA·rarity LOCTEXT·Assembler 폴더명 config·적 캡슐반높이 공유상수).
- **Codex 합의 DEFER**: `DownedMoveScale`(BP참조 grep불가)·PelletCount EditCondition(의도적 설계)·`AvailableModifiers`(직렬화 필드→U22 재저장 시)·크로스헤어 프리셋(UI 구조변경 동반)·enum 중간값 제거(직렬화 리스크, 값 보존·주석만)·저가치 백로그(IsMapReady·미션 DrawDebug cvar).
- **판정 핵심**: re-run-safety 계열(`ResetForNewRun` 등, `bRunActive` 래치 뒤)은 **의도적 전방 인프라 → 보존**(파인더 오탐 없이 방어). enum 중간값은 **제거 대신 주석**(직렬화/BP핀 안전).
- **검증**: 빌드 `Result: Succeeded`(-WaitMutex 풀빌드, UHT WarningsAsErrors) · 스모크 `ModuleLoads Result={Success}` · Codex 머지게이트 = P2 1건(패키지 cook)만 → `4a6c7b1b`로 해소.
- **⚠️ 후속(사용자 판단)**: cook 규칙(`/Engine/BasicShapes` DirectoriesToAlwaysCook)은 **패키지 빌드 미검증**(현 세션 빌드/스모크만) — 실 패키지 시 XP젬 가시성 확인 권장. 단 dev 스캐폴딩이라 U22서 실메시 교체되면 무의미.

### ⑧ W2-A 후속 = 보류항목 4건 처리 (2026-07-18, `phase/w2a-deferred-cleanup` → main)
> W2-A에서 DEFER했던 것을 **사용자 판단으로 지금 처리**("일부러 터뜨려 지금 잡아둬야 나중에 편해" — 호환 껍데기/리다이렉트 없이).
- **제거 3건**: `DownedMoveScale`(죽은 UPROPERTY) · `AvailableModifiers`(폐기 필드) · `EFPSRWeaponArchetype::Burst`(미사용 아키타입). **콘텐츠 스캔으로 영향 에셋 0건 사전 확인**.
- **`ECardGroup::WeaponUnlock`은 보존(판정 번복)**: 제거 시도 중 **7개 해금 카드 에셋(`DA_CardUnlock_*`)이 실제로 이 값을 분류로 사용** 중임을 발견 — 종전 "미사용" 판정은 **C++ 한정 조사의 누락**이었음. dead CODE이지 dead DATA가 아니므로 값 유지 + "Weapon으로 재태깅하면 `TargetWeapon`이 설정돼 실동작이 바뀐다"는 경고를 주석에 명시.
- **크로스헤어 색 프리셋 데이터에셋화**: `UFPSRCrosshairColorPresetDataAsset`(이름+색 배열) 신설, 설정 위젯이 프리셋 수만큼 스와치 버튼을 **런타임 생성**(고정 버튼 5·핸들러 5·하드코딩 색 5 제거). `UFPSRColorPresetButton`(UButton 파생)이 자기 인덱스를 네이티브 델리게이트로 실어 보내 **별도 버튼 WBP 없이** 동적 생성 가능.
- **Codex 리뷰**: P2(프리셋 미배선 시 색 설정 사라짐) = 수용, 단 fallback 복구 대신 **경고 로그 2종**으로 표면화. **P1(Burst 제거가 뒤 enum ordinal을 밀어 무기 DA 깨짐) = 오탐 판정·미적용** — UE tagged 직렬화는 enum을 이름으로 저장하며 `DA_Weapon_Bazooka/ChargeLaser/Knife/Shotgun/Sniper`가 `"EFPSRWeaponArchetype::<값>"` 문자열을 그대로 보유함을 실증(FullAuto 무기가 목록에 없는 것도 기본값 미직렬화와 정합).
- **검증**: 빌드 `Result: Succeeded`(⚠️XGE가 `C1076` 힙고갈로 실패 → 메모리 처방대로 `-NoXGE` 전환) · 스모크 `ModuleLoads Result={Success}`(신규 DLL 기준 재실행 — 실패 빌드의 구 DLL로 돈 결과는 폐기).
- **✅ 사용자 에디터 작업 = 완료·커밋·푸시**(`86691340`, 2026-07-19): `Content/UI/Data/DA_Crosshair_ColorPreset.uasset` 신규 + `WBP_Settings.uasset` 스와치 배선이 **같은 커밋에 동반**. 종전 여기 적혀 있던 "⚠️ 사용자 에디터 작업 필요(①에셋 생성 ②스와치 컨테이너 바인드 ③에셋 할당)" 항목은 **그때 전부 처리됐는데 이 줄만 안 지워져 있었다** → 2026-07-19 세션에서 stale 판정·정정(그 문구를 그대로 믿고 "미완"으로 보고한 오류 1회 발생).

### ⑥ 적 "뿌연 구름" = 진단·수정 완료 (2026-07-18, 콘텐츠 = `L_GameFloor` 인스턴스)
**증상**: 런 시작 후 적 swarm(만)에 반투명 회색 헤이즈. 멀수록 심하고 가까이 오면 옅어짐. 건물은 쨍.
- **격리(콘솔 사다리)**: 모션블러 OFF(`r.MotionBlurQuality=0`)·`showflag.Fog 0`·`showflag.Atmosphere 0`·`r.TSR.Enable 0`·`showflag.Bloom 0` = **전부 변화 없음** → `showflag.PostProcessing 0`에서만 정상 = **포스트프로세스 머티리얼**이 범인(엔진 하이트포그/대기/TSR/블룸 전부 아님).
- **진범 = `BP_SRS_Fog`의 `M_Fog`** (⚠️초기 "SRS 아웃라인" 가설은 **철회**). `Only on Custom Depth=1` + 회색 `Fog Color(0.18,0.20,0.22)` + `Camera Distance Scale=1000` → **커스텀뎁스 물체(=적, `renderCustomDepth=True`)에만 거리비례 회색 안개**. 그래서 적만 뿌옇고 건물 쨍·멀수록 심함. **`showflag.Fog 0`이 안 통한 이유** = M_Fog는 **PP 머티리얼 blendable**이지 엔진 하이트포그가 아님(그래서 `showflag.PostProcessing 0`에서만 걷힘).
- **수정 = `BP_SRS_Fog` 인스턴스 `Enable Fog=False`**(사용자 별도 MCP 세션, 저장됨). 아웃라인·셀은 무변경(셀/툰 룩 유지).
- **검증**: 사용자 PIE 육안 + git(`L_GameFloor.umap` 수정, `BP_SRS_Fog`는 레벨 배치 액터라 인스턴스 오버라이드로 정합).
- **교훈**: 거리감쇠 헤이즈 ≠ 항상 엔진 안개. PP가 **적(커스텀뎁스)에만** 끼면 아웃라인으로 단정 말고 **레벨의 모든 PostProcessComponent/Volume blendable 전수 열거** + `Only on Custom Depth`/`Fog Color` 확인. `showflag.PostProcessing 0`은 "PP가 범인"까지만 말해줌. 상세 메모리 `srs-postprocess-stack-fog-haze`.

### ① 이 세션에서 한 일 (S4 계측 아크 = 머지 완료)
- `9059af12` **feat(perf)**: S4 가독성 5지표 계측 = **`UFPSREnemyMetricsSubsystem`**(신규). ②③④는 "플레이어 한 명이 겪는 것"이라 **서버 아닌 각 클라가 자기 로컬 폰/뷰 기준**으로 집계 → CSV 커스텀 스탯 5개(`FPSREnemy/ServerAlive|RelevantAlive|VisibleFrustum|VisibleRendered|Near15m`). 신규 순회 0·월드쿼리 0·shipping 생성 거부(`CSV_PROFILER_STATS`).
- `e198f668` **fix(perf)**: ③b **그림자 패스 오염** 수정(`AActor::WasRecentlyRendered`→`GetLastRenderTimeOnScreen`). 첫 실캡처 위반 **47.87%**(3930/8210, 재확인)로 발각. **엔진 소스로 진단 확정**: `PrimitiveSceneInfoData.cpp:12/19-22`가 액터 스탬프를 무조건 씀 — Epic이 `bCastWhenHidden` 때문에 **의도적으로** 그렇게 함(`ShadowSetup.cpp:2056-2061`). 리센시 창도 초→**프레임 기준**.
- `19e1065d` **fix(perf)**: ⚠️ **위 수정만으론 불변식이 안 지켜졌음** — ③a는 액터 원점 40cm 구를 재는데 렌더러는 **메시 바운드**로 컬링(`SceneVisibility.cpp:599-622`). 근접 적 메시 AABB = 액터기준 Z ∈ [-98.87, +58.00](~157cm) vs 구 [-40,+40] → 위쪽 프러스텀 평면에 **58.9cm 밴드** = 버그 0으로 `③b>③a`. **→ ③a도 `EnemyMesh->Bounds` 테스트 = 구조적 상위집합**(렌더러가 동일 바운드를 sphere 테스트 후 box·occlusion 추가) → 불변식이 **설계로** 성립. + 프러스텀 없는 프레임의 **가짜 위반 제조** 차단, **엔진 소스와 반대로 틀린 주석 2건 교정 + 1건 삭제**(RT 50m 거리게이트·Nanite `IsAlwaysVisible` 게터 단축·PIE 멀티뷰).
- 검증 = 빌드 `Result: Succeeded` · 스모크 `FPSRoguelite.Smoke.ModuleLoads Result={Success}` · Opus 직접 엔진소스 재확인(9에이전트 워크플로 + 적대 검증 3렌즈).

### ② 다음 코드 작업 (구체)
1. ~~**보스 HUD 바 clear 경로 누락**~~ → ✅ **완료·main 머지**(2026-07-15, 아래 §④). 판정 = **재현 불가(잠재 계약 갭)** — "버그 수정" 아님. 재개 문서 `Docs/BossHUDClear_ResumePrompt.md`는 **폐기**(전제가 틀렸음, §④ 참조).
2. **U20 적 애니 계약 교정** — ⚠️ **문서 충돌**: 여기선 "다음 작업"이었으나 `TaskPrompts_Master.md §B` DAG는 **U15/U19/U20 = HOLD("U21 아트정체성 결정 전 착수 금지")**. 근거도 명시적 — **적 VAT 베이크는 U22 적 교체 이후라야 재베이크를 안 함**. 지금 `M_BroBot_VAT` 기준으로 계약을 맞추면 Synty 적 교체 시 버려짐. **→ DAG(HOLD) 채택, U21 게이트 후로 이월.** (사실관계: `FPSREnemyAnimProfile.cpp:52`가 `AnimationIndex`를 쓰는데 머티리얼엔 없음[클립선택=`StartFrame`/`EndFrame`], `Phase`→`TimeOffset`, `PlayRate`는 OK. 적 BP 3종 `AnimProfile`=null. 에셋 제약 = BroBot에 Idle/Walk/Run만, Attack/Death 애니가 프로젝트 전체에 없음 → 3상태 상한.)

### ④ 보스 HUD 바 (2026-07-15 · 두 건 = 별개 문제, 둘 다 main 머지)

> ⚠️ **먼저 읽을 것 — 이 절의 초판은 틀렸다.** 초판은 "재현 불가"로 단정했으나, **사용자 스크린샷으로 실제 버그가 발각**됐다(런 시작하자마자 BOSS 바가 꽉 찬 채로 표시). 원인은 **④-2**이고, 조사가 그걸 놓친 이유는 **재개 문서의 프레임("보스가 죽은 뒤 stale")을 그대로 물려받아 "보스가 생기기 전"을 아예 묻지 않았기 때문**이다. 게다가 그걸 잡을 유일한 차원(`hud-consumer` = 바가 null일 때 뭘 하나)이 **사용량 한도로 죽은 상태에서 결론을 냈다**(교훈: 조사가 죽으면 "모른다"고 할 것).
> **두 건은 별개다**: ④-1 = 잠재(계약), ④-2 = **실제 유저 버그**(고침). ④-1의 수정은 ④-2를 고치지 못한다.

#### ④-2. 🐛 실제 버그 — 런 시작에 BOSS 바가 뜸 (콘텐츠, `WBP_BossHUDBar`)
**증상**: 런 시작(Lv1·보스 없음)부터 상단에 BOSS 바가 `Percent=1.0`로 표시. **사용자 PIE 확인으로 수정 검증 완료.**
- **원인**: `WBP_BossHUDBar` EventGraph의 **Construct 초기 동기화가 `IsValid` 게이트 뒤에 갇혀 있었음**.
  `Bind → [IsValid(GetActiveBoss())] ─Is Valid→ OnBossChangedEvent(보스)` / **`Is Not Valid` 핀은 미연결(빈 핀)**.
  → 런 시작엔 보스가 null이라 `Is Not Valid`로 빠지고 **이벤트가 아예 호출되지 않음** → 숨김 분기가 실행될 기회가 없음 → 위젯이 **디자이너 기본값(`SelfHitTestInvisible` + `Percent=1.0`)** 그대로 화면에 남음.
- **`OnBossChangedEvent` 자체는 원래부터 정상**: `IsValid(Boss)` → 유효=`SelfHitTestInvisible`(표시) / 무효=언바인드 후 `Collapsed`(숨김). **로직이 아니라 호출이 안 되던 것.**
- **수정**: Construct 쪽 `IsValid` 매크로 노드 삭제 + `Bind.then → OnBossChangedEvent.execute` 직결(= **무조건 초기 동기화**, 이벤트가 알아서 분기). 이벤트 내부의 `IsValid`는 **그대로 둠**(그게 show/hide 본체).
- **교훈(일반화)**: 이벤트 구동 UMG 위젯은 **① 델리게이트 구독(변화) + ② Construct에서 현재 상태 무조건 반영(초기값)** 둘 다 필요하다. ②를 "값이 있을 때만"으로 게이트하면 **빈 상태가 디자이너 기본값으로 새어나온다.** 관련 메모리 `umg-event-widget-initial-sync`.
- **검증**: 배선 되읽기(무조건 호출·데이터핀 유지·IsValid 1개만 잔존) · 컴파일 OK · 디스크 변경 = `WBP_BossHUDBar.uasset` 단일(컨테이너 `WBP_GameHUD` 무변경) · **사용자 PIE 확인 완료**.

#### ④-1. 잠재 계약 갭 — `SetActiveBoss(nullptr)` 호출처 0건 (코드, `phase/boss-hud-clear` → main `--no-ff`)
**판정 = 현재 재현 불가(잠재). "버그 수정" 아님 — ④-2와 무관하며 ④-2를 고치지 못한다.** 재개 문서 `Docs/BossHUDClear_ResumePrompt.md`의 **핵심 전제가 틀렸으므로 그 문서는 폐기**한다.

- ❌ **문서의 틀린 전제**: "보스가 파괴되면 GC가 `TObjectPtr`를 조용히 null로 만드는데 `OnActiveBossChanged`는 발화 안 함". → **보스는 파괴되지 않는다**(`FPSRBossBase.cpp:155` HandleDeath가 결과 연출용으로 **의도적 존치**: "No XP drop / pooling / Destroy... keeps it visible during the result beat"). 실제 상태는 GC-null이 아니라 **"체력 0인 유효한 보스를 가리키는 멀쩡한 포인터"**.
- ✅ **관측 불가 근거 3개(각각 독립 성립)**:
  1. **트래블에서 런 GameState가 죽음** — 2홉(런→`L_Transition`→로비). `AGameModeBase::GetSeamlessTravelActorList`가 GameState를 **`bToTransition`일 때만** 넘김(`GameModeBase.cpp:548-553`) → 둘째 홉에서 버려지고 로비는 `ActiveBoss=null`인 새 GameState를 만듦. HUD 소유자(런 PC)도 클래스가 달라 `SwapPlayerControllers`가 파괴.
  2. **같은 월드 재런 = 도달 불가** — `StartRun` 호출처는 `FPSRGameMode.cpp:76`(BeginPlay) **단 하나**이고 **UFUNCTION이 아니라 BP/콘솔에서 도달 불가**. 게다가 **`bRunActive`는 어디서도 해제되지 않는 일방 래치**(선언 `.h:99` · 읽기 `.cpp:69` · set-true `:74`가 census 전부) → 두 번째 호출은 조용한 no-op. **즉 `StartRun`의 "Re-run safety" 형제 리셋들(`ResetSpawnZones`·`ResetForNewRun`·`ResetDoorTopologyToBaseline`)도 지금은 전부 도달 불가**. 코드 주석도 동의: `FPSREnemySpawnSubsystem.cpp:1479` **"FUTURE NOTE (same-world re-run only, not yet reachable)"**.
  3. 유일 관측 구간 = 결과 화면 뒤 `PostRunTravelDelay`(~3초)뿐이고 그건 **의도된 연출**.
- ✅ **그래도 고친 이유**: 계약이 이미 3곳에서 약속됨(`FPSRGameState.h:138/141-142/247`) + **보스 2페이즈(보스 사망≠런 종료)나 같은 월드 재런이 도입되면 그 시점에 즉시 관측 가능**해짐(보스가 파괴되지 않으므로 죽은 보스 바가 다음 보스까지 ~300초 유지).
- **구현**(`74e63137`, `FPSRRunDirectorSubsystem.cpp` StartRun 재런 안전 블록): `GS->SetActiveBoss(nullptr)` **→ 그 다음** `ActiveBoss->Destroy()`. ⚠️ **순서가 제약**: 액터가 garbage가 되면 복제 ref가 자가-null되어 세터의 `ActiveBoss == InBoss` 조기 반환(`FPSRGameState.cpp:40`)이 HUD에 필요한 broadcast를 **삼킨다**. 액터 파괴까지 하는 이유 = 포인터만 비우면 죽은 보스가 레벨에 그대로 서 있음. 첫 런은 전부 no-op.
- **멀티**: 세터 경유라 Push Model `MARK_PROPERTY_DIRTY` + 리슨서버 호스트 직접 broadcast(호스트는 OnRep 없음) 대칭 그대로 성립.
- **검증**: 빌드 `Result: Succeeded` · 스모크 `Result={Success}` · Codex 게이트 통과("safely clears the replicated boss reference before destroying the prior boss actor... without introducing an obvious regression"). **PIE 미실시 — 현재 도달 불가 경로라 재현 시나리오 자체가 없음**(보스 2페이즈/재런 도입 시 그때 PIE 필요).
- 📌 **미해결 인접 사실**: `StartRun`은 `GS->SetActiveMission(nullptr)`도 안 부르고 디렉터의 `ActiveMission`도 안 비운다(진행도만 0으로). 같은 재런 시나리오에서 **미션도 동일 갭** — 이번 스코프 밖, 재런이 실제로 도입될 때 같이 볼 것.

### ③ 블로커 / 주의
- ✅**S3(외곽선×VAT 정합성) · S4 성능 실측 = 통과**(사용자 판정 2026-07-18). Claude측 계측 인프라 완료. **아래 튜닝 노트는 후속 조정 시 참고용으로 보존**(S3 톤다운·불변식 합격선·밀도 측정 함정 등).
  - ⚠️ **S3 판정 순서 주의**: 지금 셀 아웃라인을 **자동노출 최대 66배 + 블룸 2.37배**를 통과시켜 보고 있음 = 아웃라인이 아니라 그레이드를 보는 것. **톤다운 먼저 → 그 다음 아웃라인 판정**(안 그러면 아웃라인을 과하게 두껍게 만들고 나중에 그레이드 고치면 흉해짐).
  - ⚠️ **PIE 창이 640×480**(`EditorPerProjectUserSettings.ini:91-92`). 화면 크기가 곧 ③이라 이대로 잰 숫자는 무의미 → **1920×1080**으로.
  - ⚠️ **불변식 합격선 = 위반율 <5% + 최대 초과 ≤2**(0%는 달성 불가 — ③b는 과거 1~3프레임 스탬프, ③a는 현재라 카메라 회전 시 전환프레임 위반이 물리적으로 남음). 진짜 버그(47.9%·초과 최대 59)와는 이 기준으로 갈림.
  - **실전 상한 192**(`GlobalAliveCap 200 − SeedReserve 8 − FrontReserved`, 단일맵이라 FrontReserved=0). `FPSR.EnemyTarget`은 런 중 **0.25초마다 디렉터가 덮어씀**(무용) → 밀도 실측은 **`FPSR.SpawnEnemies N`**(캡 우회·6m 링). ⚠️ **밀도는 시간이 아니라 파티 레벨로 오름** → `FPSR.AddXP`로 올릴 것. ⚠️ **`DA_RunSchedule`의 `AliveCountByLevel` 마지막 앵커가 실질 상한**(`FPSRRunDirectorSubsystem.cpp:162`가 앵커 있으면 시간램프 무시) — **이 값 확인 필수**(이전 캡처가 100에서 멈춘 이유일 가능성). 메모리 `enemy-swarm-measurement-gotchas`.
  - 캡처는 **`csvprofile start`/`stop` 대신 `csvprofile frames=N`** — 엔진이 스스로 EndCapture하므로 0바이트가 구조적으로 불가(`CsvProfiler.cpp:3785-3792`). **CSV 프로파일러는 월드가 아니라 엔진 전역이라 PIE를 꺼도 캡처가 안 멈춤** = `Profile(20260715_140507).csv` 0바이트의 정체.
  - **분석 스크립트 = `Scripts/s4-check-capture.py`**(인자=CSV 경로, 불변식 위반 시 non-zero exit).
- **계측 잔여(의도적 미착수, 회귀 아님)**: `RenderRecencyFrames` 3→2 · 히칭 시 시간창 폭발 클램프(200ms 프레임→600ms 창) · `Near15m`가 3D인데 tier 패스는 2D(`DistSquaredXY`)라 U7 2층서 불일치 · **4인 PIE CSV 컬럼 충돌**(`CSV_CUSTOM_STAT` 키에 월드 구분자가 없어 호스트+클라3이 같은 열을 덮어씀 → 4인 게이트 시 **필수 선결**, 수정=스탯명에 `GetPlayInEditorID()` 접미사 or 클라별 별도 프로세스) · 캡처 시 `r.AllowOcclusionQueries` 검증(0이면 ③b가 조용히 그냥 프러스텀 카운트가 됨).
- **톤다운 값 미정**(사용자 판단 대기). 노브 22개를 `PP_Synthwave_Grade` 한 곳에 집약해둠(값 보존 = 화면 무변화). **⚠️ 엔진 소스 대조 결과 용의자 4개 중 3개는 순정 UE5.7 기본값**(`0.03`/`8.0`/`bias 1.0`/`shoulder 0.26` — `Scene.cpp:500,501,445`, `SceneView.cpp:180-182`). **실제 저작된 과잉값은 `BloomIntensity 1.6` 하나**(기본 0.675의 2.37배). Extended Luminance Range = **꺼짐 확정** → Min/Max는 EV100 아닌 생 휘도. 권고 = **bias 1.0→0**(min==max 고정 경로에서 리셋 안 됨 = 영구 2배 과노출, `Scene.cpp:490/506/513/545`) · **min/max→1.0**(엔진 공인 "fake manual", `Scene.cpp:724`) · **bloom 1.6→0.7** · **`AutoExposureMethod`는 Histogram 유지**(Manual로 바꾸면 `GreyMult` 0.18→1.0으로 **5.56배** 튐, `Scene.cpp:500`) · shoulder는 **그대로**(기본값·범인 아님). **적용 전 `showflag.VisualizeHDR 1`로 클램프 방향 10초 확인 필수** — min에 붙었으면 1.0 고정=어두워짐(해결), max면 **더 밝아짐(악화)**. 고정 후 밝기 조절은 auto를 다시 열지 말고 **min==max 유지한 채 W만** 조절(`ExposureScale=1/W`).
- ⚠️ **`FPSRCharacter.h`의 S2a 잔재(BlueprintReadOnly 3줄)가 이 세션 중 사라짐**(워킹트리·HEAD 양쪽 0). `BP_FPSRPlayer.uasset`은 여전히 미커밋 수정 상태 → **그 BP가 저 노출에 의존했다면 컴파일 실패 가능**. 확인 필요(LFS 바이너리라 코드로 검증 불가).
- 파일럿 규칙("아트 통과 전 콘텐츠 커밋 0")은 이미 깨진 상태 — `b25db2ab`가 `L_GameFloor.umap` + DevBlockout 머티리얼을 main에 커밋했고 throwaway 격리 경로(`_SyntyPilot/`)도 아님.

### ④ 미커밋 콘텐츠 (= 사용자 작업으로 남김, 커밋하지 않음)
```
 M Config/DefaultEditor.ini                                (S0 블록아웃 툴 설정)
 M Content/Assets/Characters/BroBot/VAT/M_BroBot_VAT.uasset  ★ VAT A포즈 버그 수정 (아래)
 M Content/Character/Enemy/BP_EnemyMeleeBase.uasset
 M Content/Character/Player/BP_FPSRPlayer.uasset
 M Content/Game/Data/DA_EnemyRoster.uasset
 M Content/Game/Data/DA_RunSchedule.uasset
 M Content/Maps/L_GameFloor.umap                            ★ 엠블럼 충돌 제거 + 톤다운 노브 집약
 M Content/Weapons/DataTable/DA_Weapon_Rifle.uasset
```

### ⑤ 이 세션 콘텐츠 수정 2건 (내가 MCP로 한 것 — 커밋은 사용자 판단)
- **`M_BroBot_VAT`: VAT A포즈 고착 근본 수정.** `bUseMaterialAttributes=True`인데 `MF_BoneAnimation.Result`가 루트 **MaterialAttributes 핀이 아닌 BaseColor 핀**에 연결돼 있었음 → `Material.cpp:7134`가 MA 핀만 컴파일하므로 **WPO가 상수 0** → 정점셰이더 死코드 제거 → 스태틱 메시가 베이크된 A포즈 그대로. **수정 = Result→MP_MATERIAL_ATTRIBUTES**. 검산: MI **VS instr 148→572, 정점텍스처 샘플 6→20**(Epic 정상본 894/23). 사용자 PIE 확인 = 걷기 시작 + BodyColor 빨강 정상 출력. 되돌리기 `git checkout HEAD -- Content/Assets/Characters/BroBot/VAT/M_BroBot_VAT.uasset`. (BaseColor 핀은 연결 잔존 — Python에 해제 API 없음, MA 모드라 컴파일러가 안 읽어 무해)
- **`L_GameFloor`**: ① `Plaza_Emblem` 충돌 제거 → 플로우필드 바닥앵커가 47(엠블럼)→**40(dais)**로 교정, 지면(0)·슬래브(10)·dais(40)·커버(40) **전부 직접 지면시드 통과**, 지면↔dais가 "경사로 오인" 우연통과가 아닌 **진짜 40cm 단차**로 열림. ② `PP_Synthwave_Grade` 톤다운 노브 22개 오버라이드 ON(값 보존).

**완료·머지·푸시 (3개 아크 종결)**
- **무기 조립툴 개편 + 파츠 스택/스탯 진화 + 저격 스코프 위젯 아크** — `--no-ff` main 머지 `fd5ed792`(→ origin/main과 통합 머지), Codex 머지리뷰 통과+교정 `6999ff3c`, `phase/pwas-b-procedural-weapon-motion` 삭제. 폴리모픽 PartRules→**파츠별 스택 진화(순수 struct)** + **스탯 임계 트리거**, 조립툴(고정소켓 안정id·진화패널·단계 뷰포트배치·트리거/스탯/스코프 편집·순서이동), **스코프 오버레이=사이트별 위젯 BP**(리티클 텍스처 폐지). 사용자 정상작동 확인. **남은=사용자 콘텐츠 → 대부분 완료(2026-07-19 재확인)**: ✅라이플 저격 진화 재저작 = 커밋·푸시(`ce1181ff` — `DA_Fragment_Rifle_SniperScope`·`DA_CardModifiers_SniperScope` 신규) / ❌**사이트별 스코프 오버레이 WBP만 미저작**(`Content/UI/` 전체에 스코프 오버레이 위젯 0건 확인). 코드는 `FPSRRunHUDWidget.cpp:156` `UpdateScopeOverlay`가 사이트 지정 클래스 → HUD 폴백 순으로 찾으므로 **위젯이 없어도 조준 확대는 정상, 오버레이 그림만 안 뜸**(회귀 아님). 후속(pre-existing)=데디서버 파츠 게이팅(Codex F3, spawn_task). 상세=메모리 `weapon-modular-evolution-scope-plan`.
- **P8 U 연속필드 다중맵 아크 (P-0~P-H)** — `--no-ff` main 머지 `34b5eea`, L_U_Whitebox 콘텐츠 `1906d56`, `phase/p8-multimap-tier0` 삭제. Tier-0 NetCull = 대칭 거리컬 교전버블 한계까지(Option A, `NetCull` 균일 사이징); 진짜 공간 relevancy(seam pop-in 제거) = RepGraph 별도 후속.
- **반동 CrystalRecoil 어댑터 아크 (P0~P4)** — `--no-ff` main 머지 `6f1a981`, 死코드 정리 `2c91ab7`·머지게이트 교정 `afa73dc`, `phase/recoil-crystalrecoil` 삭제. 확산 = 단일소스 heat 모델(`GetHeatSpread`), 무기별 `RP_*` 반동 패턴 저작.

**✅ Synty 셀/툰 아트 파일럿(U21) = 완료** (사용자 판정 2026-07-18 · `S2a→S0→S1→S3→S4` 전 단계 통과)
- **읽을 것**: `Docs/SyntyArtPilot_Scoped_ResumePrompt.md` + `Docs/SyntyArtPilot_S1_CityBuildGuide.md`(§7 MapId 함정). ⚠️ 구 `Docs/SyntyArtPilot_ResumePrompt.md`·`Docs/AssetReplacement_Synty_ResumePrompt.md`는 **폐기본**(SRS를 "최후 폴백 유료옵션"이라 하는 등 SSOT와 모순).
- **S1**: `L_GameFloor` 264m 2×2 섹터(볼륨 1개 (0,0,0)·264m·**132×132=17,424셀**, 상한 40,000 대비 여유). 단차 walkability 통과.
- **S4**: 사용자 정성 판정("다음 유닛 넘어갈 만한 퍼포먼스")으로 통과. ⚠️**정량 수치는 리포 어디에도 기록되지 않았다** — 남은 유일한 perf 수치는 파일럿 이전의 "Custom Depth 패스 1.33ms"(2026-07-10)뿐. **U22 전후 회귀 판정 기준선이 없다** → U22 첫 단계 = 현 상태 캡처(계측 인프라 `UFPSREnemyMetricsSubsystem` + CSV 5스탯은 이미 있음).

**⚠️ U21에서 U22로 이월된 미결 제약 (파일럿 산출이 아니라 다음 유닛의 선결조건)**
- **SRS stencil 규약 미확정**: 셀 `MI_SRS_BASE_CelShader`는 stencil **1~255**를 요구하는데 맵에 stencil≥1인 프리미티브가 **0개**(`L_GameFloor.umap`에 `CustomDepthStencilValue` 0회 = 실측). 아웃라인 `M_SRS_Outline01`은 stencil **0~0**이라 **적만** 받는다(적 Mesh `renderCustomDepth=True, stencil=0`). 즉 **셀↔아웃라인이 stencil 0에서 상호배타** — 둘 다 받으려면 적 stencil을 1로 올리고 아웃라인 마스크도 1~255로 넓혀야 한다. `r.CustomDepth=3`(`DefaultEngine.ini:33`)은 이미 켜져 있음.
- 🚨 **이것이 U22 공수를 지배한다**: 규약을 먼저 확정하지 않고 환경 메시 수백 개에 `render_custom_depth`/stencil을 일괄 적용하면 **전량 재작업**이다. U22 순서 = **stencil 규약 확정 → 그 다음 물량**.
- **톤다운 값도 여전히 미결**(아래 §③ 참조) — 룩 기준선 없이 맵을 저작하면 첫 맵부터 재작업 후보.

**📌 살아있는 백로그 / 이월 (회귀 아님)**
- **사이트별 스코프 오버레이 WBP 미저작**(콘텐츠 1건) — 무기 조립툴 아크의 유일한 잔여. 코드 시임 ✅(`FPSRRunHUDWidget.cpp:156` `UpdateScopeOverlay` = 사이트 지정 클래스 → HUD 폴백). 없어도 조준 확대는 정상, 오버레이 그림만 안 뜸. ⚠️**U22에서 무기가 Synty Military로 교체되므로 사이트 에셋이 바뀐다 → U22 이후 저작이 재작업 없음**.
- **RepGraph spatial-grid relevancy** — 별도 후속 페이즈(per-acquire NetCull 반경으로 적 재-bucket). plan `Docs/Review/20260707-plan-continuous-field-arch.md` §2-4/§4 D3 · Performance §5. 클라 seam pop-in = 문서화된 Tier-0 한계(D3 수용).
- **NetCull 튜너블**: `NetCullWeaponRangeCm=10000`(무기사거리 floor)·`NetCullSeamMarginCm=4000`. 단일맵 = pre-P-H 200m 바이트동일.
- **애니메이션 콘텐츠 저작 (진행 중)** — U15(1P무기)/U19(3P팀원)/U20(적VAT) 코드 인프라 ✅, 콘텐츠 미저작. 가이드 `Docs/AnimationPass_ContentGuide.md`. ⚠️ Synty Blu+PWAS 피벗이 손저작 AnimBP를 대체할 수 있음 → 파일럿 결과 후 확정.
- **반동 잔여 (PvE 코스메틱)**: ADS 확산배수 `bIsAiming`(ServerSetAiming RPC 지연) 플릭샷 미세 불일치 · 예측거부 샷 클라 heat 드리프트(cooldown 흡수) · 레거시 블룸 orphaned 저장값(로드 시 무시, 무해) · Shotgun/Bazooka 고정 확산 · ChargeLaser base-only + 커스텀 차징 램프(의도).
- **원거리 적 / 피드백 후속**: 원격 클라 총알 시각예측 미구현(A3) · 원거리 경고 생산자(`ClientNotifyRangedTarget`) 배선 미완(B1, 현재 디버그 `FPSR.TestRangedWarn`만).
- **성능 정량**: §5 적500 정량 측정 보류 → 하드캡 잠정값 유지(Performance §5). **계측 수단은 2026-07-15 확보**(`UFPSREnemyMetricsSubsystem` + `csvprofile start/stop` → CSV 열에서 P50/P90). 종전 "측정 코드 전무"는 해소.
- **플로우필드 콘텐츠 계약 (2026-07-15 실측 확립, 맵 저작 시 필독)**: 장애물 판정 박스가 **셀바닥+60cm부터** 시작(`ObstacleProbeZ=120`/`HalfHeight=60`) → **60cm 미만 물체는 플로우필드가 못 봄**. 통행 게이트는 `ClimbableStepHeight=45`. 따라서 **≤45=밟고 넘음 / ≥60=돌아감 / 45~60=함정**(못 오르는데 장애물로도 안 잡혀 적이 낌). **커버로 스웜을 쪼개려면 ≥60cm 필수** — 현 `Cover_0~3`은 40cm(발목)이라 엄폐 기능 0. 메모리 `flowfield-cover-height-45-60-band`.

---

## ✅ 완료 이력 (요약 — 상세는 `git log <hash>` / Game.md)

> 세션 단위 핸드오프는 정리했다(git = 아크별 `--no-ff` 머지 커밋 + doc-sync 커밋으로 완전 재구성 가능). 여기엔 아크 단위 요약만 남긴다. 정리 직전 스냅샷 = 태그 `progress-pre-cleanup-20260713`.

### 최근 아크 (2026-06 ~ 2026-07)
- **무기 전면 개편** `3adc945` — 점사 프래그먼트화 · 유탄 제거 · 기관단총(SMG) 추가 · 전면 투사체화(ChargeLaser·근접 제외).
- **FPSR Data Editor** P0 `57270c5` · P1 `c4e0d77` · P2 `3fb7da6` — magnitude 티어/산술 bulk · 라우팅 누수 검증기 · 미션 스케줄 타임라인 편집(에디터 데이터 편집·검증 툴).
- **코스메틱 Tick 정리** `e059a83` — MissionOrb 死Tick 제거 · XPPickup 클라 no-op · BossHUD 체력바 이벤트 전환.
- **통합 애니메이션 패스 A/B/C** (`phase/p6-animation-pass`) — 1P무기·3P팀원·적VAT+보스스켈 코드 인프라(콘텐츠 저작 = 위 백로그).
- **U12 진실 크로스헤어 + U17 설정** `36cf3d4` — 파라메트릭 크로스헤어 + 색/두께/크기 설정.
- **U11a 멀티플레이 루프** `b3b364e` — 세션 서브시스템 · Seamless 트래블(로비→게임→보스→로비) · 로비 콘텐츠.
- **U18b 무기 해금** `78b1bb5` · **U18c 행동훅 + GAS-native 회복** `f02536a` — 해금 오퍼/추첨 라우팅 · 무기 행동훅(OnAim/Fire/Miss/Kill) · 흡혈/체력재생 패시브.
- **U4 보스 콘텐츠** `71c9bde` · **U3a 약점 부위 데미지** `89b535b`(범용 `UFPSRWeakpointComponent`).
- **U9 DBNO + MP 넷코드 Phase 1A/1B** `e38dfbe` · **U7 멀티레이어 2층 플로우필드** `8d8e232` · **U5 원거리 적** `cd7de43`.
- **오디오 설정 MVP** `747a9b2` — 마스터 볼륨(SoundClass/Mix + GameUserSettings).
- **룸 기반 점진 개방 스폰 + P4-C 무기 콘텐츠** `d285c69` — `AFPSRDoor`/`AFPSRSpawnRoom` · 누적 스폰존.

### 초기 슬라이스 (P0 ~ P4-D · ~2026-06-11)
- **무기 발사/프리즈 메커니즘 하드닝 (브랜치 `fix/weapon-fire-freeze-hardening`, 2026-06-10, PIE 검증 후 머지 대기)** — 콘텐츠(무기 DA Sniper 등) PIE 테스트 중 발견 → 무기 시스템 전수감사(서브에이전트 6영역+Opus 직접검증). ① **반동 케이던스**: 단발 무기 쿨다운 중 클릭 시 실탄은 서버 거부인데 로컬 반동만 적용되던 버그 — 클라 게이트를 `NextFireReadyTime`(절대시각, 서버 `ServerNextAllowedFireTime`과 동일 모델)로 신설, FireOneShot마다 `Now+1/FireRate` 스탬프. ② **근접 GA 프리즈 게이트 누락**(칼 홀드 중 카드선택 프리즈 진입 시 데미지) → 형제 GA 패턴 추가. ③ **재장전 프리즈 누수**: `bRunPaused`는 논리 bool이라 실시간 `FTimerHandle`이 안 멈춤 → `StartReload` 프리즈 시작차단 + GameState `OnRunStateChanged` 구독해 `FTimerManager::PauseTimer/UnPauseTimer`로 진행 타이머 일시정지(잔여시간 보존, 서버 전용). ④ **`ServerDash` 서버측 프리즈 게이트 누락** → 추가(`ServerEquipSlot`/`ServerStartChargeLaser`와 대칭). ⑤ **무기 교체 연타로 케이던스 우회**(equip이 게이트를 0으로 리셋) → **최소 교체 쿨타임** `EquipFireCooldown`(0.2s, EditDefaultsOnly): equip 시 게이트=`Now+쿨다운`, 클라 `OnWeaponEquipped`로 동일 적용(이전 무기의 긴 인터벌 비상속). ⑥ **`UFPSRWeaponDataAsset::IsDataValid` 신설**(FireAbility 누락=에러, AOE 무반경/ChargeLaser ChargeTime 0/Mag 0=경고) — 무기 DA 작성 가드. 빌드+스모크 통과, **반동/프리즈/교체는 PIE 검증 대기**. 메모리 `freeze-gate-client-server-symmetry`. (Game.md §2-2/§2-4-1/§2-10)
- **카드 풀 무기별 소유 + 타깃 무기 귀속 (main 머지, 2026-06-10, `phase/p4-card-weapon-pools`)** — ThisWeapon 카드가 항상 "장착 무기"에 적용돼 칼 들고 레벨업 시 탄약/연사 카드가 칼에 박히던 꽝 문제 해소. **오퍼가 소스 무기를 들고 다니게**: `FFPSRCardDraw.TargetWeapon`(서버가 추첨 시 세팅, 클라 위조 불가). ① `GatherCandidatePool`=중앙 풀(Character+AllWeapons, target=null) + 보유 **모든** 무기의 `WeaponCards`(ThisWeapon→그 무기 target, (card,target) 디듀프). ② `ApplyCard` ThisWeapon=`GetCurrentInstance`→`GetInstanceForWeapon(TargetWeapon)`(미보유=거부 안티치트, null=장착 폴백 하위호환). ③ `DrawWeaponModifierOffer`(미션보상)=장착 1정→**보유 전 무기** AvailableModifiers 통합(각 인스턴스 스택여유)+소스 target, 셔플 최대 3(부족 시 그만큼). ④ `UFPSRWeaponInventoryComponent::GetInstanceForWeapon` 헬퍼. Codex P3(디버그 캐시 TargetWeapon 보존) 교정. **콘텐츠 마이그레이션 완료**(중앙 풀=Character+AllWeapons, ThisWeapon→각 무기 WeaponCards, fragment 리네임). 빌드+스모크+Codex 통과. **후속(계획)**: 카드 UI 소속 무기 아이콘+이름(Game.md §2-4-1, `Icon` 필드+위젯 바인딩). (Game.md §2-3/§2-4-1)
- **무기 DA 아키타입별 조건부 노출 (main 머지, 2026-06-10, `a25b491`)** — `FFPSRWeaponStatBlock`이 전 아키타입 스탯을 항상 노출하던 편집 번잡 해소. `Archetype`을 `BaseStats`로 이동(+`GetArchetype()` 게터, FireComponent 7곳 전환)→구조체 필드 `EditConditionHides`로 자신 참조(PelletCount→Shotgun/MaxPenetration→Sniper/Projectile→AOE/Charge→ChargeLaser/Melee→Melee, BurstCount→FireMode==Burst, ADS→bHasADS). 런타임 무변경. **콘텐츠 영향**: 필드 이동으로 Knife Archetype 리셋→Melee 재설정(완료). 빌드+스모크 통과. (구현 Haiku/검증 Opus)
- **A3b ChargeLaser (main 머지, 2026-06-10, `phase/p4c-aoe-charge`, A3 분할 2/2)** — 레이저=히트스캔(§2-10)+차징(hold-to-charge, release-to-fire), 차징량 alpha가 데미지 스케일하는 **관통 빔**. **Opus 직접 구현**(서버권위 RPC, 메모리 `haiku-delegation-security-wiring`). ① 스탯 ChargeTime+ChargeFullDamageMultiplier. ② `UFPSRWeaponFragment::ModifyChargeTime` 훅. ③ `UFPSRWeaponFireComponent` 차징 cadence: StartFiring ChargeLaser 분기(즉발 없이 로컬 ChargeStartWorldTime 스탬프), StopFiring(비권위만 로컬 cosmetic 활성화), `ServerBeginCharge`/`ServerReleaseCharge`(서버권위 활성화), `ResetCharge`. ④ `AFPSRCharacter` RPC **`ServerStartChargeLaser`+`ServerReleaseChargeLaser`**(둘 다 Character채널 Reliable=ordered, ADS 패턴). ⑤ **`UFPSRGA_WeaponFire_ChargeLaser`**(신규, **LocalOnly**): 프리즈/ammo/firerate 게이트+fragment PreFire/ModifyChargeTime/OnHitActor/PostFire, **alpha=clamp((now-Start)/ChargeTime)**(각 머신 자기시각: 클라=로컬·서버=권위), 읽고 ResetCharge(이중소비 방지), 데미지=Damage×Lerp(1,FullMult,alpha)×글로벌×크릿, **관통=Visibility 벽거리(폰 무시)+Pawn 멀티트레이스 벽 이내 전원**, hit-marker 1회. **Codex 게이트 교정(2026-06-10)**: [P1] 차징 시작/릴리즈 교차채널 레이스 → release를 ordered Character RPC로 보내 서버 직접 활성화(GA LocalOnly로 GAS 자동전파 제거, 호스트 이중활성화는 비권위만 로컬활성화로 차단). [P1] 무기교체 차징 뱅킹 → equip시 ResetCharge(서버 EquipSlot+클라 OnRep). [P2] 레이저 벽판정 object-type(WorldDynamic 투사체 오인)→Visibility 채널. **콘텐츠 보류**: `DA_ChargeLaser`, 차징 머티리얼·VFX·HUD 게이지. **후속**: alpha→range/pierce 스케일, 풀차징 자동발사, 클라예측 cosmetic. 빌드 Succeeded+스모크 Success+Codex 클린+Opus 세밀 자기비판. (Game.md §2-10/§2-4-1)
- **A3a AOE 투사체 발사 GA (main 머지, 2026-06-10, `phase/p4c-aoe-charge`, A3 분할 1/2)** — A1 투사체 코어 위에 발사 GA만 추가(발사 어빌리티 grant=`FPSRWeaponInventoryComponent` generic GiveAbility → **FireComponent/grant 무변경**, AOE=FireMode=Single). ① `FFPSRWeaponStatBlock` 투사체 스탯 5종(ProjectileSpeed/ProjectileGravityScale/AOERadius/ProjectileLifetime/ProjectilePierce, 기본값=무회귀). ② `UFPSRWeaponFragment::OnProjectileSpawn(FFPSRFireContext, FFPSRProjectileParams&)` 훅(빈 기본값). ③ `UFPSRWeaponDataAsset::ProjectileClass`(콘텐츠 BP_Rocket, null=base 폴백). ④ **`UFPSRGA_WeaponFire_Projectile`**(신규): 히트스캔 GA 구조 미러 — 프리즈/ammo/firerate 게이트 + fragment PreFire/ModifyShotCount/**OnProjectileSpawn**/PostFire 재사용, 라운드별(=ShotCount, 라운드당 1탄약) 뷰포인트에서 `FFPSRProjectileParams` 구성(SpreadDegrees면 VRandCone 부채꼴) → **서버권위** `AcquireProjectile`(클라=nullptr, cosmetic 예측 후속), 데미지=Damage×글로벌배수 베이크(impact 서버권위). **Codex 게이트 교정(2026-06-10)**: [P2] 고정 100cm 머즐 오프셋이 근접 벽 너머 스폰(얇은 커버 관통) → 뷰포인트→머즐 Visibility 트레이스로 벽면 클램프. **비포함(후속)**: 클라예측 cosmetic 스폰, 투사체 impact 히트마커/per-impact 크릿/`OnHitActor`, 머즐 소켓. **콘텐츠 보류**: `BP_Rocket`(AFPSRProjectile 상속+메시/VFX), `DA_Bazooka`/`DA_Grenade`(Archetype=AOE, FireMode=Single, FireAbility=GA_Projectile, ProjectileClass=BP_Rocket). 빌드 Succeeded+헤드리스 스모크 Success+Codex 클린+Opus 자기비판. (Game.md §2-10/§2-4-1)
- **A2 Hitscan 3종 (main 머지, 2026-06-10, `phase/p4c-hitscan`, 코드 선행 백로그 #2)** — 점사·스나이퍼·샷건 = **단일 Hitscan GA 스탯 구동**(아키타입 베이스, GA 스왑/태그 분기 회피 §2-4-1). ① `FFPSRWeaponStatBlock`에 `PelletCount`(샷건 산탄, 기본1)·`MaxPenetration`(스나이퍼 관통, 기본1) 추가 — 기본값=기존 무기 무회귀. ② `FPSRGA_WeaponFire_Hitscan` **라운드×펠릿** 리팩터: fragment `ShotCount`=라운드수(라운드당 1탄약), 라운드마다 `PelletCount` 펠릿 발사 → 샷건=1탄약 N펠릿(멀티샷=라운드당 1탄약과 자연 합성, 라이플 PelletCount=1 동일 경로). ③ 관통(`MaxPenetration>1`): **벽거리=Visibility 트레이스(폰 무시 리스트)** + 적=`ECC_Pawn` 멀티트레이스 거리순, 벽 이내 최대 N마리 데미지. 데미지 적용을 람다로 단일트레이스/관통 공용화. 관통 카운트는 권한 무관, 크릿 히트별 독립 롤, hit-marker 활성화당 1회. ④ **Burst·Sniper-Single·Auto복구는 `UFPSRWeaponFireComponent`가 이미 처리 → 코드 변경 없음**. **Codex 게이트 교정(2026-06-10)**: [P2] 관통 벽판정 object-type(WorldDynamic 투사체 오인)→Visibility 채널(폰 무시 리스트). 검증: 빌드 Succeeded+헤드리스 스모크 Success+Codex 클린+Opus 자기비판. **콘텐츠 보류**: DA_BurstRifle/DA_Sniper/DA_Shotgun. **후속**: `EFPSRWeaponStat`에 PelletCount/MaxPenetration 카드 축. (Game.md §2-4/§2-4-1)
- **A1 투사체 코어 (main 머지, 2026-06-09, `phase/p4c-projectile-core`, 코드 선행 백로그 #1)** — A3(AOE/유탄)·B1(원거리 적) 공유 의존 범용 베이스. ① `AFPSRProjectile`(Sphere+`UProjectileMovementComponent` 결정적 이동+코스메틱 메시[미할당], `bReplicates`+ReplicateMovement): 서버권위 충돌→데미지 브릿지 **재사용**(Player→`UFPSREnemyHealthComponent::ApplyDamage` / Enemy→`AFPSRCharacter::ApplyContactDamage`), 단일타격+관통 또는 **AOE**(`OverlapMultiByObjectType(ECC_Pawn)`=대시 i-frame 무관), `IsHostileTarget` 팀판정(친화 폭발·instigator 차단). ② `UFPSRProjectileSubsystem`(서버권위 풀+**≤64 동시 복제캡** FIFO 강제회수, 클래스 매칭 재사용, 디버그 `FPSR.SpawnProjectile`). ③ **글로벌 프리즈 준수**(§2-2): `FTickableGameObject` 전환검출→활성 투사체 PMC정지+수명타이머 Pause, 충돌핸들러 `IsRunFrozen` 게이트, freeze프레임 월드충돌 stuck은 resume 지연임팩트로 해소. ④ 생명주기 하드닝: `bActive` 재진입 가드+멱등 해제(이중풀등록 차단), `FellOutOfWorld`→풀 회수(pending-kill 오염 차단)+`IsValid` 전수, `SetUpdatedComponent` 재연결(StopSimulating 후 정지 방지), Lifetime≤0 클램프, 점블랭크 초기오버랩 순서. **설계결정**: 미검증 cosmetic mode 제거 → A1=결정적 PMC(**예측준비**)+서버권위 데미지+풀/캡, **클라예측 로컬 cosmetic 스폰은 A3 발사 GA 책임**(Game.md §2-10 의도는 결정적 이동으로 충족). **알려진 한계**(미미): 프리즈 *전환 프레임*의 폰 오버랩 1건 드롭(1프레임·무크래시). **콘텐츠 보류**(미작성): 투사체 메시/VFX·무기 DA/BP·발사 GA(A3). 빌드+헤드리스 스모크(Success)+**Codex 11R 하드닝→클린** 통과. (Game.md §2-10/§5)
- **적 스폰포인트 코드 (main 머지, 2026-06-09, `phase/p4-enemyspawnpoints` 코드분)** — ① **디자이너 배치 스폰포인트** `AFPSREnemySpawnPoint`(Weight/ZoneTag/MinPlayerDistance/bEnabled) + `UFPSREnemySpawnSubsystem` 전 플레이어 비가시(FOV)+거리 가중랜덤 선택(후보 0 시 링 폴백, 미배치 맵 동작), 디자이너 지점 ground-snap 생략(권위 Z 보존), `SetActiveSpawnZone` 훅(TimeGate 후속). ② **플로우필드 장애물 마스크/BFS 라우팅 + 적 중력/지면추종**(`AFPSREnemyBase`, 경사/계단 보정). **Codex 5R 하드닝**: 디자이너 Z 보존(실내 천장 스냅 방지)→월드 밖 추락 KillZ 회수(슬롯 누수)→접촉 데미지 수직 게이트(바닥 관통)→동일 스폰지점 중첩을 **분리(separation) 동일위치 결정적 골든앵글 푸시**로 근본 해소(지터 전량 제거, 스폰 위치 비이동, 맵 비의존)→최종 클린. 빌드+스모크 통과. **콘텐츠 배치(L_Sandbox 스폰포인트+BP_EnemyBase 정렬, `d3a68c6`)도 머지 완료**(코드+콘텐츠 = `phase/p4-enemyspawnpoints` 완전 머지). **후속(C1)**: 플로우필드 셀 클리어런스(좁은 통로 과차단)+멀티레벨 높이 인지. (Game.md §5-2)
- **P4-D (main 머지, 2026-06-09)** 게임필/피드백 — ① **PickupRadius/XPGain** 어트리뷰트(`UFPSRCombatSet` 승수, 기본 1.0)+XP 픽업 배선(자석 대상=플레이어별 유효반경 거리비 최소, 협동 정합)+카드 2종(Instant·Add·SetByCaller). ② **런상태 HUD** `UFPSRRunHUDWidget : UCommonUserWidget`(BlueprintPure 게터+`OnRunStateUpdated` BIE, 이벤트기반; 픽 카운팅은 레벨업=즉시카드선택이라 제거)+**Game레이어 컨테이너** `UFPSRGameHUDWidget : UCommonActivatableWidget`(입력설정 소유; XPBar 위젯/클래스 폐지·RunHUD로 일원화). ③ **로컬 피드백** `UFPSRPlayerFeedbackComponent`(비복제·이벤트형)+PC Client RPC: **히트마커**(서버권위 Hit/Crit/Kill, 활성화당 1회, Unreliable), **피격 방향**(CoD식 `ApplyContactDamage`→오너클라 카메라기준 각도), **원거리 타겟 사전경고**(다수소스 id별 TMap·각도배열·추적Tick·Reliable; 생산자=원거리 적 AI 후속, 디버그 `FPSR.TestDamageDir`/`FPSR.TestRangedWarn`). **설계 정제**: 근접/사각지대 *시각* 위협 제외→사운드 이전. Codex 다회 하드닝(협동 자석/호스트 클럭/늦은복제 바인딩/확산발산→서버권위/제어상실 클리어/Unreliable·Reliable/다수소스/전방선언). 콘텐츠: 카드 GE·DA 2종+CardPool, WBP GameHUD/RunHUD/HitMarker/ThreatIndicator, BP_FPSRPC 배선. 빌드+스모크+Codex+2-client PIE 통과. **후속**: 히트마커 최종 연출(크로스헤어/발사체 후), 원거리경고 생산자 배선, 핑/Gibs/사각오디오. (Game.md §2-11/§2-14)
- **P4-B-2 (main 머지, 2026-06-08)** 무기 행동 Fragment — 합성형 발사 훅. `UFPSRWeaponFragment : UPrimaryDataAsset`(무상태, 동작=C++ 서브클래스 virtual 훅 `PreFire/ModifyShotCount/OnHitActor/PostFire`, 수치=DataAsset) + 레퍼런스 2종(`UFPSRFragment_MultiShot{ExtraShots}`, `UFPSRFragment_OnHitBonusDamage{BonusDamage}`). `FFPSRFireContext`(plain struct) / `UFPSRWeaponInstance.ActiveFragments[]`(복제 참조)+`AddFragment`(MaxStacks 스택제한)/`GetFragmentStackCount`/`HasFragment` / `GA_WeaponFire_Hitscan` 훅 배선(PreFire→ModifyShotCount=NumShots 루프→히트당 OnHitActor→PostFire) / `UFPSRCardDataAsset.GrantedFragment`(ThisWeapon→AddFragment) / `UFPSRWeaponDataAsset.AvailableModifiers[]` / `DrawWeaponModifierOffer`(스택 여유분 셔플) + ApplyCard fragment 분기 / 디버그 `FPSR.GrantMissionRewardPick`. **마무리 세션(2026-06-08)**: ① 카드 작성 EditCondition 가드(Scope별 무관 필드 숨김) + `GetCardFamilyKey` Character-scope 게이트(stale GE 패밀리 누수 차단). ② 미션보상 카드 UI(`UFPSRCardEntryWidget`): fragment는 등급 대신 카테고리 라벨(`FragmentCategoryText`, WBP override)+수치 빈칸. ③ **MultiShot 펠릿당 탄약 소모**(잔량 클램프, 최소 1발). ④ Fragment **`MaxStacks` 중첩**(중복 누적, 훅 스택마다 적용 — MultiShot 2스택=3발). 콘텐츠: Fragment DA 2종+Fragment 카드 2종+카드 `Card/`이동+Rifle AvailableModifiers+CardPool. 빌드+스모크+Codex(다회 하드닝, 최종 클린)+PIE 통과. (Game.md §2-4-1 ②)
- **P4-B-1 (main 머지)** 무기 스탯 모디파이어 기반 — 런타임 컨테이너 `UFPSRWeaponInstance`(UObject 등록형 복제 서브오브젝트: Source DA + ThisWeapon `Modifiers` + 탄약/리로드 + 해석 스탯 lazy 캐시, Push Model) 신설. 인벤토리 `Slots[]` 인스턴스화(탄약·리로드 병렬배열 → 인스턴스 응집), `AFPSRCharacter`·인벤토리 컴포넌트 `bReplicateUsingRegisteredSubObjectList=true`. 스탯 해석 = `BaseStats × 누적(ThisWeapon[인스턴스] + AllWeapons[`AFPSRPlayerState::AllWeaponsMods`, 리스폰 생존])`, 발사 3곳(FireComp·Hitscan·Melee)+탄약 `GetResolvedStats()` 배선. `ApplyCard` weapon-scope 실적용(ThisWeapon→인스턴스, AllWeapons→PlayerState, 무기 없으면 거부), `UFPSRCardDataAsset.WeaponStat/WeaponStatOp`+IsDataValid, DrawCards 범용풀 weapon-scope 합류(무기 보유 시), 프리즈 중 `ServerEquipSlot` 차단(ThisWeapon 타깃 결정성). 디버그 `FPSR.DumpWeaponStats`. 카드 magnitude 표시 `+%.0f`→퍼센트/소수 수정. 재장전 중 반동 큐 지속 버그 수정(`!CanFire()` 플러시). 콘텐츠: 무기 스탯 카드 6종(연사/탄창/반동×ThisWeapon/AllWeapons)+풀. 빌드+스모크+Codex 3R+PIE 통과. (Game.md §2-4-1 ①)
- **P4-A (main 머지, 재설계)** 런 흐름 — **라운드제 폐지 → 레벨업 전역 프리즈**. `AFPSRGameState.bRunPaused`(복제, 페이즈독립)+`RefreshPauseState`(전원 보류픽 기준 프리즈/재개)+`AddSharedXP` 즉시 프리즈. `ERunPhase`=Combat/Boss. 프리즈 게이팅(스폰·적이동/공격 동결·플레이어 입력/속도·발사GA). 오퍼 일반화 `EFPSROfferType{OpeningSeed,LevelUp,MissionReward}`+`MissionRewardPicksPending`+`ApplyCard` 타입별(weapon-scope 수락·소비, GE적용 P4-B). `UFPSRRunDirectorSubsystem`(런클럭+시간 미션스케줄 `FFPSRMissionEvent`+`BossTime`+시간 스폰스케일링, 오프닝홀드, 보스>미션 우선). 미션 프레임워크(`AFPSRMissionActor`+`UFPSRMissionDataAsset`+`AFPSRMission_HoldZone`+`AFPSRMissionSpawnPoint` 태그매칭가중랜덤)+클리어 즉시 프리즈 보상. 적 클래스 설정화(`BP_Enemy` via GameMode). 콘텐츠(미션태그/L_Sandbox 스폰포인트/GameMode/BP_Enemy/DA_RunSchedule/미션DA·BP/존데칼) 동반. Codex 다회(라운드종료적정리·폰전스폰·거리폴백·중복바인딩크래시·스폰홀드·보스미션유실) 하드닝. 빌드+스모크+PIE 통과. (Game.md §2-1/2-2/2-7/2-8)
- **P3-D (main 머지)** 카드 UI/공유XP바/오프닝시드 — CommonUI 인프라(`CommonUI`/`CommonInput`/`UMG` 모듈, `UFPSRGameViewportClient`, `DefaultEngine.ini` ViewportClient, 경량 `UFPSRPrimaryGameLayout`=4 레이어 스택) + `UFPSRXPBarWidget`(OnRep 델리게이트 이벤트기반, 폴링 없음) + `UFPSRCardSelectWidget`/`UFPSRCardEntryWidget` + PC RPC 배선(서버 캐시+인덱스+offer nonce 검증, 클라는 인텐트만). **설계 변경: 레벨업 스택=공유 카운터 → 플레이어별 `AFPSRPlayerState::CardPicksPending`**(4인 협동 정합, Game.md §2-2). breather 진입/AddXP 시 서버 자동 발급. 디버그 `FPSR.OpeningSeed`/`FPSR.RequestCards`(권한 보유 시). Codex 7라운드로 보안(클라 임의카드/무한리드로/리롤악용)·정합(nonce/지연바인딩/데드락) 하드닝. 빌드+스모크 통과.
- **P3-C** 카드 시스템(main 머지) — `UFPSRCardDataAsset`(`RarityTiers` 등급별 수치) + `UFPSRCardPoolDataAsset` + `UFPSRCardSubsystem`(등급 가중 비복원 추첨/`CardFamily` 디듀프/`ApplyCard` 레벨업 게이트/`TryReroll`). 리롤=PlayerState(플레이어별 3). `Luck` 단일 행운축(RarityBonus 폐지). 수치=`SetByCaller`(태그 `SetByCaller.CardMagnitude`). `IsDataValid` 검증. 최대체력 증가=현재체력 동반증가(서버권위). Character 카드 콘텐츠 5종+풀+GE(`Content/Cards/Character/`). PIE 확인됨. (Game.md §2-3)
- **P3-B** XP 픽업+자석 — `AFPSRXPPickup`(서버 자석 이동·수령) + `UFPSRPickupSubsystem`(cap 150, 초과 시 XP 직접가산). 적 사망 시 드롭.
- **P3-A** 런 상태(GameState 호스팅) — `AFPSRGameState`에 `SharedXP/PartyLevel/PendingLevelUps/RunPhase`(Push Model). 레벨업=스택 누적(프리즈 없음 §2-2). Breather 시 스폰·공격 게이팅.
- **P2** 적 대량화(main 머지) — `UFPSREnemySpawnSubsystem`(풀링+SpawnDirector, 하드캡 500) + `UFPSRFlowFieldSubsystem`(BFS flow-field+separation) + 거리 LOD(Significance 티어/NetUpdateFreq) + 이속 ±10% 편차 + 적 근접데미지·공격토큰·i-frame + 충돌무시 대시(+IA_Dash 콘텐츠). (Game.md §5)
- **P1.5** 사격/이동 감각 — 사격코어(FullAuto 연사/반동="복구 빚"모델/확산·블룸) + 탄약·재장전(MagSize/R, **예비탄 무한**) + ADS(FOV/확산/반동 배율) + 반동 ADS의존(힙 산탄/ADS climbing). `FPSR.RecoilPreview`. (Game.md §2-4-2)
- **P1** Net-aware 1P 슬라이스 — `AFPSRCharacter`(1P 카메라+Separated Arms+EnhancedInput) + `Weapon/`(3슬롯 서버권위 인벤토리, Push Model) + 발사/근접 GA(히트스캔·구체오버랩) + `AFPSREnemyBase`(경량 Pawn)+`UFPSREnemyHealthComponent` 데미지 브릿지. 코드리뷰 하드닝(서버 cadence 검증). 사용자 BP 3종+무기 DA+IA 셋업 완료.
- **P0** 경량 C++ 스캐폴드 — UE5.7, 플러그인 enable, GameplayTags(`Config/DefaultGameplayTags.ini`), 빌드+스모크 테스트(`FPSRoguelite.Smoke.ModuleLoads`).
- **문서/리뷰 인프라** — `Game.md`(SSOT) + `PROGRESS.md` 체계. 외부 AI 문서리뷰=`GameConfirm.md`(§10), Codex 코드리뷰=`Scripts/codex-review.ps1`→`Docs/codex-reviews/`(gitignore, §6-6), 컨설팅 토론=`Docs/ConsultLoop.md`/`/consult`→`Docs/Review/`(프롬프트 매니저 인입).

---

## 빌드 / 검증 방법
- 빌드(에디터 닫고 · **현 코드 빌드 대상 클론 = FPSRoguelite2**; 양 클론 공유 문서라 경로 중립 표기 — 빌드하는 클론의 `.uproject` 사용): `"D:\UnrealEngine\UE_5.7\Engine\Build\BatchFiles\Build.bat" FPSRogueliteEditor Win64 Development -Project="<작업 클론>\FPSRoguelite.uproject" -WaitMutex`
- 헤드리스 스모크: `UnrealEditor-Cmd.exe <uproject> -unattended -nopause -nullrhi -nosplash -nosound -ExecCmds="Automation RunTests FPSRoguelite.Smoke.ModuleLoads" -TestExit="Automation Test Queue Empty" -abslog=...`
- Codex 리뷰: `powershell -File Scripts\codex-review.ps1 -Uncommitted`(작업트리) / `-Base main`(브랜치 diff). 결과 `Docs/codex-reviews/`.
- 새 UCLASS 다수면 Live Coding 불가 → 풀빌드. IA 에셋 생성은 `Scripts/gen_input_assets.py`.

## 확정 사항 / 주의점 (운영)
- 모델 정책: **구현=Sonnet 위임 / 검증(빌드·diff·스모크·Codex·UI)=Opus 직접**(§6-5, 2026-07-02 위임 기본 Haiku→Sonnet 5 전환). 각 P단계 `phase/` 브랜치→검증→`--no-ff` 머지→브랜치 삭제(§6-7).
- 프로덕션 원칙: 콘텐츠=BP/DataAsset/config, **C++ 경로 하드코딩 금지**. 엔진 API는 소스 grep 후 사용(§6-3). 검증 없이 "완료" 보고 금지.
- **MCP(unreal) 인증 실패로 미사용** → UBT 빌드 + 헤드리스 자동화로 검증. UI는 사용자 PIE 확인.
- UE5.7 IMC 매핑은 Python 미반영 → 에디터 수동. 디버그/플레이스홀더(전환 대상)는 Game.md §8 인벤토리 참조.
- Phase 종료 시 해당 Phase 사용자 콘텐츠 동반 커밋 여부를 사용자에게 물을 것(메모리 규칙).
