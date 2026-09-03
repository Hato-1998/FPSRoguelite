# WPN1 — 정적 무기 메시에도 모듈러 파츠를 붙인다 (파츠 부모 = `ActiveWeaponMesh`)

## 1. 메타

| 항목 | 값 |
|---|---|
| 유닛 ID / 이름 | `WPN1` / 정적 무기 메시의 모듈러 파츠 부착 — 파츠 부모를 `WeaponMesh`(스켈레탈 전용)에서 `ActiveWeaponMesh`(스켈레탈·정적 공용)로 |
| 브랜치 | `proto/arcade-look` (라이플 하드서피스 트랙의 하위 유닛. 머지 단위는 트랙 전체) |
| 작성 모델 | `claude-fable-5-1` — ⚠️ 폴백 표기: §6-5-2 는 C1 설계 주체를 Opus 로 두지만, 이 세션은 사용자가 `/model` 로 Fable 로 전환한 상태에서 진행됐다(2026-09-04). G1 게이트는 **별도 Fable 서브에이전트**로 띄워 설계자≠검증자를 유지한다 |
| 작성일 / 최종 갱신 | 2026-09-04 (rev1 — G1 제출본) |
| 상태 | `초안` — G1 대기 |
| 관련 SSOT | `Docs/RifleHardSurface_ResumePrompt.md` §1-2·§2-4·§3-2 · `Docs/WeaponPack_Integration.md`(총구=파트 소켓·조준=사이트 파트 소켓 규약) · `Docs/SSOT/Workflow.md` §6-5-2 |
| 관련 메모리 | `[[reason-in-multiplayer-terms]]` · `[[code-is-immutable-structure-only]]` · `[[production-structure-first]]` · `[[event-halves-authority-vs-client]]` |
| 보드 행 | 「아케이드 룩 프로토 + 육안 게이트」 `3d03972d-dd88-81ff-87d6-f5730a06e7a4` 스코프 2/4 |

---

## 2. 목표 / 비목표

### 목표
끝나면 **`WeaponMeshStatic` 으로 표시되는 무기도 `WeaponParts` 슬롯의 파츠를 붙인다** — 스켈레탈 무기와 같은 규칙(같은 선택기, 같은 소켓 규약, 같은 재빌드 수명주기)으로.

동작 기준:
1. 정적 무기(`WeaponMesh` 비움 · `WeaponMeshStatic` 설정)가 장착되면 `WeaponParts` 의 파츠가 **몸통 정적 메시의 소켓**에 붙는다.
2. 프래그먼트(진화) 변경으로 파츠 선택이 바뀌면 **즉시 경로와 다음-틱 경로 둘 다** 정적 무기에서도 재빌드된다.
3. 파츠가 붙은 뒤 **팔 IK 블렌드가 파츠 소켓을 타깃으로 잡는다**(그립↔파츠 타깃 전환) — 스켈레탈과 동일.
4. 스켈레탈 무기 6종의 동작은 **변화 0** (회귀 대조군 = §12-5).

### 비목표 (구현자가 "친절하게" 채우면 안 되는 것)
- **무기 자체 AnimBP(노리쇠·재장전 몽타주) 경로는 손대지 않는다.** `FPSRCharacter.cpp:3221` `:3272` `:3869` 의 `WeaponMesh->GetAnimInstance()` 는 정적 무기에서 이미 null no-op 다. 정적 무기에 몽타주를 "대신" 붙이려 하지 말 것.
- **`DA_Weapon_Rifle` 의 `WeaponParts` 를 새 메시(`SM_RifleHS_*`)로 바꾸지 않는다.** DA 값 저작은 사용자 작업(`[[da-edits-are-user-work]]`). 이 유닛이 끝나면 라이플에는 **아직 Synty 파츠**가 붙는 게 맞다(§9).
- **소켓 이름·규약을 바꾸지 않는다.** `SOCKET_Mount_*_0`·`SOCKET_Aim`·`SOCKET_Muzzle`·`SOCKET_LeftHand`·`SOCKET_RightHand` 그대로.
- **헤더의 시그니처·필드 추가/변경 금지.** 바뀌는 헤더 내용은 **주석 한 줄**뿐이다(§5).
- **`RefreshWeaponPartComponents` 를 호출하는 지점을 늘리거나 줄이지 않는다.** 호출 그래프는 그대로, 가드 조건만.
- **정적 무기 파츠에 복제·저장을 붙이지 않는다.** 파츠는 지금도 복제되지 않는 로컬 코스메틱이다(§7). 그대로.
- **다른 정적 무기(나이프·비무장)에 파츠를 저작하지 않는다.** 이 유닛은 코드만.

