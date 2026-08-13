# CARDDRAW — 추첨 배제 규칙 v4 (family+레어도 쌍)

## 1. 메타

| 항목 | 값 |
|---|---|
| 유닛 ID / 이름 | CARDDRAW / 배제 규칙 v4 |
| 브랜치 | `phase/card-draw-exclusion-v4` |
| 작성 모델 | `claude-fable-5` |
| 작성일 | 2026-08-13 |
| 상태 | `확정` |
| 관련 SSOT | `CombatWeaponCard.md` §2-3-2 "배제 규칙 v4"(사용자 확정 2026-08-13) |

## 2. 목표 / 비목표

**목표** — 한 제시(오퍼 1회의 3장) 안에서:
- 같은 family + **같은** 굴린 레어도 = 공존 불가.
- 같은 family + **다른** 레어도 = 공존 가능(같은 카드 에셋이 레어도만 달리해 2장 등장 가능 — 종전 동일 카드 포인터 차단은 폐지·이 규칙이 대체).
- WeaponStat 카드의 자동 family가 스코프를 구분(`<Attr>.all` / `<Attr>.this`) → 전체무기/개별무기 같은 속성은 서로 다른 카드로 공존 가능.
- 판정표(사용자 예시): All레어+All레전더리 ✅ / All레어+All레어 ❌ / All레어+This레어 ✅.

**비목표** — 리롤 차감/서버권위 인덱스 선택/오퍼타입 라우팅/Luck 가중치/CSV 스키마 변경 없음. 명시 저작 Family 값의 의미(같은 값=한 묶음) 변경 없음 — 스코프 한정자는 **자동 파생일 때만** 붙는다.

## 3. 제1원리 3줄

1. 제시 다양성의 실체 기준은 "플레이어에게 같은 선택지가 두 번 보이는가" — 같은 속성이라도 레어도(수치)가 다르면 유효한 트레이드오프 선택지다(사용자 확정).
2. 기존 인프라 재사용: `GetCardFamilyKey()`(FName)와 서버 빌드 오퍼 루프는 유지, 배제 집합의 키만 FName → (FName, ECardRarity) 쌍으로 확장. 동일 카드 포인터 차단(`bSameCard`)은 쌍 키에 포섭되므로 제거.
3. 파생 규칙은 임포터(에디터) 소유 — 런타임은 저장된 Family만 읽는 계약 불변(§2-3-2 v3).

## 4. 파일 목록

| 경로 | 신규/수정 | 설명 |
|---|---|---|
| `Source/FPSRoguelite/Private/Card/FPSRCardSubsystem.cpp` | 수정 | 추첨 배제: family 단독 → (family, 굴린 레어도) 쌍. `bSameCard` 포인터 차단 제거 |
| `Source/FPSRogueliteEditor/Private/CardImport/FPSRCardCsvImporter.cpp` | 수정 | 자동 파생: E1이 WeaponStat이면 `<AttrId>.<all|this>`(해석된 bThisWeaponOnly 기준), 그 외 `<AttrId>` |
| `Source/FPSRogueliteEditor/Private/CardImport/FPSRCardCsvExporter.cpp` | 수정 | 파생값 공란 정규화 기준을 새 파생 규칙으로 갱신 |
| `Content/Authoring/Cards.csv` + 카드 uasset | 재생성 | 재수출→재임포트(스코프 한정 family 반영, 2회차 멱등 dirty 0) |
| `Source/FPSRogueliteEditor/Private/Tests/FPSRCardCsvSchemaTest.cpp` 또는 신규 테스트 | 수정/신규 | 파생 규칙 단위 검증(WeaponStat all/this, 비WeaponStat) |

## 5. 계약 (헤더 변경 없음 — 구현 지침)

- 배제 판정 지점 = 서버 오퍼 빌드 루프에서 **레어도 roll 이후**: 후보 (FamilyKey, Rarity)가 이미 선택된 쌍 집합에 있으면 그 후보 기각(기존 family 기각과 동일한 재시도 흐름 유지). 기존 흐름이 "family 검사 → 레어도 roll" 순서라면 순서를 뒤집거나 roll을 선행 — 구조 판단이 애매하면 명세 갭으로 보고.
- 파생 스코프 한정자는 **소문자 `.all` / `.this` 접미**. 명시 Family(비공란)는 접미 없이 그대로.
- IsDataValid의 "멀티이펙트 CardFamily 필수" 및 풀 검증기 불변.

## 6~10. (해당 없음 — 복제/수명주기/성능 신규 표면 없음: 서버 로컬 추첨 루프의 키 타입 확장뿐)

## 11. 미결정 · 갭 처리

미결정 없음. 갭 발생 시 멈추고 보고(§6-5-2).

## 12. 검증 기준

| # | 검사 | 통과 조건 |
|---|---|---|
| 1 | 빌드 | Development Editor Succeeded (-DisableUnity 1회 — 10분 초과 시 UBT 뮤텍스 갓차 주의, WorkLog 참조) |
| 2 | 자동테스트 | CardCsv 전부 + 파생 규칙 케이스 + DataEditor 회귀 Success |
| 3 | 재임포트 | 커맨드렛 1회차(=family 갱신 updated>0) → 2회차 unchanged=30 |
| 4 | validate-data | exit 0 |
| 5 | 레드팀 게이트 | §6-6-1 (코어 갈래) |
| 6 | PIE 사용자 스모크 | 판정표 3케이스 실제 관측(리롤 반복으로) |

## 13. 레드팀 지적 원장 (C3에서 채운다)
