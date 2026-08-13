# CARDCSV — 카드 CSV 저작 파이프라인 (임포터/익스포터/검증)

## 1. 메타

| 항목 | 값 |
|---|---|
| 유닛 ID / 이름 | CARDCSV / 카드 CSV 저작 파이프라인 |
| 브랜치 | `phase/card-csv-pipeline` |
| 작성 모델 | `claude-fable-5` |
| 작성일 / 최종 갱신 | 2026-08-12 |
| 상태 | `확정` |
| 관련 SSOT | `CombatWeaponCard.md` §2-3-10(신설)·§2-3-2(CardFamily v3)·§2-3-8 / `Localization.md` L-2·L-5 |
| 관련 메모리 | [[test-unity-anon-namespace-collision]] [[refactor-report-stale-premises]] |

## 2. 목표 / 비목표

**목표**
- 카드 저작이 "구글 시트(Cards/CardCatalog) → sync → `Content/Authoring/*.csv` → Tools 메뉴/커맨드렛 임포트 → `DA_Card_*` 갱신"으로 왕복된다. 새 카드 = 시트 1행(코드·에디터 저작 0).
- 기존 49 카드 uasset이 역추출→재임포트 왕복에서 **에셋 diff 0**(무회귀 증명).
- 카드 DisplayName/Description이 StringTable(`Card` 테이블, `<CardId>.DisplayName/.Description`) 참조가 되고 `ST_Card.csv`가 임포터 산출물로 생성된다(ko/en/ja).
- `CardFamily`가 `FName`이 되고, 공란 저작 시 `E1_Attr`에서 자동 파생된다.

**비목표(Non-goals)**
- 런타임 추첨/적용/복제 로직 변경 없음(`FPSRCardSubsystem` 추첨 알고리즘·서버권위 인덱스 선택 불변 — `GetCardFamilyKey` 타입 변화만).
- 카드덱 주입 구조 교체(후행 보드 행), 신규 효과 타입, DataTable 도입, DataEditor Slate 툴 개편(조회 용도 존속) 없음.
- 시트 자체의 수식/서식 저작 규칙 없음(헤더 스키마만 검증).

## 3. 제1원리 3줄 (핵심원칙 4)

1. **제1원리** — 콘텐츠 추가·밸런싱 반복 비용 최소화(§2-3 확장성 directive). 카드 1장 = CSV 1행이 저작·리뷰의 최소 인지 단위. 런타임은 v2 폴리모픽 DA 그대로 = 적500 예산·서버권위 불변식 무접촉.
2. **엔진 기본값·기존 인프라** — DataTable(flat row)은 Instanced 폴리모픽 효과를 표현 못 하므로 안 쓴다(프로젝트 DT 0건 유지). 대신 **CSV→DA 변환층(임포터)** 신설: 파서=엔진 `FCsvParser`, 에셋 생성=`IAssetTools/FAssetToolsModule`+`UPackage`(에디터 모듈 기존 의존성), 검증=기존 `IsDataValid`+`FPSRCardPoolValidator` **재사용**(포팅 아님). 임포터 헤드리스 진입 = 기존 `FPSRValidateAnchoredDataCommandlet` 패턴 미러.
3. **정합** — 로컬라이징 키·시트 동기화 규약은 LOC0(`Localization.md`)와 공유. 후행 "캐릭터별 카드덱" 행은 Cards.csv `Route`/`OwnerWeapon` 컬럼 위에서 덱 컬럼만 추가하면 된다(스키마 전방호환).

## 4. 파일 목록

