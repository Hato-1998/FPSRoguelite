# ADS1 — 조준 이동 감속의 클라이언트 예측 (압축 플래그 + 속도 레이어)

## 1. 메타

| 항목 | 값 |
|---|---|
| 유닛 ID / 이름 | **ADS1** / 조준 이동 감속 예측 (ADS 안 B의 코드 절반) |
| 브랜치 | `main` (트렁크 기반, §6-7) |
| 작성 모델 | `claude-opus-5` (§6-5-2 개정 2026-08-26 = C1 설계는 Opus, G1·G2만 Fable) |
| 작성일 / 최종 갱신 | 2026-09-12 / 2026-09-12 |
| 상태 | **`확정`** (G1 통과 + **사용자 승인 2026-09-12** → C2 구현 착수. 승인 시 §11-(3) WalkSpeed 문구 수정을 범위에 포함하기로 결정) |
| G1 판정 | Fable 서브에이전트, P1 **0건** / P2 1건 / P3 7건. 명세가 인용한 엔진 동작 13건 전부 실제 5.8 소스와 일치 확인. 지적은 이 개정(rev 2)에 반영 — P2-1→§8·§12, P3-1→§2·§3·§7·§10, P3-2·P3-3·P3-4→§11, P3-5→§6, P3-6→§6, P3-7→§6 |
| 관련 SSOT | `Docs/SSOT/PlayerFeel.md` §2-9·§2-13 · `Docs/SSOT/CombatWeaponCard.md` §2-3 · `Docs/SSOT/Workflow.md` §6-3·§6-5-2 |
| 관련 ADR | [0001](../Architecture/0001-player-movement-state-ownership.md) 불변식 1·2·3·4·9 · [0002](../Architecture/0002-true-first-person-shared-animation.md) · [0015](../Architecture/0015-first-person-gun-only-hidden-arms-driver.md) |
| 관련 메모리 | `[[reason-in-multiplayer-terms]]` · `[[production-structure-first]]` · `[[code-is-immutable-structure-only]]` · `[[dataasset-conditional-field-visibility]]` · `[[freeze-gate-client-server-symmetry]]` · `[[nonunity-build-is-67-seconds]]` |
| 보드 행 | https://app.notion.com/p/3d93972ddd8881c28744f060e7557e55 |

## 2. 목표 / 비목표

**목표** — 이 유닛이 끝나면:

1. 조준(ADS) 중 걷기 속도가 무기가 정한 배율만큼 떨어진다. 기본 0.5 → 서 있으면 900→**450**, 웅크리면 300→**150**.
2. 그 감속이 **클라이언트 예측**을 탄다. 조준을 누르고 떼는 순간 서버와 클라이언트가 **같은 move에서 같은 속도**를 쓰므로 보정(고무줄)이 나지 않는다.
3. 감속 배율은 **무기 데이터**가 정한다. ADS가 없는 무기(근접/맨손)는 조준 버튼을 눌러도 감속되지 않는다.
4. 추가 복제 대역 = **조준 중에만 move당 최대 1바이트**. 엔진은 압축 플래그 바이트를 *옵셔널*로 직렬화하므로(`SerializeOptionalValue<uint8>(…, CompressedMoveFlags, 0)`, engine `CharacterMovementComponent.cpp:9852` + `NetSerialization.h:27-44`) 플래그가 0인 move는 신호비트 1개만 보낸다. 조준 중엔 그 바이트가 실려 **조준자 1명당 상행 ~60B/s** 수준이 된다(60 move/s 가정, 웅크림·점프로 이미 바이트가 실린 프레임은 증가 0). 새 복제 프로퍼티·새 RPC는 0개.

**비목표(Non-goals)** — 일부러 하지 않는 것:

- **콘텐츠 작업 전부** — 블렌드스페이스 2개(`AS_BS_Walk_Ironsights`·`AS_BS_CrouchWalk_Ironsights`) 저작, `ABP_TPS` 스테이트 2×2 확장, 스트라이드 워핑 `SpeedModifier` 조정. 코드가 먼저 들어가고 나서 사용자·후속 세션이 한다.
- **조준 감속을 카드로 바꾸는 축** — `EFPSRWeaponStat`에 축을 새로 등재하지 않는다(§11-(1)).
- **공중·슬라이드 중 조준 감속** — 둘 다 이 유닛의 코드 경로 바깥이다(§6 삽입 지점, 의도된 무변경).
- **기존 ADS 경로(FOV 줌·절차적 소켓 정렬·확산 배수·OnAim 프래그먼트 훅) 손대기** — 읽기만 하고 건드리지 않는다.
- 시뮬레이트 프록시(팀원 화면의 남의 캐릭터)에 조준 의도를 알려주기 — 이미 `bIsAiming` 복제로 포즈가 나오고, 프록시의 속도 상한은 아무것도 읽지 않는다.
- ~~`FPSRWeaponDataAsset::WalkSpeed`의 문구 오류 수정~~ → **사용자 승인으로 범위에 포함**(§11-(3), 주석·DisplayName 문구만. 값·동작 무변경).

## 3. 제1원리 3줄 (핵심원칙 4)

