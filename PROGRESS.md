# PROGRESS — 진행 현황 (라이브 핸드오프 문서)

> 다른 세션/다른 AI가 **즉시 이어받기** 위한 단일 진행 현황 문서.
> **작업 단계를 끝낼 때마다, 그리고 중단 전 반드시 이 파일을 갱신하고 커밋한다.**
> 확정 설계·기획·코드구조·규칙은 `Game.md`(**SSOT 허브** → 도메인별 `Docs/SSOT/*.md`, 작업별 라우팅은 허브 §0-1), **완료 작업 상세는 `git log --oneline`**. 여기엔 *무엇을 했는지*만 요약한다.

**최종 갱신: 2026-07-18**

## 🔔 현재 상태 (2026-07-18 · W2-A 코드 품질 그라인딩 **✅완료·main 머지** — 다음 프론티어 = U22 전체교체)

> **U21 파일럿 게이트 통과**(사용자 판정 2026-07-18): S1 단차 walkability ✅ · S3 셀 아웃라인×VAT 정합 ✅ · S4 성능 실측 ✅("다음 유닛 넘어갈 만한 퍼포먼스"). 아트 방향 = 사전확정 셀/툰 피벗(메모리 `synty-anime-cel-art-pivot`)이 **파일럿으로 검증됨** → U22(전체교체) 게이트 해제. **✅하위결정 2건 확정(2026-07-18)**: ① 무기 백본 = **Synty Military 전환**(→ 나중에 SF 무기로 리스킨/변환) ② 캐릭터 애님 = **3인칭 Blu · 1인칭 PWAS**(손저작 AnimBP 대체 → U15 1P·U19 3P 손저작 계획은 이 파이프라인에 흡수, U20 적 VAT만 별도로 U22 적교체 후 베이크).
> **추가 수정 = SRS 아웃라인 거리 헤이즈**(아래 §⑥). 코드 미커밋 0. 작업트리 = **사용자 콘텐츠만 미커밋**(아래 §미커밋 콘텐츠 — SRS 수정은 `L_GameFloor.umap` 인스턴스에 포함).

### ⑦ W2-A 코드 품질·기술부채 그라인딩 = ✅완료·main 머지 (2026-07-18)
> U21 완료 후 "전체 프로젝트 그라인딩"의 **코드축** — U22와 무관하게 살아남는 C++ 로직/구조만 대상(콘텐츠·임시값[U14]·성능[U25]·정확성버그[W2-B] 제외). 6 도메인 감사(파인더 Sonnet) → Opus 검증 → Codex 적대 토론 → 재작업.
- **감사**: 런타임 223 + 에디터 35 = 258 파일 전수. 원시 findings 28 → **P1 없음, 전부 P2/P3**(그라인딩 성격 확인). 문서 = `Docs/codex-reviews/w2a-code-quality-20260718.md`(gitignore).
- **수정 20건**(`0a8cc495` + cook fix `4a6c7b1b`): ① 주석 드리프트 9(적/플로우필드/미션/투사체/무기타입/시작무기 코드-모순) ② §6-2 하드코딩 3(XP젬/적/보스 placeholder 메시 `ConstructorHelpers`→config `FPSRPlaceholderVisualSettings` BeginPlay fallback) ③ dead code 2(`ServerRequestReturnToMenu` RPC·`ContentBrowser` 에디터 의존) ④ UI.Layer 태그 SSOT 네이티브화(`FPSRUITags`, 11사이트, ini 중복 제거) ⑤ `CopyProperties` Push Model dirty-mark(seamless travel 복제 정합) ⑥ 데이터드리븐 4(투사체 머즐오프셋 DA·rarity LOCTEXT·Assembler 폴더명 config·적 캡슐반높이 공유상수).
- **Codex 합의 DEFER**: `DownedMoveScale`(BP참조 grep불가)·PelletCount EditCondition(의도적 설계)·`AvailableModifiers`(직렬화 필드→U22 재저장 시)·크로스헤어 프리셋(UI 구조변경 동반)·enum 중간값 제거(직렬화 리스크, 값 보존·주석만)·저가치 백로그(IsMapReady·미션 DrawDebug cvar).
- **판정 핵심**: re-run-safety 계열(`ResetForNewRun` 등, `bRunActive` 래치 뒤)은 **의도적 전방 인프라 → 보존**(파인더 오탐 없이 방어). enum 중간값은 **제거 대신 주석**(직렬화/BP핀 안전).
- **검증**: 빌드 `Result: Succeeded`(-WaitMutex 풀빌드, UHT WarningsAsErrors) · 스모크 `ModuleLoads Result={Success}` · Codex 머지게이트 = P2 1건(패키지 cook)만 → `4a6c7b1b`로 해소.
- **⚠️ 후속(사용자 판단)**: cook 규칙(`/Engine/BasicShapes` DirectoriesToAlwaysCook)은 **패키지 빌드 미검증**(현 세션 빌드/스모크만) — 실 패키지 시 XP젬 가시성 확인 권장. 단 dev 스캐폴딩이라 U22서 실메시 교체되면 무의미.

### ⑧ W2-A 후속 = 보류항목 4건 처리 (2026-07-18, `phase/w2a-deferred-cleanup` → main)
> W2-A에서 DEFER했던 것을 **사용자 판단으로 지금 처리**("일부러 터뜨려 지금 잡아둬야 나중에 편해" — 호환 껍데기/리다이렉트 없이).
- **제거 3건**: `DownedMoveScale`(죽은 UPROPERTY) · `AvailableModifiers`(폐기 필드) · `EFPSRWeaponArchetype::Burst`(미사용 아키타입). **콘텐츠 스캔으로 영향 에셋 0건 사전 확인**.
- **`ECardGroup::WeaponUnlock`은 보존(판정 번복)**: 제거 시도 중 **7개 해금 카드 에셋(`DA_CardUnlock_*`)이 실제로 이 값을 분류로 사용** 중임을 발견 — 종전 "미사용" 판정은 **C++ 한정 조사의 누락**이었음. dead CODE이지 dead DATA가 아니므로 값 유지 + "Weapon으로 재태깅하면 `TargetWeapon`이 설정돼 실동작이 바뀐다"는 경고를 주석에 명시.
- **크로스헤어 색 프리셋 데이터에셋화**: `UFPSRCrosshairColorPresetDataAsset`(이름+색 배열) 신설, 설정 위젯이 프리셋 수만큼 스와치 버튼을 **런타임 생성**(고정 버튼 5·핸들러 5·하드코딩 색 5 제거). `UFPSRColorPresetButton`(UButton 파생)이 자기 인덱스를 네이티브 델리게이트로 실어 보내 **별도 버튼 WBP 없이** 동적 생성 가능.
- **Codex 리뷰**: P2(프리셋 미배선 시 색 설정 사라짐) = 수용, 단 fallback 복구 대신 **경고 로그 2종**으로 표면화. **P1(Burst 제거가 뒤 enum ordinal을 밀어 무기 DA 깨짐) = 오탐 판정·미적용** — UE tagged 직렬화는 enum을 이름으로 저장하며 `DA_Weapon_Bazooka/ChargeLaser/Knife/Shotgun/Sniper`가 `"EFPSRWeaponArchetype::<값>"` 문자열을 그대로 보유함을 실증(FullAuto 무기가 목록에 없는 것도 기본값 미직렬화와 정합).
- **검증**: 빌드 `Result: Succeeded`(⚠️XGE가 `C1076` 힙고갈로 실패 → 메모리 처방대로 `-NoXGE` 전환) · 스모크 `ModuleLoads Result={Success}`(신규 DLL 기준 재실행 — 실패 빌드의 구 DLL로 돈 결과는 폐기).
- **⚠️ 사용자 에디터 작업 필요(이거 안 하면 크로스헤어 색 설정이 안 보임)**: ① `UFPSRCrosshairColorPresetDataAsset` 에셋 생성 + 프리셋 저작(구값: White/Green(0.1,1,0.1)/Cyan(0.1,1,1)/Red(1,0.1,0.1)/Yellow(1,1,0.1)) ② `WBP_Settings`에 스와치 컨테이너(가로 박스) 추가 → `ColorPresetContainer`로 바인드 + **옛 버튼 5개 삭제** ③ 위젯 `ColorPresets`에 ①의 에셋 할당. 누락 시 로그에 경고가 뜸.

