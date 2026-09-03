# 아케이드 포스트프로세싱 — 새 세션 실행 프롬프트

> 작성 2026-09-03. **새 세션이 그대로 복붙해 착수**하는 실행 프롬프트다.
> 선행 = 아케이드 룩 프로토(브랜치 `proto/arcade-look`, 커밋 23개). 이 문서는 그 위에 **포스트프로세싱**을 얹는다.
> 상세 경위 = `git log proto/arcade-look` · 아트 규칙 = `Docs/SSOT/ArtDirection.md` · 구조 = `Docs/Architecture/0010·0012`.

**복붙용 첫 지시문**
```
Docs/ArcadePostProcess_ResumePrompt.md 를 읽고 진행한다.
브랜치 proto/arcade-look. 보드 클레임부터 하고, §3 순서대로.
한 번 바꾸면 사용자에게 화면 판정을 받고 다음으로.
```

---

## §0 세션 시작 방법

1. **보드 클레임 먼저**(하드 게이트) — `/board 클레임 아케이드 포스트프로세싱`.
2. 브랜치 확인: `git branch --show-current` 가 `proto/arcade-look` 인지. **공유 워크트리라 다른 세션이
   갈아탔을 수 있다**(2026-09-03 실제로 세션 도중 `content/hud-synty-art` 로 바뀐 사고).
3. 에디터는 **사용자가 켠다.** 켜져 있어야 하는 작업과 닫혀 있어야 하는 작업이 갈리므로 §2 를 먼저 읽을 것.
4. 🔴 **런타임 검증(PIE)은 사용자가 실행한다.** Claude 는 게임을 직접 켜지 않는다.

---

## §1 지금 상태 (이어받을 것)

### 1-1. 무대
`L_Map_1` = 팩맨 오리지널 미로(28×31 타일, 통로 10m·벽 12m, 280×310m). 액터 162.
- **바닥** `MI_Pac_FloorGuide` — 통로 중앙 가이드라인 + 흐르는 빛. 방(2×2 전부 열림)에서는 선이 자동으로 빠진다.
- **벽** `MI_Pac_WallEdge` — 박스 12개 **모서리만** 발광. 면 격자는 폐기했다(시선 분산 + 경계 불명확).
- **적** `MI_EnemyProto_AtomCubes` — 프레넬 실루엣 림(`#FF3B4E`) + 노멀 변화율 하드 엣지(`#F09AA4`).
- 아레나 셀 200cm · 21,700셀. 베이크 완료(`DA_Map_1_ArenaBake`).

### 1-2. 포스트프로세스 현재 값 — **여기서 시작한다**
`PP_Arcade` **볼륨 1개**(`unbound=True`, `priority=1.0`). 2026-09-03 에 볼륨이 2개라 서로 싸우던 것을
하나로 합쳤다(노출 보정 0 vs 11 = 11스톱이 매 프레임 뒤집혔다). **볼륨을 새로 만들지 말고 이것을 편집할 것.**

| 설정 | 현재 | 비고 |
|---|---|---|
| `bloom_intensity` | 2.5 | |
| `bloom_threshold` | **-1.0** | 🔴 임계가 없어 **어두운 것까지 전부 번진다** — §3-1 의 본론 |
| `auto_exposure_method` | **MANUAL** | 🔒 유지 |
| `auto_exposure_bias` | 11.0 | 🔒 유지(§2-1) |
| `vignette_intensity` | 0.4 (override 꺼짐) | 켤지는 §3-3 |
| `scene_fringe_intensity` | 0 (override 꺼짐) | 색수차, §3-3 |

`BP_StylizedRenderingSystem` = `OUTLINES_ONLY`, `Only on Custom Depth=False`(씬 전체).
실측상 적에는 아웃라인이 안 걸린다 — 그래서 적 외곽선은 머티리얼로 해결했다. 건드리지 말 것.

---

## §2 🔒 건드리면 안 되는 것 / 지켜야 하는 것

### 2-1. 노출은 MANUAL 고정을 유지한다
4인 협동이다. 자동 노출로 되돌리면 **플레이어마다 화면 밝기가 따로 출렁인다**(U22a-B §6-4 가 같은 이유로
고정을 지시). 밝기를 바꾸고 싶으면 `auto_exposure_bias` 숫자를 바꾸지, 방식을 바꾸지 않는다.

