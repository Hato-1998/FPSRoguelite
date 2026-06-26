# 사운드 설정(마스터 볼륨) — 핸드오프 (새 세션)

> 생성 2026-06-25. 사용자 요청 = 사운드 조절 기능(MVP=마스터 볼륨). 플랜·접근지점 사용자 승인 완료. 이 문서 = 새 세션 착수용 **복붙 프롬프트 + 설계 상세**. 코드/콘텐츠 미착수(설계만 확정).

---

## ▶ 복붙 프롬프트 (새 세션 첫 메시지로 그대로 붙여넣기)

```
사운드 설정(마스터 볼륨) 구현 — main에서 새 브랜치 phase/audio-settings 분기(main이 유일 브랜치). 플랜 우선(HotL).

[먼저 읽기] Game.md(SSOT 허브) + PROGRESS.md(맨 위) + Docs/SSOT/Workflow.md(§6 빌드/모델정책/브랜치) + Docs/SSOT/Architecture.md(§3/§4 신규 클래스·모듈) + Docs/SSOT/PlayerFeel.md(§2-14 HUD/UI) + 이 문서 Docs/SoundSettings_Handoff.md "설계 상세" 전체.
VibeUE 연결 확인(127.0.0.1:8088, 에디터 열림+플러그인). C++ 빌드=에디터 닫고 UBT(Workflow.md §6-6). UMG 위젯/메뉴 배선=VibeUE 또는 사용자.

[목표] 사운드 조절 기능. MVP=마스터 볼륨만. 접근 2곳 = ① 메인메뉴 Settings 버튼 ② 인게임 Esc 오버레이(4인 협동이라 게임 정지 X=논-포즈 오버레이). 확장성-우선: 나중 SFX/Music/UI는 자식 SoundClass 추가만으로(중앙 0수정).

[확정 설계 — SoundClass+SoundMix 표준, 사용자 승인]
1. UFPSRGameUserSettings : UGameUserSettings — UPROPERTY(config) float MasterVolume=1.0 (GameUserSettings.ini 자동 영속) + BlueprintPure GetMasterVolume + BlueprintCallable SetMasterVolume(clamp 0~1, 저장, SaveSettings) + static Get(). 저장 전담.
2. UFPSRAudioSettings : UDeveloperSettings(Config=Game, Category="FPSR Audio") — TSoftObjectPtr<USoundMix> MasterSoundMix + TSoftObjectPtr<USoundClass> MasterSoundClass. 에셋경로 C++ 하드코딩 0(DefaultGame.ini에서 지정).
3. UFPSRAudioSubsystem : UWorldSubsystem — OnWorldBeginPlay에 ApplyMasterVolume()(맵 이동마다 재적용=오디오 디바이스 재생성 견고) + BlueprintCallable SetMasterVolume(float)(GUS 저장+Apply) + GetMasterVolume(). Apply = UGameplayStatics::SetSoundMixClassOverride(World, Mix, Class, Vol, 1.0, 0.0, true) + PushSoundMixModifier(World, Mix). 디버그 콘솔 FPSR.SetMasterVolume(IConsoleVariable).
4. DefaultEngine.ini: [/Script/Engine.Engine] GameUserSettingsClass=/Script/FPSRoguelite.FPSRGameUserSettings ; [/Script/Engine.AudioSettings] DefaultSoundClassName=…/SC_Master(미분류 사운드도 마스터 경유). DefaultGame.ini: [/Script/FPSRoguelite.FPSRAudioSettings] MasterSoundMix=… MasterSoundClass=… (soft path).
5. 게임 PC(AFPSRPlayerController 또는 게임 전용 PC): IA_Menu(Esc) Enhanced Input → WBP_Settings 오버레이 토글(CommonUI Activatable push/pop). 게임 정지 없음.

[콘텐츠/저작 = VibeUE 또는 사용자]
- SC_Master + SMix_Master 에셋 신규(예: /Game/Audio/). 기존 Content/Assets/LowPolyAnimatedModernGuns/Audio/_Common/SC_LPAMG_Master를 SC_Master 자식으로 reparent(총기 사운드도 마스터 적용).
- WBP_Settings(CommonActivatableWidget): 마스터 볼륨 슬라이더(0~1)→AudioSubsystem.SetMasterVolume + 값 텍스트(%) + Back/Close 버튼. 메인메뉴·인게임 오버레이 공용(재사용).
- WBP_MainMenu(Content/UI/Menu/)에 Settings 버튼 추가 → push WBP_Settings.
- IA_Menu 입력에셋 생성(Scripts/gen_input_assets.py 패턴 참고). ⚠️ UE5.7 IMC의 Esc 키 매핑은 Python set_editor_property 미반영 → 에디터 수동 1스텝(Game.md §9).

[검증] 빌드 Succeeded(에디터 닫고 UBT) + 헤드리스 스모크. 볼륨 실제 변동(FPSR.SetMasterVolume 0/0.5/1)·영속(재시작 후 유지)·메뉴/인게임 슬라이더·Esc 오버레이 = PIE(사용자 또는 VibeUE).
[모델/워크플로] 플랜 우선(HotL)→Codex 플랜게이트(plan-codex-comparison-gate, 5분 워치독)→구현(C++=Opus 직접; UGameUserSettings/Subsystem/DeveloperSettings 배선이라 haiku-delegation-security-wiring)→빌드/스모크→Codex 머지게이트→main --no-ff 머지. 콘텐츠 동반 커밋 확인(phase-end-commit-user-content).
[현황] main이 유일 브랜치(balance/pass2+fix/w1-loop 머지 완료, 2026-06-25). 설정/오디오 인프라 0=신규. 패키지=Packaged/Windows/(Development, steam_appid 480). 스팀 MP 인프라는 U11a/P7 기완비.
[주의] 협동이라 인게임은 게임 정지 없는 오버레이(서버 안 멈춤). 카테고리(SFX/Music/UI)는 후속 확장(SC_Master 밑 자식 SoundClass + 설정 필드 추가). 대안=SetTransientMasterVolume(SoundMix/Class 에셋 없이 1-API로 더 가볍게)이나 확장성·표준성 위해 SoundClass 방식 채택.
```

