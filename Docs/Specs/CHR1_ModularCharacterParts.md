# CHR1 — 모듈형 플레이어 캐릭터 파츠 (3P Leader Pose + 1P Copy-Pose 드라이버)

## 1. 메타

| 항목 | 값 |
|---|---|
| 유닛 ID / 이름 | **CHR1** — 모듈형 플레이어 캐릭터 파츠 |
| 브랜치 | `main` (트렁크 기반) |
| 작성 모델 | **`claude-opus-5`** — §6-5-2 개정(2026-08-26)으로 C1 설계는 Opus, Fable 은 G1·G2 검증자 |
| 작성일 / 최종 갱신 | 2026-09-11 (**rev.6** — G1 최종 회차 반려 반영) |
| 상태 | `초안` (G1 지적 전량 반영 완료 · 착수 승인 대기) |
| 관련 SSOT | `Docs/SSOT/Workflow.md` §6-5-2 · `Docs/SSOT/Architecture.md` |
| 관련 ADR | [0003](../Architecture/0003-first-person-arms-camera-anchored.md) · [0015](../Architecture/0015-first-person-gun-only-hidden-arms-driver.md) — 확정 후 ADR 0018 작성 |
| 관련 메모리 | `[[fp-gun-only-hidden-arms]]` · `[[reason-in-multiplayer-terms]]` · `[[event-halves-authority-vs-client]]` · `[[code-is-immutable-structure-only]]` · `[[da-edits-are-user-work]]` · `[[bp-node-edits-are-user-work]]` · `[[push-model-off-in-packaged-build]]` |
| 보드 행 | [VoxelChar 모듈형 파츠 조립 (Leader Pose)](https://app.notion.com/p/3d73972ddd88816290aefef3e3995ef1) · 담당 `FPSRoguelite@main · 0911-모듈형파츠` |

> **rev.6 변경 요약** (G1 최종 회차 반려 → 지적 전량 반영. P1·P2 는 메인이 도구로 재대조해 **전부 사실 확인**). ① 🔴 **P1 정정** — 「C++ 변경은 게이트 예외 1건뿐」이 **거짓**이었다. `GetRightHandGripTransform` 은 **존재하지 않고** `UFPSRCharacterAnimInstance` 의 우측 멤버는 **0개**(좌측 16). 좌측을 보고 대칭을 가정한 오류. **우측 절반 신설 + 드라이버 전용 게이트**로 교체하고, *바디는 오른손 IK 를 하면 안 된다*(총이 자기 `hand_r` 에 타서 되먹임)는 비대칭을 C++ 이 판정하도록 못박음(§5-1). ② 🔴 **틱 선행 1줄 필요** — `UpdateAimDownSights` 는 `TG_DuringPhysics`, 스켈레탈 메시는 `TG_PrePhysics` 라 드라이버가 항상 먼저 틱해 **1프레임 늦은 총 위치**를 읽는다. `AddTickPrerequisiteComponent(WeaponFire)`(§8). rev.2 의 "수동 선행 불필요"는 IK 가 들어오며 거짓이 됐다. ③ 🪤 **엘보 본 기본값이 이 스켈레톤에 없다** — `lower_arm_L` vs 실제 `lowerarm_l` → IK 무음 실패. ABP 기본값으로 교정(§4·§5-1). ④ **DA 소켓 저작이 3P 바디 왼손 IK 도 켠다**는 부수효과 + **몽타주에 IK 커브 0건** + **CopyPose 의 `bCopyCurves` 기본 false** 를 §2 에 명시하고 §12 에 판정 추가. ⑤ **색 배정 주체 확정** — 두 GameMode 가 형제라 `GenericPlayerInitialization` 양쪽에서 호출, `ApplyPlayerColor()` 를 재조립과 분리, **모든 머티리얼 슬롯** 순회. ⑥ §12 절차 자기모순 해소(트래블 판정을 별도 항목 6-d 로 분리).
>
> **rev.5 변경 요약** (레퍼런스 조사 — Epic 모듈형 문서 · Lyra · 플로팅 림즈 사례 — 결과 반영). ① 🔵 **플레이어별 색을 범위에 포함**(사용자 결정: *"4명이 각자 다른 색을 가져야 시인성에 좋다"*) — 머티리얼 **파라미터** + 복제되는 `PlayerColorIndex`. 🪤 `LobbySeatIndex` 재사용 불가(트래블에서 안 넘어감), 색 **값**의 정본은 `ArtDirection.md` §A-3-5 이고 **관계형↔정체형 충돌은 아트 SSOT 결정**으로 넘김(§5-2). ② **파츠 자체 물리·고유 애니 불가**를 콘텐츠 저작 규칙으로 명문화(흔들리는 장식은 이 구조로 못 만든다 — Epic 문서). ③ **드로우콜 실수치와 머지 전환 조건**을 §10 에 기록(파츠 40 vs 머지 8 · 4인 기준) + **머지로 가면 모프 타깃이 막힌다**는 분기점 명시. ④ 플로팅 림즈의 **이음새 문제 부재**를 §3-3 에 근거로 기록 · Lyra 와의 의도적 차이 2건(컴포넌트 vs 액터 / PlayerState vs Controller 컴포넌트) 명시.
>
> **rev.4 변경 요약** (사용자 요구 2건 — G1 3회차는 이 요구가 들어오면서 판정 대상이 낡아 중단). ① 🔵 **1인칭 양손 IK 를 범위에 포함**(사용자 결정) — 붙는 자리는 **무기 파츠 단위**. 조사 결과 **파츠 단위 그립 해석은 이미 구현돼 있고**(`RebuildPartsFromSelection:2699-2723` 가 매 재조립마다 소켓을 가진 파츠를 다시 찾는다) 필요한 C++ 는 **게이트 예외 1건**뿐이다(§5-1). 종전 rev.3 이 "조준 중 분리 = 알려진 결과"로 적었던 항목은 **철회** — 그 판단은 "ADS 해 대상 변경"과 "드라이버 그래프에서 손을 당기기"를 잘못 묶은 것이었다. ② 🔧 **정정** — `FirstPersonArms` 슬롯이 빈 것은 에셋 삭제의 부수효과가 아니라 **교체하려고 비운 것**(사용자). 범위 축소의 근거가 "안 도니 놔두자"에서 **"교체의 앞 절반을 먼저 세운다"**로 바뀐다. ③ 드라이버 ABP 부모를 `UAnimInstance` → **`UFPSRCharacterAnimInstance`** 로 확정(IK 값을 그 클래스가 계산한다).
>
> **rev.3 변경 요약** (G1 2회차 반려 → 사용자 결정 반영). ① 🔴 **3P 파츠의 렌더 타입을 `WorldSpaceRepresentation` → `None` + `OwnerNoSee` + `CastHiddenShadow`** 로 교체 — 이 프로젝트는 **VSM 이 꺼져 있어** 종전 안이면 소유자 몸 그림자가 통째로 사라진다(§3-2) ② **조준 중 손-총 분리**를 §2 의 「알려진 결과」로 명시하고 §12-7 ⑥ 검사 항목으로 넣음(사용자 결정: 후속 유닛) ③ 쿡 경로를 `/Game/Characters` → **`/Game/Characters/VoxelChar`** 로 좁힘(전자는 매너퀸 125MB 를 재귀로 끌고 온다) ④ `PossessedBy` 계약 교정 — 폴백 뒤 **항상** 재조립(안 그러면 리스폰·트래블에서 반쪽만 돈다) ⑤ **`FirstPersonArms.AnimClass = None`** 을 범위에 포함(끊어진 참조 2건 제거, 사용자 결정) + **「팔 슬롯은 비어 있어야 한다」 불변식**과 ADR 0015 I2 폐기 명시 ⑥ 드라이버 컴포넌트 규약·리더 숨김 조건·조립 순서를 §6 에 명문화 ⑦ `FPSR.SelectPartSet -1` = null · 데디 서버 조기 반환 · 빌드 2종 · §12-5 를 "기준선 확립"으로.
>
> **rev.2 변경 요약** (G1 1회차 반려 → 사용자 결정 반영). ① `FirstPersonArms` 및 그 주변 기계를 **건드리지 않는다**(사용자 결정 2026-09-11) → P1-1 이 지적한 71참조·8파일 파급이 범위에서 빠지고 `RefreshFirstPersonRendering` 도 무수정 ② 검증 불가(P1-2) → **BP 기본 파츠 세트 + 콘솔 명령**으로 해소 ③ 쿡 누락(P2-1) → §9 에 행 신설, 종전 §11-1(로드 보장)은 **전제가 틀려 삭제** ④ 바운드(P2-3) → `bUseBoundsFromLeaderPoseComponent = **false**` ⑤ 드라이버를 `GetMesh()` 자식으로(P2-4) → `CopySourceMesh` C++ 프로퍼티·BP 배선·수동 틱 선행이 **전부 불필요** ⑥ 1인칭 FOV 플래그(P2-5) 는 이번 범위에서 켜지 않는다 ⑦ P3 다수 반영(Transient · public · 틱옵션 근거 교정 · 줄번호 · 머리그림자 문구).

## 2. 목표 / 비목표

**목표**
- 플레이어 몸이 **부위별로 교체 가능**해진다. 4인 협동에서 각자 다른 외형이 **호스트·원격 클라 양쪽에서** 보인다.
- 파츠 세트는 **콘텐츠(DataAsset)가 정한다** — 슬롯 수·메시·머티리얼·표시 시점 전부.
- **1인칭과 3인칭이 컴포넌트로 갈린다** — 같은 파츠 에셋을 쓰되 렌더 상태·포즈 경로가 독립. 1인칭에 손만 내보내고, 나중에 1인칭만 워프·FOV 를 손댈 자리가 생긴다.
- 🔵 **1인칭 양손이 총을 따라간다 (사용자 결정 2026-09-11)** — 조준·힙모션으로 총이 카메라 해를 따라 움직여도 손이 붙어 있다. 붙는 자리는 **무기 파츠 단위**다: 무기 DA 가 그립 **소켓 이름**을 정하고, 그 소켓을 가진 **파츠 컴포넌트**가 타깃이 된다 → 런 중 파츠가 교체되면 파지도 따라 옮겨간다.
- 🔵 **4인이 서로 다른 색으로 보인다 (사용자 결정 2026-09-11 — 목적은 코스메틱이 아니라 *시인성*)** — 누가 누군지 한눈에 갈린다. 색은 **에셋 교체가 아니라 머티리얼 파라미터**로 바뀌므로 색 조합마다 머티리얼 인스턴스를 만들지 않는다. 모든 클라에서 같은 플레이어가 같은 색으로 보여야 하므로 **복제되는 색 인덱스**를 둔다(§5-2).
- 슬롯을 **비울 수 있다**(파츠 null = 미표시).

**비목표** (일부러 하지 않는 것)
- 🔴 **`FirstPersonArms` 와 그 주변 기계를 건드리지 않는다** (사용자 결정 2026-09-11). 대상 = `FirstPersonArms` 컴포넌트 · `UFPSRFirstPersonArmsAnimInstance` · 무기 DA 의 `ArmsAnimLayerClass`(`FPSRWeaponDataAsset.h:439`)·`ArmsReloadMontage` · `UpdateAimDownSights` 의 팔 해(`bSolvingArms`) · `ArmsHipRelativeTransform` · 건앵커 IK 분기 · GunMotion 커브의 팔 소비자 · 에디터 조립툴 `GetFirstPersonViewSetup`. **실측 71참조 / 8파일.**
  - **왜 안 건드려도 되는가**: 그 경로는 **이미 휴면**이다. 분할 조건이 `FirstPersonArms->GetSkeletalMeshAsset() != nullptr`(`FPSRCharacter.cpp:881`)인데 `BP_FPSRPlayer` 의 팔 메시 슬롯이 비어 있어 `bFirstPersonSplitActive` 가 **항상 false** 다. 따라서 `RefreshFirstPersonRendering` · `AttachWeaponMeshes` 의 건앵커 분기 · ADS 팔 해가 전부 오늘도 안 돈다. CHR1 은 그 상태를 **바꾸지 않는다** — 새 임시구조를 만드는 게 아니라 기존 휴면 상태를 유지하는 것이다.
  - **잔존물**(이번에 안 지움) = 죽은 컴포넌트 1개 + 무기 DA 죽은 필드 2개 + 안 도는 함수들. **별도 유닛으로 분리**(보드 신규 행 「1인칭 팔 기계 퇴역」). 이 명세가 그 사실을 적어 두는 것이 "미루기"와 "범위 분리"를 가른다.
  - 🔒 **불변식: `FirstPersonArms` 의 메시 슬롯은 비어 있어야 한다.** ⚠️ **정정(사용자 2026-09-11)** — 이 슬롯이 빈 것은 에셋 삭제의 부수효과가 **아니다. 교체하려고 사용자가 비운 것**이다. 즉 팔 트랙은 버려진 게 아니라 **교체 중**이고, 교체물이 바로 이 유닛이 세우는 드라이버 + 손 파츠다. 그래서 이 유닛의 범위 축소는 "어쩌다 안 도니 놔두자"가 아니라 **"교체의 앞 절반을 먼저 세운다"**이고, 뒤 절반(옛 팔 기계 퇴역)이 후속 유닛이다.
    ⚠️ **ADR 0015 I2 는 정반대("슬롯을 비우지 않는다 — 비우면 1P 에 3P 바디가 그려진다")를 불변식으로 적어 두었다.** 그 조항은 **교체 결정으로 대체**된다 — 지금은 바디를 리더로 숨기고 파츠가 대신 서므로 "3P 바디가 그려지는" 문제 자체가 없다. **누군가 ADR 0015 대로 슬롯을 다시 채우면 분할이 살아나 총이 팔로 옮겨가고 1P 손과 갈라진다** — ADR 0018 에 0015 I2/I5 의 대체를 명시한다.
  - ✅ **예외적으로 포함하는 한 칸**(사용자 결정 2026-09-11) — `BP_FPSRPlayer` 의 `FirstPersonArms.AnimClass` 를 **`None` 으로 비운다**. 현재 삭제된 `ABP_FP_Base` 를 가리키고 그 ABP 는 삭제된 `SKEL_LPAMG_Character`·`FP_Rifle_Idle` 을 하드 참조해 **폰 로드마다 누락 참조 에러 + 쿡 경고**를 낸다(`/Game/Character` 는 상시 쿡 대상이라 반드시 탄다). 기계를 고치는 게 아니라 이미 끊어진 포인터를 지우는 것이라 범위 축소와 모순되지 않으며, §12-5 회귀 로그를 읽을 수 있게 만드는 전제다.
- ❌ **ADS 해의 *대상*을 바꾸지 않는다.** 총을 손 기준으로 푸는 것(= `UpdateAimDownSights` 의 SolveTarget 변경)은 팔 기계 수정과 동치라 범위 밖이다. **대신 반대 방향으로 푼다** — 총은 지금처럼 카메라 해로 가고, **손이 IK 로 총을 따라간다**(§2 목표 4).
- ❌ **파츠에 자체 물리·고유 애니메이션을 넣지 않는다 (엔진 제약 — 콘텐츠 저작 규칙).** Epic 문서 명시: *"Leader Pose 방식의 자식 메시는 고유 애니메이션을 돌리거나 독립적으로 물리를 시뮬레이션할 수 없다."* 즉 **흔들리는 장식(스카프·안테나·머리카락·망토)은 이 구조로 만들 수 없다.** 그런 파츠가 필요해지면 그것만 Copy Pose 로 빼야 하고(그래프가 하나 더 돈다) 그건 별도 결정이다. **모르고 저작하면 "왜 안 흔들리지"로 시간을 쓴다 — 그래서 비목표에 적는다.**
  - 같은 조항의 뒷면: Leader Pose 는 자식이 리더 스켈레톤의 **정확한 부분집합**이어야 한다(*"extra joints 도 skip 도 안 된다"*). 우리 파츠 8개는 89본 전체를 들고 있어 충족한다(실측) — §6 의 스켈레톤 검사가 이 조항을 지키는 장치다.
- 🔴 **부수효과 — 무기 DA 에 그립 소켓을 넣는 순간 *3인칭 바디*의 왼손 IK 도 켜진다 (rev.6 추가).** `ABP_VoxelChar_Body` 에 이미 Two Bone IK 노드와 `LeftHandGripLocation`/`LeftHandIKAlpha` 바인딩이 있고, 오늘 알파가 0 인 유일한 이유가 DA 소켓 `None` 이다. 소켓을 저작하면 **원격 클라 화면의 바디 왼손과 소유자 그림자**가 핸드가드로 간다. 이는 ADR 0002 step 4 의 의도된 설계지만 **이번 유닛이 켜는 것이므로 4인 기준으로 검증 항목에 넣는다**(§12-7). 오른손은 §5-1 (B) 의 게이트가 바디를 막으므로 3P 에 영향 없다.
  - 🪤 **몽타주에 `LeftHandIKWeight` 커브가 0건이다**(`Content/Characters/VoxelChar/Anims/AM_Body_*` 6개 실측). 그대로 두면 **재장전 중 왼손이 핸드가드에 붙은 채 몽타주와 싸운다** — 3P·1P 양쪽에서. 커브 저작(사용자) 또는 "재장전 중 붙음 = 알려진 결과"를 §11 에서 택한다.
  - 🪤 **드라이버의 CopyPose 는 커브를 기본으로 안 옮긴다** — `bCopyCurves` 기본 `false`(`AnimNode_CopyPoseFromMesh.cpp:21`). 커브를 저작해도 드라이버가 못 본다 → ABP 노드에서 **Copy Curves = true**(§4).
- ❌ **IK 의 시각 튜닝을 Claude 가 하지 않는다.** 소켓 위치·오프셋·알파 곡선은 눈으로 맞추는 값이라 사용자 작업이다(`[[leave-fine-tuning-to-user]]`). 명세는 **수치 범위와 붙는 자리**까지만 정한다.
- ❌ **`RefreshFirstPersonRendering` 을 수정하지 않는다.** 파츠의 렌더 태그는 **생성 시 1회**가 유일한 주인이고(§6), 그 플래그들은 뷰타깃 기준이라 `bFirstPersonSplitActive` 와 무관하게 옳게 나온다.
- ❌ **캐릭터 정의 DataAsset 을 만들지 않는다** — 보드 「캐릭터별 카드덱 구조 교체」 행이 소유.
- ❌ **런 중 파츠 교체를 배선하지 않는다**(로비 선택만). 진입점만 하나로 둔다.
- ❌ **적에게 쓰지 않는다.**
- ❌ **1인칭 전용 SK 를 저작하지 않는다**(사용자 결정) — 3인칭 파츠 에셋을 재사용한다.
- ❌ **워프 Control Rig 를 만들지 않는다.** 드라이버 그래프에 **자리만**(Copy → [워프] → Output).
- ❌ **1인칭 FOV/스케일을 켜지 않는다** — `bEnableFirstPersonFieldOfView`/`bEnableFirstPersonScale` 은 엔진 기본(false) 유지. 켜면 렌더 FOV 가 ADS 해와 갈라져 사이트가 중심선을 벗어난다(기존 코드 NOTE `FPSRCharacter.cpp:914-918`). 켜는 것은 ADS 재측정과 함께 후속.
- ❌ **로비 UI 위젯을 배선하지 않는다.**
- ❌ **머리 본 숨김(`HideBoneByName`, `:1197`)을 손대지 않는다.** 팔로워가 리더의 본 가시성을 읽으므로 Head 파츠도 같이 접히고 **소유자 그림자에서 머리가 빠진다** — 그러나 이는 오늘 머지 메시에서도 동일한 기존 동작이라 CHR1 의 회귀가 아니다. §12 ④ 문구에 반영.

## 3. 제1원리 3줄 (핵심원칙 4)

1. **제1원리 근거** — 제1원리("적 수백을 싸게")는 **적** 조항이고 플레이어는 최대 4명이다. 파츠는 Leader Pose 팔로워라 **자기 애님그래프를 평가하지 않는다.** 플레이어당 실제로 도는 그래프는 **2개**(바디 + 1P 드라이버)뿐. **적에 쓰면 즉시 위반**이라 §2 비목표에 못박았다.
2. **엔진 기본값·기존 인프라와의 관계** — **UE 5.7 1인칭 템플릿 구조를 따른다.** 실측(`Templates/TP_FirstPersonBP`): `BP_FirstPersonCharacter` = `CharacterMesh0` + `FirstPersonMesh` 두 컴포넌트, 참조 스켈레탈 메시는 `SKM_Manny_Simple` **하나**(같은 에셋 두 번), 1인칭 ABP = `ABP_FP_Copy`(`AnimNode_CopyPoseFromMesh` + `AnimNode_ControlRig`). 엔진은 1인칭 전용 메시를 **배포하지 않는다**(`Engine/Content`+`Templates` 에서 `*Arms*.uasset` 0건).
   - **1인칭이 Leader Pose 가 아니라 Copy Pose 인 이유**: Leader Pose 팔로워는 그래프가 없어 포즈를 손댈 수 없다. 워프를 걸 자리가 필요하다.
   - **드라이버를 `GetMesh()` 자식으로 붙여 엔진이 주는 것을 쓴다**: (i) `AttachToComponent` 가 부모 틱 선행을 자동 추가(`SceneComponent.cpp:2465`) (ii) CopyPose 노드의 `bUseAttachedParent` 가 **어태치 체인을 올라가 스켈레탈 메시를 찾는다**(`AnimNode_CopyPoseFromMesh.cpp:79-92`) → 소스 지정용 C++ 프로퍼티도 BP 배선도 불필요 (iii) 바디 상대 트랜스폼 수동 복사 드리프트 0.
   - **리더 숨김은 `bHiddenInGame`**: `SetHiddenInGame(bool, bPropagateToChildren=false)` 기본이 `DirtyOnly` 라 자식 플래그를 안 바꾸고(`SceneComponent.cpp:3616-3646`), `IsVisible()`(`:3528`)은 부모를 안 본다. `SetVisibility(.., true)` 는 자식에 재귀해 파츠까지 끈다(ADR 0015 가 팔에서 밟은 함정).
   - 🔴 **3P 파츠에 `WorldSpaceRepresentation` 을 쓰지 않는다 — 이 프로젝트에서는 그림자가 사라진다.** 그 타입은 프록시에서 `bOwnerNoSee=true` + **`bCastHiddenShadow=false` 를 강제**하고(`PrimitiveSceneProxy.cpp:627-634`), 엔진 주석이 그 이유를 *"일반 그림자 렌더링은 건너뛰고 **VSM 의 1인칭 그림자 경로**가 집어간다"* 로 적어 두었다. 그런데 **이 프로젝트는 VSM 이 꺼져 있다** — `r.Shadow.Virtual.Enable` 코드 기본값 `0`(`VirtualShadowMapArray.cpp:86`)이고 `Config/*.ini` 어디에도 켜는 줄이 없다(실측). 그대로 쓰면 소유자 화면에서 **몸 그림자가 통째로 사라지고 총 그림자만 남는다** — 오늘 바디는 `None` 타입으로 정상 그림자를 내므로 **회귀**다.
     **대신 쓰는 것 = `None` + `SetOwnerNoSee(true)` + `SetCastHiddenShadow(true)`.** `FPrimitiveSceneProxy::IsShadowCast` 가 소유자 가시성 검사 전체를 `if (!CastsHiddenShadow())` 블록 안에 두므로(`PrimitiveSceneProxy.cpp:1598`), `bCastHiddenShadow=true` 면 그 검사를 건너뛰어 **그림자 방식과 무관하게** 소유자 그림자가 나온다. 가시성은 `IsShown()`(`:1562-1568`)에서 동일하게 뷰타깃 기준이라 4인 전 경우가 그대로 성립한다. VSM 을 켜는 것은 렌더러 전역 결정이라 이 유닛 범위 밖이다.
   - **`bUseBoundsFromLeaderPoseComponent = false`**: `true` 로 두면 `CalcMeshBound`(`SkinnedMeshComponent.cpp:2029`)가 `Leader->CalcBounds` 를 부르는데, 리더가 숨겨져 있으면 리더 쪽 `bIsVisible = ShouldRender() || bCastHiddenShadow` 가 false 라 **정적 임포트 바운드**로 떨어진다(포즈를 안 따라감 → 원격 화면에서 파츠 컬링 팝). `false` 면 `bLeaderHasPhysBodies` 분기(`:2060`)가 **리더 피직스 에셋을 이 컴포넌트 본으로** 계산해 정확하다(`SKM_VoxelChar_PhysicsAsset` 존재).
   - 조립 코드는 기존 무기 파츠(`RebuildPartsFromSelection`, `FPSRCharacter.cpp:2586`)를 미러링한다.
3. **프로젝트 제약과의 정합** — 복제는 `AFPSRPlayerState::SelectedWeapon`(`FPSRPlayerState.h:349`) 형태 그대로, 선택 풀은 `LoadoutPool`(`FPSRGameFlowSettings.h:38`) 소프트참조 관용구 그대로, 기본값 폴백은 `DefaultPrimaryWeapon`(`PossessedBy` `:495-510`) 그대로. **새 관용구 0개.**
   - ✅ **이 아트 방향이 모듈형과 특히 잘 맞는 이유** — 연결된 몸의 모듈형 캐릭터는 **이음새 문제**(파츠 경계에서 살이 뚫고 나오거나 틈이 벌어짐)를 상시로 안고 가고, 그래서 파츠 조합마다 검수가 필요하다. **플로팅 림즈는 부위가 애초에 떨어져 있어 그 문제가 원리적으로 없다** — 어떤 조합으로 갈아도 이음새가 깨질 자리가 없다. 코스메틱 교체가 목적인 이 유닛에서 이건 공짜로 얻는 안전성이고, 파츠 조합 검수를 §12 에 넣지 않아도 되는 근거다.
   - 🪞 **Lyra 대조** — Epic 의 Lyra 도 *"메인 캐릭터 메시를 보이지 않게 두고 실제로 보이는 것은 Character Part"* 이고, **데디서버에서는 코스메틱을 스폰하지 않는다**(§6 의 `NM_DedicatedServer` 게이트와 같은 판단). 의도적으로 다른 점 둘: 파츠 단위가 **액터가 아니라 컴포넌트**(파츠에 자체 로직이 필요 없어 더 싸다), 복제 위치가 Controller/Pawn 컴포넌트 쌍이 아니라 **PlayerState**(폰이 죽고 재스폰돼도 외형·색이 남는다 — 로그라이트의 부활 흐름에 맞다).

## 4. 파일 목록

| 경로 | 신규/수정 | 한 줄 설명 |
|---|---|---|
| `Public/Hero/FPSRCharacterPartSetDataAsset.h` | 신규 | `EFPSRPartViewScope` · `FFPSRCharacterPartAttachment` · `UFPSRCharacterPartSetDataAsset` · `UFPSRCharacterPartPoolDataAsset` |
| `Private/Hero/FPSRCharacterPartSetDataAsset.cpp` | 신규 | 풀 접근자 · `HasFirstPersonPart` · `IsDataValid` |
| `Public/Core/FPSRGameFlowSettings.h` | 수정 | `CharacterPartPool` 소프트참조 |
| `Public/Core/FPSRPlayerState.h` · `Private/Core/FPSRPlayerState.cpp` | 수정 | `SelectedCharacterPartSet` + **`PlayerColorIndex`** 복제 · 세터 · `OnRep_` · **`CopyProperties` 에 둘 다 복사**(§5-2 함정) |
| `Docs/SSOT/ArtDirection.md` §A-3-5 | 수정(**사용자/아트 결정**) | 차가운 대역 안에서 플레이어 4색 확정 + 예약색(`#4FD8FF`·`#2E9BFF`·`#8B6BFF`)과의 관계 정리(관계형↔정체형, §5-2) |
| `Public/Hero/FPSRCharacter.h` · `Private/Hero/FPSRCharacter.cpp` | 수정 | `FirstPersonDriverMesh` CDO 컴포넌트 · 파츠 배열 2벌 · 조립 · `PossessedBy`/`OnRep_PlayerState` 훅업 · `DefaultCharacterPartSet` · 디버그 콘솔 명령 · **`Get{Left,Right}HandGripTransform` 게이트 예외**(§5-1) |
| `Content/Weapons/.../DA_Weapon_*.uasset`(9) | 수정(**사용자**) | `LeftHandSocket`·`RightHandSocket` 을 `None` → 소켓 이름으로(현재 전부 None 이라 IK 알파가 0) |
| 무기 **파츠** 메시(핸드가드·리시버 등) | 수정(**사용자**) | 그 소켓을 파츠 메시에 저작. 소켓을 가진 파츠가 파지 타깃이 된다(§5-1) |
| `Config/DefaultGame.ini` | 수정 | `+DirectoriesToAlwaysCook=(Path="/Game/Characters/VoxelChar")` — §9 |
| `Public/Hero/FPSRCharacterAnimInstance.h` · `.cpp` | 수정 | **우측 절반 신설**(§5-1 A) — 현재 우측 멤버 0개 |
| `Content/Characters/VoxelChar/ABP_VoxelChar_FP.uasset` | 신규(**사용자**) | 부모 = `UFPSRCharacterAnimInstance`. 그래프 = **CopyPose**(Use Attached Parent ✅ · **Copy Curves ✅**) → **TwoBoneIK(`hand_l`** · Effector=`LeftHandGripLocation`(World) · JointTarget=`LeftHandJointTargetLocation`(World) · Alpha=`LeftHandIKAlpha`) → **TwoBoneIK(`hand_r`** 동형, 우측 값) → [워프자리] → Output. 클래스 기본값 **`LeftElbowBoneName=lowerarm_l` · `RightElbowBoneName=lowerarm_r`**(C++ 기본 `lower_arm_L` 은 이 스켈레톤에 없다 — §5-1) |
| `Content/Characters/VoxelChar/DA_VoxelChar_PartSet_Default.uasset` | 신규(**사용자**) | 파츠 8 · Hand_L/R = `Both` |
| `Content/Characters/VoxelChar/DA_VoxelChar_PartPool.uasset` | 신규(**사용자**) | 선택 가능한 세트 목록 |
| `Content/Character/Player/BP_FPSRPlayer.uasset` | 수정(**사용자**) | 드라이버 AnimClass = `ABP_VoxelChar_FP` · `DefaultCharacterPartSet` 지정 · **`FirstPersonArms.AnimClass` → `None`**(끊어진 참조 제거, §2) |

**건드리지 않는 파일**(§2 비목표) — `FPSRFirstPersonArmsAnimInstance.*` · `FPSRWeaponDataAsset.h` · `FPSRWeaponFireComponent.cpp` · `FPSRGunMotionCurves.h` · `FPSRGunMotionStudioData.h` · `FPSRWeaponAssemblerHelpers.cpp` · `RefreshFirstPersonRendering` 본문.

## 5. 인터페이스 선언 (헤더 스케치)

```cpp
// FPSRCharacterPartSetDataAsset.h

/** 이 파츠가 **어느 컴포넌트로 만들어지는가**. 3인칭 전용이 기본 — 1인칭에 내놓는 것은 명시적 선택이어야 한다. */
UENUM(BlueprintType)
enum class EFPSRPartViewScope : uint8
{
    ThirdPersonOnly UMETA(DisplayName = "3인칭 전용"),   // 3P 컴포넌트 1개
    FirstPersonOnly UMETA(DisplayName = "1인칭 전용"),   // 1P 컴포넌트 1개
    Both            UMETA(DisplayName = "양쪽"),         // 3P + 1P = 컴포넌트 2개(같은 메시 에셋)
};

USTRUCT(BlueprintType)
struct FFPSRCharacterPartAttachment
{
    GENERATED_BODY()

    /** 파츠 메시(소프트). null = 슬롯 비움(널 세이프). */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "캐릭터|파츠", meta = (DisplayName = "파츠 메시"))
    TSoftObjectPtr<USkeletalMesh> Part;

    /** 슬롯 식별자. **소켓명이 아니다** — 로그·툴·중복검사 키. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "캐릭터|파츠", meta = (DisplayName = "슬롯 ID"))
    FName SlotId = NAME_None;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "캐릭터|파츠", meta = (DisplayName = "슬롯 이름(표시용)"))
    FText DisplayLabel;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "캐릭터|파츠", meta = (DisplayName = "표시 시점"))
    EFPSRPartViewScope ViewScope = EFPSRPartViewScope::ThirdPersonOnly;

    /** 슬롯 0 머티리얼 오버라이드. 3P·1P 인스턴스에 **둘 다** 적용. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "캐릭터|파츠", meta = (DisplayName = "머티리얼 오버라이드"))
    TSoftObjectPtr<UMaterialInterface> MaterialOverride;
};

UCLASS(BlueprintType)
class FPSROGUELITE_API UFPSRCharacterPartSetDataAsset : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "캐릭터|파츠", meta = (DisplayName = "표시 이름"))
    FText DisplayName;

    /** 슬롯 수를 고정하지 않는다 — 파츠가 늘거나 줄 때 C++ 를 고치지 않기 위해(핵심원칙 2). */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "캐릭터|파츠",
              meta = (DisplayName = "파츠 목록", TitleProperty = "SlotId"))
    TArray<FFPSRCharacterPartAttachment> Parts;

    /** 파츠 머티리얼의 플레이어 색 벡터 파라미터 이름. `None` = 이 세트는 색을 안 받는다(파츠 기본색 유지).
     *  파라미터 **이름**만 여기 두는 이유 = 색 **값**은 아트 SSOT 소관이고 풀이 들고 있다(§9). */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "캐릭터|파츠", meta = (DisplayName = "색 파라미터 이름"))
    FName TeamColorParamName = NAME_None;

    UFUNCTION(BlueprintPure, Category = "캐릭터|파츠")
    bool HasFirstPersonPart() const;

#if WITH_EDITOR
    /** 런타임 모듈의 IsDataValid 관용구(FPSRArenaPropSetDataAsset.cpp:21 과 동일 형태):
     *  SlotId 중복 · Part 유효한데 SlotId 없음 · **3P 파츠가 0개인 세트**(§11-3) · 파츠 간 스켈레톤 불일치. */
    virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};

UCLASS(BlueprintType)
class FPSROGUELITE_API UFPSRCharacterPartPoolDataAsset : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "캐릭터|파츠")
    TArray<TObjectPtr<UFPSRCharacterPartSetDataAsset>> SelectablePartSets;

    /** 플레이어 색 슬롯. **배열 길이가 곧 색 슬롯 수**라 4색을 5색으로 늘려도 C++ 는 무변이다.
     *  🔴 값의 정본 = `Docs/SSOT/ArtDirection.md` §A-3-5(아군 = 청록→파랑→보라, 🔒 예약색 회피).
     *  비어 있으면 색을 적용하지 않는다(안전 실패). */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "캐릭터|파츠", meta = (DisplayName = "플레이어 색 슬롯"))
    TArray<FLinearColor> PlayerColors;

    UFUNCTION(BlueprintPure, Category = "캐릭터|파츠")
    UFPSRCharacterPartSetDataAsset* GetPartSetAt(int32 Index) const;

    UFUNCTION(BlueprintPure, Category = "캐릭터|파츠")
    bool IsValidIndex(int32 Index) const;
};
```

```cpp
// FPSRGameFlowSettings.h — LoadoutPool 바로 아래
UPROPERTY(EditAnywhere, Config, Category = "Loadout")
TSoftObjectPtr<UFPSRCharacterPartPoolDataAsset> CharacterPartPool;
```

```cpp
// FPSRPlayerState.h — SelectedWeapon 과 대칭
public:
    UFUNCTION(BlueprintPure, Category = "FPSR|Loadout")
    UFPSRCharacterPartSetDataAsset* GetSelectedCharacterPartSet() const { return SelectedCharacterPartSet; }

    /** 서버 전용. 값이 바뀌면 권위 측에서 폰 재조립을 **직접** 부른다(리슨 호스트는 OnRep 을 못 받는다). */
    void SetSelectedCharacterPartSet(UFPSRCharacterPartSetDataAsset* PartSet);

    UFUNCTION()
    void OnRep_SelectedCharacterPartSet();

private:
    /** 하드 참조 — SelectedWeapon 과 동일. 정적(에셋) 참조는 클라에 미로드여도 NetGUID 해석 시 동기 로드된다
     *  (`net.AllowAsyncLoading` 기본 0 · `PackageMapClient.cpp:3896`) → **런타임 로드 보장은 문제가 아니다.**
     *  진짜 필요조건은 **쿡에 포함되는 것**이며 §9 의 DirectoriesToAlwaysCook 행이 그것을 담당한다. */
    UPROPERTY(ReplicatedUsing = OnRep_SelectedCharacterPartSet)
    TObjectPtr<UFPSRCharacterPartSetDataAsset> SelectedCharacterPartSet;
```

```cpp
// FPSRCharacter.h

public:
    /** 파츠 세트로 몸을 전량 재조립. **public** — PlayerState 의 세터/OnRep 이 직접 부른다(§6).
     *  PartSet == nullptr / 유효 파츠 0개면 전부 해체하고 리더를 다시 보이게 한다 = 안전 실패. */
    void RebuildCharacterPartsFromSet(const UFPSRCharacterPartSetDataAsset* PartSet);

protected:
    /** 1인칭 포즈 드라이버 — UE5 1인칭 템플릿의 FirstPersonMesh 자리.
     *  **`GetMesh()` 자식으로 부착**(상대 항등) → 부모 틱 선행 자동 + CopyPose 의 bUseAttachedParent 가 소스를 찾는다.
     *  메시는 바디와 같은 에셋(추가 메모리 0), 그려지지 않고(bHiddenInGame), ABP_VoxelChar_FP 를 돌려 1P 파츠의 리더가 된다.
     *  ⚠️ FirstPersonArms 와 **무관하다** — 그 컴포넌트는 이 유닛에서 손대지 않는다(§2). */
    UPROPERTY(VisibleAnywhere, Category = "FPSR|Character")
    TObjectPtr<USkeletalMeshComponent> FirstPersonDriverMesh;

    /** 로비 선택이 없을 때 쓰는 기본 외형. DefaultPrimaryWeapon(:495-510) 과 같은 자리·같은 이유 —
     *  로비를 거치지 않는 디버그/직행 PIE 에서도 모듈형이 서야 검증이 가능하다(§12). */
    UPROPERTY(EditDefaultsOnly, Category = "FPSR|Character", meta = (DisplayName = "기본 파츠 세트"))
    TObjectPtr<UFPSRCharacterPartSetDataAsset> DefaultCharacterPartSet;

    /** 3인칭 파츠 — 리더 = GetMesh(). 교체가 드물어 전량 재조립(무기 파츠 :1224 와 동일 판단·동일 지정자). */
    UPROPERTY(Transient)
    TArray<TObjectPtr<USkeletalMeshComponent>> ThirdPersonPartComponents;

    /** 1인칭 파츠 — 리더 = FirstPersonDriverMesh. 3P 배열과 **인덱스 정렬되지 않는다**(부분집합). 항상 같이 비운다. */
    UPROPERTY(Transient)
    TArray<TObjectPtr<USkeletalMeshComponent>> FirstPersonPartComponents;

    /** 파츠 컴포넌트 1개 생성 + 리더 결선. bFirstPerson 이 렌더 규약을 가른다(§6 표). */
    USkeletalMeshComponent* CreatePartComponent(USkeletalMesh* Mesh, UMaterialInterface* MatOverride,
                                               USkeletalMeshComponent* Leader, bool bFirstPerson);

    bool IsModularBodyActive() const { return ThirdPersonPartComponents.Num() > 0; }
```

### 5-1. 손 IK 시임 — 기존 인프라 재사용 + 게이트 예외 1건

**새로 만들 것이 거의 없다.** 파츠 단위 그립은 **이미 구현돼 있다**(실측):
- 무기 DA 가 `LeftHandSocket`/`RightHandSocket`(소켓 **이름**)을 정한다.
- `RebuildPartsFromSelection`(`FPSRCharacter.cpp:2699-2723`)이 **매 파츠 재조립마다** 그 소켓을 가진 파츠 컴포넌트를 다시 찾아 `CachedLeftHandComponent`/`CachedRightHandComponent` 에 넣는다. 재조립은 무기 장착·**파츠 진화(런 중 교체)** 양쪽에서 돈다 → 파지가 자동으로 따라 옮겨간다. 코드 주석: *"the grip sits on the HANDGUARD part, so swapping the handguard moves where the left hand should land"*.
- `ResolveLeftHandGripComponent`(`:2733`)가 파츠 → 없으면 리시버로 폴백. 소켓을 가진 파츠가 없으면 `nullptr` = **정상 상태**(주석: *"the AnimBP has to survive 'no target' as a normal case, not an error"*).
- `UFPSRCharacterAnimInstance`(`:254-264`)가 매 프레임 `bHasLeftHandGrip` · `LeftHandGripLocation` · `LeftHandIKAlpha`(우측 쌍도 동일)를 계산한다.

> 💡 DA 가 "어느 **파츠**"가 아니라 "어느 **소켓 이름**"을 정하는 것이 중요하다 — 슬롯이 진화해 다른 메시로 바뀌어도 같은 소켓만 달고 있으면 DA 를 안 고쳐도 해석이 따라간다. 파츠를 직접 지목하면 진화 단계마다 DA 를 갱신해야 한다.

🔴 **정정(rev.6) — 「C++ 변경은 게이트 예외 1건뿐」은 거짓이었다.** 실측: **`GetRightHandGripTransform` 은 존재하지 않고**(`grep` 0건), `UFPSRCharacterAnimInstance` 의 우측 멤버도 **0개**다(좌측 16). 우측 쌍은 `UFPSRFirstPersonArmsAnimInstance` **에만** 있고 그 클래스는 §2 가 금지한 팔 기계다. 좌측을 보고 대칭을 가정해 적은 것이며 소스 대조를 빠뜨린 결과다. **양손 IK 는 우측 절반을 새로 만들어야 성립한다.**

**(A) `UFPSRCharacterAnimInstance` 에 우측 절반 신설** — 좌측(`:142-174`, `:243-`)의 미러. 이름만 바꾸고 의미는 동일:
`RightHandGripWorld` · `RightHandGripLocation` · `bHasRightHandGrip` · `RightHandJointTargetLocation` · `RightHandIKAlpha` · `RightHandIKWeight` · `RightHandIKWeightCurve`(기본 `"RightHandIKWeight"` — 팔 인스턴스와 **같은 커브 이름**을 쓴다) · `RightElbowBoneName` · `RightElbowPoleOffset` · `UpdateRightElbowPole()` · `PushToLinkedLayers` 우측 줄.

🪤 **엘보 본 기본값을 그대로 두면 IK 가 조용히 꺼진다** — C++ 기본이 `LeftElbowBoneName = "lower_arm_L"`(`FPSRCharacterAnimInstance.h:243`)인데 **VoxelChar 스켈레톤의 본은 `lowerarm_l`**(실측). 없는 본이면 `UpdateLeftElbowPole` 이 알파 0 + 경고 1회로 끝나, 사용자가 소켓·오프셋을 아무리 만져도 안 변한다(`[[value-does-nothing-check-execution-path]]`). 우측 기본값은 **`lowerarm_r`** 로 두고, ABP 클래스 기본값에서 좌측을 **`lowerarm_l`** 로 맞춘다(§4).

**(B) `GetRightHandGripTransform` 신설 — 좌측 미러 + 🔴 드라이버 전용 게이트**

```cpp
/** 오른손 그립은 **드라이버만** 받는다. 바디는 항상 false 다 —
 *  총이 바디의 hand_r 소켓에 타고 있어서, 바디가 오른손 IK 로 그 총을 향해 뻗으면
 *  타깃이 손을 따라 움직이는 되먹임이 된다(코드가 이미 경고: FPSRCharacter.cpp:991-998).
 *  드라이버는 총이 **바디의** hand_r 에 타므로 그 루프가 없다 — 이 비대칭이 설계의 일부다.
 *  콘텐츠 규칙("바디 ABP 에 우측 IK 노드를 넣지 말 것")으로 대신하지 않는다: 실패가 무음(떨림)이고,
 *  어느 메시가 뻗어도 되는가는 부착 구조가 정하므로 C++ 이 판정할 일이다. */
bool AFPSRCharacter::GetRightHandGripTransform(const USceneComponent* ForMesh, FTransform& Out) const;
// 게이트: ForMesh == FirstPersonDriverMesh && GripComp->IsAttachedTo(GetMesh())
```

**(C) 좌측 게이트 예외 1건** (`GetLeftHandGripTransform`, `:2769-2795`)

```cpp
// 현재:
if (!ForMesh || !GripComp->IsAttachedTo(ForMesh)) { OutGripWorld = FTransform::Identity; return false; }
```
무기는 **바디**의 `SOCKET_Weapon` 에 붙으므로 드라이버가 물으면 이 검사가 false → 알파 0 → IK 가 안 뜬다.
**이 게이트를 지우면 안 된다** — 존재 이유가 주석에 있다: *총을 안 든 메시가 총을 향해 팔을 뻗으면 그 팔이 소유자 그림자에 보인다.* 필요한 것은 **"드라이버는 바디의 포즈 사본이므로, 바디가 들고 있으면 드라이버도 들고 있는 것으로 본다"** 는 예외다:

```cpp
/** 드라이버는 바디 포즈의 사본(CopyPoseFromMesh)이라 "총을 든 메시"의 자격을 바디에서 물려받는다.
 *  이 예외가 없으면 1P 손 IK 가 영구히 알파 0 이고, 게이트를 통째로 없애면 총을 안 든 메시까지 팔을 뻗는다. */
const USceneComponent* EffectiveMesh = (ForMesh == FirstPersonDriverMesh) ? GetMesh() : ForMesh;
if (!EffectiveMesh || !GripComp->IsAttachedTo(EffectiveMesh)) { ... }
```
좌·우 두 함수에만 들어가고, `FirstPersonArms` 경로는 손대지 않는다(§2 비목표 유지).

### 5-2. 플레이어 색 — 시인성 축 (머티리얼 파라미터 + 복제 인덱스)

**요구는 코스메틱이 아니라 시인성이다** — 4인 협동에서 누가 누군지 갈려야 한다. 따라서 두 조건이 강제된다:
① **모든 클라에서 같은 플레이어가 같은 색**(복제되는 축이 필요) ② **색은 파라미터**(에셋 교체로 하면 색×파츠 조합마다 MI 를 만들어야 한다).

```cpp
// FPSRPlayerState.h — SelectedCharacterPartSet 옆
public:
    /** 이 플레이어의 색 슬롯(0..N-1). 시인성 축이라 코스메틱과 달리 **서버가 배정**한다(사용자가 고르는 값이 아니다).
     *  INDEX_NONE = 미배정(= 색 파라미터를 건드리지 않고 파츠 기본색). */
    UFUNCTION(BlueprintPure, Category = "FPSR|Loadout")
    int32 GetPlayerColorIndex() const { return PlayerColorIndex; }

    /** 서버: 배정. 참가 시 비어 있는 가장 낮은 인덱스를 준다(퇴장한 슬롯은 재사용). */
    void SetPlayerColorIndex(int32 NewIndex);

    UFUNCTION()
    void OnRep_PlayerColorIndex();

private:
    UPROPERTY(ReplicatedUsing = OnRep_PlayerColorIndex)
    int32 PlayerColorIndex = INDEX_NONE;
```

🪤 **`LobbySeatIndex` 를 재사용하지 않는 이유(실측)** — 이미 복제되는 0..3 축이 있어 후보였으나, `AFPSRPlayerState::CopyProperties`(`:575-588`)는 **`SelectedWeapon` 만 복사한다.** 로비→게임플레이 심리스 트래블에서 좌석 번호는 `INDEX_NONE` 으로 돌아가 런 중 색이 사라지거나 충돌한다. `PlayerColorIndex` 는 **`SelectedWeapon` 과 같은 방식으로 `CopyProperties` 에서 복사**해야 한다 — 이 한 줄이 빠지면 "로비에선 색이 맞는데 런에 들어가면 다 같은 색"이 된다.

**배정 주체·시점 (rev.6 확정)** — `AFPSRPlayerState::ServerAssignColorIfUnassigned()`: `INDEX_NONE` 일 때만, `GameState->PlayerArray` 를 훑어 **비어 있는 가장 낮은 인덱스**를 준다(좌석 선례 `AFPSRLobbyGameMode::AssignSeat` `:68-108` 과 같은 형태). 호출 지점 = **두 GameMode 의 `GenericPlayerInitialization` 오버라이드**.
🪤 **왜 로비 GM 한 곳이 아닌가** — `AFPSRGameMode` 와 `AFPSRLobbyGameMode` 는 **형제**(둘 다 `AGameModeBase` 직속)라 한쪽에만 두면 다른 쪽에서 색이 없다. 로비 GM 에만 두면 직행 PIE 가 무색, 런 GM 에만 두면 로비가 무색이다. `GenericPlayerInitialization` 은 `PostLogin` 과 `HandleSeamlessTravelPlayer` **양쪽이 거치고**, 트래블에서는 `CopyProperties` 가 그보다 **먼저** 돌므로 "미배정일 때만" 조건이 복사된 값을 보존한다.

**적용 경로** — 신설 `AFPSRCharacter::ApplyPlayerColor()`: 색 인덱스가 유효하고 `TeamColorParamName` 이 `None` 이 아니면, 3P·1P 파츠 **양쪽**의 **모든 머티리얼 슬롯**(`GetNumMaterials()` 순회 — 슬롯 0만 칠하면 파츠가 다중 슬롯일 때 반쪽 색이 된다)에 `CreateDynamicMaterialInstance(i)` → `SetVectorParameterValue(TeamColorParamName, Color)`. 색 값은 `GameFlowSettings->CharacterPartPool` 의 `PlayerColors[Index]` 에서 읽는다.
**호출 3곳** — ① `RebuildCharacterPartsFromSet` 끝 ② `OnRep_PlayerColorIndex` ③ `SetPlayerColorIndex`(리슨 호스트 반쪽 — 호스트는 `OnRep_` 을 못 받는다).
재조립과 **분리된 함수**인 이유 = 색 인덱스와 파츠 세트가 같은 번치로 도착하면 재조립이 한 프레임에 두 번 도는 것을 피한다. MID 는 파츠 컴포넌트와 수명을 같이하므로 재조립 때 자동 정리된다.

🔴 **색 값과 파라미터 이름은 이 명세가 정하지 않는다 — 아트 SSOT 가 정본이다.**
`Docs/SSOT/ArtDirection.md` **§A-3-5(🔒 예약)** 가 이미 못박아 둔 것: *"아군 = 청록→파랑→보라 / 적 = 분홍→빨강→주황"* 이 **유일한 절대 규칙**이고, `#4FD8FF`(아군 아웃라인) · `#2E9BFF`(아군 인디케이터·핑) · `#8B6BFF`(자기 자신)가 예약돼 있다.
⚠️ **여기에 결정이 하나 필요하다(아트 SSOT 쪽 작업)** — 현행 규칙은 **관계형**("자기 자신 = 보라 / 아군 = 파랑")인데 이번 요구는 **정체형**("1번은 이 색, 2번은 저 색")이다. 둘은 같은 대역을 쓰므로 그대로 겹치면 시인성이 오히려 떨어진다. **차가운 대역 안에서 4색을 뽑고 예약색과 충돌하지 않게 하는 것**은 아트 결정이라 `ArtDirection.md` §A-3-5 개정으로 처리하고, 이 유닛은 **인덱스→색 매핑을 데이터로 읽기만** 한다(§9).

## 6. 함수별 계약

| 함수 | 권위 | 호출자 | 전제조건 | 실패 시 동작 |
|---|---|---|---|---|
| `RebuildCharacterPartsFromSet(PartSet)` | **권위 무관**(코스메틱 — 서버·클라 각자) | `PossessedBy`(서버) · `OnRep_PlayerState`(클라) · `OnRep_SelectedCharacterPartSet`(원격 클라) · `SetSelectedCharacterPartSet`(리슨 호스트 **직접**) | `GetMesh()` 유효 + 메시 에셋 유효 | 리더 없음 → 조기 반환(Warning). null/빈 목록 → 전량 해체 + `GetMesh()->SetHiddenInGame(false)` |
| ↳ 파츠 1개 | — | 위 | `Part.LoadSynchronous()` 성공 **AND** `Mesh->GetSkeleton() == GetMesh()->GetSkeletalMeshAsset()->GetSkeleton()` | **그 파츠만 건너뛰고 Warning.** ⚠️ 스켈레톤 검사는 *정책*이다 — 엔진의 리더맵은 본 **이름** 기준(`UpdateLeaderBoneMap`)이라 다른 스켈레톤이어도 일부는 붙지만, 그 상태를 허용하면 조용히 어긋난 몸이 나온다 |
| ↳ 1P 파츠 | — | 위 | 위 + `FirstPersonDriverMesh` 유효 | 드라이버 없음 → **1P 파츠 전량 생략 + Warning**, 3P 는 정상 진행 |
| `PossessedBy` | **서버** | 엔진 | — | ① `PS->GetSelectedCharacterPartSet()` 이 null 이고 `DefaultCharacterPartSet` 이 있으면 `PS->SetSelectedCharacterPartSet(Default)` ② **그 뒤 항상** `RebuildCharacterPartsFromSet(PS->GetSelectedCharacterPartSet())` 을 부른다. ⚠️ ①만 두면 **리스폰·심리스 트래블에서 PS 값이 이미 있어 세터가 멱등 반환하고 새 폰이 재조립을 못 받는다** → 리슨 호스트에서 트래블 후 남의 몸이 머지 메시로 보인다. 첫 빙의에서 세터 경유와 중복 호출되는 것은 허용(전량 재조립이라 멱등) |
| `SetSelectedCharacterPartSet(PartSet)` | **서버 전용** | `PossessedBy` 폴백 · 디버그 명령 · (후속) 로비 RPC | `HasAuthority()` | 권위 아니면 조기 반환. 같은 값이면 조기 반환(멱등). 값이 바뀌면 **소유 폰의 `RebuildCharacterPartsFromSet` 직접 호출**(호스트 반쪽) |
| `FPSR.SelectPartSet <idx>` | 서버(권위 머신) | 디버그 콘솔 | — | **`-1` = null 지정**(§12-7 ⑦ 대조군이 요구하는 안전 실패 경로). 그 외 인덱스 무효/풀 미설정 → 로그 후 무시. `FAutoConsoleCommandWithWorldAndArgs` · `ECVF_Cheat`. 대상 = 로컬 플레이어의 PS |
| `RebuildCharacterPartsFromSet` 전용 게이트 | — | — | — | `GetNetMode() == NM_DedicatedServer` 면 조기 반환(순수 코스메틱). 리슨 서버 호스트는 자기 화면이 있으므로 해당 없음 |

**`ResetRunState` 는 파츠 세트를 초기화하지 않는다** — `SelectedWeapon` 은 *런 단위 선택*이라 초기화하지만 외형은 *플레이어 정체성*이라 로비 재진입마다 리셋되면 안 된다. 비대칭이 의도임을 여기 명시한다.

**`CreatePartComponent` 가 세우는 규약** — 부착으로 상속되지 않으므로 **생성 시 전부** 준다(무기 파츠 `:2638` 과 같은 이유). **이 1회가 파츠 렌더 플래그의 유일한 주인이다**(`RefreshFirstPersonRendering` 은 파츠를 건드리지 않는다).

| 설정 | 3P (`bFirstPerson=false`) | 1P (`bFirstPerson=true`) |
|---|---|---|
| `SetLeaderPoseComponent` | `GetMesh()` | `FirstPersonDriverMesh` |
| `bUseBoundsFromLeaderPoseComponent` | **`false`**(§3-2) | **`false`** |
| 소유자에게 보이는가 | `SetOwnerNoSee(true)` | `SetOnlyOwnerSee(true)` |
| `SetFirstPersonPrimitiveType` | **`None`**(§3-2 — `WorldSpaceRepresentation` 금지) | `FirstPerson` |
| `SetCastHiddenShadow` | **`true`** — 소유자에게 안 보여도 그림자는 낸다 | 건드리지 않음(`FirstPerson` 이 그림자를 이미 전부 끈다, `:619-625`) |
| `SetCollisionEnabled` | `NoCollision` | `NoCollision` |
| `SetRenderCustomDepth` | `true` | `true` |
| 부착 | `AttachToComponent(GetMesh(), KeepRelative)` | `AttachToComponent(FirstPersonDriverMesh, KeepRelative)` |

> 가시성은 **뷰타깃 기준**으로 판정된다(`FPrimitiveSceneProxy::IsShown`, `PrimitiveSceneProxy.cpp:1562-1568` — `View->ViewActor.IsPartOf(Owners)`)라 `IsLocallyControlled()` 게이트 없이 4인 전 경우(내 화면 / 남의 화면 / DBNO 관전)가 옳게 나온다.

**리더 숨김 조건** — 3P 파츠가 **1개 이상** 섰을 때만 `GetMesh()->SetHiddenInGame(true)`. 0개면 숨기지 않는다(= 머지 메시 폴백). 이 조건이 `IsModularBodyActive()` 의 정의이고, 리더를 숨기는 것이 옳은 유일한 근거는 "3P 대체물이 실제로 있다"이다.

**드라이버 컴포넌트 규약**(생성자에서 1회) — `SetHiddenInGame(true)` · `SetCollisionEnabled(NoCollision)` · `bCastShadow = false`(그림자는 3P 파츠가 낸다, 이중 캐스트 금지) · `VisibilityBasedAnimTickOption = AlwaysTickPoseAndRefreshBones`(§8) · `AttachToComponent(GetMesh(), KeepRelativeTransform)` 상대 항등.
**조립 순서 고정** — ① 드라이버 메시 에셋 지정 → ② 3P 파츠 생성 → ③ 1P 파츠 생성. 리더맵은 리더에 메시가 있어야 서므로 ①이 ③보다 먼저여야 한다.

## 7. 복제표

| 프로퍼티 / RPC | 종류 | Push Model | 조건 | 비고 |
|---|---|---|---|---|
| `AFPSRPlayerState::SelectedCharacterPartSet` | `ReplicatedUsing = OnRep_SelectedCharacterPartSet` | `MARK_PROPERTY_DIRTY_FROM_NAME` — 세터 1곳 + `CopyProperties` 직접대입 1곳(`SelectedWeapon` 과 동일, `FPSRPlayerState.cpp:586-587`) | `COND_None` — 모든 클라가 모든 플레이어의 외형을 봐야 한다 | `DOREPLIFETIME_WITH_PARAMS_FAST`, `bIsPushBased=true` |

| `AFPSRPlayerState::PlayerColorIndex` | `ReplicatedUsing = OnRep_PlayerColorIndex` | `MARK_PROPERTY_DIRTY_FROM_NAME` — 세터 1곳 + `CopyProperties` 1곳 | `COND_None` — **시인성 축이라 모든 클라가 모든 플레이어의 색을 봐야 한다** | 🔴 `CopyProperties` 복사 **필수**(§5-2 함정). `OnRep_` → 폰의 색 재적용 |

> ⚠️ **패키지 빌드에서 Push Model 컴파일 아웃** — 정합성은 `DOREPLIFETIME` 등록에서 나오므로 꺼져도 성립.
> ⚠️ **리슨 호스트는 `OnRep_` 을 못 받는다** → 세터가 직접 재조립을 부른다(`SetSelectedWeapon`/`SetReady` 가 이미 그렇게 한다, `FPSRPlayerState.cpp:264` 주석). `[[event-halves-authority-vs-client]]`.
> **이번 유닛에 RPC 는 없다** — 로비 UI 가 범위 밖이라 선택 경로는 `PossessedBy` 폴백 + 디버그 콘솔 명령뿐. 로비 RPC 는 로비 재작성 행에서 추가한다.

## 8. 수명주기 · 소유권

- **생성** — 파츠는 `NewObject` → 규약 → `RegisterComponent()` → `AttachToComponent`. CDO 컴포넌트는 `FirstPersonDriverMesh` 하나.
- **해제** — 재조립 첫 단계에서 두 배열 전량 `DestroyComponent()` 후 **둘 다** `Reset()`.
- **GC 소유** — 두 배열 `UPROPERTY(Transient)`.
- **드라이버 메시 지정** — `FirstPersonDriverMesh->SetSkeletalMeshAsset(GetMesh()->GetSkeletalMeshAsset())` 를 재조립 안에서(바디 메시가 바뀌면 자동 추종). AnimClass 는 BP 소유.
- **틱 순서 (1) 포즈** — 드라이버가 `GetMesh()` **자식**이므로 `AttachToComponent` 가 부모 틱 선행을 자동 등록한다(`SceneComponent.cpp:2465`). 팔로워→리더 선행은 `SetLeaderPoseComponent` 가 자동(`:2984`). 바디 틱은 병렬 애님 평가 완료까지 `DontCompleteUntil` 로 붙잡히므로(`SkeletalMeshComponent.cpp:2889`) `CopyPoseFromMesh::PreUpdate` 가 **이번 프레임** 포즈를 읽는다.
- 🔴 **틱 순서 (2) IK 타깃 — 수동 선행 1줄이 필요하다 (rev.6 정정)**. rev.2 의 "수동 선행 불필요"는 CopyPose 만 있을 때 참이었고 **IK 가 들어오면서 거짓이 됐다.** 실측: `UpdateAimDownSights` 는 `UFPSRWeaponFireComponent::TickComponent`(`FPSRWeaponFireComponent.cpp:674`)에서 돌고, 그 컴포넌트는 틱 그룹을 지정하지 않아 **`TG_DuringPhysics`**(기본)인데 스켈레탈 메시는 **`TG_PrePhysics`** 다. 즉 드라이버가 **항상 먼저** 틱해 IK 가 **지난 프레임의 무기 배치**를 읽는다 → 힙모션·ADS 블렌드·룩 스웨이가 매 프레임 총을 움직이므로 **1인칭 손이 상시 한 프레임 뒤를 따른다**(§12-7 ⑤ 실패).
  → `BeginPlay` 에 **`FirstPersonDriverMesh->AddTickPrerequisiteComponent(WeaponFire)`** 한 줄. 선행이 뒤 그룹이면 엔진이 종속 틱을 그 그룹으로 내린다(`TickTaskManager.cpp` — "max of the prerequisites") → 드라이버와 1P 팔로워가 `DuringPhysics` 로 이동하고 렌더는 그 뒤라 무해. `WeaponFire` 의 선행은 메시뿐(`FPSRCharacter.cpp:346`)이라 순환이 없다.
  ⚠️ **이 프로젝트는 같은 부류를 이미 한 번 고쳤다** — 팔 경로가 정확히 이 이유로 `FirstPersonArms->AddTickPrerequisiteActor(this)`(`:362-364`)를 명시 배선했고 `:3563` 이 *"one-frame gun-anchor lag class this project has already fixed once"* 를 경고로 남겼다.
- **틱 옵션** — `FirstPersonDriverMesh->VisibilityBasedAnimTickOption = AlwaysTickPoseAndRefreshBones` 를 **명시적으로** 설정한다. *근거 교정*: 클래스 기본값이 이미 이 값이지만(`SkinnedMeshComponent.cpp:460`), `ACharacter` 생성자는 `Mesh` 만 `AlwaysTickPose` 로 낮춘다(`Character.cpp:124`) — 드라이버는 별개 컴포넌트라 기본값을 받는다. 명시는 **불변식 고정**이지 결함 회피가 아니다. 바디 쪽 `FPSRCharacter.cpp:150` 도 같은 성격. **두 줄 다 건드리면 파츠가 굳는다.**
- **델리게이트** — 구독하지 않는다.
- **초기 동기화** — `OnRep_SelectedCharacterPartSet` + `OnRep_PlayerState` **양쪽** 훅업. `APawn::OnRep_PlayerState → SetPlayerState` 로 클라에서도 PS→폰 포인터가 서므로(`Pawn.cpp:626-647`) 어느 것이 먼저 와도 한 번은 돈다.
- **LOD** — 숨긴 리더는 렌더되지 않아 LOD 예측이 갱신되지 않고 팔로워 LOD 가 고정된다(`SkinnedMeshComponent.cpp:4447-4450`). 플레이어 1~4명이라 수용하고 기록만 남긴다.

## 9. 데이터드리븐 경계 (핵심원칙 2)

| 값 | 나가는 곳 | 기본값 | 비고 |
|---|---|---|---|
| 파츠 메시 · 슬롯 수 · 이름 | `UFPSRCharacterPartSetDataAsset` | — | 슬롯 수 고정 없음 |
| 어느 컴포넌트로 만들지 | 같은 DA 의 `ViewScope` | `ThirdPersonOnly` | |
| 머티리얼 변형 | 같은 DA 의 `MaterialOverride` | null | 3P·1P 양쪽 적용 |
| 선택 가능한 세트 목록 | `UFPSRCharacterPartPoolDataAsset` | — | |
| 풀 에셋 경로 | `UFPSRGameFlowSettings::CharacterPartPool` | 없음 | **C++ 하드코딩 금지** |
| 기본 외형 | `AFPSRCharacter::DefaultCharacterPartSet`(BP) | null | `DefaultPrimaryWeapon` 과 같은 자리 |
| 1인칭 워프 | `ABP_VoxelChar_FP` 의 Control Rig 슬롯(후속) | 없음(통과) | |
| 1인칭 FOV·스케일 | `UCameraComponent`(엔진 프로퍼티) | **꺼짐 유지**(§2) | 우리 필드를 만들지 않는다 |
| 손이 붙을 **소켓 이름** | 무기 DA `LeftHandSocket`/`RightHandSocket` | 현재 `None`(=IK 꺼짐) | **파츠가 아니라 소켓 이름**을 정한다(§5-1) |
| 손이 붙을 **실제 자리** | 무기 **파츠 메시**의 소켓 | — | 소켓을 가진 파츠가 타깃. 진화·교체 시 자동 추종 |
| 파지 미세보정 | `{Left,Right}HandGripOffset` | 0 | 그립 컴포넌트 공간(에디터에서 소켓 미는 값과 같은 단위) |
| IK 세기 | `{Left,Right}HandIKWeight` | 1.0 | 0 = 그 손 IK 끔 |
| 색 파라미터 **이름** | `UFPSRCharacterPartSetDataAsset::TeamColorParamName` | `None` = 색 적용 안 함 | 머티리얼의 벡터 파라미터 이름. 파츠 머티리얼이 바뀌어도 DA 만 고치면 된다 |
| 인덱스 → **색 값** | `UFPSRCharacterPartPoolDataAsset::PlayerColors`(`TArray<FLinearColor>`) | 비어 있음 = 색 적용 안 함 | **값의 정본은 `ArtDirection.md` §A-3-5** — 여기는 그 결정을 담는 그릇이다. 개수가 곧 색 슬롯 수 |

> 색 관련 값이 **C++ 에 하나도 없다** — 이름도 값도 개수도 전부 콘텐츠다. 아트가 4색을 5색으로 늘려도 코드는 무변이다.

🔴 **쿡 포함 (필수 — 이게 없으면 PIE 는 통과하고 패키지가 죽는다)**
`Config/DefaultGame.ini` 에 **`+DirectoriesToAlwaysCook=(Path="/Game/Characters/VoxelChar")`** 를 추가한다. 현재 목록에 `/Game/Character`(단수)는 있으나 **`/Game/Characters`(복수)가 없고**, VoxelChar 콘텐츠는 후자에 산다(실측). 풀·세트 DA 는 config 소프트참조로만 닿아 하드 참조 그래프에 없으므로 **쿡에서 조용히 빠진다.** 무기 풀이 살아 있는 이유는 `/Game/Weapons` 가 목록에 있어서다.
⚠️ **`/Game/Characters` 로 넓게 잡지 않는다** — 그 아래 `Mannequins/` 가 129 파일·125MB 이고 쿠커는 Path 를 로컬 디렉터리로 바꿔 **재귀 수집**한다(`CookCommandlet.cpp:485-496`). VoxelChar 폴더는 20 파일·17MB 이고 §4 의 신규 DA 3개가 전부 거기 산다. 매너퀸 애님 중 실제로 쓰는 것들은 `ABP_VoxelChar_Body` 의 **하드 참조**로 이미 쿡된다.

## 10. 성능 예산

- **애님그래프** — 플레이어당 **2**(바디 + 드라이버). 파츠는 전부 팔로워라 0. 4인 = 8.
- **컴포넌트** — 바디 1(숨김) + 3P 파츠 N + 드라이버 1(숨김) + 1P 파츠 M. 현재 N=8·M=2 → **12**, 4인 = 48. **그려지는 것은 10**(3P 8 + 1P 2).
- **바운드** — `bUseBoundsFromLeaderPoseComponent=false` 라 팔로워마다 리더 피직스에셋 AABB 를 계산한다(리더 바운드 복사보다 비싸지만 정확). 플레이어 전용이라 수용.
- **정점** — 파츠 합 ≈ 19.1k. 1P 추가분 = 손 2개 **112 정점**.
- **메모리** — 드라이버가 바디와 같은 메시 에셋 → 추가 0.
- **복제 대역** — 플레이어당 오브젝트 참조 1개, **바뀔 때만**. 런 중 0.
- **적 스웜** — 붙지 않는다.

**🔴 드로우콜 — Leader Pose 는 게임 스레드만 줄이고 렌더 비용은 안 줄인다** (Epic 문서·포럼 실측). 파츠가 같은 머티리얼을 써도 스켈레탈 메시는 배칭되지 않으므로 **컴포넌트 수 = 드로우콜 수**다.

| | 지금(머지 메시 1개) | CHR1(파츠) | Skeletal Mesh Merge 로 합치면 |
|---|---|---|---|
| 플레이어 1인 | 1 | **10**(3P 8 + 1P 2) | 2(3P 1 + 1P 1) |
| 4인 | 4 | **40** | 8 |

**지금 머지하지 않는다** — 예산은 적 수백에 걸려 있지 플레이어 40 드로우콜이 아니다(제1원리). **전환 조건을 여기 적어 둔다**: ① 플레이어 렌더가 프레임 예산에 실제로 잡힐 때 ② 파츠 수가 크게 늘 때. 전환 비용 = `Skeletal Merging` 플러그인 활성화 + 런타임 머지 단계 + **텍스처 아틀라스 전제**(Epic 권고).
⚠️ **전환하면 모프 타깃이 막힌다** — Leader Pose 는 모프를 지원하지만 **Merge 는 지원하지 않는다**(Epic 문서). 얼굴·체형 모프를 쓸 계획이 생기면 이 전환은 영구히 닫히므로, 둘 중 하나를 고르는 시점이 온다는 것을 여기 기록해 둔다.

## 11. 미결정 항목 · 명세 갭 처리

1. ~~드라이버 ABP 의 클래스~~ → **결정됨: `UFPSRCharacterAnimInstance` 를 부모로 한다.** 손 IK 가 범위에 들어오면서 드라이버 그래프가 `LeftHandGripLocation`·`LeftHandIKAlpha`(우측 쌍 포함)를 읽어야 하고, 그 값을 계산하는 곳이 이 클래스다(`:254-264`). 대가 = `bIsMainInstance` 계산이 캐릭터당 두 번 돈다(읽기 전용이라 정합성 문제 없음, 낭비만). `UAnimInstance` 직속이면 그 값들을 다시 만들어야 해서 더 비싸다.
1-b. **IK 시각 튜닝** — 소켓 위치·`{Left,Right}HandGripOffset`·`{Left,Right}HandIKWeight` 는 **사용자가 눈으로 맞춘다**(`[[leave-fine-tuning-to-user]]`). ⚠️ 과거 실패 기록: ADR 0015 가 *"손 소켓을 찍고 회전을 이식해도 **손목이 돌아간 채** 남았다 — 오른손 IK 가 effector 회전을 강제해 포즈와 싸웠다"* 를 남겼다. **다만 조건이 다르다** — 그때는 손가락 있는 구매 리그였고 지금은 **56정점 블록 손**이라 손목 회전이 읽히지 않을 가능성이 크다. 그래서 "또 깨진다"를 전제로 두지 않고 **§12 에서 실측해 판정**한다. 깨지면 회전만 IK 에서 빼고 위치만 쓰는 것이 1차 후퇴선.
2. **`FirstPersonOnly` 만 있는 세트** — `IsModularBodyActive()`(3P>0)가 거짓이라 리더가 안 숨고 바디 위에 1P 파츠가 겹친다. → **`IsDataValid` 에서 금지**(3P 파츠 0개 세트 = Error)로 처리한다. 활성 정의를 "파츠 1개 이상"으로 넓히지 않는 이유 = 리더 숨김은 3P 대체물이 있을 때만 옳다.
3. **원격 폰의 1P 파츠·드라이버 틱** — 남의 폰의 1인칭 파츠는 아무도 보지 않는다. 지금은 모든 폰에서 만들고 틱한다. → **C3 에서 실측 후** 필요하면 `IsViewedThroughOwnEyes()` 게이트를 건다(4인이라 우선순위 낮음).

**갭 처리 규칙(고정)** — 명세에 없는 판단이 필요하면 **추측하지 말고 멈추고 "명세 갭"으로 보고**. 갭은 C1 으로 돌아가 Opus 가 고치고, **구조를 바꾸는 갭이면 G1 을 다시 태운다**.

## 12. 검증 기준

| # | 검사 | 통과 조건 |
|---|---|---|
| 1 | 명세 대조 | §5·§6·§7 이 코드와 1:1 |
| 2 | 빌드 | `Build.bat FPSRogueliteEditor Win64 Development` **`Result: Succeeded`**. 신규 .cpp 2개 → **`-DisableUnity` 1회 + `-DisableAdaptiveUnity -ForceUnity` 1회 둘 다**(§6-6). 전자는 include 누락을, 후자는 익명 네임스페이스 동명 충돌을 잡는다 — 한쪽만은 반쪽 검증이다 |
| 3 | 헤드리스 스모크 | `FPSRoguelite.Smoke.ModuleLoads` 통과 |
| 4 | 쿡 경로 | `DefaultGame.ini` 에 `/Game/Characters` 행이 있고, 패키지 빌드 후 `.pak` 에 `DA_VoxelChar_PartSet_Default` 가 들어갔는지 확인(§9) |
| 5 | 회귀 | `DefaultCharacterPartSet` 과 PS 선택이 **둘 다 null 일 때** 머지 메시가 보이고 무기 부착·ADS 가 종전 경로를 탄다. ⚠️ **"변화 0" 이 아니라 "기준선 확립"이다** — 팔 슬롯이 빈 뒤로 PIE 기록이 없어(`da409893` 런타임 미검증, `838c78c1` 빌드만) 비교할 기준선 자체가 없다. 이번에 그 상태를 기록해 다음 유닛의 기준선으로 남긴다 |
| 6 | 레드팀 | **G2** = §6-6-1. **P1 잔존 시 푸시 금지** |
| 7 | PIE / 사용자 스모크 | 아래. **선택 수단** = BP 의 `DefaultCharacterPartSet`(서버가 `PossessedBy` 에서 PS 에 써서 복제) + `FPSR.SelectPartSet <idx>` |

**§12-7 사용자 확인 항목** (⚠️ `L_Lobby` 가 비어 있어 게임플레이 맵 2인 PIE 로 진행)
1. **호스트 화면**: 원격 클라의 몸이 파츠로 보인다.
2. **원격 클라 화면**: 호스트의 몸이 파츠로 보인다. — *둘 다 본다. 한쪽만 보면 반쪽이 죽은 걸 못 잡는다.*
3. **1인칭**: **손이 보이고 머리·몸통·다리는 안 보인다.**
4. 🔴 **자기 그림자**: 바닥/벽에 **몸 그림자가 나온다**(§3-2 의 `SetCastHiddenShadow(true)` 판정 — VSM 이 꺼진 프로젝트라 이 한 줄이 그림자의 유일한 근거다. 총 그림자만 홀로 떠 있으면 **실패**). ⚠️ **머리는 빠진다** — 머리 본 숨김이 팔로워에 전파되기 때문이고 **오늘 머지 메시도 동일**하다(회귀 아님). 머리 유무가 아니라 *몸통·팔다리 그림자가 포즈를 따라가는지*를 본다.
5. **무기 — 힙**: 조준하지 않은 상태에서 `SOCKET_Weapon` 에 정상 크기로 붙고 애니 중 손을 따라간다. 1인칭 손과 무기의 상대 위치가 프레임 간 흔들리지 않는다(§8 틱 순서 판정).
6. 🔵 **무기 — 조준(손 IK 판정)**: 조준해 총이 화면 중심선으로 갈 때 **양손이 총을 따라간다.** 판정 항목 셋: (a) 손이 그립에 붙는가 (b) **손목이 부자연스럽게 꺾이지 않는가**(§11-1b 의 과거 실패가 블록 손에서도 재현되는지 — 재현되면 회전을 빼고 위치만 쓰는 후퇴선) (c) 파지 타깃이 없는 무기(소켓 미저작)에서 **IK 가 조용히 꺼지고 팔이 이상한 데로 뻗지 않는가**.
6-b. **파츠 교체 추종**: 그립 소켓을 가진 파츠를 다른 것으로 바꿨을 때(또는 진화시켰을 때) **손이 새 위치로 따라간다**(§5-1 의 재조립 재해석 판정).
6-c. 🔵 **색 — 시인성 판정**: 2인 PIE 에서 **호스트와 클라가 서로 다른 색**으로 보이고, **양쪽 화면에서 같은 플레이어가 같은 색**이다. 자기 1인칭 손 색도 남이 보는 내 몸 색과 같다.
6-d. 🔴 **색 — 트래블 판정**(별도 절차): 게임플레이 맵에서 시작 → 콘솔 `FPSR.TravelLobby` → `FPSR.TravelGame` → **색이 유지되는지** 본다. `CopyProperties` 복사가 빠지면 "로비에선 맞고 런에선 다 같은 색"이 되는데 **맵에서 바로 시작하면 이 결함이 안 드러난다**(§5-2). ⚠️ `L_Lobby` 가 비어 있어도 트래블 자체는 돈다 — 보는 것은 로비의 그림이 아니라 색의 생존이다.
6-e. **3인칭 왼손(부수효과 판정)**: DA 소켓 저작으로 **바디 왼손 IK 도 켜진다**(§2). 원격 화면에서 상대의 왼손이 핸드가드에 붙는가, 그리고 **재장전 몽타주 중에 왼손이 IK 와 싸우지 않는가**(커브 0건 — §2 🪤). 싸우면 §11 의 결정(커브 저작 vs 알려진 결과)으로 넘긴다.
6-f. **무음 실패 점검**: 로그에 `elbow bone ... not on the body skeleton` 경고가 **0건**이어야 한다(엘보 본 이름이 틀리면 IK 가 조용히 꺼진다 — §5-1 🪤).
7. **원격 화면 컬링**: 상대가 재장전·슬라이드 하는 동안 화면 가장자리에서 파츠가 사라지지 않는다(§3-2 바운드 판정).
8. **대조군**: `FPSR.SelectPartSet -1` 로 세트를 비운 뒤 — 머지 메시가 통째로 보이고 1인칭에서 몸이 보인다(= 안전 실패 동작). *`[[verify-with-control-group]]`*

## 13. 레드팀 지적 원장 (C3에서 채운다)

*(비워 둠 — G2 에서 채운다)*