### ⑥ 적 "뿌연 구름" = 진단·수정 완료 (2026-07-18, 콘텐츠 = `L_GameFloor` 인스턴스)
**증상**: 런 시작 후 적 swarm(만)에 반투명 회색 헤이즈. 멀수록 심하고 가까이 오면 옅어짐. 건물은 쨍.
- **격리(콘솔 사다리)**: 모션블러 OFF(`r.MotionBlurQuality=0`)·`showflag.Fog 0`·`showflag.Atmosphere 0`·`r.TSR.Enable 0`·`showflag.Bloom 0` = **전부 변화 없음** → `showflag.PostProcessing 0`에서만 정상 = **포스트프로세스 머티리얼**이 범인(엔진 하이트포그/대기/TSR/블룸 전부 아님).
- **진범 = `BP_SRS_Fog`의 `M_Fog`** (⚠️초기 "SRS 아웃라인" 가설은 **철회**). `Only on Custom Depth=1` + 회색 `Fog Color(0.18,0.20,0.22)` + `Camera Distance Scale=1000` → **커스텀뎁스 물체(=적, `renderCustomDepth=True`)에만 거리비례 회색 안개**. 그래서 적만 뿌옇고 건물 쨍·멀수록 심함. **`showflag.Fog 0`이 안 통한 이유** = M_Fog는 **PP 머티리얼 blendable**이지 엔진 하이트포그가 아님(그래서 `showflag.PostProcessing 0`에서만 걷힘).
- **수정 = `BP_SRS_Fog` 인스턴스 `Enable Fog=False`**(사용자 별도 MCP 세션, 저장됨). 아웃라인·셀은 무변경(셀/툰 룩 유지).
- **검증**: 사용자 PIE 육안 + git(`L_GameFloor.umap` 수정, `BP_SRS_Fog`는 레벨 배치 액터라 인스턴스 오버라이드로 정합).
- **교훈**: 거리감쇠 헤이즈 ≠ 항상 엔진 안개. PP가 **적(커스텀뎁스)에만** 끼면 아웃라인으로 단정 말고 **레벨의 모든 PostProcessComponent/Volume blendable 전수 열거** + `Only on Custom Depth`/`Fog Color` 확인. `showflag.PostProcessing 0`은 "PP가 범인"까지만 말해줌. 상세 메모리 `srs-postprocess-stack-fog-haze`.

### ① 이 세션에서 한 일 (S4 계측 아크 = 머지 완료)
- `9059af12` **feat(perf)**: S4 가독성 5지표 계측 = **`UFPSREnemyMetricsSubsystem`**(신규). ②③④는 "플레이어 한 명이 겪는 것"이라 **서버 아닌 각 클라가 자기 로컬 폰/뷰 기준**으로 집계 → CSV 커스텀 스탯 5개(`FPSREnemy/ServerAlive|RelevantAlive|VisibleFrustum|VisibleRendered|Near15m`). 신규 순회 0·월드쿼리 0·shipping 생성 거부(`CSV_PROFILER_STATS`).
- `e198f668` **fix(perf)**: ③b **그림자 패스 오염** 수정(`AActor::WasRecentlyRendered`→`GetLastRenderTimeOnScreen`). 첫 실캡처 위반 **47.87%**(3930/8210, 재확인)로 발각. **엔진 소스로 진단 확정**: `PrimitiveSceneInfoData.cpp:12/19-22`가 액터 스탬프를 무조건 씀 — Epic이 `bCastWhenHidden` 때문에 **의도적으로** 그렇게 함(`ShadowSetup.cpp:2056-2061`). 리센시 창도 초→**프레임 기준**.
- `19e1065d` **fix(perf)**: ⚠️ **위 수정만으론 불변식이 안 지켜졌음** — ③a는 액터 원점 40cm 구를 재는데 렌더러는 **메시 바운드**로 컬링(`SceneVisibility.cpp:599-622`). 근접 적 메시 AABB = 액터기준 Z ∈ [-98.87, +58.00](~157cm) vs 구 [-40,+40] → 위쪽 프러스텀 평면에 **58.9cm 밴드** = 버그 0으로 `③b>③a`. **→ ③a도 `EnemyMesh->Bounds` 테스트 = 구조적 상위집합**(렌더러가 동일 바운드를 sphere 테스트 후 box·occlusion 추가) → 불변식이 **설계로** 성립. + 프러스텀 없는 프레임의 **가짜 위반 제조** 차단, **엔진 소스와 반대로 틀린 주석 2건 교정 + 1건 삭제**(RT 50m 거리게이트·Nanite `IsAlwaysVisible` 게터 단축·PIE 멀티뷰).
- 검증 = 빌드 `Result: Succeeded` · 스모크 `FPSRoguelite.Smoke.ModuleLoads Result={Success}` · Opus 직접 엔진소스 재확인(9에이전트 워크플로 + 적대 검증 3렌즈).

### ② 다음 코드 작업 (구체)
1. ~~**보스 HUD 바 clear 경로 누락**~~ → ✅ **완료·main 머지**(2026-07-15, 아래 §④). 판정 = **재현 불가(잠재 계약 갭)** — "버그 수정" 아님. 재개 문서 `Docs/BossHUDClear_ResumePrompt.md`는 **폐기**(전제가 틀렸음, §④ 참조).
2. **U20 적 애니 계약 교정** — ⚠️ **문서 충돌**: 여기선 "다음 작업"이었으나 `TaskPrompts_Master.md §B` DAG는 **U15/U19/U20 = HOLD("U21 아트정체성 결정 전 착수 금지")**. 근거도 명시적 — **적 VAT 베이크는 U22 적 교체 이후라야 재베이크를 안 함**. 지금 `M_BroBot_VAT` 기준으로 계약을 맞추면 Synty 적 교체 시 버려짐. **→ DAG(HOLD) 채택, U21 게이트 후로 이월.** (사실관계: `FPSREnemyAnimProfile.cpp:52`가 `AnimationIndex`를 쓰는데 머티리얼엔 없음[클립선택=`StartFrame`/`EndFrame`], `Phase`→`TimeOffset`, `PlayRate`는 OK. 적 BP 3종 `AnimProfile`=null. 에셋 제약 = BroBot에 Idle/Walk/Run만, Attack/Death 애니가 프로젝트 전체에 없음 → 3상태 상한.)

### ④ 보스 HUD 바 (2026-07-15 · 두 건 = 별개 문제, 둘 다 main 머지)

> ⚠️ **먼저 읽을 것 — 이 절의 초판은 틀렸다.** 초판은 "재현 불가"로 단정했으나, **사용자 스크린샷으로 실제 버그가 발각**됐다(런 시작하자마자 BOSS 바가 꽉 찬 채로 표시). 원인은 **④-2**이고, 조사가 그걸 놓친 이유는 **재개 문서의 프레임("보스가 죽은 뒤 stale")을 그대로 물려받아 "보스가 생기기 전"을 아예 묻지 않았기 때문**이다. 게다가 그걸 잡을 유일한 차원(`hud-consumer` = 바가 null일 때 뭘 하나)이 **사용량 한도로 죽은 상태에서 결론을 냈다**(교훈: 조사가 죽으면 "모른다"고 할 것).
> **두 건은 별개다**: ④-1 = 잠재(계약), ④-2 = **실제 유저 버그**(고침). ④-1의 수정은 ④-2를 고치지 못한다.

#### ④-2. 🐛 실제 버그 — 런 시작에 BOSS 바가 뜸 (콘텐츠, `WBP_BossHUDBar`)
**증상**: 런 시작(Lv1·보스 없음)부터 상단에 BOSS 바가 `Percent=1.0`로 표시. **사용자 PIE 확인으로 수정 검증 완료.**
- **원인**: `WBP_BossHUDBar` EventGraph의 **Construct 초기 동기화가 `IsValid` 게이트 뒤에 갇혀 있었음**.
  `Bind → [IsValid(GetActiveBoss())] ─Is Valid→ OnBossChangedEvent(보스)` / **`Is Not Valid` 핀은 미연결(빈 핀)**.
  → 런 시작엔 보스가 null이라 `Is Not Valid`로 빠지고 **이벤트가 아예 호출되지 않음** → 숨김 분기가 실행될 기회가 없음 → 위젯이 **디자이너 기본값(`SelfHitTestInvisible` + `Percent=1.0`)** 그대로 화면에 남음.
- **`OnBossChangedEvent` 자체는 원래부터 정상**: `IsValid(Boss)` → 유효=`SelfHitTestInvisible`(표시) / 무효=언바인드 후 `Collapsed`(숨김). **로직이 아니라 호출이 안 되던 것.**
- **수정**: Construct 쪽 `IsValid` 매크로 노드 삭제 + `Bind.then → OnBossChangedEvent.execute` 직결(= **무조건 초기 동기화**, 이벤트가 알아서 분기). 이벤트 내부의 `IsValid`는 **그대로 둠**(그게 show/hide 본체).
- **교훈(일반화)**: 이벤트 구동 UMG 위젯은 **① 델리게이트 구독(변화) + ② Construct에서 현재 상태 무조건 반영(초기값)** 둘 다 필요하다. ②를 "값이 있을 때만"으로 게이트하면 **빈 상태가 디자이너 기본값으로 새어나온다.** 관련 메모리 `umg-event-widget-initial-sync`.
- **검증**: 배선 되읽기(무조건 호출·데이터핀 유지·IsValid 1개만 잔존) · 컴파일 OK · 디스크 변경 = `WBP_BossHUDBar.uasset` 단일(컨테이너 `WBP_GameHUD` 무변경) · **사용자 PIE 확인 완료**.