| 경로 | 신규/수정 | 한 줄 설명 |
|---|---|---|
| `Content/Authoring/Cards.csv` · `CardCatalog.csv` | 신규 | 빌드 스냅샷(쿠킹/스테이징 제외 — DirectoriesToAlwaysCook/StageAsUFS에 미포함 확인만, ini 수정 없음) |
| `Source/FPSRogueliteEditor/Public/CardImport/FPSRCardCsvSchema.h` / `Private/CardImport/FPSRCardCsvSchema.cpp` | 신규 | 행 구조체 + 파서(순수 파싱·오류 수집, 에셋 무접촉) |
| `Public/CardImport/FPSRCardCsvImporter.h` / `Private/CardImport/FPSRCardCsvImporter.cpp` | 신규 | CSV→DA 임포트 코어(멱등) + ST_Card.csv 생성 |
| `Public/CardImport/FPSRCardCsvExporter.h` / `Private/CardImport/FPSRCardCsvExporter.cpp` | 신규 | 49 uasset→CSV 역추출(마이그레이션 전용) |
| `Public/CardImport/FPSRImportCardsCommandlet.h` / `Private/...cpp` | 신규 | 헤드리스 임포트+검증 게이트 |
| `Private/FPSRogueliteEditorModule.cpp` + `Public/FPSRogueliteEditorModule.h` | 수정 | Tools>FPSR 메뉴 "카드 CSV 임포트"·"카드 CSV 내보내기(마이그레이션)" 2항목 |
| `Private/Tests/FPSRCardCsvSchemaTest.cpp` · `FPSRCardCsvRoundTripTest.cpp` | 신규 | 파서 단위테스트 · export→import 멱등 automation |
| `Source/FPSRoguelite/Public/Card/FPSRCardDataAsset.h` / `Private/Card/FPSRCardDataAsset.cpp` | 수정 | `CardFamily` FGameplayTag→**FName** + IsDataValid 메시지 |
| `Private/Card/FPSRCardSubsystem.cpp` | 수정 | `GetCardFamilyKey()` FName 직독(GetTagName 제거) |
| `Config/DefaultGameplayTags.ini` | 수정 | `Card.Family.*` 태그 제거(전환 후) |
| `Config/AuthoringSheets.json` | 수정 | Cards/CardCatalog 시트 항목 추가(ID는 §5) |
| (DataEditor/Validator의 CardFamily 표시·비교 지점) | 수정 | 타입 전환 기계적 반영(거동 불변) |

## 5. 인터페이스 선언 (헤더 스케치)

