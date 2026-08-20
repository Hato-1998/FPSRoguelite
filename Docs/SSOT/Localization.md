# Localization — 문자열 외부화·로컬라이제이션 (FPSRoguelite SSOT 분할)

> `Game.md`(SSOT 허브)의 분할 문서. UI 문자열·번역·저작 시트 동기화 작업 시 이 파일을 연다.
> 신설 2026-08-12 (보드: "문자열 외부화 파이프라인 + 기존 UI 전수 이관"). 설계 명세 = `Docs/Specs/LOC0_StringTablePipeline.md`.

## L-1. 원칙 (사용자 확정 2026-08-12)

- **저작 소스 = CSV → StringTable** (`LOCTABLE_FROMFILE_GAME` 런타임 직접 로드 — UStringTable 에셋 없음, "임포트 잊음=stale" 계급 제거).
- **언어 = ko 네이티브 + en·ja 타깃** (출시 확정 타깃. UE 문화권 코드 `ko`/`en`/`ja`).
- **저작 마스터 = 구글 시트**(공유 폴더 `FPS로그라이크/시트/`) / **리포 CSV = 빌드 스냅샷**. 런타임·gather·패키징은 `Content/` 물리 파일만 읽는다 — 빌드가 드라이브 라이브 상태에 의존하면 재현성이 깨지므로 **동기화는 시트→리포 단방향**(`Scripts/sync-authoring-csv.ps1`), 스냅샷은 git 커밋으로 버전화.
- **새 UI 문자열은 태어날 때부터 키다** — C++/WBP에 리터럴 추가 금지, 시트에 행 추가 → sync → 키 참조.

## L-2. 테이블·키 정책 (카드 CSV 파이프라인과 공유)

테이블 = 도메인 = 네임스페이스(1:1, FTextId 충돌 구조적 차단):

| 테이블/NS | 파일 | 내용 | 저작 마스터 |
|---|---|---|---|
| `UI` | `Content/StringTables/ST_UI.csv` | WBP·C++ UI 전 문자열 | 시트 ST_UI |
| `CardEffect` | `Content/StringTables/ST_CardEffect.csv` | GetDescription 포맷·스탯명(`Stat.*`)·레어도명(`Rarity.*`) | 시트 ST_CardEffect |
| `Card` | `Content/StringTables/ST_Card.csv` | 카드 DisplayName/Description | **파생물** — 카드 임포터가 Cards 시트의 ko/en/ja 컬럼에서 생성(§2-3-10) |

- 키 = `.` 구분 PascalCase 계층: `HUD.Run.WaveLabel` / `Menu.Main.Play` / `Widget.CardEntry.Missing` / `Fmt.WeaponStat` / `Stat.FireRate` / `Rarity.Common`.
- 카드 키 = `<CardId>.DisplayName` / `<CardId>.Description` — CardId(세이브 키)를 재사용, 에셋 리네임에도 불변.
- CSV 헤더 = `Key,SourceString,en,ja`. SourceString=한국어(네이티브). en/ja 컬럼=번역(엔진엔 메타데이터로 임포트되고 커스텀 gather 스텝이 아카이브에 주입).
- 시드 키 `Debug.LocSmoke`(3테이블 공통) = PIE/테스트 스모크 전용 — 삭제 금지.

## L-3. 파이프라인

```
구글 시트(저작)  ─sync-authoring-csv.ps1→  Content/StringTables/*.csv (git 스냅샷)
                                              │ 런타임: LOCTABLE_FROMFILE_GAME (게임 모듈 Startup)
                                              ▼
                    localization-gather.ps1: GatherText 체인
                    Gather(ko 소스) → ImportCsvTranslations(en/ja 컬럼→아카이브) → Compile
                                              ▼
                    Content/Localization/Game/{ko,en,ja}/Game.locres  (패키징 스테이징)
```

