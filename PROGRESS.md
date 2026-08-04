# PROGRESS — 지금 하고 있는 것

> 다른 세션/다른 AI가 **즉시 이어받기** 위한 문서. **여기엔 진행 중인 것만 남긴다.**
> 끝난 작업 → `Docs/WorkLog.md` · 증상→원인→해결 → `Docs/Troubleshooting.md`
> 구조 결정 → `Docs/Architecture/*.md` · 확정 설계·기획 → `Game.md`(SSOT 허브) · 커밋 상세 → `git log --oneline`
>
> **작업을 끝낼 때마다, 그리고 중단 전 반드시 이 파일을 갱신하고 커밋한다.**
> 한 항목이 완료되면 **여기서 지우고 `Docs/WorkLog.md` 맨 위로 옮긴다.**

**최종 갱신: 2026-08-04 · 브랜치 `refactor/character`**

---

# 🔴 현재 트랙 — 1인칭 팔을 카메라 부착 + 자체 애님그래프 (ADR 0003)

## 🔄 1인칭 표현 방식 재결정 — ✅**[ADR 0003](Docs/Architecture/0003-first-person-arms-camera-anchored.md) 작성 완료** / ⏳배선 대기 (2026-08-03)
**사용자 실플레이 판정: "3인칭 애니를 1인칭으로 쓰니 부자연스럽다."** 이게 ADR 0002의 핵심 전제(*"3P 애니가 PWAS 역할을 차지한다"*)를 뒤집는다. 증상 3개가 전부 같은 뿌리다 — 팔이 카메라 기준으로 낮음 · 팔이 카메라를 **감싸서** 생기는 쐐기 · 시선과 몸 방향 어긋남.
> 🔧 정정: 쐐기의 원인을 처음엔 "근접 클립면 10cm"로 적었으나 **이 프로젝트의 근평면은 1.0cm다**(`Config/DefaultEngine.ini`). 근평면을 낮추는 방향은 헛수고다 — `Docs/Troubleshooting.md` B1.

**결정: 안 나-1 = PWAS 유지 + 팔 메시만 NeonV 룩** (사용자 2026-08-03).

### 🎁 그런데 산출물이 이미 있다 — Blender 재작업 불필요
`Content/Character/FPArms/NeonV_FPArms` (커밋 `2d6f57ac`, 2026-07-26, HEAD 조상 — ADR 0002가 폐기했지만 **파일은 안 지워졌다**).
- 스켈레톤 = **`S_Mannequin`**(PWAS와 동일, 스켈레톤 **161본** / 이 메시가 쓰는 본은 **65**) · 섹션 3(Body/Jacket/Accessoris) · 소매를 손목 10cm 위 절단
- ~~바운드 extent (55.22, 21.80, 31.97) ≈ `SK_FP_Manny_Simple` (55.44, 19.90, 31.20) → rest pose가 뼈 단위로 맞춰져 있다~~
  > 🚨 **이 결론은 틀렸다**(2026-08-04 반증). **바운드는 미러에 대해 불변**이라 Y축 반전을 못 잡는다 — 실제로는 rest pose의 Y 부호가 통째로 반대였다. 아래 "지금 막힌 곳" 참조.
- 현재 참조자 = 자기 PhysicsAsset뿐(놀고 있음)
- 저작 이력·함정 = Blender repo `Docs/HANDOFF_NEONV_FPARMS_RESULT.md`
  > ★ **UE는 스켈레톤을 공유하면 레퍼런스 포즈도 공유한다.** 메시 바인드 포즈가 스켈레톤 ref pose와 다르면 그 부위가 강제로 늘어난다(실측: 48.8cm 메시가 에디터에 **157cm**). 스켈레톤을 공유할 거면 rest pose를 뼈 하나까지 맞출 것.

### PWAS 실측 (2026-08-03)
`Content/ProceduralWeaponAnimationSystem` **145 에셋** 생존 — 무기군 6종 포즈 · FP 재장전 몽타주 · 절차 커널(`ABP_FPChar`: AnimGraph 22 / F_ProceduralAnimations 15 / EventGraph 38) · 프리셋 DA. 전부 `S_Mannequin` 기준.
- **Blu로 리타게팅(안 다)은 비싸다**: 기존 리타게터 2개(`RTG_UE4Man_to_Blu`=Rifle_01이 쓴 것, `RTG_Manny_to_Blu`)가 `S_Mannequin`을 안 덮고, 무엇보다 **커널이 마네킹 본 이름에 묶여 있다**(`clavicle_l`→`shoulder_L`, `upperarm_l`→`upper_arm_L`, `lowerarm_l`→`lower_arm_L`). 안 나-1은 이걸 전부 우회한다.
- ℹ️ Blu 손가락은 **5개 다 있다**(`little_*`/`middle_*` 명명이라 검색에서 놓치기 쉬움 — 같은 착오가 FPARMS_RESULT에도 기록돼 있다).

