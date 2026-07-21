# U22a-A — 건물 생성 툴 (에셋 선택 + 미리보기) · 재개 프롬프트

> 갱신 2026-07-21 (2차). **Phase 1(설정 DataAsset + config 기반 생성기) = 코드/빌드/커밋 완료.**
> 남은 것 = **① DA 인스턴스 생성(사용자 10초) · ② 스모크 테스트 · ③ EUW 플로팅 패널(Phase 2)**.
> 선행 배경: `Docs/U22_AssetReplacement_Prompt.md`(§3 A). 이 문서가 최신·유일 실행 프롬프트.

---

## §0 세션 시작 체크리스트

1. **에디터를 먼저 켜고**(맵 = `Map_CyberCity`) **세션을 시작한다.**
   🔴 **MCP는 세션 시작 시점에만 붙고, 도중에 에디터를 껐다 켜면 그 세션에선 영구히 안 붙는다**(2026-07-21 실측 — C++ 빌드하려 종료 후 재기동했더니 VibeUE 도구 전부 소실). → **C++ 빌드가 필요한 작업은 반드시 세션을 쪼갤 것**(세션A=코드+빌드+커밋 / 세션B=에디터 저작).
2. 작업 맵 = `/Game/Maps/Map_CyberCity`. ⚠️ `L_GameFloor`는 별도본.
3. 스크린샷 API가 안 먹음 → **룩 확인은 사용자 스크린샷**에 의존.
4. Python 재로드: `import sys,os,unreal; sys.path.append(os.path.join(unreal.Paths.project_content_dir(),"Python")); import importlib,fpsr_citygen; importlib.reload(fpsr_citygen); fpsr_citygen.register_menu()`

---

## §1 ✅ 완료 (커밋 `40679a86` → `5562dd54` → **`ec54e5a3`**, 브랜치 `phase/u22a-environment`)

**방향**: 통짜 빌딩 메시 ❌ → 모듈 조각 조립 ✅ → 프리셋 폐기, 사용자가 에셋 직접 선택 ✅
→ **에셋 피커가 모든 메시를 보여줘 규격 밖을 고르면 건물이 어긋나던 문제**를 **수집기(Kit) + 배열 프루닝**으로 차단 ✅(2026-07-21 개편).

- ✅ **C++ 2클래스** (`Source/FPSRoguelite/{Public,Private}/CityGen/`, `UPrimaryDataAsset`·`FPSROGUELITE_API`)
  - **`UFPSRCityGenKit`**(신규) = **자동 수집 목록**. 6종 배열 `Facades`/`Corners`/`Doors`/`RoofFloors`/`CorniceTrims`/`RoofProps`. 손으로 안 채운다.
  - **`UFPSRCityGenConfig`** = **이 건물에 쓸 것**. 같은 6종 **배열** + `Kit` 참조 + `RoofPropCount`/`Width`/`Depth`/`Floors`(0=박스에서 유도)/`bSetback`. `IsDataValid`가 빈 카테고리를 이름 찍어 경고.
  - **왜 Kit과 Config를 나눴나**: 재수집해도 사용자 프루닝이 안 날아가게(사용자 결정 2026-07-21).
  - **왜 전부 배열인가**: 여러 개 남기면 섞여 나오고, 하나만 남기면 고정. 단일 값이면 규격 밖을 아무거나 고를 수 있었다.
  - **동작 규칙**: 파사드·옥상소품 = **칸/개수마다 무작위**, 코너·문·코니스·지붕 = **건물당 하나 뽑아 일관되게**.
- ✅ **`Content/Python/fpsr_citygen.py`**
  - `_measure`(로컬 바운드 실측) / `_classify`(이름→카테고리) / `_validate`(카테고리별 규약 검증).
  - `collect_modular_meshes()` — `SCAN_FOLDERS` 재귀 스캔 → 분류·검증 → **Kit에 기록** + **채택/탈락 사유 로그**.
  - `fill_config_from_kit()` — Kit 6배열 → Config 복사(+`Kit` 참조 설정). 이후 사용자가 프루닝.
  - `generate_from_config()` — 생성 시 풀 재검증, 규격 밖은 `제외: <이름> — <사유>` 로그 후 제외(**무음 드롭 폐지**).
  - `find_config_asset()`/`find_kit_asset()` — 경로 우선, 실패 시 **클래스로 전역 검색 폴백**(폴더 옮겨도 안 깨짐).
  - `preview_from_config()`/`confirm_preview()`/`clear_preview()` — 태그 `CityGenPreview`·폴더 `Buildings/_Preview`·라벨 `_Preview` 접미.
  - **메뉴 8종** `Tools > FPSR CityGen`: 1 Collect / 2 Fill Config from Kit / 3 Open Config / 4 Place Sizing Box / 5 Preview / 6 Confirm / 7 Clear / 8 Bake.