- **PO 파일 수기 편집 금지** — 번역 진실은 시트/CSV 한 곳. 매 gather마다 CSV가 아카이브를 덮어쓴다.
- gather 소스 범위 = `Source/FPSRoguelite`만(에디터 모듈의 LOCTEXT 400+건은 개발자용 — 수집 제외가 의도). **패키지(에셋) gather는 이 출하 체인에 넣지 않는다** — 넣으면 의도적으로 제외한 디자인타임 플레이스홀더·로비 보류 문자열이 출하 매니페스트·아카이브에 편입돼 번역 의무가 생긴다. 에셋 수집은 **감사 전용 타깃으로 분리**했다(§L-7, 2026-08-19).
- 타깃 추가(예: zh) = 시트 컬럼 1개 + `Game_*.ini`의 `CulturesToGenerate` 1줄 + `Game_ImportCsvTranslations.ini`의 `Cultures` 1항목 + `DefaultGame.ini CulturesToStage` 1줄(+ ICU 프리셋 포함 여부 확인 — 아래 L-4).
- **테이블 추가 비용(정직한 목록)** = 매크로 1줄(`FPSRoguelite.cpp`) + CSV 1파일 + 시트 1개 + `AuthoringSheets.json` 1항목 + `Game_ImportCsvTranslations.ini CSVFiles` 1항목 + 리로드 유틸 `GTables` 1행 + 테스트 목록 1행. 중앙 "코드 로직" 수정은 없지만 등록 지점이 위 7곳이다 — CSVFiles 누락 시 번역만 조용히 빠지는 무음 실패는 커맨드렛의 100%-미스 에러 승격이 방어(전부 미스=구성 드리프트로 간주, exit 비0).

## L-4. 갓차 (걸리면 여기부터)

- **테이블 등록은 반드시 리터럴 매크로** — gather가 CSV를 찾는 방식이 "소스에서 `LOCTABLE_FROMFILE_GAME` 파싱"(`GatherTextFromSourceCommandlet.cpp:498`)이라 ini 데이터드리븐 등록으로 바꾸면 수집이 끊긴다. 새 테이블 = `FPSRoguelite.cpp`에 매크로 1줄.
- **멀티라인 = 셀 안 `\n` 리터럴**(엔진이 이스케이프 왕복 처리). 시트에서 실제 줄바꿈(Alt+Enter) 금지.
- **`ImportStrings`는 전체 클리어 후 재구축** — CSV 파손 = 테이블 전멸. sync 스크립트의 헤더 검증·실패 시 스냅샷 보존이 방어선.
- **한국어 Excel로 CSV 직접 편집 금지**(CP949 저장 → ko/ja 파손). 편집은 항상 구글 시트에서.
- **에디터 실행 중 CSV 수정** = Tools>FPSR "StringTable CSV 리로드"로 반영(재시작 불필요).
- **PIE에서 게임 텍스트 언어 전환은 `culture=`가 아니라 "미리보기 게임 언어"** — 에디터는 게임 텍스트를 항상 원문(소스)으로 표시하는 게 기본이고, PIE는 에디터 개인설정→지역 및 언어→**미리보기 게임 언어**(Preview Game Language)를 자동 적용한다(`EditorEngine.cpp:1283` EnableGameLocalizationPreview). `culture=` 콘솔 명령은 에디터 UI 언어만 바꾼다(2026-08-13 실측). 패키지에 가까운 검증 = 독립형 게임 + `-culture=ja`.
- **쿠킹 빌드 enum DisplayName 폴백** — `UEnum::GetDisplayNameTextByIndex`는 비에디터에서 raw 이름 반환. 플레이어 노출 enum 이름은 반드시 `Stat.*` 등 키로(Phase A에서 일괄 이관).
- **WBP 편집(MCP) 후 같은 세션 PIE 금지** — [[vibeue-buildgraph-pie-worldleak]]. 배치 쓰기→에디터 재시작→PIE.
- **Localization 대시보드에서 Gather/Compile 버튼 실행 금지** — 대시보드는 `Config/Localization/Game_*.ini`를 자기 생성본으로 **덮어쓴다**(수제 주석·`Game_ImportCsvTranslations.ini` 체인 소실, 엔진 `LocalizationConfigurationScript.cpp` WriteWithSCC). 실행은 반드시 `Scripts/localization-gather.ps1`. `DefaultEditor.ini`의 타깃 항목은 UI 노출용일 뿐이다.
- **매니페스트·아카이브는 UTF-16 LE + BOM**이다 — `grep`이나 `Get-Content` 기본 인코딩으로 읽으면 **매치가 조용히 0으로 나온다**(2026-08-19 실측: `grep -o '"Key"' Game.manifest` → 0). 파서는 BOM 인식 리더를 쓸 것(`[System.IO.File]::ReadAllText` / Python `encoding='utf-16'`).
- **gather는 `FText`만 본다** — BP 그래프의 **String 핀 기본값은 어떤 gather 설정으로도 안 잡힌다.** 실측 사례 = `WBP_Lobby`의 `무기 이름`·`선택중`(에셋에 UTF-16으로 실존하지만 매니페스트에 없음). 이 계급은 감사기(§L-7)로 못 막으므로 **바이너리 실측이 유일한 수단**이다.
- **BP 그래프 핀의 FText는 에디터 전용 데이터로 분류된다** — `ShouldGatherFromEditorOnlyData=false`면 통째로 누락된다(엔진 `EdGraphNode.cpp` `GatherGraphNodeForLocalization`이 `bIsEditorOnly=true`로 넘기고 `GatherTextFromAssetsCommandlet.cpp:605`가 필터). 감사 타깃은 그래서 `true`다.
- **`SkipGatherCache=true`가 "항상 새로 읽는다"는 뜻이 아니다** — `CalculatePackageLocCacheState`가 `PKG_RequiresLocalizationGather` 플래그가 붙은 패키지만 재로드 대상으로 되돌린다(엔진 `GatherTextFromAssetsCommandlet.cpp:1754`). 실측 2026-08-19: 221패키지 중 실제 로드 72개, 나머지는 패키지 헤더의 캐시본 사용.
- **패키지 문화권은 ICU 프리셋이 결정** — `CulturesToStage`만으론 부족. `InternationalizationPreset=EFIGSCJK`(DefaultGame.ini)가 ko/ja ICU 데이터를 스테이징한다. 이걸 지우면 패키지에서 culture=ja/ko 활성화 자체가 불가(빌드는 통과, 런타임만 무음 실패). 패키지 기본 문화권 = en(BaseGame `DefaultCulture=en` 미덮음 — 제품 결정 대기).