```cpp
// ── Public/CardImport/FPSRCardCsvSchema.h ──────────────────────────────────
#pragma once
#include "CoreMinimal.h"
#include "Card/FPSRCardTypes.h"

/** One parsed Cards.csv row (schema = SSOT §2-3-10; effects flattened as E1..E3 column groups). */
struct FFPSRCardCsvRow
{
    FName    CardId;                    // 행 키 = 세이브 키 = 로컬라이징 키 접두. 필수·파일 내 유일
    FString  AssetName;                 // DA_Card_<Group>_<Theme> 린트 대상. 필수
    ECardGroup Group = ECardGroup::Character;
    EFPSRCardRoute Route = EFPSRCardRoute::LevelUpGlobal;
    TArray<FString> OwnerWeapons;       // 무기 루트(LevelUpWeapon/MissionClearWeaponFeature)일 때 무기 DA 에셋명 **세미콜론 리스트**
                                        // (예: "DA_Weapon_Rifle;DA_Weapon_SMG"). 현행 콘텐츠에 다중 무기 풀 동시 소속 카드 7장 존재 —
                                        // 단일 컬럼이면 마이그레이션이 소속을 붕괴시켜 무회귀 절대조건(§2-3) 위반 (C2 발견, 2026-08-13 교정).
                                        // 그 외 루트는 빈칸. CSV 컬럼명은 OwnerWeapon 유지(값만 리스트).
    float    Weight = 1.0f;
    FName    Family;                    // 공란 = E1 AttrId에서 파생(§2-3-2 v3)
    FString  DisplayName[3];            // ko,en,ja (ST_Card.csv 원천 — DA에는 FromStringTable 참조가 들어간다)
    FString  Description[3];
    struct FEffectCol { FName AttrId; FString Override; FString Tiers; };  // Tiers 예: "C:15;R:30;E:60;L:100"
    TArray<FEffectCol> Effects;         // 유효(AttrId 비공란) 컬럼군만, 최대 3
    int32    SourceRowIndex = 0;        // 오류 메시지용(1-base, 헤더=1)
};

/** One parsed CardCatalog.csv row. EffectType은 클로즈드 문자열 enum — 임포터 타입 팩토리의 키. */
struct FFPSRCardCatalogRow
{
    FName   AttrId;                     // 예: char.maxhealth / weapon.firerate / weapon.frag.dot / unlock.smg
    FName   EffectType;                 // CharGE | CharPassive | WeaponStat | WeaponBehavior | GrantWeapon (5종 1:1)
    FString Payload;                    // CharGE/CharPassive=클래스 경로, WeaponBehavior/GrantWeapon=에셋 경로, WeaponStat=EFPSRWeaponStat raw 이름
    FString DefaultOp;                  // WeaponStat 전용: EFPSRWeaponModOp raw 이름(빈칸=PercentMultiply)
    FString DefaultThisWeaponOnly;      // WeaponStat 전용: true/false(빈칸=true)
    FString ShowAsPercent;              // CharGE 전용: true/false(빈칸=false)
    int32   SourceRowIndex = 0;
};

/** 파싱 전용(에셋 무접촉). 모든 오류는 "<파일>:<행> <컬럼>: <내용>" 포맷으로 수집 — 첫 오류에서 멈추지 않는다. */
struct FFPSRCardCsvParseResult { TArray<FFPSRCardCsvRow> Cards; TArray<FFPSRCardCatalogRow> Catalog; TArray<FString> Errors; };
namespace FPSRCardCsv
{
    /** CSV 텍스트 → 구조체. 검사: 헤더 정확 일치, CardId/AttrId 유일, enum 파싱, Tiers 문법(레어도 이니셜 C/R/E/L,
     *  중복 금지, magnitude=float), E*_Attr가 카탈로그에 존재, 무기 루트인데 OwnerWeapon 공란, 효과 0개 카드. */
    bool ParseCards(const FString& CardsCsvText, const TArray<FFPSRCardCatalogRow>& Catalog, FFPSRCardCsvParseResult& InOut);
    bool ParseCatalog(const FString& CatalogCsvText, FFPSRCardCsvParseResult& InOut);
}

// ── Public/CardImport/FPSRCardCsvImporter.h ────────────────────────────────
#pragma once
#include "CoreMinimal.h"
struct FFPSRCardCsvParseResult;

struct FFPSRCardImportResult
{
    int32 CreatedCount = 0, UpdatedCount = 0, UnchangedCount = 0;   // Unchanged = 무접촉(dirty 아님) — 멱등의 관측값
    int32 RemovedMembershipCount = 0;                                // 선언적 동기화가 제거한 풀/무기 멤버십 수(레드팀 P3-⑪ — 다이얼로그 표기)
    TArray<FString> Errors;                                          // 파싱+해석+검증 게이트 오류 전부
    bool Succeeded() const { return Errors.Num() == 0; }
};
namespace FPSRCardCsvImport
{
    /** Content/Authoring/*.csv 로드→파싱→DA 생성/갱신→ST_Card.csv 재생성→검증 게이트(IsDataValid+PoolValidator).
     *  멱등 계약: 산출 상태가 이미 목표와 같으면 Modify/MarkDirty를 부르지 않는다(2회 연속 실행 = dirty 0).
     *  bSaveAssets: 커맨드렛=true(SavePackage까지), 에디터 메뉴=true(성공분 저장; 검증 실패 에셋은 dirty로 남기고 미저장). */
    FFPSRCardImportResult ImportAll(bool bSaveAssets);
}

// ── Public/CardImport/FPSRCardCsvExporter.h ────────────────────────────────
namespace FPSRCardCsvExport
{
    /** 기존 DA_Card_* 전수 → Cards.csv/CardCatalog.csv 역추출(마이그레이션 1회용).
     *  텍스트 규칙: 현행 FText가 영어면 en 컬럼 + SourceString(ko)에 "[KO-TODO] <en원문>" / 이미 한국어면 ko에 그대로.
     *  카탈로그: 카드들이 참조하는 GE/Fragment/Weapon/Passive에서 AttrId 자동 제안(명명: 소문자 도트 계층).
     *  기존 CardFamily 태그값은 Family 컬럼에 GetTagName()으로 보존(파생값과 같으면 공란으로 정규화). */
    bool ExportAll(const FString& OutCardsCsvPath, const FString& OutCatalogCsvPath, TArray<FString>& OutErrors);
}

// ── Public/CardImport/FPSRImportCardsCommandlet.h ──────────────────────────
UCLASS()
class UFPSRImportCardsCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    virtual int32 Main(const FString& Params) override;  // ImportAll(true); 오류 나열 후 개수 반환(0=성공)
};
```

