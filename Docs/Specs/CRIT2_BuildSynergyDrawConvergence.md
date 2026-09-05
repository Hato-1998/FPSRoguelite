# CRIT2 — 빌드 시너지 추첨 수렴 + 획득 카드 원장

## 1. 메타

| 항목 | 값 |
|---|---|
| 유닛 ID / 이름 | CRIT2 / 빌드 시너지 추첨 수렴 + 획득 카드 원장 |
| 브랜치 | **`main` 직접** (트렁크 기반, `Workflow.md` §6-7) |
| 작성 모델 | `claude-opus-5` (§6-5-2 — C1 설계 = Opus) |
| 작성일 / 최종 갱신 | 2026-09-05 (**rev3** — G1 2회차 반려 반영 + 사용자 결정 C) |
| 상태 | `확정` (G1 3회차 = **사용자 결정으로 생략** — §13 참조) |
| 관련 SSOT | `CombatWeaponCard.md` §2-3-1~§2-3-4·§2-3-9·§2-3-10 / `RunFlow.md` §2-2 |
| 선행 | CRIT1(`b1c5d615`) — 수렴을 실측할 빌드 1개가 이걸로 생겼다 |
| 관련 메모리 | [[card-pool-routing]] [[reason-in-multiplayer-terms]] [[production-structure-first]] [[leave-fine-tuning-to-user]] |

### 🔴 G1 1회차 이후 바뀐 요구 2건 (G2 는 이 둘이 게이트를 안 거쳤음을 알아야 한다)

1. **시너지 적용 범위 축소 (사용자 결정)** — 스탯 카드(레벨업 풀)는 **균등**하게 두고, **기능/해금 카드(미션 풀)에만** 시너지를 적용한다. → `GetEffectiveWeight`(레벨업 경로)는 **손대지 않는다.** G1 P2-1(가중치 공식 2중화)이 **설계 축소로 해소**된다: 시너지가 함수 하나에만 산다.
2. **획득 카드 원장이 복제된다 (사용자 요구)** — Tab 키로 **모든 플레이어**의 획득 카드·스탯을 보는 정보창이 예정돼 있다. rev1 의 "신규 복제 0 / 상태창 = 비목표"는 성립하지 않는다. 원장을 비복제로 만들었다가 나중에 뒤집는 것은 「임시·미루기 구조 금지」(핵심원칙 4) 위반이므로 **처음부터 복제형**으로 만든다. 정보창 위젯 자체는 별도 유닛.

---

## 2. 목표 / 비목표

### 목표

**한 빌드를 고르기 시작하면 그 빌드가 완성될 확률이 실제로 올라간다.** 지금은 안 올라간다 — 카드 총량이 늘수록 빌드 완성 확률은 오히려 떨어져, 콘텐츠를 추가할수록 재미가 옅어진다.

1. 카드가 **어느 빌드에 속하는지** 데이터로 선언된다(`BuildTags`, 한 카드가 여러 빌드에 속할 수 있다).
2. **플레이어가 무엇을 골랐는지 서버가 기억하고, 그것이 전원에게 복제된다** — 지금은 어디에도 없다(§3 실측 1). 추첨의 입력이자 정보창의 데이터 원천.
3. 🔴 **미션/해금 풀에 가중 추첨이 생긴다** — 현재 이 풀은 **카드 선택이 균등 Fisher-Yates 셔플**이라 `Card->Weight` 가 무시된다(`FPSRCardSubsystem.cpp:502-508`). ⚠️ **Luck 은 무시되지 않는다**(rev1·2 오기, G1 P3-7) — 선택 후 `BuildSingleDraw` 가 레어도를 굴릴 때 쓴다(`:350-357`). 즉 균등인 것은 *카드 선택*뿐이다. CRIT1 의 치명타 5장이 사는 곳이 여기라, 여기를 안 고치면 목적이 달성되지 않는다.
4. 그 풀에서 **이미 고른 빌드의 기능 카드가 더 자주 제시된다.**
5. 수렴 세기·태그 어휘가 **콘텐츠 값**이다(`UFPSRCardPoolDataAsset`). 코드 상수 금지.

### 비목표

- **레벨업(스탯) 풀의 추첨 거동 변경.** 사용자 결정 — 스탯 카드는 균등하게 뜬다. `GetEffectiveWeight` 무접촉.
- **Tab 정보창 위젯.** 이 유닛은 **데이터(복제되는 원장)까지**다. 위젯·입력 바인딩·레이아웃은 별도 유닛.
- **배제 규칙 v4 변경**(family × 굴린 레어도 × TargetWeapon). 시너지는 *가중치* 축, 배제는 *중복* 축 — 직교하며 건드리지 않는다.
- **전체 카드 `BuildTags` 저작.** 스키마 + 알고리즘 + **치명타 빌드 1개분 저작**까지. 나머지 빌드 태깅은 콘텐츠 작업.
- **피티(pity)·확정 보장.** 가중치로 부족하다는 실측이 나오면 그때.
- **빌드 간 상충**(A 를 고르면 B 가 덜 나옴). 억제는 수렴보다 훨씬 거칠다 — 수렴만으로 충분한지 먼저 본다.
- **미션 풀의 레어도 굴림 순서 변경.** 현행 "선택 후 굴림"을 보존한다(§6, G1 P2-2).

---

## 3. C0 조사 실측 (설계 근거 — 추측 아님)