### ✅ ADR 0003 확정 (2026-08-03) — 설계는 끝났다. 코드는 아직 안 건드렸다
> **구현 전 [ADR 0003](Docs/Architecture/0003-first-person-arms-camera-anchored.md) 전문을 읽을 것.** 불변식 4개 + 함정 6개 + 기각안이 전부 거기 있다.

**결정**: 축1 = **포즈 에셋만**(PWAS 커널 미사용) / 축2 = **단일 무기 · 머신별 부착** / 축3 = 로컬 바디 계속 숨김.
사용자 확정 3건 — ①카메라 반동은 `UFPSRRecoilComponent`(CrystalRecoil)가 계속 소유 ②조준점 완전 고정 유지 ③로컬 바디 숨김 유지.

**🚨 착수 전 세션이 적어둔 것 중 3개가 틀렸다** (실제 파일로 검증):
1. **근평면은 10cm가 아니라 1.0cm다** — `Config/DefaultEngine.ini:22` `NearClipPlane=1.0`(엔진이 `UEngine::Init`에서 읽음). 인용됐던 `CoreGlobals.cpp:260`의 10.0f는 **config 읽기 전 초기값**. 그러니 4~5.5cm 본은 안 잘린다 → 쐐기꼴의 원인은 **팔이 카메라를 감싸는 것**. *"근평면을 더 낮추면 된다"는 헛수고*
2. **`UpdateAimDownSights`는 이미 카메라 공간에서 푼다** — 마지막 한 줄만 월드. 힙 기준도 `GetAttachParent()`로 일반적. **ADS 전환 비용 ≈ 0**(대상 한 줄)
3. **DA에 `ArmsAnimInstanceClass`는 이미 없다** — "복원"이 아니라 신규 3필드(`ArmsAnimLayerClass`·`ArmsReloadMontage`·`ArmsEquipMontage`)

**🚨 PWAS 커널을 쓰면 안 되는 이유**(uasset 덤프로 확인): `ABP_FPChar`가 `BP_FPCharacter`(데모 폰)로 **캐스트**한다 → 우리 폰에 꽂으면 값 전부 0. `AC_ProceduralWeaponAnimationSystem`(4.2MB)이 **카메라 반동·ADS 알파·스웨이·크라우치 스프링·자체 무기 프리셋 DA**를 소유 → 우리 C++ 3개와 정면 충돌.

**🪤 PWAS 폴더를 지우면 안 된다** — `NeonV_FPArms`의 스켈레톤이 `/Game/ProceduralWeaponAnimationSystem/Demo/FPManny/S_Mannequin` **바로 그 에셋**이다. ADR 0002의 "PWAS 폐기 순서" 절은 무효.

### ✅ C++ 배선 완료 (2026-08-03) — 빌드 2회 Succeeded(경고 0) · 스모크 Success
| # | 변경 | 핵심 |
|---|---|---|
| 1 | 팔을 **카메라에 부착** · `LeaderPoseComponent` 제거 · `FirstPersonArmsAnimClass`로 자체 구동 | 바디의 `AlwaysTickPoseAndRefreshBones`는 **유지**(근거가 "팔이 따라오니까"→"그림자가 본을 요구하니까"로 바뀜) |
| 2 | 분할 판정 = `IsLocallyControlled() && IsViewedThroughOwnEyes()` | 머리 숨김과 **같은 질문**을 공유(`IsViewedThroughOwnEyes()` 추출). 팔 미저작·DBNO 관전·관전 블렌드가 한 번에 맞는다 |
| 3 | `AttachWeaponMeshes()` 추출 — 대상 = 팔/바디, `FirstPersonPrimitiveType`, `RefreshWeaponVisibility(bForce)` | **모듈러 파츠도 태그**해야 한다(태그는 부착 체인으로 안 내려간다) — 생성 시점 + 분할 전환 시점 양쪽 |
| 4 | ADS 솔브 대상 = 팔, **상대 트랜스폼**으로 쓰기 | 수식 무변경. 상대로 쓰는 이유 = 카메라 회전이 **post-tick**(`GetCameraView`→`SetWorldRotation`)이라 월드로 쓰면 한 프레임 낡은 카메라에 팔이 고정 |
| 5 | `GetLeftHandGripTransform(ForMesh, Out)` + `IsAttachedTo` 가드 | 없으면 **내 그림자의 왼팔이 카메라로 끌려간다**. BP 호출 0개 확인 후 시그니처 변경 |
| 6 | `UFPSRFirstPersonArmsAnimInstance` 신규 + DA 3필드 + `RefreshArmsAnimLayer` + 팔 몽타주 | 바디와 **완전 대칭**, 서로를 참조하지 않음(불변식 11). `AFPSRCharacter::IsReloading()` 파사드 추가 |

