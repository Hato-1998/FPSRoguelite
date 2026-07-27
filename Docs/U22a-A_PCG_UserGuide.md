# U22a-A — 모듈 조립 건물 (PCG) · 사용자 작업 가이드 v2

> 작성 2026-07-20. **방향 정정(2026-07-20)**: 통짜 빌딩 메시 흩뿌리기 ❌ → **기본 모듈 조각(벽·창·문·바닥·코너·트림)을 조립해 건물을 만들고, 그 건물을 배치** ✅.
> **사용자 결정**: ①조립 방식 = **템플릿 조립 후 배치**(모듈로 건물 템플릿 몇 채 제작 → PCG로 배치) ②범위 = **전부 모듈 조립**(스카이라인 포함).
> 선행: `Docs/U22a-A_PCG_ResumePrompt.md`, `Docs/U22_AssetReplacement_Prompt.md`(§3 A).

---

## 0. 이 세션에서 확정된 것 (그대로 유효 — 재조사 불요)

- ✅ **PCG Python 저작 가능**: `PCGGraph.add_node_of_type`/`add_edge`로 코드 저작 확인.
- ✅ **볼륨 배치 시스템 동작**: PCG Volume 안에만 생성, 월드정렬 그리드, 볼륨 밖은 빔. → **"무엇을 배치하느냐"만 바꾸면 재사용**(통짜 메시 → 모듈 건물 템플릿).
- ✅ **R1 콜리전 해법**: 스포너 descriptor에 `BodyInstance.CollisionProfileName='BlockAll'` + `render_custom_depth=True`/`stencil=1` → ISM이 `ECC_WorldStatic`로 나와 **플로우필드가 벽으로 인식**(플로우필드는 WorldStatic만 프로빙). 셀룩도 적용.
- ✅ **A-0 perf 베이스라인** 확보 → §5. **드로우콜 920, 적200중 196 동시가시(시야차단 0)**.
- ⚠️ 이전에 만든 `Content/PCG/PCG_CityBuildingFiller`(통짜 Block 흩뿌리기 그래프)는 **이 방향에선 폐기 후보**. 배치 메커니즘 참고용으로만.

---

## 1. 킷 인벤토리 (조립 재료)

**PolygonCyberCity (주력)** `Content/PolygonCyberCity/Meshes/Buildings/`:
- 벽: `Wall_Window_01~08`(+Old, 유리 분리) · `Wall_Shop_01~04` · `Wall_Half_01/02` · `Wall_Quarter_01` · `Wall_Door_Double_Large` · `Wall_Trim_01~04` · `Wall_Laser`
- 바닥: `Floor_Quarter/Quater_01` · `Floor_Small_01/02` · `Floor_Grating_01` · `Floor_Trim_01/02` · `Floor_End_Corner_01`
- 천장/코너: `Ceiling_Trim_01/02` · `Corner_Trim_01~03`
- 문: `Door_Single/Double/Large`(문틀+문짝+잠금 분리) · 계단 `Stairs_01~05` · 난간 `Railing_*` · 파이프 `Pipe_*`(장식)

**PolygonScifi (액센트)** `Content/PolygonScifi/Meshes/Buildings/`:
- `Section_Wall ×5` · `Section_Window ×4` · `Section_Door ×7` · `Section_Corner ×3` · `Section_Grid`

→ 바닥 슬래브 + 창문벽/문벽/코너 + 트림 + (평)지붕으로 층을 쌓아 건물 조립 가능.

---

## 2. 🔴 반드시 먼저 (에디터 필요) — 스냅 규약 실측

**파일명만으론 조립 못 함.** 다음 세션은 **에디터를 켜고 시작**(그래야 VibeUE 붙음)해서 내가 실측:
- **격자 단위**: 벽 한 장의 폭·높이(층 높이). Synty는 보통 고정 모듈이지만 값 확인 필요.
- **피벗/스냅**: 벽·바닥·코너의 원점이 어디인지(코너? 밑변 중앙?), 코너 조각이 꼭짓점에 앉는지 셀을 대체하는지.
- ⚠️ **소켓 없을 위험**: 과거 Synty 무기 모듈은 소켓이 없어 per-part 오프셋을 직접 계산해야 했음([[synty-modular-parts-shared-origin]]). 건축 조각도 그럴 수 있음 → **벽1·바닥1·코너1·문1을 실제로 붙여 규약을 확정**하는 게 첫 관문. (Synty 데모 맵에 조립 예시가 있으면 그걸로 역설계.)

**이 측정 없이 조립 로직을 짜면 전부 어긋나 재작업.** → 첫 착수 = 내가 측정 + **건물 1채 시제품 조립**해 규약 확정.

---

## 3. 워크플로 (템플릿 조립 → 배치)

