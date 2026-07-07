# 컨설트: 무기·미션·카드 데이터 편집 툴 P1 — FPSR Data Editor (배선/카탈로그 + 카드 magnitude 그리드) (2026-07-07)

> 백엔드(Claude, 시스템/유지보수 렌즈) × 클라이언트(Codex, 기획자-툴링 렌즈) **5라운드 수렴 토론**. 원시 응답 = `Docs/Review/_raw/20260707-*-data-tooling-p1-r{1..5}.md`.
> 선행 = [`20260707-data-tooling-p0.md`](20260707-data-tooling-p0.md)(P0 검증 시임, main 머지 `57270c5`). **이 리포트가 P1 편집 툴 설계 정본.**

## 범위 / 읽은 컨텍스트
- **UE5.7 엔진 소스 실측**(과설계 게이트의 근거): `PropertyEditorToolkit`(Property Matrix) + `PropertyTableColumn.cpp`·`PropertyTable.cpp`·`AssetFileContextMenu.cpp`·Reference Viewer(`EdGraph_ReferenceViewer.cpp`). 7-리더 병렬 워크플로.
- **프로젝트 소스 대조**: `FPSRWeaponDataAsset`/`FFPSRWeaponStatBlock`(BaseStats 42필드) · `FPSRCardDataAsset`/`FPSRCardEffect`(Instanced 폴리모픽 5종)/`FPSRCardTypes`(FFPSRCardRarityTier)/`FPSRCardPoolDataAsset` · `FPSRRunScheduleDataAsset`(MissionWindows 시간축)/`FPSRMissionDataAsset`/`AFPSRMissionActor`(BP CDO 튜닝) · `FPSRLoadoutPoolDataAsset` · `FFPSRAnchoredValidationService`(P0 재사용면).
- **정책/백로그**: `Workflow.md`(빌드 -NoXGE·phase 브랜치·모델정책) · `ToolingBacklog.md`(EUW 상정 후보) · `TaskPrompts_Master.md §E`(H1/H2/H3) · 메모리 `[[card-pool-routing]]`·`[[extensibility-first-designer-tooling]]`.

## 🔧 백엔드 렌즈 핵심 입장 (Claude)
과설계 게이트를 **엔진 소스로 실증**했다: 무기 밸런스 그리드는 UE 내장 Property Matrix가 사실상 공짜로 준다(BaseStats 42필드가 전부 plain 스칼라 → 다중선택 후 열 핀·정렬·복붙편집). 따라서 **무기 매트릭스를 커스텀으로 짓는 건 재발명**. 반면 (1)카드 per-rarity magnitude는 `Instanced 효과 → RarityTiers 배열`에 묻혀 Property Matrix가 구조적으로 못 닿고(`!CPF_InstancedReference` 가드 + array root-only), (2)배선-멤버십(역참조 편집)은 Reference Viewer가 읽기만 준다. P0가 찾은 **고아 16개는 전부 배선-멤버십 문제**(사용자 즉통점). 그러니 P1 커스텀 투자 = **배선/카탈로그 편집 + 카드 magnitude 그리드**에 집중하고, `FFPSRAnchoredValidationService`를 재사용해 라이브 고아/멤버십 상태를 띄운다. 딜리버리는 하이브리드(C++ 반사 헬퍼 + 얇은 Slate), 커스텀 위젯 총량은 하드 캡한다.

## 🎮 클라이언트 렌즈 핵심 입장 (Codex)
P1의 첫 체감 가치는 "드래그드롭 UX"가 아니라 **"고아를 찾아 올바른 route에 꽂고 저장·재검증"**이다. 미션 타임라인은 "이해" 고통이라 P2 read-only 프리뷰로 족하고, 무기 매트릭스는 Property Matrix로 빠진다. 커스텀 Slate는 **고아 리스트 1 + magnitude 그리드 1**로 하드 캡하고 나머지는 `IDetailsView` 임베드로 UE 내장 편집/트랜잭션 모델을 타라. magnitude의 밸런싱 단위는 카드가 아니라 **효과**이므로 그리드 행은 `(카드,효과)` 페어여야 한다. 배선은 **route-인지**여야 한다(잘못된 배열=라우팅 버그) — 자명 누수는 즉시 차단, 애매한 behavior-fragment 라우트만 경고+확정. 확장 시임은 `UFPSRCardEffect` 서브클래스 가상에 두되, route는 스키마가 소유한 **닫힌 enum**이 정직하다. 최대 리스크 = 배선 UX가 CardPool만 닫는 것; 최대 과설계 = route 시임이 "미니 라우팅 프레임워크"로 붓는 것.

