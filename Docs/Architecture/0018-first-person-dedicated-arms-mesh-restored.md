# 0018. 1인칭 = 전용 팔 메시를 다시 그린다 — 스켈레톤은 3인칭과 공유하고, 차이는 오버라이드로 낸다

- 상태: 채택 (사용자 결정 2026-09-13 · **PIE 검증 ✅ 2026-09-13** — 팔·총 표시 정상, 왼손 그립 정상, 1P/3P 오프셋 분리 후 회귀 없음)
- 날짜: 2026-09-13
- 되돌리기 비용: 보통 (BP 플래그 두 칸이면 화면은 되돌아가지만, 그 순간 손 IK·그립 오프셋 튜닝이 다시 밀린다)
- 대체 대상: [ADR 0015](0015-first-person-gun-only-hidden-arms-driver.md) — **"총만 그린다"와 불변식 I1·I2·I4**. 총 앵커 독립(I3)과 `SOCKET_Weapon` 진실원천(I5)은 **계승**
- 계승 대상: [ADR 0003](0003-first-person-arms-camera-anchored.md) — 카메라 부착 · 독립 애님그래프 · **절차 모션(스웨이·bob·킥·ADS)은 C++ 소유** · PWAS 커널 미사용. 전부 유효

## 맥락

0015 이후 두 가지가 바뀌었다.

1. **에셋 기반이 통째로 갈렸다.** 0015 가 전제한 구매 리그(LPAMG)와 그 리타깃 클립은 2026-09-10 대청소에서 삭제됐고, 뒤이어 VoxelChar 트랙도 폐기됐다(`9921a120`). 캐릭터는 `Anime_LowPoly` 팩(바디 `SK_Girl_004`), 스켈레톤은 `SK_UE5_Mannequin_Skeleton` 으로 교체됐다. 즉 0015 의 "숨긴 팔이 총을 구동한다"에서 **팔이 사라져 있었다.**
2. **화면에 여러 방식이 겹쳐 있었다.** 사용자 지시(2026-09-13): *"여러가지 방식이 혼재되어 있어 교통정리가 필요하다. 3인칭으로 머리를 없앤 다음 표시하는 방식으로 되어 있는데, 다시 1인칭 팔을 사용하는 방식으로 바꿔야 한다."*

실측해 보니 "내 몸을 안 보이게 하는 일"을 **세 군데서 따로** 하고 있었다.

| 층 | 실체 | 문제 |
|---|---|---|
| A. 머리 본 숨김 | `HideBoneByName(HeadBoneName)` (`FPSRCharacter.cpp:1178-1201`) | 0002 의 true-FP 잔재 |
| B. 바디 OwnerNoSee | BP `CharacterMesh0.bOwnerNoSee = true` | A를 무의미하게 만듦 |
| C. 바디 WSR | 분할 시 코드가 `WorldSpaceRepresentation` 설정 | B와 중복 |
| D. 드라이버 | `FirstPersonArms` 를 `Hidden in Game` 으로 숨겨 모션만 쓰는 구조 | 팔을 그리지 않으려고 팔을 살려 두는 우회 |

## 결정

1. **1인칭은 전용 팔 메시를 그린다.** `FirstPersonArms` = 팔 메시(현행 `SKM_StaticMesh_Test1`, 레퍼런스 포즈 바운드 z 61.5~98.2) · `bHiddenInGame = false` · `bOnlyOwnerSee = true` · `AlwaysTickPoseAndRefreshBones` · 카메라 자식 (−4, 10, −100) / yaw −95.
2. **스켈레톤은 3인칭 바디와 공유한다** (`SK_UE5_Mannequin_Skeleton`). **FPS 전용 스켈레톤을 복제하지 않는다**(안 A 기각).
3. **시점별로 갈라야 하는 값은 두 가지 오버라이드로 낸다.**
   - **소켓** — 메시 소켓이 같은 이름의 스켈레톤 소켓을 덮는다. 엔진 실측: `SkeletalMesh.cpp:5238`(에디터 `FindSocketAndIndex`)와 `:5380`(쿠킹 `RebuildSocketMap` — 스켈레톤 소켓은 `!Contains` 일 때만 추가) → **에디터와 패키지가 같은 우선순위**다. 1인칭 총 위치는 팔 메시의 `SOCKET_Weapon` 이 정하고, 3인칭은 스켈레톤 것을 그대로 쓴다.
   - **그립 보정** — `FirstPersonLeftHandGripOffset` / `FirstPersonRightHandGripOffset` 신설(1인칭 전용). 기존 `LeftHandGripOffset` / `RightHandGripOffset` 은 **바디 경로 전용**으로 남는다.
