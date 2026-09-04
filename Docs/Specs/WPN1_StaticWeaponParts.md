# WPN1 — 정적 무기 메시에도 모듈러 파츠를 붙인다 (파츠 부모 = `ActiveWeaponMesh`)

## 1. 메타

| 항목 | 값 |
|---|---|
| 유닛 ID / 이름 | `WPN1` / 정적 무기 메시의 모듈러 파츠 부착 — 파츠 부모를 `WeaponMesh`(스켈레탈 전용)에서 `ActiveWeaponMesh`(스켈레탈·정적 공용)로 |
| 브랜치 | `proto/arcade-look` (라이플 하드서피스 트랙의 하위 유닛. 머지 단위는 트랙 전체) |
| 작성 모델 | `claude-fable-5-1` — ⚠️ 폴백 표기: §6-5-2 는 C1 설계 주체를 Opus 로 두지만, 이 세션은 사용자가 `/model` 로 Fable 로 전환한 상태에서 진행됐다(2026-09-04). G1 게이트는 **별도 Fable 서브에이전트**로 띄워 설계자≠검증자를 유지한다 |
| 작성일 / 최종 갱신 | 2026-09-04 (**rev2 — G1 1회차 반려 반영, 2회차 제출본**) |
| 상태 | `초안` — G1 2회차 대기 |
| 관련 SSOT | `Docs/RifleHardSurface_ResumePrompt.md` §1-2·§2-4·§3-2 · `Docs/WeaponPack_Integration.md`(총구=파트 소켓·조준=사이트 파트 소켓 규약) · `Docs/SSOT/Workflow.md` §6-5-2 |
| 관련 메모리 | `[[reason-in-multiplayer-terms]]` · `[[code-is-immutable-structure-only]]` · `[[production-structure-first]]` · `[[event-halves-authority-vs-client]]` · `[[card-pool-routing]]` |
| 보드 행 | 「아케이드 룩 프로토 + 육안 게이트」 `3d03972d-dd88-81ff-87d6-f5730a06e7a4` 스코프 2/4 |

> **rev2 변경 요약 (G1 1회차 반려 반영, 원장 = §13)** — **P2-①** ②의 형태 정정: `:2573` 의 `!WeaponMesh` 는 기본 서브오브젝트라 **항상 거짓인 죽은 가드**였고, 이를 `!ActiveWeaponMesh` 조기 반환으로 바꾸면 **언이큅 경로에서 살아나** 꼬리의 그립·파츠 캐시 초기화(`:2689` `:2692`)를 건너뛴다 → 조기 반환을 **삭제**하고 부착 루프만 가드한다. §5-2 ⚠️박스의 "좁아질 뿐"을 ①·⑤ 한정으로 정정. **P2-②** §4 "에디터 모듈에 가정 없음" 철회 — 조립툴(`FPSRWeaponAssembler*`)은 스켈레탈 몸통 전용이다 → §11 후속으로 명시, §3-3 의 "다음 단계는 전부 이 위에 선다" 완화. **P2-③** ④(`RefreshPartFramesInGunSpaceCache`)는 **현 콘텐츠에 런타임 소비자가 0**(`FPSRGunMotionStudioData` 를 가진 에셋 0개) → §12 ⓓ 라벨 정정, ④ 는 코드 대조로만 검증하고 "런타임 미검증"으로 명시. **P3** 4건: 검증기 스켈레탈 전용(§11) · stale 주석 2곳 §5 목록 추가(`.h:1221-1223` `.cpp:2447`) · ⓐ 를 개수→**배치** 기준으로 · ⓒ 에 결정적 실행 경로(단, 리뷰어가 제안한 `FPSR.Frag.Fill/Replace` 는 **라이플에서 안 돈다** — 아래 §12-6 ⓒ 참조) · 원격 클라 무기 스왑 ⓑ · 로그 기준선 ⓖ.

---

## 2. 목표 / 비목표

### 목표
끝나면 **`WeaponMeshStatic` 으로 표시되는 무기도 `WeaponParts` 슬롯의 파츠를 붙인다** — 스켈레탈 무기와 같은 규칙(같은 선택기, 같은 소켓 규약, 같은 재빌드 수명주기)으로.

동작 기준:
1. 정적 무기(`WeaponMesh` 비움 · `WeaponMeshStatic` 설정)가 장착되면 `WeaponParts` 의 파츠가 **몸통 정적 메시의 소켓**에 붙는다.
2. 프래그먼트(진화) 변경으로 파츠 선택이 바뀌면 **다음-틱 코얼레스 경로**(`ProcessPendingWeaponPartsRebuild`)가 정적 무기에서도 재빌드한다. (장착 시 즉시 경로 = `RefreshWeaponPartComponents`. rev1 은 "즉시/지연 둘 다"를 프래그먼트 경로로 오기했다 — 프래그먼트 변경은 항상 지연 경로 하나다, `:3125-3146`.)
3. 파츠가 붙은 뒤 **좌/우손 그립 해석**이 파츠 소켓 → 리시버 순으로 떨어진다(`ResolveLeft/RightHandGripComponent`, `:2695-2722`) — 스켈레탈과 동일.
4. 스켈레탈 무기 6종의 동작은 **변화 0** (회귀 대조군 = §12-5).
5. **언이큅 경로의 캐시 초기화가 그대로 유지된다**(rev2 신설 — P2-①): 무기를 내려놓으면 그립 캐시·파츠 프레임 캐시가 오늘처럼 비워진다.