| # | 실측 | 근거 |
|---|---|---|
| 1 | **플레이어가 고른 카드 이력이 어디에도 없다** | `TakenCards|PickedCards|AcquiredCards|OwnedCards|AppliedCards|CardHistory` 전수 grep 0건. `ApplyCard`(`:236`)는 효과만 적용하고 기록하지 않는다 |
| 2 | 레벨업 풀 가중치는 함수 하나 | `GetEffectiveWeight`(`:539-553`) = `Card->Weight × max(RarityBase + Luck × LuckPerRarity, 0)` |
| 3 | 🔴 **미션/해금 풀은 가중치를 쓰지 않는다** | `DrawWeaponUnlockOffer`(`:386`)가 후보를 모은 뒤 Fisher-Yates(`:502-508`)로 섞고 앞에서 `Count` 개. `GetEffectiveWeight` 미호출 |
| 4 | 미션 풀은 **선택 후** 레어도를 굴린다 | `BuildSingleDraw`(`:512`) |
| 5 | 레벨업 풀엔 비복원 가중 추출이 이미 있다 | `:158-232`. 선택 후보 자신은 술어와 무관하게 항상 제거(`:208-209`) |
| 6 | 추첨 튜닝은 카드풀 DA 에 산다 | `FPSRCardPoolDataAsset.h:26-52` |
| 7 | 카드 에셋 포인터 복제는 검증된 경로다 | `FFPSRCardDraw.Card`(`TObjectPtr<UFPSRCardDataAsset>`)가 `ClientPresentCards` RPC 로 이미 나간다 |
| 8 | 런 리셋 훅 = `ResetRunState` | `Private/Core/FPSRPlayerState.cpp:432`, `CardGrantedAbilityHandles` 정리(`:487-491`) 옆 |
| 9 | `CopyProperties` 는 런 진행 필드를 **의도적으로 안 옮긴다** | `:519` 주석 원문 — "Run-progression fields are deliberately NOT copied — they are reset on lobby entry regardless" |
| 10 | Cards.csv 헤더 리터럴은 **4곳** | `FPSRCardCsvSchema.cpp:22` · `FPSRCardCsvExporter.cpp:630` · `FPSRCardCsvSchemaTest.cpp:12` · `Config/AuthoringSheets.json:10` |

> 실측 3 이 이 유닛의 크기를 정한다. "가중치에 계수 곱하기"로 끝날 줄 알았는데 **정작 문제의 풀에는 곱할 가중치가 없다.**
> 실측 9 는 rev1 의 서술("리셋을 안 부르면 다음 런이 이전 런 빌드로 수렴한다")이 **틀렸음**을 보여준다 — 트래블은 PS 를 새로 스폰하고 `CopyProperties` 로만 넘긴다. 리셋은 여전히 옳지만(같은 PS 재진입·디버그 경로) 그 인과는 정정한다.

---

## 4. 제1원리 3줄 (핵심원칙 4)

1. **제1원리** — 뱀서류 리텐션은 "이번 런은 무슨 빌드였나"가 남는 데서 온다(§2-3-9 가 예약해 둔 설계). 추첨이 수렴하지 않으면 콘텐츠를 늘릴수록 빌드가 흐려진다. 비용 축: 이 로직은 **프리즈 때 플레이어당 1회** 도는 냉경로다 — 적 200~300 예산과 무관하다.
2. **엔진 기본값·기존 인프라와의 관계** — 엔진을 덮지 않는다(추첨은 순수 프로젝트 로직). 기존 인프라 재사용: **레벨업 풀이 이미 가진 비복원 가중 추출을 공유 헬퍼로 뽑아 미션 풀이 쓴다**(두 번째 복사본 금지 — CRIT1 이 치명타 굴림 5중 복붙에서 배운 그대로). 튜닝은 풀 DA 의 기존 관례 옆. 원장 복제는 **이미 카드 에셋 포인터를 실어 나르는 검증된 경로**(실측 7)를 따른다.
3. **정합** — 4인 협동: 원장이 PlayerState 별이라 플레이어마다 자기 빌드로 수렴하고, **전원에게 복제되므로 Tab 정보창이 남의 빌드도 읽는다**. 확장 축: 새 빌드 = 시트 태그 문자열 + 풀 DA 어휘 1항목, 코드 0.

---

## 5. 파일 목록

### 런타임 코드

| 경로 | 신규/수정 | 한 줄 설명 |
|---|---|---|
| `Public/Card/FPSRCardTypes.h` | 수정 | `FFPSRAcquiredCard` USTRUCT 신설(복제 원장 원소) |
| `Public/Card/FPSRCardDataAsset.h` / `Private/…cpp` | 수정 | `TArray<FName> BuildTags` + `IsDataValid` 린트(빈 태그·중복 태그) |
| `Public/Card/FPSRCardPoolDataAsset.h` | 수정 | `SynergyBonusPerCard` · `SynergyMaxStacks` · `BuildTagVocabulary` |
| `Public/Core/FPSRPlayerState.h` / `Private/…cpp` | 수정 | 복제 원장 `AcquiredCards` + 추가/조회/리셋. 리셋은 `ResetRunState`(`:432`) 안, **`CopyProperties` 에는 넣지 않는다** |
| `Public/Card/FPSRCardSubsystem.h` / `Private/…cpp` | 수정 | `ComputeSynergyMultiplier`(public static, 순수) · `GetUnlockDrawWeight` · 비복원 가중 추출 헬퍼 추출 · `DrawWeaponUnlockOffer` 전환 · `ApplyCard` 에서 원장 기록 |
| `Private/Tests/FPSRCardSynergyTest.cpp` | **신규** | 순수 산식 자동화(§12-3) |

### 에디터 / 저작 파이프라인 (G1 P2-3 — 헤더 리터럴은 4곳이다)