**CardFamily v3 (런타임 최소 수정)**
```cpp
// FPSRCardDataAsset.h — 기존 FGameplayTag CardFamily 를 교체:
/** Cards sharing a family are mutually exclusive within a single draw. CSV 파이프라인이 저작 소스이며 공란 시
 *  임포터가 E1 AttrId에서 파생해 기록한다(§2-3-2 v3). FName인 이유: 자동 파생 값과 태그 ini 정적 등록은 마찰. */
UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Card")
FName CardFamily;
// FPSRCardSubsystem.cpp GetCardFamilyKey(): Card->CardFamily.GetTagName() → Card->CardFamily 직독. 그 외 로직 불변.
```

**임포터 상세 계약(구현 지침)**
- 에셋 경로 = `/Game/Cards/` 하위 **기존 에셋 우선**: AssetRegistry에서 `CardId` 일치 DA 탐색(1순위) → 없으면 `AssetName` 일치(2순위, CardId를 새로 기록) → 둘 다 없으면 `/Game/Cards/Imported/<AssetName>` 신규 생성(기존 폴더 구조는 옮기지 않는다 — 리네임/이동 금지).
- Instanced 효과 재사용: 인덱스 i의 기존 `Effects[i]`가 목표 클래스와 같으면 in-place 프로퍼티 갱신, 다르면 `Rename(트래시)`→`NewObject`(Outer=카드 DA, 이름 `Effect_<i>`). 배열 길이 축소 시 잉여 효과 트래시.
- Tiers → `RarityTiers` 배열(레어도 오름차순 정렬 기록 — 순서까지 결정적이어야 diff 0). `OfferRarities`는 기존 `RefreshOfferRarities()` 경로(PostEditChangeProperty) 재사용.
- DisplayName/Description: `FText::FromStringTable(TEXT("Card"), FString::Printf(TEXT("%s.DisplayName"), *CardId.ToString()))` — **비교도 테이블 참조 여부+키로**(FText 값 비교 금지; 멱등 판정용 헬퍼 필요). **3개 언어 컬럼 전부 공란이면 `FText::GetEmpty()` 기록**(레드팀 P2-① — ST_Card.csv 키 생성 규칙과 1:1: 키 없는 참조 = `<MISSING STRING TABLE ENTRY>` UI 노출 금지).
- `ST_Card.csv` 재생성: Cards.csv의 ko/en/ja 컬럼 → `Key,SourceString,en,ja` 전체 재작성(`Debug.LocSmoke` 시드 행 보존). LOC0 규약(따옴표 최소·BOM 없음).
- 풀 멤버십: Route별 목표 집합을 CSV에서 구성 → `UFPSRCardPoolDataAsset.Cards/WeaponUnlockCards`·무기 DA `WeaponCards/UnlockableFeatures`를 **선언적으로 일치**시킨다(추가+제거 모두; CSV에 없는 기존 멤버 제거는 경고 로그 동반). OwnerWeapon 해석 = AssetRegistry 에셋명 매칭. **무기 루트 카드의 목표 집합은 OwnerWeapons 리스트의 전 무기에 대한 합집합**(다중 소속 보존 — 무회귀).
- 검증 게이트: 접촉(생성/갱신) 에셋 전부 + 멤버십이 바뀐 풀/무기 DA에 `IsDataValid`, 이어서 `FPSRCardPoolValidator` 계열 크로스체크 호출. 오류 = Errors에 수집, `bSaveAssets`여도 해당 에셋 미저장.

**AuthoringSheets.json 추가 항목**: `Cards` = `1ewnshKoJpntr01alLVwxOkAynnOb_TDR8Tp89QfMZuQ` / `CardCatalog` = `12v_vWsuwnPe8mw0nE1doAR5IHy0qnnzp77hl8-V5vT4` (gid 0, target `Content/Authoring/<이름>.csv`, expectedHeader = §2-3-10 스키마).

## 6. 함수별 계약

