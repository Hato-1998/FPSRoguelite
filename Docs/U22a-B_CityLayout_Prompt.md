# U22a-B — 도시 배치 재구성 (설계 전달 → MCP 생성) · 실행 프롬프트

> 작성 2026-07-21. **선행 = U22a-A 건물 생성 툴 완료**(`Docs/U22a-A_BuildingTool_ResumePrompt.md`).
> 이 문서는 **다음 세션이 그대로 복붙해 착수**하는 실행 프롬프트다.

---

## §0 세션 시작 방법

1. **언리얼 에디터를 먼저 켜고**(맵 = `Map_CyberCity`) 세션을 시작한다.
   (VibeUE 프록시 덕에 도중에 껐다 켜도 재연결되지만, 시작 시 켜져 있는 편이 낫다)
2. 이 문서 + `Docs/U22a-A_BuildingTool_ResumePrompt.md` + `PROGRESS.md`를 읽는다.
3. ⚠️ **프록시 세션의 MCP 도구는 8개뿐**이다(`execute_python_code`·`manage_asset`·`read_logs`·
   `discover_*`·`manage_skills`·`terrain_data`·`deep_research`). 위젯/BP 전용 도구는 없다.
4. Python 재로드(재빌드 불요):
   `import importlib, fpsr_citygen; importlib.reload(fpsr_citygen); fpsr_citygen.register_menu()`

**복붙용 첫 지시문**
```
Docs/U22a-B_CityLayout_Prompt.md 를 읽고 U22a-B를 진행한다.
§3에서 도시 설계 전달 형식을 먼저 확정하고, §4 구현 → §7 검증 순서로 간다.
맵 파일은 지우지 않는다(Map_CyberCity 내부 배치만 재구성).
```

---

## §1 목표 (사용자 결정 2026-07-21)

- **`Map_CyberCity`의 맵 파일은 유지**하고, **내부 배치 구성을 새로 짠다.**
  기존에 손으로 흩어놓은 배치물을 정리하고, **사용자가 전달하는 도시 설계도대로** 재구성한다.
- **사용자가 일일이 배치하지 않는다.** 사용자는 *큰 그림(설계도)* 을 주고, **AI가 MCP로 생성**한다.
- 삭제: **`L_Sandbox`, `L_U_Whitebox`** (이제 안 씀).

🔴 **맵을 "전부" 지우는 것이 아니다.** `L_MainMenu`·`L_Lobby`·`L_Transition`은 게임 흐름
(메뉴→로비→런→결과)에 필수라 지우면 게임이 돌아가지 않는다.

---

## §2 실측 현황 (2026-07-21)

### 2-1. 맵 15개
| 맵 | 처리 |
|---|---|
| `/Game/Maps/L_MainMenu` · `L_Lobby` · `L_Transition` | **유지 필수** (게임 흐름) |
| `/Game/Maps/Map_CyberCity` | **유지 + 내부 재구성** ← 이번 작업 대상 |
| `/Game/Maps/L_Sandbox` · `L_U_Whitebox` | **삭제** |
| 팩 데모 9개(`PolygonCyberCity/Maps/*`, `PolygonMilitary/Scenes/Demo`, `PolygonParticleFX`, `ProceduralWeaponAnimationSystem/Demo/*`, `StylizedRenderingSystem/Levels/SRS_DemoMap`, `Synty/UISciFiSoldierHUD`, `_SyntyPilot`) | 판단 보류(용량 정리 대상이나 참조 확인 필요) |

**삭제 안전성 확인됨**: `L_Sandbox`·`L_U_Whitebox`는 `Config/*.ini`와 `Source/` 어디에서도 참조되지 않는다.
런 맵은 이미 `Map_CyberCity`로 전환돼 있다 — `Config/DefaultGame.ini`
`RunMap=/Game/Maps/Map_CyberCity` · `+MapsToCook=(FilePath="/Game/Maps/Map_CyberCity")`.
→ 삭제는 `manage_asset(action='delete')`로 (참조가 있으면 도구가 막아준다).