| 경로 | 수정 내용 |
|---|---|
| `Private/CardImport/FPSRCardCsvSchema.{h,cpp}` | 헤더 리터럴(`:22`) + `BuildTags` 파싱(세미콜론) + **고정 인덱스 전수 시프트**: `EffectBaseColumn` 13→14(`:341`) **그리고 DisplayName/Description 의 7..12 → 8..13**(`:333-338`) |
| ⚠️ 위 시프트가 이 유닛 최대 위험 | `GetCell` 은 범위 밖을 **빈 문자열로 돌려주고**(`:11-20`) 행 폭 검사가 없다. 안 밀면 **실패 없이 조용히** ko←BuildTags·en←ko·ja←en 으로 36장이 한 칸씩 어긋난다. `Schema.Normal` 은 DisplayName 을 단언하지 않아 **자동화가 못 잡는다**(G1 P2-3) |
| `Private/CardImport/FPSRCardCsvImporter.cpp` | 행 → DA `BuildTags` 주입 |
| `Private/CardImport/FPSRCardCsvExporter.cpp` | 헤더 리터럴(`:630`) + 역추출(**저작 순서 유지 — 정렬하지 않는다**, §11-P3) |
| `Private/Tests/FPSRCardCsvSchemaTest.cpp` | 헤더 픽스처(`:12`) + BadHeader 픽스처(`:119`, 마지막 칸을 떼는 방식이라 위치에 민감) + **데이터 행 전수**(`:39`·`:120`·`:148-149`·`:187`·`:231`·`:258`·`:266`·`:274`·`:289`)에 셀 1개씩 + **DisplayName 단언 추가**(위 무음 시프트를 잡는 유일한 가드) |
| `Private/Validation/FPSRCardPoolValidator.cpp` | `BuildTags` ∉ `BuildTagVocabulary` → 에러(안 A). **순회 집합 = AssetRegistry 전 카드 스캔**(`ValidateCrossPoolChecks:24-28` 의 `AllCardAssets` 재사용) — 값싼 검사가 횸는 `Pool->Cards/WeaponUnlockCards`(`:78-80`·`:98`)에만 붙이면 **태깅 대상 7장 중 5장(무기 `UnlockableFeatures`)이 검사 0** 이 된다(G1 P2-5). Save 유즈케이스 제외 |
| `Config/AuthoringSheets.json` | `Cards.expectedHeader`(`:10`)에 `BuildTags` |

### 문서 (G1 P2-7 — SSOT 먼저)

| 경로 | 수정 내용 |
|---|---|
| `Docs/SSOT/CombatWeaponCard.md` | §2-3-3(미션 풀 균등→가중) · §2-3-4(시너지 가중) · §2-3-9(예약돼 있던 빌드 시너지의 1차 구현) · §2-3-10(스키마에 `BuildTags`) |
| `Docs/AuthoringSheetWriteback.md` | "22개 컬럼" → 23 |

### 콘텐츠

| 대상 | 내용 |
|---|---|
| `Content/Authoring/Cards.csv` | **헤더 마이그레이션이 먼저**(G1 P2-4): `authoring_sheet.py read_csv` 는 헤더 ≠ `expectedHeader` 면 `die`(`:68-69`) 하고 **컴럼 추가 명령이 없다**. 순서 = (1) 코드/설정 4곳 수정 (2) **CSV 파일 자체를 스크립트로 마이그레이션**(헤더와 전 데이터 행의 7번째 위치에 빈 셀 삽입 — 익스포터 재생성은 행 정렬·정규화 부작용이 있어 쓰지 않는다) (3) `apply` (4) `push` |
| `Content/Authoring/changesets/…-crit2-buildtags.json` | 저작 변경셋 |
| `DA_Character_CardPool` | 시너지 2값 + 태그 어휘(사용자) |
| 구글 시트 `Cards` | 컬럼 1개 — **writeback 설정이 없으면 사용자가 수동**(§11-4) |

---

## 6. 인터페이스 선언 (헤더 스케치)

