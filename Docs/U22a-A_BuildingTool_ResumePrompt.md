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

## §1 ✅ 완료 (커밋 `40679a86`, 브랜치 `phase/u22a-environment`)

**방향**: 통짜 빌딩 메시 ❌ → 모듈 조각 조립 ✅ → **프리셋 폐기, 사용자가 에셋 직접 선택** ✅.

- ✅ **C++ 설정 클래스 `UFPSRCityGenConfig`** (`Source/FPSRoguelite/{Public,Private}/CityGen/`)
  - `UPrimaryDataAsset` 파생 · `FPSROGUELITE_API` · 기존 프로젝트 DataAsset 규약 준수 · `IsDataValid` 경고(파사드 빔).
  - 필드 = `Facades[]`(창문벽 여러 개) · `Corner`(코너 기둥) · `Door` · `RoofFloor`(지붕 슬래브) · `CorniceTrim` · `RoofProps[]` · `RoofPropCount` · `Width`/`Depth`/`Floors`(0=박스에서 유도) · `bSetback`.
  - 각 필드가 곧 **에디터 내장 에셋 피커(썸네일)** = "보여주고 고르는" UI. 새 카테고리 추가 = 필드 1개 + Python 매핑 1줄.
  - **빌드 검증 완료**: `FPSRogueliteEditor Win64 Development` → `Result: Succeeded`.
- ✅ **`Content/Python/fpsr_citygen.py` config 기반 리팩터**
  - `generate_from_config(box, config, seed)` — 프리셋(`STYLES`) 폐기. `DEFAULT_CONFIG` 폴백이 있어 **빈 설정으로도 미리보기가 나온다**.
  - `load_config_from_dataasset()` — DA → config dict. `get_editor_property` 이름 정규화(원본/snake/`b`제거)를 **모두 시도**해 견고.
  - `preview_from_config()` / `confirm_preview()` / `clear_preview()` — 미리보기는 태그 `CityGenPreview` + 폴더 `Buildings/_Preview`, 라벨 `_Preview` 접미. Confirm=태그/폴더/라벨 정리(굽지 않음), Bake=병합 ISM.
  - **메뉴 6종** `Tools > FPSR CityGen`: 1 Place Sizing Box / 2 Open Config / 3 Preview from Config / 4 Confirm Preview / 5 Clear Preview / 6 Bake Selected.
- ✅ 기존 규약 유지: 모듈 **250 가로 × 300 층높이**, 피벗 밑변 왼쪽 +X, 콜리전 **BlockAll**(플로우필드 `ECC_WorldStatic`) + `custom_depth_stencil=1`(셀룩), 250폭 벽만(260 초과 자동 제외).
- ✅ **A-0 perf 베이스라인**: `Saved/Profiling/CSV/Profile(20260720_194245).csv` — 드로우콜 **920**·GPUTime 12ms·적200중 VisibleRendered **196**.

---

## §2 ▶ 다음 작업

### ① DA 인스턴스 생성 (아직 없으면 · 10초)
콘텐츠 브라우저 → **우클릭 → Miscellaneous → Data Asset** → 클래스 **`FPSRCityGenConfig`** 선택 → 이름 **`DA_CityGenConfig`** → 위치 **`/Game/Tools/CityGen/`**(폴더 없으면 생성. 툴 에셋은 `/Game/Tools/` 아래로 모으는 규칙 — 사용자 결정 2026-07-21).
기본 경로 = Python 상수 `fpsr_citygen.CONFIG_DA`(`/Game/Tools/CityGen/DA_CityGenConfig.DA_CityGenConfig`).
⚠️ 경로가 달라도 **`find_config_asset()`이 `FPSRCityGenConfig` 클래스로 전역 검색해 폴백**하므로 폴더를 옮겨도 툴은 계속 동작한다(다만 상수를 맞춰두는 게 정석).
(설정을 비워둬도 `DEFAULT_CONFIG`로 미리보기는 나온다 — DA는 "직접 고르기" 용도.)

### ② 스모크 테스트 (Phase 1 검증)
1. `2. Open Config` → 창문벽 몇 개·코너·문·지붕·코니스·옥상 프롭 지정.
2. `1. Place Sizing Box` → 뷰포트에서 크기 조절(가로 250·층 300 단위로 반올림됨).
3. 박스 **선택한 채** `3. Preview from Config` → 로그 `[CityGen] Building_Cfg_* 생성: WxDxF, 조각 N` 확인.
4. 조각 개별 이동/교체 가능한지 확인 → `4. Confirm Preview` → 필요시 `6. Bake Selected`(로그 `ISM N개`).
5. 실패 시 확인 포인트: DA 경로 오타 / 벽이 500폭(자동 제외됨) / 박스 미선택(→ cfg의 W·D·Floors 또는 3×3×4).

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