### 🔑 저작 지점 = **컴포넌트 슬롯 하나** (2026-08-03 사용자 결정)
처음엔 `FirstPersonArmsMesh`·`FirstPersonArmsAnimClass` 두 필드를 두고 코드가 컴포넌트를 **덮어쓰게** 짰다. 실제로 사고가 났다 — 사용자가 컴포넌트에 `NeonV_FPArms`를 넣었는데 필드는 폐기 대상 `Blu_FP_Arms`(Blu 스켈레톤)를 가리키고 있어, **PIE를 눌렀으면 포즈가 하나도 안 붙는 팔이 나왔을 것**이다.
> **사용자 지침: 컴포넌트로 넣을 수 있는 건 컴포넌트 슬롯이 진실원천.** 코드가 컴포넌트를 덮어쓰는 형태는 다른 작업에서도 고친다.
- **두 필드 삭제.** 분할 게이트 = `FirstPersonArms->GetSkeletalMeshAsset() != nullptr`. 메시·애님클래스·레스트 트랜스폼 전부 BP 컴포넌트에서 저작
- **전수 조사 결과 위반은 이 한 곳뿐이었다** — `AFPSREnemyBase`·`AFPSRXPPickup`·`AFPSRBossBase`는 이미 `if (GetStaticMesh() == nullptr)`로 **컴포넌트가 이기고 config는 폴백**. 무기 메시/파츠는 런타임 교체라 DA가 소스인 게 정당. 에디터 모듈은 프리뷰 툴

**⏳ 다음 = 콘텐츠 (사용자 작업)**
1. ✅ 컴포넌트에 `NeonV_FPArms` + 레스트 트랜스폼 `(-10, 0, -160)` 지정 완료(헤드리스 검증)
2. **팔 AnimBP `ABP_FirstPerson`** (`/Game/Character/Player/`, 작업 중) — 🚨 **부모 클래스가 `AnimInstance`로 되어 있다. `FPSRFirstPersonArmsAnimInstance`로 리페어런트해야** 값(`bIsAiming`·`LeftHandGripWorld`·`LeftHandIKAlpha`)과 레이어 push가 산다
   - 🚨 IK는 **`IK Rig` 노드가 아니라 `Two Bone IK`** — IK Rig는 Rig Definition 에셋을 요구하고(현재 경고), 월드 이펙터를 변수로 못 받는다. Two Bone IK = `hand_l` + `LeftHandGripWorld`(World Space) + 알파 `LeftHandIKAlpha` + **JointTarget 필수**(없으면 팔꿈치 플립)
   - 무기군 포즈는 최종적으로 **레이어(`ArmsAnimLayerClass`)** 로 빼야 무기 추가 시 중앙 수정이 0이 된다(지금은 `A_FP_Rifle_Pose` 직결 = 1단계로는 정상)
3. 레스트 트랜스폼 **재조정은 그래프가 들어온 뒤에** — 지금 값은 A포즈 기준이라 소총 포즈가 오면 체감이 달라진다
4. PIE 검증 7항목 — 특히 ④ 내 그림자 왼팔 · ⑥ 동료 화면 무기 · ⑦ 다운→관전 시 무기가 바디 손으로
5. `Content/Characters/Blu/SkeletalMeshes/Blu_FP_Arms/` **폐기**(참조 0 확인 후 — 이제 코드 참조는 끊겼다)

⚠️ **`WeaponAttachScale` 0.85는 Blu 팔 기준**이다(마네킹 팔은 27.5% 길다). 1P에서 총이 작아 보이면 그때 판단 — 지금 필드를 만드는 건 추측.

