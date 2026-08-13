# WBP 텍스트 인벤토리 (Phase A-2, 2026-08-13)

> 범위: `/Game/UI` 하위 WidgetBlueprint 전수(23개), TextBlock/RichTextBlock/CommonTextBlock/EditableText(Box) 계열의 `Text`/`HintText` 프로퍼티.
> 읽기 전용 조사 — 어떤 에셋도 저장(save_asset)하지 않았다. PIE 미실행.
> 산출물 B(예비 키 CSV 등록) = `Content/StringTables/ST_UI.csv`(같은 커밋).

## 0. 방법론 및 API 갓차

`Docs/SSOT/Localization.md` L-2 키 정책(`<화면군>.<위젯>.<요소>[.<상태>]` PascalCase, `UI` 테이블=`ST_UI.csv`)을 따른다.

**당초 계획 대비 갓차:**
- `UWidgetBlueprint.WidgetTree`/`bindings`(FDelegateEditorBinding[])는 Python 리플렉션에 노출되지 않는다.
  `get_editor_property("widget_tree")` → `Failed to find property`. `get_editor_property("bindings")` →
  `Property 'Bindings' ... is protected and cannot be read`(C++ protected 접근자 — Python 우회 불가, 읽기 전용 정책상 엔진 소스 패치 불가).
- **우회**: VibeUE가 제공하는 `unreal.WidgetService.get_widget_snapshot(path)`(위젯트리 순회 + 전 프로퍼티 export-text 스냅샷)로 대체.
  텍스트 값은 `NSLOCTEXT("[패키지GUID 또는 빈값]", "해시", "리터럴")` export 포맷으로 반환됨 — 이 프로젝트의 모든 WBP Text 프로퍼티는
  현재 **전부 리터럴**이며 StringTable 참조(`LOCTABLE(...)` 포맷) 형태는 하나도 없음(즉 위젯 디자이너 레벨의 선행 이관은 아직 없음).
- **바인딩 판정**: 공식 `FDelegateEditorBinding` 배열 대신, 다음 두 가지 실측 증거로 대체 판정했다(둘 다 읽기 전용 API):
  1. **C++ BindWidget**: `Source/FPSRoguelite/Public|Private/UI/*.cpp`에서 `<위젯이름>->SetText(...)` grep으로 실제 런타임 세팅 확인.
  2. **BP 그래프 SetText**: `unreal.BlueprintService.get_nodes_in_graph(path, "EventGraph"|<함수명>)`으로 해당 위젯의
     `Get <이름>` 변수 노드와 `SetText (Text)` 호출 노드의 공존을 확인(그래프 전체 스코프 상관관계 — 완전한 핀 배선 추적은 아님, 아래 "수동 확인" 참조).
- 이 우회는 `Docs/SSOT/Localization.md`에 명시된 정식 API는 아니므로, **차기 A-3(실제 WBP 편집 스파이크)에서 `bindings` 배열에
  대한 공식 접근 경로(VibeUE 확장 또는 별도 C++ 헬퍼)가 필요한지 재검토할 것.**

## 1. 인벤토리 표

상태 범례: **리터럴**=WidgetTree Text 프로퍼티가 런타임에 재설정되지 않는 실제 표시 문자열(이관 대상) /
**바인딩(C++)**=C++ `SetText` 확인 / **바인딩(BP)**=Blueprint EventGraph `SetText` 확인 / **빈값**=Text/HintText 둘 다 공백.