```cpp
// ── Public/Card/FPSRCardTypes.h ─────────────────────────────────────────────
/**
 * 이 플레이어가 이번 런에 획득한 카드 1장. **복제된다** — Tab 정보창이 남의 빌드까지 보여주기 때문이다
 * (그래서 서버 전용으로 두지 않았다. 나중에 복제로 바꾸는 것은 임시 구조 금지에 걸린다).
 * 획득 순서 = 배열 인덱스. 같은 카드를 두 번 고르면 원소가 둘 생긴다(스택형 카드의 투자도 투자다).
 */
USTRUCT(BlueprintType)
struct FFPSRAcquiredCard
{
    GENERATED_BODY()

    /** 획득한 카드. 카드 DA 포인터 복제는 이미 ClientPresentCards 가 쓰는 검증된 경로다(C0 실측 7). */
    UPROPERTY(BlueprintReadOnly, Category = "Card")
    TObjectPtr<UFPSRCardDataAsset> Card = nullptr;

    /** 굴린 레어도 — 정보창이 "치명타 확률 (에픽)"처럼 보여주려면 필요하다. */
    UPROPERTY(BlueprintReadOnly, Category = "Card")
    ECardRarity Rarity = ECardRarity::Common;

    /** 이 카드가 적용된 무기(캐릭터·전체무기 오퍼는 null). **나중에 넣을 수 없다** —
     *  복제 struct 를 나중에 바꾸는 것은 이 유닛이 애초에 복제형을 택한 이유(임시 구조 금지)와 정면 충돌한다(G1 P2-6).
     *  §2-4-1 출처 풀 라벨(2026-08-13 확정)이 카드 표시에 `TargetWeapon->DisplayName` 을 쓰므로,
     *  정보창이 "연사 속도(라이플, 레어)" 를 그리려면 지금 있어야 한다.
     *  ⚠️ `FFPSRCardDraw` 를 그대로 저장하는 대안(G1 제안)은 기각했다 — 그쪽은 *오퍼* 구조라
     *  오퍼 전용 필드가 나중에 붙으면 원장이 같이 커지고, 이름이 "Draw"라 의미도 어긋난다. */
    UPROPERTY(BlueprintReadOnly, Category = "Card")
    TObjectPtr<UFPSRWeaponDataAsset> TargetWeapon = nullptr;
};


// ── Public/Core/FPSRPlayerState.h ───────────────────────────────────────────
public:
    /** 서버: 카드 1장을 **성공적으로 적용한 뒤** 원장에 남긴다(§7 증가 시점 계약).
     *  원장은 **스탯·기능을 가리지 않고 전부** 담는다 — 정보창이 전부를 보여줘야 하기 때문이다.
     *  가중치에 썰지 여부는 `BuildTagCountMap` 이 걸러낸다. */
    void RecordAcquiredCard(UFPSRCardDataAsset* Card, ECardRarity Rarity);

    /** 원장 읽기 — 추첨 가중치의 입력이자 정보창의 데이터 원천. */
    const TArray<FFPSRAcquiredCard>& GetAcquiredCards() const { return AcquiredCards; }

    /** 원장에서 빌드 태그별 개수를 만든다. **별도 상태를 두지 않는 이유** = 원장이 단일 진실이면
     *  리셋 지점도 하나뿐이고 둘이 어긋날 수 없다. 카드 수십 장 × 태그 한두 개라 추첨 1회당 1번 만들면 충분하다. */
    /** **기능 카드만 센다**(사용자 결정 2026-09-06). 판정 = `GetCardBehaviorFragment(Card) != nullptr`.
     *  스탯 카드가 `BuildTags` 를 달고 있어도 여기서 제외된다 — 그 태그의 소비자는 정보창뿐이다(§11-2). */
    void BuildTagCountMap(TMap<FName, int32>& OutCounts) const;

private:
    /** **복제**(COND_None — 모든 클라가 모든 플레이어의 원장을 본다. Tab 정보창의 요구).
     *  Push Model: RecordAcquiredCard / ResetRunState 에서 MARK_PROPERTY_DIRTY. */
    UPROPERTY(ReplicatedUsing = OnRep_AcquiredCards)
    TArray<FFPSRAcquiredCard> AcquiredCards;

    UFUNCTION()
    void OnRep_AcquiredCards();

public:
    /** 원장이 바뀌었다 — 정보창 위젯이 구독한다(별도 유닛). **지금 넣는 이유** = 이 PS 의
     *  복제 런 상태가 전부 `ReplicatedUsing` + 델리게이트 패턴이고(3줄), 나중에 붙이면 복제
     *  인터페이스를 다시 바꾸게 된다(G1 P3-3).
     *  주의: **리슨 호스트는 OnRep 을 받지 못한다** — 권위 경로(RecordAcquiredCard/ResetRunState)에서도
     *  직접 브로드캐스해야 한다. 이 리포가 이미 그렇게 하고 있다(`FPSRPlayerState.cpp:399`·`:423`) —
     *  반쪽만 붙이면 호스트에서만 조용히 안 된다([[event-halves-authority-vs-client]]). */
    DECLARE_MULTICAST_DELEGATE(FFPSROnAcquiredCardsChanged);
    FFPSROnAcquiredCardsChanged OnAcquiredCardsChanged;

private:


// ── Public/Card/FPSRCardSubsystem.h ─────────────────────────────────────────
public:
    /** 시너지 계수의 **순수 산식**. 월드도 액터도 서브시스템 상태도 만지지 않으므로 헤드리스 단위테스트가 가능하다
     *  (G1 P1-1 — 리포 전례: AFPSRPlayerState::IsTopologyAckSatisfied, FPSRCombat::RollCrit).
     *  = 1 + BonusPerCard × min(보유수, MaxStacks).
     *  카드가 태그를 여러 개 달았으면 **가장 많이 투자한 태그 하나**만 본다(합산 아님) — 합산하면 저작이
     *  "태그를 많이 달수록 유리"로 왜곡된다. 태그 없음/보유 0 → 1.0(현행 거동). */
    static float ComputeSynergyMultiplier(const TArray<FName>& CardTags, const TMap<FName, int32>& TagCounts,
                                          float BonusPerCard, int32 MaxStacks);

private:
    /** 🔴 **시너지가 곱해지는 유일한 지점.** 미션/해금 풀 전용 — 레벨업(스탯) 풀은 사용자 결정에 따라
     *  균등하게 두므로 GetEffectiveWeight 는 이 함수를 부르지 않고, 시너지 인자를 갖지도 않는다.
     *  새 시너지 규칙은 반드시 여기 하나에만 들어간다(두 곳에 나뉘면 한 풀에서만 조용히 안 돈다). */
    float GetUnlockDrawWeight(const UFPSRCardDataAsset* Card, const TMap<FName, int32>& TagCounts) const;

    /** 🔁 레벨업 풀에만 있던 비복원 가중 추출을 **공유 헬퍼로 추출**한다. 미션 풀이 균등 셔플 대신 이걸 쓴다.
     *  현행 계약 승계: ① 매 선택마다 선택된 후보 자신은 **술어와 무관하게 항상 제거**(`:208-209`)
     *  ② 술어는 (Selected, Candidate) 순서로 받고 true 면 Candidate 도 제거 ③ 총 가중치 ≤ 0 이면 중단. */
    /** ⚠️ **public static** 이다(G1 P2-7) — §12-6 의 미션 풀 등가 판정을 자동화가 부를 수 있어야 한다.
     *  private 으로 두면 1회차 P1-1 과 같은 병(검증 계획이 인터페이스로 실행 불가)이 된다.
     *
     *  **그룹 비중 보존 2단 추출 (사용자 결정 C)** — `GroupIds` 가 비어 있으면 종래의 단순 가중 추출
     *  (레벨업 풀 경로, 거동 불변). 비어 있지 않으면 매 추출을 두 단계로 나눈다:
     *    1단계 — 남은 그룹을 **`BaselineWeights` 합**에 비례해 고른다(= 시너지가 없었다면 가졌을 비중).
     *    2단계 — 그 그룹 안에서 **`InOutWeights`**(시너지 포함)에 비례해 고른다.
     *  이러면 시너지가 **그룹 안에서만 재분배**되고 새 무기 그룹의 몰 비중은 매 추출에서 정확히 보존된다
     *  (정규화 방식은 첫 추출에서만 근사적이라 버렸다).
     *  모든 시너지가 1 이면 2단 추출은 종래 균등 추출과 **분포가 같다**(§12-6 회귀 기준).
     *
     *  현행 계약 승계(`:158-232` 실측): (1) `Reserve(Min(Max(Count,0), Num))` — `Max(0)` 는 장식이 아니다.
     *  `FPSR.DrawCards -1` 이 `TArray::Reserve` 에 음수를 넘겨 프로세스를 죽인다 (2) 선택된 후보 자신은
     *  **술어와 무관하게 항상 제거**(`:208-209`) (3) 술어는 (Selected, Candidate) 순서, true 면 Candidate 도 제거
     *  (4) 총 가중치 <= 0 이면 중단. */
    static void WeightedSampleWithoutReplacement(
        TArray<FFPSRCardDraw>& InOutCandidates, TArray<float>& InOutWeights,
        TArray<float>& InOutBaselineWeights, TArray<int32>& InOutGroupIds, int32 Count,
        TFunctionRef<bool(const FFPSRCardDraw& Selected, const FFPSRCardDraw& Candidate)> ExclusionPredicate,
        TArray<FFPSRCardDraw>& OutPicked);
```

