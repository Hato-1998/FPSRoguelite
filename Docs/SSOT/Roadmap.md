# Roadmap — 구현 상태 / 로드맵 / 디버그 인벤토리 (FPSRoguelite SSOT 분할)

> `Game.md`(SSOT 허브)의 분할 문서. **섹션 번호(§x)는 원본 그대로 보존** — 소스 주석·교차참조 호환.
> 작업 시작 전 허브 `Game.md` + `PROGRESS.md`를 먼저 읽고, 진행 상황·로드맵·재미 게이트·플레이스홀더 전환 관련 확인 시 본 파일을 연다. **라이브 진행현황은 `PROGRESS.md`가 우선** — 여기는 단계 골격.
> 담는 섹션: §7 현재 구현 상태·로드맵(§7-1~7-5) / **§7-6 출시 마일스톤(EA→정식) — M0~M6 정의·Exit Criteria 정본** / §8 디버그·플레이스홀더 인벤토리.

---

## 7. 현재 구현 상태 & 로드맵

### 7-1. 완료 (커밋·빌드 검증됨)
> ⚠️ 아래는 **P0~P1.5-A 초기 스냅샷**이다. 이후 P2~P8 전 구간 + 2차 트랙(U5~U20)·다중맵 U 아크·반동 CrystalRecoil·무기 전면개편·FPSR Data Editor가 **모두 main 머지됨**(§7-3 표 + `PROGRESS.md` '완료 이력'이 최신).
- **P0** 경량 C++ 스캐폴드 (UE5.7, 플러그인 enable, GameplayTags 초안, 빌드 OK, 스모크 테스트)
- **P1-0** 코어 프레임워크 / **P1-1** 플레이어 GAS 글로벌 속성(`UFPSRHealthSet`/`UFPSRCombatSet`)
- **P1-2** EnhancedInput(이동·시점·점프 PIE 확인) / **P1-3** 1인칭 카메라 + Separated Arms
- **P1-4** 무기 기반 — `Weapon/`: 3슬롯 서버권위, Push Model, 장착 시 발사 GA 부여
- **P1-5** 발사 GA — `UFPSRGA_WeaponFire_Hitscan`: 카메라 히트스캔 + 디버그 라인 + 크리티컬 + 적 데미지
- **P1-6** 근접 GA — `UFPSRGA_WeaponMelee`: 전방 구체 오버랩 다중 타격
- **P1-7** 적 — `AFPSREnemyBase`(경량 Pawn) + `UFPSREnemyHealthComponent`: 최근접 추격, 엔진 큐브 placeholder, 데미지 브릿지, 콘솔 `FPSR.SpawnEnemies [N]`
- **통합**: Character에 인벤토리 부착 + 기본무기 지급(서버) + IA_Fire/IA_EquipSlot1~3 배선
- **P1.5-A** 사격 코어 — `UFPSRWeaponFireComponent`: FullAuto 연사 루프 + 반동(카메라 킥) + 확산/블룸. 하드코딩 경로 제거(BP 참조 패턴) 리팩터 완료
- **빌드 성공 + 헤드리스 부팅·스모크 통과**(Fatal 0)

### 7-2. (역사 스냅샷) P1 초기 PIE / 사용자 BP 셋업 — ✅ 완료
> ⚠️ P1 슬라이스(2026-06) 당시의 초기 BP 셋업 블로킹 기록. **전부 완료됨**(§7-3 P1 ✅). 아래는 역사 참조용.
- `BP_FPSRGameMode`(**반드시 `/Game/Core/`**, 부모 `FPSRGameMode`): DefaultPawnClass / PlayerControllerClass
- `BP_FPSRCharacter`(부모 `FPSRCharacter`): IA 8개 + DefaultPrimary/SecondaryWeapon(DA_Weapon_Rifle/Knife)
- `BP_FPSRPlayerController`(부모 `FPSRPlayerController`): DefaultMappingContext=`IMC_Default`
- FireMode: Rifle=FullAuto / Knife=Single·무반동
- → 이후 full-auto PIE 테스트 → 통과 시 P1 완료, P1.5-B(탄약/재장전/ADS)

### 7-3. Phase 로드맵
> 🔁 **완료 표기 전수 재대조 2026-08-13** (§7-6 M0 EC ④, `docs/m0-baseline-reconcile`) — 아래 표의 ✅ 를 코드·에셋 실물과 전수 대조했다. **정정 = P2·P3·P4-D·P8 4행**(각 행에 인라인 각주), **일치 확인 = P0·P1·P1.5·P4-A/B/C·P5·P6·P7**. 대조 명령어·근거는 `Docs/WorkLog.md` 최상단.

| Phase | 산출물 |
|---|---|
| **P0** ✅ | 경량 C++ 스캐폴드 + Git/LFS + 플러그인 + GameplayTags + 빌드 OK + 스모크 |
| **P1** ✅ | Net-aware 1P 캐릭터(Separated Arms) + 무기 2종 + 적 1종 + 데미지 브릿지 |
| **P1.5** ✅ | 사격/이동 감각 (A: 연사/반동/확산, B: MagSize+재장전(예비 무한)/ADS) — 이후 반동/확산=CrystalRecoil heat 모델로 이관(`6f1a981`) |
| **P2** ✅ | SpawnDirector + Flow-Field + Pooling + **거리밴드 티어링**(⚠️ *정정 2026-08-13*: 종전 표기는 "Significance"였으나 **`USignificanceManager`는 미사용**이다 — 플러그인만 enable(`.uproject:58-61`)이고 `Source/` 참조 **0**. `Enemy.md §2-6`·`Architecture.md`의 2026-08-11 정정과 동일한 사실인데 이 표만 안 고쳐져 있었다) + 적 300 안정(근거 = `Performance.md §5` VAT-1 실측 스웜 렌더 2.05ms@300) + **적 이속 ±10% 편차** + **적 근접 데미지·공격토큰 baseline** + **충돌무시 대시** |
| **P3** ✅ | 공유XP + 파티레벨 + **레벨업 스택(프리즈 폐지)** + ~~**정비시간 RunPhase**~~(⚠️ *정정 2026-08-13*: **폐지된 개념이다** — P4-A 재설계가 라운드제·정비시간을 전역 프리즈로 흡수했다(2026-06-04, `Game.md §9`). `ERunPhase` 실물 = `Combat`·`Boss` **2개뿐**(`FPSRGameState.h:15-19`)이고 `RunFlow.md`에 "정비"는 0회 등장 — 이 표가 유일한 출처였다) + **오프닝 카드 시드** + Card UI + 동적 카드풀 + Rarity + 리롤 |
| **P4** ✅ | **P4-A**(재설계) 런 디렉터(시간 미션 스케줄+보스타임) + 확장형 미션 프레임워크+스폰포인트 + **레벨업/미션클리어 전역 프리즈**(라운드제 폐지)+레퍼런스 미션 1종+오프닝시드 / **P4-B** Weapon Modifier Fragment+weapon-scope 카드+미션 보상 실적용 / **P4-C** 무기 7종 / **P4-D** 게임필(히트마커·~~핑~~·위협 인디케이터·사각 오디오⚠️)+PickupRadius/XPGain+HUD위젯. (+원거리 적 규격·공격토큰 확장)<br>⚠️ *정정 2026-08-13*: ① **핑은 미구현이다** — `Source/` 전체에서 `Ping` 참조 **0**, `Content/UI/HUD/`에도 핑 위젯 없음(히트마커·위협 인디케이터는 실존). 도메인 SSOT `PlayerFeel.md §2-14`가 이미 *"핑/Gibs는 후속"* 이라 반박하고 있었다. **재배치 = 킬/크릿 핑 사운드 → M2(오디오) · 팀 핑(수동 1키 + 위험 자동) → M4(협동 UX)**. ② **사각 오디오는 기구만 완료** — `UFPSRBlindspotAudioComponent`는 완전 구현이나 울리는 큐가 `/Engine/VREditor/Sounds/UI/Camera_Shutter`(엔진 **에디터** 에셋 플레이스홀더, `da761449`)다. 쿡 생존 판정 = §7-6 M0 (d) 감사(현 설정 탈락 0 · **조건부** 취약), 프로덕션 사운드 교체 = **M2**. |
| **P5** ✅ | 4인 협동 + 세션(Steam, 2-PC E2E) + **아군 오사(50% 치사 · 기본 OFF 확정 2026-08-07 — 2026-07-01 "기본 ON"을 뒤집음 · 호스트 ON 토글; 코드와 일치 = 후속 없음)** + **수동 부활(DBNO=근접 자동부활)** — 완료 / Iris 평가=**비채택**(Push Model 유지 — 재확인 2026-08-13: `Config/` 전체에 `Iris`/`net.Iris` 0건, `DefaultEngine.ini:44` `net.IsPushModelEnabled=1` 실존. ⚠️ 단 **패키지 빌드에서는 Push Model이 빠진다**(`OpenIssues_Network.md` **N-1**) — 이 "유지"는 에디터/개발 빌드 한정이고, §7-6 M0 (b)가 측정 구성을 고정한 이유다)·NetProfiler(적500 정량)=**U14 이월** |
| **P6** ✅ | 메타 프로그레션(U10 SaveGame) + 보스(U4) + 클리어 플로우(P6-A 셸) |
| **P7** (부분) | CommonUI ✅(실물 정합 — `UFPSRGameViewportClient : UCommonGameViewportClient` 배선 확인) · 오디오 = **배관만 ✅ / `/Game` 사운드 에셋 0** (⚠️ 2026-08-11 실측 정정 2회 — 종전 "오디오 MVP ✅"는 오기록. `Content/Audio`는 `SC_Master`·`SMix_Master` 2개뿐이고 `/Game` 전체에 SoundWave·SoundCue·MetaSound **0개**, 무기 DA `FireSound`는 전부 빈 슬롯. **단 `WarningSound`는 빈 슬롯이 아니다** — `BP_FPSRPlayer`에 엔진 **에디터 전용** 에셋 `/Engine/VREditor/Sounds/UI/Camera_Shutter`가 플레이스홀더로 꽂혀 있다(`da761449`, §7-5 G1 ③ 판정이 성립한 이유). ⚠️ 그래서 리스크는 "무음"이 아니라 **"에디터에서만 울리고 패키지에선 쿡 탈락일 수 있음"** — 쿡 생존 확인 = M0 → 프로덕션 교체 = **M2**) / Insights·README·빌드 폴리시 = 잔여(→ **M5**) |
| **P8** ✅ | 다중맵 심리스 **U 연속필드**(P-0~P-H `34b5eea`) + 반동 **CrystalRecoil** 어댑터(`6f1a981`) + **무기 전면개편**(전면 투사체화 · **점사총 → 라이플 언락 프래그먼트** · 유탄런처 제거 · **SMG 추가** `3adc945` — ⚠️ *정정 2026-08-13*: 종전 "SMG·유탄런처 제거"는 오기록이다. SMG는 **추가**됐고(`DA_Weapon_SMG` 실존 + `DA_LoadoutPool` 참조 + `DA_CardUnlock_SMG`), 실제로 제거된 것은 **점사총**이다. 커밋 원문 및 `CombatWeaponCard.md:114` *"③ 기관단총(SMG) 추가"* 와 일치) + **FPSR Data Editor** P0~P2 + 통합 애니 패스 인프라 · U5~U20 2차 트랙 — 전부 main 머지(상세 `PROGRESS.md` 완료 이력) |