#### ④-1. 잠재 계약 갭 — `SetActiveBoss(nullptr)` 호출처 0건 (코드, `phase/boss-hud-clear` → main `--no-ff`)
**판정 = 현재 재현 불가(잠재). "버그 수정" 아님 — ④-2와 무관하며 ④-2를 고치지 못한다.** 재개 문서 `Docs/BossHUDClear_ResumePrompt.md`의 **핵심 전제가 틀렸으므로 그 문서는 폐기**한다.

- ❌ **문서의 틀린 전제**: "보스가 파괴되면 GC가 `TObjectPtr`를 조용히 null로 만드는데 `OnActiveBossChanged`는 발화 안 함". → **보스는 파괴되지 않는다**(`FPSRBossBase.cpp:155` HandleDeath가 결과 연출용으로 **의도적 존치**: "No XP drop / pooling / Destroy... keeps it visible during the result beat"). 실제 상태는 GC-null이 아니라 **"체력 0인 유효한 보스를 가리키는 멀쩡한 포인터"**.
- ✅ **관측 불가 근거 3개(각각 독립 성립)**:
  1. **트래블에서 런 GameState가 죽음** — 2홉(런→`L_Transition`→로비). `AGameModeBase::GetSeamlessTravelActorList`가 GameState를 **`bToTransition`일 때만** 넘김(`GameModeBase.cpp:548-553`) → 둘째 홉에서 버려지고 로비는 `ActiveBoss=null`인 새 GameState를 만듦. HUD 소유자(런 PC)도 클래스가 달라 `SwapPlayerControllers`가 파괴.
  2. **같은 월드 재런 = 도달 불가** — `StartRun` 호출처는 `FPSRGameMode.cpp:76`(BeginPlay) **단 하나**이고 **UFUNCTION이 아니라 BP/콘솔에서 도달 불가**. 게다가 **`bRunActive`는 어디서도 해제되지 않는 일방 래치**(선언 `.h:99` · 읽기 `.cpp:69` · set-true `:74`가 census 전부) → 두 번째 호출은 조용한 no-op. **즉 `StartRun`의 "Re-run safety" 형제 리셋들(`ResetSpawnZones`·`ResetForNewRun`·`ResetDoorTopologyToBaseline`)도 지금은 전부 도달 불가**. 코드 주석도 동의: `FPSREnemySpawnSubsystem.cpp:1479` **"FUTURE NOTE (same-world re-run only, not yet reachable)"**.
  3. 유일 관측 구간 = 결과 화면 뒤 `PostRunTravelDelay`(~3초)뿐이고 그건 **의도된 연출**.
- ✅ **그래도 고친 이유**: 계약이 이미 3곳에서 약속됨(`FPSRGameState.h:138/141-142/247`) + **보스 2페이즈(보스 사망≠런 종료)나 같은 월드 재런이 도입되면 그 시점에 즉시 관측 가능**해짐(보스가 파괴되지 않으므로 죽은 보스 바가 다음 보스까지 ~300초 유지).
- **구현**(`74e63137`, `FPSRRunDirectorSubsystem.cpp` StartRun 재런 안전 블록): `GS->SetActiveBoss(nullptr)` **→ 그 다음** `ActiveBoss->Destroy()`. ⚠️ **순서가 제약**: 액터가 garbage가 되면 복제 ref가 자가-null되어 세터의 `ActiveBoss == InBoss` 조기 반환(`FPSRGameState.cpp:40`)이 HUD에 필요한 broadcast를 **삼킨다**. 액터 파괴까지 하는 이유 = 포인터만 비우면 죽은 보스가 레벨에 그대로 서 있음. 첫 런은 전부 no-op.
- **멀티**: 세터 경유라 Push Model `MARK_PROPERTY_DIRTY` + 리슨서버 호스트 직접 broadcast(호스트는 OnRep 없음) 대칭 그대로 성립.
- **검증**: 빌드 `Result: Succeeded` · 스모크 `Result={Success}` · Codex 게이트 통과("safely clears the replicated boss reference before destroying the prior boss actor... without introducing an obvious regression"). **PIE 미실시 — 현재 도달 불가 경로라 재현 시나리오 자체가 없음**(보스 2페이즈/재런 도입 시 그때 PIE 필요).
- 📌 **미해결 인접 사실**: `StartRun`은 `GS->SetActiveMission(nullptr)`도 안 부르고 디렉터의 `ActiveMission`도 안 비운다(진행도만 0으로). 같은 재런 시나리오에서 **미션도 동일 갭** — 이번 스코프 밖, 재런이 실제로 도입될 때 같이 볼 것.

### ③ 블로커 / 주의
- ✅**S3(외곽선×VAT 정합성) · S4 성능 실측 = 통과**(사용자 판정 2026-07-18). Claude측 계측 인프라 완료. **아래 튜닝 노트는 후속 조정 시 참고용으로 보존**(S3 톤다운·불변식 합격선·밀도 측정 함정 등).
  - ⚠️ **S3 판정 순서 주의**: 지금 셀 아웃라인을 **자동노출 최대 66배 + 블룸 2.37배**를 통과시켜 보고 있음 = 아웃라인이 아니라 그레이드를 보는 것. **톤다운 먼저 → 그 다음 아웃라인 판정**(안 그러면 아웃라인을 과하게 두껍게 만들고 나중에 그레이드 고치면 흉해짐).
  - ⚠️ **PIE 창이 640×480**(`EditorPerProjectUserSettings.ini:91-92`). 화면 크기가 곧 ③이라 이대로 잰 숫자는 무의미 → **1920×1080**으로.
  - ⚠️ **불변식 합격선 = 위반율 <5% + 최대 초과 ≤2**(0%는 달성 불가 — ③b는 과거 1~3프레임 스탬프, ③a는 현재라 카메라 회전 시 전환프레임 위반이 물리적으로 남음). 진짜 버그(47.9%·초과 최대 59)와는 이 기준으로 갈림.
  - **실전 상한 192**(`GlobalAliveCap 200 − SeedReserve 8 − FrontReserved`, 단일맵이라 FrontReserved=0). `FPSR.EnemyTarget`은 런 중 **0.25초마다 디렉터가 덮어씀**(무용) → 밀도 실측은 **`FPSR.SpawnEnemies N`**(캡 우회·6m 링). ⚠️ **밀도는 시간이 아니라 파티 레벨로 오름** → `FPSR.AddXP`로 올릴 것. ⚠️ **`DA_RunSchedule`의 `AliveCountByLevel` 마지막 앵커가 실질 상한**(`FPSRRunDirectorSubsystem.cpp:162`가 앵커 있으면 시간램프 무시) — **이 값 확인 필수**(이전 캡처가 100에서 멈춘 이유일 가능성). 메모리 `enemy-swarm-measurement-gotchas`.
  - 캡처는 **`csvprofile start`/`stop` 대신 `csvprofile frames=N`** — 엔진이 스스로 EndCapture하므로 0바이트가 구조적으로 불가(`CsvProfiler.cpp:3785-3792`). **CSV 프로파일러는 월드가 아니라 엔진 전역이라 PIE를 꺼도 캡처가 안 멈춤** = `Profile(20260715_140507).csv` 0바이트의 정체.
  - **분석 스크립트 = `Scripts/s4-check-capture.py`**(인자=CSV 경로, 불변식 위반 시 non-zero exit).