### 2-2. `Map_CyberCity`의 현재 액터 구성
```
385  StaticMeshActor          ← 손으로 흩어놓은 배치물 = 정리/재구성 대상
 38  PointLight
 16  BP_Veh_* / BP_Prop_*     ← 팩 프리팹 배치물
 13  FPSREnemySpawnPoint      ← 적 스폰 (있음)
  4  PlayerStart              ← 4인 협동 (있음)
  4  SkeletalMeshActor
  1  FPSRFlowFieldBoundsVolume ← 플로우필드 범위 (있음)
  1  PCGWorldActor            ← 폐기한 PCG 잔재
  SRS 4액터(BP_StylizedRenderingSystem / BP_SRS_Fog / CubemapReflection / LightCalibration)
  DirectionalLight · SkyLight · SkyAtmosphere · ExponentialHeightFog · PostProcessVolume
```

🔴 **없어서 런이 끝까지 안 가는 것 3종**
| 클래스 | 역할 | 현재 |
|---|---|---|
| `AFPSRMissionSpawnPoint` | 미션 생성 지점. **0개면 미션이 안 뜬다** | **0** |
| `AFPSRBossSpawnPoint` | 보스 등장 지점 | **0** |
| `AFPSRSpawnRoom` | 룸 점진 개방(밸런스 2차) | **0** |

> 종전 `PROGRESS.md`에 "게임플레이 레이어 0"이라 적혀 있었으나 **부정확**하다.
> 적 스폰·PlayerStart·플로우필드 볼륨은 **있고**, 없는 것은 위 3종이다.

---

## §3 🔴 먼저 확정할 것 — "도시 설계"를 어떤 형식으로 전달할 것인가

**이 세션의 첫 작업은 형식 확정이다.** 사용자에게 아래 3안을 제시하고 고르게 한다.

### A안. ASCII 그리드 (권장 — 직관적이고 수정이 쉽다)
```
■ 건물블록   . 도로   P 플레이어시작   M 미션지점   B 보스아레나   O 광장(빈터)

■■■.■■■.■■■■
■■■.■■■.■■■■
............
■■.OOOO.■■■■
■■.OOOO.■■■M
■■.PPPP.■■■■
............
■■■■.■■■.B■■
```
- **한 칸 = 1000유닛(10m)** 제안. 건물 모듈이 250이므로 한 칸 = 4모듈.
- 건물 블록은 `fpsr_citygen`의 **블록 모드**(`cfg['block']=True`)로 폭·높이 제각각 생성.
- 도로 폭 1칸(10m)은 적 200~300이 몰리기엔 좁을 수 있다 → **2칸(20m)** 을 기본으로 검토.

### B안. 파라미터 (빠르지만 통제력이 낮다)
`격자 8×8 / 블록 3×3칸 / 도로 폭 2칸 / 층수 4~12 랜덤 / 광장 1개 / 보스 아레나 남동쪽`

### C안. 좌표 목록 (정밀하지만 사람이 쓰기 번거롭다)
`{블록: [{x, y, 폭, 깊이, 층수}, ...], 미션지점: [...], ...}`

> 확정 후 **그 형식을 파싱하는 함수**를 `fpsr_citygen.py`에 추가한다(§4-1).

---

## §4 구현할 것

### 4-1. 레이아웃 파서 + 도시 생성기 (신규)
- 설계 입력(§3 확정안) → **건물 블록 목록**으로 변환 → 기존 `generate_from_config` 재사용.
- `cfg['block']=True`로 켜면 **박스 하나가 폭·높이 제각각인 여러 채**로 나뉘고,
  맞닿는 면의 벽·코니스·코너를 층 단위로 생략한다(드로우콜 절약, 이미 구현·미검증).
- 기호(P/M/B)에서 **게임플레이 액터를 함께 배치**한다(§4-2).
- **재실행 가능해야 한다**: 같은 설계 = 같은 결과(시드 고정), 기존 생성물은 태그로 찾아 지우고 다시.

### 4-2. 게임플레이 액터 자동 배치 (없으면 런이 안 끝난다)
| 기호 | 배치할 액터 | 개수 기준 |
|---|---|---|
| `P` | `PlayerStart` | 4개(4인 협동) |
| `M` | **`AFPSRMissionSpawnPoint`** | 미션 스케줄 수 이상 |
| `B` | **`AFPSRBossSpawnPoint`** | 1개 |
| (블록 주변) | `AFPSREnemySpawnPoint` | 기존 13개 참고, 거리별 분산 |
| (맵 전체) | `AFPSRFlowFieldBoundsVolume` | 도시 전체를 덮도록 크기 재조정 |
| (선택) | `AFPSRSpawnRoom` / `AFPSRDoor` | 룸 점진 개방을 쓸 경우 |