### 비목표 (구현자가 "친절하게" 채우면 안 되는 것)
- **무기 자체 AnimBP(노리쇠·재장전 몽타주) 경로는 손대지 않는다.** `FPSRCharacter.cpp:3221` `:3272` `:3869` 의 `WeaponMesh->GetAnimInstance()` 는 정적 무기에서 이미 null no-op 다(`PlayScaledReload` 람다 `:3195-3199` 가 null 검사 — G1 재확인). 정적 무기에 몽타주를 "대신" 붙이려 하지 말 것.
- **`DA_Weapon_Rifle` 의 `WeaponParts` 를 새 메시(`SM_RifleHS_*`)로 바꾸지 않는다.** DA 값 저작은 사용자 작업(`[[da-edits-are-user-work]]`). 이 유닛이 끝나면 라이플에는 **아직 Synty 파츠**가 붙는 게 맞다(§9).
- **소켓 이름·규약을 바꾸지 않는다.** `SOCKET_Mount_*_0`·`SOCKET_Aim`·`SOCKET_Muzzle`·`SOCKET_LeftHand`·`SOCKET_RightHand` 그대로.
- **헤더의 시그니처·필드 추가/변경 금지.** 바뀌는 헤더 내용은 **주석 2곳**뿐이다(§5-1).
- **`RefreshWeaponPartComponents` 를 호출하는 지점을 늘리거나 줄이지 않는다.** 호출 그래프는 그대로, 가드 조건만.
- **정적 무기 파츠에 복제·저장을 붙이지 않는다.** 파츠는 지금도 복제되지 않는 로컬 코스메틱이다(§7). 그대로.
- **다른 정적 무기(나이프·비무장)에 파츠를 저작하지 않는다.** 이 유닛은 코드만.
- **조립툴(`FPSRogueliteEditor/Assembler`)·DA 검증기(`IsDataValid`)의 정적 몸통 대응은 이 유닛이 하지 않는다** — §11 후속. (rev2: 존재를 숨기지 않고 여기 못 박는다.)
- **`FPSRWeaponDataAsset.h:124` 등 다른 파일의 stale 주석은 손대지 않는다** — §11 후속. 이 유닛의 diff 는 `FPSRCharacter.cpp/.h` 두 파일로 닫는다.

---

## 3. 제1원리 3줄 (핵심원칙 4)

1. **제1원리 근거** — 이 코드는 **플레이어(1~4)** 의 무기에만 돈다. 적 스웜 경로와 무관하고, 액터당 비용은 파츠 컴포넌트 ≤7개 × 플레이어 ≤4 로 지금과 같다. 서버권위: 파츠 선택의 입력(스탯·프래그먼트)은 **이미 복제되는 상태**이고 파츠 자체는 각 머신이 로컬로 재생성한다 — 이 유닛은 그 입력도 출력도 바꾸지 않는다.
2. **엔진 기본값·기존 인프라와의 관계** — **기존 인프라를 그대로 쓴다.** 파츠는 이미 `UStaticMeshComponent` 로 생성돼 `AttachToComponent(Parent, KeepRelativeTransform, SocketName)` 으로 붙는다(`:2600` `:2608`). 바뀌는 것은 **부모 하나**다. 정적 부모에서 필요한 두 API 는 엔진이 제공한다: `UStaticMeshComponent::DoesSocketExist`(`StaticMeshComponent.cpp:1334`) · `UStaticMeshComponent::GetSocketTransform(Name, RTS_Component)`(`:1365`, `RTS_Component` 분기 `:1389-1392`). 둘 다 `UMeshComponent`/`USceneComponent` 가상 인터페이스라 `ActiveWeaponMesh`(`TObjectPtr<UMeshComponent>`, `FPSRCharacter.h:997`)로 호출하면 스켈레탈/정적 어느 쪽이든 자기 구현으로 간다. `AttachToComponent` 는 부모 null 을 스스로 걸러낸다(`SceneComponent.cpp:2312` `if(Parent != nullptr)`). 엔진 기본값을 덮는 게 아니라 **엔진이 이미 다형으로 제공하는 것을 쓰지 않고 있던 것을 쓰게 하는 것**이다.
3. **프로젝트 제약과의 정합** — 무기 조립 규약(총구=파트 소켓, 조준=사이트 파트 소켓, 좌/우손=파트 또는 리시버)은 **런타임 쪽**에서는 이미 `ActiveWeaponMesh` 폴백으로 짜여 있다(`:2472-2476` 조준, `:2618-2632` 총구, `:2695-2722` 양손, `GetWeaponRootPlacementInGunFrame :2860` `WeaponRoot = ActiveWeaponMesh`). **파츠 부착·프레임 캐시 4곳만 `WeaponMesh` 를 직접 잡고 있다** — 그 비대칭을 없앤다. ⚠️ **에디터·검증기 쪽은 여전히 스켈레탈 전용이다**(§11) — 이 유닛은 *런타임* 비대칭만 닫고, 저작 도구의 비대칭은 후속 유닛에 넘긴다. 따라서 "다음 단계가 전부 이 위에 선다"가 아니라 **"런타임 파츠 표시는 이 위에 서고, DA 저작은 당분간 Details 수기"** 다.

