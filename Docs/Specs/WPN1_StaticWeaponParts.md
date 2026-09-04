# WPN1 — 정적 무기 메시에도 모듈러 파츠를 붙인다 (파츠 부모 = `ActiveWeaponMesh`)

## 1. 메타

| 항목 | 값 |
|---|---|
| 유닛 ID / 이름 | `WPN1` / 정적 무기 메시의 모듈러 파츠 부착 — 파츠 부모를 `WeaponMesh`(스켈레탈 전용)에서 `ActiveWeaponMesh`(스켈레탈·정적 공용)로 |
| 브랜치 | `proto/arcade-look` (라이플 하드서피스 트랙의 하위 유닛. 머지 단위는 트랙 전체) |
| 작성 모델 | `claude-fable-5-1` — ⚠️ 폴백 표기: §6-5-2 는 C1 설계 주체를 Opus 로 두지만, 이 세션은 사용자가 `/model` 로 Fable 로 전환한 상태에서 진행됐다(2026-09-04). G1 게이트는 **별도 Fable 서브에이전트**로 띄워 설계자≠검증자를 유지했다 |
| 작성일 / 최종 갱신 | 2026-09-04 (**rev3 — G1 2회차 반려(P2 1·P3 8) 반영. 승인 제출본**) |
| 상태 | `확정` — **사용자 승인 2026-09-04**(rev3 `6081cd14`). 경위: G1 1회차 반려(P2 3) → rev2 → G1 2회차 반려(P2 1 = 검증표 문구; *"설계 자체의 결함은 못 찾았다 … rev3는 Opus 대조만으로 승인 단계로 가도 무방"*) → rev3 → 3회차 미실행(§6-5-2 (5) 사용자 보고로 갈음) → 승인. 다음 = C2 Sonnet 축자 구현 → C3 → PIE → G2 |
| 관련 SSOT | `Docs/RifleHardSurface_ResumePrompt.md` §1-2·§2-4·§3-2 · `Docs/WeaponPack_Integration.md`(총구=파트 소켓·조준=사이트 파트 소켓 규약) · `Docs/SSOT/Workflow.md` §6-5-2 |
| 관련 메모리 | `[[reason-in-multiplayer-terms]]` · `[[code-is-immutable-structure-only]]` · `[[production-structure-first]]` · `[[event-halves-authority-vs-client]]` · `[[card-pool-routing]]` · `[[uasset-strings-name-table-only]]` |
| 보드 행 | 「아케이드 룩 프로토 + 육안 게이트」 `3d03972d-dd88-81ff-87d6-f5730a06e7a4` 스코프 2/4 |

> **rev3 변경 요약 (G1 2회차 반영, 원장 = §13)** — **P2** §12-6 ⓑ 라벨 정정: 라이플↔SMG 스왑은 `Weapon` 이 항상 non-null 이라 언이큅 분기(`:2297`)를 안 탄다 → 목표 5(언이큅 캐시 초기화 보존)는 **런타임 결정적 재현 경로가 없고 #1 diff 대조로만 검증**된다고 명시. **P3-1** §5-2 박스의 "OnRep 순서 역전 창" 근거 삭제(그 창에서는 캐시가 이미 비어 있다) → 근거 = 오늘 실행 경로와의 동일성·불변식 보존. **P3-2** ⓒ 의 근거 2건 철회: 카드 멤버십(`WeaponCards` vs `UnlockableFeatures`)은 **네임테이블로 판별 불가** → 에디터 실측 항목으로 격하, `:711-715` 는 거부 게이트가 아님 → ⓒ 를 유계 절차(`DrawCards` 3회 → `Frag.Fill`)로. **P3-3** 갭 규칙 자기모순(①·⑤ 문자열 동일) 정정. **P3-4** "재해석 3곳"→4곳. **P3-5** ⓐ 에 Grip 방향 육안 항목. **P3-6** #7 에 라이플 `:2480` 경고 소멸 신호. **P3-7** ⓔ 통과 기준. **P3-8** §10 틱 문구 정정. ②의 새 형태에 대한 신규 결함 = **없음**(리뷰어가 가드 진리표 전수로 확인).
>
> **rev2 변경 요약 (G1 1회차 반려 반영)** — **P2-①** ②의 형태 정정: `:2573` 의 `!WeaponMesh` 는 기본 서브오브젝트라 항상 거짓인 죽은 가드였고, 이를 `!ActiveWeaponMesh` 조기 반환으로 바꾸면 언이큅 경로에서 살아나 꼬리의 캐시 초기화(`:2689` `:2692`)를 건너뛴다 → 조기 반환 삭제 + 부착 루프만 가드. **P2-②** §4 "에디터 모듈에 가정 없음" 철회(조립툴 스켈레탈 전용) → §11. **P2-③** ④ 런타임 소비자 0 → ⓓ 라벨 정정. P3 4건 반영.

---

## 2. 목표 / 비목표

### 목표
끝나면 **`WeaponMeshStatic` 으로 표시되는 무기도 `WeaponParts` 슬롯의 파츠를 붙인다** — 스켈레탈 무기와 같은 규칙(같은 선택기, 같은 소켓 규약, 같은 재빌드 수명주기)으로.

동작 기준:
1. 정적 무기(`WeaponMesh` 비움 · `WeaponMeshStatic` 설정)가 장착되면 `WeaponParts` 의 파츠가 **몸통 정적 메시의 소켓**에 붙는다.
2. 프래그먼트(진화) 변경으로 파츠 선택이 바뀌면 **다음-틱 코얼레스 경로**(`ProcessPendingWeaponPartsRebuild`)가 정적 무기에서도 재빌드한다(장착 시 즉시 경로 = `RefreshWeaponPartComponents`; 프래그먼트 변경은 항상 지연 경로 하나다, `:3125-3146`).
3. 파츠가 붙은 뒤 **좌/우손 그립 해석**이 파츠 소켓 → 리시버 순으로 떨어진다(`ResolveLeft/RightHandGripComponent`, `:2695-2722`) — 스켈레탈과 동일.
4. 스켈레탈 무기 6종의 동작은 **변화 0** (회귀 대조군 = §12-5).
5. **언이큅 경로의 캐시 초기화가 그대로 유지된다**: `RebuildPartsFromSelection(빈)` 의 꼬리(`:2689` `:2692`)가 오늘처럼 실행된다. ⚠️ **이 목표는 런타임으로 결정적 재현이 불가능하다**(rev3, G1 2회차 P2): `!Weapon` 언이큅 분기(`:2297`)에 들어가는 수단이 현 코드베이스에 없다 — `EquipSlot :309-312` 가 빈 슬롯을 거부하고, `CurrentSlotIndex` 를 `INDEX_NONE` 으로 되돌리는 코드가 0건이며, 사망(`HandleOutOfHealth`)도 무기를 내려놓지 않는다. **검증 = §12 #1 diff 대조(②에 조기 반환이 없고 꼬리 호출 2줄이 살아 있음)뿐.**