---

## 설계 상세 (위 프롬프트의 근거)

### 난이도 / 범위
- **마스터 볼륨 코어는 낮은 난이도**(UE 표준 SoundClass+SoundMix, C++ ~3 작은 클래스 + 슬라이더 1개). 반나절급.
- 작업량을 가르는 건 **접근 지점** — 사용자 확정 = **메인메뉴 Settings + 인게임 Esc 오버레이 둘 다**.

### 현황 조사 (2026-06-25, 신규 인프라)
- 설정/옵션 UI·`UGameUserSettings` 서브클래스·SoundMix 사용·일시정지 메뉴 = **전부 없음**(grep 0).
- AudioModulation 플러그인 미사용.
- 유일한 사운드클래스 = 총기팩 `Content/Assets/LowPolyAnimatedModernGuns/Audio/_Common/SC_LPAMG_Master`(이걸 SC_Master 자식으로 두면 총기 SFX도 마스터 적용).
- 메뉴 위젯 = `Content/UI/Menu/`(WBP_MainMenu·PlayButton·QuitButton·Result·ReturnButton) — Settings/Options·Pause 위젯 없음 → 신규.

### 왜 SoundClass+SoundMix (제1원리)
1. **제1원리**: 마스터 볼륨은 전역 오디오 스케일. SoundClass 트리(SC_Master) + SoundMix 오버라이드가 표준이며, 미분류 사운드도 `[AudioSettings] DefaultSoundClassName=SC_Master`로 포함. 확장(SFX/Music/UI)이 자식 SoundClass 추가만으로 가능(중앙 0수정) → extensibility-first directive 정합.
2. **Lyra/표준**: Lyra는 Audio Modulation 컨트롤 버스(더 무거움). 여기선 규모상 SoundClass+Mix가 충분·표준 — 의도적 경량 선택.
3. **프로젝트 정합**: 신규 클래스 3개(GameUserSettings/DeveloperSettings/WorldSubsystem) 전부 작고, 에셋경로 하드코딩 0(DeveloperSettings soft ref), 서버 무관(클라 로컬 설정).

### 라이프사이클/엣지
- **적용 시점**: WorldSubsystem `OnWorldBeginPlay`에서 매 맵 적용(메뉴·로비·런 전부) → 레벨 트래블·오디오 디바이스 재생성에도 견고.
- **영속**: `UGameUserSettings`는 `Saved/Config/.../GameUserSettings.ini`에 자동 저장. SetMasterVolume에서 SaveSettings() 호출.
- **인게임 논-포즈**: 4인 협동은 서버가 안 멈추므로 Esc 오버레이는 게임을 멈추지 않고 슬라이더만 노출(기존 §2-2 전역프리즈는 카드선택 전용 — 무관).
- **공용 위젯**: WBP_Settings 하나를 메인메뉴 push + 인게임 Esc push 양쪽에서 재사용.

### 후속(선택)
- 카테고리 볼륨(SFX/Music/UI): SC_Master 밑 자식 SoundClass + UFPSRGameUserSettings에 필드 추가 + WBP_Settings에 슬라이더 추가. 중앙 로직 무변경.
- 기타 설정(해상도/그래픽/감도): UGameUserSettings가 이미 해상도/그래픽 보유 → WBP_Settings 탭 확장.

---

## ✅ 코드 페이즈 완료 (2026-06-26, 브랜치 `phase/audio-settings`)
> **C++/config 전부 구현·검증 완료. 남은 것 = 콘텐츠 저작(에셋·WBP·입력·BP 디폴트) → PIE 검증 → main 머지.**
- 커밋: `3a1476c` feat(audio) 코어 4클래스+배선+config / `0df34d5` fix(audio) Codex 머지게이트 P2 2건(컨트롤러 슬라이더 저장·Back 핸들러).
- 검증: 빌드 Succeeded(에디터 닫고 UBT) + 헤드리스 스모크 `Result={Success}` + Codex 플랜게이트(2블로커 반영)·머지게이트(2 P2 교정) 통과.
- 코드는 콘텐츠 미저작 상태에서도 **안전 no-op**(SoundMix/SoundClass 미설정 시 적용 스킵). 즉 지금 빌드/PIE는 깨지지 않으나, 마스터 볼륨이 실제로 들리려면 아래 콘텐츠가 필요.

