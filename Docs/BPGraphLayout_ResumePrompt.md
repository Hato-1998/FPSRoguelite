# 전 블루프린트 그래프 배치 정리 — 작업 프롬프트

> 다른 세션에 이 파일 경로만 주면 됩니다: `Docs/BPGraphLayout_ResumePrompt.md`
> 작성 2026-07-30. 계기 = 4a 애님 그래프를 스크립트로 저작하면서 노드 좌표를 감으로 넣어
> 선이 교차하고 게터가 흩어진 상태가 됨. 사람이 읽고 디버깅해야 하는 그래프라 배치도 산출물이다.

## ✅ 1차 실행 완료 (2026-07-30) — 아래 "쓸 수 있는 API"에 정정이 있다

자작 BP 69개 중 연결선이 있는 그래프를 정리했다(커밋 `ef11412e`~`391ab3a4`). 배선 되돌림 0건.

> ## 🛠 이 문서는 이제 이력이다. 앞으로는 **에디터에서 Tools > FPSR BP 노드 정리**를 쓴다.
> 콘텐츠 브라우저에서 BP나 폴더를 고르고 메뉴를 누르면 된다. 되돌리기(Ctrl+Z) 되고,
> 저장은 안 하니 확인 후 Ctrl+S. 엔진 = `Content/Python/fpsr_bp_layout.py`(멱등 — 이미 정리된 건 안 건드린다).
> 그래프는 자동 인식(AnimBP 상태머신 포함), 외부 팩은 자동 제외.

**⚠️ 아래 수치는 옛 측정 모델(선을 노드 원점끼리 이어 잼) 기준이라 과다하다.**
지금은 실제대로 **출발 노드 오른쪽 가장자리의 출력핀 → 도착 노드 왼쪽 가장자리의 입력핀**으로 잰다.
핀 기준 최종: **교차 146 → 111 · 노드 위를 지나가는 선 63/761(8.3%)**.

### ⛔ 정정 — `auto_layout_graph`는 쓸 수 없다 (실측)

아래 표의 `auto_layout_graph` / `auto_layout_selected_nodes`를 **믿지 말 것.** 8개 그래프에 직접
돌려 재본 결과, **노드가 많은 그래프에서는 빈 문자열(성공처럼 보이는 값)을 반환하면서 노드를 하나도
옮기지 않았다**: WBP_RunHUD(48노드)·WBP_Lobby(77)·WBP_DownedOverlay(45)·BP_Door(29) 전부 0개 이동.
작동한 그래프에서는 교차를 **늘렸다**: WBP_BossHUDBar 1→20, ApplyWeaponAimOffset 13→25.
즉 "자동 배치를 먼저 돌리고 게터만 손보는 2단계"는 성립하지 않는다.
믿을 수 있는 원시 도구는 **`set_node_position` 하나뿐**이고, 배치 알고리즘은 직접 짜야 한다.

### 그 밖의 실측 정정

- **`compile_blueprint`는 부르지 말 것.** 노드 좌표는 컴파일 결과에 들어가지 않아 재컴파일이 필요 없고,
  자식 WBP를 품은 위젯을 프로그래매틱으로 컴파일하면 재인스턴싱 중 에디터가 죽은 전례가 있다(2026-06-24).
  저장도 `save_asset`(모달) 대신 `EditorLoadingAndSavingUtils.save_packages([pkg], False)`를 쓴다.
- **에디터를 꺼야 한다는 조건(아래 ⛔ 3번)은 커맨드릿 경로에만 해당한다.** 에디터가 켜져 있으면
  VibeUE MCP `execute_python_code`로 **에디터 안에서** 돌리는 편이 오히려 충돌이 없다(파일을 밖에서
  덮어쓰지 않으므로). 단 편집 탭은 닫아야 하고, 스크립트가 `close_all_editors_for_asset`으로 처리한다.
- **이름이 겹치는 그래프는 개별 지정이 안 된다.** `Transition`이 ABP_Blu_Body에 42개, ABL에 15개,
  `Ground`가 3개 있는데 API가 이름으로만 주소를 잡는다. 각 2~3노드라 정리 가치는 없어 제외했다.
- **자작 BP 69개 중 45개는 "BeginPlay/Tick 노드 3개, 연결 0개" 빈 껍데기**다. 옮겨도 얻는 게 없고
  바이너리 diff만 는다 — 연결선이 있는 그래프만 대상으로 잡는다.

---

## 목표

프로젝트의 **모든 블루프린트 그래프**(BP · AnimBP · WidgetBP)를 열어 노드 배치를 정리한다.
**로직·배선·값은 한 톨도 바꾸지 않는다.** 옮기는 것만 한다.

## ⛔ 절대 지킬 것

1. **연결·핀 값·노드 추가/삭제 금지.** `set_node_position` / `auto_layout_*` 외의 변경 API를 쓰지 말 것.
2. **작업 전후로 배선 스냅샷을 떠서 대조**한다. 노드 수와 각 노드의 연결된 입력핀 목록이
   **완전히 동일**해야 한다. 하나라도 다르면 그 에셋은 되돌리고 기록만 남긴다.
3. **에디터가 켜져 있으면 시작하지 말 것.** 커맨드릿이 에셋을 덮어쓰면 열린 에디터와 충돌한다.
   `Get-Process | Where-Object { $_.ProcessName -match 'UnrealEditor' }` 가 비어야 시작.
4. **커밋은 배치 전용으로 분리**한다(`chore(bp): 그래프 배치 정리`). 다른 변경과 섞지 말 것.
5. 한 번에 전부 하지 말고 **폴더 단위로 나눠 커밋**한다. 되돌릴 단위를 작게.