4. **몸은 `bOwnerNoSee` 로 숨긴다. 머리 본 숨김은 퇴역한다**(`HeadBoneName = None` — 코드에 이미 있는 "데이터로 비활성" 분기를 쓴다. 코드 삭제 아님).
5. **애님 소스 = PWAS 무기군 포즈 + 장전 몽타주.** `A_FP_Rifle_Pose`(비-애디티브 · 1프레임 · RateScale 0 = 정적 그립 포즈)를 Sequence Evaluator 로 깔고 `DefaultSlot` 몽타주를 얹는다. 원본이 `S_Mannequin` 소속이지만 **호환 스켈레톤 양방향 등록**으로 리타깃 없이 재생한다(뼈 이름 차집합 0, 실측). 절차 모션은 0003 대로 **C++ 컴포넌트 트랜스폼**이 계속 소유한다 — PWAS 커널은 여전히 쓰지 않는다.
6. **손 IK 재가동**(0015 I4 뒤집힘). 왼손 = 핸드가드 파츠의 `SOCKET_LeftHand` 로 TwoBoneIK, 조인트 타깃은 `lowerarm_l`(Bone Space) = 애니가 만든 팔꿈치 방향 유지. 오른손은 **IK 가 아니라 총 앵커 CopyBone** 이 잡는다(`RightHandSocket = None` 유지).

## 근거 (핵심원칙 4 — 3줄)

1. **제1원리** — 0015 의 근거였던 *"손을 제대로 붙이는 비용이 무기 수만큼 계속 청구된다"* 는 전제가 무너졌다. 그립 소켓은 이제 **무기 팩이 이미 저작해 제공**하고(`SM_Wep_Mod_A_Handguard_03` 의 `SOCKET_LeftHand`), 보정은 무기마다가 아니라 **팔 리그당 한 번**이면 끝난다 — 그래서 오프셋이 무기 DA 가 아니라 캐릭터에 있다. 비용이 무기 수에 비례하지 않으므로 0015 의 계산이 성립하지 않는다.
2. **엔진 기본값·기존 인프라와의 관계** — 시점별 차이를 내는 수단으로 엔진이 이미 제공하는 것이 **소켓 오버라이드**다(§3). 스켈레톤 복제는 그 위에 애님 호환·슬롯 그룹·리타깃 소스·가상 본·블렌드 프로파일을 **두 벌 동기화하는 비용**을 새로 만들고, 어긋났을 때의 실패가 조용하다(포즈가 안 물리거나 몽타주가 침묵). 코드도 새 인프라를 만들지 않았다 — `ComputeGripInGunFrame` · `RefreshHandGripInGunFrameCache` · `LeftGripInGunLocation/Rotation` · `LeftHandIKAlpha` 는 **0015 가 휴면시켜 둔 것을 그대로 깨운 것**이고, 신설은 필드 두 개뿐이다.
3. **프로젝트 제약과의 정합** — 4인 협동 기준선(메모리 `reason-in-multiplayer-terms`). 그립 오프셋을 1P/3P 로 가른 이유가 정확히 이것이다: 내 화면에서는 분할이 켜져 무기가 팔에 붙으므로 바디의 왼손 IK 가 `IsAttachedTo` 게이트에 막혀 꺼지지만, **원격 클라가 보는 내 캐릭터**는 분할이 없어 바디 IK 가 켜지고 같은 값을 쓴다. 한 값을 두 리그가 나눠 쓰면 한쪽을 맞출 때마다 다른 쪽이 틀어진다. 서버권위·복제는 무접촉(렌더 상태는 전부 로컬).

## 불변식