---

## 3. 제1원리 3줄 (핵심원칙 4)

1. **제1원리 근거** — 이 코드는 **플레이어(1~4)** 의 무기에만 돈다. 적 스웜 경로와 무관하고, 액터당 비용은 파츠 컴포넌트 ≤7개 × 플레이어 ≤4 로 지금과 같다. 서버권위: 파츠 선택의 입력(스탯·프래그먼트)은 **이미 복제되는 상태**이고 파츠 자체는 각 머신이 로컬로 재생성한다 — 이 유닛은 그 입력도 출력도 바꾸지 않는다.
2. **엔진 기본값·기존 인프라와의 관계** — **기존 인프라를 그대로 쓴다.** 파츠는 이미 `UStaticMeshComponent` 로 생성돼 `AttachToComponent(Parent, KeepRelativeTransform, SocketName)` 으로 붙는다(`:2600` `:2608`). 바뀌는 것은 **부모 하나**다. 정적 부모에서 필요한 두 API 는 엔진이 제공한다: `UStaticMeshComponent::DoesSocketExist`(`StaticMeshComponent.cpp:1334`) · `UStaticMeshComponent::GetSocketTransform(Name, RTS_Component)`(`:1365`, `RTS_Component` 분기 `:1389-1392`). 둘 다 `UMeshComponent`/`USceneComponent` 가상 인터페이스라 `ActiveWeaponMesh`(`TObjectPtr<UMeshComponent>`, `FPSRCharacter.h:997`)로 호출하면 스켈레탈/정적 어느 쪽이든 자기 구현으로 간다. 엔진 기본값을 덮는 게 아니라 **엔진이 이미 다형으로 제공하는 것을 쓰지 않고 있던 것을 쓰게 하는 것**이다.
3. **프로젝트 제약과의 정합** — 이 프로젝트의 무기 조립 규약(총구=파트 소켓, 조준=사이트 파트 소켓, 좌/우손=파트 또는 리시버)은 이미 `ActiveWeaponMesh` 폴백으로 짜여 있다(`:2472-2476` 조준, `:2618-2632` 총구, `:2695-2711` 좌손, `:2713-` 우손, `GetWeaponRootPlacementInGunFrame :2860` `WeaponRoot = ActiveWeaponMesh`). **파츠 부착·프레임 캐시 4곳만 `WeaponMesh` 를 직접 잡고 있다** — 그 비대칭이 버그의 원천이고, 이 유닛은 그 비대칭을 없앤다. 다음 단계(라이플 파츠 8종·저격 진화 2x 교체·무기 8종 파일럿 확장)는 전부 이 위에 선다.

---

## 4. 파일 목록

| 경로 | 신규/수정 | 한 줄 설명 |
|---|---|---|
| `Source/FPSRoguelite/Private/Hero/FPSRCharacter.cpp` | 수정 | 5개 지점의 파츠 부모·가드를 `ActiveWeaponMesh` 기준으로 (§5-2) |
| `Source/FPSRoguelite/Public/Hero/FPSRCharacter.h` | 수정 | `RefreshWeaponPartComponents` 선언 **주석 1줄** — "스켈레탈만" 계약 문구 제거 (§5-1). 시그니처 불변 |

그 외 파일 변경 없음. 에디터 모듈(`FPSRogueliteEditor/Assembler/*`)·`FPSRWeaponPartSelector`·`FPSRWeaponAnimInstance` 에는 스켈레탈 전용 가정이 없다(전수 grep `ActiveWeaponMesh != WeaponMesh|skeletal weapon mesh only|SKELETAL weapon mesh|Parts attach to the` → `FPSRCharacter.cpp/.h` 5건뿐, 2026-09-04).