- 🔎 **검증 기준(카테고리별 — 실측 전이라 일부러 느슨함)**
  | 카테고리 | 기준 |
  |---|---|
  | facades / doors | 폭 **정확히 1칸(250)** + 피벗 밑변왼쪽. 500폭(2칸)은 `멀티칸 미지원` 사유로 제외 |
  | corners / cornices | **한 칸(250) 안에 들어가고 바닥에 앉을 것**만. ⚠️250 배수 규칙을 걸면 얇은 장식이라 **전멸**(기본값조차) |
  | rooffloors | 가로·세로 **125 배수** |
  | roofprops | 검사 없음(장식) |
  → **첫 Collect 로그가 이 메시들의 최초 실측 데이터**다. 기준이 현실과 안 맞으면 그 숫자 보고 `TOL`/규칙을 조정하는 게 정상 절차(실측 전 과한 제약 금지).
- ✅ 기존 규약 유지: 모듈 **250 가로 × 300 층높이**, 피벗 밑변 왼쪽 +X, 콜리전 **BlockAll**(플로우필드 `ECC_WorldStatic`) + `custom_depth_stencil=1`(셀룩).
- ✅ **빌드 검증 2회**: `FPSRogueliteEditor Win64 Development` → `Result: Succeeded`.
- ✅ **A-0 perf 베이스라인**: `Saved/Profiling/CSV/Profile(20260720_194245).csv` — 드로우콜 **920**·GPUTime 12ms·적200중 VisibleRendered **196**.

---

## §2 ▶ 다음 작업

### ⓪ 🔴 첫 실행 결과 (2026-07-21 세션 끝, **여기서 이어서 시작**)
**진전**: `Collect` ✅ 동작 — Kit `Facades`에 **11종**이 썸네일까지 정상 수집(`Wall_Window_01~06,08` · `Old_Single_01` · `Base_Wall_Window_01/Double_01/Half_01`).
`Preview` ✅ 실행됨 — **벽이 층별로 정렬돼 쌓임 = 조립 로직 자체는 동작**(사용자 스크린샷).

**증상**: 미리보기 건물 **위에 불투명한 회색 직육면체 덩어리**가 얹혀 있어 형태를 가린다.

**의심 1순위 = 사이징 박스가 안 치워짐**(코드 근거): `preview_from_config()`는 박스를 **삭제도 숨김도 하지 않는다**
(구 `generate_from_selection()`은 생성 후 `destroy_actor(box)`로 소비했음). `place_sizing_box()`가 만드는 것은
`/Engine/BasicShapes/Cube`(기본 회색 머티리얼, 750×750×1200) → **회색 덩어리의 색·형태와 일치**.

**⚠️ 단, 미확인이다**(에디터 미연결로 실측 못 함). 2순위 후보 = `RoofFloors`에 두꺼운/큰 메시가 뽑혀
`P(rooffloor, ix*125, iy*125, zr, 0)` 격자로 겹쳐 쌓인 것. **먼저 아래 3개를 확인한 뒤 고칠 것**:
1. 회색 덩어리를 **클릭 → Outliner에서 액터 이름** 확인 (`SizingBox`인가, `Building_Cfg_*`의 자식인가)
2. **Output Log에서 `[CityGen]`** 줄 — `생성: WxDxF, 조각 N` + `제외:` 경고 목록
3. Kit의 **`Corners` / `CorniceTrims` / `RoofFloors`** 배열에 각각 몇 개가 들어왔는지(0개면 검증 기준이 과한 것)

**대기 중인 수정(Python 전용 — 재빌드 불요, 리로드만 하면 적용)**:
`preview_from_config()`에서 사용한 사이징 박스를 `box.set_is_temporarily_hidden_in_editor(True)`로 숨기고,
`clear_preview()`/`confirm_preview()`에서 다시 보이게 되돌린다(라벨 `SizingBox*` 대상). 헬퍼 `_set_sizing_boxes_hidden(hidden)` 하나로.
→ 숨김이라 Clear하면 박스가 돌아와 크기를 다시 조절할 수 있다(삭제보다 나음).

### ① DA 인스턴스 2개 생성 (각 10초 · 콘텐츠 브라우저)
**우클릭 → Miscellaneous → Data Asset** → 클래스 선택 → 이름 지정. 위치는 둘 다 **`/Game/Tools/CityGen/`**
(툴 에셋은 `/Game/Tools/` 아래로 모으는 규칙 — 사용자 결정 2026-07-21).

| 클래스 | 에셋 이름 | 용도 |
|---|---|---|
| `FPSRCityGenKit` | **`DA_CityGenKit`** | 수집기가 채움(**신규 — 아직 없음, 꼭 만들 것**) |
| `FPSRCityGenConfig` | **`DA_CityGenConfig`** | 실제 사용할 것(이미 생성됨, 커밋 `055b0733`) |

⚠️ 경로가 달라도 `find_kit_asset()`/`find_config_asset()`이 **클래스로 전역 검색해 폴백**하므로 폴더를 옮겨도 동작한다(상수는 맞춰두는 게 정석).