| WBP | 위젯 | 클래스 | 현재 텍스트 | 상태 | 제안 키 | ko(SourceString) | en | ja |
|---|---|---|---|---|---|---|---|---|
| WBP_BasicCrosshair | (텍스트 위젯 없음) | — | — | — | — | — | — | — |
| WBP_BossHUDBar | BossNameText | TextBlock | `BOSS` | 리터럴(추정 — `OnBossChangedEvent` 그래프가 0노드 반환, 동적 세팅 여부 미확증) | `HUD.Boss.NameLabel` | 보스 | BOSS | ボス |
| WBP_DamageNumber | DmgText | TextBlock | `99` | 바인딩(BP, EventGraph `SetText`) | — | — | — | — |
| WBP_DownedOverlay | DownedText | TextBlock | `다운 — 아군을 기다리는 중` | 바인딩(BP, `Select Text`→`SetText`, 리터럴 문자열이 그래프 핀 기본값으로 존재 — §3 참조) | — | — | — | — |
| WBP_DownedOverlay | ReviverText | TextBlock | `아군 부활 중...` | 바인딩(BP, EventGraph `SetText`) | — | — | — | — |
| WBP_EnemyHealthBar | (텍스트 위젯 없음) | — | — | — | — | — | — | — |
| WBP_GameHUD | (텍스트 위젯 없음, 하위 WBP 컨테이너) | — | — | — | — | — | — | — |
| WBP_HitMarker | (텍스트 위젯 없음) | — | — | — | — | — | — | — |
| WBP_MissionBanner | BannerText | TextBlock | `Mission Start` | 바인딩(BP, `OnMissionChangedEvent`→`SetText`) | — | — | — | — |
| WBP_RunHUD | PhaseText | TextBlock | `COMBAT` | 바인딩(BP, `Select`→`SetText`, "Combat"/"Boss" 리터럴이 그래프 핀 기본값 — §3 참조) | — | — | — | — |
| WBP_RunHUD | ClockText | TextBlock | `00:00` | 바인딩(BP, `SetText`) + 숫자 포맷(L-6 비대상) | — | — | — | — |
| WBP_RunHUD | FrozenBanner | TextBlock | `LEVEL UP — SELECT CARDS` | **리터럴**(그래프는 `Set Visibility`만 호출, `SetText` 없음 — 텍스트는 디자인타임 값이 실제 표시값) | `HUD.Run.FrozenBanner` | 레벨 업 — 카드를 선택하세요 | LEVEL UP — SELECT CARDS | レベルアップ — カードを選択 |
| WBP_RunHUD | LevelLabel | TextBlock | `Level : ` | **리터럴**(is_variable=False, 그래프 미접촉) | `HUD.Run.LevelLabel` | 레벨 :  | Level :  | レベル :  |
| WBP_RunHUD | LevelText | TextBlock | `1` | 바인딩(BP, `To Text(Integer)`→`SetText`) + 숫자(L-6 비대상) | — | — | — | — |
| WBP_ThreatIndicator | (텍스트 위젯 없음) | — | — | — | — | — | — | — |
| WBP_BackButton | Label | TextBlock | `Button` | **리터럴**(그래프 미접촉) — ⚠️ 라벨 오류 의심(§3) | `Menu.Button.Back` | 버튼 | Button | ボタン |
| WBP_MainMenu | (텍스트 위젯 없음, Play/Quit/Settings 버튼 WBP 조합) | — | — | — | — | — | — | — |
| WBP_PlayButton | Label | TextBlock | `Play` | **리터럴** | `Menu.Button.Play` | 플레이 | Play | プレイ |
| WBP_QuitButton | Label | TextBlock | `Quit` | **리터럴** | `Menu.Button.Quit` | 종료 | Quit | 終了 |
| WBP_Result | OutcomeText | TextBlock | (빈 문자열) | 바인딩(BP, `Switch on EFPSRRunOutcome`→`SetText`, 결과별 리터럴이 그래프 핀 기본값 — §3 참조) | — | — | — | — |
| WBP_ReturnButton | Label | TextBlock | `Return to Menu` | **리터럴** | `Menu.Button.Return` | 메뉴로 돌아가기 | Return to Menu | メニューに戻る |
| WBP_SettingsButton | Label | TextBlock | `Settings` | **리터럴** | `Menu.Button.Settings` | 설정 | Settings | 設定 |
| WBP_Settings | TitleText | TextBlock | `MASTER VOLUME` | **리터럴**(is_variable=False) | `Settings.MasterVolume.Title` | 마스터 볼륨 | MASTER VOLUME | マスターボリューム |
| WBP_Settings | MasterVolumeValueText | CommonTextBlock | (빈 문자열) | 바인딩(C++, `FPSRSettingsWidget.cpp:123` `SetText(FText::AsPercent(...))`) + 숫자(L-6 비대상) | — | — | — | — |
| WBP_CardEntry | CardNameText | TextBlock | `Card Name` | 바인딩(C++, `FPSRCardEntryWidget.cpp:62`) | — | — | — | — |
| WBP_CardEntry | RarityText | TextBlock | `Rarity` | 바인딩(C++, `FPSRCardEntryWidget.cpp:92-107`, 이미 `CardEffect.Rarity.*` LOCTABLE 키 사용 중) | — | — | — | — |
| WBP_CardEntry | DescriptionText | TextBlock | `Description Description Description` | 바인딩(C++, `FPSRCardEntryWidget.cpp:115`) | — | — | — | — |
| WBP_CardEntry | MagnitudeText | TextBlock | `Magnitude` | 바인딩(C++, `FPSRCardEntryWidget.cpp:122`) | — | — | — | — |
| WBP_CardSelecter | RerollChargesText | TextBlock | `Count : 3` | 바인딩(C++, `FPSRCardSelectWidget.cpp:93` `SetText(FText::AsNumber(Charges))`) — ⚠️ "Count : " 프리픽스는 런타임에 소실됨(§3) | — | — | — | — |
| WBP_LoadoutEntry | NameText | TextBlock | `Weapon` | **리터럴**(그래프·C++ 어디서도 세팅 근거 없음 — ⚠️ 미배선 의심, §3) | `Widget.LoadoutEntry.NameLabel` | 무기 | Weapon | 武器 |
| WBP_Lobby | WpnTxt0 | TextBlock | `FullAutoRifle` | **리터럴** | `Widget.Lobby.WeaponSlot0` | 풀오토 소총 | FullAutoRifle | フルオートライフル |
| WBP_Lobby | WpnTxt1 | TextBlock | `Sniper` | **리터럴** | `Widget.Lobby.WeaponSlot1` | 저격소총 | Sniper | スナイパー |
| WBP_Lobby | WpnTxt2 | TextBlock | `Shotgun` | **리터럴** | `Widget.Lobby.WeaponSlot2` | 샷건 | Shotgun | ショットガン |
| WBP_Lobby | WpnTxt3 | TextBlock | `Bazooka` | **리터럴** | `Widget.Lobby.WeaponSlot3` | 바주카 | Bazooka | バズーカ |
| WBP_Lobby | WpnTxt4 | TextBlock | `GrenadeLuncher` | **리터럴** — ⚠️ 오타("Luncher"→"Launcher", §3) | `Widget.Lobby.WeaponSlot4` | 유탄발사기 | GrenadeLuncher | グレネードランチャー |
| WBP_Lobby | WpnTxt5 | TextBlock | `ChargeLaser` | **리터럴** | `Widget.Lobby.WeaponSlot5` | 차지 레이저 | ChargeLaser | チャージレーザー |
| WBP_Lobby | WpnTxt6 | TextBlock | `LMG` | **리터럴** | `Widget.Lobby.WeaponSlot6` | 경기관총 | LMG | 軽機関銃 |
| WBP_Lobby | WpnTxt7 | TextBlock | `BurstRifle` | **리터럴** | `Widget.Lobby.WeaponSlot7` | 점사소총 | BurstRifle | バーストライフル |
| WBP_Lobby | InviteText | TextBlock | `Invite Friends (F)` | **리터럴** | `Widget.Lobby.InviteButton` | 친구 초대 (F) | Invite Friends (F) | フレンドを招待 (F) |
| WBP_Lobby | PlayerListText | TextBlock | (UMG 기본값 `Text Block`) | 빈값+바인딩(BP, `RefreshPlayerList` 계열에서 `SetText` 확인) | — | — | — | — |
| WBP_Lobby | ReadyText | TextBlock | `Ready (R)` | 바인딩(BP, EventGraph `SetText`, `OnToggleReadyClicked`/`OnReadyRefreshed`로 토글 추정) | — | — | — | — |
| WBP_Lobby | LobbyCodeText | TextBlock | `CODE: ------` | 바인딩(BP, EventGraph `SetText`, "CODE: " 프리픽스가 그래프 핀 리터럴일 가능성 — §3) | — | — | — | — |
| WBP_Lobby | JoinCodeInput | EditableTextBox | Text='', HintText='' | 빈값(Text·HintText 둘 다 미설정 — ⚠️ 플레이스홀더 힌트 부재, §3) | — | — | — | — |
| WBP_Lobby | JoinText | TextBlock | `Join (V)` | **리터럴** | `Widget.Lobby.JoinButton` | 참가 (V) | Join (V) | 参加 (V) |
| WBP_PrimaryGameLayout | (텍스트 위젯 없음, 레이아웃 컨테이너) | — | — | — | — | — | — | — |