## 토론 로그 요약
- **R1**: 4쟁점(순서·딜리버리·편집깊이·magnitude 뷰) 즉시 수렴. 순서 역전 확정(배선+magnitude 먼저, 무기매트릭스=문서, 미션타임라인=P2). 하이브리드. 완전편집이되 좁게. 그리드=(카드,효과) 페어.
- **R2**: 3엣지 압박. (A)커스텀 Slate 캡=고아리스트1+그리드1, 나머지 IDetailsView(양방향 전용뷰는 P1.5+). (B)magnitude=존재티어 in-place+literal만(생성/산술 bulk 후속). (C)H2 비차단, 단 route-라벨+최소 preflight guard.
- **R3**: 확장성(OCP) 시임. (D)`UFPSRCardEffect`에 WITH_EDITOR 가상(GetEditorGridSummary/GetEditorMagnitudeText/GetEditorEligibleRoutes), Slate는 Editor 모듈. (E)preflight 이원화=자명 누수 하드차단 + behavior-fragment만 경고(기본권장 UnlockableFeatures). (F)검증=헬퍼 라운드트립 2개(magnitude·멤버십) 상한.
- **R4**: 구조 2개. (G)route=닫힌 C++ enum `EFPSRCardRoute`(효과는 부분집합 선언), 태그 아님. (H)배선편집기=anchor-agnostic 셸(서비스 앵커/고아 재사용+IDetailsView), 축별 전용화면 P2. (I)단일 도킹 탭; 미션 read-only 바 프리뷰 P1 저비용 편승(편집은 P2); 무기매트릭스=Docs 1페이지.
- **R5(critic·잠금)**: (crit1)최대 리스크=배선이 CardPool만 닫힘 → 고아 3축(카드9/미션6/Knife) 전부 guided-add 아니면 P1 목표문구를 "CardPool 배선+magnitude"로 낮춰라. (crit2)과설계=route 시임이 프레임워크化 → 컷라인 "닫힌 enum4 + 현 5효과 매핑까지, 새 route DSL/policy/reflection 금지". (crit3)놓친 논점 1=`OfferRarities` stale → grid setter는 반드시 owning card helper 경유(Refresh/validation). 그 외 **수렴**. (crit4)공수=**L**, **2 브랜치**(배선 먼저 머지 → magnitude 후속).

## ✅ 합의 권고 — 최종 P1 스펙 (FPSR Data Editor)
1. **단일 도킹 탭** `Tools > FPSR > Data Editor`, 기존 `FPSRogueliteEditor` 모듈에 추가(신규 모듈 금지). 좌=앵커 목록(서비스 `FindAnchorAssets`)+고아 목록(`FindOrphans`), 우=선택 컨텍스트.
2. **배선 편집(anchor-agnostic 셸)**: 우측 기본 = `IDetailsView`로 선택 앵커의 멤버십 배열(add/remove·트랜잭션 공짜) 편집. 고아 리스트에서 `Add orphan → [route 라벨 드롭다운]`. **route-인지 preflight**: 자명 누수 하드차단(GrantWeapon→WeaponUnlock 외/Character→무기배열/WeaponStat(thisWeapon)→전역/Group↔route 충돌) + behavior-fragment 레벨업↔미션클리어만 경고+확정(기본 UnlockableFeatures).
3. **카드 magnitude 그리드**(카드풀/카드 선택 시): 행=`(Card,Effect)` 페어 평탄화, 컬럼 `CardId|Name|EffectIndex|EffectType|Summary|Common|Rare|Epic|Legendary`. **존재 티어만 in-place literal 편집**(없는 등급=비활성). 그리드 setter는 **owning card helper 경유**(→ `OfferRarities` Refresh + validation). 티어 생성/삭제·산술 bulk=P2.
4. **확장성 시임**: `UFPSRCardEffect`에 `#if WITH_EDITOR` 가상 — `GetEditorGridSummary(Rarity)` / `GetEditorMagnitudeText(Rarity)` / `GetEditorEligibleRoutes() → TArray<EFPSRCardRoute>`. Slate/컬럼 렌더는 Editor 모듈 소유. **route = 닫힌 enum** `EFPSRCardRoute{LevelUpGlobal, MissionClearNewWeapon, LevelUpWeapon, MissionClearWeaponFeature}`; route→배열 매핑 registry는 Editor/Validation 소유. **컷라인: 닫힌 enum4 + 현 5효과 매핑까지. route DSL/policy object/reflection 자동라우팅 금지.**
5. **미션 read-only 타임라인 바**(RunSchedule 선택 시): `MissionWindows [Min,Max]` + `BossTime` 벽만 페인트. 드래그/리사이즈/윈도우 편집/충돌해결 UI = **P2**.
6. **쓰기 경로**: `Modify()` → 직접 set → `PostEditChangeProperty` → `MarkPackageDirty`. **"Save Modified + Rescan"** 버튼(저장+AssetRegistry 재스캔→고아/reachable 재계산). 저장 전 = 명시적 **"검증 미갱신/stale"** 상태 표시. async 스캔 가드(`IsLoadingAssets`).
7. **커스텀 Slate 하드캡** = 고아 리스트 1 + magnitude 그리드 1 + read-only 타임라인 바 1(순수 페인트). **그 외 전부 `IDetailsView`.**
8. **딜리버리 = 하이브리드**: C++ 헬퍼(멤버십 add/remove, magnitude flatten get/set, preflight, `FFPSRAnchoredValidationService` 재사용) + 얇은 C++ Slate. **EUW 아님**(타입세이프·유지보수, 백로그 EUW 상정은 폐기 근거 명시).
9. **무기 밸런스 = 문서화**: `Docs/PropertyMatrix_DataEditing.md` 1페이지(무기 DA 다중선택 → Property Matrix 일괄편집; 커스텀 Data Editor는 배선/고아/magnitude 전용). **커스텀 매트릭스 미구현.**
10. **검증 세트**: `-NoXGE` 빌드 `Result: Succeeded` + 헤드리스 `ModuleLoads` 스모크 + **헬퍼 라운드트립 자동테스트 2개**(magnitude set / 멤버십 add-remove: 동일 C++ get/set 헬퍼 직접호출→Modify→PostEditChange→save→reload→validate; `OfferRarities` 갱신·`RarityTiers` 보존·null효과 미생성·`CardId`/`CardFamily` 무손상·고아 감소 단언; 임시에셋=excluded test path) + 수동 탭 확인 + 실콘텐츠 1건 수정 후 `validate-data` 커맨드릿 재실행(고아 16→감소).
11. **브랜치/공수**: 공수 **L**. **🔒사용자결정(D4)=1 브랜치** `phase/editor-tooling-p1-dataeditor`에서 배선+magnitude 함께 완성 후 `--no-ff` 1회 머지(+Codex 머지게이트). 구현=Sonnet 위임/검증=Opus 직접. (Codex 권고는 2브랜치였으나 사용자가 단순화 선택 — 완성까지 미머지 트레이드오프 수용.)

