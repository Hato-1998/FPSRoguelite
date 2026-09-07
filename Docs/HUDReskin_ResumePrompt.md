# HUD 재스킨 — 키아트 v2 배치 · 실행 프롬프트 (새 세션용)

> **한 줄:** 사용자 최종안 키아트(`Docs/Concept/ArcadePixel_KeyArt_UserRef_2026-09-06_v2_HUD.png`)의 HUD 배치를 **현행 위젯의 세터·값 소스·가시성 규칙은 그대로 둔 채 스킨과 배치만** 아케이드 픽셀로 바꾼다. 없는 요소(획득 카드 그리드 · 보스 진행 바 · 스테이지 표기 · 스테이지명 배너 · 스킬 슬롯/스킬 칸 자리)는 새로 만든다.
> 정본 = `Docs/SSOT/ArtDirection.md` **§B-11**(요소 ↔ 시스템 대조표) · §A-3-7(UI 색) · §B-6(모티프) · [ADR 0016](Architecture/0016-art-direction-retro-arcade-pixel.md) D10·「사용자 결정 기록」. 이 문서는 실행 순서다.
> 보드 행 = 「HUD 재스킨 — 키아트 v2 배치(§B-11)」 https://app.notion.com/p/3d33972ddd8881a5a4c9f8adcba56abb (대기 · 미듐 · M1 · 크기 M · 추천모델 Opus).

---

## §0 세션 시작 방법

1. `Game.md` §0-1 라우팅 → `ArtDirection.md §A-3-7 · §B-6 · §B-10 · §B-11` · `PlayerFeel.md §2-14`(HUD 규칙: 실드가 체력 위, `MaxShield==0` 이면 게이지·숫자 함께 숨김) · ADR 0016 「사용자 결정 기록」만 읽는다. WorkLog 는 「HUD 위젯 3종 저작(2026-09-02)」「Synty HUD 팩 통합(2026-09-03)」 두 항목만.
2. **보드 클레임** — `/board 클레임 HUD 재스킨 — 키아트 v2 배치`. 선행(ADR 0016)은 완료. 병렬 충돌 검사 대상 = `Content/UI/HUD/`·`WBP_GameHUD`(사용자 임베드 지점)·`FPSRRunHUDWidget.h`.
3. **에디터 = 켜져 있어야 한다.** 위젯 저작은 VibeUE MCP(에디터 8088 → 자작 프록시 8089)로 한다(2026-09-02 선례 — 3위젯 컴파일 0/0). 헤드리스 커맨드렛으로는 UMG 저작 불가. 단 **git 작업(merge/checkout) 전엔 에디터를 닫는다**(`.uasset` 락).
4. **역할 경계**: 위젯 저작·그래프 배선 = Claude(VibeUE). **`WBP_GameHUD` 임베드·폰트 임포트·최종 PIE 판정 = 사용자.** C++ 표면이 부족하면(§2 의 "표면 확인" 항목) Sonnet 위임 구현 + Opus 검증(§6-5) — 콘텐츠 저작과 **커밋을 분리**한다.
5. `git status` 로 남의 미커밋(드론 세션 `Scripts/gen_voxel_drone.py` 등, Source 변경분)을 확인하고 건드리지 말 것. 커밋은 자기 파일만 명시 경로로.

## §1 결정 (이 세션이 바꾸지 않는 것)