| 함수 | 권위 | 호출자 | 전제조건 | 실패 시 동작 |
|---|---|---|---|---|
| `FPSRCardCsv::Parse*` | 에디터 | Importer/테스트 | 없음(순수) | Errors 수집, false — 에셋 무접촉 |
| `FPSRCardCsvImport::ImportAll` | 에디터 | 메뉴/커맨드렛 | Authoring CSV 존재 | 오류 에셋 미저장·dirty 유지, Result.Errors |
| `FPSRCardCsvExport::ExportAll` | 에디터 | 메뉴(마이그레이션) | — | false + Errors, 파일 미생성 |
| `UFPSRImportCardsCommandlet::Main` | 커맨드렛 | validate 파이프라인 | — | 비0 반환(오류 수) |

## 7. 복제표 (§6-3)

해당 없음 — 전부 에디터 전용. 런타임 변경은 `CardFamily` 타입(비복제 프로퍼티, 서버 로컬 추첨에서만 사용)뿐.

## 8. 수명주기 · 소유권

- 임포터가 만드는 UObject의 Outer = 카드 DA 패키지(효과는 DA가 Outer). 트래시 처리 = `Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors)` 후 참조 소거 — 재임포트 반복에도 고아 서브오브젝트 잔존 금지(라운드트립 테스트가 감지).
- 메뉴 액션은 `FScopedSlowTask` 진행 표시. 커맨드렛은 헤드리스(슬레이트 무접촉).

## 9. 데이터드리븐 경계 (핵심원칙 2)

| 값 | 나가는 곳 | 비고 |
|---|---|---|
| 카드 정의·수치·텍스트 | Cards.csv(시트) | DA=파생물 |
| 속성→클래스/에셋 매핑 | CardCatalog.csv(시트) | 에셋 경로가 C++이 아니라 카탈로그 데이터에 산다(§6-2 부합) |
| EffectType→서브클래스 팩토리 | **C++ map 1곳**(Importer) | 클로즈드 5종 — 새 타입 = 서브클래스+맵 1행(OCP) |

## 10. 성능 예산 (핵심원칙 1)

에디터 전용 — 런타임 예산 무접촉. 임포트 전수(49+α)는 초 단위면 충분(SlowTask). 런타임 유일 변화 = FName 비교(태그 비교와 동급).

## 11. 미결정 항목 · 명세 갭 처리

**미결정**
- 역추출 시 기존 에셋 폴더 구조(`Character/Data/`, `Weapons/Card/` 등)는 그대로 두고 CSV의 AssetName만 기록 — 신규 카드만 `Imported/`에 생성. 폴더 재편은 별도 결정.
- `E*_Override` 파싱 키 집합 = 카탈로그 Default* 3종과 동일(`Op=`, `ThisWeaponOnly=`, `ShowAsPercent=`). 그 외 키 = 오류.

**C2 갭 판정 기록 (2026-08-13)**
- 카드 uasset 실수 = **30**(스펙 초안의 "49"는 Content/Cards/ 전체 uasset 수 — GE/풀/프래그먼트 포함 집계의 착오). 30이 맞다.
- 오펀 카드(어느 풀에도 미소속) 폴백 라우팅 = 카드 효과들의 `GetEditorEligibleRoutes()` 교집합에서 유도(C2 구현 채택 — LevelUpGlobal 고정은 PoolValidator가 반증). **수용.**
- `OwnerWeapon` 단일 → `OwnerWeapons` 리스트 확장(위 §5) — C2가 발견한 다중 소속 붕괴의 교정. B-7 커밋으로 구현.

**갭 처리 규칙(고정)**: 명세에 없는 판단 필요 시 멈추고 "명세 갭" 보고 → C1 수정 후 재개.

## 12. 검증 기준