---

## 5. 인터페이스 선언 (헤더 스케치)

### 5-1. 헤더 — 주석만 바뀐다

```cpp
// FPSRCharacter.h:1247-1249  (시그니처 불변. 주석의 "(only when a SKELETAL weapon mesh is shown; static/melee/empty attach nothing)" 를 아래로)
/** Destroy any existing modular part components and rebuild them from the equipped weapon's WeaponParts list.
 *  Attaches to ActiveWeaponMesh — whichever of WeaponMesh (skeletal) / WeaponMeshStatic (static) is shown; no weapon
 *  or no active mesh attaches nothing. Called from the weapon refresh. */
void RefreshWeaponPartComponents(const UFPSRWeaponDataAsset* Weapon);
```

### 5-2. `.cpp` 5개 지점 — 바뀌는 줄만 (본문은 쓰지 않는다; 축자 구현 대상)

| # | 위치 | 현재 | 변경 |
|---|---|---|---|
| ① | `RefreshWeaponPartComponents` `:2530` | `if (!Weapon \|\| !WeaponMesh \|\| ActiveWeaponMesh != WeaponMesh)` | `if (!Weapon \|\| !ActiveWeaponMesh)` |
| ② | `RebuildPartsFromSelection` `:2573` | `if (!WeaponMesh) { return; }` | `if (!ActiveWeaponMesh) { return; }` |
| ③ | `RebuildPartsFromSelection` `:2608` | `PartComp->AttachToComponent(WeaponMesh, FAttachmentTransformRules::KeepRelativeTransform, PartDef.Socket);` | `PartComp->AttachToComponent(ActiveWeaponMesh, FAttachmentTransformRules::KeepRelativeTransform, PartDef.Socket);` |
| ④ | `RefreshPartFramesInGunSpaceCache` `:3044` · `:3068` | `if (!WeaponMesh \|\| !GetWeaponRootPlacementInGunFrame(WeaponInGunFrame))` · `WeaponMesh->GetSocketTransform(Socket, RTS_Component)` | `if (!ActiveWeaponMesh \|\| !GetWeaponRootPlacementInGunFrame(WeaponInGunFrame))` · `ActiveWeaponMesh->GetSocketTransform(Socket, RTS_Component)` |
| ⑤ | `ProcessPendingWeaponPartsRebuild` `:3152` | `if (!Weapon \|\| !WeaponMesh \|\| ActiveWeaponMesh != WeaponMesh)` | `if (!Weapon \|\| !ActiveWeaponMesh)` |

주석 갱신 대상(같은 diff 안에서): `:2527-2529`(①의 설명) · `:2572`(②의 설명). "SKELETAL only" 서술을 "ActiveWeaponMesh(스켈레탈/정적 공용)" 로. 그 외 주석 불변.

> ⚠️ **`ActiveWeaponMesh` 가 null 인 조건**(`:2348` 대입 규칙): 스켈레탈도 정적도 로드되지 않았을 때. 그 경우 ①·⑤ 는 종전과 같이 파츠를 전부 제거하고 시그니처를 0 으로 되돌린다 — 이 분기의 **동작은 불변**이다(가드가 참이 되는 집합이 "스켈레탈 아님" 에서 "표시 메시 없음" 으로 **좁아질 뿐**).

---

## 6. 함수별 계약