1. **제1원리 근거** — 이 게임은 **4인 협동 서버권위**이고(메모리 `[[reason-in-multiplayer-terms]]`) 플레이어는 1~4명뿐이라 액터당 비용이 문제가 아니다. 문제는 **핑 아래에서의 조작감**이다. 조준으로 속도가 900→450 바뀌는데 그 의도가 move에 실리지 않으면 서버는 RTT/2만큼 늦게 알게 되고, 그 사이에 벌어진 위치차(50ms × 450cm/s ≈ 22cm, 누르고 떼는 왕복 전체로는 ~37cm)를 **보정 패킷으로 되돌린다** = 조준할 때마다 고무줄. ADR 0001 불변식 2가 "서버 응답을 기다렸다 움직이는 이동은 없다"고 못박은 것이 바로 이 경로다.
2. **엔진 기본값·기존 인프라와의 관계** — **엔진 기본값을 그대로 쓴다.** 엔진은 이 문제를 위해 압축 플래그 바이트에 커스텀 비트 4개를 비워 두었고(`CharacterMovementComponent.h:3136` `FLAG_Custom_0..3`), 웅크리기 의도(`bWantsToCrouch`)를 정확히 이 방식으로 실어 보낸다. 새 RPC·새 복제 프로퍼티·새 커스텀 무브먼트 모드 **전부 불필요**. 프로젝트 인프라도 그대로 쓴다 — 속도 레이어는 이미 2단(장착·카드·다운 기준선 = `RefreshWalkSpeedCap`, 프레임 상태 배수 = `GetMaxSpeed`의 백페달)이고, 무기→무브먼트 **단방향 푸시**(`SetLoadoutWalkSpeed`)도 이미 있다. 덮는 것은 **엔진 함수 3개의 오버라이드**뿐이며, 그중 `ClientUpdatePositionAfterServerUpdate`는 엔진이 `bWantsToCrouch`에 해 주는 일(리플레이 앞뒤 저장·복원, `CharacterMovementComponent.cpp:8655`·`:8718`)을 **커스텀 플래그에는 해 주지 않기 때문에** 반드시 덮어야 한다(§8 리플레이 경계).
3. **프로젝트 제약과의 정합** — 숫자는 전부 데이터로 나간다(불변식 9). 대역은 조준 중 move당 ≤1바이트(§2 목표 4)로, 플레이어가 최대 4명이라 절대량이 무의미하다. 의존 방향은 무기→무브먼트 푸시 한 방향이라 불변식 1·4 유지 — 무브먼트 컴포넌트는 "무기"를 모르고 **불리언 하나와 배수 하나**만 안다. 배율의 양쪽 일치는 불변식 3의 *문자*(GAS 어트리뷰트로 복제)가 아니라 **`LoadoutWalkSpeed` 선례**를 따른다 — 장착 복제가 무기 DA 참조를 나르고 양쪽이 같은 애셋에서 읽으므로 같은 값을 본다(§8 초기 동기화의 창 포함).

## 4. 파일 목록

| 경로 | 신규/수정 | 한 줄 설명 |
|---|---|---|
| `Source/FPSRoguelite/Public/Weapon/FPSRWeaponTypes.h` | 수정 | `FFPSRWeaponStatBlock::ADSMoveSpeedMultiplier` 필드 1개 추가 (`Weapon\|ADS`) |
| `Source/FPSRoguelite/Public/Weapon/FPSRWeaponDataAsset.h` | 수정(문구만) | `WalkSpeed`의 "0 = 기본 600" → 900 정정(§11-(3)). **동작·기본값 무변경** |
| `Source/FPSRoguelite/Public/Hero/FPSRCharacterMovementComponent.h` | 수정 | 세터 2개·오버라이드 2개·필드 2개 + `FSavedMove_FPSR`에 비트 1개·오버라이드 1개. 클래스 주석 3곳 갱신 |
| `Source/FPSRoguelite/Private/Hero/FPSRCharacterMovementComponent.cpp` | 수정 | 위 선언들의 구현 + `GetMaxSpeed()`에 배수 1줄 |
| `Source/FPSRoguelite/Private/Weapon/FPSRWeaponFireComponent.cpp` | 수정 | `SetAiming`에서 소유 클라/호스트일 때 CMC로 의도 푸시 |
| `Source/FPSRoguelite/Private/Weapon/FPSRWeaponInventoryComponent.cpp` | 수정 | `PushEquippedWalkSpeed()`가 조준 배수도 같이 푸시 |
| `Docs/Architecture/0001-player-movement-state-ownership.md` | 수정(문서) | "`FLAG_Custom_0~3` 전부 미사용"이 이 유닛으로 낡는다 → **날짜 부기 한 줄** 정정(§11-(5)) |

> 신규 파일 0개 · 신규 UCLASS 0개. 헤더를 고치므로 검증은 논-유니티 풀빌드를 포함한다(§12).

## 5. 인터페이스 선언 (헤더 스케치)

### (1) `FFPSRWeaponStatBlock` — 무기가 정하는 숫자 (`FPSRWeaponTypes.h`, `--- ADS ---` 블록 안, `ADSInterpSpeed` 다음)

```cpp
	// Same specifiers as the three neighbours in this block: the stat block is authored by content in the DA, so
	// EditDefaultsOnly; EditCondition="bHasADS" is a FLAT name inside this same struct, which is the only shape the
	// engine's EditCondition parser resolves (a nested "BaseStats.bHasADS" path silently does nothing — see spec 11-(2)).
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Weapon|ADS", meta = (EditConditionHides, EditCondition = "bHasADS", ClampMin = "0.0"))
	float ADSMoveSpeedMultiplier = 0.5f; // walk-speed scale while aiming (0.5: 900 -> 450 standing, 300 -> 150 crouched)
```

### (2) `UFPSRCharacterMovementComponent` — public, 걷기 속도 레이어 주석 블록 안 (`SetDownedLocomotion` 다음)

```cpp
	/** ADS intent, as a PREDICTED input latch — the same shape the engine gives bWantsToCrouch.
	 *
	 *  Writable on the LOCALLY CONTROLLED machine only (the owning client, or the listen-server host for its own
	 *  pawn); ignored everywhere else. On a dedicated server the value arrives in the move packet instead
	 *  (UpdateFromCompressedFlags), and letting the ServerSetAiming RPC write it here as well would put two writers on
	 *  one value at two different TIMES — the RPC lands a frame or two off the move that carries the flag, and the
	 *  server would then simulate a different speed than the client did for exactly those frames. That IS the
	 *  rubber-band this unit exists to remove, so the guard is structural rather than a call-site convention. */
	void SetWantsToAim(bool bNewWantsToAim);

	/** Walk-speed scale applied while the aim intent above is set; 1.0 = no opinion (the neutral identity, NOT a
	 *  tuning value — the tunable lives in FFPSRWeaponStatBlock::ADSMoveSpeedMultiplier, invariant 9).
	 *  Pushed by the inventory on every equip, on the server and on each client's OnRep alike — the same one-way push
	 *  SetLoadoutWalkSpeed already uses, so this component still knows nothing about weapons (invariant 4). */
	void SetAimWalkSpeedMultiplier(float InMultiplier);
```

