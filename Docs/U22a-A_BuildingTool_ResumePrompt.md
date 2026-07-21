# U22a-A — 건물 생성 툴 · 재개 프롬프트

> 갱신 2026-07-21 (3차). **Phase 1 = 실행 검증까지 완료.** 첫 실행에서 드러난 결함 8건 수정 + 창문 배치 규칙 +
> 편집 도구 3종까지 끝났고, 사용자 육안 게이트를 통과했다.
> 남은 것 = **① EUW 배선(사용자 수작업) · ② Confirm/Bake 검증 · ③ 간판(사용자가 직접 배치)**.
> 선행 배경: `Docs/U22_AssetReplacement_Prompt.md`(§3 A).

---

## §0 세션 시작 체크리스트

1. **에디터를 먼저 켜고**(맵 = `Map_CyberCity`) 세션을 시작한다.
2. 🔴 **C++ 빌드가 필요한 작업은 세션을 쪼갠다**(세션A=코드+빌드+커밋 / 세션B=에디터 저작).
3. 스크린샷 API가 안 먹음 → **룩 확인은 사용자 스크린샷**에 의존.
4. Python 재로드(재빌드 불요):
   `import importlib, fpsr_citygen; importlib.reload(fpsr_citygen); fpsr_citygen.register_menu()`
5. ⚠️ **이 세션의 VibeUE 프록시는 도구가 8개뿐**이다(`execute_python_code`·`manage_asset`·`read_logs`·
   `discover_*`·`manage_skills`·`terrain_data`·`deep_research`). 메모리에 적힌 `create_blueprint`·
   `list_variables` 같은 **위젯/BP 전용 도구는 없다**. 도구 목록을 먼저 확인하고 계획을 세울 것.

---

## §1 ✅ 완료 (브랜치 `phase/u22a-environment`)

`ec54e5a3`(수집기+Kit/Config 분리) → **`be36b78d`**(결함 8건) → **`779c8bf5`**(창문 규칙+편집 도구)

### 1-1. 구조
- **C++ 2클래스** (`Source/FPSRoguelite/{Public,Private}/CityGen/`, `UPrimaryDataAsset`)
  - `UFPSRCityGenKit` = 자동 수집 목록 / `UFPSRCityGenConfig` = 실사용 풀(+`Kit` 참조·크기·셋백)
  - 6종 배열 `Facades`/`Corners`/`Doors`/`RoofFloors`/`CorniceTrims`/`RoofProps`
  - DA 인스턴스 2개 = `/Game/Tools/CityGen/DA_CityGenKit`·`DA_CityGenConfig` (**생성·커밋 완료**)
- **`Content/Python/fpsr_citygen.py`** — `_load_pools` / `_build_one` / `generate_from_config` 분해.
  `_build_one`은 공유 Random이 아니라 **자기 시드**를 받는다(같은 시드 = 같은 조합 재현 → 편집 도구의 토대).

### 1-2. 메뉴 `Tools > FPSR CityGen` (12개)
```
1 Collect Modular Meshes (→Kit)      7 Clear Preview
2 Fill Config from Kit               8 Bake Selected (merge ISM)
3 Open Config                        9 건물 다시 굴리기 (선택 건물)
4 Place Sizing Box                  10 층 +1 / 11 층 -1 (선택 건물)
5 Preview from Config               12 조각 메시 바꾸기 (선택 조각)
6 Confirm Preview
```

### 1-3. 실측으로 확정된 규약 (2026-07-21 첫 Collect·바운드 실측)
| 카테고리 | 기준 | 채택 |
|---|---|---|
| facades / doors | 폭 **정확히 250**(1칸) + 피벗 밑변왼쪽. 500폭은 `멀티칸 미지원`으로 제외 | 11 / 4 |
| **cornices** | 폭 250 + `minX≈0`만. **Z는 자유** — `Ceiling_Trim`의 `minZ=261`은 "층 천장에 붙는다"는 뜻이라 정상 | **8** |
| **corners** | 한 칸 안 + 바닥에서 시작 + **높이 300**(층높이). `Pillar_Half_*`(150)는 층마다 빈틈 | **8** |
| rooffloors | 가로·세로 125 배수 | 5 |
| roofprops | 검사 없음(장식) | 11 |
- `_classify` 제외어: `_glass`·`beams`·`shutter`·`blockout`·**`_arm_`·`_dish_`·`_wing_`**(위성 분해 부품)·**`_45_`**(45도 코너 전용, 지붕에 깔면 사선으로 어긋남)
- 모듈 **250 가로 × 300 층높이**, 피벗 밑변왼쪽 +X, 콜리전 **BlockAll**(`ECC_WorldStatic`) + `custom_depth_stencil=1`
- 간판 규약(참고, 자동배치는 안 함): `sign`(그림, 두께 0·피벗 중앙)이 `backing`(액자) **안쪽에 12.5 묻힘**
  → 짝으로 쓰려면 `off = backing.maxY - sign.maxY + 0.5` 만큼 앞으로 밀어야 한다. 벽 바깥면 ≈ `y=-20`.