### 비목표 (구현자가 "친절하게" 채우면 안 되는 것)
- **무기 자체 AnimBP(노리쇠·재장전 몽타주) 경로는 손대지 않는다.** `FPSRCharacter.cpp:3221` `:3272` `:3869` 의 `WeaponMesh->GetAnimInstance()` 는 정적 무기에서 이미 null no-op 다(`PlayScaledReload` 람다 `:3195-3199`).
- **`DA_Weapon_Rifle` 의 `WeaponParts` 를 새 메시(`SM_RifleHS_*`)로 바꾸지 않는다.** DA 값 저작은 사용자 작업(`[[da-edits-are-user-work]]`). 이 유닛이 끝나면 라이플에는 **아직 Synty 파츠**가 붙는 게 맞다(§9).
- **소켓 이름·규약을 바꾸지 않는다.**
- **헤더의 시그니처·필드 추가/변경 금지.** 바뀌는 헤더 내용은 **주석 2곳**뿐이다(§5-1).
- **`RefreshWeaponPartComponents` 를 호출하는 지점을 늘리거나 줄이지 않는다.**
- **정적 무기 파츠에 복제·저장을 붙이지 않는다**(§7).
- **다른 정적 무기(나이프·비무장)에 파츠를 저작하지 않는다.**
- **조립툴(`FPSRogueliteEditor/Assembler`)·DA 검증기(`IsDataValid`·`FPSRWeaponValidator`)의 정적 몸통 대응은 이 유닛이 하지 않는다** — §11 후속.
- **`FPSRWeaponDataAsset.h:124` 등 다른 파일의 stale 주석은 손대지 않는다** — §11 후속. 이 유닛의 diff 는 `FPSRCharacter.cpp/.h` 두 파일로 닫는다.
- **언이큅 경로를 결정적으로 재현하는 디버그 명령을 추가하지 않는다**(rev3) — 목표 5 검증을 위해 코드를 늘리지 않는다. 필요해지면 별도 유닛.

---

## 3. 제1원리 3줄 (핵심원칙 4)

1. **제1원리 근거** — 이 코드는 **플레이어(1~4)** 의 무기에만 돈다. 적 스웜 경로와 무관하고, 액터당 비용은 파츠 컴포넌트 ≤7개 × 플레이어 ≤4 로 스켈레탈 무기가 오늘 내는 것과 같다. 서버권위: 파츠 선택의 입력(스탯·프래그먼트)은 **이미 복제되는 상태**(`Slots`·`CurrentSlotIndex`·`ActiveFragments` 전부 조건 없는 복제 — G1 확인)이고 파츠 자체는 각 머신이 로컬로 재생성한다.
2. **엔진 기본값·기존 인프라와의 관계** — **기존 인프라를 그대로 쓴다.** 파츠는 이미 `UStaticMeshComponent` 로 생성돼 `AttachToComponent(Parent, KeepRelativeTransform, SocketName)` 으로 붙는다(`:2600` `:2608`). 바뀌는 것은 **부모 하나**다. 정적 부모에서 필요한 API 는 엔진이 다형으로 제공한다: `UStaticMeshComponent::DoesSocketExist`(`StaticMeshComponent.cpp:1334`) · `GetSocketTransform(Name, RTS_Component)`(`:1365`, `:1389-1392`). `AttachToComponent` 는 부모 null 을 스스로 걸러내고(`SceneComponent.cpp:2312`), 없는 소켓명은 **경고 없이** 부모 원점으로 붙인다(`:2295-2420` 전수 — 경고는 self/cycle/mobility/template 뿐, G1 확인). 파츠·`WeaponMeshStatic` 모두 기본 Movable 이라 모빌리티 거부(`:2365`)에 안 걸린다.
3. **프로젝트 제약과의 정합** — 무기 조립 규약(총구=파트 소켓, 조준=사이트 파트 소켓, 좌/우손=파트 또는 리시버)은 **런타임 쪽**에서는 이미 `ActiveWeaponMesh` 폴백으로 짜여 있다(`:2472-2476` 조준, `:2618-2632` 총구, `:2695-2722` 양손, `GetWeaponRootPlacementInGunFrame :2860`). **파츠 부착·프레임 캐시 4곳만 `WeaponMesh` 를 직접 잡고 있다** — 그 비대칭을 없앤다. ⚠️ **에디터·검증기 쪽은 여전히 스켈레탈 전용이다**(§11) — 이 유닛은 *런타임* 비대칭만 닫는다: **"런타임 파츠 표시는 이 위에 서고, DA 저작은 당분간 Details 수기"**.

---

## 4. 파일 목록

| 경로 | 신규/수정 | 한 줄 설명 |
|---|---|---|
| `Source/FPSRoguelite/Private/Hero/FPSRCharacter.cpp` | 수정 | 5개 지점의 파츠 부모·가드를 `ActiveWeaponMesh` 기준으로 (§5-2) + 주석 3곳 |
| `Source/FPSRoguelite/Public/Hero/FPSRCharacter.h` | 수정 | 주석 **2곳** — `RefreshWeaponPartComponents` 선언(`:1247-1248`)·`WeaponPartComponents` 멤버(`:1221-1223`). 시그니처 불변 |

그 외 파일 변경 없음. 이 유닛 **범위 밖**에 남는 스켈레탈 전용 지점(§11): 조립툴 `FPSRWeaponAssemblerViewportClient.cpp:94-96` · `Helpers.cpp:52-58` · `:115` / DA 검증기 `FPSRWeaponDataAsset.cpp:197` `:306-308` `:370-372` / 주석 `FPSRWeaponDataAsset.h:124` · `.cpp:190` / `FPSRCharacter.h:1251-1253`(`RebuildPartsFromSelection` 주석의 오른손 누락 — 기존).

---

## 5. 인터페이스 선언 (헤더 스케치)

### 5-1. 헤더 — 주석 2곳만 바뀐다 (시그니처·필드 불변)

```cpp
// FPSRCharacter.h:1221-1223  (WeaponPartComponents 멤버 주석)
/** Runtime-created modular weapon-part components (U15), child-attached to ActiveWeaponMesh (the shown weapon mesh —
 *  skeletal WeaponMesh or static WeaponMeshStatic) and rebuilt on each weapon change. Visible to everyone, like the
 *  weapon they hang off (ADR 0002 — they used to be OnlyOwnerSee). Empty for partless weapons / no weapon. */
UPROPERTY(Transient)
TArray<TObjectPtr<UStaticMeshComponent>> WeaponPartComponents;

// FPSRCharacter.h:1247-1249  (RefreshWeaponPartComponents 선언 주석)
/** Destroy any existing modular part components and rebuild them from the equipped weapon's WeaponParts list.
 *  Attaches to ActiveWeaponMesh — whichever of WeaponMesh (skeletal) / WeaponMeshStatic (static) is shown; no weapon
 *  or no active mesh attaches nothing. Called from the weapon refresh. */
void RefreshWeaponPartComponents(const UFPSRWeaponDataAsset* Weapon);
```