---

## 4. 파일 목록

| 경로 | 신규/수정 | 한 줄 설명 |
|---|---|---|
| `Source/FPSRoguelite/Private/Hero/FPSRCharacter.cpp` | 수정 | 5개 지점의 파츠 부모·가드를 `ActiveWeaponMesh` 기준으로 (§5-2) + 주석 3곳 |
| `Source/FPSRoguelite/Public/Hero/FPSRCharacter.h` | 수정 | 주석 **2곳** — `RefreshWeaponPartComponents` 선언(`:1247-1248`)·`WeaponPartComponents` 멤버(`:1221-1223`). 시그니처 불변 |

그 외 파일 변경 없음.

🔁 **rev1 의 "전수 grep — 스켈레탈 전용 가정은 이 두 파일 5건뿐" 은 철회한다**(G1 P2-②). 그 grep 은 리터럴 4종 검색이었지 *가정* 검색이 아니었다. 실제로 남아 있는 스켈레탈 전용 지점(이 유닛 **범위 밖**, §11):
- 조립툴: `FPSRogueliteEditor/Private/Assembler/FPSRWeaponAssemblerViewportClient.cpp:94-96`(`DA->WeaponMesh` 를 `USkeletalMeshComponent` 에 올림) · `FPSRWeaponAssemblerHelpers.cpp:52-58`(`BakeSockets` 가 `USkeletalMesh* Body` null 이면 0 반환)
- DA 검증기: `FPSRWeaponDataAsset.cpp:197`(파츠 소켓) · `:306-308`(총구) · `:370-372`(조준) — 전부 `SkelWeapon` 게이트
- 주석: `FPSRWeaponDataAsset.h:124`("Socket on the WEAPON mesh (SKEL_LPAMG_<W>)") · `FPSRWeaponDataAsset.cpp:190`
이 유닛 안에서 고치는 주석 = `FPSRCharacter.cpp/.h` 의 것만(§5).

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
| ② | `RebuildPartsFromSelection` `:2572-2576` | `// Parts attach to the skeletal weapon mesh only …`<br>`if (!WeaponMesh) { return; }` | **조기 반환 블록 삭제.** 대신 부착 루프(`for (const FFPSRWeaponPartAttachment& PartDef : Selected)` `:2583`)를 `if (ActiveWeaponMesh) { … }` 로 감싼다. 루프 뒤의 재해석 3곳·`RefreshHandGripInGunFrameCache()`·`RefreshPartFramesInGunSpaceCache()`(`:2618-2692`)는 **항상** 실행된다 |
| ③ | `RebuildPartsFromSelection` `:2608` | `PartComp->AttachToComponent(WeaponMesh, FAttachmentTransformRules::KeepRelativeTransform, PartDef.Socket);` | `PartComp->AttachToComponent(ActiveWeaponMesh, FAttachmentTransformRules::KeepRelativeTransform, PartDef.Socket);` |
| ④ | `RefreshPartFramesInGunSpaceCache` `:3044` · `:3068` | `if (!WeaponMesh \|\| !GetWeaponRootPlacementInGunFrame(WeaponInGunFrame))` · `WeaponMesh->GetSocketTransform(Socket, RTS_Component)` | `if (!ActiveWeaponMesh \|\| !GetWeaponRootPlacementInGunFrame(WeaponInGunFrame))` · `ActiveWeaponMesh->GetSocketTransform(Socket, RTS_Component)` |
| ⑤ | `ProcessPendingWeaponPartsRebuild` `:3152` | `if (!Weapon \|\| !WeaponMesh \|\| ActiveWeaponMesh != WeaponMesh)` | `if (!Weapon \|\| !ActiveWeaponMesh)` |

주석 갱신(같은 diff): `:2527-2529`(①) · `:2447`("on the (skeletal) weapon mesh" → "on the active weapon mesh") · ②의 삭제된 주석은 루프 가드에 한 줄로("parts hang off whichever weapon mesh is shown; nothing shown = nothing to attach, but the cache refreshes below still run — the unequip path relies on it").

> 🔁 **rev2 — 왜 ②는 조기 반환이면 안 되는가 (G1 P2-①).** `WeaponMesh` 는 생성자 기본 서브오브젝트(`:112`)라 `!WeaponMesh` 는 **살아 있는 캐릭터에서 항상 거짓**이다 → 오늘 `RebuildPartsFromSelection` 은 언이큅 경로(`:2297-2303` → `RefreshWeaponPartComponents(nullptr)` → `RebuildPartsFromSelection(빈 배열)`)에서도 **항상 꼬리까지 내려가** `RefreshHandGripInGunFrameCache()`(`:2689`)와 `RefreshPartFramesInGunSpaceCache()`(`:2692` → `:3041` `Reset()`)를 실행한다. 언이큅은 `AttachWeaponMeshes`(`:1094`)도 `RefreshEquippedWeaponVisual` 끝(`:2522`)도 지나지 않으므로(`:3406-3410` 주석: *"!ActiveWeaponMesh is the UNEQUIP path, which does NOT go through AttachWeaponMeshes … stale grip cache the arms would then reach for bare-handed"*) **이 꼬리가 유일한 초기화 지점**이다. rev1 처럼 `if (!ActiveWeaponMesh) return;` 로 바꾸면 그 초기화가 사라진다. 도달 가능한 창 = 소유 원격 클라에서 `OnRep_CurrentSlotIndex` 가 슬롯 인스턴스보다 먼저 오는 순서 역전(`FPSRWeaponInventoryComponent.cpp:460-464` 가 명시). 따라서 ②는 **"부착만" 가드**한다. 부착 루프 안의 `AttachToComponent` 는 부모 null 을 스스로 걸러내지만(`SceneComponent.cpp:2312`) `NewObject`·`RegisterComponent` 는 실행되므로 루프 전체를 가드해야 고아 컴포넌트가 안 생긴다.
>
> ⚠️ **①·⑤ 의 "좁아짐"은 그대로 참이다**: 두 가드가 참이 되는 집합이 "스켈레탈 아님"에서 "표시 메시 없음"으로 **좁아진다**. `ActiveWeaponMesh` 가 null 인 조건(`:2348` 대입 규칙) = 스켈레탈도 정적도 로드되지 않았을 때 = `:2272` 언이큅. 그 경우 ① 은 종전대로 `RebuildPartsFromSelection(빈)` + `LastWeaponPartSignature=0` 을 실행하고(②가 꼬리를 살려 두므로 캐시 초기화도 종전대로), ⑤ 는 종전대로 조기 반환한다.