### 1-4. 첫 실행에서 고친 결함 8건 (`be36b78d`)
1. **회색 덩어리** = SizingBox(750×750×2500)가 건물을 감쌈 → Preview 중 숨김·Clear/Confirm 시 복원
2. 박스를 숨기면 선택이 풀려 2회차 Preview가 기본 크기로 떨어짐 → 레벨의 박스 자동 탐색 폴백
3. **항아리 모양** = 셋백이 반 칸(125) 들여 격자가 어긋남 → **셋백 기본 OFF** + 들여쓰기 격자 단위로
4. 지붕 타일 간격 125 고정 → 250 타일이 **4겹 중첩·875까지 돌출** → 메시 실측 크기로 간격 산출(36→9장)
5. 지붕/옥상소품 범위가 W/D 기준 → **최상층 크기(top_*)** 기준으로 통일
6. 코니스 검증이 진짜 코니스를 탈락시킴(위 1-3) 7. 반층 기둥 채택 8. 위성 부품·45도 조각 혼입

### 1-5. 창문 배치 규칙 + 편집 도구 (`779c8bf5`)
- **수직 일관**: 칸 위치를 키로 벽을 고정 → 같은 자리는 1층부터 꼭대기까지 같은 창문.
  `facade_mode` = `column`(기본)/`floor`/`building`/`random`
- **지상층 분리**: 이름에 `Base_`가 붙은 벽 = 1층용(도어도 전부 `Base_` 계열). 1층은 `Base_`만, 2층↑은 나머지만.
  - 고친 결함: `ground` 플래그를 **정면에만** 넘겨 1층인데 옆·뒷면은 상층 벽을 쓰고 있었음 → 네 면 모두 전달
- **편집 도구**: 생성 시 부모 태그에 `CityGenSize:WxDxF`·`CityGenSeed:N` 기록(자식 바운드 역산은 부정확).
  - `reroll_selected()` 같은 자리·크기로 조합만 새로 뽑기 / `change_floors(±1)` **시드 유지**라 창문 그대로
  - `cycle_piece_mesh(±1)` 선택 조각을 같은 카테고리 다음 후보로 순환(Config에 남긴 것들 안에서만)
- **블록(일괄 거리 생성)**: 코드는 있으나 **기본 OFF**(`cfg['block']`). 사용자 결정 = **한 채씩 놓아가며 거리 조성**.
  켜면 폭·높이 제각각인 여러 채로 나누고 맞닿는 면의 벽·코니스·코너를 층 단위로 생략(코너는 같은 자리라 z-fighting).

### 1-6. 검증 결과 (실측)
- 8개 층 X/Y 범위 전부 `0~750`(직사각형) · 지붕 9장 커버 750(돌출 0) · 옥상 소품 옥상 안 · 박스 `hidden=True`
- 1층에 `Base_` 아닌 벽 0 / 2층↑에 `Base_` 0 / 칸별 수직 일관 위반 0 / 도어 정면 가운데 1개
- reroll = 위치·크기 유지 + 조합 변경 / 층+1 = 겹치는 32칸 **전부 창문 동일**(시드 재현 정확)
- 조각 교체 = `Window_04→05→06→05` 순환·되돌리기 동작
- **A-0 perf 베이스라인**: `Saved/Profiling/CSV/Profile(20260720_194245).csv` — 드로우콜 920·GPUTime 12ms

---

## §2 ▶ 다음 작업

### ① EUW 배선 (사용자 수작업 — 셸은 생성됨)
**`/Game/Tools/CityGen/EUW_CityGen`** = 빈 EditorUtilityWidget. **생성·저장 완료.**

🔴 **왜 셸만인가**: 이 세션의 VibeUE 프록시엔 위젯 도구가 없고, `unreal.WidgetTree`는 Python에
**메서드·프로퍼티가 0개** 노출이라 버튼 하나 넣을 수 없다. 배선은 UMG 에디터에서 손으로 해야 한다.

**배선 순서**
1. `EUW_CityGen` 더블클릭 → Designer에서 `Vertical Box` 안에 `Button` + `Text` 배치(원하는 만큼)
2. 각 버튼 선택 → Details 하단 `On Clicked` **+** → Graph에 이벤트 생성
3. 이벤트 뒤에 **`Execute Python Command`** 노드 연결 → `Python Command` 핀에 아래 문자열을 넣는다