### 5-2. `.cpp` 5개 지점 — 바뀌는 줄만 (본문은 쓰지 않는다; 축자 구현 대상)

| # | 위치 | 현재 | 변경 |
|---|---|---|---|
| ① | `RefreshWeaponPartComponents` `:2530` | `if (!Weapon \|\| !WeaponMesh \|\| ActiveWeaponMesh != WeaponMesh)` | `if (!Weapon \|\| !ActiveWeaponMesh)` |
| ② | `RebuildPartsFromSelection` `:2572-2576` | `// Parts attach to the skeletal weapon mesh only …`<br>`if (!WeaponMesh) { return; }` | **조기 반환 블록 삭제.** 대신 부착 루프(`for (const FFPSRWeaponPartAttachment& PartDef : Selected)` `:2583`)를 **루프 전체**(`NewObject` 포함) 기준으로 `if (ActiveWeaponMesh) { … }` 로 감싼다. 루프 뒤의 **재해석 4곳**(총구 `:2618` · 조준 `:2636` · 왼손 `:2657` · 오른손 `:2673`)과 `RefreshHandGripInGunFrameCache()` `:2689` · `RefreshPartFramesInGunSpaceCache()` `:2692` 는 **항상** 실행된다 |
| ③ | `RebuildPartsFromSelection` `:2608` | `PartComp->AttachToComponent(WeaponMesh, FAttachmentTransformRules::KeepRelativeTransform, PartDef.Socket);` | `PartComp->AttachToComponent(ActiveWeaponMesh, FAttachmentTransformRules::KeepRelativeTransform, PartDef.Socket);` |
| ④ | `RefreshPartFramesInGunSpaceCache` `:3044` · `:3068` | `if (!WeaponMesh \|\| !GetWeaponRootPlacementInGunFrame(WeaponInGunFrame))` · `WeaponMesh->GetSocketTransform(Socket, RTS_Component)` | `if (!ActiveWeaponMesh \|\| !GetWeaponRootPlacementInGunFrame(WeaponInGunFrame))` · `ActiveWeaponMesh->GetSocketTransform(Socket, RTS_Component)` |
| ⑤ | `ProcessPendingWeaponPartsRebuild` `:3152` | `if (!Weapon \|\| !WeaponMesh \|\| ActiveWeaponMesh != WeaponMesh)` | `if (!Weapon \|\| !ActiveWeaponMesh)` |

주석 갱신(같은 diff): `:2527-2529`(①) · `:2447`("on the (skeletal) weapon mesh" → "on the active weapon mesh") · ②의 삭제된 주석은 루프 가드에 한 줄로("parts hang off whichever weapon mesh is shown; nothing shown = nothing to attach, but the re-resolves and cache refreshes below still run — the unequip path relies on them").

> **왜 ②는 조기 반환이면 안 되는가 (G1 1회차 P2-①, rev3 근거 정정).** `WeaponMesh` 는 생성자 기본 서브오브젝트(`:112`)라 `!WeaponMesh` 는 **살아 있는 캐릭터에서 항상 거짓**이다 → 오늘 `RebuildPartsFromSelection` 은 언이큅 경로(`:2297-2303` → `RefreshWeaponPartComponents(nullptr)` → `RebuildPartsFromSelection(빈 배열)`)에서도 **항상 꼬리까지 내려가** `RefreshHandGripInGunFrameCache()`(`:2689` → `:2888-2901`, `ComputeGripInGunFrame` 이 `!WeaponRoot` 로 false 를 돌려 두 `TOptional` 을 **비움**)와 `RefreshPartFramesInGunSpaceCache()`(`:2692` → `:3041` `Reset()`)를 실행한다. 언이큅은 `AttachWeaponMeshes`(`:1094`)도 `RefreshEquippedWeaponVisual` 끝(`:2522`)도 지나지 않으므로(`:3406-3410`) **이 꼬리가 유일한 초기화 지점**이고, 게터 `:2909` `:2932` `:2975` 는 라이브 재해석 없이 **캐시만 읽는다**. rev1 처럼 `if (!ActiveWeaponMesh) return;` 로 바꾸면 그 초기화가 사라진다 — 따라서 ②는 **"부착만" 가드**한다. 근거는 **오늘 실행 경로와의 동일성 + 불변식("무기 없음 = 그립·파츠 캐시 비어 있음") 보존**이다. (rev2 가 근거로 든 "소유 클라 OnRep 순서 역전 창"은 **철회** — 그 창에서는 장착 이력이 없어 캐시가 이미 비어 있으므로 해악을 증명하지 못한다, G1 2회차 P3-1.)
>
> **가드 진리표(G1 2회차가 전수 확인)**: 스켈레탈(`ActiveWeaponMesh==WeaponMesh`) ①②④⑤ 동일 객체·동일 분기 / Knife(정적, 파츠0) ① 은 `SelectParts`→빈→같은 결과, 시그니처 0→0, ⑤ 는 0==0 조기 반환 / Unarmed·언이큅(`ActiveWeaponMesh` null) ① 은 빈 재빌드+꼬리, ⑤ 조기 반환 — **전부 오늘과 동일.** `RebuildPartsFromSelection` 호출자는 `:2532`·`:3167` 둘뿐이고 둘 다 null 부모에 비어 있지 않은 `Selected` 를 넘기지 않으므로 루프 가드는 방어선일 뿐이다. `RefreshWeaponVisibility :837-843` 가 두 메시 모두 `bPropagateToChildren=true` → 정적 부모의 파츠도 스코프 숨김을 동반한다.

---

## 6. 함수별 계약

