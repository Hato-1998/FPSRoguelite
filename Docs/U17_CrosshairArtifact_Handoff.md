# 재개 프롬프트 — 크로스헤어 Spread 아티팩트 (VibeUE 세션) + U17 머지 마무리

> 새 세션에 **이 파일 통째로** 붙여넣고 시작. 전제: **UE 에디터 열림 + VibeUE 플러그인 활성**(127.0.0.1:8088, 등록명 `VibeUE-Claude` — 메모리 [[unreal-editor-mcp-vibeue]]).

---

[작업] 크로스헤어 동적 Spread 아티팩트 원인 확정 + 수정 (VibeUE로 머티리얼 그래프 직접 열람/편집), 그다음 **U17(크로스헤어 크기) 머지 마무리**.

## 0. 먼저 할 것
- `Game.md`(§0-1) + `PROGRESS.md` 최신 핸드오프 읽기. 도메인 SSOT: `Docs/SSOT/PlayerFeel.md §2-14`(HUD/크로스헤어, U17 반영됨), `Docs/SSOT/Workflow.md §6·§6-7`.
- **VibeUE 연결 확인**: `ToolSearch`로 VibeUE 도구가 뜨는지 확인. 안 뜨면 MCP 미배선(설정 `.mcp.json`/settings 확인) → 에디터 열고 플러그인 켠 뒤 재시도. 메모리 [[vibeue-mcp-capabilities]](되는/안 되는 것), [[vibeue-render-target-gpu-hazard]](⚠️ UI머티 RT 반복 렌더=GPU 물림 / 컨테이너 WBP 프로그래매틱 저장=행/손상), [[ue-editor-file-locks-block-git]](git 머지/checkout 전 에디터 종료).

## 1. 버그
발사로 크로스헤어가 **최대 Spread에 가까워지면** 중앙의 **대각선 4방향에 검은 마크(조각)** 가 나타남. (스샷: 흰 "+" 4바 사이 대각선에 어두운 파편.)

## 2. 이번(이전) 세션에서 실측 확정한 사실 — 재조사 불필요
- **실제 렌더되는 크로스헤어** = `UFPSRRunHUDWidget::CrosshairImage`(BindWidgetOptional, `WBP_RunHUD`) → `MI_Crosshair_Default`(부모 `M_DynamicCrosshair`) → 텍스처 `T_CH001`(32×32).
  - ⚠️ `WBP_BasicCrosshair`(V3)는 **고아**(어떤 위젯도 미참조) — 무시. (정리 태스크 별도 등재됨.)
- `M_DynamicCrosshair`는 **절차적** 크로스헤어. 스칼라 파라미터: `Spread`, `BaseZoom=2.0`, `InnerRadius=0.03`, `Feather=0.04`, `RingRadius=1.0`.
- Spread 구동: `Source/FPSRoguelite/Private/UI/FPSRRunHUDWidget.cpp:85-87` → `Spread = GetCurrentBloom() × SpreadToPush(0.25)`, 상한 `MaxCrosshairSpread=0.18`([FPSRRunHUDWidget.h:79/83]).
- **원인이 아닌 것(배제 완료)**:
  - ❌ U17 변경 — `CrosshairImage->SetRenderScale`는 위젯 균일 스케일만, UV/머티리얼/Spread 무관. 크기를 키우면 기존 아티팩트가 확대돼 보일 뿐.
  - ❌ 텍스처 샘플러 주소 — 크로스헤어 폴더 텍스처 **20개 전부 Wrap→Clamp로 변경+저장**했으나 **아티팩트 그대로**. ⇒ 원인은 **머티리얼 그래프의 UV 연산**(Frac/타일링/Custom 또는 4-암 오프셋이 텍스처 코너를 드러냄), 텍스처 주소가 아님.
    - ⚠️ 이 **20개 텍스처 Clamp 변경은 현재 커밋 안 됨**(에디터 로드 중). T_AH001-4·T_CH001-9·T_HM_001-4·T_KI_001-3. → 새 세션에서 **유지 vs 되돌림 결정**(중앙 정렬 UI 레티클은 Wrap일 이유 없어 Clamp 유지도 정당하나, 이 버그는 안 고침).
  - Python 콘솔로 그래프 읽기 실패(5.7 API: `MaterialEditingLibrary.get_material_expressions` 부재, `MaterialEditorOnlyData`에 `expression_collection` 프로퍼티 없음) → **그래서 VibeUE 필요**.