| 버튼 | Python Command |
|---|---|
| Collect | `import fpsr_citygen; fpsr_citygen.collect_modular_meshes()` |
| Fill Config | `import fpsr_citygen; fpsr_citygen.fill_config_from_kit()` |
| Place Box | `import fpsr_citygen; fpsr_citygen.place_sizing_box()` |
| **Preview** | `import fpsr_citygen; fpsr_citygen.preview_from_config()` |
| Confirm | `import fpsr_citygen; fpsr_citygen.confirm_preview()` |
| Clear | `import fpsr_citygen; fpsr_citygen.clear_preview()` |
| Bake | `import fpsr_citygen; fpsr_citygen.bake_selection()` |
| **다시 굴리기** | `import fpsr_citygen; fpsr_citygen.reroll_selected()` |
| 층 +1 / -1 | `import fpsr_citygen; fpsr_citygen.change_floors(1)` / `...change_floors(-1)` |
| 조각 교체 | `import fpsr_citygen; fpsr_citygen.cycle_piece_mesh(1)` |

4. (선택) 설정도 한 화면에 두려면 팔레트의 **`Details View`** 를 배치하고, `Event Construct` →
   `Load Asset`(`/Game/Tools/CityGen/DA_CityGenConfig`) → DetailsView `Set Object`.
5. 컴파일·저장 후 콘텐츠 브라우저에서 **우클릭 → Run Editor Utility Widget**.

⚠️ 컨테이너 위젯을 **프로그래매틱으로** compile/save 하면 모달행·크래시·`.uasset` 손상 전례가 있다
([[vibeue-render-target-gpu-hazard]]). 손으로 하는 건 안전하지만, 다음 세션의 AI가 이 위젯을
자동 편집하려 들지 않도록 주의.

### ② Confirm / Bake 검증 (Phase 1 스모크의 마지막)
`6. Confirm Preview` → `8. Bake Selected` (로그 `ISM N개`) 확인.
🔴 **최대 리스크 = Bake 후 ISM 콜리전**: 적 플로우필드가 `ECC_WorldStatic`을 읽으므로 ISM에 콜리전이
살아있지 않으면 적이 건물을 통과한다. Bake 후 반드시 PIE로 실증할 것.

### ③ 간판·벽면 디테일 = **사용자가 직접 배치**(사용자 결정 2026-07-21)
자동 배치는 넣지 않기로 했다. 팩 보유분(참고): `Billboard_Clean/Damaged_Sign_01~03`(1313×438) ·
`Billboard_Sign_Verticle_01`(332×823) · `Billboard_Sign_Small_01~03`(783×438) · `Holo_Sign_01~08` ·
`Poster_01~21` · `Canopy_01~06` · `Wires_01~18`. 짝 규약은 §1-3 참고.

### ④ 참고 — 뷰포트가 밝은 회색인 이유
조명 문제가 아니라 **뷰포트가 Unlit 모드**이기 때문(사용자 확인 2026-07-21). 야간 룩 판단은 Lit에서 할 것.

---

## §3 콘텐츠 계약
- 벽 per-edge: 앞 yaw0 / 오른쪽 yaw90(앵커 `W*250`) / 뒤 yaw180(앵커 `(i+1)*250`) / 왼쪽 yaw270(앵커 `(j+1)*250`).
  코너 4모서리 yaw 0/90/180/270(**중앙 피벗**).
- 조각 콜리전 = **BlockAll(ECC_WorldStatic)** + stencil 1. 옥상 프롭은 NoCollision 가능.
- `unreal.Rotator(a,b,c)` = **(roll,pitch,yaw)** → 반드시 `unreal.Rotator(yaw=Y)` 키워드. attach는 **Movable** 필요.

## §4 검증 / 완료 (A-5)
1. 편집 → **에디터 재시작 → PIE**: `FPSR.SpawnEnemies 200` → 적이 건물 피해 전역 추격, `no-floor`·45~60 함정 0.
2. perf: §1-6 베이스라인 대비 회귀 없음(드로우콜·GPUTime).
3. 블록아웃 검증기 `FFPSRBlockoutValidator::ValidateLevel` + 앵커 커맨드릿.
4. 사용자 육안 게이트.

## §5 상태 / 커밋
- ✅ 커밋됨: `be36b78d`(결함 8건) · `779c8bf5`(창문 규칙+편집 도구) · `DA_CityGenKit`·`DA_CityGenConfig`
- **미커밋**: `Content/Maps/Map_CyberCity.umap`(프리뷰 액터 포함, 확정 후) · `M_Parallax_Full_01` ·
  `Content/PCG/`(폐기 후보, 삭제 권한 막힘 → 사용자가 직접) · `Scripts/vibeue-proxy.py`(사용자 생성, 커밋 여부 미정)
- 브랜치 = `phase/u22a-environment`

## §6 범위 밖 (A 완료 후)
게임플레이 레이어 이식(룸·미션·문) → D(런 완주 → L_Sandbox 삭제 → 쿡 → main 머지).
상세 `Docs/U22a-A_PCG_ResumePrompt.md` §7.