## 2. 요약

- **WBP 총수**: 23개(`/Game/UI` 재귀, 클래스=WidgetBlueprint 전수).
- **텍스트 위젯 총수**: 38개(TextBlock/RichTextBlock/CommonTextBlock/EditableText 계열, `Text`/`HintText` 보유 기준).
- **이관 대상(리터럴 → 키 등록)**: **20개** — 아래 §1 표에서 "제안 키" 채워진 행. `ST_UI.csv`에 신규 20행 추가함(기존 5행 보존, 충돌 없음).
- **비대상**: 18개 — 바인딩(C++) 6개, 바인딩(BP EventGraph SetText) 10개(숫자 포맷 2개 포함), 빈값 2개(PlayerListText는 빈값+BP바인딩 중복 집계 제외 시 순수 빈값은 JoinCodeInput 1개).

## 3. 발견 사항 (범위 밖이지만 보고)

읽기 전용 조사 중 발견한, 이번 태스크 범위(텍스트 인벤토리) 밖의 관찰 사항. **수정하지 않았음** — 사용자 판단 필요.

1. **`WBP_BackButton.Label` = `"Button"`** — Back 버튼인데 라벨이 "Button"으로 되어 있음. UMG 기본 placeholder(`NSLOCTEXT("UMG","TextBlockDefaultValue","Text Block")`)와는 다른 값이라 의도적으로 타이핑된 값으로 보이나, 의미상 "Back"이어야 할 자리. 인벤토리에는 현재 값을 그대로 반영했다(임의로 "Back"으로 바꾸지 않음) — 실제 수정은 별도 확인 후 진행 권고.
2. **`WBP_Lobby.WpnTxt4` = `"GrenadeLuncher"`** — 오타("Luncher" → "Launcher"). 8개 무기 슬롯 라벨 전체가 하드코딩 리터럴이며 데이터 기반이 아님(무기 인벤토리 시스템과 별개로 텍스트만 나열된 디버그/로비 표시용으로 보임) — 로케일별 무기명 번역이 실제로 필요한지(고유명사로 유지할지) 기획 확인 필요.
3. **`WBP_CardSelecter.RerollChargesText`**: 디자인타임 텍스트는 `"Count : 3"`이지만 C++(`FPSRCardSelectWidget.cpp:93`)은 `SetText(FText::AsNumber(Charges))`로 **텍스트 전체를 숫자만으로 덮어쓴다** — "Count : " 접두어는 런타임에 표시되지 않는다. 디자인타임 placeholder와 실제 런타임 표시가 다른 잠재적 UX 갭(라벨 없이 숫자만 노출)으로 보임.
4. **`WBP_LoadoutEntry.NameText` = `"Weapon"`**: BP 그래프(`OnSelectClicked`뿐) 및 C++ 어디에서도 이 위젯에 대한 `SetText` 근거를 찾지 못했다. 로스터 아이템마다 무기 이름이 바뀌어야 할 위젯으로 보이는데 현재는 정적 리터럴 — 배선 누락 가능성(별도 확인 권고, 이번 커밋은 인벤토리만).
5. **`WBP_RunHUD` / `WBP_Result`의 BP 그래프 핀 리터럴**: `PhaseText`(Select "Combat"/"Boss"), `OutcomeText`(Switch on EFPSRRunOutcome), `DownedText`(Select Text A/B)는 WidgetTree Text 프로퍼티가 아니라 **Blueprint 그래프 노드의 핀 기본값**으로 실제 표시 문자열을 들고 있다. 이번 인벤토리는 WidgetTree 순회만 지시받았으므로 이 핀 리터럴들은 표에 개별 행으로 넣지 않았다 — **A-3에서 별도로 "BP 그래프 핀 리터럴 스윕"이 필요**(대상 후보: WBP_RunHUD의 Select 노드 2개 문자열, WBP_Result의 Switch 노드 N개 문자열, WBP_DownedOverlay/WBP_Lobby의 Select Text 노드).
6. **`WBP_BossHUDBar.BossNameText`**: `OnBossChangedEvent` 함수 그래프가 `get_nodes_in_graph`에서 0개 노드를 반환했다(그래프 이름 불일치 가능성 또는 실제로 빈 스텁). 리터럴로 잠정 분류했으나 재확인 권장.

## 4. 수동 확인 필요 목록

- `WBP_BossHUDBar` — `OnBossChangedEvent` 그래프 0노드 반환 원인(그래프명 확인 또는 에디터에서 직접 열람).
- `WBP_Lobby.JoinCodeInput` — HintText 부재가 의도적 공백인지 누락인지.
- BP 그래프 핀 리터럴 전수(§3-5) — `bindings` 배열 프로퍼티가 protected라 공식 API로 못 읽으므로, 이 우회(그래프 노드 상관)로는 완전한 배선 추적이 아님. A-3에서 각 후보 WBP를 에디터로 열어 육안 확인 권고.