---

## 6. 함수별 계약

| 함수 | 권위 | 호출자 | 전제조건 | 실패 시 동작 |
|---|---|---|---|---|
| `RefreshWeaponPartComponents(Weapon)` | 로컬(각 머신) | `RefreshEquippedWeaponVisual` `:2448` — **`ActiveWeaponMesh` 대입(`:2348`) 이후** · 언이큅 `:2302`(null) | `ActiveWeaponMesh` 가 현재 표시 메시(스켈레탈 또는 정적), 또는 null(언이큅) | `Weapon`/`ActiveWeaponMesh` null → `RebuildPartsFromSelection(빈)` + `LastWeaponPartSignature=0` (불변) |
| `RebuildPartsFromSelection(Selected)` | 로컬 | ① `RefreshWeaponPartComponents` ② `ProcessPendingWeaponPartsRebuild` | 없음 — **null 부모에서도 끝까지 실행된다**(rev2). `ActiveWeaponMesh` null 이면 부착 루프만 건너뛰고 해체·캐시 리셋·재해석·두 캐시 갱신은 실행 | 해당 없음(조기 반환 없음). 파츠 소켓이 부모에 없으면 `AttachToComponent` 가 **경고 없이 루트에 붙는다**(`USceneComponent::GetSocketTransform` 폴백 `SceneComponent.cpp:2946-2962` — G1 재확인) — 스켈레탈과 동일, 이 유닛은 바꾸지 않는다 |
| `RefreshPartFramesInGunSpaceCache()` | 로컬 | `RebuildPartsFromSelection :2692` | `ActiveWeaponMesh` 유효 + `GetWeaponRootPlacementInGunFrame` 성공(내부에서 이미 `ActiveWeaponMesh` 사용 `:2860`) | `:3041` `Reset()` 뒤 조기 반환(불변) |
| `ProcessPendingWeaponPartsRebuild()` | 로컬 | 다음 틱 타이머(`:3144`) ← `NotifyEquippedWeaponModifiersChanged`(`:3125`) ← `UFPSRWeaponInstance::NotifyOwnerModifiersChanged`(`FPSRWeaponInstance.cpp:200`) ← `AddFragment`(`:86`)·`RemoveFragment`(`:101`)·**`OnRep_ActiveFragments`(`:164-167`, 원격 클라 반쪽)** | 시그니처 비교 후 변경 시에만 재빌드 | 가드 실패 → 조기 반환(불변) |
| `FPSRWeaponPartSelector::SelectParts` | — | 위 두 함수 | **변경 없음** — `Weapon.WeaponParts` 를 DA 순서로 순회, 메시 타입을 보지 않는다(`FPSRWeaponPartSelector.cpp:42`) | — |

---

## 7. 복제표 (§6-3 서버권위 + Push Model)

| 프로퍼티 / RPC | 종류 | Push Model | 신뢰성 | 조건 | 비고 |
|---|---|---|---|---|---|
| (없음) | — | — | — | — | **이 유닛은 복제 표면을 만들지도 바꾸지도 않는다.** 파츠 입력(`GetResolvedStats`·`GetActiveFragments`)은 `UFPSRWeaponInstance` 가 이미 복제한다(`OnRep_ActiveFragments` → 같은 재빌드 경로, §6). `WeaponPartComponents`·`LastWeaponPartSignature`·`CachedPartFramesInGunSpace` 는 `Transient` 로컬 캐시(`FPSRCharacter.h:1224` `:1244-1245`). 리슨 호스트와 원격 클라 각자 **같은 입력에서 같은 파츠**를 만든다 |

> 검증에서 **양쪽 반쪽**(`[[event-halves-authority-vs-client]]`)을 본다(§12-6 ⓑ·ⓒ).

---

## 8. 수명주기 · 소유권