## 3. 이 세션(VibeUE)에서 할 일
1. VibeUE로 `Content/Assets/UI/Crosshair/M_DynamicCrosshair` 그래프 열람. **TexCoord → (BaseZoom 스케일 · Spread 오프셋) → TextureSample** 체인 추적.
2. 최대 Spread(≈0.18)에서 **검은 대각선 마크를 만드는 노드** 특정. 유력 가설: (a) UV에 Frac/tiling이 걸려 반복 발생, (b) 텍스처를 4회(암별) 오프셋 샘플하며 코너를 드러냄, (c) 절차적 링/암 요소가 고Spread서 어긋남.
3. **최소 수정**: 샘플 직전 UV를 `saturate`/Clamp로 [0,1] 구속, 또는 노드 내 샘플러를 Clamp로, 또는 Spread 오프셋 상한을 텍스처 안쪽으로. 아티팩트만 제거하고 **정상 확산 룩·정지 상태는 무변**이 원칙.
4. **PIE 검증**: 발사→최대 확산 시 대각선 마크 없음 / 크로스헤어 확산 자체는 정상 / 정지 시 룩 무변 / ADS 숨김 무회귀.
   - 즉효 폴백(머티리얼 손 못 대면): `MaxCrosshairSpread`(현 0.18)를 0.10~0.12로 하향(WBP_RunHUD 클래스 디폴트 또는 C++ 기본값) — 아티팩트 구간 회피(밴드에이드).
5. **범위/제1원리 3줄** 명시: 이건 **U12(동적 스프레드 머티리얼) 폴리시** 영역이지 U17(크기만) 아님. `phase/settings-system`에 폴리시로 얹을지 별도 브랜치로 뺄지 결정.

## 4. 그다음 — U17 머지 마무리 (기능 검증은 이미 통과)
- **U17 기능 PIE 통과**(사용자 확인): 메뉴/인게임 슬라이더로 크로스헤어 크기 실시간 변경·세션 간 영속·기본 1.0·마스터볼륨 무회귀. 코드 검증 완료(빌드 Succeeded + 스모크 4/4 Success).
- 브랜치 `phase/settings-system` 커밋 상태: `aea1237 docs(U17)`(SSOT), `d9373a7 feat(U17)`(GUS `CrosshairScale`+`OnCrosshairSettingsChanged` / 설정위젯 슬라이더 GUS 직접 / 런HUD `CrosshairImage` RenderScale + 스모크). origin 푸시됨.
- **남은 것**: ① `WBP_Settings` 슬라이더 콘텐츠 커밋(사용자 저작, 현재 미커밋) ② 20개 텍스처 Clamp 유지/되돌림 결정 후 처리 ③ 머티리얼 아티팩트 수정 커밋(폴리시) ④ `Scripts/codex-review.ps1 -Base main`(머지 게이트, 메모리 [[codex-review-gate]]) ⑤ `PROGRESS.md` + `Docs/TaskPrompts_Master.md §B U17 ✅` 갱신 ⑥ 에디터 종료 후 `--no-ff` main 머지 + push + phase 브랜치 삭제(§6-7).
  - ⚠️ 머지/checkout 전 **에디터 종료**(에셋 락, [[ue-editor-file-locks-block-git]]).

## 5. 참고 경로
- 머티리얼: `Content/Assets/UI/Crosshair/M_DynamicCrosshair.uasset` / MI: `.../MI/MI_Crosshair_Default.uasset` / 텍스처: `.../T_CH001.uasset`(32×32)
- HUD C++: `Source/FPSRoguelite/Private/UI/FPSRRunHUDWidget.cpp`(NativeTick Spread=cpp:85-87) / `Public/UI/FPSRRunHUDWidget.h`(SpreadToPush=0.25 h:79, MaxCrosshairSpread=0.18 h:83)
- 엔진: UE 5.7 `D:\UnrealEngine\UE_5.7`. 빌드: `Build.bat FPSRogueliteEditor Win64 Development -Project=E:/Git_Project/FPSRoguelite/FPSRoguelite.uproject -WaitMutex`(에디터 종료). 스모크: `UnrealEditor-Cmd.exe <proj> -ExecCmds="Automation RunTests FPSRoguelite.Smoke; Quit" -unattended -nullrhi -nosound -stdout -FullStdOutLogOutput`
- 모델 정책: 구현=Sonnet 위임 / 검증(빌드·스모크·머티리얼 룩 확인)=Opus 직접.