| 함수 | 권위 | 호출자 | 전제조건 | 실패 시 동작 |
|---|---|---|---|---|
| `RefreshWeaponPartComponents(Weapon)` | 로컬(각 머신) | `RefreshEquippedWeaponVisual` `:2448` — **`ActiveWeaponMesh` 대입(`:2348`) 이후** | `ActiveWeaponMesh` 가 현재 표시 메시(스켈레탈 또는 정적) | `Weapon`/`ActiveWeaponMesh` null → 파츠 전부 제거 + `LastWeaponPartSignature=0` (불변) |
| `RebuildPartsFromSelection(Selected)` | 로컬 | ① `RefreshWeaponPartComponents` ② `ProcessPendingWeaponPartsRebuild` | 호출자가 `ActiveWeaponMesh` 유효성을 보장. 파츠 소켓이 부모에 없으면 `AttachToComponent` 가 **경고 없이 루트에 붙는다**(엔진 동작 — `USceneComponent::AttachToComponent` 는 없는 소켓명을 부모 원점으로 취급) → **이 유닛은 이를 바꾸지 않는다**(스켈레탈에서도 같은 동작) | 위와 같음 |
| `RefreshPartFramesInGunSpaceCache()` | 로컬 | `RebuildPartsFromSelection :2692` | `ActiveWeaponMesh` 유효 + `GetWeaponRootPlacementInGunFrame` 성공(내부에서 이미 `ActiveWeaponMesh` 사용 `:2860`) | 캐시 비움(불변) |
| `ProcessPendingWeaponPartsRebuild()` | 로컬 | 다음 틱 타이머(`:3144`) | 시그니처 비교 후 변경 시에만 재빌드 | 가드 실패 → 조기 반환(불변) |
| `FPSRWeaponPartSelector::SelectParts` | — | 위 두 함수 | **변경 없음** — `Weapon.WeaponParts` 를 DA 순서로 순회, 메시 타입을 보지 않는다(`FPSRWeaponPartSelector.cpp:42`) | — |

---

## 7. 복제표 (§6-3 서버권위 + Push Model)

| 프로퍼티 / RPC | 종류 | Push Model | 신뢰성 | 조건 | 비고 |
|---|---|---|---|---|---|
| (없음) | — | — | — | — | **이 유닛은 복제 표면을 만들지도 바꾸지도 않는다.** 파츠 입력(`GetResolvedStats`·`GetActiveFragments`)은 `UFPSRWeaponInstance` 가 이미 복제한다. `WeaponPartComponents`·`LastWeaponPartSignature`·`CachedPartFramesInGunSpace` 는 `Transient` 로컬 캐시(`FPSRCharacter.h:1224` `:1244-1245`). 리슨 호스트와 원격 클라 각자 **같은 입력에서 같은 파츠**를 만든다 — 정적 부모라고 달라질 이유가 없다 |

> 검증에서 **양쪽 반쪽**(`[[event-halves-authority-vs-client]]`)을 본다: 호스트가 든 정적 라이플의 파츠가 **원격 클라 화면에서도** 붙는가, 그 반대도 (§12-6).

---

## 8. 수명주기 · 소유권

- **생성 / 등록**: 불변. 파츠는 `RebuildPartsFromSelection` 이 `NewObject<UStaticMeshComponent>(this)` → `RegisterComponent` → attach (`:2590-2610`). 부모만 `ActiveWeaponMesh`.
- **부모의 수명**: `WeaponMesh`·`WeaponMeshStatic` 둘 다 **생성자 기본 서브오브젝트**(`:112` `:120`)라 캐릭터와 수명이 같다. `ActiveWeaponMesh` 는 그 둘 중 하나를 가리키는 `Transient` 포인터(`FPSRCharacter.h:996-997`) — 무기 교체마다 `:2348` 에서 재대입. 정적 부모라고 새로 생기거나 사라지는 객체는 없다.
- **해제**: 불변. `RebuildPartsFromSelection` 진입부가 기존 파츠를 `DestroyComponent` 하고 배열·캐시를 리셋한다(`:2550-2570`). 부모가 바뀌어도 파츠는 **부모에 소유되지 않는다**(소유자 = 캐릭터) — 스켈레탈→정적 무기 교체 시 종전 파츠는 위 진입부에서 파괴된 뒤 새 부모에 새로 붙는다.
- **GC**: `WeaponPartComponents` 가 `UPROPERTY(Transient)` `TArray<TObjectPtr<>>` 로 참조를 쥔다(`:1224-1225`). 불변.
- **델리게이트**: 없음(불변).
- **재부착 경로**: `AttachWeaponMeshes` 는 `WeaponMesh`·`WeaponMeshStatic` 둘 다 붙이고(`:1074-1075`) 파츠에는 **렌더 태그만** 다시 건다(`:1080-1086`) — 파츠는 부모를 따라가므로 정적 부모여도 재부착이 필요 없다. 불변.