| # | 문장 | 없으면 깨지는 것 | 등급 |
|---|---|---|---|
| I1 | 1인칭 팔은 **그린다**(`bHiddenInGame=false`) + `bOnlyOwnerSee=true` + `AlwaysTickPoseAndRefreshBones` | 팔이 사라지거나(0015 로 되돌아감), 총이 마지막 포즈에 굳는다 | 구조적 |
| I2 | 내 몸을 안 보이게 하는 수단은 **`bOwnerNoSee` 하나**다. 머리 본 숨김·마스크로 중복해 숨기지 않는다 | 같은 일을 세 군데서 하게 되고(맥락 표), 한 곳을 고쳐도 화면이 안 바뀐다 | 구조적 |
| I3 | 총 앵커 CopyBone(hand_r→ik_hand_gun) Alpha 는 **1.0 고정**, 손 IK 알파와 독립 (0015 I3 계승) | IK 를 끄는 순간 총이 가슴 앞에 뜬다(Troubleshooting E5) | 구조적 |
| I4 | 시점별 차이는 **메시 소켓 오버라이드 + 1P 전용 오프셋 필드**로 낸다. 스켈레톤을 복제하지 않는다 | 스켈레톤 두 벌 동기화 비용 + 조용한 실패 | 구조적 |
| I5 | 1P 애님 소스는 **호환 스켈레톤 등록**에 의존한다. 새 클립을 붙일 때 소속 스켈레톤을 확인한다 | 어긋난 본으로 조용히 재생된다(Troubleshooting A19) | 운영 |
| I6 | 그립 보정 필드를 **1P/3P 로 다시 합치지 않는다** | 원격 프록시의 손과 내 손이 같은 값을 두고 싸운다 | 구조적 |
| I7 | 팔 메시 슬롯을 비우지 않는다 (0015 I2 후단 계승) | 분할이 꺼지는데 몸은 `bOwnerNoSee` 로 숨어 있어 **소유자가 빈 화면을 본다** | 구조적 |

## 검토한 대안

### 안 A — FPS 전용 스켈레톤을 복제한다 — **기각**

사용자 제안(*"1인칭은 소켓 위치가 달라야 할 것 같은데 스켈레톤을 복제해서 FPS용으로"*). 목적이 **소켓 위치 하나**라면 §3의 오버라이드로 충분하고(엔진 소스 확인), 복제는 팔 메시 스켈레톤 재지정 · ABP 타깃 교체 · 호환 등록 3쌍 · `DefaultSlot` 슬롯 그룹 · 리타깃 소스 · 가상 본 · PhysicsAsset 을 전부 다시 잇게 만든다. **값어치가 생기는 시점은 "1인칭 전용 *본*이 필요할 때"** — 카메라 본 · 무기 보조 본 · 1P 전용 IK 체인처럼 3인칭에 있으면 안 되는 뼈가 필요해지면 이 기각을 뒤집는다(그때도 애님 공유는 호환 등록으로 유지된다).

### 안 B — PWAS 데모의 팔 메시 `SK_FP_Manny_Simple` 을 쓴다 — **기각(불필요)**

세션이 프로젝트 팔 메시를 **재보지 않고** "풀바디"로 단정해 이 메시로 교체했다가 사용자 정정으로 원복했다. 실측하면 `SKM_StaticMesh_Test1` 이 팔 메시가 맞다(바운드 반경 37.5 · z 61.5~98.2). 대안 자체는 유효하다 — 팔만 있는 메시(PhysicsAsset 이 `upperarm/lowerarm/hand` 6본만) + 자체 `SOCKET_Weapon` 보유 — 프로젝트 팔이 없어지면 되살릴 후보로 남긴다.

### 안 C — 3인칭 클립을 `CopyPoseFromMesh` 로 드라이버에 복사한다(UE5 1인칭 템플릿 방식) — **기각**

스켈레톤을 공유하므로 성립하고 로코모션이 자동으로 따라온다. 그러나 0003 이 이미 기각한 방향이다 — 1P 프레이밍이 3P 카메라 기준이고, 우리 절차 모션층과 이중으로 움직여 조준선이 흔들린다. 걷기 bob 만 필요해지면 **그 항만 절차층에 추가**하는 편이 싸다.

### 안 D — true-FP(바디를 그리고 머리만 숨김) 유지 — **폐기**

0002 의 잔재. 바디를 통째로 안 그리는 이상 머리만 지우는 층은 아무 일도 하지 않으면서 "왜 안 바뀌지"를 만든다.

## 검증 시나리오 (사용자 PIE)

- [x] 1인칭에 **팔과 총이 보이고 몸은 안 보인다** ✅ 2026-09-13
- [x] 왼손이 핸드가드를 잡는다 — 초기엔 손이 그립을 넘어갔고, 원인은 *IK 가 손목 본을 소켓에 놓는데 소켓은 손바닥 접촉선에 저작돼 있어서*였다. `FirstPersonLeftHandGripOffset = (−10, −20, 0)` 로 해결 ✅
- [x] 오프셋을 1P 전용 필드로 옮긴 뒤 **화면이 그대로다**(회귀 없음) ✅ 2026-09-13
- [ ] 원격 프록시(2인 PIE)의 왼손 위치 — 3P 오프셋은 현재 0이라 별도 튜닝 필요
- [ ] 장전 몽타주 재생 중 왼손 거동 (아래 반론 참조)
- [ ] 무기 교체(비무장·근접)에서 왼손이 허공을 잡지 않는다 = `LeftHandIKAlpha` 0 확인