- **생성 / 등록**: 불변. 파츠는 `RebuildPartsFromSelection` 이 `NewObject<UStaticMeshComponent>(this)` → `RegisterComponent` → attach (`:2590-2610`). 부모만 `ActiveWeaponMesh`. rev2: 부착 루프가 `ActiveWeaponMesh` 로 가드되므로 **부모 없이 등록만 된 고아 컴포넌트는 생기지 않는다**.
- **부모의 수명**: `WeaponMesh`·`WeaponMeshStatic` 둘 다 **생성자 기본 서브오브젝트**(`:112` `:120`)라 캐릭터와 수명이 같다. `ActiveWeaponMesh` 는 그 둘 중 하나를 가리키는 `Transient` 포인터(`FPSRCharacter.h:996-997`) — 무기 교체마다 `:2348` 에서 재대입. 정적 부모라고 새로 생기거나 사라지는 객체는 없다.
- **해제**: 불변. `RebuildPartsFromSelection` 진입부가 기존 파츠를 `DestroyComponent` 하고 배열·캐시를 리셋한다(`:2550-2570`). 부모가 바뀌어도 파츠는 **부모에 소유되지 않는다**(소유자 = 캐릭터) — 스켈레탈→정적 무기 교체 시 종전 파츠는 위 진입부에서 파괴된 뒤 새 부모에 새로 붙는다.
- **언이큅 캐시 초기화**(rev2 명시): `RebuildPartsFromSelection(빈)` 의 꼬리 `:2689` `:2692` 가 유일한 지점. §5-2 ②가 이를 보존한다.
- **GC**: `WeaponPartComponents` 가 `UPROPERTY(Transient)` `TArray<TObjectPtr<>>` 로 참조를 쥔다(`:1224-1225`). 불변.
- **델리게이트**: 없음(불변).
- **재부착 경로**: `AttachWeaponMeshes` 는 `WeaponMesh`·`WeaponMeshStatic` 둘 다 붙이고(`:1074-1075`) 파츠에는 **렌더 태그만** 다시 건다(`:1080-1086`) — 파츠는 부모를 따라가므로 정적 부모여도 재부착이 필요 없다. 불변.

---

## 9. 데이터드리븐 경계 (핵심원칙 2)

| 값 | 나가는 곳 | 기본값 | 비고 |
|---|---|---|---|
| 어느 파츠가 어느 소켓에 | `UFPSRWeaponDataAsset::WeaponParts` (Details「무기 › 모듈 파츠」) | — | **이 유닛은 읽기만.** 라이플은 현재 Synty 파츠 7개가 `SOCKET_Mount_{Barrel,Handguard,Stock,Mag,Grip,Trigger,Reddot}_0` 에 저작돼 있고(에디터 실측 2026-09-03 — DA 의 `socket=` 값을 직접 읽음, `_0` 접미 포함), 하드서피스 몸통은 그중 **6개**를 같은 이름으로 갖는다(`ffa6d5ca`; `Trigger` 마운트 없음). 따라서 이 유닛이 들어가면 **Synty 파츠 6개가 마운트에, Trigger 파츠 1개가 몸통 원점에 붙는 중간 상태**가 된다 — 결함이 아니라 예상 상태. `SM_RifleHS_*` 로의 교체는 사용자 DA 저작(§2 비목표) |
| 진화 단계별 교체 | `WeaponParts[i].Stages`(`FFPSRWeaponPartStage` — `Trigger=FragmentStacks`, `MinStacks≥1`, `FPSRWeaponDataAsset.h:73-95`) / `EvolutionFragment`(`:145`) | — | 불변. 라이플 Reddot 슬롯 = `DA_Fragment_Rifle_SniperScope` 스택 시 `SM_Wep_Mod_Scope_09`(G1 재확인) |
| 프래그먼트 슬롯 상한 | `MaxFragmentSlots`(`FPSRWeaponDataAsset.h:389`, 기본 3) | 3 | 불변 |

에셋 경로 하드코딩 없음(추가되는 문자열 0).

---

## 10. 성능 예산 (핵심원칙 1)

- **틱**: 변화 없음. 이 유닛이 만지는 함수는 전부 **장착/수정 이벤트**에서만 돈다. 애니메이션 프레임당 경로(`ApplyWeaponPartCurves`·`GetPartFrameInGunSpace`)는 캐시를 읽을 뿐이고 손대지 않는다.
- **액터당 비용**: 적 스웜에 붙지 않는다. 플레이어 ≤4 × 파츠 ≤7 컴포넌트 — 스켈레탈 무기가 오늘 이미 내는 비용과 동일.
- **복제 대역**: 0 바이트 추가(§7).
- **완화**: 해당 없음 — 추가 비용이 없다.

---

## 11. 미결정 항목 · 명세 갭 처리

**결정해 둔 것(구현자가 다시 묻지 않게)**
- ④ 의 게이트는 **명시적으로 `!ActiveWeaponMesh`** 를 남긴다. `GetWeaponRootPlacementInGunFrame` 이 내부에서 같은 null 을 검사하지만, ④ 의 `:3068` 이 `ActiveWeaponMesh->` 를 역참조하므로 **이 함수 자체의 전제를 이 함수 안에 적어 두는 것**이 맞다.
- ①·⑤ 는 `ActiveWeaponMesh != WeaponMesh` 항을 **삭제**한다. `ActiveWeaponMesh == WeaponMeshStatic` 같은 **새 분기 추가 금지** — 부모가 무엇이든 같은 코드.
- ② 는 **조기 반환 금지**(§5-2). 루프 가드만.