- **배치 = 키아트 v2.** 좌상 획득 아이템 그리드 + 팀 상태 / 좌하 XP·HP·실드 + 스킬 슬롯 / 하단 중앙 스킬 칸(육각) + 스테이지명 배너 / 우하 무기 패널 / 상단 중앙 보스 진행 바 / 우상 스테이지 표기.
- **세터·값 소스·가시성 규칙 유지.** `WBP_PlayerVitals`(`GetShield01/GetHealth01/HasShield/AreVitalsReady`) · `WBP_WeaponPanel`(`GetCurrentAmmo/GetCurrentMagSize/GetCurrentWeapon→Icon/GetOwnedWeapons/GetCurrentSlotIndex`) · `WBP_TeammateVitals`(`AFPSRGameState::GetPartyVitals()`) · `UFPSRRunHUDWidget`(`GetRunClockText/GetRunPhase/GetPartyLevel/GetSharedXP/GetRequiredXPForNextLevel/GetXPProgress01/IsScopeActive`). 바뀌는 것 = 자식 위젯·브러시·폰트·색·슬롯 위치.
- **색 = §A-3-7 그대로.** 본문 `#EAF6FF` · 보조 `#8FA8C4` · 긍정/진행/실드 `#5FE0D2`(실드 채움은 `#2E9BFF` 가능) · 보상/XP/콤보 `#FFC24A` · 경고/체력 위험 `#FF4D5E`. **HP 녹색 금지**(연두 = 파괴물 예약). 월드에 겹치는 UI(적 체력바·비네트)에는 뜨거운 색 금지. HEX 는 sRGB 로 읽어 리니어로 변환해 넣는다.
- **사용자 결정 3건(2026-09-06)**: 하단 육각 = **플레이어 스킬 칸, 자리만**(대시 아님, 기능 없음) · 좌하 **MAIN SKILL 4슬롯 = 그려만 둠**(잠금 아이콘) · **예비탄 없음** — RESERVE 줄 삭제, 탄창 숫자만.
- **크로스헤어 = U12 절차 SDF 유지**(`CrosshairImage` BindWidgetOptional). 픽셀 스냅만. **억제기 코어 방향 마커 유지**(팩맨 미로 길찾기 장치, 연두 + 거리).
- **폰트**: Latin·숫자 = Press Start 2P(OFL) / 한글 = Galmuri(OFL) **후보**. 라이선스·글리프 커버리지 확인 뒤 사용자가 임포트. 이미지의 깨진 한글("경험치IENCE BAR")은 생성 오류 — 라벨은 LOC0 String Table 키로.

## §2 요소별 작업표 (§B-11 을 실행 단위로)