기존 `//~UCharacterMovementComponent` 오버라이드 목록에 2개 추가:

```cpp
	/** Applies the aim intent bit the client sent with this move. Server + the owning client's correction replay. */
	virtual void UpdateFromCompressedFlags(uint8 Flags) override;

	/** Save/restore the aim intent across a correction replay. NOT optional — see the implementation comment. */
	virtual bool ClientUpdatePositionAfterServerUpdate() override;
```

### (3) `UFPSRCharacterMovementComponent` — protected 필드 (걷기 속도 레이어 필드 블록, `bDownedLocomotion` 다음)

```cpp
	/** Predicted ADS intent. NOT replicated, and NOT stored in FSavedMove_FPSR as a field of its own: the saved move
	 *  carries it as a compressed FLAG, which is both the wire format and the replay storage (GetCompressedFlags). */
	bool bWantsToAim = false;

	/** Equipped weapon's aim speed scale; 1.0 = no opinion. Read by GetMaxSpeed while bWantsToAim. */
	float AimWalkSpeedMultiplier = 1.0f;
```

### (4) `FSavedMove_FPSR`

```cpp
	virtual uint8 GetCompressedFlags() const override;   // 기존 4개 오버라이드 옆

	/** ADS intent for this move, sent as FLAG_Custom_0. A BITFIELD like bSavedIsSliding, so the constructor
	 *  initializes it (a pooled move is reused before Clear() is guaranteed to have run). */
	uint8 bSavedWantsToAim : 1;
```

### (5) 주석 갱신 — 선언이 아니라 **계약 문서**라서 명세에 포함한다 (빠지면 명세 불일치로 본다)

| 위치 | 현재 문장 | 갱신 방향 |
|---|---|---|
| `FPSRCharacterMovementComponent.h` 클래스 주석 "Network cost: **ZERO custom compressed flags**…" | 커스텀 플래그가 0개라고 단언 | 커스텀 플래그 **1개**(`FLAG_Custom_0` = 조준 의도) + **대역은 공짜가 아니다**: 플래그 바이트는 옵셔널 직렬화라(engine `:9852`) 플래그 0인 move는 1비트, 하나라도 켜진 move는 9비트 → 조준 중 move당 ~1바이트, 조준 안 하면 0(§2 목표 4와 같은 수치). ⚠️ **rev 1의 "대역 증가 0"을 그대로 옮겨 적지 말 것** — G1 P3-1로 정정된 문장이다. 슬라이드·벽점프가 왜 아직 플래그를 안 쓰는지는 그대로 남긴다 |
| `FSavedMove_FPSR` 클래스 주석 "Note there is **no GetCompressedFlags override**…" | 오버라이드가 없다는 설명 | 이제 있다 + 실리는 것은 조준 의도 1비트 + 나머지는 여전히 로컬 리플레이 전용. 비용 문장은 위 클래스 주석으로 넘기고 여기서 "공짜"라고 쓰지 않는다 |
| 걷기 속도 레이어 주석 (`RefreshWalkSpeedCap` = 유일한 `MaxWalkSpeed` 기록자) | 레이어가 전부 여기 모인다는 뉘앙스 | **2단이라고 명시**: 기준선·장착·카드·다운 = `RefreshWalkSpeedCap` / **프레임 상태 배수(백페달·조준) = `GetMaxSpeed`**. 조준 레이어를 찾는 사람이 헤매지 않게 |

## 6. 함수별 계약

| 함수 | 권위 | 호출자 | 전제조건 | 실패 시 동작 |
|---|---|---|---|---|
| `SetWantsToAim(bool)` | **소유 클라 + 호스트 전용** (`CharacterOwner->IsLocallyControlled()`) | `UFPSRWeaponFireComponent::SetAiming` 한 곳 | `CharacterOwner` 유효 | 조용히 리턴. 프록시·데디서버의 원격 폰에서 불려도 안전 — 그쪽 진실은 플래그다 |
| `SetAimWalkSpeedMultiplier(float)` | 전 머신 (장착이 서버·OnRep 양쪽에서 푸시된다) | `UFPSRWeaponInventoryComponent::PushEquippedWalkSpeed` 한 곳 | 없음 | 음수는 `FMath::Max(0.0f, …)`로 클램프 (기존 3개 세터와 같은 모양) |
| `UpdateFromCompressedFlags(uint8)` | **서버** + 소유 클라의 **보정 리플레이** | 엔진 `MoveAutonomous` (engine `CharacterMovementComponent.cpp:10677`, 이 함수의 유일한 호출자) | — | `Super::`를 **먼저** 호출(점프·웅크리기 의도와 서버측 `Jump()` 엣지를 그쪽이 처리한다), 그다음 `bWantsToAim = (Flags & FSavedMove_Character::FLAG_Custom_0) != 0`. ⚠️ `Super::`는 `!CharacterOwner`에서 조기 리턴하므로(engine `:13404`) 우리 대입은 **Super의 실행 여부와 무관하게 무조건** 실행한다 |
| `ClientUpdatePositionAfterServerUpdate()` | 소유 클라 | 엔진 | — | 리플레이 **전에 `bWantsToAim`을 지역 변수로 저장하고, `Super::` 호출 후 되돌린다.** 엔진이 `bWantsToCrouch`에 하는 처리와 같다(engine `:8655`/`:8718`) |
| `FSavedMove_FPSR::GetCompressedFlags()` | 클라(저장·전송) | 엔진 | — | `Super::` 결과에 `bSavedWantsToAim ? FLAG_Custom_0 : 0`을 OR |
| `FSavedMove_FPSR::SetMoveFor` / `Clear` | 클라 | 엔진 | — | 기존 필드들과 같은 자리에서 복사 / 0 초기화 |
| `FSavedMove_FPSR::PrepMoveFor` | 클라(리플레이) | 엔진 | — | **아무것도 복원하지 않는다.** 엔진이 `PrepMoveFor` 직후 같은 move의 플래그로 `MoveAutonomous`를 부르고(engine `:8674`→`:8686`) 그 사이에 `bWantsToAim`을 읽는 코드가 없으므로 복원은 중복이다 — **왜 없는지 주석으로 남긴다**(다음 사람이 누락으로 오해하지 않게) |
| `UFPSRWeaponFireComponent::SetAiming` | 기존 계약 무변경 | 기존 호출자 전부 | **역할 가드**(권위 또는 소유 클라, `FPSRWeaponFireComponent.cpp:89`) 통과 후 | CMC 푸시 지점 = **역할 가드 뒤 · `bIsAiming == bNewAiming` 조기 리턴(`:93`) 앞**. 같은 값으로 다시 불려도 푸시가 한 번 더 일어나므로, 두 값이 어떤 이유로든 어긋나면 **다음 `SetAiming` 호출에서 스스로 복구된다**(같은 값 쓰기라 비용 0). 조기 리턴 뒤에 두면 어긋난 상태가 다음 *엣지*까지 남는다 |
| `PushEquippedWalkSpeed()` | 전 머신 | 기존 **4개** 호출지점 (`FPSRWeaponInventoryComponent.cpp` — 서버 장착 · 클라 `OnRep_CurrentSlotIndex` · `OnRep_Slots` 등. 구현 후 줄번호 = `:235`·`:343`·`:473`·`:496`) | — | 걷기 속도와 **같은 자리에서** 조준 배수도 푸시. 무기가 없거나 `bHasADS == false`면 **1.0**. 값은 `GetCurrentInstance()->GetResolvedStats()`에서 읽는다(§11-(1)) |