### 신규 C++ 심볼 (콘텐츠가 참조할 것)
- `UFPSRSettingsWidget`(UI 베이스) — BindWidget: `MasterVolumeSlider`(USlider, **필수**) / BindWidgetOptional: `MasterVolumeValueText`(CommonTextBlock), `BackButton`(CommonButtonBase). bIsBackHandler=true(Esc/back로 닫힘).
- `AFPSRPlayerController::SettingsWidgetClass`(EditDefaultsOnly) — 인게임 오버레이 클래스.
- `AFPSRCharacter::MenuAction`(EditDefaultsOnly UInputAction) — Esc 입력(IA_Menu).
- `UFPSRMainMenuWidget::SettingsButton`(BindWidgetOptional CommonButtonBase) + `SettingsWidgetClass`(EditDefaultsOnly) — 메뉴 Settings 버튼.
- config는 `/Game/Audio/SC_Master`·`/Game/Audio/SMix_Master` 경로를 이미 가리킴(DefaultEngine.AudioSettings.DefaultSoundClassName + DefaultGame.FPSRAudioSettings) — **이 경로/이름 그대로 저작**.

---

## ▶ 콘텐츠 저작 핸드오프 프롬프트 (새 세션 첫 메시지로 붙여넣기 — 에디터 켜고 VibeUE 재연결)

```
사운드 설정 콘텐츠 저작 — 브랜치 phase/audio-settings(코드 완료, 커밋 3a1476c·0df34d5). 에디터 열고 VibeUE(127.0.0.1:8088, 플러그인 활성) 재연결 후 진행. 안 되면 사용자 수동.

[먼저 읽기] Docs/SoundSettings_Handoff.md(이 문서, "코드 페이즈 완료"+"신규 C++ 심볼") + Game.md §9(UE5.7 IMC 매핑 수동) + 메모리 [[vibeue-mcp-capabilities]]/[[vibeue-render-target-gpu-hazard]](위젯 컨테이너 프로그래매틱 compile/save 손상 위험 주의).

[목표] 코드가 참조하는 오디오/입력/UI 콘텐츠를 저작해 마스터 볼륨을 실제로 들리게+조절가능하게.

[저작 목록]
1. /Game/Audio/SC_Master (USoundClass, 신규) + /Game/Audio/SMix_Master (USoundMix, 신규·빈 채로 OK — 오버라이드는 SetSoundMixClassOverride가 런타임 주입). 경로/이름 정확히(config가 이미 가리킴).
2. 기존 Content/Assets/LowPolyAnimatedModernGuns/Audio/_Common/SC_LPAMG_Master 의 ParentClass를 SC_Master로 reparent(총기 SFX도 마스터 경유).
3. IA_Menu (UInputAction, value_type=Bool) 생성(/Game/Input, Scripts/gen_input_assets.py 패턴) + IMC_Default에 Esc→IA_Menu 매핑. ⚠️ UE5.7은 Python으로 IMC 키매핑이 안 박히니 에디터에서 수동 1스텝(Game.md §9).
4. WBP_Settings (CommonActivatableWidget, 부모=UFPSRSettingsWidget) 신규(/Game/UI 권장): USlider 이름 MasterVolumeSlider(필수) + CommonTextBlock MasterVolumeValueText(옵션) + CommonButtonBase BackButton(옵션). 슬라이더 Min/Max는 코드가 0~1로 세팅.
5. WBP_MainMenu(Content/UI/Menu/)에 CommonButtonBase 이름 SettingsButton 추가 + 위젯 디폴트 SettingsWidgetClass=WBP_Settings.
6. BP 디폴트 배선: BP_FPSRPC(부모 AFPSRPlayerController) → SettingsWidgetClass=WBP_Settings / BP_FPSRPlayer(부모 AFPSRCharacter) → MenuAction=IA_Menu.

[검증 PIE]
- 콘솔 FPSR.SetMasterVolume 0 / 0.5 / 1 → 볼륨 실제 변동.
- 0.5로 두고 에디터/게임 재시작 → 영속(Saved/Config/.../GameUserSettings.ini의 [/Script/FPSRoguelite.FPSRGameUserSettings] MasterVolume).
- 메인메뉴 Settings 버튼 → WBP_Settings 열림, 슬라이더 조절 시 % 갱신·볼륨 변동, Back/Esc로 닫힘.
- 인게임 Esc → 오버레이 열림(게임 안 멈춤=협동), 슬라이더 동작, Esc/Back 닫힘. 패드 슬라이더도 저장되는지.

[머지] 콘텐츠 동반 커밋(content: …) + 빌드/스모크 재확인 불요(C++ 무변경) → main --no-ff 머지 + 브랜치 정리. PROGRESS 갱신.
```
