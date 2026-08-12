# 재개 프롬프트 — 인게임 맵 교체 (Synthwave City Kit → `L_Map1_City`)

> ## ✅ 이 작업은 2026-08-12 완료됐다 — 이 문서는 더 이상 재개 지시가 아니다.
> **정본 = `Docs/WorkLog.md` 최상단** ("인게임 맵 교체 완료"). 확정 배치값·실측치·함정이 전부 거기 있다.
> 아래 §3 "배치용 실측치"는 **폐기된 값**이다(사용자 레벨 편집으로 지형이 7×9·Z=-50 → 10×11·Z=200 으로 바뀜).
> 이 문서는 당시 기록으로만 보존한다.

> 최초 2026-08-11. **현행화 2026-08-12** — P1 + SSOT 전파 완료, 잔여 = 레벨 배치(사용자 담당).
> 이어서 할 머신이 바뀐다(2026-08-12 사용자). **핸드오프 SSOT = PM 보드 행의 `## 진행 로그 — 2026-08-12` 섹션**이고
> 이 문서는 그 요약본이다. 충돌하면 보드가 정본.

---

## 0. VibeUE MCP

```
Claude  --http-->  프록시 127.0.0.1:8089/mcp  --http-->  VibeUE 플러그인 127.0.0.1:8088/mcp (에디터 프로세스 내부)
```

| 구성요소 | 실체 |
|---|---|
| 플러그인 | **Fab 설치본, 엔진 레벨** — `<UE>/Engine/Plugins/Marketplace/Untitleddd272512e3f0V1/` · VibeUE **4.0** · 바이너리 빌드 포함 |
| 프록시 | `Scripts/vibeue-proxy.py` — `Scripts/start-vibeue-proxy.bat` 로 기동 |
| MCP 등록 | `claude mcp add --scope user --transport http vibeue http://127.0.0.1:8089/mcp` |

> 🔎 플러그인 폴더명이 `Untitleddd272512e3f0V1`(Fab 난독화)이라 이름으로 검색하면 안 나온다.

**🔑 API 키가 필요하다 (2026-08-12 실측).** `claude mcp list` 가 ✓ Connected 여도, 키가 없으면 **모든 `tools/call` 이**
`"A valid VibeUE API key is required"` 로 거부된다 — 프록시 릴레이는 정상이고 플러그인 단에서 막는 것이라 포트·연결만 보면 진단이 안 된다.
키 발급 = vibeue.com/login (무료). **자격증명이라 에이전트가 대신 입력하지 않는다 — 사용자가 넣어야 한다.**

**세션 시작 전**: ①언리얼 에디터를 먼저 켠다 ②프록시 기동(`.\Scripts\start-vibeue-proxy.bat`, 중복 실행 안전)
③그 다음 Claude 세션 시작. 재부팅하면 프록시는 자동으로 안 뜬다.

**대체 경로**: 에디터를 닫고 커맨드렛 — `UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script="<파일>" -unattended -nopause`.
단 Slate 를 건드리는 API 는 즉사한다(`Docs/Troubleshooting.md` D7). 블록아웃 "레벨 검증"은 순수 C++ 클래스
(`FFPSRBlockoutValidator`, UFUNCTION 아님)라 **파이썬에서 못 부른다 — 에디터 버튼으로만 돌아간다.**

---

## 1. 완료된 것 (커밋됨, 브랜치 `content/map1-synthwave`)

| 커밋 | 내용 |
|---|---|
| `dca19b4e` | **P1** — 키트 임포트(`Content/Synthwave_city/` 223파일) · `Map_CyberCity.umap` 삭제 · `L_Map1_City.umap` 신규 · `DefaultGame.ini` 의 `RunMap`(37행)·`MapsToCook`(54행) 교체 · 블록아웃 팔레트에 `/Game/Synthwave_city` 등록 |
| `0105e7ca` | **SSOT 전파** — `Game.md` §9 · `Roadmap.md` §7-6(a′)·§8 에서 `Map_CyberCity` → `L_Map1_City`, "Fab 임포트 블로커" 해소 · 키트 격자 실측 근거 · `Troubleshooting.md` **G9** 신규 |
| `c435fe88` | 사용자 레벨 편집 — 액터 1개 삭제 |