### ② 스모크 테스트 (Phase 1 검증) — 🔴 실행 검증 미완, 여기부터가 첫 실제 실행
1. **`1. Collect Modular Meshes`** → 로그를 **꼼꼼히 읽는다**(이게 최초 실측 데이터):
   - 카테고리별 채택 수 + 예시 메시의 실제 size
   - `제외` 목록과 사유 — 기준이 현실과 안 맞으면(예: 코너가 0개, 파사드가 몇 개뿐) **여기서 `_validate`/`TOL`을 조정**하고 재수집. 재수집은 Kit만 덮으므로 안전.
2. `2. Fill Config from Kit` → Config에 후보가 채워짐.
3. `3. Open Config` → **안 쓸 메시를 삭제**(프루닝). 코너/문/코니스/지붕은 하나만 남기면 고정.
4. `4. Place Sizing Box` → 크기 조절(가로 250·층 300 단위로 반올림).
5. 박스 **선택한 채** `5. Preview from Config` → 로그 `[CityGen] Building_Cfg_* 생성: WxDxF, 조각 N` 확인 + `제외:` 경고 확인.
6. 조각 개별 이동/교체 확인 → `6. Confirm Preview` → 필요시 `8. Bake Selected`(로그 `ISM N개`).
7. 실패 시 확인 포인트: **Kit 에셋 미생성**(수집기가 경고함) / 박스 미선택(→ cfg의 W·D·Floors 또는 3×3×4) / 파이썬 리로드 안 함(§0-4).

### ③ Phase 2 = EUW 플로팅 패널 (남은 본 작업)
**`EUW_CityGen`** = 얇은 셸: **DetailsView(`DA_CityGenConfig` 바인딩)** + 버튼 `[Place Box][Preview][Confirm][Bake]`.
버튼은 `unreal.PythonScriptLibrary.execute_python_command`로 `fpsr_citygen.*()` 호출.
🔴 **위험**: 컨테이너 위젯(자식 WBP 임베드) 프로그래매틱 compile/save = **모달행+크래시+`.uasset` 손상 전례**(`WBP_GameHUD`, [[vibeue-render-target-gpu-hazard]]). → **append-only·비대화 저장·자주 저장**, 위험하면 **셸만 만들고 GUI 배선은 사용자와 협업**. DetailsView 1개+버튼 몇 개는 중첩이 얕아 상대적으로 안전.
⚠️ VibeUE `create_blueprint`는 **DataAsset 부모로는 행/실패**한다(UDataAsset이 Blueprintable 아님 — 그래서 C++로 간 것). Widget/Actor 부모는 정상.

---

## §3 콘텐츠 계약 (생성 시 준수)
- 모듈 250×300, 피벗 밑변왼쪽 +X. 벽 per-edge: 앞 yaw0 / 오른쪽 yaw90(앵커 W*250) / 뒤 yaw180(앵커 (i+1)*250) / 왼쪽 yaw270(앵커 (j+1)*250). 코너 4모서리 yaw 0/90/180/270.
- 조각 콜리전 = **BlockAll(ECC_WorldStatic)** + stencil 1. 옥상 프롭은 NoCollision 가능.
- 250폭 벽만(가드 있음). 500폭(Window_07·Old_01·Old_02) 쓰려면 2칸 배치 지원 추가 필요.
- `unreal.Rotator(a,b,c)` = **(roll,pitch,yaw)** → 반드시 `unreal.Rotator(yaw=Y)` 키워드. attach는 **Movable** 필요(편집=Movable, Bake=Static).

## §4 검증 / 완료 (A-5)
1. 편집→**에디터 재시작→PIE**: `FPSR.SpawnEnemies 200` → 적이 건물 피해 전역 추격, `no-floor`·45~60 함정 0 (**R1: Bake ISM 콜리전 실증 = 최대 리스크**).
2. perf: §1 베이스라인 대비 회귀 없음(드로우콜·GPUTime, VisibleRendered↓).
3. 블록아웃 검증기 `FFPSRBlockoutValidator::ValidateLevel` + 앵커 커맨드릿.
4. 사용자 육안 게이트.

## §5 상태 / 커밋
- ✅ **커밋됨** `40679a86`: `Content/Python/{fpsr_citygen,init_unreal}.py` + `Source/FPSRoguelite/{Public,Private}/CityGen/FPSRCityGenConfig.{h,cpp}`.
- **미커밋(육안 게이트 후)**: `Content/Maps/Map_CyberCity.umap`(수정) · `Content/PolygonScifi/` · `Content/PCG/`(구 흩뿌리기 그래프=폐기후보) · `M_Parallax_Full_01`.
- 브랜치 = `phase/u22a-environment`.

## §6 범위 밖 (A 완료 후)
게임플레이 레이어 이식(룸·미션·문) → D(런 완주 → L_Sandbox 삭제 → 쿡 → main 머지). 상세 `Docs/U22a-A_PCG_ResumePrompt.md` §7.