### 7-4. P0/P1 잔여 연결 항목 (착수 전 확인)
- 기본 맵/GameMode config 연결(OpenWorld 템플릿 기본값 제거), `GlobalDefaultGameMode` 지정 검토
- CommonUI viewport/input data 설정 (P1 후반~P3): ViewportClient, InputData·Back/Click·ControllerData, Activatable Widget Stack 레이어(Game/GameMenu/Menu/Modal)

### 7-5. 코어 재미 게이트 (확정 2026-06-10 / **2026-06-16 G1·G2 2분할** — [Review/20260616-volumeup-design.md](../Review/20260616-volumeup-design.md))
로그라이트의 생사 = **30초 루프(스웜 사격 + 카드 선택)가 재밌는가**. 기능 단위 로드맵(§7-3)만으로는 이 판정이 누락되므로 **명시적 게이트**를 둔다.
**왜 2분할인가(컨설트 2026-06-16 수렴)**: 빌드 다양성(②) 판정은 시너지 축(상태이상) 부재 시 *거짓 판정* 위험("손맛이 죽은 건지 축이 없어서인지" 분리 불가) → 손맛/페이싱/성능(G1)과 빌드다양성/시너지(G2)를 분리한다.

- **G1 — 손맛 / 페이싱 / 성능 (양산 강제 관문)**
  - **시점**: 무기 8종(P4-C + U16) 도달이라 표본 충족. **현 로드맵(보스 U3/U4 + 로비 U11a) 진행 후 착수**(사용자 결정 2026-06-16 = 현 로드맵 유지). G1 판정 *전*에 **프리즈 하이브리드 최소 프로토**를 넣어 전역프리즈 baseline vs 하이브리드를 A/B 동시 판정(아니면 이미 아는 페이싱 리스크 재확인에 그침).
  - **판정 항목**: ① 스웜 사격 손맛(타격감·반동·학살 쾌감, §2-4-2) ② 프리즈/하이브리드 페이싱 수용성(§2-2 — 일반 수치카드 비동기 Q/E 전환 필요성 판단) ③ 1인칭 사각 위협 체감(§2-14 사각 오디오) ④ 적 300~500 체감 성능(§5).
  - **통과 = 콘텐츠 양산(무기/카드/적/미션) 개시 허용**. 불합격 = 다음 콘텐츠 전 §2-2(프리즈)/§2-4(사격)/§5(성능) 재설계 우선.
  - **🔁 재판정 조항 (신설 2026-08-11, §7-6 M4 EC ②의 정본)**: G1은 **무기 8종 시점의 1회 판정**이었다. 콘텐츠가 늘면 손맛·페이싱·성능은 다시 갈릴 수 있으므로, **§7-6 M4(콘텐츠 양산) 종료 시 ①스웜 사격 손맛 · ②프리즈 페이싱 · ④적 300~500 체감 성능 3축으로 재판정**한다(판정 주체 = 사용자). **③ 사각 위협 체감은 §7-6 M2 EC ②가 대체**하므로 재판정 축에서 제외한다. 불합격 = M5(EA 진입) 착수 금지.
  - **▶ 판정 결과 (2026-06-30) = ✅ G1 합격** (사용자 결정): ① 스웜손맛·② 프리즈(**B안 baseline-only**, 하이브리드 미평가)·③ 사각(V1)·④ 적500 체감 합격. ⑤ 카드 빌드분기 = G2 보류 유지. **⚠️ §5 적500 정량 측정은 미실시(보류)** → 하드캡 잠정값 유지, 콘텐츠밸런싱/U14 perf 패스로 이월(Performance §5). **U1 양산 해금 → 2차 트랙(U5/U6/U7/U8/U10/U15) 개시 허용.** 판정 시트 = [`Docs/Archive/gates/U1_GateSheet.md`](../Archive/gates/U1_GateSheet.md) §D.
- **G2 — 빌드 다양성 / 시너지**
  - **시점**: 경량 적 상태이상 서브시스템 + 상태축 2~3개(Burn/Shock/Frost/Rupture 중) 프로토 구현 *후*.
  - **판정 항목**: ⑤ 카드 선택의 의미(빌드가 달라지는 체감, §2-3 시너지) ⑥ Fragment × 상태축 물림(상호작용 시너지).
  - **통과 = 빌드 축 확정 → 시너지/카드 콘텐츠 양산 허용**. 불합격 = §2-3 시너지 축 재설계.

---

### 7-6. 출시 마일스톤 (EA → 정식) — 확정 2026-08-11 (사용자 결정)

> **이 절 = 마일스톤의 *정의·Exit Criteria·순서 근거*(설계 = git).** 마일스톤의 *상태·진척·작업 연결*은 **Notion "마일스톤" DB**(현황 = Notion, `Workflow.md` **§6-9 (8)**).
> Exit Criteria는 **여기에만** 둔다 — Notion 마일스톤 페이지는 이 절을 가리킬 뿐 복제하지 않는다(이중 SSOT 금지).
> **§7-3 Phase 로드맵(P0~P8)은 여기서 끝난다** — P 단계는 *코어를 세우는* 축이었고, M 단계는 *출시로 가는* 축이다. 새 작업은 P가 아니라 M에 배정한다.

#### (1) 확정 전제 (사용자 결정 2026-08-11)

| 축 | 결정 | 근거 |
|---|---|---|
| **기간 산정** | **달력 날짜 없음.** 마일스톤 = Exit Criteria / 행 = 상대크기 `S·M·L·XL` / 순서 = `선행작업` 릴레이션 | 1인 + AI 세션 구동은 캘린더 추정의 분산이 커서, 날짜 필드는 **첫 슬립에 죽는다**. 실제로 보드 62행 중 마감일로 관리된 행이 0건이었다 |
| **절단 방식** | **수직 슬라이스** — 마일스톤마다 *플레이 가능한 상태*가 나온다 | §7-5 재미 게이트(G1/G2)는 **플레이 가능한 상태에서만 판정 가능**하다. 기능축 절단(UI 마일스톤·사운드 마일스톤…)은 각 마일스톤 끝에 게임이 없고 통합 리스크를 전부 끝으로 민다 |
| **폴리시** | UI·오디오·VFX·성능·접근성 = 독립 마일스톤이 아니라 **전 마일스톤 관통 레인**(아래 (3)) | 각 슬라이스가 "그 슬라이스에 필요한 만큼"을 함께 만든다. 뒤로 몰면 **전 콘텐츠 소급 패스**가 된다 |
| **출시 형태** | **얼리 액세스 → 정식.** 동결선 2회(EA / 1.0) | EA는 **Steam 실앱 ID·텔레메트리**를 진입 시점으로 앞당긴다 — 정식 1회 출시였다면 미룰 수 있었던 둘이다. ⚠️ **정정 2026-08-11(레드팀)**: 종전 여기 함께 적혀 있던 "세이브 마이그레이션"은 **틀렸다** — `URogueliteSaveGame::CurrentSaveVersion`/`MigrateIfNeeded()` + `FPSRoguelite.Smoke.SaveGame` 자동 테스트가 **P6에 이미 구현돼 있다**(EA 결정과 무관) |

#### (2) 마일스톤 체인