**`GetEffectiveWeight`(레벨업) — 변경 없음.** 시그니처·본문 그대로 둔다. `BuildSingleDraw` 의 호출부 2곳(`:363`, `:372`)도 무접촉.

**`DrawWeaponUnlockOffer` 변경**: 후보 수집은 현행 그대로(3정 캡·슬롯 캡·스택 상한 판정 불변, `(카드,무기)` 디득 `:484-499` 포함) →
그룹 라벨을 붙인다(**A = 새 무기**(`Pool->WeaponUnlockCards` 출신) / **B = 기능 카드**(무기 `UnlockableFeatures` 출신)) →
`WeightedSampleWithoutReplacement`(그룹 비중 보존 2단) → 선택된 카드에 `BuildSingleDraw` 로 레어도를 굴린다
(**현행 순서 보존**, G1 P2-2) → `Draw.TargetWeapon = CandidateWeapons[i]` 로 이월한다(현행 `:515`, G1 P3-6).

- 가중치: `BaselineWeights[i]` = `Card->Weight`(시너지 제외) · `Weights[i]` = `GetUnlockDrawWeight`(= `Card->Weight` x 시너지).
  A 그룹은 시너지가 없으므로 둘이 같다.
- 헬퍼에 넘기는 `FFPSRCardDraw::Rarity` 는 이 시점에 **아직 굴리지 않은 기본값**이며 술어가 그것을 읽지 않는다.
- **배제 술어 = 없음**(헬퍼 계약 (2)의 자기 제거만). rev2 가 "현행과 동일한 같은 카드 중복 방지"라고 적은 것은
  **틀렸다**(G1 P2-2 — 코드로 재확인 `:484-499`). 현행은 수집 때 `(카드, 무기)` 로만 디득하므로
  **같은 카드가 무기만 달리해 한 오퍼에 공존할 수 있고, 그것이 §2-3-2 v4 의 의도**("다중 무기 공유 카드 =
  별개 선택지"). "같은 Card 포인터 제거"로 구현하면 그 의도를 조용히 깨뜨린다.

---

## 7. 함수별 계약

| 함수 | 권위 | 호출자 | 전제조건 | 실패 시 |
|---|---|---|---|---|
| `ComputeSynergyMultiplier` | 순수 | `GetUnlockDrawWeight`, 자동화 | 없음 | 태그 없음/카운트 0 → 1.0 |
| `GetUnlockDrawWeight` | 서버(읽기만) | `DrawWeaponUnlockOffer` | `ActivePool` 유효 | 풀 null → `Card->Weight` |
| `RecordAcquiredCard` | 서버 전용 | `ApplyCard` **성공 직후** | 적용이 실제로 성공 | Card null → no-op |
| `BuildTagCountMap` | 서버(읽기만) | `DrawWeaponUnlockOffer` 시작 | 없음 | 원장 비면 빈 맵 |
| `WeightedSampleWithoutReplacement` | 순수 | 두 추첨 경로 | `Candidates.Num()==Weights.Num()` | 총 가중치 ≤0 → 중단 |

**기록 시점 계약 (익스플로잇 차단)**: 원장은 `ApplyCard` 가 효과를 **성공적으로 적용한 뒤**에만 는다 — 픽 소비(`:329`) 뒤, `return true`(`:331`) 앞. 제시만 받은 카드·거부된 픽·**리롤로 버린 카드**는 기록되지 않는다. `ServerRerollOffer`(`:458-479`)는 `DrawCards` 만 부르고 `RequestCardOffer`(`:313-353`)도 추첨만 하므로, 증가 지점이 `ApplyCard` 안 하나뿐이면 이 성질이 자동으로 성립한다. ⚠️ 디버그 `FPSR.ApplyCard`(`:691`)와 교체 경로도 `ApplyCard` 를 타므로 **서브시스템 안이 유일 지점**이다(컨트롤러에 두면 새어 나간다).

---

## 8. 수명주기 · 소유권 (G1 P2-6 — rev1 에 통째로 빠져 있었다)

- **생성/소유**: 원장은 `AFPSRPlayerState` 의 복제 프로퍼티. 신규 서브시스템·컴포넌트·델리게이트 없음.
- **리셋**: `AFPSRPlayerState::ResetRunState()`(`Private/Core/FPSRPlayerState.cpp:432`) 안에서 `AcquiredCards.Empty()` + Push Model dirty. 위치는 `CardGrantedAbilityHandles` 정리(`:487-491`) 바로 옆 — 같은 계급(런 진행 상태)이므로 함께 산다. 호출 경로 = 로비 진입(`FPSRLobbyGameMode.cpp:37` PostLogin · `:65` HandleSeamlessTravelPlayer).
- **`CopyProperties` 에는 넣지 않는다.** `:519` 가 이미 "Run-progression fields are deliberately NOT copied — they are reset on lobby entry regardless" 라고 못박았다. 엔진은 심리스 트래블에서 PS 를 **새로 스폰**하고 `CopyProperties` 로만 넘기므로 원장은 자연히 비어서 시작한다. **위험은 반대 방향이다** — 나중에 누군가 "서버 상태니까"라며 여기 추가하면 런 사이로 새 나간다. 그래서 이 문장을 계약으로 적는다.
- **GC 소유**: `FFPSRAcquiredCard::Card` 는 `UPROPERTY` 라 리플렉션이 참조를 살린다. 카드 DA 는 풀/무기 DA 가 이미 붙들고 있다.
- **초기 동기화**: 늦참 클라는 PS 복제로 원장 전체를 받는다(배열 복제 기본 동작). 별도 처리 불요.
- **델리게이트**: 신규 구독 0. (정보창 위젯이 갱신 통지를 원하면 `OnRep` 을 그 유닛에서 추가한다 — 이 유닛은 `UPROPERTY(Replicated)` 까지만.)

---

## 9. 복제표 (§6-3 서버권위 + Push Model)

| 프로퍼티 | 종류 | Push Model | 조건 | 비고 |
|---|---|---|---|---|
| `AFPSRPlayerState::AcquiredCards` | `Replicated` | `RecordAcquiredCard` / `ResetRunState` 에서 `MARK_PROPERTY_DIRTY_FROM_NAME` | `COND_None` | **모든 클라가 모든 플레이어의 원장을 본다**(Tab 정보창). `GetLifetimeReplicatedProps` 등록 |

**대역폭**: 4인 × 런당 카드 수십 장 × (에셋 참조 + 레어도 1바이트). 변화는 **픽할 때만**(런당 수십 회) 일어난다. 스웜 복제 예산과 무관한 축이다.
⚠️ 패키지 빌드에서 Push Model 이 꺼져도 정합성은 성립한다(배열이 통째로 복제될 뿐 내용은 같다).
**스탯은 새로 복제하지 않는다** — `UFPSRCombatSet` 이 `COND_None`(`FPSRCombatSet.cpp:24-26`)이라 다른 플레이어 속성도 이미 전원에게 도달한다. 정보창은 그것을 읽기만 하면 된다.

---

## 10. 데이터드리븐 경계

| 값 | 나가는 곳 | 기본값 |
|---|---|---|
| 카드의 빌드 소속 | 시트 `Cards.BuildTags`(세미콜론 리스트) | 공란 |
| 카드 1장당 시너지 | `DA_Character_CardPool.SynergyBonusPerCard` | 0.5 |
| 시너지 상한 | `DA_Character_CardPool.SynergyMaxStacks` | 4 |
| **허용 태그 어휘** | `DA_Character_CardPool.BuildTagVocabulary` | `crit` (이번 저작분) |

> 0.5 / 4 = 4장 투자 시 **3배**. **출발점이지 정답이 아니다** — 실제 세기는 PIE 후 사용자가 조정([[leave-fine-tuning-to-user]]).

---

## 11. 미결정 · 결정된 것

**사용자 결정 (2026-09-06) — 셋 다 확정**

1. **태깅 세트 = 미션 기능 카드 5장 + 스탯 2장**(`DA_Card_CritChance`·`DA_Card_CritMult`). `DA_Card_Damage`(범용)·`DA_Card_ADSZoom_ThisWeapon`(정밀 축)은 **제외** — 범용 카드에 이름표를 붙이면 치명타를 의도하지 않은 플레이어까지 치명타 기능 카드로 끌려간다. 넓히고 싶으면 시트 셀 하나.
2. 🔴 **진행도에 세는 것은 기능 카드뿐이다.** 스탯 카드는 이름표를 달아도 **추첨 가중치에 기여하지 않는다.**
   - 판정 = `GetCardBehaviorFragment(Card) != nullptr`(`FPSRCardSubsystem.cpp:467` 의 기존 헬퍼 재사용 — 오퍼 타입을 원장에 따로 담지 않아도 카드 자체로 구분된다).
   - **따라서 스탯 카드의 `BuildTags` 는 이 유닛에서 소비자가 없다.** 유일한 예정 소비자 = **Tab 정보창**(빌드별 묶음 표시). 죽은 데이터가 아니라 *아직 소비자가 안 온* 데이터다 — 이 문장이 없으면 다음 사람이 "왜 안 먹지"에서 시간을 버린다.
   - 의미: **빌드를 선언하는 것은 기능 카드다.** 수렴은 첫 치명타 *기능* 카드를 얻은 뒤 시작된다.
   - ⚠️ 현 콘텐츠 한계(결정 탓이 아니라 물량 탓): 라이플 기능 카드가 치명타 5장뿐이라 B 그룹 내부 재분배가 무의미하다. **두 번째 무기의 기능 카드가 후보에 섞이는 시점부터** 체감이 생긴다. §12 PIE 1 은 그 조건에서 판정한다.
3. **오타 가드 = 안 A**(풀 DA `BuildTagVocabulary` + `FPSRCardPoolValidator` 교차검증). 설정 파일을 안 건드려 "새 빌드 = 시트 작업"이 유지된다. 안 B(`Build.*` GameplayTag)도 정당했으나 기각 사유 = ini 등록 축이 늘어난다.

**결정됨 (노트로 강등, G1 판정 반영)**

4. 시너지 세기 0.5/4 = 튠 출발점(미결정 아님).
5. 다중 태그 = **최대값**(태그 부풀리기 방지).
6. 무기 가로지름 — `CritChance`/`CritMult` 가 캐릭터 카드라 **사실상 강제**다. 대안 `crit.rifle` 은 캐릭터 카드를 라이플 빌드에 묶지 못한다.
7. **CSV 컬럼 위치 = `Family` 바로 뒤**(카드 레벨 메타끼리 모은다). 대가 = `EffectBaseColumn` 13→14(`FPSRCardCsvSchema.cpp:341`). 끝에 붙이면 상수는 안 바뀌지만 §2-3-10 의 "E그룹이 꼬리" 규약이 깨진다.
8. **CSV 헤더 절차**(결정 아니라 절차): 헤더는 4곳(실측 10)을 코드/설정에서 먼저 맞추고 → `authoring_sheet.py apply`(헤더가 `expectedHeader` 와 맞아야 돈다) → `push`(writeback 설정 시 헤더 포함 미러). **이 머신엔 `Config/AuthoringSheets.writeback.json` 이 없으므로 시트 헤더는 사용자가 수동으로 맞춘다.**

**갭 처리**: 명세에 없는 판단이 필요하면 추측하지 말고 멈추고 "명세 갭"으로 보고. 특히 ① 배제 규칙(v4)을 건드려야 할 것 같을 때 ② 기록 시점을 `ApplyCard` 성공 이후가 아닌 곳으로 옮기고 싶을 때 ③ 두 풀의 추출 헬퍼를 다시 갈라놓고 싶을 때 ④ **레벨업 풀에 시너지를 넣고 싶어질 때**(사용자가 명시적으로 배제했다).

**P3 (한 줄씩 반영)**
- 중복 태그(`crit;crit`)는 2회 카운트된다 → `IsDataValid` 에서 중복 태그를 에러로.
- 교체(`ServerSelectCardReplacement`)로 빠진 프래그먼트의 원장 기록은 **남는다** — "투자는 투자다"(의도).
- 익스포터 `BuildTags` 는 **저작 순서 유지**(정렬하지 않는다) — 왕복 안정성.

---

## 12. 검증 기준

| # | 검사 | 통과 조건 |
|---|---|---|
| 1 | 명세 대조 | §6·§7·§8·§9 선언·계약이 코드와 1:1 |
| 2 | 빌드 | `-DisableUnity` **`Result: Succeeded`** + 테스트 추가분이 있으므로 **`-DisableAdaptiveUnity -ForceUnity` 도 필수**(§6-6) |
| 3 | 신규 자동화 `FPSRoguelite.Card.Synergy` | 순수 `ComputeSynergyMultiplier` 만 겨냥(월드·액터 불요). ⓐ 카운트 0 → 1.0 ⓑ 2 → `1+0.5×2` ⓒ 10·상한 4 → `1+0.5×4` ⓓ 다중 태그 → **최대값** ⓔ `BonusPerCard=0` → 항상 1.0 ⓕ 빈 태그 배열 → 1.0 |
| 4 | 기존 자동화 | `Editor.CardCsv.RoundTrip`(선행 실패) 외 **신규 실패 0**. 특히 `Card.FamilyDerivation`·`CardCsv.Schema` |
| 5 | 회귀 — 레벨업 풀 | **코드 무접촉이므로 자명**. `GetEffectiveWeight` diff 0 을 근거로 삼는다 |
| 6 | 회귀 — 미션 풀 | 모든 `Weight`=1 · 시너지 0 일 때 균등과 **분포 등가**. 판정법: 후보 N개에서 Count 개를 M회(예 10,000) 뽑아 **후보별 포함 빈도가 Count/N 에 수렴**(±3σ). RNG 스트림은 달라지므로 비트 동일은 기준이 아니다 |
| 7 | 임포터 왕복 | `Editor.CardCsv.RoundTrip` 이 선행 실패이므로 **대체 판정**: 커맨드렛 임포트를 2회 연속 실행 후 `git status -- Content/Cards Content/StringTables` 변경 0 |
| 8 | 레드팀 G2 | 푸시 전 `origin/main..HEAD` diff. **P1 잔존 시 푸시 금지**. G1 이후 바뀐 요구 2건(§1)을 프롬프트에 명시 |
| 9 | PIE / 사용자 | 아래 |

**PIE 사용자 스모크**
1. 치명타 **기능** 카드를 1~2장 가진 상태에서 미션 클리어 오퍼에 **나머지 치명타 기능 카드가 눈에 띄게 자주** 뜨는가.
2. **스탯 카드(레벨업)는 평소와 같은가** — 치명타 스탯이 더 자주 뜨면 안 된다(사용자 결정).
3. 아무것도 안 고른 런 시작 시 미션 오퍼 분포가 평소와 같은가(첫 카드에 시너지가 붙으면 안 된다).
4. **리롤로 시너지가 쌓이지 않는가** — 카드를 제시받고 리롤로 버린 뒤 원장이 안 늘었는지.
5. **협동 2인**: 각자 다른 빌드로 갈 때 각자 자기 빌드로 수렴하는가.
6. 런을 끝내고 새 런을 시작하면 이전 런의 수렴이 사라지는가.
7. **원장 복제**: 클라이언트에서 **다른 플레이어의** `AcquiredCards` 가 채워져 보이는가(정보창 위젯의 선행조건 — 이 유닛의 실증 지점).

---

## 13. 레드팀 지적 원장

### G1 1회차 (2026-09-05, `claude-fable-5`) — **반려** · P1 1 · P2 8 · P3 7

| # | 지적 | 처리 |
|---|---|---|
| P1-1 | `GetSynergyMultiplier` 가 private + 월드 서브시스템/액터 의존 → §12-3 자동화 **작성 불가** | **수용** — 순수 `public static ComputeSynergyMultiplier(태그, 카운트맵, 보너스, 상한)` 로 분리(§6) |
| P2-1 | 가중치 공식이 두 곳 → CRIT1 교훈 절반만 적용 | **수용(설계 축소로 해소)** — 사용자 결정으로 시너지가 미션 풀 전용이 되어 `GetUnlockDrawWeight` **한 함수에만** 산다. 레벨업은 무접촉 |
| P2-2 | 미션 풀 레어도 굴림 순서 미명시 | **수용** — 현행 "선택 후 굴림" 보존을 §6 에 명시 + 헬퍼에 넘기는 Rarity 가 미굴림 값임을 계약화 |
| P2-3 | CSV 컬럼 위치 미결정 + 파일 4곳 누락 | **수용** — 위치 = `Family` 뒤(§11-7), 헤더 리터럴 4곳 실측 확인 후 §5 에 전부 열거 |
| P2-4 | 태깅 대상 미열거 → 수렴 관측 불가 | **수용** — §11-1 로 승격(사용자 결정) |
| P2-5 | 태그 오타 가드 없음 = "조용히 안 도는" 클래스 | **수용** — 안 A(풀 DA 어휘 + Validator) 채택, B 도 정당함을 §11-3 에 병기 |
| P2-6 | §8 수명주기 누락 + 리셋 계약 부정확 | **수용** — §8 신설. rev1 의 인과 서술이 **틀렸음**을 실측(`CopyProperties:519`)으로 확인해 §3-9 에 정정 기록 |
| P2-7 | SSOT-first 위반(도메인 파일 누락) | **수용** — §5 문서 표에 `CombatWeaponCard.md` 4개 절 + `AuthoringSheetWriteback.md` |
| P2-8 | §12-6 왕복 판정이 선행 실패 자동화에 의존 | **수용** — §12-7 대체 판정(2회 임포트 후 `git status` 변경 0) |
| P3×7 | 헬퍼 계약 승계·중복 태그·교체 잔존·익스포터 순서·헤더 절차 순서·미션 등가 판정법 등 | **수용** — §6 헬퍼 주석 · §11 P3 · §12-6 에 반영 |

**기각 0건.** G1 이 든 근거(파일:줄)를 두 건 독립 확인했다: `CopyProperties:519` 주석 원문, 헤더 리터럴 4곳.

### G1 2회차 (2026-09-05, `claude-fable-5`) — **반려** · P1 0 · P2 7 · P3 13

구조(복제 원장 · 시너지 단일 지점 · 추출 헬퍼 공유)는 **승인**. 1회차 지적 10건 중 **닫힘 8 · 부분 닫힘 2**(P2-3 CSV, P2-5 검증기) 판정을 받았다.

| # | 지적 | 처리 |
|---|---|---|
| P2-1 | 현 콘텐츠에서 시너지의 실제 효과는 "새 무기 오퍼 희석" — 라이플 `UnlockableFeatures` 가 치명타 5장뿐이라 Part B 내부엔 수렴시킬 대상이 없다. 새 무기 ≥1장 확률 64%→31%(손계산) | **사용자 결정 C 로 해소** — 그룹 비중 보존 2단 추출. A(새 무기) 그룹 몫은 매 추출에서 정확히 보존되고 시너지는 B 안에서만 재분배된다 |
| P2-2 | "현행과 동일한 같은 카드 중복 방지"가 **거짓** — 현행 미션 풀엔 배제 술어가 없고 `(카드,무기)` 디듑뿐. 축자 구현하면 §2-3-2 v4 의도를 깬다 | **수용** — 코드로 재확인(`:484-499`) 후 §6 을 "배제 술어 없음"으로 정정 |
| P2-3 | CSV 파서가 DisplayName/Description 을 **고정 인덱스 7..12** 로 읽는다. 컬럼 삽입 시 무음 오임포트(ko←BuildTags…) + 스키마 테스트가 DisplayName 을 단언 안 해 자동화가 못 잡음 | **수용** — 인덱스 시프트·픽스처 데이터 행·DisplayName 단언을 §5 에 명시 |
| P2-4 | `Cards.csv` 파일 자체의 헤더 마이그레이션 절차 부재 → §11-8 순서가 실행 불가 | **수용** — §5 에 4단계 절차 |
| P2-5 | 어휘 검증기의 순회 집합 미명시 — 값싼 검사는 풀 배열만 훑는데 태깅 대상 7장 중 5장이 무기 `UnlockableFeatures` 라 **검사 0** | **수용** — AssetRegistry 전 카드 스캔으로 명시 |
| P2-6 | 원장 원소가 `TargetWeapon` 을 버린다 → 정보창이 "연사 속도(라이플)"를 못 그리고, 나중에 넣으면 복제 struct 변경 | **수용** — 원소에 추가. `FFPSRCardDraw` 재사용 대안은 기각(오퍼 구조라 의미·수명이 다르다) |
| P2-7 | `WeightedSampleWithoutReplacement` 가 private → §12-6 등가 판정 실행 불가(1회차 P1-1 과 같은 병) | **수용** — public static |
| P3×13 | 복제 문구·항상로드 기제·변경 통지·도메인 클램프·`Count` 가드·`TargetWeapon` 이월·Luck 오기·왕복 판정 문구·PIE 3 오프닝 시드·§13 열거·CombatSet 한정·교체 잔존·`Card->Weight` 부활 노트 | **수용** — 해당 절에 반영 |

**기각 0건.** G1 이 든 근거 중 셋을 독립 확인했다: `(카드,무기)` 디듑(`:484-499`) · 파서 고정 인덱스(`:333-341`) · `BuildSingleDraw` 의 Luck 사용(`:350-357`, rev1·2 의 "Luck 무시" 오기 확정).

### G1 3회차 — **생략 (사용자 결정 2026-09-05)**

`Workflow.md` §6-5-2 (5) 는 같은 게이트 3회차부터 사용자 보고를 요구한다. 보고했고 **사용자가 생략을 택했다.**
사유: ① 2회차에서 **P1 이 0** 이라 구조는 승인됐다 ② P2 7건은 전부 명세 *문장* 결함이고 게이트가 **처방까지 적어 줬다** ③ 남은 Fable 호출 1회는 G2(머지 게이트)에 쓰는 편이 낫다.

🔴 **G2 가 집중해서 볼 것** — 3회차를 안 태웠으므로 아래는 **어떤 게이트도 검증하지 않은 판단**이다:
1. **그룹 비중 보존 2단 추출**(사용자 결정 C)의 설계 자체. "모든 시너지가 1이면 종래 균등과 분포가 같다"는 주장의 참·거짓.
2. rev3 에서 새로 쓴 **"현행과 동일" 계열 문장 전부.** 이 유닛에서 그 계열이 두 번 틀렸다(배제 술어·Luck) — 같은 계급의 세 번째가 남아 있을 수 있다.
3. 원장 원소 확장(`TargetWeapon`)과 `OnRep`+호스트 브로드캐스트가 **정보창 유닛의 요구를 실제로 만족**하는가.

### G1 2회차 재제출 — *(생략)*
### G2 머지 게이트 — *(푸시 직전)*