| 함수 | 권위 | 호출자 | 전제조건 | 실패 시 동작 |
|---|---|---|---|---|
| `RefreshWeaponPartComponents(Weapon)` | 로컬(각 머신) | `RefreshEquippedWeaponVisual` `:2448` — **`ActiveWeaponMesh` 대입(`:2348`) 이후** · 언이큅 `:2302`(null) | `ActiveWeaponMesh` 가 현재 표시 메시, 또는 null(언이큅) | `Weapon`/`ActiveWeaponMesh` null → `RebuildPartsFromSelection(빈)` + `LastWeaponPartSignature=0` (불변) |
| `RebuildPartsFromSelection(Selected)` | 로컬 | ① `RefreshWeaponPartComponents` ② `ProcessPendingWeaponPartsRebuild` | 없음 — **null 부모에서도 끝까지 실행된다.** `ActiveWeaponMesh` null 이면 부착 루프만 건너뛰고 해체·캐시 리셋·재해석 4곳·두 캐시 갱신은 실행 | 조기 반환 없음. 파츠 소켓이 부모에 없으면 **경고 없이 루트에 붙는다**(`SceneComponent.cpp:2946-2962`) — 스켈레탈과 동일 |
| `RefreshPartFramesInGunSpaceCache()` | 로컬 | `RebuildPartsFromSelection :2692` | `ActiveWeaponMesh` 유효 + `GetWeaponRootPlacementInGunFrame` 성공(`:2860` 이미 `ActiveWeaponMesh`) | `:3041` `Reset()` 뒤 조기 반환(불변) |
| `ProcessPendingWeaponPartsRebuild()` | 로컬 | 다음 틱 타이머(`:3144`) ← `NotifyEquippedWeaponModifiersChanged`(`:3125`) ← `UFPSRWeaponInstance::NotifyOwnerModifiersChanged`(`FPSRWeaponInstance.cpp:200`) ← `AddFragment`(`:86`)·`RemoveFragment`(`:101`)·**`OnRep_ActiveFragments`(`:164-167`, 원격 반쪽 — 시뮬레이티드 프록시 포함, 조건 없는 복제)** | 시그니처 비교 후 변경 시에만 재빌드 | 가드 실패 → 조기 반환(불변) |
| `FPSRWeaponPartSelector::SelectParts` | — | 위 두 함수 | **변경 없음** — `Weapon.WeaponParts` 를 DA 순서로 순회, 메시 타입을 보지 않는다(`FPSRWeaponPartSelector.cpp:42`) | — |

---

## 7. 복제표 (§6-3 서버권위 + Push Model)

| 프로퍼티 / RPC | 종류 | Push Model | 신뢰성 | 조건 | 비고 |
|---|---|---|---|---|---|
| (없음) | — | — | — | — | **이 유닛은 복제 표면을 만들지도 바꾸지도 않는다.** 파츠 입력은 `UFPSRWeaponInstance` 가 이미 복제한다(`ActiveFragments` `FPSRWeaponInstance.cpp:22`, `Slots`·`CurrentSlotIndex` `InventoryComponent.cpp:131-134` — 전부 조건 없음). `WeaponPartComponents`·`LastWeaponPartSignature`·`CachedPartFramesInGunSpace` 는 `Transient` 로컬 캐시(`FPSRCharacter.h:1224` `:1244-1245`). 리슨 호스트와 원격 클라 각자 **같은 입력에서 같은 파츠**를 만든다 |

---

## 8. 수명주기 · 소유권