| 코드 | 마일스톤 | 성격 |
|---|---|---|
| **M0** | 기준선 정정 | ⚠️ 기반 (유일한 비-수직슬라이스, 아래 예외 근거) |
| **M1** | 재미 축 확정 (G2 통과) | 수직 슬라이스 |
| **M2** | 감각 완성 (오디오·VFX) | 수직 슬라이스 |
| **M3** | 런 루프 완결 | 수직 슬라이스 |
| **M4** | 콘텐츠 양산 | 수직 슬라이스 |
| **M5** | EA 진입 | 출시 · 🔒 동결선 1 |
| **M6** | EA 운영 → 정식 1.0 | 출시 · 🔒 동결선 2 |

---

**M0 — 기준선 정정**
> ⚠️ **수직 슬라이스 원칙의 명시적 예외다.** 아래는 **콘텐츠가 늘수록 비용이 초선형으로 증가**하는 항목이라 지금이 가장 싸다. 예외임을 감추지 않고 여기 적어 둔다.
> 🔒 **내부 순서 강제**: **(a′)와 (a″)를 먼저 끝내고 (b)를 측정한다.** 둘 다 *측정 대상 자체를 바꾸는* 작업이라, 먼저 재면 그 값이 무효가 된다. 이 순서는 프로즈가 아니라 **보드 `선행작업` 릴레이션으로 건다**((1)의 "순서 = 선행작업" 전제 — 안 걸면 전제가 거짓말이 된다).

