# LOC0 — StringTable CSV 파이프라인 공통 기반 (Phase 0)

## 1. 메타

| 항목 | 값 |
|---|---|
| 유닛 ID / 이름 | LOC0 / StringTable CSV 파이프라인 공통 기반 |
| 브랜치 | `phase/loc-foundation-stringtable` |
| 작성 모델 | `claude-fable-5` |
| 작성일 / 최종 갱신 | 2026-08-12 |
| 상태 | `구현완료` (C3 통과 2026-08-12 — §13 원장 참조) |
| 관련 SSOT | `Docs/SSOT/Localization.md`(본 유닛이 신설), `Docs/SSOT/Workflow.md` §6 |
| 관련 메모리 | [[vibeue-buildgraph-pie-worldleak]](후속 Phase A에서만 해당) |

## 2. 목표 / 비목표

**목표**
- 게임 런타임 문자열의 소스가 `Content/StringTables/*.csv` 3개(UI/CardEffect/Card)가 된다 — 코드/에셋에 새 하드코딩 문자열을 추가할 이유가 사라진다.
- `Scripts/localization-gather.ps1` 1회 실행으로 ko(네이티브) gather → en/ja 번역 주입 → LocRes 컴파일까지 왕복된다.
- 구글 시트(저작 마스터) → 리포 CSV(빌드 스냅샷) 동기화가 `Scripts/sync-authoring-csv.ps1`로 무인증 동작한다.
- PIE에서 `culture=en` / `culture=ja` 전환 시 시드 문자열이 해당 언어로 표시된다.

**비목표(Non-goals)**
- 기존 하드코딩 문자열의 이관(= Phase A), 카드 CSV 임포터(= Phase B)는 하지 않는다.
- `ST_Card.csv`의 내용 채움(임포터 파생물 — Phase B). 본 유닛은 시드 1행 + 등록만.
- 에디터 전용 LOCTEXT(검증 메시지·Slate 툴 400+건)는 gather 대상에 넣지 않는다.
- ja 폰트 폴백 구성(글리프 확인만 하고, 미포함 시 별도 보드 행).
- PO 편집 워크플로/외부 번역사 연동.

## 3. 제1원리 3줄 (핵심원칙 4)

1. **제1원리 근거** — 콘텐츠(문자열·카드 수치)는 C++가 아니라 데이터가 소유한다(핵심 2). 저작 소스와 런타임 소스를 같은 파일(CSV)로 두면 "임포트 잊음 = stale 에셋" 오류 계급이 통째로 사라지고 diff가 완전해진다.
2. **엔진 기본값·기존 인프라와의 관계** — 엔진 공식 경로를 그대로 쓴다(덮지 않음): `LOCTABLE_FROMFILE_GAME` 매크로(`StringTableRegistry.h:117`), gather의 매크로 소스-파싱 수집(`GatherTextFromSourceCommandlet.cpp:498` — **CSV 발견은 소스의 매크로 리터럴을 파싱해서 이뤄지므로 테이블 등록은 반드시 리터럴 매크로 호출이어야 한다. ini 데이터드리븐 등록으로 바꾸면 gather가 CSV를 못 찾는다** — 이 제약이 §5의 "매크로 3줄 하드코딩"의 근거), 커스텀 gather 스텝(`GatherTextCommandlet.cpp:383-442`), 번역 주입 `FLocTextHelper::ImportTranslation`(`LocTextHelper.h:771`), 런타임 경로 `BaseGame.ini +LocalizationPaths=%GAMEDIR%Content/Localization/Game`(타깃명 Game이면 프로젝트 ini 추가 불필요).
3. **프로젝트 제약과의 정합** — 카드 CSV 파이프라인(Phase B)과 "시트=저작 마스터, 리포 CSV=빌드 스냅샷, 파생물=에셋" 모델을 공유. 문자열 테이블 추가 = 등록 지점 7곳(매크로/CSV/시트/AuthoringSheets.json/CSVFiles/리로드 GTables/테스트 목록 — 정직한 목록은 SSOT L-3; C3 레드팀 P3-4로 원 문구 "중앙 수정 없음" 정정). CSVFiles 누락류 드리프트는 커맨드렛 100%-미스 에러 승격이 방어.

## 4. 파일 목록