- **생성 / 등록**: 불변. 파츠는 `NewObject<UStaticMeshComponent>(this)` → `RegisterComponent` → attach(`:2590-2610`). 부모만 `ActiveWeaponMesh`. 부착 루프가 **루프 전체** 기준으로 가드되므로 부모 없이 등록만 된 고아 컴포넌트는 생기지 않는다(가드를 `NewObject` 뒤에 넣는 변형은 #1 대조로 거른다).
- **부모의 수명**: `WeaponMesh`·`WeaponMeshStatic` 둘 다 **생성자 기본 서브오브젝트**(`:112` `:120`). `ActiveWeaponMesh` 는 그 둘 중 하나를 가리키는 `Transient` 포인터(`FPSRCharacter.h:996-997`), 무기 교체마다 `:2348` 에서 재대입.
- **해제**: 불변(`:2550-2570`). 파츠 소유자 = 캐릭터.
- **언이큅 캐시 초기화**: `RebuildPartsFromSelection(빈)` 의 꼬리 `:2689` `:2692` 가 유일한 지점. §5-2 ②가 이를 보존한다. 런타임 재현 불가 → #1 로 검증(목표 5).
- **GC / 델리게이트 / 재부착 경로**: 불변(`:1224-1225` · 없음 · `:1074-1086`).

---

## 9. 데이터드리븐 경계 (핵심원칙 2)

| 값 | 나가는 곳 | 기본값 | 비고 |
|---|---|---|---|
| 어느 파츠가 어느 소켓에 | `UFPSRWeaponDataAsset::WeaponParts` (Details「무기 › 모듈 파츠」) | — | **이 유닛은 읽기만.** 라이플은 Synty 파츠 7개가 `SOCKET_Mount_{Barrel,Handguard,Stock,Mag,Grip,Trigger,Reddot}_0` 에 저작(에디터 실측 2026-09-03), 하드서피스 몸통은 그중 **6개**를 같은 이름으로 갖는다(`ffa6d5ca`; `Trigger` 없음; **`Grip` 마운트는 몸통 원점**). 따라서 이 유닛이 들어가면 **Synty 파츠 6개가 마운트에, Trigger 1개가 몸통 원점에 붙는 중간 상태** — 예상 상태. `SM_RifleHS_*` 로의 교체는 사용자 DA 저작 |
| 진화 단계별 교체 | `WeaponParts[i].Stages`(`FFPSRWeaponPartStage`, `FPSRWeaponDataAsset.h:73-95`) / `EvolutionFragment`(`:145`) | — | 불변. 라이플 Reddot 슬롯 = `DA_Fragment_Rifle_SniperScope` 스택 시 `SM_Wep_Mod_Scope_09` |
| 프래그먼트 슬롯 상한 | `MaxFragmentSlots`(`:389`, 기본 3) | 3 | 불변 |

에셋 경로 하드코딩 없음.

---

## 10. 성능 예산 (핵심원칙 1)

- **틱**(rev3 정정): 이 유닛이 만지는 함수는 장착/수정 이벤트에서만 돈다. 다만 **라이플 소유자에게는 스켈레탈 무기와 동일한 파츠-커브 비용이 새로 발생**한다 — `ApplyWeaponPartCurves`(`:3073-3110`)가 오늘은 `WeaponPartComponents.Num()==0` 으로 즉시 반환하지만 변경 후 파츠 7 × 커브 6 = 42 해시 조회/프레임을 낸다. 플레이어 1인 기준이며 SMG 소유자가 오늘 이미 내는 것과 같은 급. 제1원리(적 스웜) 무관.
- **액터당 비용**: 적 스웜에 붙지 않는다.
- **복제 대역**: 0 바이트 추가(§7).

---

## 11. 미결정 항목 · 명세 갭 처리

**결정해 둔 것(구현자가 다시 묻지 않게)**
- ④ 의 게이트는 **명시적으로 `!ActiveWeaponMesh`** 를 남긴다(`:3068` 역참조의 전제를 같은 함수 안에).
- ①·⑤ 는 `ActiveWeaponMesh != WeaponMesh` 항을 **삭제**한다. `ActiveWeaponMesh == WeaponMeshStatic` 같은 **새 분기 추가 금지**.
- ② 는 **조기 반환 금지**(§5-2). 루프 가드만, 루프 전체 기준.

**후속 유닛으로 넘기는 것 (범위 밖 — 사용자가 알고 승인할 것)**
- **조립툴 정적 몸통 미지원** — `FPSRWeaponAssemblerViewportClient.cpp:94-96` · `Helpers.cpp:52-58`. 라이플 파츠 8종 DA 저작은 **Details 수기 입력**. 툴의 현행 소켓 명명 `SOCKET_Mount_<Base36Guid>`(`Helpers.cpp:115`) vs DA 의 `SOCKET_Mount_<Slot>_0` 드리프트 — 재베이크 시 이름이 바뀔 수 있다.
- **DA 검증기 정적 대칭화** — `FPSRWeaponDataAsset.cpp:197` `:306-308` `:370-372`(`SkelWeapon` 게이트). 라이플의 `SOCKET_Mount_Trigger_0` 원점 부착이 스켈레탈이었으면 경고, 정적에선 무음.
- **stale 주석** — `FPSRWeaponDataAsset.h:124` · `.cpp:190` · `FPSRCharacter.h:1251-1253`(오른손 누락, 기존).
- **④ 런타임 소비자 부재** — `FPSRGunMotionStudioData` 참조 콘텐츠 에셋 0개(2026-09-04 재실측 2회). 총모션 스튜디오 데이터가 저작되는 유닛의 검증 항목으로.
- ~~**`DA_Weapon_Rifle` 카드 배치 확인**(rev3, P3-2)~~ → **해소(에디터 실측 2026-09-04)**: `DA_CardModifiers_SniperScope` ∈ `UnlockableFeatures`. `WeaponCards` 에는 WeaponBehavior 카드가 없으므로 `FPSRWeaponValidator.cpp:50` 오류 상태 **아님**. ⓒ = `FPSR.Frag.Fill`. (교훈 재확인: 멤버십은 이름표로 판별 불가 — `[[uasset-strings-name-table-only]]`. rev2·rev3 가 그걸 어기고 단정했었다.)
- **범위 밖 발견(G1)**: `FPSRCardSubsystem.cpp:110` 주석과 `:134` 코드 불일치(티어 가진 WeaponBehavior 카드는 레벨업 풀에서 뽑힌다) / 데디서버는 장착 시 파츠를 만들지만 진화는 무시(`:3128`, 기존) / `AttachWeaponMeshes` 가 `ActiveWeaponMesh` 대입 전에 그립 캐시를 한 번 갱신(`:1094`→`:2522` 가 덮음, 기존).

**갭 처리 규칙(고정)**: 구현 중 명세에 없는 판단이 필요해지면 **추측해서 채우지 말고 멈추고 "명세 갭"으로 보고**한다. 위치는 줄 번호가 아니라 **표 5-2 의 (함수명, 코드 문자열)** 쌍으로 잡는다 — ①과 ⑤는 문자열이 같으므로 **함수명 안에서 유일하면 진행**(rev3, P3-3); 같은 함수 안에 두 번 나오면 갭으로 보고.

---

## 12. 검증 기준 (무엇을 통과라 부르는가)

| # | 검사 | 통과 조건 |
|---|---|---|
| 1 | 명세 대조 | `git diff` 가 **정확히** §5-2 의 5개 변경(②는 조기반환 삭제 + **루프 전체** 가드, 재해석 4곳·캐시 갱신 2줄 잔존) + §5-1 주석 2곳 + `:2527-2529`·`:2447`·②자리 주석. 그 외 변경 0. 헤더 시그니처 변경 0. **목표 5 의 유일한 검증**(런타임 재현 불가) |
| 2 | 빌드 | `Build.bat FPSRogueliteEditor Win64 Development -Project=... -WaitMutex -DisableAdaptiveUnity -ForceUnity` **Succeeded**, `[Adaptive Build] Excluded` 없음(G13). ⚠️ ①/⑤의 `!ActiveWeaponMesh` 를 `!WeaponMesh` 로 오타 내도, ④를 `WeaponMesh->` 로 남겨도 **초록** — 1번이 막는다 |
| 3 | 헤드리스 스모크 | `Automation RunTests FPSRoguelite.Smoke.ModuleLoads` 통과(`FPSRSmokeTest.cpp:8`). 이 변경의 결함은 못 잡는다 — 5·6 이 담당 |
| 4 | 레드팀 게이트(G2) | §6-6-1. **P1 잔존 시 머지 금지**. G1→G2 사이에 플랜에 없던 결정이 생기면 그 목록을 G2 프롬프트에 명시. 결과는 §13 |
| 5 | **회귀 — 스켈레탈 대조군** (사용자 PIE, 리슨 호스트) | **SMG**(AimSocket 있음)와 **Shotgun**(없음) 장착 → 파츠 7개가 **종전과 같은 자리·같은 방향**에 부착·ADS·총구화염 동일. 나이프·비무장 → 파츠 0. **변경 전 빌드에서 같은 장면의 스크린샷·로그를 먼저 떠 둔다**(7번 기준선) |
| 6 | **신규 동작 — 정적 라이플** (사용자 PIE, **리슨 호스트 + 원격 클라 양쪽**) | 아래 ⓐ~ⓕ |
| 7 | 로그 | (a) 스켈레탈 대조군: `AimSocket ... not found` 계열 경고(`:2466` `:2480`)가 기준선 대비 새로 생기지 않음. (b) **라이플: 기준선(변경 전)에는 `:2480` "AimSocket 'SOCKET_Aim' not found on WeaponMeshStatic" 이 있고, 변경 후에는 없어야 한다**(`Reddot_01` 이 `SOCKET_Aim` 을 제공) — 남아 있으면 파츠 미부착/조준 재해석 실패의 결정적 신호(rev3, P3-6) |

### 12-6 정적 라이플 세부
- **ⓐ 배치·방향** — 장착 시 Synty 파츠 **6개가 각 마운트 위치**에(총열 앞, 개머리판 뒤, 탄창 아래, 핸드가드 앞, 그립 아래, 레드닷 위), **Trigger 1개만 몸통 원점**(예상). 7개가 전부 원점이면 소켓 이름 불일치 = 실패. ⚠️ **Grip 마운트 = 몸통 원점**이라 `SOCKET_Mount_Grip_0` 불일치는 위치로 안 보인다 → **Grip 파츠의 방향**(Synty 규약 +Y 정면, 아래로 뻗음)을 육안 항목에 추가(rev3, P3-5).
- **ⓑ 원격 클라 반쪽 + 건-앵커 캐시 재해석**(rev3 라벨 정정 — 목표 5 검증 아님) — 원격 클라 화면에서 ⓐ 와 동일. 원격 클라 쪽에서 무기 스왑(라이플 ↔ SMG ↔ 라이플)을 해 **해체·재빌드와 건-앵커 그립/파츠 캐시가 새 무기 기준으로 재해석**되는지 본다(스왑 뒤 손 위치가 이전 무기에 남지 않음). ⚠️ 이 절차는 `Weapon` 이 항상 non-null 이라 **언이큅 분기(`:2297`)를 타지 않는다** — 목표 5 는 #1 로만 검증된다.
- **ⓒ 지연 재빌드(⑤) — 결정적 절차**(rev3 → **rev3a 에디터 실측 2026-09-04 로 확정**). 실측: `DA_CardModifiers_SniperScope`(`CardEffect_WeaponBehavior` → `DA_Fragment_Rifle_SniperScope`, `offer_rarities=[LEGENDARY]`)는 **`UnlockableFeatures`** 에 있다(`WeaponCards` 2장은 `DA_Card_MagSize_ThisWeapon`·`DA_Card_FireRate_ThisWeapon` = 스탯 카드). `MaxFragmentSlots=3`. Reddot 슬롯[6] = `EvolutionFragment=DA_Fragment_Rifle_SniperScope`, stage `FragmentStacks min_stacks=1 → SM_Wep_Mod_Scope_09`. → rev2·rev3 의 "WeaponCards 에 있다" 가정은 **틀렸다**(G1 2회차 P3-2 가 옳았다). `FPSRWeaponValidator.cpp:50` 오류 상태 우려는 **해당 없음**(검증기가 요구하는 배치 그대로).
  1. 호스트 콘솔 **`FPSR.Frag.Fill`** — `DebugFillFragmentSlots` 가 `UnlockableFeatures` 의 WeaponBehavior 카드를 모아(`PoolFrags` = `[SniperScope]`) Remove→Add 루프(`FPSRPlayerController.cpp:688-700`)로 **먼저 붙인다**(Cap 3 ≥ 1). 이어지는 `PoolCards.Num()(1) <= Cap(3)` 분기(`:711-715`)는 "교체 오퍼를 못 낸다"는 **경고 로그**를 남길 뿐, 추가는 이미 끝났다. 기대 로그: `[Frag] Fill: 'DA_Weapon_Rifle' now holds 1/3 distinct fragment(s):` / `slot[0] = DA_Fragment_Rifle_SniperScope` / 그 뒤 `cannot present a replacement offer` 경고(정상).
  2. `FPSR.DrawCards` 는 **쓰지 않는다** — 레벨업 풀(`WeaponCards`)에 이 카드가 없어 영원히 안 나온다(rev3 의 "3회 시도" 절차는 폐기).
  3. **기대**: 다음 틱 Reddot 슬롯이 `SM_Wep_Mod_Reddot_01` → `SM_Wep_Mod_Scope_09` 로 교체, **나머지 6개 파츠가 그대로**. 원격 클라(호스트 라이플을 관전하는 **시뮬레이티드 반쪽**)에서도 `OnRep_ActiveFragments`(`FPSRWeaponInstance.cpp:164-167`)로 같은 교체. ⚠️ 원격 클라 **자기** 라이플의 오너 반쪽은 콘솔로 불가(`Frag.Fill` 은 `HasAuthority()` 게이트 `:651`) — 실제 미션 해금 UI 로만.
  4. 제거 방향은 관측 불가 — `Frag.Fill` 재실행은 같은 집합을 Remove→Add 하므로 다음 틱 시그니처가 같아 `:3163` 에서 조기 반환(무변화가 정상). ⑤ 의 가드는 추가/제거 공통 경로이므로 3단계가 ⑤ 를 검증한다.
- **ⓓ 그립 해석** — 왼손이 **몸통 `SOCKET_LeftHand`** 로 떨어져 붙는다(Synty `Handguard_03` 에 좌손 소켓 없음; `ResolveLeftHandGripComponent` 폴백 `:2706`). 기존 폴백의 확인이지 ④ 검증이 아니다. 팔 ABP 의 좌손 IK 배선이 살아 있어야 관측 가능(미검증).
- **④ 런타임 미검증** — 소비자 0. 검증 = #1(`ActiveWeaponMesh->GetSocketTransform(Socket, RTS_Component)` 문자열).
- **ⓔ ADS**(rev3, P3-7) — 통과 기준: 조준 중 활성 사이트 파츠(`Reddot_01`/`Scope_09`)의 렌즈 중심이 화면 중앙 **±20px** 안. `ADSAimRotationOffset` 판정(`RifleHardSurface_ResumePrompt.md` §6-2)과 같이 본다.
- **ⓕ 총구화염** — `Barrel_04` 의 `SOCKET_Muzzle` 에서(`CachedMuzzleComponent`). 위치만 — 서버 트레이스는 `CachedMuzzleComponent` 를 쓰지 않아 게임플레이 영향 없음(`:3313` `:3862`).

### C0 실측치 (G1 이 전제를 검증할 수 있게 — §6-5-2 (3))
- **DA 전수(2026-09-04, `Scripts/run_census_weapon_da.bat`)**: 9종. 스켈레탈+파츠7 = Bazooka·ChargeLaser·LMG·SMG·Shotgun·Sniper(대조군; Shotgun `aim_socket None`) / 정적+파츠0 = Knife / 메시없음+파츠0 = Unarmed / **정적+파츠7+ADS+`SOCKET_Aim` = Rifle 1건**.
- **DA 파츠 소켓명(에디터 실측 2026-09-03)**: 라이플 `weapon_parts[i].socket` = `SOCKET_Mount_{Barrel,Handguard,Stock,Mag,Grip,Trigger,Reddot}_0`(`_0` 포함, 직접 읽음).
- **몸통 소켓(2026-09-04, `ffa6d5ca`)**: `SOCKET_Mount_{Stock,Grip,Mag,Handguard,Barrel,Reddot}_0` + 양손 2 = 8개(`find_socket` 재조회). `Grip` 마운트 = (0,0,0). 총열 `SOCKET_Muzzle`, 조준경 2종 `SOCKET_Aim`. 몸통에 `SOCKET_Aim`·`SOCKET_Mount_Trigger` 없음(이름표).
- **Synty 파츠 이름표(2026-09-04)**: `Reddot_01`·`Scope_09` 만 `SOCKET_Aim`, `Barrel_04` 만 `SOCKET_Muzzle`, 어느 파츠에도 `SOCKET_LeftHand/RightHand` 없음.
- **런타임(사용자 PIE 2026-09-04)**: 정적 몸통이 `WeaponMeshStatic` 으로 위치·방향 정상(파츠 0). 로그에 `:2480` 경고가 있어야 정상(7-b 기준선).
- **호출 순서(코드)**: `ActiveWeaponMesh` 대입 `:2348` → `RefreshWeaponPartComponents` `:2448`. 언이큅 `:2272`(null) → `:2302`. `WeaponMesh` = `:112` 기본 서브오브젝트(항상 non-null). `RebuildPartsFromSelection` 호출자 = `:2532`·`:3167` 뿐.
- **엔진**: `StaticMeshComponent.cpp:1334` `:1365` `:1389-1392` · `SceneComponent.cpp:2312`(부모 null) · `:2295-2420`(소켓 부재 경고 없음) · `:2946-2962`(원점 폴백) · `:114`(기본 Movable).
- **복제**: `Slots`·`CurrentSlotIndex`(`InventoryComponent.cpp:131-134`)·`ActiveFragments`(`WeaponInstance.cpp:22`) 조건 없음.
- **④ 소비자**: `FPSRGunMotionStudioData` 참조 콘텐츠 에셋 **0개**(2회 재실측). 게이트 `FPSRFirstPersonArmsAnimInstance.cpp:168-171`.
- **카드 배치(에디터 실측 2026-09-04)**: `DA_Weapon_Rifle.UnlockableFeatures` = [`DA_CardModifiers_SniperScope`](`CardEffect_WeaponBehavior` → `DA_Fragment_Rifle_SniperScope`, `offer_rarities=[LEGENDARY]`, `weight=0`) / `WeaponCards` = [`DA_Card_MagSize_ThisWeapon`, `DA_Card_FireRate_ThisWeapon`](스탯 카드, WeaponBehavior 없음) / `MaxFragmentSlots=3` / Reddot 슬롯 stage `FragmentStacks min_stacks=1 → SM_Wep_Mod_Scope_09`, 나머지 6슬롯 `EvolutionFragment=None`. (`DA_CardUnlock_Rifle` 은 이 DA 의 어느 배열에도 없다 — 이름표에 보였던 건 다른 참조였다.)
- **미검증**: 없음(`Card->Weight` 도 0 으로 읽힘 — `Frag.Fill` 경로는 가중치를 쓰지 않는다).

---

## 13. 레드팀 지적 원장

> `Workflow.md` §6-6-1. **기각엔 근거가 필요하다** — 제1원리 조항 / 코드 인용 / 실측치 중 하나.

### G1 1회차 (2026-09-04) — **반려**(P2 3·P3 4) → rev2

| 심각도 | 지적 (요약 + 파일:줄) | 처리 | 근거 |
|---|---|---|---|
| P2 | ② 를 `!ActiveWeaponMesh` 조기 반환으로 바꾸면 죽어 있던 가드가 살아나 **언이큅 경로의 캐시 초기화(`:2689` `:2692`)가 건너뛰어진다** — `:2573` · `:3406-3410` | **수용** | `WeaponMesh` 는 `:112` 기본 서브오브젝트 → 꼬리가 오늘 항상 실행됨을 코드로 확인. ② = 조기 반환 삭제 + 루프 가드 |
| P2 | §4 "에디터 모듈에 스켈레탈 가정 없음"은 거짓 — `FPSRWeaponAssemblerViewportClient.cpp:94-96` · `Helpers.cpp:52-58` | **수용** | §4 철회 + §3-3 완화 + §11 |
| P2 | §12 ⓓ "④ 검증" 오라벨 — ④ 소비자 0개 | **수용** | `grep -rl -a` 0개. ⓓ 라벨 정정, ④ 런타임 미검증 |
| P3 | DA 검증기 스켈레탈 전용(`FPSRWeaponDataAsset.cpp:197` `:306-308` `:370-372`) | **수용(후속)** | §11 |
| P3 | stale 주석 4곳 | **부분 수용** | `FPSRCharacter.*` 2곳 §5. DataAsset 2곳 §11(파일 범위) |
| P3 | §12-6 ⓐ "7개 부착"은 개수라 전부 원점이어도 통과 | **수용** | 배치 기준으로. `_0` 접미는 에디터 실측 C0 |
| P3 | §12-6 ⓒ 결정적 경로 부재 — `Frag.Fill/Replace` 제안 | **수용(경로 정정, rev3 에서 재정정)** | rev2 는 "라이플에서 안 돈다"로 대체했으나 그 근거가 이름표 추정 + `:711-715` 오독이었다(2회차 P3-2) → rev3 유계 절차 |
| — | 범위 밖: 빌드 플래그 모순 · 조립툴 소켓 명명 드리프트 · `:1094`/`:2522` 순서 | **수용/기록** | 플래그 정정 `a7a5ae30`. 나머지 §11 |

### G1 2회차 (2026-09-04, rev2 `a7a5ae30`) — **반려**(P2 1·P3 8) → rev3. *"설계 자체의 결함은 못 찾았다 … rev3 는 Opus 대조만으로 승인 단계로 가도 무방"*

| 심각도 | 지적 (요약 + 파일:줄) | 처리 | 근거 |
|---|---|---|---|
| P2 | §12-6 ⓑ(스왑)는 목표 5 를 검증하지 못한다 — `Weapon` non-null 이라 `:2297` 언이큅 분기를 안 탐; `!Weapon` 경로를 결정적으로 만들 수단 없음(`EquipSlot :309-312`, `CurrentSlotIndex` 되돌림 0건) | **수용** | 코드 인용 확인. ⓑ 라벨 → "원격 반쪽 + 건-앵커 캐시 재해석", 목표 5 = #1 로만 검증(§2·§8·§12 명시). 디버그 명령 추가는 비목표 |
| P3-1 | §5-2 박스 "OnRep 순서 역전 창"은 캐시가 이미 빈 창이라 해악을 증명 못함 | **수용** | 근거를 "오늘 동작과의 동일성 + 불변식 보존"으로 교체 |
| P3-2 | ⓒ 근거 2건 불성립 — 카드 멤버십은 이름표로 판별 불가(`WeaponCards`·`UnlockableFeatures` 둘 다 이름표에 존재) · `:711-715` 는 거부 게이트 아님(Remove/Add `:690` `:699` 가 먼저) · `FPSRWeaponValidator.cpp:50` 은 `WeaponCards` 의 WeaponBehavior 를 Error 로 봄 | **수용** | 멤버십 = §11 에디터 실측 항목으로 격하. ⓒ = `DrawCards` 3회 → `Frag.Fill` 유계 절차. 검증기 오류 가능성 §11 기록 |
| P3-3 | 갭 규칙 자기모순 — ①·⑤ 문자열 동일 | **수용** | "(함수명, 문자열) 쌍, 함수 안에서 유일하면 진행" |
| P3-4 | "재해석 3곳"→4곳(`:2618` `:2636` `:2657` `:2673`) | **수용** | §5-2·§6 정정 |
| P3-5 | ⓐ 가 Grip 소켓명 불일치를 못 잡음(Grip 마운트 = 원점) | **수용** | 방향 육안 항목 추가 |
| P3-6 | #7 이 라이플 `:2480` 경고 소멸 신호를 버림 | **수용** | 7-(b) 추가 |
| P3-7 | ⓔ 통과 기준 부재 | **수용** | ±20px 기준 |
| P3-8 | §10 "틱 변화 없음" 부정확 — `ApplyWeaponPartCurves` 42 조회/프레임 신규 | **수용** | §10 정정 |
| — | 범위 밖: `FPSRCardSubsystem.cpp:110` 주석/코드 불일치 · `FPSRCharacter.h:1251-1253` 오른손 누락 · 데디서버 진화 무시(`:3128`) | **기록** | §11 |

- **G1 에 무엇을 줬나(1·2회차 공통)**: 명세 · 코드 경로 · 엔진 소스 경로 · `Docs/InternalRedTeamReview.md`. 설계 변호 없음. 2회차는 "1회차 처리가 해소됐는가 + ②의 새 형태가 새 결함을 들여왔는가"를 초점으로 명시.
- **G1 3회차**: **태우지 않음** — 2회차 잔여 P2 가 검증표 문구 1건이고 리뷰어가 승인 단계 진행을 권고. §6-5-2 (5) "3회차부터 사용자 보고" 규칙에 따라 이 문서로 보고하고 사용자 승인으로 넘긴다.

### C2 · C3 진행 (2026-09-04)

| 단계 | 결과 |
|---|---|
| C2 Sonnet 축자 구현 | 완료 — 갭 0건. `FPSRCharacter.cpp` +48/−49, `.h` 3줄. 커밋·빌드·`git add` 없음(지시대로) |
| C3 #1 명세 대조 | **통과** — ①⑤ 가드 문자열 일치 · ② 조기 반환 삭제 + 루프 전체(`NewObject`~`MakePartCurveNames`) `if (ActiveWeaponMesh)` 안, 재해석 4곳(`:2618` `:2636` `:2657` `:2673`)·`RefreshHandGripInGunFrameCache`·`RefreshPartFramesInGunSpaceCache` 는 가드 밖 잔존 → **목표 5 검증 완료** · ③ 부모 `ActiveWeaponMesh` · ④ 가드+`GetSocketTransform` 둘 다 · `.h` 주석 2곳 §5-1 과 동일 · `.cpp:2447` 주석 · 그 외 hunk 0(큰 +/− 는 루프 본문 재들여쓰기, 내용 줄 단위 동일) · 헤더 시그니처 변경 0. **플랜에 없던 구조 결정 = 0건**(②자리 주석을 1줄→2줄로 래핑한 서식 판단 1건뿐 — G2 프롬프트에 명시 대상 아님) |
| 기준선 캡처(§12-5·#7 의 비교 대상) | **생략 — 사용자 결정 2026-09-04.** 결과: §12-7(a) 대조군 로그 판정은 비교 대상 없음 → 판정 항목에서 제외, §12-5 스켈레탈 회귀는 **육안(종전 알려진 정상 배치 대비)** 으로만, §12-7(b) 라이플 `:2480` 경고는 **"변경 후 없음"** 만 확인(기준선의 "있음"은 미확인) |
| C3 #2 빌드 | **통과** — `Build.bat FPSRogueliteEditor Win64 Development -WaitMutex -DisableAdaptiveUnity -ForceUnity` → 로그 `Result: Succeeded`(450s, XGE) · `[Adaptive Build] Excluded` **0** · `error C`/`LNK` **0**. 판정은 종료 코드가 아니라 로그 줄 |
| C3 #3 스모크 | **통과** — `Scripts/run_smoke_moduleloads.bat` → `Test Completed. Result={Success} Name={ModuleLoads}` · `1 tests performed` · `TestExit: Automation Test Queue Empty` (`Saved/wpn1_smoke.log`) |
| 코드 커밋 | `e181d137` feat(weapon): WPN1 — 두 파일만, 명세 대조·빌드·스모크 통과 후 |
| §11 카드 배치 에디터 실측 | **완료 2026-09-04** — `DA_CardModifiers_SniperScope` ∈ **`UnlockableFeatures`**(1장, LEGENDARY) / `WeaponCards` = 스탯 카드 2장 / `MaxFragmentSlots=3` / Reddot 슬롯 stage `min_stacks=1 → SM_Wep_Mod_Scope_09`. → ⓒ = **`FPSR.Frag.Fill`** 확정(`DrawCards` 폐기). rev2·rev3 의 "WeaponCards" 가정은 틀렸었다(G1 2회차 P3-2 적중) |
| §12-5/6 사용자 PIE | (대기 — 리슨 호스트 + 원격 클라) |
| 사용자 PIE #1 (리슨 호스트, 2026-09-04) | **부분 통과** — ⓐ 파츠 배치 ✓(파츠 붙음) · ⓒ 스코프 교체 ✓(`SM_Wep_Mod_Scope_09`) · **손 방향 이상 관측** → 원인 = WPN1 코드가 아니라 **소켓 데이터 누락**: HS 몸통 `SOCKET_LeftHand/RightHand` 회전이 (0,0,0) — Synty 몸통 `SK_Wep_Mod_A_Body_01` 손 소켓은 회전을 갖는데(L P80/Y180/R−90 · R P−70.1/Y−52.4/R−35.9, 본 공간) `ffa6d5ca` 저작이 위치만 찍었다. 손 IK 는 소켓 **전체 트랜스폼**(회전 포함)을 쓰므로 identity 회전 = 손바닥 방향 붕괴. 수정 = Synty 회전을 기저 변환(`Te = Minv·Ts·M`)해 이식 `2fbc6af4`, 데이터 경로 고정 = 생성기 `BODY_SOCKET_ROTATIONS` → manifest `body_socket_rotations` → 소켓 스크립트(add/upd 회전 적용·verify 위치+회전) `6b0fbb2b`, 에디터 라운드트립 12/12 `sockets_ok=True`. **재판정 대기**(손 방향 정상 여부 · 위치 오차 cm — 위치는 HS 형상 기준 `BODY_SOCKETS` 값 유지). ⓑ · ⓓ · ⓔ · ⓕ · 로그 `:2480` 경고 부재 · 대조군(SMG·Shotgun·나이프·비무장) = **미판정** |
| G2 머지 게이트 | (PIE 뒤) |

- **레드팀(G2)에 무엇을 줬나**: (C3 후 기입)