---

## 9. 데이터드리븐 경계 (핵심원칙 2)

| 값 | 나가는 곳 | 기본값 | 비고 |
|---|---|---|---|
| 어느 파츠가 어느 소켓에 | `UFPSRWeaponDataAsset::WeaponParts` (Details「무기 › 모듈 파츠」) | — | **이 유닛은 읽기만.** 라이플은 현재 Synty 파츠 7개가 `SOCKET_Mount_*_0` 에 저작돼 있고, 하드서피스 몸통도 **같은 이름의 소켓 6개**를 갖는다(`ffa6d5ca`; Trigger 마운트는 없음). 따라서 이 유닛이 들어가면 **Synty 파츠가 하드서피스 몸통에 붙는 중간 상태**가 된다 — 결함이 아니라 예상 상태. `SM_RifleHS_*` 로의 교체는 사용자 DA 저작(§2 비목표) |
| 진화 단계별 교체 | `WeaponParts[i].Stages` / `EvolutionFragment` | — | 불변 |

에셋 경로 하드코딩 없음(추가되는 문자열 0).

---

## 10. 성능 예산 (핵심원칙 1)

- **틱**: 변화 없음. 이 유닛이 만지는 함수는 전부 **장착/수정 이벤트**에서만 돈다(`RefreshEquippedWeaponVisual`, 다음-틱 코얼레스 재빌드). 애니메이션 프레임당 경로(`ApplyWeaponPartCurves`·`GetPartFrameInGunSpace`)는 캐시를 읽을 뿐이고 손대지 않는다.
- **액터당 비용**: 적 스웜에 붙지 않는다. 플레이어 ≤4 × 파츠 ≤7 컴포넌트 — 스켈레탈 무기가 오늘 이미 내는 비용과 동일.
- **복제 대역**: 0 바이트 추가(§7).
- **완화**: 해당 없음 — 추가 비용이 없다.

---

## 11. 미결정 항목 · 명세 갭 처리

**결정해 둔 것(구현자가 다시 묻지 않게)**
- ④ 의 게이트는 **명시적으로 `!ActiveWeaponMesh`** 를 남긴다. `GetWeaponRootPlacementInGunFrame` 이 내부에서 같은 null 을 검사해 `false` 를 돌려주므로 중복이지만, ④ 의 `:3068` 이 `ActiveWeaponMesh->` 를 역참조하므로 **이 함수 자체의 전제를 이 함수 안에 적어 두는 것**이 맞다. 제거하지 말 것.
- ①·⑤ 는 `ActiveWeaponMesh != WeaponMesh` 항을 **삭제**한다(정적 허용 = 이 항이 없어지는 것이 곧 변경). `ActiveWeaponMesh == WeaponMeshStatic` 같은 **새 분기 추가 금지** — 부모가 무엇이든 같은 코드.

**열어 둔 것**
- 없음. 이 유닛에 설계 자유도는 없다 — 5개 치환 + 주석.

**갭 처리 규칙(고정)**: 구현 중 명세에 없는 판단이 필요해지면 **추측해서 채우지 말고 멈추고 "명세 갭"으로 보고**한다. 예상 갭 후보: 줄 번호 이동(다른 세션 커밋으로) — 줄 번호가 아니라 **표 5-2 의 코드 문자열**로 위치를 잡을 것; 문자열이 두 번 이상 나오면 갭으로 보고.

---

## 12. 검증 기준 (무엇을 통과라 부르는가)