### ⏭️ 다음
1. **1인칭 팔 메시 모양** — 뼈가 살아난 지금도 팔이 구겨진 종이처럼 뾰족하게 보이면 스키닝/웨이트 별건(P2 산출물). 포즈는 이제 정상이므로 순수 메시 문제로 좁혀졌다.
2. **4b** — 8방향 시작·정지 전환(클립 40개+) + Split_Jumps 108 리타게팅.
3. 무기 DA 나머지 8개에 `BodyAnimLayerClass` 미배정 → 나이프·맨손은 본체 폴백(`ABP_Blu_Body`의 `GetWeaponLocomotionPose` = `Blu_W2_Stand_Relaxed_Idle_IPC` 단일 클립)으로 선다. 무기별 레이어가 필요해지면 그때 만든다. ⚠️ 스텁 클립은 **BlendSpace의 idle 샘플과 다른 클립**으로 둘 것(위 함정 2).
4. (미해결 잔여) `wall_topout` f0/f12 포즈 판단 · 1인칭 팔 컷 채택 여부.


### 🩺 팔 메시 = **결함 5개였다. Blender 쪽은 전부 끝. UE 임포트만 남음** (2026-08-04)
증상 *"애니는 도는데 메시가 늘어나고 꼬인다"* 의 원인이 하나가 아니었다. **전부 실측으로 갈랐다.**

| # | 결함 | 원인 | 상태 |
|---|---|---|---|
| 1 | rest pose **Y축 통째 반전** | 옛 `fp_arms_retarget.py`가 UE 좌표를 Blender 좌표로 그대로 씀 | ✅ 2026-07-26 `fp_arms_rebuild.py`가 이미 고쳤는데 **GLB가 UE에 안 들어가 있었다** |
| 2 | 전 본 **롤 90°** | **UE의 glTF 임포터**가 FBX 임포터와 본 축 규약이 다름 | ✅ **FBX로 내보내 해결** |
| 3 | 본 **66개**(65+1) | Blender FBX가 아마추어 오브젝트를 본으로 씀 | ✅ 아마추어를 `root`로 개명 + 기존 `root` 본 삭제 |
| 4 | **스킨 웨이트 미완성** | `fp_arms_rebuild.py` Phase 3이 **이름만** 바꿔서 Blu에 없는 본이 빈칸 | ✅ **기준 메시에서 통째 전이**(`fp_arms_reweight.py` 신규) |
| 5 | **새끼손가락 실종** | `fp_arms_extract.py`가 팔 뼈 목록에 `pinky`를 적었는데 **Blu는 `little`이라 부른다** → 추출 첫 단계에서 양손 482정점이 몸통에 남음 | ✅ 접두사 수정 + 파이프라인 전체 재실행 |

**본 정합은 그대로 유지** — `SK_FP_Manny_Simple` 대비 어긋난 본 0/65, 스켈레톤 `S_Mannequin` 무손상.

**4번이 왜 컸나**: Manny 리그는 팔뚝 표면을 **주로 twist 본이 굴린다**(기준 메시에서 `upperarm_twist_02`가 2307정점으로 `lowerarm` 1372보다 많다). 우리 팔은 그 twist 8개·metacarpal 8개가 전부 0이었다. 게다가 소매·팔·장식 **1385정점이 `clavicle` 하나에 통짜로 물려** 있어서, 위팔에 물린 부분과의 경계가 접히며 갈라졌다.

**산출물** `Saved/NeonV/NeonV_FPArms_v2.fbx` (`Saved/`는 gitignore — Blender 체인은 `블랜더/NeonV_FPArms_0{1,2,3}_*.blend` → `NeonV_FPArms_manny.blend`)

| 검증 | 결과 |
|---|---|
| 재현 충실도 (옛 메시 대비) | 기존 정점 2656개가 **최대 0.0002cm** 오차로 동일 · 늘어난 484개 중 **482개(100%)가 새끼 뼈 5cm 안** |
| 포즈 이탈량 (기준 팔 대비, 대조군 0.00) | 소매 전완 4.76→**1.67**cm · 맨살 손목 3.85→**0.52** · 손등 0.60→**0.07** · 새끼 1.56(4정점)→**0.02**(460정점) |
| 웨이트 | 무가중 정점 0 · 합 1 · twist 8/8 · metacarpal 8/8 · `pinky_01/02/03` 양손 물림 |
| 지오메트리 | `Icosphere` 제거 · 소매 비매니폴드 엣지 11→**0** · 새끼 마디별 106/133/80정점(검지 75/105/56과 동급) |

**✅ UE 재임포트 완료·검증 통과 (2026-08-04 12:56)** — 남은 것은 **PIE 눈 확인 하나**.