**보드**: 행 "인게임 맵 교체 — Synthwave City Kit 데모맵 기반 재구축"
(https://app.notion.com/3b83972ddd88819197bce3ee11664ab1) — 상태=**진행중** · 담당=`FPSRoguelite@content/map1-synthwave · 0811-맵교체`
· 브랜치=`content/map1-synthwave`. **클레임은 이미 돼 있다 — 다시 클레임하지 말 것.**

### 사용자 결정 3건 (재론 금지)
1. **RayTracing 되돌림** — `DefaultEngine.ini` 에 혼입돼 있던 `r.RayTracing=True` · `r.SkinCache.CompileShaders=True` 제거.
   RT 전역 on 은 동적 프리미티브마다 가속구조를 매 프레임 갱신해 "적 200-300 저비용"(제1원리)과 반대이고,
   적은 VAT(비스켈레탈)라 SkinCache 이득이 0이며, M0 (b) 베이스라인을 오염시킨다. 룩은 Lumen(SW)+이미시브로 유지된다.
2. **아레나 = 지면 전체 7×9(315×405m)** — 맵을 먼저 고정해야 스웜·캐릭터 이동속도 튜닝이 가능하고,
   그 후 템포가 이상하면 그때 맵을 줄인다. (제기된 우려 = 균등랜덤 스폰 + 적 250cm/s 라 아레나가 크면
   스웜 상당수가 항상 이동중이라 교전밀도가 낮다. 사용자가 인지한 뒤 유지 결정.)
3. **세부 배치는 사용자 담당** — 별도 지시 없으면 에이전트는 레벨 배치에 손대지 않는다.

---

## 2. 잔여 작업

1. **레벨 배치 (사용자)** — 필수 배치물 5종 + 세부 프롭
2. 블록아웃 탭 **"레벨 검증"** 0건까지 (가드레일 6종 · Message Log `FPSRBlockout`)
3. **PIE 스모크**
4. `Docs/WorkLog.md` 맨 위에 상세 경위
5. 보드 행 마감(커밋 해시 기입)
6. `--no-ff` 로 main 머지

> 🔑 **프롭 밀도는 이번 작업 단위 안에서 확정한다.** 나중에 얹으면 ①플로우필드 장애물 마스크 ②드로우콜/프레임 예산이
> **둘 다** 바뀌어 M0 (b) 성능 베이스라인 실측이 무효가 된다(§7-6 M0 (a′) 가 (b) 의 선행인 것과 같은 논리).

---

## 3. 배치용 실측치 (2026-08-12, 에디터 조회로 확정)

| 항목 | 값 |
|---|---|
| 모듈 격자 | **4500×4500cm**, 코너 피벗 — `SM_floor`/`_02`/`_03` · `SM_road` · `SM_module_road_02`/`_03`/`_04` 전부 동일 |
| 지면 타일 배치 | `X = -15410 + 4500i` (i=0..6) · `Y = -17210 + 4500j` (j=0..8) · `rot yaw=-90` · `scale 1` · `actorZ=-250` |
| 지면 범위 | X `-15410..16090` · Y `-21710..18790` = **315×405m**, 윗면 **Z=-50** 평탄 · **7×9 완전 연결(구멍 없음)** |
| 플로우필드 볼륨(권장 AABB) | 중심 `(340, -1460)` · 반extent `(15750, 20250)` → **158×203 = 32,074 / 40,000셀**, 축당 ≤256 ✅ 기본 셀 200cm 로 전역 커버, `CellSizeOverride` 불요 |
| 적 스폰포인트 Z | **+50** (바닥 -50 + 100 · 캡슐 반높이 90) |
| GameMode | WorldSettings 미지정 → `GlobalDefaultGameMode = BP_FPSRGameMode` 상속. **config 가 SSOT 라 그대로 두는 게 맞다** |
| MapId 태그 | 전부 **Default(미설정)** — 단일맵. 태그를 붙이면 멀티맵 전용 경로가 깨어난다 |
| 콜리전 감사 | 키트 메시 58개 중 **46개 보유**. 미보유 12 = `SM_SkySphere` · `SM_sun` · `SM_advertisement_01~10` — 전부 공중/배경이라 장애물 마스크에 불필요. 구조물(건물·바닥·도로·계단·나무)은 전부 보유 |

**볼륨 주의**: 축정렬 유지(회전하면 AABB 가 커진다) · 정확히 1개(0=원점 폴백, 2+=단일맵 모호).
바닥 앵커는 **박스 상단 +100 에서 하향 트레이스**(`FPSRFlowFieldSubsystem::DetectFloorZForVolume`)라 박스 중심 XY 위에
떠 있는 콜라이더가 있으면 원점이 그리로 잡힌다 → "레벨 검증" 가드레일 #6 이 이걸 잡는다. **아직 미검증.**

**`PlacementGridSize` = 250 유지.** 4500 = 18×250 이라 250 으로도 모듈이 격자에 정확히 얹히고, 프롭 배치엔 250 이 훨씬 낫다.
45m 모듈만 대량으로 깔 구간에선 4500 으로 바꾸면 타일링이 무조건 맞는다(근거는 `Config/DefaultEditor.ini` 주석).

---

## 4. 🪤 이 작업에서 실제로 밟은 함정

**`get_actor_bounds` 로 지면/층을 판정하지 말 것** (전문 = `Docs/Troubleshooting.md` **G9**).
액터 바운즈 상단으로 "지면 vs 고가"를 갈랐다가 *"Y 한 행이 통째로 뚫린 45m 협곡"* 이라 오판하고 없는 구멍에
바닥 타일 11칸을 깔았다. 실제로는 `BP_road_02_C` 에 붙은 **가로등**이 액터 바운즈를 607 까지 올린 것이고
(도로 메시 자체는 상단 Z=-50), `BP_building_C` 안에도 `SM_floor` 가 들어 있었다. **이 맵엔 2층도 고저차도 없다.**
→ 지면 판정은 **컴포넌트 단위로** — `comp.get_world_location().z` + 그 컴포넌트 메시의 `get_bounds()`.

**에디터 오토세이브 때문에 "저장 안 했으니 안전"이 성립하지 않는다.** 위 오탐 타일이 오토세이브로 `.umap` 에
기록됐다(264,696 → 290,778바이트). 복구 = `git checkout -- Content/Maps/L_Map1_City.umap` **직후 에디터에서
그 레벨을 변경사항 버리고 다시 열기** — 안 그러면 메모리에 남은 액터가 다음 저장 때 되살아난다.

---

## 5. 사용할 도구 (이미 구현돼 있다 — 새로 만들지 말 것)

`Source/FPSRogueliteEditor/.../Blockout/` — 에디터 탭 **"FPSR 블록아웃 툴"**
- 팔레트(폴더별 카드) · "선택 배치"(카메라 앞) · **"뷰포트 배치"**(심시티식 고스트, 격자·바닥 스냅, 좌클릭 연속배치, `[`/`]` 회전, ESC 종료)
- "상태 검사" — 팔레트 전 에셋 WorldStatic 콜리전 배지
- **"레벨 검증"** — 가드레일 6종(콜리전·지면·스폰Z·볼륨·셀예산·중심) → Message Log `FPSRBlockout`
- "선택→프리팹" — 선택 액터들을 경량 `BP_*` 로 묶어 팔레트에 즉시 등록(서브레벨 없음)
- 설정 = Project Settings > FPSR > FPSR Blockout (= `Config/DefaultEditor.ini`)

## 6. 읽을 문서

- `Game.md`(허브) + `Docs/SSOT/Workflow.md` §6 — 브랜치·검증·보드 프로토콜
- `Docs/SSOT/Roadmap.md` §7-6 M0 (a′) · §8 인벤토리
- `Docs/SSOT/Performance.md` §5-2 (플로우필드 2층 인지)
- `Source/FPSRogueliteEditor/Public/Blockout/FPSRBlockoutValidator.h` — 가드레일 6종 정의
- `Source/FPSRoguelite/Public/Enemy/FPSRFlowFieldComputer.h:262-266` — 셀 예산 상수