**후속 유닛으로 넘기는 것 (이 유닛 범위 밖 — 사용자가 알고 승인할 것)**
- **조립툴 정적 몸통 미지원** — `FPSRWeaponAssemblerViewportClient.cpp:94-96` · `Helpers.cpp:52-58`. 현재 `DA_Weapon_Rifle` 은 `WeaponMesh=None` 이라 툴에 몸통이 안 뜨고 소켓 베이크가 불가 → 라이플 파츠 8종 DA 저작은 **Details 수기 입력**이다. 또 툴의 현행 소켓 명명은 `SOCKET_Mount_<Base36Guid>`(`Helpers.cpp:115`)라 DA 의 `SOCKET_Mount_<Slot>_0` 은 구버전 산물 — 재베이크 시 이름이 바뀔 수 있다(G1 범위 밖 발견).
- **DA 검증기 정적 대칭화** — `FPSRWeaponDataAsset.cpp:197` `:306-308` `:370-372` 가 `SkelWeapon` 게이트라 정적 몸통+파츠 DA 는 소켓 검증을 못 받는다. 라이플의 `SOCKET_Mount_Trigger_0` 원점 부착이 스켈레탈이었으면 경고, 정적에선 무음.
- **stale 주석** — `FPSRWeaponDataAsset.h:124` · `FPSRWeaponDataAsset.cpp:190`.
- **④ 의 런타임 소비자 부재** — `FPSRGunMotionStudioData` 를 가진 콘텐츠 에셋 0개(`grep -rl -a` 2026-09-04). 총모션 스튜디오 데이터가 저작되면 그때 ④가 처음 런타임 경로를 탄다 — 그 유닛의 검증 항목으로 넘긴다.

**갭 처리 규칙(고정)**: 구현 중 명세에 없는 판단이 필요해지면 **추측해서 채우지 말고 멈추고 "명세 갭"으로 보고**한다. 예상 갭 후보: 줄 번호 이동(다른 세션 커밋으로) — 줄 번호가 아니라 **표 5-2 의 코드 문자열**로 위치를 잡을 것; 문자열이 두 번 이상 나오면 갭으로 보고.

---

## 12. 검증 기준 (무엇을 통과라 부르는가)

| # | 검사 | 통과 조건 |
|---|---|---|
| 1 | 명세 대조 | `git diff` 가 **정확히** §5-2 의 5개 변경(②는 조기반환 삭제 + 루프 가드) + §5-1 주석 2곳 + `:2527-2529`·`:2447`·②자리 주석. 그 외 변경 0. 헤더 시그니처 변경 0 |
| 2 | 빌드 | `Build.bat FPSRogueliteEditor Win64 Development -Project=... -WaitMutex -DisableAdaptiveUnity -ForceUnity` **Succeeded**, 로그에 `[Adaptive Build] Excluded` 없음(`Troubleshooting.md` G13). ⚠️ 이 검사는 ④를 `WeaponMesh->` 로 틀리게 남겨도 초록이다(둘 다 `USceneComponent*` 로 컴파일) — 1번 대조가 그 구멍을 막는다 |
| 3 | 헤드리스 스모크 | `Automation RunTests FPSRoguelite.Smoke.ModuleLoads` 통과(`FPSRSmokeTest.cpp:8`). 무기 DA·`IsDataValid` 를 타는 자동화 테스트는 리포에 없다(G1 확인) — 런타임 검증은 5·6 이 담당 |
| 4 | 레드팀 게이트(G2) | `Workflow.md` §6-6-1. **P1 잔존 시 머지 금지**. G1→G2 사이에 플랜에 없던 결정이 생기면 그 목록을 G2 프롬프트에 명시(§6-5-2 (3)). 결과는 §13 |
| 5 | **회귀 — 스켈레탈 대조군** (사용자 PIE, 리슨 호스트) | **SMG**(AimSocket 있음)와 **Shotgun**(없음) 장착 → 파츠 7개가 **종전과 같은 자리**에 부착·ADS·총구화염 동일. 나이프·비무장 → 파츠 0(변화 없음). **변경 전 빌드에서 같은 장면의 스크린샷·로그를 먼저 떠 둔다**(기준선 없이는 7번 판정 불가) |
| 6 | **신규 동작 — 정적 라이플** (사용자 PIE, **리슨 호스트 + 원격 클라 양쪽**) | 아래 ⓐ~ⓕ |
| 7 | 로그 | PIE 중 `AimSocket ... not found` 계열 경고(`:2466` `:2480`)가 **스켈레탈 대조군에서 기준선 대비 새로 생기지 않음** |