- **계측 잔여(의도적 미착수, 회귀 아님)**: `RenderRecencyFrames` 3→2 · 히칭 시 시간창 폭발 클램프(200ms 프레임→600ms 창) · `Near15m`가 3D인데 tier 패스는 2D(`DistSquaredXY`)라 U7 2층서 불일치 · **4인 PIE CSV 컬럼 충돌**(`CSV_CUSTOM_STAT` 키에 월드 구분자가 없어 호스트+클라3이 같은 열을 덮어씀 → 4인 게이트 시 **필수 선결**, 수정=스탯명에 `GetPlayInEditorID()` 접미사 or 클라별 별도 프로세스) · 캡처 시 `r.AllowOcclusionQueries` 검증(0이면 ③b가 조용히 그냥 프러스텀 카운트가 됨).
- **톤다운 값 미정**(사용자 판단 대기). 노브 22개를 `PP_Synthwave_Grade` 한 곳에 집약해둠(값 보존 = 화면 무변화). **⚠️ 엔진 소스 대조 결과 용의자 4개 중 3개는 순정 UE5.7 기본값**(`0.03`/`8.0`/`bias 1.0`/`shoulder 0.26` — `Scene.cpp:500,501,445`, `SceneView.cpp:180-182`). **실제 저작된 과잉값은 `BloomIntensity 1.6` 하나**(기본 0.675의 2.37배). Extended Luminance Range = **꺼짐 확정** → Min/Max는 EV100 아닌 생 휘도. 권고 = **bias 1.0→0**(min==max 고정 경로에서 리셋 안 됨 = 영구 2배 과노출, `Scene.cpp:490/506/513/545`) · **min/max→1.0**(엔진 공인 "fake manual", `Scene.cpp:724`) · **bloom 1.6→0.7** · **`AutoExposureMethod`는 Histogram 유지**(Manual로 바꾸면 `GreyMult` 0.18→1.0으로 **5.56배** 튐, `Scene.cpp:500`) · shoulder는 **그대로**(기본값·범인 아님). **적용 전 `showflag.VisualizeHDR 1`로 클램프 방향 10초 확인 필수** — min에 붙었으면 1.0 고정=어두워짐(해결), max면 **더 밝아짐(악화)**. 고정 후 밝기 조절은 auto를 다시 열지 말고 **min==max 유지한 채 W만** 조절(`ExposureScale=1/W`).
- ⚠️ **`FPSRCharacter.h`의 S2a 잔재(BlueprintReadOnly 3줄)가 이 세션 중 사라짐**(워킹트리·HEAD 양쪽 0). `BP_FPSRPlayer.uasset`은 여전히 미커밋 수정 상태 → **그 BP가 저 노출에 의존했다면 컴파일 실패 가능**. 확인 필요(LFS 바이너리라 코드로 검증 불가).
- 파일럿 규칙("아트 통과 전 콘텐츠 커밋 0")은 이미 깨진 상태 — `b25db2ab`가 `L_GameFloor.umap` + DevBlockout 머티리얼을 main에 커밋했고 throwaway 격리 경로(`_SyntyPilot/`)도 아님.

### ④ 미커밋 콘텐츠 (= 사용자 작업으로 남김, 커밋하지 않음)
```
 M Config/DefaultEditor.ini                                (S0 블록아웃 툴 설정)
 M Content/Assets/Characters/BroBot/VAT/M_BroBot_VAT.uasset  ★ VAT A포즈 버그 수정 (아래)
 M Content/Character/Enemy/BP_EnemyMeleeBase.uasset
 M Content/Character/Player/BP_FPSRPlayer.uasset
 M Content/Game/Data/DA_EnemyRoster.uasset
 M Content/Game/Data/DA_RunSchedule.uasset
 M Content/Maps/L_GameFloor.umap                            ★ 엠블럼 충돌 제거 + 톤다운 노브 집약
 M Content/Weapons/DataTable/DA_Weapon_Rifle.uasset
```

### ⑤ 이 세션 콘텐츠 수정 2건 (내가 MCP로 한 것 — 커밋은 사용자 판단)
- **`M_BroBot_VAT`: VAT A포즈 고착 근본 수정.** `bUseMaterialAttributes=True`인데 `MF_BoneAnimation.Result`가 루트 **MaterialAttributes 핀이 아닌 BaseColor 핀**에 연결돼 있었음 → `Material.cpp:7134`가 MA 핀만 컴파일하므로 **WPO가 상수 0** → 정점셰이더 死코드 제거 → 스태틱 메시가 베이크된 A포즈 그대로. **수정 = Result→MP_MATERIAL_ATTRIBUTES**. 검산: MI **VS instr 148→572, 정점텍스처 샘플 6→20**(Epic 정상본 894/23). 사용자 PIE 확인 = 걷기 시작 + BodyColor 빨강 정상 출력. 되돌리기 `git checkout HEAD -- Content/Assets/Characters/BroBot/VAT/M_BroBot_VAT.uasset`. (BaseColor 핀은 연결 잔존 — Python에 해제 API 없음, MA 모드라 컴파일러가 안 읽어 무해)
- **`L_GameFloor`**: ① `Plaza_Emblem` 충돌 제거 → 플로우필드 바닥앵커가 47(엠블럼)→**40(dais)**로 교정, 지면(0)·슬래브(10)·dais(40)·커버(40) **전부 직접 지면시드 통과**, 지면↔dais가 "경사로 오인" 우연통과가 아닌 **진짜 40cm 단차**로 열림. ② `PP_Synthwave_Grade` 톤다운 노브 22개 오버라이드 ON(값 보존).

**완료·머지·푸시 (3개 아크 종결)**
- **무기 조립툴 개편 + 파츠 스택/스탯 진화 + 저격 스코프 위젯 아크** — `--no-ff` main 머지 `fd5ed792`(→ origin/main과 통합 머지), Codex 머지리뷰 통과+교정 `6999ff3c`, `phase/pwas-b-procedural-weapon-motion` 삭제. 폴리모픽 PartRules→**파츠별 스택 진화(순수 struct)** + **스탯 임계 트리거**, 조립툴(고정소켓 안정id·진화패널·단계 뷰포트배치·트리거/스탯/스코프 편집·순서이동), **스코프 오버레이=사이트별 위젯 BP**(리티클 텍스처 폐지). 사용자 정상작동 확인. **남은=사용자 콘텐츠**: 라이플 저격 진화 재저작(옛 PartRules 서브오브젝트 링커경고 해소) + 사이트별 스코프 WBP 저작 + PIE. 후속(pre-existing)=데디서버 파츠 게이팅(Codex F3, spawn_task). 상세=메모리 `weapon-modular-evolution-scope-plan`.
- **P8 U 연속필드 다중맵 아크 (P-0~P-H)** — `--no-ff` main 머지 `34b5eea`, L_U_Whitebox 콘텐츠 `1906d56`, `phase/p8-multimap-tier0` 삭제. Tier-0 NetCull = 대칭 거리컬 교전버블 한계까지(Option A, `NetCull` 균일 사이징); 진짜 공간 relevancy(seam pop-in 제거) = RepGraph 별도 후속.
- **반동 CrystalRecoil 어댑터 아크 (P0~P4)** — `--no-ff` main 머지 `6f1a981`, 死코드 정리 `2c91ab7`·머지게이트 교정 `afa73dc`, `phase/recoil-crystalrecoil` 삭제. 확산 = 단일소스 heat 모델(`GetHeatSpread`), 무기별 `RP_*` 반동 패턴 저작.

**⏭ Synty 셀/툰 아트 파일럿 = 실행 중 (스코프 축소판)**
- **읽을 것**: `Docs/SyntyArtPilot_Scoped_ResumePrompt.md`(전체 상태·결정) + `Docs/SyntyArtPilot_S1_CityBuildGuide.md`(§7 MapId 함정). ⚠️ 구 `Docs/SyntyArtPilot_ResumePrompt.md`·`Docs/AssetReplacement_Synty_ResumePrompt.md`는 **폐기본**(SRS를 "최후 폴백 유료옵션"이라 하는 등 SSOT와 모순) — Scoped 판이 최신.
- 순서 `S2a✅ → S0✅ → S1✅ → S3(진행) → S4(최종 게이트)`.
- **S1 완료**: `L_GameFloor` 264m 2×2 섹터(볼륨 1개 (0,0,0)·264m·**132×132=17,424셀**, 상한 40,000 대비 여유). ① 단차 walkability **통과**(사용자 확인).
- **S3 = SRS 이미 배치됨**(BP_StylizedRenderingSystem, `unbound=True`, 블렌더블 2: 셀 `MI_SRS_BASE_CelShader` + 아웃라인 `M_SRS_Outline01`). ⚠️ **셀은 stencil 1~255를 요구하는데 맵에 stencil≥1인 프리미티브가 0개** → 셀이 사실상 아무 데도 안 걸림. 아웃라인은 stencil 0~0이라 **적만** 받음(적 Mesh `renderCustomDepth=True, stencil=0`). 셀↔아웃라인이 stencil 0에서 **상호배타** — 둘 다 받으려면 적 stencil을 1로 올리고 아웃라인 마스크도 1~255로. **잔여 판정 = 외곽선이 VAT 애니를 따라가나**(이제 애니 정상화돼 판정 가능).
- **S4 = 최종 게이트**. 계측 인프라 이번 세션 완성(위 §① 참조). 실측 미완.