### `GetMaxSpeed()` 삽입 지점 — 순서가 계약이다

```
슬라이드면 조기 리턴(SlideMaxSpeed)         ← 변경 없음: 슬라이드 중 조준은 감속하지 않는다
FPSRScaled(Super::GetMaxSpeed())            ← 서면 MaxWalkSpeed, 웅크리면 MaxWalkSpeedCrouched
  × 지면 가속 램프 커브
  × 백페달 배수
  × 조준 배수   ← ★ 여기 (백페달 바로 뒤, 스탠스 전환 Lerp 앞)
  스탠스 전환 Lerp(StanceSpeedFrom → 위 결과)
```

- **왜 `Super::GetMaxSpeed()` 뒤인가**: 엔진이 스탠스에 따라 `MaxWalkSpeed`/`MaxWalkSpeedCrouched`를 골라 주므로 여기서 한 번 곱하면 **서·웅크림 둘 다** 덮인다. `RefreshWalkSpeedCap`에서 곱하면 `MaxWalkSpeed`만 바뀌어 **웅크려 조준할 때 감속이 안 걸린다**(웅크림 상한 300은 엔진이 따로 들고 있다 — `FPSRCharacterMovementComponent.cpp:293`). 콘텐츠 계획의 `AS_BS_CrouchWalk_Ironsights` Speed 축 **0~150**이 이미 300×0.5를 전제한다.
- **왜 스탠스 Lerp 앞인가**: `StanceSpeedFrom`은 스탠스가 바뀐 순간의 `GetMaxSpeed()` 결과라 **이미 조준 배수가 곱해져 있다.** Lerp 뒤에 곱하면 그 출발점에 배수가 두 번 걸린다.
- **왜 백페달 뒤인가**: 둘 다 곱셈이라 순서가 결과를 바꾸지 않는다. 기존 "백페달이 마지막" 주석을 "프레임 상태 배수 둘이 마지막"으로 읽히게 고친다.
- **공중**: 이 프로젝트의 공중 이동은 `AirStrafeWishSpeed`/`AirStrafeMaxSpeed`로 따로 돌고 `MaxWalkSpeed`가 공중 속도를 제한하지 않는다(헤더 주석 + `CalcVelocity`의 낙하 분기). 즉 **공중 조준은 감속되지 않는다** — 의도된 무변경이고 이 유닛은 그 경로를 건드리지 않는다.
- **의도된 무변경(프로젝트 소비자) — ADS 스웨이 이동 계수의 상한이 0.5가 된다**(G2 지적, 2026-09-12): `AFPSRCharacter`의 ADS 스웨이는 `속도 / BaseWalkSpeed(900)`로 "얼마나 움직이는가"를 정규화하는데, 조준 중 도달 가능한 상한이 450이므로 그 계수는 **0.5를 넘지 못한다**(그 코드 주석의 "걸으면 최대까지 올라간다"는 계약이 조준 중에는 성립하지 않게 된다). 오너 로컬 코스메틱이고 양쪽 결정적이라 **그대로 둔다** — 조준 중 실제로 느리게 움직이니 스웨이도 덜 흔들리는 쪽이 자연스럽다. 감각상 문제가 되면 정규화 기준을 조준 배율이 반영된 캡으로 바꾸는 후속 행에서 다룬다.
- **의도된 무변경(엔진의 다른 `GetMaxSpeed()` 소비자)**: 조준 중이면 아래 세 곳도 같은 배수를 본다. 전부 서버·클라 결정적이고 게임플레이 영향이 미미해 **그대로 둔다** — G2가 다시 파지 않도록 여기 적어 둔다.
  - `UCharacterMovementComponent::JumpOff` (engine `:1258`, 비보행 베이스에서 밀려나는 속도 ×0.85)
  - `ApplyImpactPhysicsForces` (engine `:7867`, 물리 오브젝트를 밀 때의 힘)
  - `PhysFalling`의 "virtual ditch" 랜덤 넛지 (engine `:5236`)

## 7. 복제표 (§6-3 서버권위 + Push Model)