| 경로 | 신규/수정 | 한 줄 설명 |
|---|---|---|
| `Source/FPSRoguelite/Public/FPSRoguelite.h` | 수정 | `FFPSRogueliteGameModule` 선언 추가 |
| `Source/FPSRoguelite/Private/FPSRoguelite.cpp` | 수정 | 모듈 구현 교체 + `LOCTABLE_FROMFILE_GAME` 3건 |
| `Content/StringTables/ST_UI.csv` | 신규 | UI 네임스페이스 시드(헤더+1행) |
| `Content/StringTables/ST_CardEffect.csv` | 신규 | CardEffect 네임스페이스 시드 |
| `Content/StringTables/ST_Card.csv` | 신규 | Card 네임스페이스 시드(Phase B 임포터가 채움) |
| `Config/DefaultGame.ini` | 수정 | UFS 스테이징 + CulturesToStage |
| `Config/DefaultEditor.ini` | 수정 | `GameTargetsSettings`에 타깃 `Game` (엔진 `LocalizationConfigurationScript.cpp` 생성 포맷 준수) |
| `Config/Localization/Game_Gather.ini` 외 태스크 ini 일습 | 신규 | Gather/Export/Compile/ImportCsvTranslations 스텝 설정 |
| `Source/FPSRogueliteEditor/Public/Localization/FPSRImportCsvTranslationsCommandlet.h` | 신규 | 번역 컬럼 주입 gather 스텝 |
| `Source/FPSRogueliteEditor/Private/Localization/FPSRImportCsvTranslationsCommandlet.cpp` | 신규 | 〃 |
| `Source/FPSRogueliteEditor/Private/Localization/FPSRStringTableReload.h/.cpp` | 신규 | CSV 재등록 유틸(Tools 메뉴) |
| `Source/FPSRogueliteEditor/Private/FPSRogueliteEditorModule.cpp` | 수정 | Tools>FPSR 메뉴에 "StringTable CSV 리로드" 항목 |
| `Source/FPSRogueliteEditor/Private/Tests/FPSRStringTableCsvTest.cpp` | 신규 | CSV 파싱·키 유일성·커버리지·해석 automation |
| `Scripts/sync-authoring-csv.ps1` | 신규 | 시트 export URL → 리포 CSV pull |
| `Scripts/localization-gather.ps1` | 신규 | (선택 -Sync) → GatherText 체인 원버튼 |
| `Config/AuthoringSheets.json` | 신규 | 시트ID/gid/대상경로/기대헤더 매핑 |
| `Docs/SSOT/Localization.md` | 신규 | 키 정책·파이프라인·워크플로 SSOT (0-1 커밋) |
| `Game.md` | 수정 | §0-1 라우팅 행 추가 (0-1 커밋) |

## 5. 인터페이스 선언 (헤더 스케치)

```cpp
// ── Source/FPSRoguelite/Public/FPSRoguelite.h ──────────────────────────────
#pragma once
#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

/** Primary game module. Owns runtime string-table registration (CSV = source of truth).
 *  Tables are registered with literal LOCTABLE_FROMFILE_GAME calls in the .cpp — the localization
 *  gather commandlet DISCOVERS csv tables by parsing source for that macro, so this must stay
 *  a literal call site (no ini-driven loop). Adding a table = one macro line + one CSV. */
class FFPSRogueliteGameModule : public FDefaultGameModuleImpl
{
public:
    virtual void StartupModule() override;   // LOCTABLE_FROMFILE_GAME x3 (UI / CardEffect / Card)
    virtual void ShutdownModule() override;  // FStringTableRegistry::UnregisterStringTable x3 (대칭 해제)
};
// .cpp: IMPLEMENT_PRIMARY_GAME_MODULE(FFPSRogueliteGameModule, FPSRoguelite, "FPSRoguelite");

// ── Source/FPSRogueliteEditor/Public/Localization/FPSRImportCsvTranslationsCommandlet.h ──
#pragma once
#include "Commandlets/GatherTextCommandletBase.h"
#include "FPSRImportCsvTranslationsCommandlet.generated.h"

/** Custom gather step: reads translation columns (column name == culture code, e.g. "en","ja")
 *  from the authoring CSVs and imports them into per-culture archives via FLocTextHelper.
 *  Runs between Gather and Compile in the GatherText step chain ([GatherTextStep{N}] config).
 *  Config keys (own section): CSVFiles (semicolon list, ContentDir-relative), Cultures (comma list). */
UCLASS()
class UFPSRImportCsvTranslationsCommandlet : public UGatherTextCommandletBase
{
    GENERATED_BODY()
public:
    virtual int32 Main(const FString& Params) override; // 반환: 0=성공, 비0=실패(파싱/컬럼/아카이브 오류)
};

// ── Source/FPSRogueliteEditor/Private/Localization/FPSRStringTableReload.h ──
#pragma once
#include "CoreMinimal.h"

/** Editor-only: re-register the game's CSV string tables without restarting the editor.
 *  Unregisters each table id then re-runs the same registration the game module performs. */
namespace FPSRStringTableReload
{
    /** 반환: 재등록 성공 테이블 수. 실패 테이블은 로그 Error + 알림 토스트. */
    int32 ReloadAll();
}
```