**📌 살아있는 백로그 / 이월 (회귀 아님)**
- **RepGraph spatial-grid relevancy** — 별도 후속 페이즈(per-acquire NetCull 반경으로 적 재-bucket). plan `Docs/Review/20260707-plan-continuous-field-arch.md` §2-4/§4 D3 · Performance §5. 클라 seam pop-in = 문서화된 Tier-0 한계(D3 수용).
- **NetCull 튜너블**: `NetCullWeaponRangeCm=10000`(무기사거리 floor)·`NetCullSeamMarginCm=4000`. 단일맵 = pre-P-H 200m 바이트동일.
- **애니메이션 콘텐츠 저작 (진행 중)** — U15(1P무기)/U19(3P팀원)/U20(적VAT) 코드 인프라 ✅, 콘텐츠 미저작. 가이드 `Docs/AnimationPass_ContentGuide.md`. ⚠️ Synty Blu+PWAS 피벗이 손저작 AnimBP를 대체할 수 있음 → 파일럿 결과 후 확정.
- **반동 잔여 (PvE 코스메틱)**: ADS 확산배수 `bIsAiming`(ServerSetAiming RPC 지연) 플릭샷 미세 불일치 · 예측거부 샷 클라 heat 드리프트(cooldown 흡수) · 레거시 블룸 orphaned 저장값(로드 시 무시, 무해) · Shotgun/Bazooka 고정 확산 · ChargeLaser base-only + 커스텀 차징 램프(의도).
- **원거리 적 / 피드백 후속**: 원격 클라 총알 시각예측 미구현(A3) · 원거리 경고 생산자(`ClientNotifyRangedTarget`) 배선 미완(B1, 현재 디버그 `FPSR.TestRangedWarn`만).
- **성능 정량**: §5 적500 정량 측정 보류 → 하드캡 잠정값 유지(Performance §5). **계측 수단은 2026-07-15 확보**(`UFPSREnemyMetricsSubsystem` + `csvprofile start/stop` → CSV 열에서 P50/P90). 종전 "측정 코드 전무"는 해소.
- **플로우필드 콘텐츠 계약 (2026-07-15 실측 확립, 맵 저작 시 필독)**: 장애물 판정 박스가 **셀바닥+60cm부터** 시작(`ObstacleProbeZ=120`/`HalfHeight=60`) → **60cm 미만 물체는 플로우필드가 못 봄**. 통행 게이트는 `ClimbableStepHeight=45`. 따라서 **≤45=밟고 넘음 / ≥60=돌아감 / 45~60=함정**(못 오르는데 장애물로도 안 잡혀 적이 낌). **커버로 스웜을 쪼개려면 ≥60cm 필수** — 현 `Cover_0~3`은 40cm(발목)이라 엄폐 기능 0. 메모리 `flowfield-cover-height-45-60-band`.

---

## ✅ 완료 이력 (요약 — 상세는 `git log <hash>` / Game.md)

> 세션 단위 핸드오프는 정리했다(git = 아크별 `--no-ff` 머지 커밋 + doc-sync 커밋으로 완전 재구성 가능). 여기엔 아크 단위 요약만 남긴다. 정리 직전 스냅샷 = 태그 `progress-pre-cleanup-20260713`.

### 최근 아크 (2026-06 ~ 2026-07)
- **무기 전면 개편** `3adc945` — 점사 프래그먼트화 · 유탄 제거 · 기관단총(SMG) 추가 · 전면 투사체화(ChargeLaser·근접 제외).
- **FPSR Data Editor** P0 `57270c5` · P1 `c4e0d77` · P2 `3fb7da6` — magnitude 티어/산술 bulk · 라우팅 누수 검증기 · 미션 스케줄 타임라인 편집(에디터 데이터 편집·검증 툴).
- **코스메틱 Tick 정리** `e059a83` — MissionOrb 死Tick 제거 · XPPickup 클라 no-op · BossHUD 체력바 이벤트 전환.
- **통합 애니메이션 패스 A/B/C** (`phase/p6-animation-pass`) — 1P무기·3P팀원·적VAT+보스스켈 코드 인프라(콘텐츠 저작 = 위 백로그).
- **U12 진실 크로스헤어 + U17 설정** `36cf3d4` — 파라메트릭 크로스헤어 + 색/두께/크기 설정.
- **U11a 멀티플레이 루프** `b3b364e` — 세션 서브시스템 · Seamless 트래블(로비→게임→보스→로비) · 로비 콘텐츠.
- **U18b 무기 해금** `78b1bb5` · **U18c 행동훅 + GAS-native 회복** `f02536a` — 해금 오퍼/추첨 라우팅 · 무기 행동훅(OnAim/Fire/Miss/Kill) · 흡혈/체력재생 패시브.
- **U4 보스 콘텐츠** `71c9bde` · **U3a 약점 부위 데미지** `89b535b`(범용 `UFPSRWeakpointComponent`).
- **U9 DBNO + MP 넷코드 Phase 1A/1B** `e38dfbe` · **U7 멀티레이어 2층 플로우필드** `8d8e232` · **U5 원거리 적** `cd7de43`.
- **오디오 설정 MVP** `747a9b2` — 마스터 볼륨(SoundClass/Mix + GameUserSettings).
- **룸 기반 점진 개방 스폰 + P4-C 무기 콘텐츠** `d285c69` — `AFPSRDoor`/`AFPSRSpawnRoom` · 누적 스폰존.