| 요소 | 위젯 | 데이터 | 이 세션에서 |
|---|---|---|---|
| HP · SHIELD (좌하) | `WBP_PlayerVitals` 재스킨 | 기존 | 세그먼트 바(10칸) 픽셀 스타일. 실드 위·체력 아래 유지. HP 색 = 본문색, 위험 시 `#FF4D5E` |
| XP 바 · LV | `WBP_RunHUD`(또는 PlayerVitals 상단) | `GetXPProgress01/GetPartyLevel/GetSharedXP/GetRequiredXPForNextLevel` | "LV 12" + 바(`#FFC24A`). "/15" 상한 표기 없음 |
| TEAM STATUS ×3 (좌상) | `WBP_TeammateVitals` 재스킨 | `GetPartyVitals(false)` | 패널 1개 = 팀원 1명. 초상은 **픽셀 플레이스홀더**(P2/P3/P4 글자 + 플레이어별 프레임 색 = 시안·파랑·보라·청록 변형만). 다운 = `#FF4D5E` + "DOWN" |
| COLLECTED ITEMS 그리드 (좌상) | **신규** `WBP_CardLedger` | `AFPSRPlayerState` 획득 카드 원장 `FFPSRAcquiredCard` + `OnAcquiredCardsChanged` | 4열 그리드, 카드당 픽셀 아이콘 1칸. **아이콘은 희귀도별 기본 아이콘 3종으로 시작**(★1~3), 카드별 고유 아이콘은 후속 행. **확인됨(grep, 2026-09-07)**: 카드 헤더(`Source/.../Public/Card/*.h`)에 아이콘 필드 **없음** → 이 세션은 희귀도 아이콘만. 카드별 아이콘 필드 추가 = 후속 행(C++ + CSV 임포터 `CARDCSV` 정합) |
| STAGE PROGRESS TO BOSS (상단 중앙) | **신규** `WBP_BossProgress` | 런 클록 `GetRunClockSeconds` ÷ BossTime — 스케줄이 런 시작 시 GameState 로 복제됨(`FPSRGameState.h:441` 주석 "client HUD can read MissionWindows + BossTime") | 진행률 = 보스타임까지 시간. 마커 아이콘 + %(v3b). 색 `#5FE0D2`. **확인됨**: `UFPSRRunScheduleDataAsset::BossTime`(기본 300s, `FPSRRunScheduleDataAsset.h:120`)이 GameState 에 런 시작 시 복제됨. BP 에서 그 DA 를 읽는 경로가 노출돼 있는지 확인 — 없으면 `UFPSRRunHUDWidget` 에 `GetBossProgress01()` 1개 추가(C++ · Sonnet 위임) |
| CURRENT STAGE 5-1 (우상) | **신규** 텍스트 블록 | `GetStageIndex()` + 현재 아레나 종류(Combat/Boss, `FPSRArenaTypes.h`) | "STAGE {n}-{m}". m = 서브레벨 순번(Map_1=1, Map_2=2, Boss=B). **표면 확인**: 서브레벨 순번을 BP 에서 읽는 경로 — 없으면 getter 1개 |
| LEVEL n - {스테이지명} 배너 (하단) | `WBP_MissionBanner` 재사용 | 스테이지명 = **LOC0 String Table 신규 키**(`HUD.Stage.Name.{n}`) | 미션 배너와 슬롯을 공유(미션 중엔 미션 문구 우선). 기존 키 `HUD.Boss.NameLabel` 은 건드리지 않음 |
| 무기 패널 (우하) | `WBP_WeaponPanel` 재스킨 | 기존 | "WEAPON" 헤더 + `_Clean` 아이콘 틴트 + 큰 탄창 숫자 "2 / 30". 슬롯 1·2·3 표시 유지. RESERVE 없음 |
| 플레이어 스킬 칸 (하단 중앙 육각) | **신규** 정적 위젯 | 없음 | 육각 프레임 + 잠금 아이콘. 기능·바인딩 0. 나중에 스킬 설계가 채운다 |
| MAIN SKILL 4슬롯 (좌하) | **신규** 정적 위젯 | 없음 | 4칸, 전부 잠금. 바인딩 0 |
| 코어 방향 마커 | 기존 경로 확인(`AFPSRArenaLandmark`·억제기) | — | 연두 마커 + 거리. 이미지에 없어도 유지 |
| 크로스헤어 | `WBP_BasicCrosshair`/`CrosshairImage` | 기존 | 손대지 않음 |

## §3 작업 순서

1. **공통 스타일 먼저** — 픽셀 프레임(9-slice 또는 단색 테두리 2px), 세그먼트 바 위젯 1종(칸 수·색 파라미터), 픽셀 폰트 스타일 3종(제목/본문/숫자). 이걸로 나머지를 조립한다. Synty 팩 위젯(`HUD_SciFiSoldier_*`)은 **더 쓰지 않는다** — 아케이드 픽셀과 언어가 다르다. `_Clean` 무기 아이콘 텍스처만 재사용.
2. **기존 3위젯 재스킨** — 그래프는 그대로, 디자이너 트리만 교체. 노드 삭제가 필요하면 **GUID 로만** 지정하고 지운 뒤 끊긴 `self`·입력을 전수 확인(2026-09-02 사고: 제목으로 지워 가시성 로직이 끊겼는데 컴파일 0/0).
3. **신규 위젯** — CardLedger → BossProgress → Stage 텍스트 → 스킬 칸/슬롯(정적) 순. 이벤트 구동 위젯은 Construct 초기 동기화를 `IsValid` 뒤에 가두지 말 것(메모리 `umg-event-widget-initial-sync`). 자기-토글 오버레이는 루트를 항상 페인트 상태로(`umg-hidden-widgets-dont-tick`).
4. **C++ 표면 부족분**(§2 "표면 확인" 3건) — 있으면 그대로, 없으면 getter 를 **한 커밋으로 묶어** Sonnet 위임 → Opus 검증 → `Build.bat ... -DisableUnity` `Result: Succeeded` 확인 후 위젯 배선.
5. **`WBP_GameHUD` 배치** = 사용자. 슬롯 좌표·앵커 표를 넘긴다(1920×1080 기준, 안전 영역 32px).
6. **PIE 판정** = 사용자(리슨 호스트 + 원격 클라 2인 이상, `L_Lobby` 에서 시작 — 메모리 `pie-2player-test-recipe`). 보드 = `검증중`.

