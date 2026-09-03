# CityGen 패널 만들기 — 완전 초보자용 따라하기

> `EUW_CityGen`(빈 껍데기)에 버튼을 달아 **CityGen 도구 창**을 만드는 방법.
> UMG(위젯 편집기)를 한 번도 안 써봤어도 그대로 따라오면 됩니다.
> ⚠️ **핵심 요령: 버튼 1개를 먼저 완성해서 눌러보고, 되는 걸 확인한 다음 복사·붙여넣기로 늘립니다.**
> 11개를 다 만들어 놓고 안 되면 어디가 잘못됐는지 찾기 어렵습니다.

---

## 0. 왜 손으로 하나요?

원래는 제가(AI) 자동으로 만들려고 했는데 **엔진이 막아놨습니다.** 위젯을 담는 그릇
(`WidgetTree`)의 `RootWidget` 항목이 잠겨 있어서(protected) 프로그램으로는 "이 위젯을 맨 위로"
지정할 수가 없습니다. 그래서 껍데기까지만 만들어 두었고, 안을 채우는 건 사람 손이 필요합니다.
(자세한 실측 근거: `U22a-A_BuildingTool_ResumePrompt.md` §2-①)

**한 번만 만들어 두면 계속 씁니다.** 20~30분이면 끝납니다.

---

## 1. 위젯 편집기 열기

1. 언리얼 에디터 아래쪽 **콘텐츠 브라우저**에서 주소창에 `Tools/CityGen` 폴더로 이동합니다.
   (또는 콘텐츠 브라우저 검색창에 `EUW_CityGen` 입력)
2. **`EUW_CityGen`** 을 **더블클릭**합니다.
3. 새 창이 뜹니다. 이게 위젯 편집기입니다.

### 화면 구조 (이것만 알면 됩니다)

```
┌────────────────────────────────────────────────────────┐
│ [컴파일] [저장]                    [디자이너] [그래프]  │ ← 오른쪽 위 탭 2개
├──────────┬──────────────────────────────┬──────────────┤
│ 팔레트    │                              │  디테일       │
│ (부품통)  │        화면 미리보기          │ (선택한 것의  │
│          │                              │   설정)      │
├──────────┤                              │              │
│ 계층 구조 │                              │              │
│ (부품 목록)│                             │              │
└──────────┴──────────────────────────────┴──────────────┘
```

- **디자이너 탭** = 눈에 보이는 배치를 만드는 곳
- **그래프 탭** = "버튼을 누르면 무슨 일이 일어나는지"를 만드는 곳
- **팔레트** = 부품 창고 (버튼, 글자, 상자…)
- **계층 구조** = 지금 넣은 부품들의 목록
- **디테일** = 선택한 부품의 설정값

---

## 2. 세로 상자 놓기 (버튼들을 담을 그릇)

지금은 완전히 비어 있습니다. 먼저 버튼들을 **세로로 줄 세워 줄 그릇**을 놓습니다.

1. 오른쪽 위 **`디자이너`(Designer)** 탭이 선택돼 있는지 확인합니다.
2. 왼쪽 **팔레트**의 검색창에 **`Vertical Box`** 를 입력합니다.
3. 나온 **`Vertical Box`** 를 마우스로 끌어서, 왼쪽 아래 **계층 구조**의
   맨 위 항목(`[EUW_CityGen]`) **위에 떨어뜨립니다.**

> 💡 **왜 Vertical Box인가요?** 이 안에 버튼을 넣으면 자동으로 세로로 차곡차곡 쌓입니다.
> `Canvas Panel`을 쓰면 버튼마다 위치를 일일이 지정해야 해서 훨씬 번거롭습니다.

✅ 계층 구조에 `Vertical Box`가 생겼으면 성공입니다.

---

## 3. 버튼 1개 만들기

1. 팔레트 검색창에 **`Button`** 입력 → **`COMMON` 항목 아래의 `Editor Utility Button`** 을 끌어서
   계층 구조의 **`Vertical Box` 위에 떨어뜨립니다.**
