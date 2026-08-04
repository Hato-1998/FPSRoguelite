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
본 단위 잔차가 **0/65인데도** 애니에서 메시가 조각나면 남은 건 **정점이 어느 본에 물렸는가**다. 실사고: `Jacket_FPArm`(소매 1296정점)이 **뼈 6개**에만 강체 바인딩되어 팔을 굽히는 순간 판때기로 찢어졌고, `pinky_02/03`은 무가중이라 새끼손가락이 사라져 보였다.

**가르는 법 = A/B.** 같은 애님그래프의 프리뷰 메시를 **정상 메시**(여기선 `SK_FP_Manny_Simple`)로 바꿔 본다. 그쪽이 멀쩡하면 메시 유죄, 같이 깨지면 그래프/포즈 유죄. 10초면 갈린다.

**감사 방법**(Blender, 읽기 전용): 메시별로 `vertex_groups`를 돌며 **본별 물린 정점 수**를 찍는다. 물린 본이 한 자릿수면 강체 바인딩이고, 특정 말단 본이 0이면 그 부위가 안 따라간다. 웨이트 합·미가중 정점이 0이어도 **"전이가 덜 된 것"** 은 못 잡히니 반드시 **본 목록**으로 볼 것.

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

---

## B. 렌더 · 카메라

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
