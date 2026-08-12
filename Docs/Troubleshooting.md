# 트러블슈팅 — 증상에서 원인으로

> **증상으로 찾는 문서다.** 무엇을 하려다 무엇을 봤는지로 목차를 잡았다.
> 전체 경위와 실측치는 `Docs/WorkLog.md`의 해당 절에, 구조 결정의 근거는 `Docs/Architecture/*.md`에 있다.
>
> 여기 있는 항목은 전부 **이 프로젝트에서 실제로 당한 것**이다. 일반론은 넣지 않는다 —
> 한 번 당한 것만 적어야 목록이 신호를 잃지 않는다.

## 목차

- [A. 애니메이션 · 스켈레톤](#a-애니메이션--스켈레톤)
- [B. 렌더 · 카메라](#b-렌더--카메라)
- [C. 빌드 · 에디터 기동](#c-빌드--에디터-기동)
- [D. VibeUE · 에디터 Python](#d-vibeue--에디터-python)
- [E. 데이터 · 컴포넌트 · BP](#e-데이터--컴포넌트--bp)
- [F. 단위 · 좌표계](#f-단위--좌표계)
- [G. 검증 방법 자체의 함정](#g-검증-방법-자체의-함정)

---

## A. 애니메이션 · 스켈레톤

### A1. 애니는 재생되는데 메시가 늘어나고 꼬인다
**원인**: 메시의 **바인드 포즈가 스켈레톤 레퍼런스 포즈와 다르다.** UE는 스켈레톤을 공유하면 ref pose도 공유하므로, 어긋난 부위는 강제로 늘어난다.

**실사고 2건**
- `NeonV_FPArms`: rest pose의 **Y 부호가 통째로 반대**였다(2026-08-04 발견, 2026-07-26부터 잠복). 이 스켈레톤은 좌우가 X축이라 Y = 앞뒤 → **팔이 뒤를 향한 rest pose**. 앞을 향해 저작된 애니 회전을 얹으니 살이 뒤집혔다.
- 그 이전: 본 *방향*만 맞추고 *길이*는 원본 유지 → 실제 바운드 48.8cm 메시가 에디터에 **157cm**로 뭉갰다.

**확인법** (아래 G1도 같이 볼 것)
```
정상 메시 1개를 기준으로 임시 SkeletalMeshActor 2개를 열린 레벨에 스폰
→ set_mobility(MOVABLE) 후 set_skeletal_mesh_asset
→ get_socket_transform(bone, RTS_COMPONENT) 로 전 본 위치·쿼터니언 각도 대조
→ destroy_actor
```
**언등록 컴포넌트는 트랜스폼이 전부 0이다 — 반드시 스폰해야 한다.**

**해결**: DCC(Blender)에서 rest pose를 고쳐 재익스포트. **미러는 회전이 아니므로 UE 임포트의 180° yaw로 못 고친다**(X·Y를 둘 다 뒤집는다).

### A1-b. 위치는 맞는데 전 본이 정확히 90° 돌아 있다 → **glTF로 넣지 마라**
`확인됨` **UE의 glTF 임포터는 FBX 임포터와 본 축 규약이 다르다.** 같은 메시를 glTF로 넣으면 FBX로 들어온 기준 메시 대비 **전 본이 균일하게 90°**(축 = 본 로컬 +X, 편차 0) 어긋난다. 위치는 완벽해서 rest 포즈는 멀쩡해 보이고 **애니를 재생할 때만 살이 뒤틀린다**.

- **Blender의 `edit_bone.roll`로는 못 고친다** — ±90°를 넣으면 120°가 나온다(두 회전축이 다르다는 뜻). Blender에서 자유로운 본 방향 파라미터는 roll 하나뿐이므로 **Blender 쪽 수정으로는 닿지 않는 문제**다.
- **해결 = FBX로 내보낸다.** 본 축 옵션은 **기본값이 정답**이다(`primary_bone_axis='X'` 류를 주면 180°/120°로 더 어긋난다).
- ⚠️ Blender FBX는 **아마추어 오브젝트를 본 하나로 추가**한다(65 → 66). 스켈레톤에 없는 본이 생기므로 위험하다 → 아마추어 오브젝트 이름을 `root`로 바꾸고 **기존 `root` 본을 삭제**하면 자식(`pelvis`·`ik_hand_root`)이 최상위가 되어 계층이 정확히 같아진다.
- 검증된 export 옵션 전문 = `Docs/FPArms_Weights_ResumePrompt.md`.

### A1-c. 뼈는 완벽한데 포즈만 무너진다 → 스킨 웨이트를 세라
본 단위 잔차가 **0/65인데도** 애니에서 메시가 조각나면 남은 건 **정점이 어느 본에 물렸는가**다. 실사고: `Jacket_FPArm`(소매 1296정점)이 **뼈 6개**에만 강체 바인딩되어 팔을 굽히는 순간 판때기로 찢어졌다.

**가르는 법 = A/B.** 같은 애님그래프의 프리뷰 메시를 **정상 메시**(여기선 `SK_FP_Manny_Simple`)로 바꿔 본다. 그쪽이 멀쩡하면 메시 유죄, 같이 깨지면 그래프/포즈 유죄. 10초면 갈린다.

**감사 방법**(Blender, 읽기 전용): 메시별로 `vertex_groups`를 돌며 **본별 물린 정점 수**를 찍는다. 물린 본이 한 자릿수면 강체 바인딩이고, 특정 말단 본이 0이면 그 부위가 안 따라간다. 웨이트 합·미가중 정점이 0이어도 **"전이가 덜 된 것"** 은 못 잡히니 반드시 **본 목록**으로 볼 것.

**🚨 twist 본을 "나중에"로 미루지 마라.** Manny 리그는 팔뚝 표면을 **주로 twist가 굴린다** — 기준 메시에서 `upperarm_twist_02`가 2307정점으로 `lowerarm`(1372)·`clavicle`(850)보다 많다. twist 웨이트가 0이면 팔이 `upperarm`/`lowerarm`에 통짜로 물려 **손목이 돌아간다.**

**고치는 법 = 손으로 메꾸지 말고 기준 메시에서 통째로 전이.** 애니가 저작된 바로 그 웨이트라 결과를 대조할 수 있다. Blender `DATA_TRANSFER` 모디파이어 + `vert_mapping='POLYINTERP_NEAREST'` + `layers_vgroup_select_src='ALL'` → `datalayout_transfer` → apply → 대상 스켈레톤 밖 그룹 제거 → `limit_total(8)` → `normalize_all`. 스크립트 = 블랜더 repo `NeonV_scripts/fp_arms_reweight.py`.
**전제**: 두 리그가 월드에서 겹쳐 있어야 한다. 본 잔차로 먼저 확인하고(0.5cm 넘으면 중단) 진행할 것.

**판정 게이트 = 포즈 이탈량**(눈으로 하지 마라). 기준 팔과 우리 팔에 **같은 월드 회전**을 먹인 뒤, rest에서 각 정점의 최근접 기준 삼각형을 기록해 두고 포즈 후 `mathutils.geometry.barycentric_transform`으로 "가 있어야 할 자리"를 예측해 실제 위치와 비교한다. **대조군 = 기준 메시 자신**(0.00이 나와야 계측을 믿을 수 있다).

### A1-d. 특정 손가락만 안 따라간다 → 웨이트가 아니라 **살이 없는지** 먼저 보라
"본이 무가중이다"를 웨이트 결함으로 읽으면 아무리 전이해도 안 고쳐진다. **그 본 자리에 정점이 있는지부터 세라.**

실사고: `pinky_02/03`이 무가중이라 "웨이트 미완성"으로 3주를 갔는데, 실제로는 **새끼손가락 살 자체가 메시에 없었다.** 원인은 `fp_arms_extract.py`의 팔 뼈 접두사 목록에 `"pinky"`가 적혀 있던 것 — **Blu 스켈레톤에 `pinky`라는 본은 없다. 새끼를 `little`이라고 부른다.** 그래서 추출 첫 단계에서 양손 **482정점이 팔이 아닌 것으로 분류돼 몸통에 남았다.**

**가르는 법 두 가지 (둘 다 싸다)**
- **수치**: 본 구간(head→자식 head)에서 반경 1.2cm 안의 정점 수를 손가락별로 센다. 검지 75/105/56 인데 새끼가 **4/0/0** 이면 웨이트 이야기가 아니다
- **렌더**: 손을 손등 쪽에서 한 장 찍는다. 손가락 개수는 한눈에 보인다 — 이게 결국 3주를 갈랐다

**🪤 일반화**: 리타게팅 파이프라인은 **원본 이름과 대상 이름이 섞인다.** 필터·접두사·화이트리스트는 **어느 쪽 이름 규약인지 파일마다 명시**할 것. 이 repo에서 `little`(Blu) ↔ `pinky`(Manny)는 `fp_arms_rebind.py`/`rebuild.py`의 `MAP`이 옮기고, `fp_arms_extract.py`는 **원본(Blu) 이름**으로 적어야 맞다.

### A2. 값은 전부 정상인데 포즈가 얼어 있다
**원인**: `AlwaysTickPose`는 **그래프 Update만** 돌린다. `ShouldUpdateTransform()`이 `bRecentlyRendered`만 보므로(`SkinnedMeshComponent.cpp:1617`), 메시가 일반 패스에서 안 그려지면(`WorldSpaceRepresentation` 태그 등) **평가(Evaluate)가 스킵**된다. Speed·bIsAiming 프로브는 전부 초록인데 뼈만 마지막 평가에 고정.

**해결**: `VisibilityBasedAnimTickOption = AlwaysTickPoseAndRefreshBones`.
**판정**: 변수 값이 아니라 **뼈 좌표의 시간 변화(span)** 로, 반드시 **움직이는 상태에서**.

### A3. 에디터에서 열면 멀쩡한데 재시작하면 T자로 돌아온다
**원인**: 헤드리스로 만든 **BlendSpace의 그리드(삼각분할)가 안 구워진 채 저장**됐다. 여는 순간 메모리에서만 구워진다.
**해결**: 축 Max를 바꿨다 되돌려 `PostEditChangeProperty`를 태우고 `save_asset`. 그냥 열기만 하면 dirty가 안 되어 Ctrl+S가 아무 일도 안 한다.
**확인**: 파일 크기가 커지는지로 본다(실측 13.9KB → 39.0KB). **헤드리스 저작 BlendSpace는 콜드 로드로 검증할 것.**

### A4. 임포트 중 "Skeleton Conflicts" 창이 떴다
**Done을 누르지 마라. 소스 파일이 틀렸다는 신호다.** 무시하고 눌렀다가 **Blu 스켈레톤이 108본 → 65본(마네킹)으로 통째로 교체**됐고, 541개 에셋이 그 스켈레톤을 참조하고 있었다.
**복구** = 에디터 닫고 `git checkout`.

### A5. 레이어를 무엇으로 바꿔도 레퍼런스 포즈가 나온다
**원인**: `Save Cached Pose` → `Use Cached Pose` 경유 가지가 평가되지 않는 경우가 있었다(캐시 쌍을 새로 만들어도 동일).
**해결**: 캐시를 걷어내고 `Linked Anim Layer`를 상태 안에 **직결**.
**참고**: `LinkedAnimLayer`는 **그래프 위치와 무관**하다(`AnimBlueprintGeneratedClass.cpp:498-511`) — "상태기계 안이라 안 붙는다"는 메커니즘은 엔진에 없으니 그 가설로 노드를 옮기지 말 것.

### A6. Two Bone IK를 걸었는데 아무 일도 안 일어난다
- **`ik_*` 본을 지정했을 가능성.** `ik_hand_l`의 부모는 `ik_hand_gun`이라 **팔 체인 밖**이고 스킨을 움직이지 않는다. 실제 체인은 `hand_l ← lowerarm_l ← upperarm_l ← clavicle_l`.
- **`Effector Location Space`가 기본값(`BCS_ComponentSpace`)인데 월드 좌표를 넣었을 가능성.** 엔진 기본값 확인: `AnimNode_TwoBoneIK.cpp:35`.
- **`Joint Target Location`이 (0,0,0)** = 관절 그 자체 → 폴 방향이 없어 팔꿈치가 뒤집힌다.
- **AnimBP 프리뷰에서는 IK가 원래 안 돈다** — 프리뷰 인스턴스엔 폰 오너가 없어 알파 소스가 0이다. 튜닝은 PIE에서.

### A7. "본 잔차 0/65"가 통과했는데 FBX가 제대로 들어갔는지는 **여전히 모른다**
**스켈레톤을 공유하는 동안 이 검증은 아무것도 증명하지 못한다.** `get_socket_transform`이 메시가 아니라 **스켈레톤의 ref pose**를 돌려주기 때문에, 소스 FBX가 어떻든 0/65가 나온다. 단위 100배 사고를 이 지표가 못 잡은 것도(F1-b) 같은 이유다.
**자체 스켈레톤이 되어서야 잴 수 있다.** 그때 재는 법 = ①`RefSkeleton` **로컬 회전**을 원본 스켈레톤과 UE 안에서 직접 비교(좌표계 변환을 안 거친다) ②rest 위치를 Blender가 뽑아 둔 스펙 JSON과 비교. **대조군(같은 메시 두 번 스폰)을 반드시 같이 잰다.**
실측(2026-08-04, `SK_NeonV_FPArms`): 회전 최대 **0.00089°** / 위치 최대 **0.00016cm** → **FBX 경로는 뼈를 보존한다**(glTF는 전 본에 90°를 넣었다 — A1-b).

### A8. 1프레임짜리 애니를 내보냈더니 임포터가 ensure를 뱉는다
`Ensure condition failed: TimeDescription.RangeStopSecond > TimeDescription.RangeStartSecond` (`FbxAnimation.cpp:1048`). **길이 0인 `AnimSequence`가 된다.** 임포트는 "성공"하고 포즈도 맞게 들어오지만, 애님그래프에서 블렌드·재생속도 계산이 0으로 나눈다.
**해결 = 같은 포즈를 두 프레임에 박아 내보낸다.** 정지 포즈인 건 그대로면서 길이를 갖는다(PWAS 견본도 2프레임). Blender: `frame_start=F, frame_end=F+1` + 두 프레임 모두 `keyframe_insert`.

### A9. 두 리그의 손을 겹쳐 쟀더니 9cm가 나온다 (실제로는 0이어야 하는데)
**뼈의 로컬 축으로 프레임을 맞췄기 때문이다.** Manny와 Blu는 뼈 로컬 축 규약이 전혀 다르다 — 실측 축 차이가 검지 7~29°, 새끼 54~78°, **엄지 100~107°**. 그 축으로 겹치면 손이 통째로 돌아간 채 얹힌다.
**해결 = 손 뼈 **머리들**로 최적 강체정합(Kabsch)을 푼다.** 축을 안 쓰므로 규약 차이에 면역이고, **그 정합 잔차 자체가 "손이 강체로 남았는가"의 답**이다. 실측: 자체 스켈레톤 0.0000cm / 옛 마네킹 컨폼 1.0973cm.

### A10. 뼈 로컬 좌표를 Blender ↔ UE로 옮길 때도 **Y 부호가 뒤집힌다**
월드 좌표뿐 아니라 **`hand_r` 로컬 오프셋 같은 뼈 로컬 값도** `(x, -y, z)`다. 실측: `middle_01_r`이 Blender `(-7.56, +1.40, 1.74)` / UE `(-7.56, **-1.40**, 1.74)` — X·Z는 소수점까지 같다.
**UE에서 잰 값(예: 옛 소켓 오프셋)을 Blender 축으로 분해하면 조용히 섞인다.** 소켓 값을 그렇게 냈다가 Y가 0.5cm 틀렸다. **계산은 한쪽 공간에서 끝내고**, 넘길 때만 변환하라. 검증법 = 같은 랜드마크를 양쪽에서 재서 `(x,-y,z)`로 맞는지 본다.

### A11. 리타게팅한 애니가 전부 **똑같은 정적 포즈**다 (에러도 경고도 없이)

**증상** — IK 리타게터로 뽑은 Idle/ADS/Reload 셋이 소수점까지 같은 값을 낸다. 서로 다른 두 애니의
**압축 DDC 해시까지 동일**하다. 시점을 바꿔 재도 값이 안 변한다. 그런데 어느 단계에서도 실패가 안 났다 —
리그 생성·op·매핑 호출·내보내기·압축까지 전부 "성공"으로 끝난다.

**원인** — UE 5.7 리타게터는 **op 스택**이고 **체인 매핑이 op 안에 산다.** 세 겹이었다:
1. `auto_map_chains()` 를 **op 이름 없이** 부르면 기본 `FK Chains` op 에 안 붙는다
2. op 이름을 주고 `set_source_chain()` 을 직접 걸어도 붙지 않는다 —
   `ChainMapping->HasChain(Target)` 이 false 면 조용히 `continue` (`IKRetargeterController.cpp:1022`)
3. 그 false 의 진짜 이유: **op 의 `IKRig Asset` 슬롯이 None.** op 이 타깃 리그를 모르니 체인 목록이
   0줄이고, 0줄이라 UI 의 `Auto-Map Chains` 버튼도 **회색으로 비활성**된다

**해결** — 에디터에서 `Op Stack > FK Chains` 선택 → `Details > Retarget Chains Settings > IKRig Asset` 에
타깃 IK Rig 지정 → 그러면 체인 목록이 채워지고 `Auto-Map Chains > Exact` 가 살아난다.
`GetTargetIKRigForOp` 는 **게터만** 노출돼 있어 이 슬롯은 스크립트로 못 채운다.

**판정** — `Scripts/lpamg_probe_mapping.py` 로 매핑을 되읽어 15/15 인지 본다. 빌드 스크립트
(`lpamg_build_retarget.py`)가 이걸 게이트로 걸어 아니면 `RTG_FAIL` 로 중단한다.

> 🪤 **트랙 개수·핵심 본 존재 검사로는 이 사고를 못 잡는다.** 실제로 트랙 79개가 다 있고 핵심 본
> 12개도 다 있는데 값만 정적이었다. `lpamg_verify_pose_values.py` 처럼 **값**을 봐야 한다. 그리고
> "리타게팅이 고장난 것"과 "측정 도구가 고장난 것"을 가르려면 **대조군**이 필요하다 —
> `lpamg_control_test.py` 는 원본 애니가 시점마다 변하는지 먼저 확인한다(G7·G8 참조).

### A12. 소켓을 수정해도 게임에 아무 변화가 없다 (양손 IK 그립, 2026-08-07)

**증상** — 무기 바디의 `SOCKET_LeftHand` 위치·회전을 어떻게 고쳐도 왼손 IK 타깃이 소수점까지 같은 값에
얼어 있다. PIE 재시작도 소용없다.

**원인 3겹** (같은 증상, 다른 진범 — 순서대로 소거할 것):
1. **같은 이름 소켓이 파츠에 하나 더 있었다.** 그립 해석은 "소켓을 실은 파츠가 리시버보다 우선"이라
   (`RebuildPartsFromSelection`), `SM_Wep_Mod_A_Handguard_03` 의 중복 `SOCKET_LeftHand` 가 이기고
   바디 소켓은 영원히 안 읽혔다. → 편집 무반영이면 **같은 이름 소켓을 전 파츠에서 grep 부터**.
2. **그립 캐시는 장착 시점에만 재계산된다** — PIE 켠 채 편집하면 다음 PIE 까지 무반영.
   → 에디터 빌드 한정 자동 재부착 씸 추가로 해소(`AFPSRCharacter::OnEditorObjectPropertyChanged`).
3. **타깃이 팔 리치 밖이면** 완전히 뻗은 팔이 리치 구면에 고정돼, 소켓을 옮겨도 방향 몇 도만 변해
   "무변화"로 보인다. → 어깨→타깃 거리 vs 팔 체인 길이를 **숫자로** 먼저 잴 것.

**판정** — 애님 인스턴스 게시값(`LeftGripInGunLocation`)이 소켓 편집을 따라 변하는지, 그리고
`hand_l→ik_hand_l` 잔여 거리로 도달 여부를 본다. 좌우가 한쪽만 이상하면 정상 쪽이 대조군이다.

### A13. 왼손목이 정확히 반대로 꺾인다 (총-기준 IK 회전)

**증상** — 오른손목은 `Take Rotation From Effector Space` 로 완벽한데, 같은 배선의 왼손목만 뒤집힌다.

**원인** — 그립 회전의 기준 프레임 `ik_hand_gun` 은 **hand_r(오른손)의 복사본**이다. "그립 회전 = 항등"은
오른손엔 "애니 저작 방향 유지"지만, 왼손엔 **오른손의 방향을 입히는 것** — 거울상 손이라 정반대로 보인다.

**해결** — 왼손 목표 회전은 클립에서 **애니 왼손(hand_l)의 방향을 따로 추출**해 소켓 회전으로 역산한다
(합성 순서상 소켓 회전이 마지막 인자라, "소켓 회전 = 게시 회전의 역"이 정확한 무변화 값이다).
수치 검산이 되므로 손 유도로 쿼터니언 순서를 뒤집다 부호 함정에 빠지지 말 것.

### A14. 시퀀서 FK 컨트롤 릭이 2프레임 포즈 클립에서 A포즈만 낸다

**증상** — `Edit in Sequencer → Bake To Control Rig` 후 컨트롤 값이 전부 0, 프레임 0에서도 A포즈.
플레이헤드가 `0001*`(프레임 사이)로 시작하는 것도 특징.

**원인** — 0.033초·2프레임 클립이 이 워크플로의 엣지 케이스를 밟는다(베이크가 키를 못 싣는다).

**우회 2가지** — ① 스크립트 저작: `AnimSequencerController.set_bone_track_keys` 로 직접 키를 쓴다
(`Scripts/tune_left_thumb.py` 가 이 방식 — 회전만 바꾸고 이동 키는 원본 보존).
② 기즈모가 필요하면 시퀀서 말고 **Persona 의 Set Key**(`S`, "Add Bone Transform to Additive Layer
Tracks", `AnimationEditorCommands.cpp:20`) — 애디티브 레이어 트랙이라 2프레임 무관.
🚨 ①과 ② 는 **합산**된다(레이어가 원본 위에 얹힘) — 한 본에는 한 방식만 쓸 것.

### A15. 3인칭 바디가 어떤 머신에서도 전혀 안 움직인다 (멀티에서 발견, 2026-08-07)

**증상** — PIE 2인에서 상대방 캐릭터가 이동해도 포즈가 고정. 위치는 정확히 따라오는데(복제 정상)
애님 인스턴스 변수가 전부 0/스테일(`Speed=0`, `bIsDowned=True` 잔존). 레이어 링크·ABP 배선은 전부 정상.

**원인** — `BP_FPSRPlayer` Mesh(바디) 컴포넌트 기본값에 **`bPauseAnims=True`가 저장**돼 있었다.
`839abef0`(팔 뼈 정합 세션)에서 뷰포트 비교를 위해 바디를 얼려둔 체크가 그대로 커밋돼 6커밋을 잠복.
1인칭에선 자기 바디가 안 보여서(WorldSpaceRepresentation) 아무도 몰랐고, 멀티에서 처음 드러났다.

**판정법** — "애니가 안 움직인다"는 신고에서 그래프를 의심하기 전에 **`mesh.bPauseAnims`부터 읽어라**
(스냅샷: 애님 변수 전부 0 + 위치 복제는 정상이면 그래프가 아니라 업데이트 정지다). 라이브 편집
세션 뒤에는 BP 컴포넌트 기본값 diff 를 의심할 것 — [[dirty-flag-cleared-means-saved-not-reverted]] 계열 사고.

### A16. 애디티브 클립에서 특정 뼈만 "움직이지 않게" 만들기 (총 고정 장전, 2026-08-07)

**증상** — 총-앵커 구조에서 장전 몽타주를 틀면 총이 통째로 이동(실측 23~31cm).
CopyBone(hand_r→ik_hand_gun)이 클립의 오른팔 모션을 총에 그대로 실어 나른다.

**함정 2개** (둘 다 실측으로 걸러냄):
1. **트랙 삭제 ≠ 델타 0.** 트랙이 없으면 그 뼈의 클립 포즈가 스켈레톤 refpose로 떨어져
   델타 = refpose − base 라는 **큰 상수 오염**이 들어간다(hand_r 회전 32°→82°로 악화).
2. **베이스 시퀀스를 AnimPose로 직접 샘플한 값도 틀리다** — 런타임 애디티브 추출과
   리타게팅 경로가 달라 몇 cm/수십 도 어긋난다.

**정답** — 같은 클립의 **delta와 raw를 같은 평가기(AnimPose)로 뽑아 base를 역산**한다:
`base_rot = delta_rot⁻¹ * raw_rot`, `base_t = raw_t − delta_t`
(엔진 `FAnimationRuntime::ConvertTransformToAdditive` 정의 대조). 이 base를 해당 뼈들의
**상수 키**로 다시 쓰면 델타가 정확히 0 — 판정은 AnimPose 델타 재계측(0.000cm/0.00°) +
PIE 대조군(원본 클립 드리프트 재현). 사례 = `FP_Rifle_Reload_GunLocked`(`dc4b13bd`).
🚨 몽타주의 `SlotAnimTracks`는 파이썬 보호 속성 — 세그먼트 교체는 **동일 경로 재생성**으로
(DA 소프트 참조는 경로만 같으면 유지된다).

### A17. 몽타주 재생에서 IK 해제 커브가 조용히 무시된다 (2026-08-09)

**증상** — 장전 시 왼손이 탄창으로 안 내려가고 총에 붙은 채 총만 따라 움직인다. 클립을 재보면
왼팔 트랙·커브 다 멀쩡하다.

**원인** — 커브(`LeftHandIKWeight`)를 몽타주에서 클립으로 옮겼더니(미러 재생용), **정식 몽타주
재생 경로는 세그먼트 클립의 커브를 `GetCurveValue` 까지 전달하지 않는다**(실측). IK 알파가
기본 1로 남아 왼손을 총-공간 그립에 도로 붙잡았다 — "애니가 사라졌다"가 아니라 "IK 가 덮었다".

**해결/규약** — 커브는 **양쪽에 있어야 한다**: 몽타주(정식 재생용) + 클립(슬롯 다이내믹 몽타주
미러용). 둘 다 상수 0 유지 — 값이 갈라지면 두 경로가 다르게 논다(드리프트 주의, 커브를 모양
있게 저작하게 되면 한쪽을 소스로 정하고 다른 쪽은 복사 스크립트로).

### A18. 1인칭 조준각과 3인칭 프록시 시선각이 **상수로** 어긋난다 (2026-08-08)

**증상** — 1인칭이 수평을 조준하는데 다른 화면의 3인칭 프록시는 총·머리가 ~20° 아래를 본다.
반대로 프록시가 수평으로 보이려면 1인칭이 그만큼 위를 봐야 한다. 각도 차이가 조준 각도와 무관하게 일정하다.

**원인 판별 — "상수 오프셋"이면 복제 유죄가 아니다.** RemoteViewPitch(16bit)·서버 카메라 캐시
(ServerUpdateCamera RPC) 경로는 지연·계단은 만들어도 상수 오프셋은 못 만든다. 이 건은 코드·배선·
AO 설정(mesh-space additive, 기준=Center) 전부 정상이었고, **AO 샘플 패밀리 전체(센터 포함)가
카메라 수평보다 아래를 보도록 저작**된 것이 원인. `AnimPose`로 잰 샘플 간 상대 회전은 라벨 그대로
45.0°/89.9° — 즉 패밀리 내부는 정확하고 **절대 기준만 통째로 시프트**돼 있었다.

**계측** — `FPSR.Debug.AimSync 1`(캐릭터별 pitch 소스 분해 readout, 2=로그 덤프). 수치가 송신측과
같은데 각도만 다르면 콘텐츠, 수치가 다르면 복제/코드.

**해결** — 재저작 대신 **그래프 사후 보정**: `ABL_Blu_W2_Rifle`의 AO 적용(Apply Mesh Space Additive)
뒤에 `Transform (Modify) Bone` — Bone `chest`, Rotation Mode `Add to Existing`, Space `Component`,
**Roll −20**(PIE 2인 캘리브레이션 확정값). TwoWayBlend(서기/앉기) 뒤라 한 노드로 두 스탠스 커버,
걷기/조깅 베이스도 같은 시프트 계열이라 함께 보정된다. 새 무기 세트 레이어에도 같은 패턴 복제.

**함정 3개**
- **TMB Rotation Mode 기본값 = `Replace Existing`** — 노드를 넣는 순간 (0,0,0)으로 교체돼 상체가 90°
  꺾인다. `Add to Existing`으로 바꾸고 (0,0,0)에서 포즈 불변인지 먼저 확인.
- 이 릭은 **Blender식 네이밍**: `spine_03`이 없고 `root>hips>spine>chest>neck>head` (척추 2개뿐).
- 이 릭의 상하 조준축 = **컴포넌트 X축 회전 = rotator 표기상 Roll**이다(Pitch 필드가 아님).

### A19. 몽타주가 **다른 스켈레톤**을 가리켜도 UE는 아무 말 없이 재생한다 (2026-08-10)

**증상** — 1인칭 장전 애니가 "움직이긴 하는데 이상하다"(팔이 엉뚱한 방향으로 꺾임). 클립·몽타주·커브는
다 멀쩡하고 에러도 경고도 없다.

**원인** — `DA_Weapon_Rifle.ArmsReloadMontage`가 **`S_Mannequin`(161본)** 몽타주를 가리키는데 실제 팔은
**`SKEL_LPAMG_Character`**다. 양쪽 `CompatibleSkeletons`가 비어 있어 리매핑이 없고, 본 인덱스가 어긋난다
(`clavicle_l` 5↔10 · `hand_r` 29↔66 · `ik_hand_gun` 65↔153). 본 **이름**은 겹쳐서 "맞는 것처럼" 보인다.

🚨 **UE 5.7 `Montage_PlayInternal`은 호환성을 검사하지 않는다** — `if (CurrentSkeleton && MontageToPlay->GetSkeleton())`
로 **널만** 본다(`AnimInstance.cpp:2396`). else 절의 `"Playing a Montage (%s) for the wrong Skeleton"`
경고문(`:2460`)은 **널일 때만** 뜨므로, 스켈레톤이 달라도 그 경고는 영영 안 나온다. 즉 이 사고는
로그에 아무 흔적을 남기지 않는다.

**판정법** — 애니를 의심하기 전에 **몽타주의 스켈레톤과 재생 대상 메시의 스켈레톤을 직접 대조**하라.
`m.get_editor_property("skeleton").get_name()` vs `comp.get_skeletal_mesh_asset().get_editor_property("skeleton")`.
보조 단서: 해당 몽타주가 **에셋 참조 0건**인데 DA는 다른 걸 물고 있다(`get_referencers`로 확인).
⚠️ `skeletal_mesh` 속성은 폐기됐다 — `get_skeletal_mesh_asset()`을 쓸 것([[ue-deprecated-property-stale-read]] 계열).

**해결** — DA가 팔과 **같은 스켈레톤**의 몽타주를 가리키게 고친다. 여기선 마켓 데모 폴더
(`/Game/ProceduralWeaponAnimationSystem/`)의 동명이인 에셋(`AM_FP_RifleReload`)이 프로젝트 자체 에셋
(`AM_FP_Rifle_Reload` — **언더바 위치만 다름**)을 밀어내고 있었다. 이름이 아니라 **경로로** 고를 것.

### B1. 카메라 근처의 메시가 잘려 보인다
**근평면은 10cm가 아니라 1.0cm다.** `Config/DefaultEngine.ini`의 `[/Script/Engine.Engine] NearClipPlane=1.0`을 엔진이 `UEngine::Init` → `SetNearClipPlaneGlobals`(`UnrealEngine.cpp:2221`)에서 읽는다. `CoreGlobals.cpp:260`의 `GNearClippingPlane = 10.0f`는 **config를 읽기 전 초기값**이다.
→ 몇 cm짜리 잘림은 근평면이 아니라 **지오메트리가 카메라 평면을 관통**하는 것이다. 근평면을 더 낮춰봐야 헛수고.

### B2. 1인칭 프리미티브의 그림자가 엉뚱한 곳에 생긴다
`EFirstPersonPrimitiveType::FirstPerson` 태그는 **부착 체인을 따라 내려가지 않는다.** 무기를 태그해도 그 자식인 모듈러 파츠는 안 된다 → 파츠에도 따로 걸어야 한다(생성 시점 + 상태 전환 시점 양쪽).
태그가 붙으면 엔진이 `bCastDynamicShadow/Static/Volumetric/Far`를 **전부 강제 off** 한다(`PrimitiveSceneProxy.cpp`).

### B3. 본을 숨겼는데 화면에 그대로 있다
`hide_bone_by_name` **직후 같은 프레임에 렌더하면 갱신 전 화면**이 나온다. 한 프레임 지나서 확인할 것.

### B4. 1인칭 FOV/스케일을 켜면 조준이 어긋난다
`bEnableFirstPersonFieldOfView` / `bEnableFirstPersonScale`은 **둘 다 엔진 기본 false**다. 켜면 1인칭 패스가 다른 FOV·스케일로 렌더돼서 **사이트가 "렌더되는 곳"과 코드가 "월드에 놓는 곳"이 갈라진다.** 이 프로젝트는 조준점을 화면 중앙에 못 박으므로, 켜려면 ADS 정렬을 다시 재야 한다.

---

## C. 빌드 · 에디터 기동

### C1. 에디터를 켜둔 채 빌드했다
**"실패한다"가 아니라 "성공해 버리고 에디터를 죽인다."** 미저장 작업이 날아간다.
빌드 전에 프로세스를 실제로 확인할 것:
```powershell
Get-Process | Where-Object { $_.ProcessName -match 'UnrealEditor' }
```

### C2. `Unable to build while Live Coding is active`
**다른 클론**(FPSRoguelite2 등)의 에디터가 같은 엔진을 공유해 이 트리 빌드까지 막는다. 그쪽 에디터를 닫을 것.

### C3. 빌드가 간헐적으로 C1076(힙 고갈)·초장시간
XGE executor 문제다. `-NoXGE`로 재빌드하면 결정적으로 돈다.

### C4. 커맨드렛이 exit 3으로 죽는다
**작업은 끝나 있는 경우가 많다.** 임포트/저장 완료 후 콘텐츠 브라우저가 결과를 UI에 표시하려다 `Assertion failed: CurrentApplication.IsValid()`(Slate 없음)로 죽는다.
→ **종료 코드가 아니라 로그의 완료 마커**(`ALLDONE`/`ALLPASS` 등)로 판정할 것.

### C5. 에디터가 "no writable DDC nodes"로 크래시
Riot Client가 **8558 포트**를 점유해 ZenServer가 못 뜬 것. Riot을 끄고 재시도.

---

## D. VibeUE · 에디터 Python

### D1. 에디터가 즉사했다 (로그가 예외 없이 끊김)
- **레벨 전환 API 호출** — `LevelEditorSubsystem.new_level()` / `load_level()`은 **에디터 즉사**다(2회 실증). 임시 레벨을 만들지 말고 **이미 열린 레벨에 스폰하고 끝나면 지운다.**
- **`create_node_by_key`로 애님 노드 생성** — `Interface=None, Layer=""`인 빈 껍데기가 다음 틱에 하드 크래시. 더 나쁜 건 **저장한 적 없는 그 노드가 디스크에 남는다.** 애님 노드는 `AnimGraphService`의 타입별 `add_*`만 쓸 것.
- **컨테이너 위젯(자식 WBP 임베드)을 프로그래매틱으로 compile/save** — 모달 행·크래시·저장 중단 시 `.uasset` 손상.

### D1-a. 켜져 있는 에디터에 Python으로 FBX를 임포트했더니 통째로 굳었다
**`InterchangeManager.import_asset`을 라이브 에디터에서 부르지 마라.** MCP/Python 호출은 **게임 스레드에서** 실행되는데, Interchange 임포트도 게임 스레드를 요구해 그대로 데드락이 걸린다. 실사고(2026-08-04): FBX 번역(0.067초)까지 로그를 남기고 `LogOutputDevice: Warning: Script Stack (1 frames) : /Script/InterchangeEngine.InterchangeManager.ImportAsset` 에서 멈춤 → **CPU 완전 정지 · 응답 없음 · 뜬 대화상자 없음 · 강제 종료 외 방법 없음.**

**진단 3종**(에디터가 죽었으니 전부 에디터 **밖에서**):
- `Get-Process UnrealEditor` 의 **CPU를 두 번 재라.** 안 오르면 일하는 게 아니라 멈춘 것
- `Saved/Logs/*.log` 를 **파일로 직접** tail — 어디서 멈췄는지 나온다
- 숨은 모달인지 확인: `EnumWindows`로 그 PID의 보이는 창 목록. 창이 메인+에셋에디터뿐이면 누를 게 없다는 뜻

**대신**: 임포트는 **커맨드렛(별도 프로세스)** 에서 한다 — `UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=...`. 앞 세션이 같은 API로 성공한 것도 전부 커맨드렛 안이었다. 스켈레톤을 대상으로 하는 임포트라면 커맨드렛도 쓰지 말 것(A4 — "Skeleton Conflicts" 창이 없어 조용히 덮어쓴다). **에디터에서 사람이 한다.**

> 🪤 이 사고의 진짜 교훈: 그 임포트는 **대조군을 만들려던 것이고, 없어도 됐다.** 에셋의 `asset_import_data.get_first_filename()` 이 이미 새 소스를 가리키고 있어 임포트 여부는 확정이었다. **검증을 한 겹 더 쌓기 전에 이미 가진 증거로 충분한지부터 보라.**

### D1-b. 커맨드렛에서 에셋을 임포트하면 끝나자마자 죽는다
`AssetTools.import_asset_tasks`는 임포트를 **완료한 뒤** 콘텐츠 브라우저를 동기화하는데, 커맨드렛엔 Slate가 없어 거기서 죽는다 — 콜스택이 `AssetTools → ContentBrowser → Slate`, `Assertion failed: CurrentApplication.IsValid()`.
→ **`InterchangeManager.get_interchange_manager_scripted().import_asset(dest, source_data, params)`** 를 직접 쓰면 그 경로를 안 탄다(실증: 같은 GLB가 크래시 없이 들어옴).

### D2. `execute_python_code`가 타임아웃 났다
**30초 타임아웃**이다. 툴은 에러를 주지만 **에디터 쪽 작업은 계속 진행돼 결과가 남는다.**
→ 재시도 전에 **반드시 현재 상태를 조회**할 것. 배치는 30초 안에 끝나는 크기로 쪼개고, **이미 만들어진 건 건너뛰게** 짜면 크래시·타임아웃에도 이어서 갈 수 있다.

### D3. 본 트랜스폼이 전부 (0,0,0)으로 나온다
**등록되지 않은 컴포넌트**(`unreal.SkeletalMeshComponent()`를 그냥 만든 것)는 본 이름·부모는 주지만 **트랜스폼은 전부 0**이다. 위치가 필요하면 액터를 스폰해야 한다(A1 레시피).

### D4. PIE 중에 그래프 조회가 안 된다
**PIE 중에는 `AnimGraphService`·`BlueprintService`가 죽는다**(`Failed to load AnimBlueprint`). 그래프 조회는 PIE 끄고.
반대로 **애님 문제 진단은 PIE 중인 AnimInstance를 직접 읽는 게 빠르다** — `UnrealEditorSubsystem.get_game_world()` → `GameplayStatics.get_all_actors_of_class` → `mesh.get_anim_instance()`.

### D5. 자동 저장이 안 되고 있다
로그에 `Skipping auto-save: previous Python execution crashed`가 뜨면 그 세션 내내 꺼져 있다. **매 단계 `save_asset`을 명시 호출**할 것.

### D6. 회전값을 넣었는데 축이 다르다
`unreal.Rotator(a, b, c)` = **(roll, pitch, yaw)** 순이다.

---

### D7. 커맨드렛이 콜스택만 남기고 즉사한다 — **에디터 UI를 건드리는 API**

파이썬 예외가 아니라 **프로세스 종료**라 `try/except` 로 못 막는다. 부르기 **전에** 갈라야 한다.
이 프로젝트에서 실제로 당한 둘:

| API | 증상 | 이유 |
|---|---|---|
| `LevelEditorSubsystem.is_in_play_in_editor()` | `EXCEPTION_ACCESS_VIOLATION`, 콜스택 최상단 `UnrealEditor-LevelEditor.dll` | 구현이 `GUnrealEd->IsPlayingSessionInEditor()`(`LevelEditorSubsystem.cpp:284`). 커맨드렛엔 레벨 에디터가 없다 |
| `IKRetargetBatchOperation.duplicate_and_retarget()` | `Assertion failed: CurrentApplication.IsValid()` (`SlateApplication.h:321`) | 진행 대화상자가 Slate 를 요구한다. **계산은 끝나고 압축까지 갔는데 저장 직전에 죽어** 결과가 안 남는다 |

**해결** — ①커맨드렛인지 먼저 가른다: `"-run=" in unreal.SystemLibrary.get_command_line().lower()`
(커맨드렛이면 PIE 가 있을 수 없으므로 검사 자체가 불필요) ②Slate 가 필요한 것은 **정식 에디터**로 돌린다:
`UnrealEditor.exe <uproject> -ExecutePythonScript="<파일>" -nosplash -unattended`

### D8. 파이썬 커맨드렛에 인자가 안 들어온다

`-run=pythonscript -script="x.py" -- apply` 로 줘도 `sys.argv` 에 안 온다 — `-script=` 는 경로만 읽는다.
**엔진 커맨드라인에서 직접 읽어라**: `unreal.SystemLibrary.get_command_line()` 에 스위치가 그대로 있다.

### D9. 파이썬에 없는 API 셋 (실측)

| 기대한 것 | 실제 |
|---|---|
| `SkeletalMeshComponent.get_bone_names()` | 없다. `get_num_bones()` + `get_bone_name(i)` (`SkinnedMeshComponent.h:1066/1087`) |
| `SkeletalMeshComponent.tick_animation()` | 없다. 포즈 샘플링은 `AnimationLibrary.get_bone_pose_for_time()` 으로 부모 체인을 곱해라 |
| `AnimBlueprint.get_editor_property("parent_class")` | 없다. `generated_class()` 에서 올라가라 |
| `IKRigController.get_retarget_chain_names()` | 없다. `get_retarget_chains()` (`IKRigController.h:346`) |

> 🪤 조회 API 이름을 틀린 채 `try/except` 로 감싸면 **"없다"와 "못 읽었다"가 구분되지 않는다.**
> 실제로 체인이 15개 멀쩡히 있는데 0개로 오판해 엉뚱한 곳을 팠다.

## E. 데이터 · 컴포넌트 · BP

### E1. 컴포넌트 슬롯에 넣은 값이 런타임에 다른 것으로 바뀐다
**코드가 컴포넌트를 덮어쓰고 있다.** 데이터 필드와 컴포넌트 슬롯이 같은 것을 가리키면 **슬롯이 거짓말이 된다** — 디자이너가 넣고, 뷰포트도 맞게 보이고, 런타임만 다르다.

**사고**: 컴포넌트에는 `NeonV_FPArms`, 데이터 필드에는 폐기 대상 `Blu_FP_Arms`(다른 스켈레톤)가 들어가 있었다. 그대로 PIE를 눌렀으면 포즈가 하나도 안 붙는 팔이 나왔을 것이다.

**규약(사용자 지침 2026-08-04)**: **컴포넌트로 넣을 수 있는 건 컴포넌트 슬롯이 진실원천.**
config/DataAsset은 **슬롯이 비었을 때만** 채우는 폴백으로 둔다 — `AFPSREnemyBase`·`AFPSRXPPickup`·`AFPSRBossBase`가 `if (GetStaticMesh() == nullptr)`로 이미 그렇게 되어 있다.
(런타임에 실제로 교체되는 것 — 장착 무기 메시 같은 것 — 은 예외다. 그건 데이터가 소스인 게 맞다.)

### E2. C++ 생성자에서 읽은 값이 BP와 다르다
**BP 서브클래스의 오버라이드는 C++ 생성자 이후에 역직렬화된다.** 생성자에서 읽으면 항상 C++ 기본값이다.
→ BP 값이 필요하면 `BeginPlay`에서 다시 읽는다(리포 선례: `BaseWalkSpeed`를 `BeginPlay`에서 다시 밀어넣는 이유).
같은 함정을 캡슐 반높이·`BaseEyeHeight`에서 한 번 더 겪었다(C++ 88/64 ↔ BP 81/69.7).

### E3. 값을 PIE 인스턴스에서 바꿨더니 서버·클라가 어긋난다
사격 원점처럼 **각 머신이 자기 복사본에서 계산하는 값**은 **BP 클래스 기본값**에서 튜닝해야 한다. PIE 인스턴스만 바꾸면 서로 다른 값을 본다.

### E4. `IsDataValid`/검증기가 struct 멤버 쓰기를 거부한다
`EditDefaultsOnly` 멤버를 가진 struct를 파이썬이 "인스턴스"로 보고 `DisableEditOnInstance` 필드 쓰기를 거부한다. 멤버만 `EditAnywhere`로 두면 된다(컨테이너가 `EditDefaultsOnly`면 편집 가능 범위는 그대로).

---

## F. 단위 · 좌표계

### F1. Blender 1cm ≠ UE 1cm
리그는 Blender에서 **키 184.0cm**인데 UE는 `CharacterMesh0`를 **0.8806배**로 넣어 162cm를 만든다.
**게임 값을 Blender로 옮길 땐 0.8806으로 나눈다.** 이걸 놓쳐 벽을 34cm(정답 38.6)에, 1인칭 눈높이를 150.7cm(정답 171.1)에 놓는 실수를 했다.

### F1-b. FBX로 넣은 메시가 UE에서 100배 작다 → 아마추어 스케일로 실린 단위 변환이 버려진 것
**Blender는 미터, UE는 센티미터인데, UE는 그 ×100 변환을 지오메트리가 아니라 "아마추어 스케일"로 얹는다.**
- **새 스켈레톤을 만드는 임포트**: 본이 scale 100으로 들어온다. rest는 멀쩡해 보이고 **scale-1 애니가 재생되는 순간** 메시가 무너지고 꼬인다
- **기존 스켈레톤에 붙이는 임포트**(우리 1인칭 팔): 본은 그 스켈레톤 ref pose를 쓰므로 **그 아마추어 스케일이 통째로 버려지고**, 지오메트리만 미터 값으로 남아 **메시가 100배 작게** 들어간다

**🪤 이 경우 본 단위 잔차 검증이 통과한다.** 스켈레톤을 공유하면 `get_socket_transform`은 메시가 아니라 **스켈레톤의 ref pose**를 돌려주므로, 메시가 어떻든 0/65가 나온다. 구조적으로 못 잡는다.

**증상 묶음** — 팔이 점만 하게 보임 · BP에서 스케일 100을 줘야 맞음 · `Approx Size`가 4,445×5,080×8,438(정상 116×42×64) · 정점/삼각형 수는 정확 · 메시 에디터에서 모양은 멀쩡(뷰포트에 크기 기준이 없다).

**판정**: 메시 에디터 뷰포트 통계의 **`Approx Size`** 하나면 된다. 기준 메시와 자릿수가 다르면 이것이다.

**고치는 법**(블랜더 repo 커밋 `e8f329a`에서 이미 검증된 우회법): 지오메트리(본 head/tail + 정점)에 ×100을 굽고 **최상위 오브젝트 스케일을 0.01**로 둔 뒤 `apply_scale_options="FBX_SCALE_NONE"`으로 내보낸다. Blender에서는 둘이 상쇄돼 보이는 게 그대로고, 임포트 때 UE의 ×100이 0.01을 상쇄해 아마추어 스케일 1 + 이미 cm인 지오메트리가 된다. `FBX_SCALE_ALL`도 단순 ×100 굽기도 실패했다(전자는 안 먹고 후자는 이중 스케일).
- **0.01은 부모에만** 준다. 메시가 아마추어의 자식이면 같이 주면 0.0001로 이중 적용된다
- 스크립트 = 블랜더 repo `NeonV_scripts/fp_arms_export_fbx.py`. 굽기 전후 월드 좌표가 안 변하는지 자체 검사가 들어 있다
- **왕복 확인 지표 = 아마추어 노드 스케일**. 고치기 전 1.0 → 고친 뒤 0.01이면 제대로 실린 것이다(메시 크기는 양쪽 다 같게 보이므로 크기로는 못 가른다)

### F2. 게임 월드 값과 스켈레톤 값을 섞지 말 것
ADR 0002의 *"head 본은 발에서 155.1cm"* 는 **스케일 적용 전 스켈레톤 값**이라 Blender와 그냥 일치한다. 그래서 F1의 스케일 불일치가 안 잡혔다.

### F3. 스켈레탈 메시 소켓의 `relative_location`은 컴포넌트 좌표가 아니다
**부모 본 기준**이다. 좌표계를 추론하지 말고 **액터를 실제로 스폰해 월드 좌표를 읽는 편이 빠르고 옳다.**

---

## G. 검증 방법 자체의 함정

> 이 절이 가장 비싸다. **틀린 검증은 결함을 통과시키고, 통과했다는 기록까지 남긴다.**

### G1. 바운드가 일치해도 rest pose가 맞는 게 아니다
**바운드는 미러에 대해 불변이다.** Y를 통째로 뒤집어도 extent는 그대로다.
A1의 Y축 반전이 2026-07-26부터 잠복할 수 있었던 이유가 정확히 이것이다 — 앞 세션이 *"바운드가 거의 일치하므로 rest pose가 뼈 단위로 맞춰져 있다"* 로 결론냈다.
→ **rest pose 검증은 본 단위로. 그리고 대조군(같은 메시 두 번)을 같이 재서 0이 나오는지 먼저 확인**한다. 안 하면 계측 오류를 결함으로 오독한다.

### G2. 빈 목록은 "비었다"의 증거가 아니다
`get_used_anim_sequences`가 `[]`를 돌려준 것을 "애니가 하나도 없다"로 읽어 오진했다. 그 함수는 **AnimSequence만** 세는데 살아 있는 경로의 포즈 소스는 전부 **BlendSpace**였다.
같은 형태로 `get_animation_track_names`도 **애니되는 본을 다 나열하지 않는다**(`neck`·`spine`·`head`가 목록에 없는데 움직였다).

### G3. 정지 포즈로 잰 마진은 애니메이션의 마진이 아니다
무기 스케일 85%를 애니 4개의 **프레임 0**으로 재서 통과시켰는데, 186개를 전 프레임으로 재니 **54개가 음수**(최악 −3.11cm)였다.
→ 표본 4개가 아니라 전량을, 프레임 0이 아니라 전 프레임을.

### G4. 숫자만으로 애니 완료를 판정하지 말 것
8포즈 중 4개가 깨진 채 UE로 넘어갔고, **바닥면을 넣은 렌더 한 장**이 넷 다 잡아냈다.
포즈 참조 JSON을 뜰 때는 **그 프레임이 멀쩡한지 먼저 재라** — 공중에 뜬 프레임에서 캡처한 참조가 멀쩡한 클립 3개를 오염시켰다.

### G5. 벡터를 "수평으로 눕히는" 연산은 그 벡터가 수직일 때 방향이 노이즈다
발 접지 교정에서 오른발이 뒤를 향했다. 원인은 원본이 거의 수직(dz −33.0)이라 수평 성분이 **+5.4cm 노이즈뿐**이었는데 그걸 발 길이만큼 증폭한 것.
→ 크기만 보지 말고 **방향의 신뢰도를 게이트**할 것.

### G6. 문서를 출발점으로만 쓸 것
한 세션에 문서를 믿었다가 세 번 틀렸다 — P3 "상태기계 가설"(엔진 소스가 부정) · "3P 애니가 PWAS를 대체한다"(실플레이가 부정) · "1인칭 팔을 새로 저작해야 한다"(이미 있었다). 전부 **실제 파일을 열어보니 다른 얘기**였다.

### G7. 결과가 안 변할 때 **대조군 없이** 원인을 단정하지 말 것
리타게팅한 재장전이 시점마다 값이 안 변했다. "리타게팅이 고장났다"로 바로 갈 뻔했는데,
**원본 재장전을 같은 도구로 먼저 재보니**(t=0 −11.32 → t=1.0 −9.64) 도구는 멀쩡했다.
그제서야 결과가 정적인 게 진짜라고 단정할 수 있었다.
→ "안 변한다"는 관측은 **측정 도구가 고장난 경우와 구분되지 않는다.** 변해야 하는 것을
먼저 재라. `Scripts/lpamg_control_test.py` 가 그 대조군이다.

### G8. "있다" 검사는 "맞다" 검사가 아니다
리타게팅 결과를 트랙 개수(79개)·핵심 본 존재(12/12)로 검증하고 통과시켰다.
**전부 있는데 값만 정적이었다.** 존재 검사는 이 사고를 구조적으로 못 잡는다 —
값을 읽어 **서로 달라야 할 것이 실제로 다른지** 봐야 한다(Idle ≠ ADS ≠ Reload).

### G9. `get_actor_bounds` 로 지면/층을 판정하지 말 것 (2026-08-12, 맵1)
`L_Map1_City` 지면을 훑으면서 `get_actor_bounds(...).z + extent.z`(액터 바운즈 상단)로
"지면 타일 vs 고가 구조물"을 갈랐다. 결론 = *"Y 한 행이 통째로 뚫린 45m 협곡이라 지면이
남북으로 분단됐고 플로우필드 연결성이 깨진다"* → **전부 거짓.** 바닥 타일 11칸을 없는 구멍에
새로 깔았고, 에디터 오토세이브가 그걸 `.umap` 에 써버렸다(264,696 → 290,778 바이트).

**원인**: `get_actor_bounds` 는 **액터의 모든 컴포넌트를 합친** AABB다. `BP_road_02_C` 는
도로 메시(상단 Z=-50)에 **가로등이 붙어 있어** 액터 바운즈 상단이 607이 된다. `BP_building_C`
안에도 `SM_floor`(상단 Z=-50)가 들어 있다. 즉 "액터가 높다"와 "그 액터의 바닥면이 높다"는
전혀 다른 명제인데 전자로 후자를 판정했다.

→ **지면 판정은 컴포넌트 단위로.** `comp.get_world_location().z` + 그 컴포넌트 **메시의**
`get_bounds()` 로 그 면의 상단을 직접 계산할 것. 액터 바운즈는 "이 액터가 차지하는 부피"이지
"밟을 수 있는 면"이 아니다.

→ 그리고 **11칸이 동시에 비었다는 결과 자체가 신호였다.** 데모 레벨이 지면 한 행을 통째로
빼놓을 이유가 없다. 규칙적인 데이터에서 큰 덩어리 이상이 나오면 **대상이 아니라 판정식을
먼저 의심**할 것(G1과 같은 실패형).

→ 🪤 **에디터 오토세이브 때문에 "저장 안 했으니 안전"이 성립하지 않는다.** 레벨을 스크립트로
건드리기 전에 `git status` 기준선을 잡아두고, 되돌릴 땐 파일 복구(`git checkout -- <umap>`)
**직후 에디터에서 그 레벨을 변경사항 버리고 다시 열어야** 한다 — 안 그러면 메모리에 남은
액터가 다음 저장 때 되살아난다.
