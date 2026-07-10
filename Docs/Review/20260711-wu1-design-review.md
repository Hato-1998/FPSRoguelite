# W-U1 무기 모듈러 슬롯+선택기 — 구현 설계 적대검증 기록 (2026-07-11)

> 착수 전 read-only 적대검증. 구현 스펙(플랜 v3 §3~6을 실제 코드에 매핑한 구체 결정)을 승인 전에 굳히기 위해 실행. **Opus 6에이전트 워크플로**(5렌즈 병렬 검증 → 종합). 결과를 최종 플랜에 반영 후 구현(Sonnet)·검증(Opus)·커밋 `f3cd82b5`.

## 방법
- 워크플로 `wu1-design-review`: 5렌즈(①UE 리플렉션 ②네트워킹/생명주기 ③선택기 의미론 ④격리계약+§9 게이트 ⑤IsDataValid/C5 범위) 병렬 → 종합 1에이전트. 각 렌즈=실제 소스 file:line 대조, 확정결함/개연우려/확인됨 분류.
- 총 712k 토큰·91 tool_use·6/6 성공.

## 총평
코어 설계(순수 선택기·시그니처diff 코얼레스·타이머 뼈대·격리계약 게이트 ①②④⑤)는 실측 대조에서 견고 → 방향 승인. 단 브리프대로 착수 시 **4 확정결함** 발생 → 조정 후 승인.

## 확정결함 4건 (사전교정, 코드에 반영됨)
1. **C2 통지가 resolved 스탯의 AllWeapons 절반 누락** — resolved=base+ThisWeapon+**AllWeapons**(`FPSRWeaponInstance.cpp:236`)인데 통지가 ThisWeapon만 탐. 순수 "전체무기" 스탯카드로 StatThreshold 진화조건을 넘겨도 리빌드 미발화(MP 전원+DBNO 관전자 stale). → `FPSRPlayerState`의 `AddAllWeaponsModifier`/`OnRep_AllWeaponsMods`/`ResetRunState` 3사이트에 통지 추가.
2. **C2 경로가 머즐/에임 캐시 리셋 우회** — 리셋 유일지점이 상류 `RefreshFirstPersonWeaponVisual`(`FPSRCharacter.cpp:985-987`)라, 소켓 잃은 파츠로 슬롯 교체 시 파괴 컴포넌트 댕글링(오정렬, receiver fallback 미발동). → 리셋을 `RebuildPartsFromSelection` 진입부로 이동(equip+modifier-change 두 경로 공유).
3. **HasFragment를 선택적 FragmentTag로 매칭 시 오작동** — 프래그먼트 정체성=에셋 포인터, `FragmentTag`는 선택적(미저작 다수)라 false neg/pos. → 에셋 포인터(`ActiveFragments.Contains`+MinStacks) 매칭.
4. **`USTRUCT` 내부 Instanced 조건은 리포 미검증** — 리포의 모든 폴리모픽 Instanced는 UObject 직속(카드·적 스폰규칙). 헤드리스 import_text/배열요소 복제 딥카피 경로 미검증. → 규칙=폴리모픽 UObject `UFPSRWeaponPartRule`, 조건은 그 내부 Instanced. 메모리 `polymorphic-instanced-uobject-direct`.

## 굳힌 코어 결정 (CONFIRM)
- 선택기: 슬롯리스 base 전부(회귀0) + 슬롯별 승자 Tier↓·Priority↓·RuleIndex↑(RuleIndex 전역 유일=완전순서, AssetPath tiebreak 제거 안전).
- C2 뼈대: `SetTimerForNextTick`(캐릭터 Tick은 shipping-dead) + 데디서버 guard + equipped 게이트 + `GetCurrentWeapon` 재읽기 + 전량 teardown 선행 → 게이트③(≤next-tick 1회)·리슨호스트 자기진화·원격 DBNO 관전자·스왑 동프레임 경합 전부 안전.
- 격리계약 §2-A ①②④⑤ 통과: 선택기/조건/캐릭터 `Card/` 미include, WeaponInstance에 파츠 복제/세이브 필드 0, 빈 PartRules 회귀0, 사이트 제거 시 ADS receiver fallback·트레이스/데미지 불변.
- S1 드롭: 슬롯을 규칙에만(파츠 struct 무수정=회귀0, 이중 Slot 필드 제거).
- ComputeSignature: 슬롯 FName lexical 정렬 + HashCombine(머신 안정·충돌 코스메틱).

## 사용자 결정 3건
1. **규칙 형태 = 조건 분리(합성)** — 규칙 UObject + 내부 Instanced 조건(플랜 v3 폴리모픽 조건 잠금 유지, 조건 재사용). ↔ 대안: 규칙=술어(통합).
2. **C7 = 저작-타임 전용** — WITH_EDITOR IsDataValid, W-U1 §9 머지게이트에서 제외 명시(빌드/스모크가 IsDataValid 미실행). 커맨드릿 게이트화는 후속(선택).
3. **C5 async = 후속 W-U1b 분리** — 현 동기로드=선택 부분집합(프리즈/장착 게이트로 히치 상한↓). 미드컴뱃 modifier 소스 생기면 승격.

## 결과
빌드 -NoXGE Succeeded(0err/0warn) · 스모크 validate-data exit0 · 커밋 `f3cd82b5`. PIE 시각검증=A트랙(팔그립) 대기. 원 종합/렌즈별 결과=세션 워크플로 journal(transcript dir).