**CSV 스키마(3파일 공통)** — 헤더 `Key,SourceString,en,ja` (Key/SourceString=엔진 예약, en/ja=메타데이터로 임포트되는 번역 컬럼). 시드 행: `Debug.LocSmoke,"로컬라이제이션 시드","Localization seed","ローカライズシード"` (테이블별 동일 키 — PIE 스모크·테스트 전용, Phase A/B에서 유지).

**AuthoringSheets.json 스키마**
```json
{ "sheets": [ { "name": "ST_UI",
                "sheetId": "18c4OM0fDL7021aQsZceCIzSC3ayCh72LP5NomTuqka8",
                "gid": "0",
                "target": "Content/StringTables/ST_UI.csv",
                "expectedHeader": ["Key","SourceString","en","ja"] },
              { "name": "ST_CardEffect", "sheetId": "1HbZxxghuw2CWwkXFbrYfuNsauj6VRouublXtAi7uHbs",
                "gid": "0", "target": "Content/StringTables/ST_CardEffect.csv",
                "expectedHeader": ["Key","SourceString","en","ja"] } ] }
```
(ST_Card는 파생물이라 시트 매핑 없음. Cards/CardCatalog 항목은 Phase B가 추가.)

**스크립트 계약**
- `sync-authoring-csv.ps1 [-SheetName <이름>]`: 각 항목에 대해 `https://docs.google.com/spreadsheets/d/<sheetId>/export?format=csv&gid=<gid>` GET(원시 바이트 수신·30초 타임아웃) → 1행 헤더를 `expectedHeader`와 정확 비교(불일치=해당 파일 스킵+exit 1 예약) → 통과 시 `target`에 수신 바이트 그대로 저장(재인코딩 금지 — PS5.1 `$Response.Content` 문자열 디코드는 Latin-1 이중 인코딩 파손, C3 실측) → `Config/AuthoringSheets.manifest.json`(UFS 스테이징 밖)에 `{name, utcTime, sha256}` 기록. 다운로드/검증 실패 시 기존 파일 무접촉.
- `localization-gather.ps1 [-Sync]`: (-Sync 시 위 스크립트 선행, 실패 시 중단) → `UnrealEditor-Cmd.exe <uproject> -run=GatherText -config="Config/Localization/Game_Gather.ini;Config/Localization/Game_ImportCsvTranslations.ini;Config/Localization/Game_Compile.ini" -log` → exit code 전파. 엔진 경로·인보크 방식은 `Scripts/validate-data.ps1`의 기존 패턴을 미러.

## 6. 함수별 계약

| 함수 | 권위 | 호출자 | 전제조건 | 실패 시 동작 |
|---|---|---|---|---|
| `FFPSRogueliteGameModule::StartupModule` | 로컬(전 머신 동일) | 모듈 로더 | CSV 3파일 존재 | 엔진 매크로가 내부 로그 — 추가 크래시 금지, 누락 테이블은 키 원문 노출로 관측 |
| `ShutdownModule` | 로컬 | 모듈 로더 | — | 등록 안 된 테이블 해제 시도는 no-op |
| `UFPSRImportCsvTranslationsCommandlet::Main` | 에디터 커맨드렛 | GatherText 체인 | Gather 스텝이 매니페스트 선생성 | 비0 반환 → 체인 중단(부분 LocRes 방지) |
| `FPSRStringTableReload::ReloadAll` | 에디터 | Tools 메뉴 | — | 실패 테이블 로그+토스트, 나머지는 계속 |