### 2-2. 스웜 가독성이 최우선이다
`ArtDirection A-5` = *"적 메시를 화려하게 만들지 말고 VFX 에 헤드룸을 남긴다"* ·
U22a-B §1 = *"화면의 화려함은 환경이 아니라 전투가 낸다."*
**CRT·스캔라인·색수차를 세게 걸면 적 실루엣의 엣지가 뭉갠다.** 적 200~300 이 목표인 게임에서 이건
분위기 손해가 아니라 **직접적인 플레이 손해**다. 세기는 "있는지 없는지 헷갈릴 정도"에서 시작해 올린다.

### 2-3. 계측 축을 부수지 말 것
`Performance.md:36` 이 `r.PostProcessing.DisableMaterials 1` 을 **셀 off 계측 축**으로 쓴다.
PP 머티리얼을 추가하면 그 명령에 **스캔라인도 같이 꺼진다** — 계측 시 두 축이 분리되지 않는다.
새 PP 머티리얼을 넣거든 이 사실을 `Performance.md` 에 한 줄 남길 것.

### 2-4. `M_PP_StageFade` 와의 순서
`Content/Materials/PostProcess/M_PP_StageFade` 가 이미 있다(`CustomDepth==SceneDepth` 마스크,
`FPSRCharacter.cpp:152-154`). 스캔라인 blendable 을 추가하면 **블렌드 순서**를 정해야 한다 —
스테이지 페이드가 스캔라인 위에 와야 전환 연출이 화면을 온전히 덮는다.

---

## §3 작업 순서

### 3-1. 블룸 임계부터 ★여기서 시작
지금 `bloom_threshold = -1.0` 이라 **검정 배경까지 번진다.** 무대가 "깊은 검정 + 얇은 네온"인데
임계가 없으면 전체가 뿌옇게 들뜬다. 임계를 올려 **발광 선만** 번지게 한다.
- 노출 보정이 11 이라 씬 값이 크게 부스트된 상태다. 임계를 **1.0 부터** 올려보며 검정이 검정으로 남는
  지점을 찾는다. 한 번 바꾸고 사용자 판정을 받는다.
- 판정 기준: ①복도 안쪽 검정이 검정인가 ②가이드라인의 흐르는 빛이 여전히 번지는가 ③적 림이 죽지 않는가.

### 3-2. CRT · 스캔라인 (새 PP 머티리얼)
`Material Domain = Post Process` 머티리얼을 만들어 `PP_Arcade` 의 blendable 에 넣는다.
- 스캔라인 = `ViewportUV.y` 기반 주기 감쇠. **화면 공간**이라 월드 좌표가 아니다.
- 파라미터로 뽑을 것: `ScanlineCount` · `ScanlineStrength` · `Curvature`(넣는다면).
- 🔴 세기 기본값은 **0.05 이하**에서 시작. §2-2.
- ⚠️ `SceneTexture:PostProcessInput0` 는 **float4** 다. float3 파라미터와 Lerp 하면 폭 불일치로
  **조용히** 깨진다(메모리 `material-if-node-scalar-silent-fail`).

### 3-3. 색수차 · 비네트 (내장 설정, 머티리얼 불필요)
- `scene_fringe_intensity` — 1.5 정도부터. 아케이드 CRT 느낌에 기여하지만 과하면 적 엣지가 갈라진다.
- `vignette_intensity` — 이미 0.4 값이 있으나 override 가 꺼져 있다. 켤지 판단.

### 3-4. 마무리
- 보드 마감(커밋 해시) → `Docs/WorkLog.md` 최상단에 경위 이관.
- `Performance.md` 에 §2-3 한 줄.

---

## §4 저작 함정 — 2026-09-03 에 전부 실제로 밟은 것들

### 4-1. 🔴 에셋 임포트는 **두 실행 컨텍스트 모두에서 죽는다**
```
라이브 에디터 : Assertion failed: ++Queue(QueueIndex).RecursionGuard == 1  (TaskGraph.cpp:689)
커맨드렛      : Assertion failed: CurrentApplication.IsValid()             (SlateApplication.h:321)
```
파일 형식과 무관하다(237바이트 PNG 에서도 터졌다). **텍스처가 필요하면 사용자가 에디터에서 직접 임포트**하거나,
아예 요구를 없앤다(미로는 셰이더 상수로 넣어 해결했다). 세 번째 우회를 찾지 말 것.