| 프로퍼티 / RPC | 종류 | Push Model | 신뢰성 | 조건 | 비고 |
|---|---|---|---|---|---|
| `bWantsToAim` | **복제 안 함** | — | — | — | 예측 상태. 클라→서버는 **압축 플래그**(`FLAG_Custom_0`), 서버→프록시는 **보내지 않는다**(프록시는 이미 `bIsAiming`으로 포즈를 잡는다) |
| `AimWalkSpeedMultiplier` | **복제 안 함** | — | — | — | 양쪽이 같은 무기 데이터에서 각자 읽는다(장착 자체가 복제됨). `LoadoutWalkSpeed`와 같은 구조 |
| `FLAG_Custom_0` (조준 의도) | 기존 move 패킷 `CompressedMoveFlags` 바이트 안 1비트 | — | move 채널(엔진) | — | 그 바이트는 **옵셔널 직렬화**다(engine `:9852`, `NetSerialization.h:27-44`): 플래그 0이면 신호비트 1개, 아니면 1+8비트. 따라서 **조준 중 move당 ≤1바이트**(조준자 1명당 상행 ~60B/s), 조준 안 하면 증가 0 |
| `UFPSRWeaponFireComponent::bIsAiming` | 기존 `Replicated` | 기존 `MARK_PROPERTY_DIRTY` | — | 기존 `COND_SkipOwner` | **무변경.** 코스메틱(포즈·FOV·확산)의 진실은 그대로 여기 |
| `AFPSRCharacter::ServerSetAiming` | 기존 RPC | — | 기존 | — | **무변경.** 단 이 RPC가 서버에서 **이동 속도의 진실이 되지 않는다**는 점이 이 유닛의 핵심이다(§6 `SetWantsToAim` 가드) |

> 패키지 빌드에서 Push Model이 꺼져도(메모리 `[[push-model-off-in-packaged-build]]`) 이 유닛은 영향이 없다 — 새 복제 프로퍼티가 0개다.

**의도적으로 남기는 비대칭(수용된 위험)**: 이동 속도의 진실 = 플래그(클라 발), 조준 코스메틱·확산의 진실 = `bIsAiming`(서버 검증). 조작된 클라이언트는 "플래그는 끄고 RPC로만 조준"해서 감속 없이 ADS 확산 이득을 볼 수 있다. **수용한다** — PvE 협동이라 피해자가 없고, 막으려면 서버가 두 값의 불일치를 감지해 한쪽으로 몰아야 하는데 그 순간 시간 정렬이 깨져 **정상 플레이어에게 고무줄이 돌아온다**(이 유닛이 없애려는 바로 그 현상). §2 목표 2와 맞바꿀 수 없다.

## 8. 수명주기 · 소유권

- **생성 / 등록**: 새 객체·컴포넌트·서브시스템 **없음**. 필드 2개는 컴포넌트 수명과 동일.
- **해제 / 등록 해제**: 없음(델리게이트 0개, 타이머 0개, 동적 할당 0개).
- **GC 소유**: UObject 참조를 새로 들지 않는다 — `float`·`bool`뿐.
- **초기 동기화 / 장착 창(G1 P2-1 정정)**: `AimWalkSpeedMultiplier` 초기값 = 1.0(무의견). 실제 값은 `PushEquippedWalkSpeed()`가 넣는데, **장착은 예측하지 않는 서버권위**라 서버는 `EquipSlot`(`:343`) 시점, 클라는 `OnRep_CurrentSlotIndex`(`:457`) 시점에 갱신된다 → **RTT/2 만큼 두 머신의 배수가 다른 창이 열린다.** 이 창은 첫 장착뿐 아니라 **런 중 슬롯 교체마다**(`Input_EquipSlot1..3`) 다시 열리고, 무기 교체는 조준을 끄지 않으므로 **조준을 누른 채 라이플(0.5)→근접(1.0)으로 바꾸면** 그 창 동안 클라 450 / 서버 900 → 보정 1회가 난다(100ms RTT에 ~22cm).
  - **수용한다.** 닫으려면 장착 자체를 예측해야 하고 그것은 이 유닛 범위 밖이다. 기존 `LoadoutWalkSpeed`가 이미 같은 창을 갖고 있고(두 무기의 `WalkSpeed`가 다를 때), 이 유닛은 `WalkSpeed`가 같아도 조준 중이면 열리게 만드는 차이가 있다.
  - ⚠️ **검증자 주의**: 이 보정은 §12 검사 8(유일한 합격 기준)의 **실패가 아니다** — 장착 경로의 기존 창이다. 검사 8은 *무기 교체 없이* 조준만 누르고 떼는 시나리오로 판정한다.
- **상태 정리(불변식 8 = 프리즈/DBNO)**: 조준 의도는 `SetAiming`을 통해서만 세워지므로 **기존 4개 해제 경로가 그대로 해제자**가 된다 — 버튼 릴리스 / 설정메뉴 열기(`Input_Menu`) / 런 프리즈(`HandleRunStateChanged_Vision`) / 다운(`OnRep_LifeState`). **새 정리 경로를 만들지 않는 것**이 이 푸시 지점을 고른 이유다(메모리 `[[freeze-gate-client-server-symmetry]]`).
- **리플레이 경계 — 이 유닛에서 가장 깨지기 쉬운 곳**: 보정 리플레이는 저장된 move들의 플래그를 다시 적용하므로, 리플레이가 끝난 뒤의 라이브 `bWantsToAim`은 **마지막으로 재생된 move의 값**이 된다. 엔진은 `bWantsToCrouch`를 위해 리플레이 앞뒤로 저장·복원하지만(`:8655`/`:8718`) 커스텀 플래그는 알 수 없으니 해 주지 않는다. 복원하지 않으면 **조준을 뗀 직후 보정이 한 번 오면 감속이 영구히 남는다**(다시 누르고 뗄 때까지 450으로 걷는다). 웅크리기는 나중에 다시 눌리면 엔진이 의도를 재설정하지만 조준은 그런 재설정자가 없어 더 나쁘다 → `ClientUpdatePositionAfterServerUpdate` 오버라이드가 **필수**이며 §12 검사 7이 이것을 본다.

## 9. 데이터드리븐 경계 (핵심원칙 2)

| 값 | 나가는 곳 | 기본값 | 비고 |
|---|---|---|---|
| 조준 이동속도 배율 | `FFPSRWeaponStatBlock::ADSMoveSpeedMultiplier` (무기 DA, `Weapon\|ADS`) | **0.5** | 사용자 확정(보드 결정사항) = 900의 0.5배 450. 무기별로 다르게 저작 가능 |
| ADS 보유 여부 | 기존 `FFPSRWeaponStatBlock::bHasADS` | `false` | false면 배율을 **푸시 지점에서 1.0으로 중화** — 근접/맨손이 조준 버튼으로 느려지지 않게 |
| 조준 감속의 *적용 방식* | 코드(구조) | — | 곱하는 지점·순서는 구조라 코드에 남는다(메모리 `[[code-is-immutable-structure-only]]`) |

