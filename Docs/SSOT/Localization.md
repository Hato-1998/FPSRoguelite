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
                    Gather(ko 소스+에셋) → ImportCsvTranslations(en/ja 컬럼→아카이브) → Compile
                                              ▼
                    Content/Localization/Game/{ko,en,ja}/Game.locres  (패키징 스테이징)
```

- **PO 파일 수기 편집 금지** — 번역 진실은 시트/CSV 한 곳. 매 gather마다 CSV가 아카이브를 덮어쓴다.
- gather 소스 범위 = `Source/FPSRoguelite`만(에디터 모듈의 LOCTEXT 400+건은 개발자용 — 수집 제외가 의도). 패키지(에셋) gather는 Phase 0 미사용 — Phase A에서 "잔존 인라인 FText 탐지" 용도로 추가 검토.
- 타깃 추가(예: zh) = 시트 컬럼 1개 + `Game_*.ini`의 `CulturesToGenerate` 1줄 + `Game_ImportCsvTranslations.ini`의 `Cultures` 1항목 + `DefaultGame.ini CulturesToStage` 1줄(+ ICU 프리셋 포함 여부 확인 — 아래 L-4).
- **테이블 추가 비용(정직한 목록)** = 매크로 1줄(`FPSRoguelite.cpp`) + CSV 1파일 + 시트 1개 + `AuthoringSheets.json` 1항목 + `Game_ImportCsvTranslations.ini CSVFiles` 1항목 + 리로드 유틸 `GTables` 1행 + 테스트 목록 1행. 중앙 "코드 로직" 수정은 없지만 등록 지점이 위 7곳이다 — CSVFiles 누락 시 번역만 조용히 빠지는 무음 실패는 커맨드렛의 100%-미스 에러 승격이 방어(전부 미스=구성 드리프트로 간주, exit 비0).

## L-4. 갓차 (걸리면 여기부터)

- **테이블 등록은 반드시 리터럴 매크로** — gather가 CSV를 찾는 방식이 "소스에서 `LOCTABLE_FROMFILE_GAME` 파싱"(`GatherTextFromSourceCommandlet.cpp:498`)이라 ini 데이터드리븐 등록으로 바꾸면 수집이 끊긴다. 새 테이블 = `FPSRoguelite.cpp`에 매크로 1줄.
- **멀티라인 = 셀 안 `\n` 리터럴**(엔진이 이스케이프 왕복 처리). 시트에서 실제 줄바꿈(Alt+Enter) 금지.
- **`ImportStrings`는 전체 클리어 후 재구축** — CSV 파손 = 테이블 전멸. sync 스크립트의 헤더 검증·실패 시 스냅샷 보존이 방어선.
- **한국어 Excel로 CSV 직접 편집 금지**(CP949 저장 → ko/ja 파손). 편집은 항상 구글 시트에서.
- **에디터 실행 중 CSV 수정** = Tools>FPSR "StringTable CSV 리로드"로 반영(재시작 불필요).
- **쿠킹 빌드 enum DisplayName 폴백** — `UEnum::GetDisplayNameTextByIndex`는 비에디터에서 raw 이름 반환. 플레이어 노출 enum 이름은 반드시 `Stat.*` 등 키로(Phase A에서 일괄 이관).
- **WBP 편집(MCP) 후 같은 세션 PIE 금지** — [[vibeue-buildgraph-pie-worldleak]]. 배치 쓰기→에디터 재시작→PIE.
- **Localization 대시보드에서 Gather/Compile 버튼 실행 금지** — 대시보드는 `Config/Localization/Game_*.ini`를 자기 생성본으로 **덮어쓴다**(수제 주석·`Game_ImportCsvTranslations.ini` 체인 소실, 엔진 `LocalizationConfigurationScript.cpp` WriteWithSCC). 실행은 반드시 `Scripts/localization-gather.ps1`. `DefaultEditor.ini`의 타깃 항목은 UI 노출용일 뿐이다.
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