2. 팔레트 검색창에 **`Text`** 입력 → 나온 **`Text Block`** 을 끌어서
   방금 만든 **버튼 위에 떨어뜨립니다.** (버튼 안에 글자가 들어갑니다)
3. 계층 구조에서 방금 넣은 **`Text Block`** 을 클릭 → 오른쪽 **디테일** 패널에서
   **`Text`** 칸에 **`미리보기`** 라고 입력합니다.

> ⚠️ **"그냥 `Button`은 목록에 없는데요?"** — 맞습니다. **Editor Utility Widget을 편집할 때는
> 팔레트가 에디터 전용 버전인 `Editor Utility Button`을 대신 보여줍니다.**
> 이건 일반 `Button`을 **그대로 상속한 것**이라(`EditorUtilityButton → Button`) 기능이 100% 같고,
> `On Clicked`도 똑같이 있습니다. 그냥 쓰시면 됩니다.
> 참고로 **버튼만** 이렇습니다 — 글자·세로상자·설정패널은 에디터용 대체본이 아예 없어서
> `Text Block` / `Vertical Box` / `Details View` 원래 이름 그대로 나옵니다.

✅ 가운데 미리보기 화면에 "미리보기"라고 쓰인 버튼이 보이면 성공입니다.

### 버튼 이름 바꾸기 (권장)

계층 구조에서 **`Button_0`** 을 **더블클릭**하면 이름을 바꿀 수 있습니다.
**`Btn_Preview`** 로 바꿔두세요. 나중에 버튼이 많아지면 어느 게 어느 건지 구분하기 쉽습니다.

---

## 4. "버튼을 누르면" 만들기

1. 계층 구조에서 **버튼**(`Btn_Preview`)을 클릭합니다.
2. 오른쪽 **디테일** 패널을 **맨 아래까지 스크롤**합니다.
3. **`이벤트`(Events)** 칸에 **`On Clicked`** 라는 항목과 그 옆에 **초록색 [+] 버튼**이 있습니다.
   **[+] 를 클릭**합니다.
4. 자동으로 **그래프 탭**으로 넘어가고, **`On Clicked (Btn_Preview)`** 라는
   빨간 상자가 하나 생깁니다.

✅ 여기까지 왔으면 절반 넘었습니다.

---

## 5. 파이썬 명령 노드 연결하기

이제 "버튼을 누르면 → 이 파이썬 명령을 실행" 을 연결합니다.

1. 방금 생긴 빨간 상자 오른쪽에 **하얀 삼각형 화살표(▷)** 가 있습니다.
   그걸 **마우스로 끌어서** 빈 공간에 놓습니다.
2. 손을 떼면 검색 창이 뜹니다. **`Execute Python Command`** 를 입력합니다.
3. 나온 **`Execute Python Command`** 를 클릭합니다.
   (비슷한 이름의 `Execute Python Command (Advanced)` 도 있는데, **그냥 `Execute Python Command`** 를 고르세요)
4. 새로 생긴 노드에 **`Python Command`** 라는 입력칸이 있습니다. 거기에 아래를 **그대로** 붙여넣습니다.

```
import fpsr_citygen; fpsr_citygen.preview_from_config()
```

### 이런 모양이 되면 맞습니다

```
┌──────────────────────┐        ┌────────────────────────────┐
│ On Clicked           │        │ Execute Python Command     │
│ (Btn_Preview)      ▷─┼───────▶│▷                           │
└──────────────────────┘        │ Python Command:            │
                                │ [import fpsr_citygen; ...] │
                                └────────────────────────────┘
```

> ⚠️ **`Execute Python Command` 가 검색에 안 나오면**: 검색창 오른쪽의
> **`상황에 맞는 항목만`(Context Sensitive)** 체크를 **끄고** 다시 검색해 보세요.

---

## 6. 컴파일 → 저장 → 실행