C++에 남는 상수는 `AimWalkSpeedMultiplier = 1.0f` 초기값뿐이고, 이것은 **항등원**(무의견)이라 튜닝값이 아니다.

## 10. 성능 예산 (핵심원칙 1)

- **틱**: 새 틱 0개. `GetMaxSpeed()`에 `bool` 검사 1회 + 곱셈 1회. 이 함수는 **플레이어당** 프레임당 몇 번 불리고 플레이어는 1~4명이다.
- **액터당 비용**: 적 스웜에 붙지 않는다 — 플레이어 전용 컴포넌트(적은 경량 `UHealthComponent` + 플로우필드 경로). 스웜 예산 영향 **0**.
- **복제 대역**: 개체당 **0바이트 증가**(§7).
- **완화**: 필요 없음(이 규모에서 측정 가능한 비용이 없다).
- 유일한 미시 영향: 조준 엣지를 넘는 move는 **병합되지 않는다**(§11-(4)) → 클라 프레임레이트가 송신율보다 높을 때 그 엣지 프레임에 move 하나가 더 간다. 웅크리기·벽점프가 이미 같은 값을 지불하고, 그쪽은 0.15~0.35초 창인데 이쪽은 **엣지 1프레임**이라 더 싸다.

## 11. 미결정 항목 · 명세 갭 처리

**(1) 조준 감속을 카드가 바꿀 수 있게 할 것인가 — 열어 둔다(사용자 결정 사항).**
`ADSMoveSpeedMultiplier`를 스탯 블록에 두었으므로 나중에 `EFPSRWeaponStat`에 축을 등재하면 카드·프래그먼트가 이 값을 바꿀 수 있다("조준하며 빠르게 움직인다" 카드). 지금은 **등재하지 않는다** — `RecomputeResolved()`가 축 enum 기준으로만 수정자를 적용하므로 미등재 필드는 base 값 그대로 통과하고, 따라서 오늘은 `GetResolvedStats()`와 `BaseStats`가 이 축에서 **항상 같다**(= 클라·서버 불일치 창이 없다). 등재하는 날 필요한 유일한 추가 작업 = `NotifyOwnerModifiersChanged` 경로에서 `PushEquippedWalkSpeed()`를 한 번 더 부르기. 그럼에도 지금부터 `GetResolvedStats()`에서 읽는 이유는, 나중에 축이 생겼을 때 **읽는 곳을 고치지 않아도 맞는 값이 되기** 때문이다.

**(2) DA 조건부 표시 — 중첩 경로는 쓸 수 없다(실측으로 닫힌 항목).**
필드를 `FPSRWeaponDataAsset`의 `무기|이동`(보드 초안 위치)에 두려면 `EditCondition = "BaseStats.bHasADS"`가 필요한데, 엔진 해석기는 **소유 struct의 평면 이름만** 찾는다(`EditConditionContext.cpp`의 `FindTypedField` → `FindFProperty(Property->GetOwnerStruct(), *FieldName)`). 못 찾으면 `LogEditCondition` 에러만 남기고 조건이 **조용히 무시**된다. 그래서 `bHasADS`가 평면 이름인 스탯 블록 안에 둔다 — 사용자 선호(메모리 `[[dataasset-conditional-field-visibility]]`)를 지키는 유일한 배치다.

**(3) ✅ 범위에 포함(사용자 승인 2026-09-12)**: `FPSRWeaponDataAsset::WalkSpeed`의 라벨·주석이 "0 = 기본 600"인데 실제 기본값은 **900**(`AuthoredBaseWalkSpeed = 900.0f`, `FPSRCharacterMovementComponent.h`). 새 필드가 900/450을 말하므로 이웃 문구가 틀린 채 남으면 저작 실수를 부른다.
- 고칠 곳 = 주석의 "`AFPSRCharacter::BaseWalkSpeed`, 600" · `DisplayName = "걷기 속도(0=기본 600)"` · 주석의 "근접/맨손이 700" 같은 파생 수치가 있으면 함께 검토. **문구만 고친다 — `float WalkSpeed = 0.0f` 기본값과 어떤 동작도 바꾸지 않는다**(값을 건드리면 모든 무기의 이동속도가 바뀐다).

**(4) 보드 초안에서 의도적으로 바꾼 것 2개** (G1·사용자가 판정할 지점):

- 초안: "`CanCombineWith`에서 플래그 다르면 false." → **추가하지 않는다.** 엔진 `FSavedMove_Character::CanCombineWith`가 이미 `GetCompressedFlags() != NewMove->GetCompressedFlags()`를 검사하며 주석이 "**any custom movement flags from overrides**"라고 명시한다(engine `CharacterMovementComponent.cpp:13160`). 게다가 저장된 `MaxSpeed`가 450 차이나므로 임계 10의 검사(`MaxSpeedThresholdCombine`)에도 걸린다. 벽점프가 손으로 가드를 짠 이유는 **플래그가 없어서**였고(그 헤더 주석이 그대로 그렇게 적혀 있다), 조준은 플래그가 생기므로 공짜다. 중복 가드는 "엔진이 안 해 준다"는 잘못된 인상을 남기므로 **코드 대신 주석**으로 남긴다.
- 초안: "`RefreshWalkSpeedCap()`에 조준 배율을 곱한다." → **`GetMaxSpeed()`에 곱한다.** 근거 3개 = 웅크림 상한 커버 · 스탠스 Lerp 이중적용 방지 · 프레임 상태 배수(백페달)가 이미 그 함수에 사는 기존 패턴(§6 삽입 지점).

**(5) G1이 올린 후속 3건 (이 유닛에서 고치지 않는다 — 근거 포함)**