### 12-6 정적 라이플 세부
- **ⓐ 배치**(rev2: 개수 → 배치) — 장착 시 Synty 파츠 **6개가 각 마운트 위치**에(총열 앞, 개머리판 뒤, 탄창 아래, 핸드가드 앞, 그립 아래, 레드닷 위), **Trigger 파츠 1개만 몸통 원점**에(마운트 없음 — 예상). 7개가 전부 원점에 뭉쳐 있으면 소켓 이름 불일치 = 실패.
- **ⓑ 원격 클라 반쪽 + 순서 역전 창** — 원격 클라 화면에서 ⓐ 와 동일. 그리고 **원격 클라 쪽에서 무기 스왑**(라이플 ↔ SMG ↔ 라이플)을 해 P2-① 의 언이큅/재장착 경로가 클라에서도 도는지 본다. 스왑 뒤 맨손/다른 무기의 손 위치가 라이플 그립에 남아 있지 않아야 한다(캐시 초기화 = 목표 5).
- **ⓒ 지연 재빌드(⑤) — 결정적 실행 경로**. ⚠️ 리뷰어가 제안한 `FPSR.Frag.Fill/Replace` 는 **라이플에서 안 돈다**: `DebugFillFragmentSlots` 는 `Weapon->UnlockableFeatures` 만 읽는데(`FPSRPlayerController.cpp:668`) 라이플의 그 풀은 `DA_CardUnlock_Rifle`(`CardEffect_GrantWeapon`) 1장뿐이라 프래그먼트 0개 → 명령이 스스로 거부한다(`:711-715`). 대신 **레벨업 풀** 경로를 쓴다 — 라이플의 `WeaponCards` 에 `DA_CardModifiers_SniperScope`(`CardEffect_WeaponBehavior` → `DA_Fragment_Rifle_SniperScope`)가 있다(네임테이블 2026-09-04):
  1. 호스트 콘솔 `FPSR.DrawCards 10` → 로그 `[Card] [i] DA_CardModifiers_SniperScope (...)` 에서 인덱스 `i` 를 읽는다(`FPSRCardSubsystem.cpp:659`; 풀은 소유 무기의 `WeaponCards`, `:591-605`). 안 나오면 `FPSR.Reroll` 또는 다시 `DrawCards`.
  2. `FPSR.ApplyCard i` → OpeningSeed 로 적용(`:727`, 대기 픽 불필요) → `AddFragment` → `NotifyOwnerModifiersChanged` → 다음 틱 `ProcessPendingWeaponPartsRebuild`.
  3. **기대**: Reddot 슬롯이 `SM_Wep_Mod_Reddot_01` → `SM_Wep_Mod_Scope_09` 로 교체되고 **나머지 6개 파츠가 그대로 남는다.** 원격 클라에서도 `OnRep_ActiveFragments`(`FPSRWeaponInstance.cpp:164-167`)로 같은 교체가 보인다.
  4. 제거 방향은 현 디버그 도구로 관측 불가(레벨업 프래그먼트를 빼는 명령이 없다 — `Frag.Replace` 는 위 이유로 불가). ⑤ 의 가드는 추가/제거 공통 경로이므로 3단계가 ⑤ 를 검증한다. 이 한계를 명시한다.
- **ⓓ 그립 해석**(rev2: "④ 검증" 라벨 철회) — 왼손이 핸드가드 파츠의 `SOCKET_LeftHand`(Synty `Handguard_03` 에는 없음 — G1 확인) 가 아니라 **몸통 `SOCKET_LeftHand`** 로 떨어져 붙는다(`ResolveLeftHandGripComponent` 폴백 `:2706`). 이건 기존 `ActiveWeaponMesh` 폴백의 확인이지 ④의 검증이 아니다.
- **④ 는 런타임 미검증** — 소비자(`FPSRGunMotionStudioData` 저작 클립)가 0개라 PIE 로 못 본다. 검증 = 1번 diff 대조(`ActiveWeaponMesh->GetSocketTransform(Socket, RTS_Component)` 문자열 확인)뿐. 정직하게 미검증으로 남긴다.
- **ⓔ ADS** — 조준 소켓을 가진 파츠(`Reddot_01`/`Scope_09`)가 정렬 기준이 된다(`CachedAimComponent`). 화면 중앙 정렬 여부는 `ADSAimRotationOffset` 판정(`RifleHardSurface_ResumePrompt.md` §6-2)과 같이 본다.
- **ⓕ 총구화염** — `Barrel_04` 파트의 `SOCKET_Muzzle` 에서 난다(`CachedMuzzleComponent`).

### C0 실측치 (G1 이 전제를 검증할 수 있게 — §6-5-2 (3))
- **DA 전수(2026-09-04, `Scripts/run_census_weapon_da.bat` → `Saved/census_weapon_da.log`)**: 9종. 스켈레탈+파츠7 = Bazooka·ChargeLaser·LMG·SMG·Shotgun·Sniper(대조군) / 정적+파츠0 = Knife / 메시없음+파츠0 = Unarmed / **정적+파츠7 = Rifle 1건**(유일한 신규 부착 대상).
- **DA 파츠 소켓명(에디터 실측 2026-09-03)**: 라이플 `weapon_parts[i].socket` = `SOCKET_Mount_{Barrel,Handguard,Stock,Mag,Grip,Trigger,Reddot}_0` (`_0` 포함, 직접 읽음 — 네임테이블 추정 아님).
- **몸통 소켓(2026-09-04, `ffa6d5ca`)**: `SM_RifleHS_Body` 에 `SOCKET_Mount_{Stock,Grip,Mag,Handguard,Barrel,Reddot}_0` + 양손 2 = 8개(`find_socket` 재조회). 총열 `SOCKET_Muzzle`, 조준경 2종 `SOCKET_Aim`.
- **런타임(사용자 PIE 2026-09-04)**: 정적 몸통이 `WeaponMeshStatic` 으로 위치·방향 정상 표시(파츠 0 — 현 가드 그대로).
- **호출 순서(코드)**: `ActiveWeaponMesh` 대입 `:2348` → `RefreshWeaponPartComponents` `:2448`. 무기 없음 경로 `:2272`(null) → `:2302`. `WeaponMesh` 는 `:112` 기본 서브오브젝트(항상 non-null).
- **엔진**: `UStaticMeshComponent::DoesSocketExist` `StaticMeshComponent.cpp:1334` · `GetSocketTransform` `:1365`(`RTS_Component` `:1389-1392`) · `USceneComponent::AttachToComponent` 부모 null 가드 `SceneComponent.cpp:2312`.
- **④ 소비자**: `FPSRGunMotionStudioData` 참조 콘텐츠 에셋 **0개**(`grep -rl -a` Content/, 2026-09-04). 게이트 `FPSRFirstPersonArmsAnimInstance.cpp:168-171`.
- **카드/프래그먼트(네임테이블 2026-09-04)**: `DA_CardModifiers_SniperScope` → `CardEffect_WeaponBehavior` → `DA_Fragment_Rifle_SniperScope`; `DA_CardUnlock_Rifle` → `CardEffect_GrantWeapon`(프래그먼트 없음). `DebugFillFragmentSlots` 풀 = `UnlockableFeatures` 만(`FPSRPlayerController.cpp:668`).