## ⚖️ 미해결 쟁점
- **미션 타임라인 프리뷰 scope-creep 선**: P1에 read-only 바를 얹으면 이득이 크나, "충돌 해결/편집"으로 번지기 쉽다. 합의 = **read-only 바까지만**(그 이상은 P2). 양 렌즈 동의, 실행 시 컷라인 준수가 관건.

## 🙋 사용자 결정 필요 → 🔒 확정 (2026-07-07)
1. **(D1) 미션 read-only 타임라인 바 P1 편승** → 🔒 **편승**. RunSchedule 선택 시 read-only 바(편집 P2).
2. **(D2) magnitude 그리드 편집 범위** → 🔒 **편집 포함(존재 티어 in-place)**. 티어 생성/삭제·산술 bulk = P2.
3. **(D3) P1 배선 목표 범위** → 🔒 **고아 3축(카드9/미션6/Knife) 전부 guided-add**, "고아 16 감소"를 P1 검증 목표로.
4. **(D4) 브랜치 분할** → 🔒 **1브랜치** `phase/editor-tooling-p1-dataeditor`(배선+magnitude 함께, --no-ff 1회 머지).

> **H2(라우팅 스펙 정합) 관계**: P1 툴 자체는 **비차단**(자명 누수는 preflight가 흡수, 애매부만 경고). H2는 "자동 라우팅-누수 검증" 추가의 선행조건으로 남는다(별개). **H3(미션 튜닝 DA-소유 통합)**: 미션 타임라인 *편집*(P2)의 선행 SSOT 결정 — P1 read-only 바는 무관.

## 📌 액션 아이템
- **[구현·사용자승인후]** 위 ✅ 최종 P1 스펙. 2 브랜치(`phase/editor-tooling-p1-wiring` → `-magnitude`). 신규파일→에디터종료→`-NoXGE` 빌드→재기동.
- **[문서]** `Docs/PropertyMatrix_DataEditing.md` 신설(무기 밸런스 = 내장 Property Matrix 경로).
- **[백로그]** `ToolingBacklog.md`: "Card Catalog & Balance Matrix" + "Run Schedule Timeline Editor(read-only)" 상태/범위를 이 P1 스펙으로 갱신(EUW 상정 → C++ 하이브리드로 정정, 배선 편집기 신설 행 추가).
- **[SSOT·후속]** H2(CombatWeaponCard §2-3-4 라우팅 정합) = 자동 라우팅-누수 검증의 선행. H3(RunFlow §2-8 미션 튜닝 DA-소유) = 미션 타임라인 편집(P2)의 선행.
- **[§E 인입]** 이 리포트를 `TaskPrompts_Master.md §E`에 H4 행으로 등재.