| UE 쪽 확인 | 결과 |
|---|---|
| 기록된 소스 | `Saved/NeonV/NeonV_FPArms_v2.fbx` |
| 메시 | 정점 5055 · 섹션 3 (Body/Jacket/Accessoris) |
| 본 잔차 vs `SK_FP_Manny_Simple` | 회전 중앙 **0.0002°**(최대 0.0055) · 위치 중앙 **0.0000cm**(최대 0.0002) · **어긋난 본 0/65** |
| 대조군 (기준 vs 기준) | 0.0000° / 0.0000cm / 0/65 — 계측 유효 |
| 스켈레톤 무결 | `Content/ProceduralWeaponAnimationSystem/` git 변경 **0** |

> 🔑 **이번에 가장 값어치 있던 것**: 대조군(같은 메시 두 번 재기)과 **A/B(같은 그래프에 PWAS 팔 넣기)**.
> 둘 다 없었으면 롤을 Blender roll로 고치려다 계속 헛돌았고(±90° 넣으니 120°가 나와 축이 다름이 드러났다),
> 웨이트가 범인인 것도 못 갈랐다. **바운드·눈대중으로는 다섯 다 안 잡힌다.**
> 새끼손가락도 "무가중이니 웨이트 문제"로 적혀 있었지만 **실제로는 살이 없었다** — 렌더 한 장이 갈랐다.

### 🩹 결함 6 — **FBX 메시가 UE에서 100배 작게 들어왔다** (2026-08-04, 해결)
임포트 후 팔이 점만 하게 보여 BP에서 `FirstPersonArms` 스케일을 100으로 보정해 놓은 상태였다.
**원인**: UE는 metre→cm ×100 변환을 지오메트리가 아니라 **아마추어 스케일**로 얹는데, 기존 스켈레톤(`S_Mannequin`)에 붙이면 **그 스케일이 통째로 버려진다.**
**🪤 본 잔차 0/65 검증이 이걸 구조적으로 못 잡는다** — 스켈레톤을 공유하면 `get_socket_transform`이 메시가 아니라 스켈레톤 ref pose를 돌려주기 때문. 판정 지표는 **`Approx Size`** 하나였다(4,445×5,080×8,438 → 고친 뒤 114×58×97, 기준 116×42×64).
**해결**: 지오메트리 ×100 굽기 + 최상위 오브젝트 스케일 0.01 + `FBX_SCALE_NONE`(블랜더 repo `e8f329a`에서 이미 검증된 우회법 — 이 프로젝트 두 번째 사례). 상세 = `Docs/Troubleshooting.md` **F1-b**.
→ `FirstPersonArms` 스케일은 **1**이 정답.

### 🔴🔴 트랙 전환 (2026-08-04) — 팔에 **자체 스켈레톤**을 준다 ([ADR 0004](Docs/Architecture/0004-first-person-arms-own-skeleton.md))
**사용자 결정**: *"애니메이션을 안 쓰려고 지금 애니메이션도 다시 저작하려는 것"* — PWAS 포즈를 그대로 재생하지 않으므로 `S_Mannequin` 에 묶일 이유가 사라졌다. ADR 0003 이 예고한 재검토 조건에 도달했고, 그 ADR 자신이 스켈레톤을 불변식에서 제외해 뒀다.

**묶여 있던 대가 = 손 왜곡**(실측): 손바닥 **+9~21%** · 손가락 **−7~10%** · 손 폭 +9%. Manny 는 손바닥 안에 손허리뼈가 있어 손가락 뿌리가 1.0~1.6cm 더 멀고, rebuild 가 메시를 그만큼 늘렸다. **포즈로는 못 고친다.**

**방향을 뒤집는다** — 메시를 뼈에 맞추던 것을, **뼈를 메시에 맞추는** 쪽으로. 본 이름·계층은 Manny 65본 그대로, 위치만 NEON-V 해부로.

> 🚨 **착수 전 결정 하나**: 팔까지 되돌리면 **소총을 두 손으로 못 잡는다**(도달 41.5cm vs 그립이 43cm 앞). 권장 = **손만 되돌리기**. 상세 = ADR 0004 "미결".

**➡️ 새 세션은 [`Docs/FPArms_OwnSkeleton_ResumePrompt.md`](Docs/FPArms_OwnSkeleton_ResumePrompt.md) 하나만 읽으면 된다.**

<details><summary>전환 전까지 끝내둔 것 (참고용)</summary>