- **`FindCurveTimeForSpeed`가 프레임 상태 배수를 모른다** (`FPSRCharacterMovementComponent.cpp:665-697`). 스탠스 전환 시 속도→커브시간 remap을 `Super::GetMaxSpeed()`(배수 없는 값)로 정규화하는데 실제 캡은 거기에 조준·백페달 배수가 더 곱해진다. 웅크려 조준(150)에서 일어서면 램프가 150/900=0.167 지점에서 재개돼 캡이 잠깐 낮게(≈75) 시작하고 450 도달이 ~0.17초 늦다. **양쪽 결정적이라 고무줄이 아니고**(커브 시간은 move에 저장된다), 지면 속도 커브가 배정된 경우에만 발현하며, **백페달 배수가 이미 같은 갭을 갖고 있다** — 즉 이 유닛이 만든 문제가 아니다. 고치려면 정규화 대상 속도를 프레임 상태 배수로 나누는 한 줄인데, 그건 램프 감각을 바꾸는 변경이라 별도 행에서 실측하며 해야 한다.
- **조준 엣지에 완충이 없다**(900→450이 1프레임). 스탠스 변경은 `StanceSpeedFrom`으로 이징하는데 조준은 계단이다. 양쪽 동일하므로 정합성 문제는 없고, 사용자가 확정한 것은 *450*뿐이며 전환 방식은 미정이다. **기본값 = 계단**으로 간다: 이징을 넣으면 그 이징 상태가 매 move 예측 대상이 되어(저장·복원 필요) 이 유닛이 줄이려는 발산 지점을 스스로 늘린다. PIE에서 거슬리면 후속 행에서 `StanceSpeedFrom` 방식을 재사용한다.
- **ADR 0001 문서가 이 변경으로 낡는다** — 본문에 "`FLAG_Custom_0~3` 전부 미사용"이라고 적힌 곳이 있고 도식은 `FLAG_Custom_0`을 슬라이드용으로 그려 두었다. **날짜 부기 한 줄**(선례 있음)로 정정한다 — §4 파일 목록에 포함.

**갭 처리 규칙(고정)**: 구현 중 명세에 없는 판단이 필요해지면 **추측해서 채우지 말고 멈추고 "명세 갭"으로 보고**한다. 갭은 C1으로 돌아가 명세를 고친 뒤 재개한다.

## 12. 검증 기준 (무엇을 통과라 부르는가)

| # | 검사 | 통과 조건 |
|---|---|---|
| 1 | 명세 대조 | §5·§6·§7의 선언·시그니처·삽입 지점·주석 갱신 3곳이 코드와 1:1 일치. **추가된 API는 세터 2개 + 오버라이드 3개(CMC 2 + SavedMove 1)뿐** |
| 2 | 빌드 | `Build.bat FPSRogueliteEditor Win64 Development -Project=<클론>\FPSRoguelite.uproject -WaitMutex` **Succeeded**. 판정은 로그의 `Result:` 줄로(메모리 `[[build-exit-code-lies-grep-result]]`) |
| 3 | 유니티 검증 | 헤더를 고치므로 `-DisableUnity` 풀빌드 1회(67초, 메모리 `[[nonunity-build-is-67-seconds]]`) + 푸시 전 1회 `-DisableAdaptiveUnity -ForceUnity`(§6-6) |
| 4 | 헤드리스 스모크 | `Automation RunTests FPSRoguelite.Smoke.ModuleLoads` 통과. `Scripts/run_*.bat` 경유(메모리 `[[headless-editor-use-bat-runners]]`), 판정은 stdout으로(메모리 `[[automation-abslog-truncated-read-stdout]]`) |
| 5 | 레드팀 게이트 | G2 = §6-6-1. **P1 잔존 시 푸시 금지.** 결과는 §13에 |
| 6 | 회귀(코드 대조) | 슬라이드·벽점프·스탠스 블렌드·백페달·다운 속도의 기존 경로 변화 0. `bIsAiming` 복제 설정·`ServerSetAiming` 게이트·`OnAim` 훅 변화 0 |
| 7 | 리플레이 복원(**사용자 PIE**) | 2인 PIE에서 `Net PktLag=100`(또는 `PktLoss`)을 켜고 **조준을 여러 번 짧게 누르고 뗀 뒤**, 조준하지 않은 상태의 걷기 속도가 900으로 돌아오는지(이동 디버그 readout 또는 체감). 450에 남으면 §8의 리플레이 복원이 깨진 것 |
| 8 | 합격 기준(**사용자 PIE — 이 작업의 유일한 필수 검증**) | **2인 PIE에서 조준을 누를 때 고무줄이 없을 것.** 호스트가 아닌 클라이언트로 접속해 걷는 중 ADS를 누르고 떼며 캐릭터가 뒤로 끌리거나 튀지 않는지. **판정 범위 = 무기 교체 없이 조준만**. 조준을 누른 채 슬롯을 바꿀 때 나는 보정 1회는 §8의 기존 장착 창이므로 이 검사의 실패로 세지 않는다 |
| 9 | 무기별 동작(**사용자 PIE**) | 근접/ADS 없는 무기로 조준 버튼을 눌러도 **속도 변화 없음**. 라이플은 서서 450, 웅크려 150 |

> Claude는 게임을 직접 실행하지 않는다(메모리 `[[do-not-launch-game]]`) — 7·8·9는 사용자 검증 항목이고, 그동안 보드 상태는 `검증중`으로 둔다.

## 13. 레드팀 지적 원장 (C3에서 채운다)

**G2 = Fable 레드팀 서브에이전트, 2026-09-12.** 판정: **P1 0건 → 푸시 허용**(§6-6-1). 총계 P1 0 · P2 1 · P3 9.