| # | 검사 | 통과 조건 |
|---|---|---|
| 1 | 명세 대조 | `git diff` 가 **정확히** §5-2 의 5개 치환 + §5-1 주석 + `:2527-2529`·`:2572` 주석. 그 외 변경 0. 헤더 시그니처 변경 0 |
| 2 | 빌드 | `Build.bat FPSRogueliteEditor Win64 Development -Project=... -WaitMutex -DisableAdaptiveUnity -ForceUnity` **Succeeded**, 로그에 `[Adaptive Build] Excluded` 없음(`Troubleshooting.md` G13). `.cpp` 만 바뀌므로 `-DisableUnity` 는 불필요하나 헤더 주석 변경으로 재컴파일 범위는 확인 |
| 3 | 헤드리스 스모크 | `Automation RunTests FPSRoguelite.Smoke.ModuleLoads` 통과 |
| 4 | 레드팀 게이트(G2) | `Workflow.md` §6-6-1. **P1 잔존 시 머지 금지**. 결과는 §13 |
| 5 | **회귀 — 스켈레탈 대조군** (사용자 PIE) | 스켈레탈 6종 중 최소 **SMG**(AimSocket 있음)와 **Shotgun**(없음) 장착 → 파츠 7개 부착·ADS·총구화염 **종전과 동일**. 나이프·비무장 → 파츠 0(변화 없음) |
| 6 | **신규 동작 — 정적 라이플** (사용자 PIE, **리슨 호스트 + 원격 클라 양쪽**) | ⓐ 장착 시 하드서피스 몸통에 파츠 **7개 부착**(현 DA 기준 Synty 파츠 — 중간 상태, §9) ⓑ 원격 클라 화면에서도 동일 ⓒ 진화 프래그먼트 부여/제거 → 파츠가 **유지·교체**되고 사라지지 않음(⑤ 검증) ⓓ 왼손이 핸드가드 파츠 소켓(또는 몸통 `SOCKET_LeftHand`)에 붙음(④ 검증) ⓔ ADS 시 조준 소켓을 가진 파츠가 정렬 기준이 됨 |
| 7 | 로그 | PIE 중 `AimSocket ... not found` 계열 경고(`:2466` `:2480`)가 **스켈레탈 대조군에서 새로 생기지 않음** |

### C0 실측치 (G1 이 전제를 검증할 수 있게 — §6-5-2 (3))
- **DA 전수(2026-09-04, `Scripts/run_census_weapon_da.bat`)**: 9종. 스켈레탈+파츠7 = Bazooka·ChargeLaser·LMG·SMG·Shotgun·Sniper(대조군) / 정적+파츠0 = Knife / 메시없음+파츠0 = Unarmed / **정적+파츠7 = Rifle 1건**(유일한 신규 부착 대상).
- **런타임(사용자 PIE 2026-09-04)**: 정적 몸통 `SM_RifleHS_Body` 가 `WeaponMeshStatic` 으로 위치·방향 정상 표시(파츠 0 — 현 가드 그대로).
- **소켓(2026-09-04, `ffa6d5ca`)**: 몸통 정적 메시에 `SOCKET_Mount_{Stock,Grip,Mag,Handguard,Barrel,Reddot}_0` + 양손 2 = 8개 실재(`find_socket` 재조회). 총열 `SOCKET_Muzzle`, 조준경 2종 `SOCKET_Aim`.
- **호출 순서(코드)**: `ActiveWeaponMesh` 대입 `:2348` → `RefreshWeaponPartComponents` `:2448`. 무기 없음 경로 `:2272`(null) → `:2302`.
- **엔진**: `UStaticMeshComponent::DoesSocketExist` `StaticMeshComponent.cpp:1334` · `GetSocketTransform` `:1365`(`RTS_Component` `:1389-1392`).
- **전수 grep**: 스켈레탈 전용 가정은 `FPSRCharacter.cpp/.h` 5건뿐(§4).

---

## 13. 레드팀 지적 원장 (C3에서 채운다)

> `Workflow.md` §6-6-1. **기각엔 근거가 필요하다** — 제1원리 조항 / 코드 인용 / 실측치 중 하나.

| 심각도 | 지적 (요약 + 파일:줄) | 처리 | 근거 |
|---|---|---|---|
| P1 | | | |
| P2 | | | |
| P3 | | | |

- **G1(플랜 게이트) 결과**: (제출 후 기입)
- **레드팀(G2)에 무엇을 줬나**: (C3 후 기입)