1. **팔 메시 재임포트 — `Saved/NeonV/NeonV_FPArms_v5.fbx`** (사용자, 에디터에서)
   재킷·어깨장식을 뺀 **몸만** 버전(섹션 3→1). 1인칭에서 소매가 시야를 가려 사용자 결정(2026-08-04).
   되살리려면 export 인자만 빼고 다시 내보내면 된다 — `.blend`는 셋을 다 들고 있어 앞 단계 재실행 불필요.
2. **BP `BP_FPSRPlayer` → `Left Hand Grip Offset` = `(1, 3, 5)`** (사용자)
   왼손이 핸드가드를 뚫고 잡던 것 보정. C++ 기본값은 0 — 조정값을 코드에 박으면 다음 조정이 코드 수정이 된다.
3. PIE 확인 — 소매·손목·새끼손가락 3증상 + 왼손 그립

되돌리기: 옛 `Saved/NeonV/NeonV_FPArms.fbx` 재임포트, 또는 에디터 닫고 `git checkout` (에셋은 커밋 `839abef0`).

### ⏭️ 남은 것 — 왼손 손가락 포즈
그립 오프셋은 손목 **위치**만 고친다. Two Bone IK는 손가락을 안 건드리므로, 손가락이 펴진 채 핸드가드 아래에 놓인다. "쥔 모양"은 왼손 손가락을 핸드가드 굵기에 맞춰 말아주는 **포즈 저작**(Blender)이 필요하다.
> ⏸️ **손 모양이 바뀌므로 자체 스켈레톤 전환 뒤로 미룬다.**

</details>

### ⏳ 팔 AnimBP `ABP_FirstPerson` (`/Game/Character/Player/`) — 사용자 저작 중
`포즈 → Slot('DefaultSlot') → Two Bone IK(hand_l) → Output`. 부모 = `FPSRFirstPersonArmsAnimInstance` ✅
- ⚠️ **`Effector Location Space` = World Space 확인 필요** — 엔진 기본값이 `BCS_ComponentSpace`인데
  연결한 `LeftHandGripLocation`은 **월드 좌표**다. 그대로 두면 손이 맵 밖으로 날아간다
- ⚠️ `Joint Target Location`이 (0,0,0) = 팔꿈치 그 자체 → 폴 방향이 없어 팔꿈치가 뒤집힐 수 있다.
  ABP 프리뷰에선 폰 오너가 없어 `LeftHandIKAlpha`=0이라 **IK가 아예 안 돈다** → PIE에서 맞출 것
- 무기군 포즈는 최종적으로 **레이어(무기 DA `ArmsAnimLayerClass`)** 로 뺀다(지금은 `A_FP_Rifle_Pose` 직결)

---

# ⏳ 대기 목록 (코드는 끝났고 사람 손/확인만 남은 것)

각 항목의 전체 맥락은 `Docs/WorkLog.md`의 같은 제목 절에 있다.

| 항목 | 남은 것 |
|---|---|
| **4c 슬라이드·벽 포즈** | 사용자 재저작 — 슬라이드 저자세 · `wall_topout` f0/f12 판단 |
| **적 그림자 LOD** | 코드 커밋됨(`cb5a612d`)·빌드/스모크 통과, **거리별 on/off PIE 실측**만 |
| **에어 스트레이프** | PIE (`WallJumpMaxSpeed` 1400이 BP 오버라이드에 안 먹히는지 포함) |
| **근접 3번 슬롯** | 맨손 손맛 세트(몽타주·사운드·VFX, 칼도 비어 있음) + PIE 10항목 |
| **BP 그래프 정리** | 에디터에서 눈으로 확인 (`WBP_Lobby`·`WBP_RunHUD`·`BP_Door`·`ABL_Blu_W2_Rifle`) |
| **폐루프 디렉터 P0a-0** | PIE 2-client 라이브훅 게이트 → 통과 시 P0a-1 |
| **키 바인딩** | 휠 행 키 종류(`MouseWheelAxis` 축 키 ↔ `IA_Jump` Boolean) |
| **4b 로코모션** | 8방향 시작·정지 전환(클립 40개+) + Split_Jumps 108 리타게팅 |
| **무기 DA 8개** | `BodyAnimLayerClass`·`WeaponAttachScale`·`LeftHandSocket` 미배정(필요할 때) |
| **`Blu_FP_Arms` 폐기** | 참조 0 확인 후 삭제 (`Content/Characters/Blu/SkeletalMeshes/Blu_FP_Arms/`) |