---

## 13. 레드팀 지적 원장

> `Workflow.md` §6-6-1. **기각엔 근거가 필요하다** — 제1원리 조항 / 코드 인용 / 실측치 중 하나.

### G1 1회차 (2026-09-04) — **반려** → rev2 로 반영

| 심각도 | 지적 (요약 + 파일:줄) | 처리 | 근거 |
|---|---|---|---|
| P2 | ② 를 `!ActiveWeaponMesh` 조기 반환으로 바꾸면 죽어 있던 가드가 살아나 **언이큅 경로의 그립·파츠 캐시 초기화(`:2689` `:2692`)가 건너뛰어진다** — `:2573` · `:3406-3410` · `FPSRWeaponInventoryComponent.cpp:460-464` | **수용** | `WeaponMesh` 는 `:112` 기본 서브오브젝트 → `!WeaponMesh` 항상 거짓 = 오늘 꼬리가 항상 실행됨을 코드로 확인. §5-2 ② 를 "조기 반환 삭제 + 루프 가드"로, §2 목표 5·§6·§8 에 언이큅 초기화 보존을 명시 |
| P2 | §4 "에디터 모듈에 스켈레탈 가정 없음"은 거짓 — 조립툴 `FPSRWeaponAssemblerViewportClient.cpp:94-96` · `Helpers.cpp:52-58` 스켈레탈 전용 | **수용** | 인용 코드 확인. §4 철회 + §3-3 완화 + §11 후속 명시 + §2 비목표 추가 |
| P2 | §12 ⓓ "④ 검증" 오라벨 — ④ 소비자(`FPSRGunMotionStudioData`) 콘텐츠 0개, ⓓ 는 그립 폴백 경로 | **수용** | `grep -rl -a` 재실측 0개. ⓓ 라벨 정정, ④ 런타임 미검증 명시 |
| P3 | DA 검증기 `IsDataValid` 스켈레탈 전용(`FPSRWeaponDataAsset.cpp:197` `:306-308` `:370-372`) | **수용(후속)** | §11 에 명시. 이 유닛 범위 밖 |
| P3 | stale 주석 4곳(`.h:1221-1223` `.cpp:2447` `FPSRWeaponDataAsset.h:124` `.cpp:190`) | **부분 수용** | `FPSRCharacter.*` 2곳은 §5 에 추가. DataAsset 2곳은 파일 범위를 넘으므로 §11 후속(범위 규율 우선) |
| P3 | §12-6 ⓐ "7개 부착"은 개수라 전부 원점이어도 통과 | **수용** | ⓐ 를 배치 기준으로. DA `_0` 접미는 에디터 실측(2026-09-03)으로 확인해 C0 에 추가 |
| P3 | §12-6 ⓒ 결정적 경로 부재 — `FPSR.Frag.Fill/Replace` 제안 | **수용(경로는 정정)** | 제안된 명령은 라이플에서 **안 돈다**(`UnlockableFeatures` 풀에 프래그먼트 0 — `FPSRPlayerController.cpp:668, 711-715` + 네임테이블). `FPSR.DrawCards`/`FPSR.ApplyCard`(`FPSRCardSubsystem.cpp:663-731`) 로 대체. 제거 방향 관측 불가는 한계로 명시 |
| — | 범위 밖: `RifleHardSurface_ResumePrompt.md` 빌드 플래그 `-DisableUnity -ForceUnity` 모순 | **수용** | 같은 커밋에서 `-DisableAdaptiveUnity -ForceUnity` 로 정정 |
| — | 범위 밖: 조립툴 소켓 명명 `SOCKET_Mount_<Base36Guid>` vs DA `_<Slot>_0` 드리프트 · `AttachWeaponMeshes` 가 `ActiveWeaponMesh` 대입 전에 그립 캐시를 한 번 갱신(`:1094` → `:2522` 가 덮음, 기존) | **기록** | §11 후속 |

- **G1 1회차에 무엇을 줬나**: 명세 rev1(`ad89c671`) · 코드 경로 · 엔진 소스 경로 · `Docs/InternalRedTeamReview.md`. 설계 변호 없음.
- **G1 2회차**: (제출 후 기입)
- **레드팀(G2)에 무엇을 줬나**: (C3 후 기입)