### 4-2. 라이브 에디터에서 **안전한 것 / 위험한 것**
- ✅ 안전: 머티리얼 **생성**, Custom 노드 **코드 교체**, 파라미터 값 변경, 액터 스폰·삭제·머티리얼 교체
- ❌ 위험: **에셋 임포트**(4-1)
- 커맨드렛에서 안전: 머티리얼 생성/편집. ❌ 위험: 임포트, 레벨 편집 API(Slate 없음)

### 4-3. 검증 없이 "완료"가 되는 경로들
| 함정 | 증상 | 대응 |
|---|---|---|
| `connect_material_expressions` 반환값 무시 | 머티리얼이 반쯤 배선된 채 **조용히 검정** | 모든 배선을 `link()` 헬퍼로 감싸 실패 수집 |
| Custom 노드 **이름 붙은 입력 미연결** | 컴파일 에러 | `inputs` 를 통째로 교체(기본 핀 `"1"` 제거) |
| Custom 노드 출력은 **하나** | `"R"`/`"G"` 핀 이름으로 못 뽑음 | `ComponentMask` 로 분리 |
| **커맨드렛은 셰이더 통계를 안 낸다** | `VS=0 PS=0` 이 정상처럼 보임 | **에디터에서** `get_statistics` 재확인 |
| `save_asset` 기본 `only_if_is_dirty=True` | 로그엔 `save=True`, 디스크는 그대로 | `only_if_is_dirty=False` + **`git status` 대조** |
| 파이썬 액터 편집이 맵을 더티로 안 만듦 | `save_current_level()` 이 조용히 건너뜀 | `save_dirty_packages(True,True)` + `save_asset(map, False)` + git 대조 |

### 4-4. HLSL / API
- 🔴 **`line` 은 HLSL 예약어**(`point`/`triangle`/`lineadj`/`triangleadj` 도). `float line = ...` 이
  *"modifiers must appear before type"* 로 깨지고 **머티리얼이 통째로 Default Material 로 폴백**한다.
- 에러 본문은 `Failed to compile` 줄이 아니라 **그 다음 연속 줄들**에 있다.
- `unreal.Rotator(a, b, c)` = **(Roll, Pitch, Yaw)**. 키워드로 줄 것.
- Custom 노드 안에서는 **함수 정의 불가**(본문이 인라인된다) — 매크로나 인라인 식으로.
- `fwidth` 를 **비균일 제어 흐름**(조기 return 뒤)에서 부르지 말 것.

### 4-5. MCP 도구 특성
- `DeprecationWarning` 과 `unreal.log_warning` 이 **에러로 승격**된다.
  → 스크립트 상단에 `import warnings; warnings.filterwarnings("ignore")`, 조언성 로그는 `unreal.log` 로.
- **에디터 창이 백그라운드면 스크린샷 자동화가 프레임을 못 받는다.** 안 떨어지면 붙들지 말고
  **수치로 검증**하거나(예: 레벨의 실제 액터 좌표와 공식 대조) 사용자에게 판정을 넘길 것.

---

## §5 판정 기준 (사용자에게 물을 것)

한 번 바꾸면 스크린샷을 받아 이 셋을 묻는다:
1. **검정이 검정인가** — 무대의 깊이는 어둠에서 온다.
2. **적이 즉시 보이는가** — 복도에 적을 세우고, 시선이 먼저 가는지.
3. **가이드라인의 흐름이 살아 있는가** — 빛이 지나가는 것이 읽히는지.

---

## §6 아직 안 정해진 것 (이 트랙 밖, 알고만 있을 것)

- 🔴 **통로 폭 재검토** — 적 캡슐 80×180cm vs 통로 10m = **폭의 10%**(팩맨 유령은 ~90%).
  5m 로 줄이면 20% + 아레나가 140×155m 가 되어 셀 200cm 편법 없이 ADR 0012 상한 안에 든다.
- **셀↔아웃라인 stencil 규약** — ADR 0007 이 남긴 미결. 환경 메시 물량 저작 전에 필요.
- **무기 복셀** — 사용자가 Blockbench 로 저작(glTF), Claude 가 임포트·머티리얼·배치.
- **`ArtDirection.md` 확장** — 플랜 v3 가 이미 있다(스크래치패드). 프로토가 실측을 냈으므로 착수 가능.
  `A-1` 면적 규칙의 수치를 `LineWidth`·`EdgeWidth`·`RimGain` 실측에서 뽑는다.