## 실행 경로 — 커맨드릿 (VibeUE MCP 아님)

VibeUE MCP 툴은 API 키 게이트에 걸릴 수 있다(로그: `VibeUE API key validation failed (HTTP 0)`).
게이트는 **MCP 계층에만** 있으므로 엔진 커맨드릿으로 같은 Python 서비스를 직접 부르면 키가 필요 없다.

```bash
"D:/UnrealEngine/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "E:/Git_Project/FPSRoguelite/FPSRoguelite.uproject" \
  -run=pythonscript -script="<스크립트 절대경로>" -unattended -nosplash -nopause
```

**⚠️ `print()` 출력은 stdout이 아니라 로그 파일로 간다.** 결과는 이렇게 읽는다:

```bash
LOG=$(ls -t Saved/Logs/*.log | head -1); grep -a "LogPython: <태그>" "$LOG" | sed 's/^.*LogPython: //'
```

모든 출력에 고유 태그(`LAY` 등)를 붙여 두면 grep이 쉽다.

## 쓸 수 있는 API (실측 확인됨)

| API | 비고 |
|---|---|
| `unreal.BlueprintService.auto_layout_graph(bp, graph)` | 위상정렬 기반 전체 자동 배치 |
| `unreal.BlueprintService.auto_layout_selected_nodes(bp, graph, [ids])` | 일부만 |
| `unreal.BlueprintService.set_node_position(bp, graph, id, x, y)` | 좌표 직접 |
| `unreal.BlueprintService.get_nodes_in_graph(bp, graph)` | `node_id` `node_title` `node_type` `pos_x` `pos_y` |
| `unreal.BlueprintService.get_node_details(bp, graph, id)` | `input_pins[].is_connected` — **대조용 스냅샷의 핵심** |
| `unreal.AnimGraphService.list_graphs(bp)` | 그래프 목록(AnimBP) |
| `unreal.AnimGraphService.list_states_in_machine(bp, machine)` | 상태 내부 그래프는 **상태 이름으로** 주소 지정 |
| `unreal.BlueprintEditorLibrary.compile_blueprint(asset)` | 정리 후 반드시 |
| `unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False)` | 크래시 후엔 자동저장이 꺼져 있으니 **명시 호출 필수** |

## 배치 규칙

`auto_layout_graph`는 좌→우 흐름은 잡아주지만 **변수 게터를 소비 노드 핀 높이에 맞춰주지 않는다.**
그래서 자동 배치를 먼저 돌리고, 그 뒤에 게터만 손보는 2단계가 결과가 가장 낫다.

1. **주 흐름은 좌→우 한 줄.** 열 간격 320~360, 행 간격 140~180으로 통일한다.
2. **출력/결과 노드는 체인의 맨 오른쪽 끝.** (지금 여러 그래프에서 위쪽에 있어 선이 되짚어 올라간다)
3. **변수 게터는 소비 노드 바로 왼쪽, 연결될 핀의 높이에.** 이것만 지켜도 교차가 대부분 사라진다.
4. **한 게터가 여러 노드에 나가면** 소비 노드들의 중간 높이에 두고 왼쪽으로 뺀다.
5. **분기(블렌드 트리)는 위→아래 순서를 입력핀 순서와 일치**시킨다.
   (예: `BlendPose_0`에 들어갈 노드를 `BlendPose_1`짜리보다 위에)
6. 엔진이 자동 삽입한 변환 노드(`Local To Component` 등)는 **건드리지 말고 자동 배치에 맡긴다.**

## 대상 범위

`Content/` 아래 전부. 다만 **우선순위를 두고, 외부 팩은 건드리지 않는다.**

**하기**
- `Content/Characters/Blu/Anims/` — `ABP_Blu_Body`, `ABL_Blu_W2_Rifle`(4a에서 저작, 가장 지저분함)
- `Content/Character/`, `Content/Weapons/`, `Content/UI/`, `Content/Enemy/`, `Content/Game/` 등 자작 BP

**하지 말기**
- `Content/PolygonCyberCity/`, `Content/PolygonMilitary/`, `Content/StylizedRenderingSystem/`,
  `Content/Rifle_01/`, `Content/ProceduralWeaponAnimationSystem/` — 외부 구매/마켓 팩.
  건드리면 팩 갱신 시 충돌하고 diff만 늘어난다.
- 엔진/플러그인 경로 전체

## 절차

1. 에디터 종료 확인 → `git status`가 깨끗한지 확인(아니면 먼저 정리)
2. 대상 BP 목록을 만들어 출력(개수와 경로를 먼저 보고할 것)
3. **BP 하나당**: 배선 스냅샷 → 배치 → 컴파일 → 스냅샷 재취득 → **대조** → 같으면 저장, 다르면 되돌림
4. 폴더 단위로 커밋
5. 마지막에 요약: 정리한 그래프 수 / 건너뛴 것과 이유 / 대조 불일치가 난 것

## 보고

- 무엇이 실제로 좋아졌는지 **before/after 수치**로: 노드 겹침 수, 평균 연결선 길이 등 잴 수 있는 것
- 잰 게 없으면 잰 게 없다고 쓸 것. "깔끔해졌다"는 보고가 아니다
- 되돌린 에셋이 있으면 **왜 배선이 달라졌는지**를 추적해 남길 것 — 그게 이 작업의 진짜 위험이다

## 참고 — 이 프로젝트 상시 규칙

- `CLAUDE.md`와 `Game.md`(SSOT 허브), `PROGRESS.md`를 먼저 읽는다
- 검증 없이 "완료" 보고 금지
- 보고·질문은 한국어, 쉬운 말로