1. 창 **왼쪽 위 `컴파일`(Compile)** 버튼을 누릅니다. → 초록 체크가 뜨면 성공입니다.
2. 그 옆 **`저장`(Save)** 을 누릅니다.
3. 위젯 편집기 창을 닫습니다.
4. 콘텐츠 브라우저에서 **`EUW_CityGen` 을 우클릭** →
   **`에디터 유틸리티 위젯 실행`(Run Editor Utility Widget)** 을 클릭합니다.
5. 작은 창이 하나 뜹니다. 거기 **"미리보기" 버튼을 눌러 보세요.**

✅ **레벨에 건물이 생기면 성공입니다.** (사이징 박스를 미리 놔뒀다면 그 크기로 생성됩니다)

> 💡 뜬 창은 에디터 아무 곳에나 **드래그해서 도킹**할 수 있습니다. 디테일 패널 옆에 붙여두면 편합니다.

---

## 7. 나머지 버튼 늘리기 (복사·붙여넣기)

**하나가 됐으면 나머지는 복사로 끝납니다.**

### 디자이너 쪽
1. 계층 구조에서 **버튼**(`Btn_Preview`)을 클릭 → **Ctrl+C** → **Ctrl+V**
2. 복사된 버튼이 `Vertical Box` 안에 하나 더 생깁니다.
3. 그 안의 `Text`를 클릭해서 글자만 바꿉니다 (예: `확정`)
4. 버튼 이름도 더블클릭해서 바꿉니다 (예: `Btn_Confirm`)
5. 필요한 개수만큼 반복합니다.

### 그래프 쪽
1. 새로 만든 버튼을 클릭 → 디테일 맨 아래 **`On Clicked` [+]** → 새 빨간 상자 생성
2. 그래프에서 기존 **`Execute Python Command` 노드**를 클릭 → **Ctrl+C** → **Ctrl+V**
3. 복사된 노드를 새 빨간 상자에 **화살표로 연결**
4. **`Python Command` 칸의 글자만** 아래 표에서 바꿔 넣습니다

### 버튼별 명령문 (복사해서 쓰세요)

| 버튼 이름표 | Python Command 칸에 넣을 것 |
|---|---|
| 메시 수집 | `import fpsr_citygen; fpsr_citygen.collect_modular_meshes()` |
| 설정 채우기 | `import fpsr_citygen; fpsr_citygen.fill_config_from_kit()` |
| 설정 열기 | `import fpsr_citygen, unreal; _da = fpsr_citygen.find_config_asset(); unreal.get_editor_subsystem(unreal.AssetEditorSubsystem).open_editor_for_assets([_da])` |
| 박스 놓기 | `import fpsr_citygen; fpsr_citygen.place_sizing_box()` |
| **미리보기** | `import fpsr_citygen; fpsr_citygen.preview_from_config()` |
| 확정 | `import fpsr_citygen; fpsr_citygen.confirm_preview()` |
| 지우기 | `import fpsr_citygen; fpsr_citygen.clear_preview()` |
| 굽기(ISM) | `import fpsr_citygen; fpsr_citygen.bake_selection()` |
| **다시 굴리기** | `import fpsr_citygen; fpsr_citygen.reroll_selected()` |
| 층 +1 | `import fpsr_citygen; fpsr_citygen.change_floors(1)` |
| 층 -1 | `import fpsr_citygen; fpsr_citygen.change_floors(-1)` |
| 조각 바꾸기 | `import fpsr_citygen; fpsr_citygen.cycle_piece_mesh(1)` |

> 💡 **다 만들 필요 없습니다.** 자주 쓰는 것부터 만드세요. 추천 5개:
> **미리보기 · 다시 굴리기 · 층 +1 · 층 -1 · 확정**
> 나머지는 기존 메뉴(`도구 > FPSR CityGen`)로도 쓸 수 있습니다.

---

## 8. (선택) 설정 패널도 같이 붙이기

건물 설정(어떤 창문을 쓸지 등)을 같은 창에서 보고 싶다면:

1. **디자이너** 탭에서 팔레트에 **`Details View`** 검색 → `Vertical Box` 안에 끌어다 놓기
2. 계층 구조에서 그 `Details View`를 클릭 → 이름을 **`ConfigView`** 로 변경
3. 디테일 패널 위쪽의 **`변수`(Is Variable)** 체크박스를 **켭니다** (안 켜면 그래프에서 못 씁니다)
4. **그래프** 탭으로 이동 → 빈 곳 **우클릭** → **`Event Construct`** 검색해서 추가
5. `Event Construct` 화살표를 끌어 → **`Load Asset`** 검색·추가 →
   `Asset Path` 칸에 `/Game/Tools/CityGen/DA_CityGenConfig` 입력
6. 왼쪽 **`내 블루프린트`** 패널에서 **`ConfigView`** 를 그래프로 끌어다 놓기 →
   거기서 화살표를 끌어 **`Set Object`** 검색·추가
7. `Load Asset`의 결과(파란 핀)를 `Set Object`의 `Object` 칸에 연결
8. 컴파일 → 저장

> 이 단계는 없어도 됩니다. **`설정 열기` 버튼**을 만들어 두면 별도 창으로 열려서 똑같이 편집할 수 있습니다.

---

## 9. 안 될 때

| 증상 | 확인할 것 |
|---|---|
| 버튼을 눌러도 아무 일 없음 | 아래쪽 **`출력 로그`(Output Log)** 창을 열어 빨간 글씨 확인 |
| `No module named fpsr_citygen` | `Content/Python/fpsr_citygen.py` 파일이 있는지 확인 → 에디터 재시작 |
| 명령은 도는데 건물이 안 생김 | 출력 로그에서 `[CityGen]` 로 시작하는 줄을 읽어보기 (풀이 비었다는 경고가 있을 수 있음) |
| `Execute Python Command` 검색 안 됨 | 검색창 옆 **`상황에 맞는 항목만`(Context Sensitive)** 체크 해제 |
| 창은 뜨는데 버튼이 안 보임 | `Vertical Box` **안에** 버튼이 들어갔는지 계층 구조에서 확인 |
| 팔레트에 그냥 `Button`이 없음 | 정상입니다. **`Editor Utility Button`** 을 쓰세요(일반 Button을 상속한 같은 물건) |
| 팔레트에 그냥 `Text`가 없음 | **`Text Block`** 이 맞는 이름입니다 |
| 바꿨는데 반영이 안 됨 | **컴파일**을 눌렀는지 확인 (저장만으로는 반영 안 됩니다) |
| 파이썬 코드를 고친 뒤 반영 안 됨 | 아래 "파이썬을 고쳤을 때" 참고 |

### 파이썬을 고쳤을 때 (재시작 불필요)

`fpsr_citygen.py`를 수정했으면, 에디터 하단 **`출력 로그`** 창 아래 명령 입력줄을
**`Python`** 모드로 바꾸고 아래를 붙여넣어 실행하면 즉시 반영됩니다.

```
import importlib, fpsr_citygen; importlib.reload(fpsr_citygen); fpsr_citygen.register_menu()
```

---

## 10. 요약 (한 장)

```
1. EUW_CityGen 더블클릭
2. 디자이너 탭 → 팔레트에서 Vertical Box 를 계층 구조 맨 위로 끌기
3. Editor Utility Button 을 Vertical Box 안으로, Text Block 을 그 버튼 안으로 끌기
4. 버튼 선택 → 디테일 맨 아래 On Clicked 의 [+] 클릭
5. 그래프에서 화살표 끌어 Execute Python Command 추가
6. Python Command 칸에 명령문 붙여넣기
7. 컴파일 → 저장
8. 콘텐츠 브라우저에서 우클릭 → 에디터 유틸리티 위젯 실행
9. 되면 Ctrl+C/V 로 나머지 버튼 복제, 명령문만 교체
```