## L-5. 시트 동기화 규약

- 매핑 = `Config/AuthoringSheets.json` (sheetId/gid/target/expectedHeader). 공유 폴더ID `1jdMK1VlVI2t71nMc89DWCPxuw-jQjnLv`.
- 시트 권한 = "링크 보유자 보기 가능"(무인증 export URL — 2026-08-12 HTTP 200 검증). 민감해지면 서비스 계정 인증으로 승격.
- **초기 시딩(리포→시트)은 테이블당 1회만**(마이그레이션 시점). 이후 시트=마스터, 동기화=단방향. 양방향 동기화 금지.
- provenance = `Config/AuthoringSheets.manifest.json`(다운로드 UTC·sha256) — 스냅샷 커밋에 동봉. (Content/StringTables/ 안에 두면 UFS 스테이징에 걸려 pak에 실리므로 Config/에 둔다.)

## L-6. 경계

- 로케일 중립 문자열(숫자 조합 `%02d:%02d` 타이머 등)은 키 이관 대상 아님. 퍼센트/숫자는 `FText::AsPercent`/`AsNumber`(ICU) 사용.
- ja 폰트 글리프 미포함 시 폰트 폴백 구성 = 별도 보드 행(이번 범위 밖).
- 카드 CSV 저작 스키마·임포터 = `CombatWeaponCard.md` §2-3-10(Phase B가 신설).

## L-7. BP 인라인 Text 감사 (M0 EC ② 회귀 가드, 신설 2026-08-19)

> **왜 있는가**: `Roadmap.md` §7-6 M0 **EC ②**는 검사 대상을 *"C++ `FText`/`LOCTEXT` **및 BP 위젯의 인라인 Text 프로퍼티**"*로 정의하고 기계적 판정 수단을 *"`Game.manifest` 수집분"*으로 지정했는데, 출하 gather에 **에셋 수집 단계가 없어 정의의 절반이 탐지되지 않았다**. EC ②의 BP 축은 수동 열거로 닫혔고, 새 리터럴을 막을 장치가 없었다. 이 절이 그 장치다.

| 구성 요소 | 경로 | 역할 |
|---|---|---|
| 감사 gather 타깃 | `Config/Localization/Game_AuditBPText.ini` | `GatherTextFromAssets` → `Saved/Localization/BPTextAudit/BPTextAudit.manifest`. **출하 체인과 완전 분리**(`Saved/`는 gitignore) |
| 검사기 | `Scripts/localization-audit-bptext.ps1` | 매니페스트 파싱 → 분류 → 얼라우리스트 대조 → 신규 있으면 **exit 1** |
| 얼라우리스트 | `Config/Localization/BPTextAudit_Allowlist.json` | 문서화된 카브아웃(24항목). `path`+`source` 일치로 면제 |