### 초기 슬라이스 (P0 ~ P4-D · ~2026-06-11)
- **무기 발사/프리즈 메커니즘 하드닝 (브랜치 `fix/weapon-fire-freeze-hardening`, 2026-06-10, PIE 검증 후 머지 대기)** — 콘텐츠(무기 DA Sniper 등) PIE 테스트 중 발견 → 무기 시스템 전수감사(서브에이전트 6영역+Opus 직접검증). ① **반동 케이던스**: 단발 무기 쿨다운 중 클릭 시 실탄은 서버 거부인데 로컬 반동만 적용되던 버그 — 클라 게이트를 `NextFireReadyTime`(절대시각, 서버 `ServerNextAllowedFireTime`과 동일 모델)로 신설, FireOneShot마다 `Now+1/FireRate` 스탬프. ② **근접 GA 프리즈 게이트 누락**(칼 홀드 중 카드선택 프리즈 진입 시 데미지) → 형제 GA 패턴 추가. ③ **재장전 프리즈 누수**: `bRunPaused`는 논리 bool이라 실시간 `FTimerHandle`이 안 멈춤 → `StartReload` 프리즈 시작차단 + GameState `OnRunStateChanged` 구독해 `FTimerManager::PauseTimer/UnPauseTimer`로 진행 타이머 일시정지(잔여시간 보존, 서버 전용). ④ **`ServerDash` 서버측 프리즈 게이트 누락** → 추가(`ServerEquipSlot`/`ServerStartChargeLaser`와 대칭). ⑤ **무기 교체 연타로 케이던스 우회**(equip이 게이트를 0으로 리셋) → **최소 교체 쿨타임** `EquipFireCooldown`(0.2s, EditDefaultsOnly): equip 시 게이트=`Now+쿨다운`, 클라 `OnWeaponEquipped`로 동일 적용(이전 무기의 긴 인터벌 비상속). ⑥ **`UFPSRWeaponDataAsset::IsDataValid` 신설**(FireAbility 누락=에러, AOE 무반경/ChargeLaser ChargeTime 0/Mag 0=경고) — 무기 DA 작성 가드. 빌드+스모크 통과, **반동/프리즈/교체는 PIE 검증 대기**. 메모리 `freeze-gate-client-server-symmetry`. (Game.md §2-2/§2-4-1/§2-10)
- **카드 풀 무기별 소유 + 타깃 무기 귀속 (main 머지, 2026-06-10, `phase/p4-card-weapon-pools`)** — ThisWeapon 카드가 항상 "장착 무기"에 적용돼 칼 들고 레벨업 시 탄약/연사 카드가 칼에 박히던 꽝 문제 해소. **오퍼가 소스 무기를 들고 다니게**: `FFPSRCardDraw.TargetWeapon`(서버가 추첨 시 세팅, 클라 위조 불가). ① `GatherCandidatePool`=중앙 풀(Character+AllWeapons, target=null) + 보유 **모든** 무기의 `WeaponCards`(ThisWeapon→그 무기 target, (card,target) 디듀프). ② `ApplyCard` ThisWeapon=`GetCurrentInstance`→`GetInstanceForWeapon(TargetWeapon)`(미보유=거부 안티치트, null=장착 폴백 하위호환). ③ `DrawWeaponModifierOffer`(미션보상)=장착 1정→**보유 전 무기** AvailableModifiers 통합(각 인스턴스 스택여유)+소스 target, 셔플 최대 3(부족 시 그만큼). ④ `UFPSRWeaponInventoryComponent::GetInstanceForWeapon` 헬퍼. Codex P3(디버그 캐시 TargetWeapon 보존) 교정. **콘텐츠 마이그레이션 완료**(중앙 풀=Character+AllWeapons, ThisWeapon→각 무기 WeaponCards, fragment 리네임). 빌드+스모크+Codex 통과. **후속(계획)**: 카드 UI 소속 무기 아이콘+이름(Game.md §2-4-1, `Icon` 필드+위젯 바인딩). (Game.md §2-3/§2-4-1)
- **무기 DA 아키타입별 조건부 노출 (main 머지, 2026-06-10, `a25b491`)** — `FFPSRWeaponStatBlock`이 전 아키타입 스탯을 항상 노출하던 편집 번잡 해소. `Archetype`을 `BaseStats`로 이동(+`GetArchetype()` 게터, FireComponent 7곳 전환)→구조체 필드 `EditConditionHides`로 자신 참조(PelletCount→Shotgun/MaxPenetration→Sniper/Projectile→AOE/Charge→ChargeLaser/Melee→Melee, BurstCount→FireMode==Burst, ADS→bHasADS). 런타임 무변경. **콘텐츠 영향**: 필드 이동으로 Knife Archetype 리셋→Melee 재설정(완료). 빌드+스모크 통과. (구현 Haiku/검증 Opus)
- **A3b ChargeLaser (main 머지, 2026-06-10, `phase/p4c-aoe-charge`, A3 분할 2/2)** — 레이저=히트스캔(§2-10)+차징(hold-to-charge, release-to-fire), 차징량 alpha가 데미지 스케일하는 **관통 빔**. **Opus 직접 구현**(서버권위 RPC, 메모리 `haiku-delegation-security-wiring`). ① 스탯 ChargeTime+ChargeFullDamageMultiplier. ② `UFPSRWeaponFragment::ModifyChargeTime` 훅. ③ `UFPSRWeaponFireComponent` 차징 cadence: StartFiring ChargeLaser 분기(즉발 없이 로컬 ChargeStartWorldTime 스탬프), StopFiring(비권위만 로컬 cosmetic 활성화), `ServerBeginCharge`/`ServerReleaseCharge`(서버권위 활성화), `ResetCharge`. ④ `AFPSRCharacter` RPC **`ServerStartChargeLaser`+`ServerReleaseChargeLaser`**(둘 다 Character채널 Reliable=ordered, ADS 패턴). ⑤ **`UFPSRGA_WeaponFire_ChargeLaser`**(신규, **LocalOnly**): 프리즈/ammo/firerate 게이트+fragment PreFire/ModifyChargeTime/OnHitActor/PostFire, **alpha=clamp((now-Start)/ChargeTime)**(각 머신 자기시각: 클라=로컬·서버=권위), 읽고 ResetCharge(이중소비 방지), 데미지=Damage×Lerp(1,FullMult,alpha)×글로벌×크릿, **관통=Visibility 벽거리(폰 무시)+Pawn 멀티트레이스 벽 이내 전원**, hit-marker 1회. **Codex 게이트 교정(2026-06-10)**: [P1] 차징 시작/릴리즈 교차채널 레이스 → release를 ordered Character RPC로 보내 서버 직접 활성화(GA LocalOnly로 GAS 자동전파 제거, 호스트 이중활성화는 비권위만 로컬활성화로 차단). [P1] 무기교체 차징 뱅킹 → equip시 ResetCharge(서버 EquipSlot+클라 OnRep). [P2] 레이저 벽판정 object-type(WorldDynamic 투사체 오인)→Visibility 채널. **콘텐츠 보류**: `DA_ChargeLaser`, 차징 머티리얼·VFX·HUD 게이지. **후속**: alpha→range/pierce 스케일, 풀차징 자동발사, 클라예측 cosmetic. 빌드 Succeeded+스모크 Success+Codex 클린+Opus 세밀 자기비판. (Game.md §2-10/§2-4-1)
- **A3a AOE 투사체 발사 GA (main 머지, 2026-06-10, `phase/p4c-aoe-charge`, A3 분할 1/2)** — A1 투사체 코어 위에 발사 GA만 추가(발사 어빌리티 grant=`FPSRWeaponInventoryComponent` generic GiveAbility → **FireComponent/grant 무변경**, AOE=FireMode=Single). ① `FFPSRWeaponStatBlock` 투사체 스탯 5종(ProjectileSpeed/ProjectileGravityScale/AOERadius/ProjectileLifetime/ProjectilePierce, 기본값=무회귀). ② `UFPSRWeaponFragment::OnProjectileSpawn(FFPSRFireContext, FFPSRProjectileParams&)` 훅(빈 기본값). ③ `UFPSRWeaponDataAsset::ProjectileClass`(콘텐츠 BP_Rocket, null=base 폴백). ④ **`UFPSRGA_WeaponFire_Projectile`**(신규): 히트스캔 GA 구조 미러 — 프리즈/ammo/firerate 게이트 + fragment PreFire/ModifyShotCount/**OnProjectileSpawn**/PostFire 재사용, 라운드별(=ShotCount, 라운드당 1탄약) 뷰포인트에서 `FFPSRProjectileParams` 구성(SpreadDegrees면 VRandCone 부채꼴) → **서버권위** `AcquireProjectile`(클라=nullptr, cosmetic 예측 후속), 데미지=Damage×글로벌배수 베이크(impact 서버권위). **Codex 게이트 교정(2026-06-10)**: [P2] 고정 100cm 머즐 오프셋이 근접 벽 너머 스폰(얇은 커버 관통) → 뷰포인트→머즐 Visibility 트레이스로 벽면 클램프. **비포함(후속)**: 클라예측 cosmetic 스폰, 투사체 impact 히트마커/per-impact 크릿/`OnHitActor`, 머즐 소켓. **콘텐츠 보류**: `BP_Rocket`(AFPSRProjectile 상속+메시/VFX), `DA_Bazooka`/`DA_Grenade`(Archetype=AOE, FireMode=Single, FireAbility=GA_Projectile, ProjectileClass=BP_Rocket). 빌드 Succeeded+헤드리스 스모크 Success+Codex 클린+Opus 자기비판. (Game.md §2-10/§2-4-1)
- **A2 Hitscan 3종 (main 머지, 2026-06-10, `phase/p4c-hitscan`, 코드 선행 백로그 #2)** — 점사·스나이퍼·샷건 = **단일 Hitscan GA 스탯 구동**(아키타입 베이스, GA 스왑/태그 분기 회피 §2-4-1). ① `FFPSRWeaponStatBlock`에 `PelletCount`(샷건 산탄, 기본1)·`MaxPenetration`(스나이퍼 관통, 기본1) 추가 — 기본값=기존 무기 무회귀. ② `FPSRGA_WeaponFire_Hitscan` **라운드×펠릿** 리팩터: fragment `ShotCount`=라운드수(라운드당 1탄약), 라운드마다 `PelletCount` 펠릿 발사 → 샷건=1탄약 N펠릿(멀티샷=라운드당 1탄약과 자연 합성, 라이플 PelletCount=1 동일 경로). ③ 관통(`MaxPenetration>1`): **벽거리=Visibility 트레이스(폰 무시 리스트)** + 적=`ECC_Pawn` 멀티트레이스 거리순, 벽 이내 최대 N마리 데미지. 데미지 적용을 람다로 단일트레이스/관통 공용화. 관통 카운트는 권한 무관, 크릿 히트별 독립 롤, hit-marker 활성화당 1회. ④ **Burst·Sniper-Single·Auto복구는 `UFPSRWeaponFireComponent`가 이미 처리 → 코드 변경 없음**. **Codex 게이트 교정(2026-06-10)**: [P2] 관통 벽판정 object-type(WorldDynamic 투사체 오인)→Visibility 채널(폰 무시 리스트). 검증: 빌드 Succeeded+헤드리스 스모크 Success+Codex 클린+Opus 자기비판. **콘텐츠 보류**: DA_BurstRifle/DA_Sniper/DA_Shotgun. **후속**: `EFPSRWeaponStat`에 PelletCount/MaxPenetration 카드 축. (Game.md §2-4/§2-4-1)
- **A1 투사체 코어 (main 머지, 2026-06-09, `phase/p4c-projectile-core`, 코드 선행 백로그 #1)** — A3(AOE/유탄)·B1(원거리 적) 공유 의존 범용 베이스. ① `AFPSRProjectile`(Sphere+`UProjectileMovementComponent` 결정적 이동+코스메틱 메시[미할당], `bReplicates`+ReplicateMovement): 서버권위 충돌→데미지 브릿지 **재사용**(Player→`UFPSREnemyHealthComponent::ApplyDamage` / Enemy→`AFPSRCharacter::ApplyContactDamage`), 단일타격+관통 또는 **AOE**(`OverlapMultiByObjectType(ECC_Pawn)`=대시 i-frame 무관), `IsHostileTarget` 팀판정(친화 폭발·instigator 차단). ② `UFPSRProjectileSubsystem`(서버권위 풀+**≤64 동시 복제캡** FIFO 강제회수, 클래스 매칭 재사용, 디버그 `FPSR.SpawnProjectile`). ③ **글로벌 프리즈 준수**(§2-2): `FTickableGameObject` 전환검출→활성 투사체 PMC정지+수명타이머 Pause, 충돌핸들러 `IsRunFrozen` 게이트, freeze프레임 월드충돌 stuck은 resume 지연임팩트로 해소. ④ 생명주기 하드닝: `bActive` 재진입 가드+멱등 해제(이중풀등록 차단), `FellOutOfWorld`→풀 회수(pending-kill 오염 차단)+`IsValid` 전수, `SetUpdatedComponent` 재연결(StopSimulating 후 정지 방지), Lifetime≤0 클램프, 점블랭크 초기오버랩 순서. **설계결정**: 미검증 cosmetic mode 제거 → A1=결정적 PMC(**예측준비**)+서버권위 데미지+풀/캡, **클라예측 로컬 cosmetic 스폰은 A3 발사 GA 책임**(Game.md §2-10 의도는 결정적 이동으로 충족). **알려진 한계**(미미): 프리즈 *전환 프레임*의 폰 오버랩 1건 드롭(1프레임·무크래시). **콘텐츠 보류**(미작성): 투사체 메시/VFX·무기 DA/BP·발사 GA(A3). 빌드+헤드리스 스모크(Success)+**Codex 11R 하드닝→클린** 통과. (Game.md §2-10/§5)
- **적 스폰포인트 코드 (main 머지, 2026-06-09, `phase/p4-enemyspawnpoints` 코드분)** — ① **디자이너 배치 스폰포인트** `AFPSREnemySpawnPoint`(Weight/ZoneTag/MinPlayerDistance/bEnabled) + `UFPSREnemySpawnSubsystem` 전 플레이어 비가시(FOV)+거리 가중랜덤 선택(후보 0 시 링 폴백, 미배치 맵 동작), 디자이너 지점 ground-snap 생략(권위 Z 보존), `SetActiveSpawnZone` 훅(TimeGate 후속). ② **플로우필드 장애물 마스크/BFS 라우팅 + 적 중력/지면추종**(`AFPSREnemyBase`, 경사/계단 보정). **Codex 5R 하드닝**: 디자이너 Z 보존(실내 천장 스냅 방지)→월드 밖 추락 KillZ 회수(슬롯 누수)→접촉 데미지 수직 게이트(바닥 관통)→동일 스폰지점 중첩을 **분리(separation) 동일위치 결정적 골든앵글 푸시**로 근본 해소(지터 전량 제거, 스폰 위치 비이동, 맵 비의존)→최종 클린. 빌드+스모크 통과. **콘텐츠 배치(L_Sandbox 스폰포인트+BP_EnemyBase 정렬, `d3a68c6`)도 머지 완료**(코드+콘텐츠 = `phase/p4-enemyspawnpoints` 완전 머지). **후속(C1)**: 플로우필드 셀 클리어런스(좁은 통로 과차단)+멀티레벨 높이 인지. (Game.md §5-2)
- **P4-D (main 머지, 2026-06-09)** 게임필/피드백 — ① **PickupRadius/XPGain** 어트리뷰트(`UFPSRCombatSet` 승수, 기본 1.0)+XP 픽업 배선(자석 대상=플레이어별 유효반경 거리비 최소, 협동 정합)+카드 2종(Instant·Add·SetByCaller). ② **런상태 HUD** `UFPSRRunHUDWidget : UCommonUserWidget`(BlueprintPure 게터+`OnRunStateUpdated` BIE, 이벤트기반; 픽 카운팅은 레벨업=즉시카드선택이라 제거)+**Game레이어 컨테이너** `UFPSRGameHUDWidget : UCommonActivatableWidget`(입력설정 소유; XPBar 위젯/클래스 폐지·RunHUD로 일원화). ③ **로컬 피드백** `UFPSRPlayerFeedbackComponent`(비복제·이벤트형)+PC Client RPC: **히트마커**(서버권위 Hit/Crit/Kill, 활성화당 1회, Unreliable), **피격 방향**(CoD식 `ApplyContactDamage`→오너클라 카메라기준 각도), **원거리 타겟 사전경고**(다수소스 id별 TMap·각도배열·추적Tick·Reliable; 생산자=원거리 적 AI 후속, 디버그 `FPSR.TestDamageDir`/`FPSR.TestRangedWarn`). **설계 정제**: 근접/사각지대 *시각* 위협 제외→사운드 이전. Codex 다회 하드닝(협동 자석/호스트 클럭/늦은복제 바인딩/확산발산→서버권위/제어상실 클리어/Unreliable·Reliable/다수소스/전방선언). 콘텐츠: 카드 GE·DA 2종+CardPool, WBP GameHUD/RunHUD/HitMarker/ThreatIndicator, BP_FPSRPC 배선. 빌드+스모크+Codex+2-client PIE 통과. **후속**: 히트마커 최종 연출(크로스헤어/발사체 후), 원거리경고 생산자 배선, 핑/Gibs/사각오디오. (Game.md §2-11/§2-14)
- **P4-B-2 (main 머지, 2026-06-08)** 무기 행동 Fragment — 합성형 발사 훅. `UFPSRWeaponFragment : UPrimaryDataAsset`(무상태, 동작=C++ 서브클래스 virtual 훅 `PreFire/ModifyShotCount/OnHitActor/PostFire`, 수치=DataAsset) + 레퍼런스 2종(`UFPSRFragment_MultiShot{ExtraShots}`, `UFPSRFragment_OnHitBonusDamage{BonusDamage}`). `FFPSRFireContext`(plain struct) / `UFPSRWeaponInstance.ActiveFragments[]`(복제 참조)+`AddFragment`(MaxStacks 스택제한)/`GetFragmentStackCount`/`HasFragment` / `GA_WeaponFire_Hitscan` 훅 배선(PreFire→ModifyShotCount=NumShots 루프→히트당 OnHitActor→PostFire) / `UFPSRCardDataAsset.GrantedFragment`(ThisWeapon→AddFragment) / `UFPSRWeaponDataAsset.AvailableModifiers[]` / `DrawWeaponModifierOffer`(스택 여유분 셔플) + ApplyCard fragment 분기 / 디버그 `FPSR.GrantMissionRewardPick`. **마무리 세션(2026-06-08)**: ① 카드 작성 EditCondition 가드(Scope별 무관 필드 숨김) + `GetCardFamilyKey` Character-scope 게이트(stale GE 패밀리 누수 차단). ② 미션보상 카드 UI(`UFPSRCardEntryWidget`): fragment는 등급 대신 카테고리 라벨(`FragmentCategoryText`, WBP override)+수치 빈칸. ③ **MultiShot 펠릿당 탄약 소모**(잔량 클램프, 최소 1발). ④ Fragment **`MaxStacks` 중첩**(중복 누적, 훅 스택마다 적용 — MultiShot 2스택=3발). 콘텐츠: Fragment DA 2종+Fragment 카드 2종+카드 `Card/`이동+Rifle AvailableModifiers+CardPool. 빌드+스모크+Codex(다회 하드닝, 최종 클린)+PIE 통과. (Game.md §2-4-1 ②)
- **P4-B-1 (main 머지)** 무기 스탯 모디파이어 기반 — 런타임 컨테이너 `UFPSRWeaponInstance`(UObject 등록형 복제 서브오브젝트: Source DA + ThisWeapon `Modifiers` + 탄약/리로드 + 해석 스탯 lazy 캐시, Push Model) 신설. 인벤토리 `Slots[]` 인스턴스화(탄약·리로드 병렬배열 → 인스턴스 응집), `AFPSRCharacter`·인벤토리 컴포넌트 `bReplicateUsingRegisteredSubObjectList=true`. 스탯 해석 = `BaseStats × 누적(ThisWeapon[인스턴스] + AllWeapons[`AFPSRPlayerState::AllWeaponsMods`, 리스폰 생존])`, 발사 3곳(FireComp·Hitscan·Melee)+탄약 `GetResolvedStats()` 배선. `ApplyCard` weapon-scope 실적용(ThisWeapon→인스턴스, AllWeapons→PlayerState, 무기 없으면 거부), `UFPSRCardDataAsset.WeaponStat/WeaponStatOp`+IsDataValid, DrawCards 범용풀 weapon-scope 합류(무기 보유 시), 프리즈 중 `ServerEquipSlot` 차단(ThisWeapon 타깃 결정성). 디버그 `FPSR.DumpWeaponStats`. 카드 magnitude 표시 `+%.0f`→퍼센트/소수 수정. 재장전 중 반동 큐 지속 버그 수정(`!CanFire()` 플러시). 콘텐츠: 무기 스탯 카드 6종(연사/탄창/반동×ThisWeapon/AllWeapons)+풀. 빌드+스모크+Codex 3R+PIE 통과. (Game.md §2-4-1 ①)
- **P4-A (main 머지, 재설계)** 런 흐름 — **라운드제 폐지 → 레벨업 전역 프리즈**. `AFPSRGameState.bRunPaused`(복제, 페이즈독립)+`RefreshPauseState`(전원 보류픽 기준 프리즈/재개)+`AddSharedXP` 즉시 프리즈. `ERunPhase`=Combat/Boss. 프리즈 게이팅(스폰·적이동/공격 동결·플레이어 입력/속도·발사GA). 오퍼 일반화 `EFPSROfferType{OpeningSeed,LevelUp,MissionReward}`+`MissionRewardPicksPending`+`ApplyCard` 타입별(weapon-scope 수락·소비, GE적용 P4-B). `UFPSRRunDirectorSubsystem`(런클럭+시간 미션스케줄 `FFPSRMissionEvent`+`BossTime`+시간 스폰스케일링, 오프닝홀드, 보스>미션 우선). 미션 프레임워크(`AFPSRMissionActor`+`UFPSRMissionDataAsset`+`AFPSRMission_HoldZone`+`AFPSRMissionSpawnPoint` 태그매칭가중랜덤)+클리어 즉시 프리즈 보상. 적 클래스 설정화(`BP_Enemy` via GameMode). 콘텐츠(미션태그/L_Sandbox 스폰포인트/GameMode/BP_Enemy/DA_RunSchedule/미션DA·BP/존데칼) 동반. Codex 다회(라운드종료적정리·폰전스폰·거리폴백·중복바인딩크래시·스폰홀드·보스미션유실) 하드닝. 빌드+스모크+PIE 통과. (Game.md §2-1/2-2/2-7/2-8)
- **P3-D (main 머지)** 카드 UI/공유XP바/오프닝시드 — CommonUI 인프라(`CommonUI`/`CommonInput`/`UMG` 모듈, `UFPSRGameViewportClient`, `DefaultEngine.ini` ViewportClient, 경량 `UFPSRPrimaryGameLayout`=4 레이어 스택) + `UFPSRXPBarWidget`(OnRep 델리게이트 이벤트기반, 폴링 없음) + `UFPSRCardSelectWidget`/`UFPSRCardEntryWidget` + PC RPC 배선(서버 캐시+인덱스+offer nonce 검증, 클라는 인텐트만). **설계 변경: 레벨업 스택=공유 카운터 → 플레이어별 `AFPSRPlayerState::CardPicksPending`**(4인 협동 정합, Game.md §2-2). breather 진입/AddXP 시 서버 자동 발급. 디버그 `FPSR.OpeningSeed`/`FPSR.RequestCards`(권한 보유 시). Codex 7라운드로 보안(클라 임의카드/무한리드로/리롤악용)·정합(nonce/지연바인딩/데드락) 하드닝. 빌드+스모크 통과.
- **P3-C** 카드 시스템(main 머지) — `UFPSRCardDataAsset`(`RarityTiers` 등급별 수치) + `UFPSRCardPoolDataAsset` + `UFPSRCardSubsystem`(등급 가중 비복원 추첨/`CardFamily` 디듀프/`ApplyCard` 레벨업 게이트/`TryReroll`). 리롤=PlayerState(플레이어별 3). `Luck` 단일 행운축(RarityBonus 폐지). 수치=`SetByCaller`(태그 `SetByCaller.CardMagnitude`). `IsDataValid` 검증. 최대체력 증가=현재체력 동반증가(서버권위). Character 카드 콘텐츠 5종+풀+GE(`Content/Cards/Character/`). PIE 확인됨. (Game.md §2-3)
- **P3-B** XP 픽업+자석 — `AFPSRXPPickup`(서버 자석 이동·수령) + `UFPSRPickupSubsystem`(cap 150, 초과 시 XP 직접가산). 적 사망 시 드롭.
- **P3-A** 런 상태(GameState 호스팅) — `AFPSRGameState`에 `SharedXP/PartyLevel/PendingLevelUps/RunPhase`(Push Model). 레벨업=스택 누적(프리즈 없음 §2-2). Breather 시 스폰·공격 게이팅.
- **P2** 적 대량화(main 머지) — `UFPSREnemySpawnSubsystem`(풀링+SpawnDirector, 하드캡 500) + `UFPSRFlowFieldSubsystem`(BFS flow-field+separation) + 거리 LOD(Significance 티어/NetUpdateFreq) + 이속 ±10% 편차 + 적 근접데미지·공격토큰·i-frame + 충돌무시 대시(+IA_Dash 콘텐츠). (Game.md §5)
- **P1.5** 사격/이동 감각 — 사격코어(FullAuto 연사/반동="복구 빚"모델/확산·블룸) + 탄약·재장전(MagSize/R, **예비탄 무한**) + ADS(FOV/확산/반동 배율) + 반동 ADS의존(힙 산탄/ADS climbing). `FPSR.RecoilPreview`. (Game.md §2-4-2)
- **P1** Net-aware 1P 슬라이스 — `AFPSRCharacter`(1P 카메라+Separated Arms+EnhancedInput) + `Weapon/`(3슬롯 서버권위 인벤토리, Push Model) + 발사/근접 GA(히트스캔·구체오버랩) + `AFPSREnemyBase`(경량 Pawn)+`UFPSREnemyHealthComponent` 데미지 브릿지. 코드리뷰 하드닝(서버 cadence 검증). 사용자 BP 3종+무기 DA+IA 셋업 완료.
- **P0** 경량 C++ 스캐폴드 — UE5.7, 플러그인 enable, GameplayTags(`Config/DefaultGameplayTags.ini`), 빌드+스모크 테스트(`FPSRoguelite.Smoke.ModuleLoads`).
- **문서/리뷰 인프라** — `Game.md`(SSOT) + `PROGRESS.md` 체계. 외부 AI 문서리뷰=`GameConfirm.md`(§10), Codex 코드리뷰=`Scripts/codex-review.ps1`→`Docs/codex-reviews/`(gitignore, §6-6), 컨설팅 토론=`Docs/ConsultLoop.md`/`/consult`→`Docs/Review/`(프롬프트 매니저 인입).

---

## 빌드 / 검증 방법
- 빌드(에디터 닫고 · **현 코드 빌드 대상 클론 = FPSRoguelite2**; 양 클론 공유 문서라 경로 중립 표기 — 빌드하는 클론의 `.uproject` 사용): `"D:\UnrealEngine\UE_5.7\Engine\Build\BatchFiles\Build.bat" FPSRogueliteEditor Win64 Development -Project="<작업 클론>\FPSRoguelite.uproject" -WaitMutex`
- 헤드리스 스모크: `UnrealEditor-Cmd.exe <uproject> -unattended -nopause -nullrhi -nosplash -nosound -ExecCmds="Automation RunTests FPSRoguelite.Smoke.ModuleLoads" -TestExit="Automation Test Queue Empty" -abslog=...`
- Codex 리뷰: `powershell -File Scripts\codex-review.ps1 -Uncommitted`(작업트리) / `-Base main`(브랜치 diff). 결과 `Docs/codex-reviews/`.
- 새 UCLASS 다수면 Live Coding 불가 → 풀빌드. IA 에셋 생성은 `Scripts/gen_input_assets.py`.

## 확정 사항 / 주의점 (운영)
- 모델 정책: **구현=Sonnet 위임 / 검증(빌드·diff·스모크·Codex·UI)=Opus 직접**(§6-5, 2026-07-02 위임 기본 Haiku→Sonnet 5 전환). 각 P단계 `phase/` 브랜치→검증→`--no-ff` 머지→브랜치 삭제(§6-7).
- 프로덕션 원칙: 콘텐츠=BP/DataAsset/config, **C++ 경로 하드코딩 금지**. 엔진 API는 소스 grep 후 사용(§6-3). 검증 없이 "완료" 보고 금지.
- **MCP(unreal) 인증 실패로 미사용** → UBT 빌드 + 헤드리스 자동화로 검증. UI는 사용자 PIE 확인.
- UE5.7 IMC 매핑은 Python 미반영 → 에디터 수동. 디버그/플레이스홀더(전환 대상)는 Game.md §8 인벤토리 참조.
- Phase 종료 시 해당 Phase 사용자 콘텐츠 동반 커밋 여부를 사용자에게 물을 것(메모리 규칙).