- **레드팀에 무엇을 줬나**: 리뷰 대상 = 푸시 단위 `git diff origin/main..HEAD`(커밋 2개 — 이 유닛 `e282061a` + 이전 세션의 `20b51771` GASM1, 후자는 게이트 통과 여부 불명이라 범위에 포함) · `Docs/InternalRedTeamReview.md` 경로 · 이 명세 · 리포·엔진 소스 읽기 권한 · 빌드·스모크 결과 · **G1 이후 구조 결정 0건 + 주석 문안 3건 변경 사실**(§6-5-2의 G1↔G2 간극 규율). 설계 변호·토론 이력·"어디를 봐 달라"는 유도는 싣지 않았다.
- ⚠️ **게이트 이후 변경(재리뷰 안 됨)**: 아래 처리 중 코드 수정 3파일(`FPSRDebugExec.cpp` 약한 바인딩 1줄 + 주석, CMC 헤더 주석 2곳, `FPSRWeaponDataAsset.h` 주석 수치)과 문서 4파일이 G2 **이후**에 들어갔다. 전부 **지적에 대한 대응**이며 구조 변경 0건이다. Fable 호출 상한(코어 갈래당 2회)을 이미 소진해 재리뷰는 하지 않았고, 대신 빌드·스모크를 재실행했다.

| 심각도 | 지적 (요약 + 파일:줄) | 처리 | 근거 |
|---|---|---|---|
| **P2** | `FPSR.Debug.ExecAfter`가 raw `UWorld*`를 게임인스턴스 소유 타이머에 붙잡아, 맵 이동 후 발화하면 죽은 월드로 `Exec` → use-after-free (`Private/Core/FPSRDebugExec.cpp:70-74`, **커밋 `20b51771`**) | **수용·수정** | 엔진 직접 확인: `UWorld::GetTimerManager()`는 `OwningGameInstance`의 매니저를 돌려주고(`World.cpp:8056`) 월드 테어다운이 비우지 않는다. 코드 주석이 근거로 든 "월드가 타이머를 소유" 전제가 틀렸다. 엔진 자신의 대응책(`CreateWeakLambda(World, …)`, `World.cpp:5831`)을 그대로 적용 |
| P3 | "이 컴포넌트의 유일한 와이어 비용 = 조준 비트"가 `bSlidingVisual`·`SlideVisualSerial` 복제를 빠뜨림 (`Public/Hero/FPSRCharacterMovementComponent.h:85`·`:780`) | **수용·수정** | 같은 컴포넌트가 두 프로퍼티를 `COND_SkipOwner`로 복제한다(`cpp:201-202`). 주석을 "클라→서버 move 스트림 기준"으로 한정하고 반대 방향 복제를 명시 |
| P3 | `WalkSpeed` 주석의 파생 예시가 정정 후 자기모순("걷기 600이면 900, 700이면 1000"; 700×1.5=1050) (`Public/Weapon/FPSRWeaponDataAsset.h:333`) | **수용·수정** | 명세 §11-(3)이 "파생 수치 함께 검토"를 범위로 명시했는데 누락. 900→1350 / 700→1050 + `SlideMaxEntrySpeed` 상한 언급으로 재작성 |
| P3 | ADR 0001의 stale 서술 2곳에 부기 누락 — 도식(`:105`)과 벽 매달리기 절(`:379`) (`Docs/Architecture/0001-...md`) | **수용·수정** | 명세 §11-(5)가 도식 정정을 약속했는데 구현이 다른 2곳만 달았다. 두 곳에 같은 형식의 날짜 부기 추가 |
| P3 | 조준 감속 규칙이 SSOT 도메인 파일에 없음 (`Docs/SSOT/PlayerFeel.md`·`CombatWeaponCard.md`) | **수용·수정** | `CLAUDE.md` 핵심 3 = "설계 변경은 해당 도메인 파일 먼저". PlayerFeel §2-9 조준 항목 신설 + CombatWeaponCard의 ADS 두 줄에 배율 추가 |
| P3 | 불변식 3의 두 번째 예외(비-어트리뷰트 이동 수치)가 생겼는데 ADR 미갱신 | **수용·수정** | 불변식 표 아래에 예외 2건(`WalkSpeed`·`ADSMoveSpeedMultiplier`)과 수용된 장착 창을 날짜 부기로 명시 |
| P3 | 조준 중 ADS 스웨이 이동 계수 상한이 0.5로 고정 — 명세가 "기존 ADS 경로 무변경"이라 선언했으나 실제로는 바뀜 (`Private/Hero/FPSRCharacter.cpp:3700-3701`) | **수용 — 문서화(코드 무변경)** | 오너 로컬 코스메틱 + 양쪽 결정적. 조준 중 실제로 느리니 스웨이가 덜 흔들리는 쪽이 자연스럽다 → §6 "의도된 무변경"에 명시. 감각 문제 시 후속 행 |
| P3 | 명세 §6의 `PushEquippedWalkSpeed` 호출 줄번호가 구현 후 실제와 불일치(`:457`·`:480` → `:473`·`:496`) | **수용·수정** | 구현으로 함수가 길어져 줄이 밀렸다. 명세 표 갱신 |
| P3 ×2 | GASM1(`20b51771`)의 `SetIsReplicated` 안전 근거 주석이 역전 (`Private/Enemy/FPSREnemyBase.cpp:719-720`) · 스웜 베이스 헤더가 Shipping에서도 GAS 헤더를 무조건 include (`Public/Enemy/FPSREnemyBase.h:11`) | **보류 — 후속(이 유닛 밖)** | 둘 다 다른 작업(GASM1)의 산출물이고 동작 결함이 아니다(주석 오류 / include 위생). 헤더 include는 `#if` 가드 안으로 옮기면 UHT 통과 확인이 필요해(메모리 `[[uht-ignores-shipping-guard]]`) 별도 검증이 붙는다 → GASM1 담당 행으로 넘긴다 |

**범위 밖 발견(레드팀이 함께 보고, 이 푸시에서 고치지 않음)**: ① `FPSRPlayerController.cpp:912-977`의 SkipCards/Invuln 타이머가 위 P2와 **같은 결함**(이미 푸시된 기존 코드) ② `RefreshWalkSpeedCap`이 웅크림 상한에 카드·장착 배수를 못 걸는 기존 제약 ③ `GetResolvedStats()`가 클라에서 PlayerState 미도착 시 캐시를 굳혀 AllWeapons 모디파이어를 놓칠 수 있는 기존 결함. ①은 실패 모양이 P2와 동일하므로 후속 우선순위가 가장 높다.