1. **측정 + 프로토타입 1채**(§2): 스냅 규약 확정. 1채 조립해 층 높이·코너·문 배치·지붕 캡 확인.
2. **템플릿 3~5채 제작**: 저층(상가 2~3층)·중층(5~8층)·고층(12층+, 스카이라인용)·와이드(창고형) 등. 각 = 바닥슬래브 + 층별 외벽(창문/문/코너) + 트림 + 지붕. **회전/폭·높이 변주로 변형 확보.**
3. **템플릿 저장 방식 결정**(§4 = 성능 핵심).
4. **PCG로 배치**: 내가 만든 볼륨 시스템 재사용 — 스포너를 "통짜 메시" 대신 "건물 템플릿"으로 교체. 볼륨 놓은 곳에만 건물, 도로/광장은 빔. 아레나=저층 템플릿·콜리전 ON / 스카이라인=고층 템플릿.
5. **검증**(§6): R1 플로우필드 PIE + perf 회귀 + 육안.

---

## 4. ⚠️ 성능 함정 & 해법 (전부 모듈 조립이라 특히 중요)

모듈 건물 1채 = 조각 수십 개. **배치 방식에 따라 드로우콜이 폭증**한다(베이스라인 920 → 수천 가능 = 회귀). 세 가지 저장/배치 방식:

| 방식 | 조립 난이도 | 드로우콜 | 비고 |
|---|---|---|---|
| **(권장) PCG Asset로 조각 전역 ISM 병합** | 중 | **최소** | 템플릿을 `PCGLevelToAsset`로 PCG 에셋화 → 그래프가 스폰 시 **모든 건물의 같은 조각을 하나의 ISM으로 병합**(맵 전체에서 벽타입당 1드로우콜). 콜리전·stencil 유지 가능. UE PCG 기본 워크플로. |
| BP(HISM) 템플릿 흩뿌리기 | 하 | 중 | 건물 BP가 조각을 HISM으로 묶음 → 건물당 조각타입 수만큼 드로우콜, **건물 간 병합 안 됨**(60채×6타입=360드로우콜). 만들기 쉬움. |
| BP(개별 컴포넌트) | 하 | **최악** | 조각마다 컴포넌트 = 드로우콜 폭증. 지양. |

→ **전부 모듈 조립 = (권장) PCG-Asset 병합이 사실상 필수.** 다음 세션에서 이 워크플로를 실측 검증(PCG Asset 스폰이 콜리전+stencil 유지하는지 = R1과 동일 관문).
→ 스카이라인(먼 배경) 템플릿은 조각 수를 적게(창문벽 위주, 문/계단 생략)해 비용 절감. HLOD/컬거리도 활용.

---

## 5. perf 베이스라인 (건물 전, 비교 기준)

`Saved/Profiling/CSV/Profile(20260720_194245).csv` — Map_CyberCity, PIE 1920×1080, `FPSR.SpawnEnemies 200`, 600f. 분석 `python Scripts/s4-check-capture.py --warmup 3`.

| 항목 | P50 | P90 | max |
|---|---|---|---|
| VisibleRendered(적) | **196** | 200 | 200 |
| FrameTime | 12.86ms(~78fps) | 14.02 | 21.89 |
| GPUTime | 12.12 | 12.90 | 13.61 |
| **DrawCalls** | **920** | 931 | 937 |

→ 건물 배치 후 재캡처: **VisibleRendered 내려가야**(시야 끊김=목표), **드로우콜 소폭만**(폭증=조립/병합 실패). 재캡처법: PIE(New Editor Window)→`r.setRes 1920x1080w`→`FPSR.SpawnEnemies 200`→`csvprofile frames=600`.

---

## 6. 검증 (완료 판정)

편집→**에디터 재시작→PIE**(World Leak 회피):
1. 🔴 **R1 플로우필드**: `FPSR.SpawnEnemies 200` → 적이 건물 피해 전역 추격, `no-floor`·45~60 함정 0. **조립 건물의 콜리전이 벽으로 작동하는지 = 최대 리스크.**
2. 블록아웃 검증기 `FFPSRBlockoutValidator::ValidateLevel` + 앵커 커맨드릿 `UFPSRValidateAnchoredDataCommandlet`.
3. perf: §5 대비 회귀 없음(드로우콜·GPUTime, VisibleRendered↓).
4. 육안 게이트.

---

## 7. 다음 세션 시작 방법
1. **에디터를 먼저 켜고** 세션 시작(VibeUE 붙음).
2. 나에게: "U22a 모듈 조립 이어서" → 나는 이 문서대로 **측정 → 프로토타입 1채 → §4 병합 워크플로 검증**부터 진행.
3. 미커밋 상태: `Content/PolygonScifi/`·`Content/PCG/`(구 그래프) untracked, 맵 미저장. 육안 게이트 통과 후 커밋.

## 8. 범위 밖 (A 완료 후)
게임플레이 레이어 이식(룸·미션·문) → D(런 완주 → L_Sandbox 삭제 → 쿡 → main 머지). 상세 `Docs/U22a-A_PCG_ResumePrompt.md` §7.