| # | 검사 | 통과 조건 |
|---|---|---|
| 1 | 명세 대조 | §5 선언·계약 1:1 |
| 2 | 빌드 | Win64 Development Editor Succeeded (`-DisableUnity` 1회 포함) |
| 3 | 파서 단위테스트 | 정상/오류(헤더·중복키·Tiers 문법·카탈로그 미존재 Attr·OwnerWeapon 공란)/멀티이펙트 케이스 |
| 4 | **역추출→재임포트 왕복** | 49 에셋 대상: export→import 후 **dirty 패키지 0** + 2회 연속 import dirty 0 (RoundTripTest automation) |
| 5 | 기존 검증기 | `validate-data.ps1` 그린(임포트 산출물 IsDataValid+PoolValidator) |
| 6 | 레드팀 게이트 | §6-6-1 — P1 잔존 시 머지 금지 |
| 7 | PIE / 사용자 스모크 | 카드 추첨 3장 표시(멀티이펙트 포함)·선택 적용·툴팁 자동설명 ko/en/ja·무기 해금 오퍼·family 상호배제(같은 속성 2장 동시 제시 없음) |
| 8 | 시트 왕복 | Cards/CardCatalog 시트 시딩 후 `sync-authoring-csv.ps1` → 임포트 → diff 0 |

## 13. 레드팀 지적 원장 (C3, 2026-08-13)

**레드팀에 무엇을 줬나**: `git diff main..HEAD`(9커밋·57파일) · 이 명세 · SSOT §2-3-10/§2-3-2 · `Docs/InternalRedTeamReview.md` 프라이머 · 리포+엔진 소스. 판정: **P1 0 / P2 7 / P3 9**.

| 심각도 | 지적 (요약) | 처리 | 근거/수정 |
|---|---|---|---|
| P2-① | Description 공란 카드 11장 → 미존재 키 참조 = UI `<MISSING STRING TABLE ENTRY>` 노출(StringTableCore.cpp:84-88 확정) | **수용·수정** | 3언어 전부 공란 = `FText::GetEmpty()`(§5 개정) — ST_Card 키 생성 규칙과 1:1 |
| P2-② | 해석 오류 카드가 혼합 상태로 저장(옛 클래스+새 티어) — §6 "오류 에셋 미저장" 위반 | **수용·수정** | 행 오류 카드 = 저장·멤버십 제외 |
| P2-③ | 검증 실패 신규 카드 미저장인데 풀은 그 참조를 담아 저장 → 온디스크 dangling | **수용·수정** | 실패 카드 멤버십 제거 후 재동기화 |
| P2-④ | SavePackages 실패를 exit 0으로 삼킴(반환값 폐기) | **수용·수정** | 반환 검사 → Errors 편입 |
| P2-⑤ | AssetName 중복 시 뒷행이 앞행 카드 무음 하이재킹(CardId 덮어씀 — 세이브 키 파괴) | **수용·수정** | 파서 AssetName 유일성 검사 |
| P2-⑥ | `Effect_<i>` NewObject가 동명·타클래스 잔존 서브오브젝트와 충돌 시 엔진 Fatal(UObjectGlobals.cpp:3656) | **수용·수정** | StaticFindObject 선(先)트래시 |
| P2-⑦ | Family 자동 파생이 FireRate·RecoilVertical의 AllWeapon/ThisWeapon 쌍에 **신규 상호배제** 도입(main 대비 거동 변화) | **기각(의도 판정)** | SSOT §2-3-2 v3 = "같은 속성 = 한 제시 1장"이 사용자 확정 기본안(보드 로그 2026-08-12 피드백) — 자동 파생의 목적 그 자체. 영향 쌍 2건을 여기 명시, 사용자 최종 확인 대기(뒤집으려면 해당 카드 Family 명시 옵트아웃 = 시트 셀 2개) |
| P3 ⑦~⑪ | Weight≤0 경고 / 비무기 루트+OwnerWeapon 경고 / CardId 중복 타이브레이크 / 라운드트립 주석·dirty 계수 / 제거 건수 다이얼로그 집계 | **수용·수정(경량 동승)** | 수정 배치에 포함 |
| P3 잔여 | 익스포터 메뉴 상시 노출 가드 / 익스포터 조기 반환 시 경고 미도달 / stale OfferRarities 미치유 / **머지 시 타 브랜치 미재저장 uasset의 CardFamily 태그→FName 무음 드롭**(PropertyName.cpp:88-108) | **후속(보류)** | 마지막 항목은 머지 체크리스트에 반영: 머지 직후 validate-data 전수 + 카드 재임포트 1회로 그물질 |
| 범위 밖 | FireRate_ThisWeapon이 Knife(근접) 풀에도 소속 — 기획 의도 확인 감 | 사용자 확인 항목 | 기존 콘텐츠 유래(파이프라인 무관) |