> F8(eject)로 보면 3인칭 모델이 보이는 것은 **정상**이다. `bOwnerNoSee` 는 뷰의 `ViewActor` 가 그 액터일 때만 숨긴다(`PrimitiveSceneProxy.cpp:1588`).

## 논의 중 제기된 반론 · 미해소

- **1인칭에서 자기 그림자가 없다.** 분할이 켜지면 코드가 바디를 `WorldSpaceRepresentation` 으로 넘기는데, 엔진은 그 경로가 **VSM 의 1인칭 그림자 경로**에 받히는 것을 전제한다(`PrimitiveSceneProxy.cpp:630-648` — FirstPerson 프리미티브는 그림자 플래그 전부 강제 off, WSR 은 `bOwnerNoSee=true` + `bCastHiddenShadow=false` 강제). 이 프로젝트는 VSM 이 꺼져 있어 그 경로가 없다 → `None + OwnerNoSee + CastHiddenShadow` 로 정리해야 한다. **별도 행.**
- **BP 의 `bOwnerNoSee=true` 가 코드의 페일세이프를 무력화한다.** `RefreshFirstPersonRendering` 은 "팔 메시가 없으면 분할을 끈다"로 빈 화면을 막는데, BP 플래그는 그때도 몸을 숨긴 채로 둔다. I7 이 이를 운영 규칙으로 막고 있을 뿐 구조적 해결은 위 항목과 같은 자리에서 해야 한다.
- **드라이버 메시 이름이 테스트 자산이다**(`SKM_StaticMesh_Test1`, `/Game/Anime_LowPoly/` 루트). 정식 경로·이름으로 옮겨야 한다. **별도 행.**
- **장전 중 왼손이 안 떨어진다.** `A_FP_RifleReload`·`AM_FP_RifleReload` 에 `LeftHandIKWeight` 커브가 없고(실측), 코드 계약이 "커브 없음 = 1"이라 IK 가 계속 잡는다. 커브를 저작하기 전까지 탄창 동작이 눌린다.
- **오른손 IK 는 여전히 휴면**(`RightHandSocket = None`). 조인트 타깃(`lowerarm_r`)만 미리 채워 뒀다 — 켜는 순간 팔꿈치가 컴포넌트 원점으로 꺾이는 함정을 없애기 위해서다.

## 영향받는 기존 결정

- [0015](0015-first-person-gun-only-hidden-arms-driver.md) — **대체.** "총만 그린다"(결정 1·2)와 I1·I2·I4 는 무효. I3(총 앵커 독립)·I5(`SOCKET_Weapon` 진실원천)는 계승. 보류였던 ARM2(손만 표시)는 **불필요해졌다** — 팔 전체를 그리므로.
- [0006](0006-first-person-arms-purchased-rig-retargeted.md) — 리그(LPAMG)가 삭제되어 **에셋 기반은 소멸**했으나, 구조(리타깃 팔 + 독립 ABP + `ik_hand_gun` 총 앵커)는 이 문서가 이어받는다.
- [0003](0003-first-person-arms-camera-anchored.md) — 계승. **PWAS 커널 미사용**과 "절차 모션은 C++ 소유"가 특히 그대로다.
- **CHR1 명세** `Docs/Specs/CHR1_ModularCharacterParts.md`(rev.6, 착수 승인 대기) — 전제 두 개가 깨졌다: ① *"`FirstPersonArms` 슬롯은 비어 있어야 한다 · 그 기계는 휴면이다"* → 이 문서가 정반대로 못 박았다(I1·I7) ② 파츠 기반 에셋(VoxelChar)이 삭제됐다. **명세 개정 또는 범위 재정의 필요.**
- **`Docs/SSOT/PlayerFeel.md` §2-9 · `Docs/SSOT/Concept.md` FP 팔 항목** — 본 ADR 을 가리키는 정정 한 줄 추가.
- 1인칭 팔 구조를 다룰 때 읽는 순서 = **0003 → 0006 → 0015 → 0018**.