**실행**

```
powershell -File Scripts\localization-audit-bptext.ps1
```

- `-SkipGather` = 커맨드렛 생략, 기존 매니페스트 재사용(빠른 반복). `-ReportOnly` = 신규가 있어도 exit 0(조사용).
- 실측 2026-08-19: 총 68건 = **게이트 24**(전부 면제) + **참고 44**. 커맨드렛 수 초 · 에디터 GUI/PIE 불필요.

**분류 규칙** (검사기 6단계 주석과 일치해야 한다)

- **게이트**(실패 판정에 쓰는 것) = 경로에 `WBP_` 또는 `:WidgetTree.` — 단 `EUW_`(에디터 유틸리티 위젯 = 개발도구, 패키지 미포함)와 네임스페이스 `UObjectDisplayNames`(위젯 애니메이션 트랙 `DisplayName` = 엔진 자동생성 메타데이터)는 **구조적으로 제외**. 이 둘을 얼라우리스트로 처리하면 애니메이션 트랙을 가진 위젯이 새로 생길 때마다 오탐이 난다.
- **참고**(실패 판정에 쓰지 않음) = 나머지 프로젝트 에셋. 여기 뜬 것이 무해하다는 뜻은 아니다 — 실제로 **무기·프래그먼트·미션 DataAsset의 인라인 `FText` ~28건**이 여기서 발견됐고 별도 보드 행으로 등록했다(EC ②의 문면 정의 밖이지만 플레이어 노출 문자열이다).

**유지보수 계약**

- 감사 config의 `IncludePathFilters=Content/*` + 서드파티 루트 제외는 **denylist**다 — **새 서드파티 킷을 `Content/` 에 임포트하면 `ExcludePathFilters` 에 그 루트를 한 줄 추가**해야 한다. 안 하면 `참고` 섹션이 킷 문자열로 뒤덮이고 로드 패키지 수가 늘어 느려진다(게이트 판정 자체는 위젯 경로만 보므로 틀려지지는 않는다). allowlist(포함 목록) 방식을 택하지 않은 이유 = 프로젝트가 새 폴더를 만들 때 **게이트가 조용히 뚫리는 쪽**이 더 위험하다.
- 얼라우리스트는 `path`+`source` 일치라 **위젯을 리네임하거나 문자열을 고치면 게이트가 발화한다**(fail-closed). 의도된 동작이다 — 확인하고 얼라우리스트를 갱신하거나 StringTable로 이관한다.

**얼라우리스트 규약**

- 항목 = `{path, source, reason, note}`. `namespace`·`key`도 쓸 수 있고, **값이 있는 필드가 전부 일치**할 때만 면제된다(필드가 하나도 없는 항목은 무시 + 경고 — 전체 와일드카드 금지).
- `reason` = **`design-time-placeholder`**(런타임 `SetText`/프로퍼티 바인딩으로 덮여 화면에 안 뜨는 것 — 이관 금지, 의도된 상태) 또는 **`M3-lobby-rework`**(로비 전면 개편에서 통째로 교체될 화면 — 지금 이관하면 버려진다).
- 🔒 **"지금 존재하니까"는 면제 근거가 아니다.** 새 항목을 넣을 땐 `note`에 SSOT 근거를 적는다. 근거가 없으면 **넣지 말고 고치거나 사용자에게 보고**한다.
- 항목 추가는 손으로 옮기지 말고 매니페스트에서 뽑아라(경로·원문 전사 오류가 곧 오탐/누락이 된다).

**한계 (정직 기록)**

- **`FText`가 아닌 리터럴은 못 잡는다**(§L-4 갓차) — `WBP_Lobby`의 `무기 이름`·`선택중`이 그 사례다. 즉 이 감사기는 EC ②의 BP 축을 **완전히** 기계화하지 못하고, `FText` 계급만 막는다.
- 훅/CI 배선은 **하지 않았다** — `.claude/settings.json`이 클론 로컬이라(`Workflow.md` §6-9 (7)) 배선은 공유되지 않는다. 지금은 수동 실행이고, exit 코드는 CI가 그대로 물 수 있는 형태다.