## §4 검증 (완료 판정 — Opus 직접, 하위 모델 위임 금지)

- [ ] 위젯 전부 컴파일 0에러 0경고 **그리고** 그래프 연결 전수 확인(끊긴 `self`·입력 0) — 컴파일 0/0 은 이 결함을 못 잡는다.
- [ ] 색 검사: 위젯 브러시·텍스트 색을 덤프해 §A-3-7 HEX(리니어 변환값) 밖의 색 0. 녹색 계열 0.
- [ ] 문자열: 하드코딩 텍스트 0 — 전부 String Table 키(LOC0 파이프라인 `Docs/Specs/LOC0_StringTablePipeline.md`).
- [ ] 세터 경로 회귀 없음: 실드 0 이면 게이지·숫자 함께 숨김 / 무기 교체 시 아이콘·탄창 갱신 / 팀원 다운 표시 / 카드 획득 시 그리드 즉시 갱신(호스트·원격 클라 양쪽 — 메모리 `event-halves-authority-vs-client`).
- [ ] PIE 육안(사용자): 배치가 키아트 v2 와 같은 자리, 크로스헤어·코어 마커 살아 있음, 4인 화면에서 팀 패널 3개.
- [ ] `capture_preview` 는 배경이 흰색이라 판정 도구가 아니다 — 룩 판정은 PIE 만.

## §5 범위 밖
- 대시 구현(사용자: 만들지 않음) · 플레이어 스킬 시스템 · 예비탄 · 카드별 고유 픽셀 아이콘 · 3P 초상(NEON-V 트랙) · 메인 메뉴/로비("INSERT COIN") · STAGE CLEAR/CONTINUE 전환 화면(`WBP_DownedOverlay` 재스킨은 별도 행).

## §6 함정 (이미 밟은 것)
- Synty 팩 위젯 `bDemoActive` 기본 True(혼자 왕복 애니) · SizeBox 하드 오버라이드(슬롯 작게 주면 잘림) — 이번엔 팩 위젯을 안 쓰지만 남아 있는 임베드가 있으면 확인.
- VibeUE 그래프 API: 노드 제목에 `\r\n<타깃>` 접미사가 붙는다 → 삭제는 GUID 로. `get_connections` 가 한 핀에 소스 2개를 보고할 수 있다 → `disconnect_pin` → `connect_nodes` 로 명시 재연결.
- 위젯 BP 를 프로그램으로 컴파일·저장하면 컨테이너 위젯이 깨질 수 있다(메모리 `vibeue-render-target-gpu-hazard`) — 복구 = git + LFS restore.
- Push Model 은 패키지 빌드에서 꺼진다(메모리 `push-model-off-in-packaged-build`) — PIE 에서만 보이는 복제 타이밍에 기대지 말 것.
- 헤드리스 저작은 에디터가 켜져 있으면 조용히 저장 실패(`save_asset` False, `DONE` 은 찍힘).

---

## §7 진행 기록 · 인계물 (2026-09-07 세션)

### §7-1 확정된 결정 (사용자, 이 세션)

| # | 결정 | 근거 |
|---|---|---|
| 1 | **폰트 = Galmuri11 Bold/Regular** (Press Start 2P 조합 기각) | Press Start 2P 는 한글 0자(실측) → 2폰트 폴백 강제 + 가로폭 과다. Galmuri 는 한글 11,172자 전부 + 라틴 + `★☆—·…` 커버. 상세 = `ArtDirection.md` §B-7 |
| 2 | **카드 희귀도 = ★1~4** (프롬프트의 ★1~3 정정) | `ECardRarity` 가 Common/Rare/Epic/Legendary **4단계** |
| 3 | **기존 3위젯을 같은 경로에서 통째로 재작성** | Synty 잔재를 피해 수술하는 대신 깨끗이. 임베드 재작업 0 |
| 4 | **Synty 팩 위젯 전면 미사용** | 아트 컨셉 확정 전에 붙인 것. `_Clean` 무기 아이콘 텍스처만 재사용 |