- (a) **드리프트 정정 — 전수** ⚠️ *범위 정정 2026-08-11(레드팀): §7-3만 보면 부족하다.* 대상 = **§7-3 완료 표기 + §7-5 판정 기록 + §8 인벤토리 + 도메인 SSOT**. 이미 확인된 실물 불일치: §8·`Game.md §9`가 인용하는 **`L_Sandbox`가 실재하지 않고**(`Content/Maps` = L_Lobby·L_MainMenu·L_Transition·Map_CyberCity·TestWorld → **2026-08-12 커밋 `dca19b4e` 이후 Map_CyberCity 자리는 `L_Map1_City`**), `CombatWeaponCard.md §2-3`이 *"현행 코드는 v1 단일효과"* 라 적었으나 **코드는 v2**다(`UFPSRCardEffect` 실존, `ECardScope` 참조 0).
- (a′) **인게임 맵 확정** ⚠️ *누락 정정 2026-08-11(레드팀): 종전 이 항목이 정본에 없어 (b)를 무효화할 경로가 열려 있었다.* 맵을 갈면 **플로우필드 베이크·스폰포인트·미션존·성능 베이스라인이 전부 무효**가 된다 → **유지할 맵 위에서만 (b)를 측정한다.** **교체는 이미 결정됐다** — `Map_CyberCity` 폐기 → Fab **Synthwave City Kit** 데모맵 기반 재구축(사용자 결정 2026-08-11; §8에 전파 완료). ~~⚠️ **블로커 = 사용자의 Fab 에셋 임포트**라 (b)는 그때까지 열리지 않는다.~~
  - 🔄 **진행 2026-08-12 — P1 완료(커밋 `dca19b4e`)**: Fab 임포트 블로커 **해소**(`Content/Synthwave_city/` 223파일). `Map_CyberCity.umap` 삭제 · `L_Map1_City.umap` 신규 · `DefaultGame.ini` 의 `RunMap`·`MapsToCook` 교체 · 블록아웃 팔레트에 키트 등록.
    사용자 결정 = `DefaultEngine.ini` 에 혼입돼 있던 `r.RayTracing=True`·`r.SkinCache.CompileShaders=True` **되돌림**(RT 전역 on 은 동적 프리미티브마다 가속구조를 갱신해 "적 200-300 저비용" 제1원리와 반대이고 (b) 베이스라인을 오염시킨다).
  - ✅ **완료 2026-08-12 — (a′) 닫힘.** 배치(PlayerStart · 플로우필드 볼륨 · 적 스폰포인트 18 · **세부 프롭 밀도**) 확정 → **"레벨 검증" 0건 + PIE 스모크 통과**.
    **(b) 의 남은 선행은 1건** — ①**(a″) 적 인스턴싱/VAT**. ②[결정] 목표 프레임 예산 수치 + 측정 빌드 구성은 **✅ 확정 2026-08-12**(사용자 결정 → `Performance.md §5` "🎯 목표 프레임 예산 확정" 블록: 적300 평균 ≥60fps·P95 ≤20ms / 적500 헤드룸 ≥30fps / 스웜 렌더 ≤4ms@300 / 측정 = ~~패키지 Test 구성~~ → **Development**(정정 2026-08-13 — 런처 엔진은 Test 빌드 불가, `Performance.md §5` 정정 블록)).
    보드 `선행작업` 릴레이션 3건 중 맵(a′)·프레임예산(②)이 닫혀 실질 남은 선행 = (a″) 1건 — 본문 문구와 릴레이션이 이제 일치한다.
    상세 경위 = `Docs/WorkLog.md` 최상단.
    ⚠️ **아래 "실측 확정치(오전)"는 폐기됐다** — 사용자 레벨 편집으로 지형이 커지고 올라가면서 전부 무효가 됐다. 아래가 정본이다:
    · 주 지면 **10×11 = 110타일 · 450×495m · 윗면 Z=200** 평탄·완전연결 (이전: 7×9 · 315×405m · Z=-50)
    · **2단 지면** — 고가 지면 **Z≈800**(면적 20%), 단차 600cm라 계단·다리로만 연결(`NumLayers=2` 한도에 정확히 맞음)
    · 플로우필드 셀 **181×200 = 36,200 / 40,000**, **`CellSizeOverride = 250` 필수** (기본 200cm 로는 225×248 = 55,800 으로 **상한 초과**)
    · 볼륨 위치 `(0,2250,850)` · Extent `(22600,24900,750)` · PlayerStart `(0,0,299)` → 격자원점 Z=199
    · 지면이 -50 → 200 으로 오르면서 가드레일 #6 제약 신설: `지면Z ≤ 볼륨Min.Z + 150` → **Min.Z ≥ 50**
    · 키트 모듈 격자 **4500×4500cm**(코너 피벗)은 그대로 유효
    🪤 **천장/장식 판은 반드시 NoCollision** — 격자원점+2000 아래에 WorldStatic 판이 있으면 맵 전체 바닥 판정이 그리로 끌려간다. 그리고 **"레벨 검증"이 이걸 못 잡는다**(가드레일 #1은 *비*-WorldStatic 블로커만 본다).
- (a″) **적 인스턴싱 / VAT** ⚠️ *M2 → M0 이동 (사용자 결정 2026-08-11).* 실측: **인스턴싱 0**(적마다 개별 `UStaticMeshComponent`, `InstancedStaticMesh`·`HierarchicalInstanced` 참조 0) · `SignificanceManager` 참조 0 — `Enemy.md §2-6`의 2026-08-11 정정과 일치.
  - **왜 M0인가(제1원리)**: 적 200-300을 싸게 굴리는 것이 이 게임의 제1원리(`Game.md §1`)인데, **액터당 렌더 비용의 지배적 항이 아직 손도 안 댄 상태**다. 이 위에서 잰 값은 "우리가 유지할 아키텍처의 성능"이 아니라 **버릴 아키텍처의 성능**이다.
  - **뒤로 미뤘을 때의 대가**: M2에서 VFX를 다 만든 뒤 예산이 안 나오면 → 인스턴싱 도입 → **VFX 예산 재산정 + 이미 만든 VFX 재작업**. 맵 교체와 같은 부류의 무효화다.
  - ~~⚠️ **`XL`이라 클레임 금지**(§6-9 (8)) — 착수 전 `S`~`L`로 분할해야 한다.~~ → ✅ **분할 완료 2026-08-12(사용자 승인)**: 보드에 **VAT-1**(렌더 경로 설계+셀셰이딩 정합 스파이크, M) → **VAT-2**(ISM 렌더 전환, L) · **VAT-3**(VAT 베이크 파이프라인, L) → **VAT-4**(LOD·거리밴드 재정합, M) 4행 생성, 원 XL 행은 4조각 완료 후 닫는 우산 행. VAT-1부터 착수(`phase/vat-renderpath-spike`).
  - ✅ **VAT-1 완료 2026-08-13 — 렌더 경로 = CPD 채택(ISM 기각)**: 스파이크가 "ISM 설계"가 아니라 대조 실험으로 판정 — **B(MID 폐기+CustomPrimitiveData) = 전 게이트 통과·전 지표 A(현행) 우세**(스웜 렌더 합 2.05ms@300 ≤ 예산 4ms, 여유 2배; 500에서도 3.03ms). 정본 = `Docs/Architecture/0007` · 실측 = `Performance.md §5`. **인스턴싱은 도입하지 않는 것으로 종결**(엔진 동적 인스턴싱+CPD가 병합 보존 — "인스턴싱/VAT"라는 이 항목의 이름과 달리, 필요한 것은 인스턴싱 컴포넌트가 아니라 MID 제거였다). **VAT-2 = "CPD 정식화+잔여 검증(V1·V5)"로 스코프 교체**(사용자 승인 2026-08-13, L→S~M).
- (b) **성능 정량 베이스라인** — §7-5 G1에서 **미실시로 이월된 `Performance.md §5` 적 500 정량 측정**. 셀셰이딩(SRS Custom Depth) on/off × 적 300/500 교차 실측. **(a′)·(a″) 완료 후**에 잰다.
  - ⚠️ **측정 빌드 구성을 반드시 고정**한다 — `Performance.md §5`가 *"Push Model 전제는 출시(패키지) 빌드에선 성립하지 않는다 … 호스트 프레임 예산을 계산할 때 이 차이를 빼고 세지 말 것"* 이라 명시했다(미해결 = `OpenIssues_Network.md` **N-1**). **PIE 실측만 하면 M2~M5의 "베이스라인 대비" 판정이 전부 §5가 금지한 비교 위에 선다.** ~~최소 = **패키지 빌드 1회 포함**, N-1 결정과의 선후를 (5)-6에서 정한다.~~ → ✅ **확정 2026-08-12**: 측정 = ~~패키지 Test 구성~~ → **Development**(정정 2026-08-13 — 런처 엔진은 Test 빌드 불가, `Performance.md §5`), Push Model 꺼진 상태 그대로(전제 명기) — N-1 해소 시 동일 구성 재측정으로 전후 비교.
  - ~~⚠️ **목표 프레임 예산 수치가 어느 SSOT에도 없다**(§5 예산표는 캡/빈도 표라 프레임값 행이 없음) → (5)-6 결정 후 §5에 행 신설.~~ → ✅ **해소 2026-08-12**: `Performance.md §5` 헤더에 "🎯 목표 프레임 예산 확정" 블록 신설.
- (c) **문자열 외부화** — StringTable 파이프라인 + 기존 UI 위젯 전수 이관. **이 선을 넘긴 뒤 만들어지는 UI는 외부화된 채로 태어난다.** ~~로컬라이제이션은 현재 0(`Config/Localization` 부재).~~
  - ✅ **정정 2026-08-13 (EC ④ 재대조) — 위 취소선은 사실과 반대다.** 파이프라인은 **이미 서 있다**: `Config/Localization/`(`Game_Gather.ini`·`Game_Compile.ini`·`Game_ImportCsvTranslations.ini`) · `Content/Localization/Game/`(manifest 95엔트리 + **ko·en·ja** archive/locres) · `Content/StringTables/`(`ST_UI.csv`·`ST_Card.csv`·`ST_CardEffect.csv` — **UStringTable 에셋 0은 설계 의도**다: `LOCTABLE_FROMFILE_GAME` 런타임 직접 로드, `Localization.md` §L-1) · 전용 SSOT `Docs/SSOT/Localization.md`. **Phase A UI 전수 이관도 완료**(`f1b7e314`·`3e23a33a`·`609839c2`). 이 문장을 그대로 두면 M0 Exit 판정이 (c)를 미착수로 오판한다.
  - 🔑 **EC ②의 "검사 대상"은 이미 기계화돼 있다** — `Config/Localization/Game_Gather.ini`가 `SearchDirectoryPaths=Source/FPSRoguelite/`(에디터 모듈 배제) + `ShouldGatherFromEditorOnlyData=false`(`#if WITH_EDITOR` 블록 스킵)이므로, **`Content/Localization/Game/Game.manifest`에 수집된 것 = 프로젝트가 스스로 정의한 검사 대상**이다. 검사기가 정의를 새로 만들 필요가 없다.
  - ✅ **잔여 전부 해소 2026-08-19** (착수 시점 실측 2026-08-13 + 사용자 결정 반영 2026-08-13. 머지 `c485b68e`(C++ 축) · `1c646085`(BP 축)):

    | 구분 | 건수 | 판정 |
    |---|---|---|
    | 디자인타임 플레이스홀더(`WBP_CardEntry`의 `CardName`·`Description…`, `WBP_DamageNumber`의 `99`, `WBP_RunHUD`의 `00:00`, `WBP_Button_Base`의 `TestBlock` 등) | ~10 | ❌ **검사 대상 아님** — 아래 EC ② 판정 정의 |
    | **`WBP_Lobby` 계열** — 인라인 Text 10(`WeaponSlot0~7`·`InviteButton`·`JoinButton`) + BP 그래프 핀 4(`CODE: ------`·`Ready (R)`·`무기 이름`·`선택중`) | 14 | 🟡 **보류** — 로비 전면 개편 대기(아래) |
    | `WBP_LoadoutEntry.NameText` | 1 | 🟡 **보류**(로비 로드아웃 행으로 추정). ⚠️ **참조 0** — C++·다른 위젯 어디서도 안 쓴다. `WBP_PlayButton`·`WBP_ReturnButton` 처럼 **고아일 가능성** → 개편 시 함께 판정 |
    | **C++ LOCTEXT** — Boss `GetDescription` 4(실질은 `#if WITH_EDITOR` 가드 누락) + `FPSRCardEffect.cpp:356` 1(진짜 UI 노출) | 5 | ✅ **완료 2026-08-19**(`c485b68e`). Boss 4건은 이관이 아니라 **가드**로 소거(에디터 전용 문자열은 번역 대상이 아니다), CardEffect 1건은 `LOCTABLE("CardEffect","Fallback.UnknownWeapon")` 이관 |
    | **BP 인라인 Text** — `WBP_Result`·`WBP_DownedOverlay`·`WBP_MissionBanner` | 3곳 → **실측 7건** | ✅ **완료 2026-08-19**(`1c646085`). ⚠️ *실물은 "그래프 핀 3곳"이 아니라* **위젯 트리 Text 3 + 그래프 핀 4** 였다. `WBP_RunHUD` 의 Select `Combat`/`Boss` 는 **대상 아님 확정**(`Combat` 부재 · `Boss` 는 기존 키 `HUD.Boss.NameLabel` 의 부분일치) |
    | 고아 WBP 삭제 — `WBP_PlayButton`·`WBP_ReturnButton`(참조 0 확인) | 2 | ✅ **완료 2026-08-19**(`9fde733a`). `Content` 10루트 `.uasset`/`.umap` + `Source` + `Config` 전수 ASCII·UTF-16 참조 0 확인 후 파일 삭제(리다이렉터는 delete 에서 안 생긴다) |

  - 🟡 **로비 전면 개편으로 인한 보류 15건** (사용자 방향 2026-08-13): 로비를 **UI 화면 방식이 아니라 이동 가능한 작은 방(공간형, PEAK류)으로 전면 재구축**한다. `WBP_Lobby` 와 그 파생 문자열은 개편에서 **통째로 교체될 대상**이라, 지금 StringTable로 이관하면 그대로 버려진다.
    - **이 카브아웃이 EC ②의 취지를 훼손하지 않는 이유**: (c)가 걸고자 한 것은 *"이 선을 넘긴 뒤 만들어지는 UI는 외부화된 채로 태어난다"* 이고, **새 로비는 (3) 폴리시 레인의 UI 규칙("새 화면은 StringTable 경유로 태어난다")이 이미 강제**한다. 죽을 예정인 화면을 외부화하는 것은 그 취지가 요구하는 바가 아니다.
    - ⚠️ **단 `ST_UI.csv` 의 `Widget.Lobby.*` 11키는 지금 고아 상태로 남는다** — 개편 시 재사용하거나 그때 정리한다. **"고아 키니까 지우자"고 판단하지 말 것**(이 보류 기록이 그 이유다).
- (d) **엔진 에셋 쿡 생존 감사** ⚠️ *(d) 교체 2026-08-11(레드팀).* 종전 (d)는 "세이브 버저닝 + 마이그레이션 하네스"였으나 **이미 구현돼 있다**(`URogueliteSaveGame::CurrentSaveVersion`/`SaveVersion`/`MigrateIfNeeded()` + `FPSRoguelite.Smoke.SaveGame` 자동 테스트, P6 `RunFlow.md §2-11`) → 착수 0으로 이미 충족이라 게이트로서 아무것도 막지 못했다. 대신 실제 갭을 넣는다: **`/Engine/` 에디터 전용 에셋을 참조하는 `/Game` 콘텐츠가 패키지 쿡에서 살아남는지**(확인된 사례 = `BP_FPSRPlayer.WarningSound` → `/Engine/VREditor/Sounds/UI/Camera_Shutter`). 살아남지 못하면 §7-5 G1 ③(사각 오디오) 판정 근거가 **패키지 빌드에서 무효**가 된다.
  - ✅ **감사 완료 2026-08-13 — 정본 = [`Docs/Review/EngineRefCookAudit_20260813.md`](../Review/EngineRefCookAudit_20260813.md)** (LFS 실체 4,314/4,314 전건 확인 → 감사 불가 항목 0).
    · **결론: 현 설정에서 쿡 탈락은 0건이다.** UE 5.7의 엔진 콘텐츠 쿡 제외는 **`/Engine/Editor*`·`/Engine/VREditor` 두 접두사뿐**이고(`CookSavePackage.cpp:292`), 그 게이트인 `bSkipEditorContent`가 `BaseGame.ini:113`에서 `false`이며 **프로젝트가 오버라이드하지 않는다**. 즉 §7-5 G1 ③의 판정 근거는 현재 무효가 아니다.
    · **⚠️ 조건부 취약 2건**(에디터 전용 디렉터리를 **가드 없는 런타임 UPROPERTY**로 하드 참조 — `bSkipEditorContent`를 켜거나 UAT에 `-SkipCookingEditorContent`가 들어오면 즉시 탈락): ① `BP_FPSRPlayer.WarningSound` → `/Engine/VREditor/Sounds/UI/Camera_Shutter`(기지 사례, M2 기등록) ② 🆕 **`BP_MissionPointSet`의 Billboard `Sprite` → `/Engine/EditorResources/Spawn_Point`** — 종전 미등록. `UBillboardComponent::Sprite`는 `WITH_EDITORONLY_DATA` **밖**의 순수 런타임 프로퍼티이고(`BillboardComponent.h:23-24`) 이 BP는 `bIsEditorOnly`를 설정하지 않았다(대조군 = 프로젝트 자체 `FPSRFlowFieldBoundsVolume.cpp:28`은 제대로 설정한다). **M2 신규 등록.**
    · 무해 5건(엔진 C++이 `#if WITH_EDITOR` + `EditorOnlyCollect`로 참조를 스스로 차단) · 런타임 쿡 대상 101건 · Slate loose 리소스 4건 · **`Source/` 하드코딩 `/Engine/` 경로 = 0건**.
  - ~~세이브 쪽 잔여 = **EA 요구와의 갭 정의만**(런중 세이브 정책 · Steam Cloud 슬롯 · 손상 복구) → 갭이 있으면 M5로.~~
    - ✅ **갭 정의 완료 2026-08-13 — 정본 = [`Docs/Review/SaveSystem_EAGap_20260813.md`](../Review/SaveSystem_EAGap_20260813.md).** 요구 3축 판정 = 런중 세이브 정책 ❌ · Steam Cloud ❌(코드 0, ini 주석 스텁뿐) · 손상 복구 🟡(백업 슬롯 3단 폴백은 있고 체크섬·부분손상 감지·사용자 고지가 없다). **추가 축 5건 발견**(스키마 공백 · `UserIndex=0` 전제 오류 · 저장 실패 무고지 · 세이브 삭제 경로 부재 · 런 종료 직후 이탈 클라 보상 유실). **전부 M5로 이관**(→ M5 항목).

**Exit Criteria** (판정 주체 = **사용자**, §7-5 게이트와 동일)
① ⬜ `Performance.md §5`에 **측정 빌드 구성이 명기된** 적 300/500 실측 프레임값이 기입됨(추정치 아님). 그 값은 **(a′)로 확정된 맵 위에서, (a″)로 확정된 렌더 경로가 적용된 상태**로 잰 것이어야 한다 — 즉 **우리가 유지할 아키텍처의 수치**여야 한다.
  - ⚠️ *문구 정정 2026-08-13(EC ④ 재대조)*: 종전 *"(a″) **인스턴싱/VAT**가 적용된 상태"* 는 **VAT-1 결론과 모순**이다 — ADR `0007`이 ISM/인스턴싱을 **기각**하고 CPD를 채택했으므로(위 (a″) 참조), 문자 그대로 읽으면 이 EC는 영원히 닫히지 않는다. 실제 요구는 "**MID 제거 + CustomPrimitiveData 렌더 경로 위에서**"다.
② ✅ **완료 2026-08-19 — 하드코딩 UI 문자열 = 0.** (C++ 축 = 머지 `c485b68e` · BP 축 = 머지 `1c646085`. 기계적 근거 = `Game.manifest` 에서 C++ LOCTEXT 네임스페이스 `FPSRBossDefinition`·`FPSRCardEffect` **소멸**, 대상 BP 리터럴 7건 **바이너리 0건**. 경위 = `Docs/WorkLog.md` 최상단) *검사 대상 정의*: C++의 `FText` 리터럴/`LOCTEXT` 및 **BP 위젯의 인라인 Text 프로퍼티**. 검사기는 이 정의를 스스로 정하지 않는다.
  - 🔒 **판정 정의 확정 (사용자 결정 2026-08-13) — "표시되는 것만 센다."** 런타임에 C++ `SetText`로 덮여 **화면에 나타나지 않는** 디자인타임 플레이스홀더는 이 "0"에 **포함하지 않는다**(`WBP_CardEntry`의 `CardName`·`Description…`, `WBP_DamageNumber`의 `99`, `WBP_RunHUD`의 `00:00`, `WBP_Button_Base`의 `TestBlock` 등 ~10건).
    - **근거**: 이 조항이 막으려는 것은 **번역 누락**이다. 화면에 안 뜨는 글자는 번역되지 않아도 플레이어가 볼 일이 없으므로, 제외는 편의가 아니라 조항의 목적에 맞다. 부수적으로 에디터에서 카드·HUD 레이아웃을 눈으로 보며 저작하는 편의도 유지된다(비우면 빈 칸만 보인다).
    - ⚠️ **대가(정직 기록)**: "표시되는가"는 **정적으로 판정할 수 없다** — 이 결정으로 EC ② 검사는 완전히 기계적이지 않게 됐다. 남는 위험 = 나중에 누군가 디자인타임 플레이스홀더를 **실제로 표시되게 배선하면 번역 없이 조용히 출시**된다. 검토했던 완화책(접두 규약 `[DT]` 강제)은 채택하지 않았다 — 되살릴 필요가 생기면 여기서 시작할 것.
    - 🚫 **후속 세션 주의**: 위 ~10건을 "하드코딩 문자열"로 보고 StringTable로 이관하지 말 것. **의도된 상태다.**
  - 검사 대상의 기계적 정의(무엇을 훑는가) = `Game.manifest` 수집분(위 (c) 🔑).
  - ⚠️ **단 출하 `Game.manifest` 의 기계적 정의는 C++ 에만 성립한다**(발견 2026-08-19) — `Game_Gather.ini` 에 `GatherTextFromSource` 단계만 있고 **패키지(에셋) 수집 단계가 없어** 조항이 명시한 *"BP 위젯의 인라인 Text 프로퍼티"* 는 출하 manifest 로 탐지되지 않는다. 이번 BP 축은 **수동 열거 + 바이너리 실측**으로 닫았다. 잔여 내역·보류분 = 위 (c) 표.
    - ✅ **BP 축 자동 검사 신설 2026-08-19** — 정본 = [`Docs/SSOT/Localization.md`](Localization.md) **§L-7**. 감사 **전용** gather 타깃(`Config/Localization/Game_AuditBPText.ini` → `Saved/`)이 `GatherTextFromAssets` 로 BP 위젯 인라인 Text 를 뽑고, `Scripts/localization-audit-bptext.ps1` 이 얼라우리스트(`Config/Localization/BPTextAudit_Allowlist.json`, 문서화된 카브아웃 24항목)와 대조해 **신규가 있으면 exit 1** 로 떨어진다. 실행 = `powershell -File Scripts\localization-audit-bptext.ps1`(에디터 GUI·PIE 불요, 수 초). 실측 = 총 68건 → 게이트 24(전부 면제) + 참고 44.
      - 🔒 **출하 체인은 건드리지 않았다** — 에셋 수집을 출하 타깃에 켜면 "표시되는 것만 센다" 결정으로 제외한 플레이스홀더와 로비 보류분이 출하 매니페스트·아카이브에 편입돼 번역 의무가 생긴다. 그래서 감사 타깃을 분리했다.
      - ⚠️ **완전 기계화는 아니다(정직 기록)** — gather 는 `FText` 만 본다. BP 그래프의 **String 핀 기본값은 어떤 설정으로도 안 잡힌다**(실측: `WBP_Lobby` 의 `무기 이름`·`선택중` 은 에셋에 UTF-16 으로 실존하나 매니페스트에 없다). 이 계급은 여전히 바이너리 실측이 유일한 수단이다. 즉 이 검사기는 **`FText` 계급의 회귀만** 막는다.
      - ℹ️ 훅/CI 배선은 범위 밖(`.claude/settings.json` 은 클론 로컬 — §6-9 (7)). 수동 실행이며 exit 코드는 CI 가 그대로 물 수 있다.
      - 🆕 **부수 발견** = `참고` 44건 중 **무기·프래그먼트·미션 DataAsset 의 인라인 `FText` ~28건**(`DA_Weapon_*.DisplayName`·`DA_Weapon_Rifle.WeaponParts[].DisplayLabel`·`DA_Mission_*.DisplayName/Description`·`DA_Fragment_*.DisplayName`). **EC ② 의 문면 정의(C++ · BP 위젯) 밖이라 이 조항의 "0" 을 깨지 않는다**(그래서 게이트가 아니라 참고로 분류했다). 다만 플레이어 노출 문자열이라 별도 보드 행으로 등록했다.
  - 🟡 **`WBP_Lobby` 계열 15건은 EC ② 판정에서 보류한다**(로비 전면 개편 대기 — (c) 참조). **즉 EC ②는 C++ 5 + BP 그래프 핀 3곳(+ 고아 WBP 2 정리)으로 닫힌다.**
③ ✅ **완료 2026-08-13** — **`/Engine/` 참조 감사 결과가 문서화되고**, 쿡 탈락분은 교체 대상으로 M2에 등록됨. 정본 = [`Docs/Review/EngineRefCookAudit_20260813.md`](../Review/EngineRefCookAudit_20260813.md) (아래 (d) 요약).
④ ✅ **완료 2026-08-13** — (a) 전수 재대조 완료. §7-3(4행 정정) · §7-5 판정 기록(일치) · §8 인벤토리(7행 정정) · 도메인 SSOT(`Performance.md`·`RunFlow.md`·`Enemy.md`·`CombatWeaponCard.md`·`PlayerFeel.md` 정정)가 실물과 일치. 경위 = `Docs/WorkLog.md` 최상단.

---

**M1 — 재미 축 확정 (G2 통과)**
> **콘텐츠 양산(M4)보다 반드시 앞이다.** 시너지 축이 정해지기 전에 만든 카드·무기는 축이 바뀌면 전부 재설계 대상이다(§7-5 G2 근거 그대로).

- 경량 적 상태이상 서브시스템 — ⚠️ **제1원리**: 스웜에 GE 금지(`Game.md §1`). 비-GE 경량 상태 비트로.
- 상태축 2~3개(Burn / Shock / Frost / Rupture 중) + Fragment × 상태축 물림 프로토
- ⚠️ **판정 가능성 전제**: 상태이상은 **보이지 않으면 판정할 수 없다.** M1에는 **플레이스홀더 수준의 상태 VFX·오디오 큐가 반드시 포함**된다(프로덕션화는 M2). 이걸 빼면 §7-5가 G1/G2를 쪼갠 바로 그 실패("손맛이 죽은 건지 축이 없어서인지 분리 불가")를 반복한다.

**Exit Criteria** — **G2 판정 합격**(§7-5: ⑤ 카드 선택의 의미 ⑥ Fragment × 상태축 시너지). 불합격 = `CombatWeaponCard.md §2-3` 시너지 축 재설계 후 재판정.

---

**M2 — 감각 완성 (오디오 · VFX)**
> `/Game` 오디오·VFX가 **실측 0**이다(2026-08-11: SoundWave/Cue/MetaSound 0개, 자작 Niagara 1개). **M4 양산 *전에* 파이프라인이 서야** 이후 콘텐츠가 "소리·이펙트를 달고 태어난다". 뒤로 미루면 전 콘텐츠 소급 패스.
> 🔑 **(1) 폴리시 결정문과의 관계**(레드팀 지적 반영): (1)은 "오디오·VFX = 독립 마일스톤이 아니라 관통 레인"이라 했고 M2는 그 예외가 **아니다**. M2 = **레인의 1회성 부트스트랩** — ①파이프라인을 세우고 ②M2 이전에 만들어진 콘텐츠의 **소급분을 청산**한다. M2 이후 신규 콘텐츠는 레인((3))이 막으므로 소급 패스가 다시 생기지 않는다. 즉 M2는 (1)이 기각한 "기능축 마일스톤"이 아니라 **소급 부채의 1회 상환**이다.

- **오디오**: 무기(발사·장전·탄피·ADS — DA `FireSound` 전부 빈 슬롯) · 피격/사망 · UI · 앰비언트 · 음악 · **사각 오디오 프로덕션 사운드 교체**(`PlayerFeel.md §2-14` USP 보조 장치. ⚠️ *정정 2026-08-11(레드팀)*: `WarningSound`는 **비어 있지 않다** — 엔진 에디터 에셋 `/Engine/VREditor/…/Camera_Shutter` 플레이스홀더가 꽂혀 있다. 할 일은 "채우기"가 아니라 **프로덕션 사운드로 교체 + M0 (d)의 쿡 생존 결과 반영**이다)
- **VFX**: 총구화염 · 탄착 · 피격 · 사망 · XP 오브 · 카드/레벨업 · 보스 · **상태이상**(M1 플레이스홀더의 프로덕션화)
- **성능 재측정** — M0 베이스라인 대비(VFX는 스웜 프레임예산 직결). ℹ️ **적 인스턴싱/VAT는 M0 (a″)로 이동**했다(사용자 결정 2026-08-11) — M2는 *이미 인스턴싱된* 스웜 위에 VFX를 얹고 재는 것이라, 예산 초과 시 되돌릴 대상이 **VFX뿐**이다(렌더 아키텍처가 아니라)

**Exit Criteria** (판정 주체 = **사용자**)
① 무음·무이펙트 슬롯 = 0. *검사 대상 정의*: 무기/적/미션 **DataAsset의 사운드·VFX 슬롯 전수** + **BP 컴포넌트 디폴트의 사운드·VFX 프로퍼티**(⚠️ DA만 훑으면 `WarningSound` 같은 **컴포넌트 프로퍼티를 구조적으로 놓친다** — 이번 오판의 원인) + **`/Engine/` 참조 잔존 0**
② **눈을 감고도 뒤에서 오는 위협을 방향까지 판별 가능**(§2-14 사각 오디오 실플레이 판정 — 4인 혼잡 상황 포함)
③ 적 300 스웜 + 전체 VFX 상태에서 **M0 베이스라인과 같은 빌드 구성으로 측정해** 프레임 예산 내

---

**M3 — 런 루프 완결**

- 포스트런 개인화(로비 선복귀 + 결산을 로비에서 — E안, 보드에 결정완료로 대기 중)
- 메타 프로그레션 실물화 — 언락 곡선(`RunFlow.md §2-11`), §1-C-8 "30시간 여정" 스코프의 실체화
- 난이도 계단식 승급 · 로비 UX
  - 🔄 **로비 전면 개편 방향 (사용자 2026-08-13)** — 로비를 **UI 화면 방식이 아니라 이동 가능한 작은 방(공간형, PEAK류)으로 전면 재구축**한다. 현행 `WBP_Lobby`(무기 슬롯 8 · 초대/참가 버튼 · 준비 표시 등)와 `WBP_LoadoutEntry` 는 **통째로 교체될 대상**이다.
  - **이미 걸린 영향**: §7-6 M0 **EC ②** 의 `WBP_Lobby` 계열 잔여 **15건이 보류**로 빠졌다(위 M0 (c)) — 죽을 화면을 외부화하는 비용을 막기 위해서다. 새 로비는 (3) 폴리시 레인 UI 규칙("새 화면은 StringTable 경유로 태어난다")이 강제하므로 취지는 유지된다.
  - ⚠️ **마일스톤 배정 미확정** — 여기(M3 "로비 UX")에 방향만 적어 둔 것이고, 개편 자체가 M3 소관인지 별도 슬라이스인지는 **사용자 확인 필요**(§6-9 (8) 임의 배정 금지). 보드에도 `마일스톤` 공란으로 행을 열어 뒀다.
  - ℹ️ 범위·수용조건은 아직 없다 — 착수 전에 정해야 한다(현행 로비가 담당하던 것 = Steam 세션·친구초대·무기 선택·준비/카운트다운·심리스 트래블. 공간형으로 옮길 때 **이 기능들이 어디로 가는지**가 설계의 본체다).

**Exit Criteria** (판정 주체 = **사용자**) — ① 로비 → 런 → (사망 및 클리어) → 로비를 **4인으로 3연속 완주**(끊김·재접속 없이) ② 메타 진행이 세이브에 남고 **재시작 후 다음 런에 반영**됨 ③ 난이도 계단을 최소 3단 올릴 수 있고 각 단이 체감상 구분됨

---

**M4 — 콘텐츠 양산**
> ⚠️ **착수 전 사용자 결정 필요 = 콘텐츠 물량표.** 지금 SSOT에 물량표가 **없다**. §1-C-8이 "30시간 여정 / ~90런"을 북극성으로 잡았지만 *무기 N · 카드 M · 적 K · 맵 L · 미션 J*로 환산된 표가 없어서, **이 표 없이는 M4의 Exit Criteria가 성립하지 않는다.**

- **협동유도 스페셜 적**(`Concept.md §1-C-6` 4축) — **USP 직결이라 "일반 적 추가"에 섞지 말 것.** §1-C-8에서 여전히 미확정으로 남은 유일한 항목이다.
- 맵 2·3(Nature / Space) — U 연속필드 아키텍처는 이미 구현·머지됨(`34b5eea`), **콘텐츠만** 남았다
- 무기 · 카드 · 미션 · 보스 물량

**Exit Criteria** (판정 주체 = **사용자**) — ① 콘텐츠 물량표의 **EA 컬럼** 충족 ② **G1 재판정 통과** — 판정 축 정의의 정본은 **§7-5 G1의 재판정 조항**이다(§7-6에만 두면 §7-5 개정 시 조용히 어긋난다)

---

**M5 — EA 진입** · 🔒 **동결선 1 (EA Feature Freeze)**
> EA라서 여기로 **앞당겨진** 항목: Steam 실앱 ID · 텔레메트리 (세이브 마이그레이션은 M0에서 선처리).

- **Steam 실앱 ID 전환** — 현재 `SteamDevAppId=480`(Spacewar 공용 테스트 앱)이다. 전환 시 **세션 · 친구초대 · Steam Cloud 전면 재검증**(`DefaultEngine.ini` §Steam 주석 참조).
- **세이브 — EA 운영 요구 충족**(M0 (d)에서 갭 정의 완료 이관, 2026-08-13. 정본 = [`Docs/Review/SaveSystem_EAGap_20260813.md`](../Review/SaveSystem_EAGap_20260813.md))
  - **Steam Cloud 슬롯 정책** — 코드 0. Auto-Cloud(파일 글롭) vs `ISteamRemoteStorage` API 선택 · Root/Path 글롭 값 · 충돌 해결 정책 미정. **실앱 ID 전환이 선행**(480으로는 파트너 사이트 Auto-Cloud 구성 불가).
  - **런중 세이브 정책** — 현재 저장은 `EndRun` 1회뿐이고 `EFPSRSaveReason::Lobby`는 호출처가 0이다. 호스트 크래시·Alt+F4 = 그 런 전부 소실(`Deinitialize()`가 pending을 flush하지 않는다).
  - **손상 복구 보강 + 저장 실패 고지** — 백업 슬롯 3단 폴백은 있으나 체크섬·부분손상 감지·torn-write 방어가 없고, `OnSaveComplete` 구독자가 0이라 실패가 플레이어에게 보이지 않는다. EA 리뷰의 "진행이 사라졌다"가 여기서 나온다.
  - **세이브 삭제/새 게임 경로** — `DeleteGameInSlot`이 셰이핑 코드에 0건. 진행 초기화 수단도, 손상 세이브를 플레이어가 치울 수단도 없다.
  - ⚠️ **`UserIndex=0` + 단일 슬롯 전제 재검토** — 코드 주석은 "Steam=머신당 단일 유저"라 단정하나 UE 기본 SaveGameSystem은 **Steam 계정이 아니라 Windows 사용자** 기준이다. 공유 PC·Family Sharing·같은 윈도우 계정에서의 Steam 계정 전환 시 세이브가 상호 덮인다. **Cloud를 붙이면 이 충돌이 클라우드까지 전파되므로 Cloud보다 먼저 판정한다.**
  - ℹ️ **저장 스키마 자체의 공백은 M3 소관** — 현재 저장 필드는 `SaveVersion` + 중립 `Reserved0` **2개뿐**이라 "저장할 것"이 아직 없다. 스키마가 확정돼야 위 4항목이 의미를 갖는다(→ M3 메타 프로그레션 실물화).
- **텔레메트리 파이프라인** — §1-C-8이 D1 35% / D7 15%를 북극성으로 확정했는데 **계측 수단이 현재 0**이다. 마일스톤에 없으면 출시일에도 없다.
- **스토어 페이지 + EA 로드맵·FAQ** — Steam EA 필수 제출물.
- **에셋 라이선스 감사 + 크레딧** — Synty · SRS · PWAS · LowPolyAnimatedModernGuns · Paragon/BroBot. 상업 배포 조건이 팩마다 다르고, **Fab 미이관 리스크는 §8에 이미 기록돼 있다**. 출시 직전에 한 팩이 걸리면 그 슬롯 전체를 갈아야 한다.
- **로컬라이제이션 한/영** — M0 외부화 위에서 수행.
- 빌드 파이프라인 자동화 + 크래시 리포팅 + 안정성 패스 (§7-3 P7 잔여 = Insights·README·빌드 폴리시 흡수)

**Exit Criteria** — ① **실앱 ID 기준** 패키지 빌드로 2-PC 세션 성공 ② 1시간 연속 플레이 크래시 0 ③ 텔레메트리 이벤트가 실제로 수신됨(대시보드 확인) ④ 스토어 페이지 승인 + EA 로드맵 게시 ⑤ 라이선스 감사 통과(전 팩 상업 배포 조건 확인 + 크레딧 표기 완료)

---

**M6 — EA 운영 → 정식 1.0** · 🔒 **동결선 2 (1.0 Content Complete)**

- 계측 기반 밸런싱(`BalanceTuning_Reference.md`) · EA 로드맵 공약 소화 · 잔여 콘텐츠 · 로컬라이제이션 확장

**Exit Criteria** (판정 주체 = **사용자**) — ① D1·D7 **실측치 확보**(§1-C-8 목표 대비 교정) ② EA 로드맵 공약 전부 소화 ③ **콘텐츠 물량표의 `1.0` 컬럼 충족** (⚠️ *정정 2026-08-11(레드팀)*: 종전 "1.0 물량표"는 그 표를 만드는 절차가 (5)에 없어 **M6가 정의상 닫히지 않았다.** 물량표는 **하나**이고 `EA`/`1.0` 두 컬럼을 갖는다 — (5)-1에서 한 번에 정한다)

#### (3) 폴리시 레인 (전 마일스톤 관통)

| 레인 | 규칙 |
|---|---|
| **UI** | 새 화면은 **StringTable 경유로 태어난다**(M0 이후 강제). CommonUI 레이어 규약 준수 |
| **오디오** | 새 무기·적·미션은 **사운드 슬롯이 채워진 채로** 머지된다(M2 이후 강제) |
| **VFX** | 위와 동일. **VFX 추가 = 성능 레인의 재측정 트리거** |
| **성능** | 스웜 비용에 닿는 변경(VFX·셰이더·복제)은 **M0 베이스라인 대비 재측정 후 머지** |
| **접근성** | 캐주얼화 레버(조준보조·반동↓·확산↓)는 **옵션으로만** 제공 — 기본값은 코어층 기준(`Concept.md §1-C-4`) |

#### (4) 마일스톤 × 우선순위 축 규칙

- **마일스톤 = 언제(순서)** / **우선순위 = 그 마일스톤 *안*에서의 순서.** 우선순위의 스코프가 마일스톤 내부로 좁혀진다.
- 🔒 **개시 규칙 (신설 2026-08-11, 레드팀 P2)** — 종전엔 "순서 축"이라고만 하고 *무엇이 언제 열리는지*를 안 적어서, 엄격히 읽으면 실버그가 다음 슬라이스까지 동결되고 느슨히 읽으면 아무것도 정하지 않았다.
  - **기본** = M(n)의 Exit Criteria가 닫히기 전에 M(n+1) 행을 **착수하지 않는다.**
  - **예외 3가지**(마일스톤을 건너뛸 수 있다): ① `크리티컬`(사용자 전용) ② **현행 빌드를 깨는 회귀·크래시** ③ **뒤 슬라이스의 판정을 오염시키는 결함** — 예: 적 재스폰 체력바 잔존 버그는 M1 G2 실플레이 판정(상태이상을 눈으로 읽는 것이 핵심)을 오염시키므로 M1 전에 처리한다.
  - 예외로 당겨 쓰면 **행 로그에 사유를 남긴다**(조용한 순서 위반 금지).
- ⚠️ **집행 가능성 = 조회에 실려야 성립한다.** 세션이 행을 고르는 경로(§6-9 (2) 전수 조회 SQL)에 `마일스톤`이 실리지 않으면 이 규칙은 **집행 수단이 없다** — 그래서 (2)의 SELECT에 `마일스톤`·`크기`를 넣는다(같은 SQL 1회라 **호출 수 증가 0**).
- **`크리티컬`만 마일스톤을 무시하는 인터럽트**다(§6-9 (5) 사용자 전용 권한은 그대로).
- **마일스톤 미배정 행** = 아직 어느 슬라이스에 속하는지 정해지지 않은 것. `백로그` 우선순위와는 다른 축이다.
- **완료 행에는 마일스톤을 소급 배정하지 않는다** — 38행 수작업으로 얻는 것이 과거 그래프뿐이다.

#### (5) 미해결 — 착수 전 사용자 결정 필요

| # | 항목 | 필요 시점 |
|---|---|---|
| 1 | **콘텐츠 물량표** — 무기 N · 카드 M · 적 K · 맵 L · 미션 J (§1-C-8 "30시간 / ~90런"의 환산). **`EA`/`1.0` 두 컬럼을 한 번에** 정한다(M4 EC ①과 M6 EC ③이 같은 표를 가리킨다) | **M4 착수 전** |
| 2 | **EA 진입 최소 콘텐츠 기준선** — Steam EA는 "지금 무엇이 들어 있는가"를 명시해야 한다 | **M5 착수 전** |
| 3 | **EA 로드맵 공약 범위** — 공개하면 되돌리기 어렵다 | **M5 착수 전** |
| 4 | **텔레메트리 수집 항목 · 보관 정책**(개인정보 고지 포함) | **M5 착수 전** |
| 5 | ~~인게임 맵 확정~~ → **결정대기 아님. 이미 결정됐고 SSOT에 전파만 안 됐다** — 보드 행 본문에 *"기존 인게임 맵 `Map_CyberCity` 폐기 결정(2026-08-11 사용자)"* + Fab **Synthwave City Kit** 링크가 있다. §8을 아래대로 갱신해 해소했다. **실제 블로커 = 사용자의 Fab 에셋 임포트**(행 본문 "선행 조건") | 해소 — §8 갱신 완료 |
| 6 | **목표 프레임 예산 수치 + 측정 빌드 구성** — §5에 프레임값 행이 없고, `OpenIssues_Network.md` **N-1**(패키지 빌드에서 Push Model이 꺼짐)과의 선후도 미정. 이게 없으면 M2~M5의 "베이스라인 대비" 판정이 전부 무근거 | **M0 (b) 착수 전** (레드팀 신설) |

---

## 8. 디버그 / 플레이스홀더 인벤토리 (프로덕션 전환 대상)

> 🔁 **전수 재대조 2026-08-13** (§7-6 M0 EC ④) — 9행을 실물과 대조했다. **해소 3행**(취소선) · **기전/내용 스테일 4행**(정정) · **절반 해소 1행** · **유효 1행**. 해소 행은 지우지 않고 취소선으로 남긴다(왜 더 안 하는지가 사라지지 않도록, §6-9 (6)과 같은 이유).

| 항목 | 현재 | 전환 계획 |
|---|---|---|
| 발사/근접 DrawDebug 라인·구체 | 검증용 시각화 | ~~`#if !UE_BUILD_SHIPPING` 게이트~~ ✅ **게이트는 이미 걸려 있다** — 다만 `!UE_BUILD_SHIPPING`이 아니라 **`#if ENABLE_DRAW_DEBUG`**(`FPSRGA_WeaponFire_Hitscan.cpp:350`·`FPSRGA_WeaponMelee.cpp:103`, 재대조 2026-08-13). **잔여 = VFX 교체 → M2** |
| ~~`FPSR.SpawnEnemies [N]` 콘솔 커맨드~~ **해소 2026-08-13** | 적 스폰 테스트 | ✅ **제거 대상이 아니라 유지되는 개발 도구다.** ①shipping 제외 이미 적용(`FPSREnemySpawnSubsystem.cpp:1536-1542`가 `#if !UE_BUILD_SHIPPING` 안) ②SpawnDirector(`UFPSREnemySpawnSubsystem`)도 실존 ③**VAT-1 렌더 측정 러너가 상시 쓴다**(`FPSR.SpawnEnemies N 6000`, `Performance.md §5`) |
| ~~적 큐브 placeholder 메시~~ → **적 스웜 메시 = VAT BroBot** | ⚠️ *정정 2026-08-13*: **큐브가 아니다.** `BP_EnemyMeleeBase`가 참조하는 것은 `/Game/Assets/Characters/BroBot/VAT/SM_BroBot_VAT` + `MI_BroBot_VAT_Enemy_CPD`다. 엔진 큐브는 **BP가 메시를 안 준 raw C++ 스폰의 폴백**일 뿐(`DefaultGame.ini:19` + `FPSRPlaceholderVisualSettings.h:27-29`) | ⚠️ *정정 2026-08-13*: 종전 전환계획 "인스턴싱/VAT"는 **기각된 방안**이다 — ADR [`0007`](../Architecture/0007-enemy-swarm-render-path-cpd.md)(채택 2026-08-13)이 ISM/인스턴싱을 기각하고 **개별 `UStaticMeshComponent` + CustomPrimitiveData**를 채택했다(엔진 동적 인스턴싱이 드로우 병합을 보존, 실측 2.05ms@300). §7-6 (a″) 자신이 *"인스턴싱은 도입하지 않는 것으로 종결"* 이라 적었는데 이 행만 안 고쳐져 있었다. **잔여 = U22 최종 아트 메시 교체** |
| XP 픽업 placeholder 스피어(`AFPSRXPPickup`) | 엔진 기본 스피어. ⚠️ *기전 정정 2026-08-13*: **`ConstructorHelpers`는 `Source/` 전체에 0건**이다 — 지금은 config 소프트패스다(`DefaultGame.ini:18` `XPGemMesh=/Engine/BasicShapes/Sphere.Sphere` + `FPSRPlaceholderVisualSettings.h:24-25`) | 실제 XP 오브 VFX + 풀링/배칭(P3-B 후속) → **M2** |
| FP팔/3P 바디 메시 | ⚠️ *정정 2026-08-13*: **Infima는 없다**(`Content/`에 Infima 폴더 0). 실물 = FP팔 `Content/Character/FPArms/SK_NeonV_FPArms`(+`ABP_FPArms`/`ABP_FP_Base`) · **3P 바디 = `/Game/Characters/Blu/…/Blu_` + `ABP_Blu_Body`**. BroBot은 이제 **적 스웜 VAT 메시**로만 남았다.<br>🪤 `BP_FPSRPlayer`가 `SK_LPAMG_Arms_Base_Smooth`도 참조해 **팔 계보가 3중**(NeonV/Blu/LPAMG)이라 문서만으로는 어느 것이 활성인지 판별 불가 | ✅ **3P 바디 = 완료**('Anime Girl Blu' 리스킨이 이미 플레이어에 적용됨 — 이 칸이 "전환 계획"이라 적어 둔 것이 이미 실물이다). **잔여 = FP 팔 Blu 추출 + PWAS 절차 애니**(§1-C-9) |
| ~~적 추격 = 단순 스티어링~~ **해소** | Flow-Field + separation + 배치 | ✅ **P2에서 교체 완료**(재대조 2026-08-13 — `Enemy/` 플로우필드 서브시스템·바운즈 볼륨 실존, §7-3 P2 ✅와 일치) |
| 환경/레벨 지오메트리 | ⚠️ **정정 2026-08-11(레드팀)**: 종전 "L_Sandbox 화이트박스"는 **실재하지 않는다** — `Content/Maps` = `L_MainMenu`·`L_Lobby`·`L_Transition`·**`Map_CyberCity`**(유일 인게임 맵)·`TestWorld`. **현행 = `L_Map1_City`**(🔁 *재정정 2026-08-13*: 이 칸이 아직 `Map_CyberCity`였다 — 실물 `Content/Maps` = `L_MainMenu`·`L_Lobby`·`L_Transition`·**`L_Map1_City`**·`TestWorld`, `DefaultGame.ini:37` `RunMap=/Game/Maps/L_Map1_City`) | 🔄 **맵1 베이스 변경 (사용자 결정 2026-08-11, SSOT 전파 2026-08-11)**: `Map_CyberCity` **폐기** → Fab **Synthwave City Kit** 데모맵 기반 재구축. 종전 "Synty POLYGON Sci-Fi Cyber City(맵1 베이스)"(2026-07-10)를 **맵1에 한해 대체**한다 — **전체 셀/툰 통일(§1-C-9)·로우폴리 제1원리·2맵 Nature·3맵 Space 계획은 불변**. 갱신 지점 = `Config/DefaultGame.ini`의 `RunMap`·`MapsToCook`(코드 참조는 주석뿐). ~~⚠️ **블로커 = 사용자의 Fab 에셋 임포트**~~ · 이 교체는 §7-6 **M0 (a′)** 이며 성능 베이스라인 (b)의 **선행**이다(보드 릴레이션으로 강제).<br>🔄 **2026-08-12 P1 완료(`dca19b4e`)**: 임포트 블로커 해소 · 신규 맵 = **`L_Map1_City`**(`Map_CyberCity.umap` 삭제) · config 2지점 교체 · 팔레트에 `/Game/Synthwave_city` 등록. ~~**잔여 = 맵1 배치**(플레이어 스타트 · 플로우필드 볼륨 ×1 · 적 스폰포인트 Z=바닥+100 · 세부 프롭) → **배치 담당 = 사용자**(2026-08-12 지시)~~ → ✅ **배치 완료 2026-08-12 — (a′) 닫힘**("레벨 검증" 0건 + PIE 스모크 통과. 실측 지형 수치·가드레일은 §7-6 M0 (a′)) |
| ~~PlayerController `[Input] Added DefaultMappingContext` Warning~~ **해소** | 1회성 로그 | ✅ **이미 Verbose다** — `FPSRPlayerController.cpp:205` `UE_LOG(LogFPSR, Verbose, …)`(재대조 2026-08-13) |
| ~~CommonUI `LogUIActionRouter` 에러~~ **해소** | 무해 | ✅ **해결됨** — `UFPSRGameViewportClient : UCommonGameViewportClient`(`FPSRGameViewportClient.h:11`) + `DefaultEngine.ini:20` `GameViewportClientClassName` 배선(재대조 2026-08-13. §7-3 P7 ✅와 일치) |

> **환경 에셋 방향 = Path A (통일 로우폴리 Synty POLYGON 패밀리) 확정 (피벗 2026-07-03, `Concept.md §1-C-9`)**. 근거(제1원리): 로우폴리 = 드로우콜/텍스처 최소 → 적 200-300 프레임예산 보존 + 기존 로우폴리 에셋 정합 + 벤더 통일 = 통일감 자동. (Path B 리얼리스틱 = 무겁·톤충돌로 기각.)
> ⚠️ **착수 전 필수 = 파일럿 검증**: Synty 후보 1팩을 **UE5.7 임포트 + 적 300 스폰 + U7 플로우필드 + 20분 런 프레임 실측** → 통과분만 채택. **Fab 등재 여부 팩별 확인**(예: POLYGON Sci-Fi City는 Fab 미이관·Epic 볼트만일 수 있음).
>
> **🔄 2026-07-10 아트 스택 재확정 (사용자 결정)** — 로우폴리 유지하되 **전체 셀/툰(애니) 통일 룩**으로 상향. 상세 = 아래 슬롯별 표. ⚠️ 종전 여기 지목돼 있던 `AssetReplacement_Synty_ResumePrompt.md`는 **폐기본**(SRS를 "최후 폴백 유료옵션"이라 하는 등 이 SSOT와 모순, `TaskPrompts_Master.md` §U22a/U22b에서 "읽지 말 것"으로 지정)이라 **`Docs/Archive/prompts/`로 내렸다** — 읽지 말 것. 임포트 리스트는 U22 실행 프롬프트가 재작성한다(2026-07-19 정정):
> - **환경** Synty Cyber City(맵1) · **무기** Synty **Military Pack 모듈 백본 + 사이버 리스킨**(Infima 교체) · **캐릭터** 애니 셀 'Anime Girl Blu' 리스킨(플레이어/팀원) · **적 스웜** 별도 저코스트 VAT(애니 리스킨 금지) · **FP 팔** Blu 팔 추출 + **PWAS** 절차 애니 · **UI** Synty **Sci-Fi Soldier HUD** · **VFX** Synty Particle FX + Epic Niagara(무료) · **오디오** Synty 밖(Sonniss·Kenney·Fab).
> - **✅ 렌더러 = SRS(Stylized Rendering System) 확정**(2026-07-10 파일럿 실측 합격). **통합 계약**: 셀/아웃라인 = per-mesh Custom Depth-Stencil 마스킹(효과 받을 메시 `render_custom_depth=True` + `r.CustomDepth=3`). **실측**: 적 300 스웜 커스텀뎁스 = Custom Depth 패스 1.33ms(예산 내) → 스웜 채택 OK. 불통 대안 = DIY 스크린스페이스 Sobel+포스터라이즈. 세부 룩 튜닝은 인게임 반복.
> - ⚠️ **제1원리 리스크**: 셀 아웃라인=post-process(inverted-hull 금지=스웜 드로우콜 2배) · 셀×VAT 스웜 정합 실측 · 애니 고폴리를 스웜에 리스킨 금지 · Synty 5.4→5.7 마이그레이션.
