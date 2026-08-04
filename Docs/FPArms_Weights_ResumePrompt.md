# 재개 프롬프트 — 1인칭 팔 스킨 웨이트 수리 (Blender)

> 작성 2026-08-04 · 브랜치 `refactor/character`
> **이 문서 하나만 읽고 시작할 수 있게 썼다.** 배경이 더 필요하면 `Docs/Architecture/0003-*.md`.

## 한 줄

**팔 메시의 뼈는 이제 기준과 완전히 일치한다(0/65). 남은 결함은 스킨 웨이트 하나뿐이고, Blender에서 고쳐야 한다.**

## 지금 상태

UE 쪽 배선·에셋은 끝났다. `Content/Character/FPArms/NeonV_FPArms`는 `SK_FP_Manny_Simple` 대비
**위치 중앙 0.0001cm · 회전 중앙 0.0002° · 어긋난 본 0/65**. 스켈레톤은 `S_Mannequin` 그대로다.

그런데 애니를 재생하면 **소매가 조각으로 갈라지고, 손목이 돌아가고, 새끼손가락이 사라져 보인다.**
같은 애님그래프에 `SK_FP_Manny_Simple`(PWAS 자체 팔)을 넣으면 **멀쩡하다** — 즉 메시 문제다.

## 실측 — 웨이트가 미완성이다

`Saved/NeonV/NeonV_FPArms.glb` 감사 결과:

| 메시 | 정점 | 물린 본 | 문제 |
|---|---|---|---|
| `Jacket_FPArm` (소매) | 1296 | **6** — `clavicle`·`upperarm`·`lowerarm` ×2 | **`hand`도 twist도 없다.** 팔당 뼈 3개에 딱딱하게 물려 굽히면 판때기로 찢어진다 |
| `Body_FPArm` (맨살·손) | 2656 | 34 | **`pinky_02_l/r`·`pinky_03_l/r` 무가중** = 새끼손가락 끝 2마디가 아무 본도 안 따라감. 손등뼈 8개·twist 8개도 0 |
| `Accessoris_FPArm` | 197 | 4 | 장식이라 정상 |
| **`Icosphere`** | 42 | **0** | **쓰레기 지오메트리가 export에 섞였다** |

> 웨이트 합·미가중 정점은 0이라 "깨진 웨이트"가 아니라 **애초에 전이가 덜 된 것**이다.

## 할 일

1. **`Jacket_FPArm` 재웨이트** — `hand_*`와 twist 본까지 포함. 자동 웨이트 또는 원본 NEON-V 메시에서 전이.
   뼈 3개짜리 강체 바인딩이 소매 파열의 직접 원인이다
2. **`Body_FPArm`의 `pinky_02_l/r`·`pinky_03_l/r`에 웨이트 부여**
   (손등뼈·twist는 부모 `hand`/`lowerarm`이 대신 받고 있어 급하지 않다)
3. **`Icosphere` 제거** + `fp_arms_export.py`가 씬 전체를 선택하는 문제 정리
   (핸드오프에도 *"카메라 10·라이트 12·`REF_*` 3이 다 딸려간다"* 로 적혀 있다)
4. **원인 추적** — 웨이트 전이 단계(`NeonV_scripts/fp_arms_rebind.py`)를 읽어 어디서 빠졌는지 확인.
   안 고치면 다음 재추출(의상·스킨 변경 시 예정된 일)에서 그대로 재발한다

## 내보내기 — **반드시 FBX로**

glTF로 내보내면 **UE의 glTF 임포터가 전 본에 90° 롤을 넣는다**(FBX 임포터와 규약이 다름).
검증된 레시피(`Saved/NeonV/NeonV_FPArms.fbx`를 만든 방법):

```python
# 1) 아마추어 오브젝트가 UE에서 본 하나로 잡히는 걸 막는다 (65 -> 66 방지)
arm.name = "root"; arm.data.name = "root"
#    기존 root 본은 지운다 — 자식(pelvis, ik_hand_root)이 최상위가 되어 계층이 같아진다
bpy.ops.export_scene.fbx(
    filepath=OUT, use_selection=True,
    apply_unit_scale=True, apply_scale_options="FBX_SCALE_NONE", global_scale=1.0,
    add_leaf_bones=False, object_types={"ARMATURE", "MESH"},
    bake_anim=False, mesh_smooth_type="FACE")
#    본 축 옵션은 건드리지 않는다(기본값이 정답). X/Y 나 X/-Y 로 주면 180°/120° 어긋난다
```

Blender = `F:\Blender\blender.exe` (5.1.2), 헤드리스 `-b -P <script> -- <args>`.

## 검증 — 눈으로 하지 말 것

UE에 넣은 뒤 **`SK_FP_Manny_Simple` 대비 본 단위 잔차**를 잰다. 목표 = **0/65**.
레시피는 `Docs/Troubleshooting.md` A1/G1. 요지:

- 임시 `SkeletalMeshActor` 2개를 **열린 레벨에** 스폰(`set_mobility(MOVABLE)` → `set_skeletal_mesh_asset`)
  → `get_socket_transform(bone, RTS_COMPONENT)` 비교 → `destroy_actor`
- **언등록 컴포넌트는 트랜스폼이 전부 0**이라 반드시 스폰해야 한다
- **대조군(같은 메시 두 번)을 같이 재서 0이 나오는지 먼저 확인** — 안 하면 계측 오류를 결함으로 오독한다
- **바운드로 판정 금지** — 미러도 웨이트 결함도 extent를 안 바꾼다

웨이트 감사는 Blender 쪽에서: 메시별 **물린 본 목록 + 정점 수**를 찍어 위 표와 대조한다.

## 🪤 이미 당한 것

1. **바운드가 맞으면 rest pose가 맞다** — 틀렸다. Y축이 통째로 반전된 채 9일을 갔다
2. **커맨드렛에서 `AssetTools.import_asset_tasks`** — 임포트를 끝내고 콘텐츠 브라우저를 동기화하다 죽는다
   (`Assertion: CurrentApplication.IsValid()`). `InterchangeManager.import_asset`를 직접 쓸 것
3. **"Skeleton Conflicts" 창이 뜨면 Done 금지** — 소스가 틀렸다는 신호다. 헤드리스 임포트엔 이 창이 없으니
   `S_Mannequin`을 대상으로 한 임포트는 **에디터에서** 하고, 끝나면 `git status`로 스켈레톤 무결을 확인한다
4. **재임포트 후 저장을 잊지 말 것** — 에디터를 닫으면 통째로 날아간다

## 참고 파일

| | |
|---|---|
| 현재 소스(뼈는 정답, 웨이트 미완성) | `Saved/NeonV/NeonV_FPArms.glb` · `Saved/NeonV/NeonV_FPArms.fbx` |
| Blender 파이프라인 | `C:\Users\koras\Desktop\작업\개발작업\블랜더\NeonV_scripts\fp_arms_*.py` |
| 저작 이력·함정 | 같은 repo `Docs\HANDOFF_NEONV_FPARMS_RESULT.md` |
| 설계 | `Docs/Architecture/0003-first-person-arms-camera-anchored.md` |