### §7-2 §3-4 삭제 — C++ 신규 0줄

프롬프트가 예정한 "표면 부족분 3건 → Sonnet 위임"은 **불필요**하다. 전부 이미 BP 에 열려 있다(실측):
`GetRunTotalDuration()` · `GetStageIndex()`/`GetActiveArena()→StageOrder`·`ArenaRole` · `GetAcquiredCards()`/`OnAcquiredCardsChanged`.
갱신 훅 `OnStageTransitionChanged`(BlueprintAssignable)까지 있어 Tick 없이 이벤트 구동이 된다. 대조표 = `ArtDirection.md` §B-11 하단.

### §7-3 완료물

| 에셋 | 상태 |
|---|---|
| `Content/Assets/Font/Galmuri11` · `Galmuri11-Bold` · `F_Galmuri` | ✅ Monochrome 힌팅, Regular/Bold 한 에셋 |
| `Docs/Licenses/Galmuri-OFL-1.1.txt` | ✅ OFL 1.1 원문 |
| `Content/UI/Style/TS_HUD_*` ×8 | ✅ 컴파일·리니어 색 대조 완료 |
| `Content/UI/HUD/Parts/M_HUDSegBar` | ✅ UI 도메인 · 텍스처 0 · 컴파일 에러 0 |
| `Content/UI/HUD/Parts/WBP_PixelSegBar` | ✅ EventGraph 20연결 · `SetFillPercent` 8연결 전수 확인 |

### §7-4 `WBP_GameHUD` 배치표 (사용자 작업 — 1920×1080 기준, 안전 영역 32px)

전부 **캔버스 슬롯**. 앵커를 화면 모서리에 붙였으므로 해상도가 바뀌어도 모서리 기준이 유지된다.

| 자식 위젯 | 앵커 (Min=Max) | 정렬 | 위치 (X, Y) | 크기 (X, Y) |
|---|---|---|---|---|
| `WBP_CardLedger` | (0, 0) 좌상 | (0, 0) | 32, 32 | 320, 220 |
| `WBP_TeammateVitals` | (0, 0) 좌상 | (0, 0) | 32, 268 | 300, 210 |
| `WBP_BossProgress` | (0.5, 0) 상단중앙 | (0.5, 0) | 0, 32 | 760, 56 |
| `WBP_StageLabel` | (1, 0) 우상 | (1, 0) | −32, 32 | 220, 64 |
| `WBP_PlayerVitals` | (0, 1) 좌하 | (0, 1) | 32, −140 | 420, 108 |
| `WBP_SkillBar` | (0, 1) 좌하 | (0, 1) | 32, −32 | 220, 48 |
| `WBP_SkillHex` | (0.5, 1) 하단중앙 | (0.5, 1) | 0, −120 | 112, 128 |
| `WBP_MissionBanner` | (0.5, 1) 하단중앙 | (0.5, 1) | 0, −32 | 720, 40 |
| `WBP_WeaponPanel` | (1, 1) 우하 | (1, 1) | −32, −32 | 300, 140 |
| 크로스헤어 · 코어 마커 | 기존 유지 | — | — | 손대지 않음 |

⚠️ **Synty 팩 위젯의 SizeBox 하드 오버라이드 함정은 여기 해당 없음** — 새 위젯은 전부 캔버스 앵커라 슬롯 크기를 그대로 따른다.

### §7-5 다음 세션이 이어받을 것

부품 `WBP_PixelSlot`(카드 칸·스킬 칸 공용) → 화면 요소 8종 조립 → §4 검증 → 사용자 임베드·PIE.
**저작 중 PIE 를 켜면 에셋 API 가 전부 조용히 막힌다**(메모리 `vibeue-verification-blind-spots`) — 켜기 전에 한마디 주고받을 것.