## 7. 복제표 (§6-3)

해당 없음 — 본 유닛은 복제 프로퍼티/RPC를 도입하지 않는다(문자열 해석은 각 머신 로컬, LocRes는 패키지 동봉).

## 8. 수명주기 · 소유권

- **등록**: 게임 모듈 `StartupModule`(엔진 모듈 로드 직후, 어떤 UI보다 먼저). **해제**: `ShutdownModule`에서 3건 대칭 해제.
- 에디터 리로드 유틸은 해제→재등록을 한 함수 안에서 원자적으로 수행(부분 상태 노출 금지).
- GC 무관(스트링 테이블 레지스트리는 UObject 아님). 델리게이트 없음.

## 9. 데이터드리븐 경계 (핵심원칙 2)

| 값 | 나가는 곳 | 기본값 | 비고 |
|---|---|---|---|
| 문자열 본문·번역 | `Content/StringTables/*.csv` (마스터=구글 시트) | — | 코드에는 키만 |
| 시트 매핑 | `Config/AuthoringSheets.json` | — | 스크립트 전용, 쿠킹 무관 |
| 스테이징 문화권 | `DefaultGame.ini CulturesToStage` | ko,en,ja | |
| 테이블 등록(Id/NS/경로) | **예외적으로 C++ 리터럴** | 3건 | §3-2 gather 소스-파싱 제약 — ini화 금지 근거 명세됨 |

## 10. 성능 예산 (핵심원칙 1)

- 틱 없음. 등록은 시작 시 1회(CSV 3파일 파싱, KB 단위). 적 스웜 무관(문자열은 UI 레이어 전용).
- `FText::FromStringTable` 해석은 UI 위젯 빈도(초당 수십 회 이하) — 예산 무관.

## 11. 미결정 항목 · 명세 갭 처리

**미결정**
- `Game_*.ini` 태스크 파일의 정확한 키 집합 — **C2 구현자가 엔진 `LocalizationConfigurationScript.cpp`(GenerateGatherTextConfigFile 계열)를 grep해 생성 포맷을 그대로 미러**할 것(ENGINE SOURCE FIRST). 에디터 대시보드로 1회 생성 후 커밋하는 방법도 허용(결과 ini가 동일하면 수단 무관).
- Tools 메뉴 확장 지점: 기존 `FPSRogueliteEditorModule`의 메뉴 등록 패턴을 따르되, 헬퍼 함수 위치(모듈 cpp 직속 vs 별도 파일)는 기존 Blockout/Assembler 메뉴 항목과 같은 방식을 따른다.

**갭 처리 규칙(고정)**: 명세에 없는 판단 필요 시 멈추고 "명세 갭" 보고 → C1 수정 후 재개.

## 12. 검증 기준

| # | 검사 | 통과 조건 |
|---|---|---|
| 1 | 명세 대조 | §5 선언·시그니처·CSV 헤더·JSON 스키마가 코드와 1:1 |
| 2 | 빌드 | Win64 Development Editor **Succeeded** (`-DisableUnity` 포함 1회) |
| 3 | 자동테스트 | `FPSRStringTableCsvTest` 통과: ①3 CSV 파싱 성공 ②테이블 내 Key 유일 ③en/ja 빈칸 리포트(경고, 실패 아님) ④`FText::FromStringTable("UI","Debug.LocSmoke")` 해석 = 소스 문자열 |
| 4 | gather 왕복 | `localization-gather.ps1` exit 0 + `Content/Localization/Game/{en,ja}/Game.locres` 생성 + 재실행 멱등 |
| 5 | sync 왕복 | `sync-authoring-csv.ps1` exit 0(시트 헤더 기입 후) + 헤더 오염 시트로 부정 테스트 = 기존 파일 무접촉 |
| 6 | 레드팀 게이트 | §6-6-1 (P1 잔존 시 머지 금지) |
| 7 | PIE / 사용자 스모크 | PIE 콘솔 `culture=en` → 시드 문자열 "Localization seed" / `culture=ja` → "ローカライズシード" / `culture=ko` 복귀. ja 표시가 □(두부)면 폰트 글리프 미포함 → 보드 후속 행 등록 |
| 8 | 패키징 스모크 | 개발 패키징 1회: `StringTables/*.csv` + `Localization/Game/**` 스테이징 확인, **패키지 실행에서 `culture=ja`/`culture=ko` 전환이 실제 표시로 이어짐**(ICU 프리셋 미탑재 결함은 스테이징 확인만으론 안 잡힌다 — 레드팀 P2-2) |