### 4-3. 기존 배치물 정리
- `StaticMeshActor` 385개 + 팩 프리팹 = **어디까지 지울지 사용자 확인 후** 진행.
- 유지: 조명·SRS 4액터·SkyAtmosphere 등 **렌더링 셋업**(다시 만들기 번거롭고 룩이 이미 맞춰져 있다).
- `PCGWorldActor`·`Content/PCG/` = 폐기한 접근의 잔재 → 정리 대상.

### 4-4. 맵 삭제
`L_Sandbox`, `L_U_Whitebox` → `manage_asset(action='delete')`.

---

## §5 제약 · 리스크

1. **perf 예산이 제1원리다.** 적 동시 200~300을 싸게 굴려야 한다.
   - 베이스라인: 드로우콜 **920** · GPUTime **12ms** (`Saved/Profiling/CSV/Profile(20260720_194245).csv`)
   - 도시가 커지면 **Bake(ISM) 필수**. Bake는 검증 완료(콜리전 유지·인스턴스 누락 0, 66조각→ISM 14).
   - **ISM 개수는 조각 수가 아니라 메시 종류 수에 비례** → Config에서 창문 종류를 줄이면 드로우콜이 준다.
2. **플로우필드**
   - 장애물 판정 = `ECC_WorldStatic` **ObjectType 쿼리 + 채널 트레이스**(`FPSRFlowFieldComputer.cpp:1006`).
     건물 조각은 `BlockAll`이라 충족. Bake 후에도 유지됨(실증).
   - 🔴 **커버 높이 45/60 규칙**: 60cm 미만 장애물은 안 보이고, 45cm 초과는 못 오른다.
     **45~60cm는 적이 끼는 함정 구간** → 거리 장식물 높이를 이 구간에 두지 말 것.
   - 바닥 앵커는 **첫 `PlayerStart` 아래로 트레이스**해 잡는다 → PlayerStart 위에 장식물을 놓으면
     지면 시드가 오염된다.
   - 도시 전체를 덮도록 `FPSRFlowFieldBoundsVolume` 크기를 반드시 다시 잡을 것.
3. **에디터 파일 락**: 에디터가 켜진 채로는 `git checkout`이 `unable to unlink`로 실패한다.
   맵/에셋을 되돌려야 하면 에디터를 먼저 닫는다.
4. **멀티플레이 기준**: 4인 협동·서버권위가 기본선. 배치도 4인 동선을 전제로.

---

## §6 참고 — 이미 쓸 수 있는 것

**`Tools > FPSR CityGen` 메뉴 12개** + **`EUW_CityGen` 패널**(사용자가 배선 완료, 동작 확인)
```
1 Collect  2 Fill Config  3 Open Config  4 Place Box  5 Preview  6 Confirm
7 Clear    8 Bake(ISM)    9 다시 굴리기   10 층+1  11 층-1  12 조각 메시 교체
```
- 건물 규약: 모듈 **250 가로 × 300 층높이**, 피벗 밑변왼쪽, 콜리전 `BlockAll` + stencil 1
- 창문은 **수직 일관**(같은 자리는 위로 쭉 같은 창문) + **지상층 분리**(1층은 `Base_` 계열)
- 카테고리별 검증 규약과 실측값은 `U22a-A_BuildingTool_ResumePrompt.md` §1-3
- 간판은 **사용자가 직접 배치**(자동배치 안 함). 짝 규약: `sign`이 `backing` 안쪽 12.5 묻힘

---

## §7 검증 기준 (완료 판정)

1. **런이 끝까지 돈다**: PIE → 미션이 뜨고 → 보스까지 진행(현재는 미션 스폰 0이라 불가).
2. **적이 건물을 피해 전역 추격**: `FPSR.SpawnEnemies 200` → `no-floor` 0, 45~60 함정 0.
3. **perf 회귀 없음**: 드로우콜·GPUTime이 §5-1 베이스라인 대비 악화되지 않을 것(Bake 후 측정).
4. **재현성**: 같은 설계도를 다시 돌리면 같은 도시가 나온다.
5. **사용자 육안 게이트**(레퍼런스 = 좁은 거리 + 양옆 고층 + 네온 간판).

---

## §8 범위 밖 (U22a-B 이후)
게임플레이 레이어 튜닝(룸·문 밸런스) → **D**(런 완주 → 쿡 → `main` 머지).
간판·벽면 디테일은 사용자가 직접 배치.