## 13. 레드팀 지적 원장 (C3, 2026-08-12)

**레드팀에 무엇을 줬나**: `git diff 90682a0f..c7833d77` · 이 명세 · `Docs/InternalRedTeamReview.md` 프라이머 · 리포+엔진 소스 읽기 권한. 판정: **P1 0 / P2 2 / P3 6** — P2 전건 수용·수정.

| 심각도 | 지적 (요약) | 처리 | 근거/수정 |
|---|---|---|---|
| P2-1 | 리로드 유틸 실패 감지 = 데드 코드, 깨진 CSV 리로드 시 빈 테이블로 파괴 교체 후 거짓 성공 (`Internal_LocTableFromFile`은 실패에도 무조건 등록, StringTableRegistry.cpp:198-209) | **수용·수정** | 엔진 파일워처와 동일한 in-place `ImportStrings` 경로로 재작성(검증 후 클리어 → 깨진 CSV=기존 문자열 보존, 해제 창 자체 제거). 미등록 케이스는 스크래치 테이블 검증 후 등록 |
| P2-2 | `CulturesToStage=ja` 추가에도 ICU 프리셋이 English 잔존 → 패키지에 ja/ko ICU 미탑재, culture 전환 불가 | **수용·수정** | `DefaultGame.ini InternationalizationPreset=EFIGSCJK` + §12-8 통과 조건에 패키지 내 culture 전환 명시. DefaultCulture=en 유지(제품 결정 대기) |
| P3-3 | 대시보드 Gather/Compile 클릭이 수제 `Game_*.ini` 덮어씀 — 금지 규칙 미문서 | **수용·수정** | SSOT L-4에 대시보드 실행 금지 갓차 추가 |
| P3-4 | 테이블 목록 4곳 복제 + NS 파생 암묵 결합, 위반 시 exit 0 무음 / 명세 §3-3 "중앙 수정 없음" 과장 | **수용·수정(부분)** | 커맨드렛 100%-매니페스트-미스 → 에러 승격. §3-3·SSOT L-3 문구를 정직한 7곳 목록으로 정정. CSVFiles↔테스트 목록 자동 대조는 후속(P3 잔여) |
| P3-5 | "원자적 리로드" 주석이 코드가 없는 보장(락 2회 분리) 주장 | **수용·해소** | P2-1 재작성으로 해제 창 제거 — 주석도 실제 보장으로 교체 |
| P3-6 | sync/gather 스크립트 엣지: (a) 타임아웃 없음 (b) 항목 1개 시 manifest 스키마 요동 (c) Write-Error가 exit 도달 차단 | **수용·수정** | `-TimeoutSec 30` / `ConvertTo-Json -InputObject @(...)` 강제 배열 / `Write-Warning`+exit |
| P3-7 | 스테이징 누락 동작 주석 오서술("원문 폴백" — 실제는 빈 테이블) | **수용·수정** | DefaultGame.ini 주석 정정 |
| (자체) | `.sync-manifest.json`이 UFS 스테이징에 걸려 pak 탑재(C3 패키징 실측) | **수용·수정** | provenance를 `Config/AuthoringSheets.manifest.json`으로 이동 |

**C2 명세 편차 4건(전부 수용)**: ① `ModuleInterface.h`→`ModuleManager.h`(FDefaultGameModuleImpl 선언 위치, ModuleManager.h:879) ② 커맨드렛 NS 파생 = 파일명−`ST_`(LOCTABLE 등록과 1:1) ③ gather 소스 범위 `Source/FPSRoguelite/`만(§2 비목표 이행; 패키지 gather는 Phase A에서 잔존 인라인 탐지용으로 재검토) ④ docs 파일은 선행 커밋에 이미 존재. + 구현 내부 private 헬퍼 `ImportCsvForNamespace` 추가(계약 불변, .cpp 내부 구조).
