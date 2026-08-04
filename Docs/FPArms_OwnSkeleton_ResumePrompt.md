# 재개 프롬프트 — 1인칭 팔 자체 스켈레톤 (**전환 완료 · 배선만 남음**)

> 갱신 2026-08-04 · 브랜치 `refactor/character`
> 앞선 판은 "이제 전환한다"였다. **전환은 끝났다.** 이 문서는 그 뒤를 잇는다.

## 한 줄

**뼈를 메시에 맞추는 전환이 끝났고 게이트 15개가 통과했다. 남은 건 에디터 배선과 PIE뿐이다.**

## 지금 있는 것

| | |
|---|---|
| 메시·스켈레톤 | `Content/Character/FPArms/SK_NeonV_FPArms{,_Skeleton,_PhysicsAsset}` — 65본, 이름·계층 `S_Mannequin` 과 완전 일치 |
| 포즈 | `Content/Character/FPArms/Anims/FP_Rifle_{Idle,ADS}` — 우리 스켈레톤, PWAS 회전 전이(원본과 0.0066° 이내) |
| 소켓 | `SOCKET_Weapon` = **메시** 소유 · `hand_r` 로컬 `(-4.37, 0.52, 3.34)` = 손바닥 |
| 옛 에셋 | `NeonV_FPArms` **안 지웠다** — 배선만 되돌리면 즉시 롤백 |
| Blender | `NeonV_FPArms_own.blend` (옛 `_manny.blend` 는 대조군으로 보존) |
| FBX·스펙 | `Saved/NeonV/SK_NeonV_FPArms.fbx` · `Saved/NeonV/fparms_own_spec.json` |
| 렌더 | `Saved/NeonV/verify_own/hand_{rest,fist}_{front,side,top,back}.png` |

**손 왜곡은 사라졌다** — 손목→손가락 뿌리가 원본 NEON-V 값으로 정확히 복귀(엄지 4.08 · 검지 8.43 · 중지 7.88 · 약지 8.15 · 새끼 8.16).

## 할 일 (전부 에디터 · 사용자)

1. **`BP_FPSRPlayer` → `FirstPersonArms`** 메시를 `SK_NeonV_FPArms` 로. **스케일 1**
2. **`ABP_FirstPerson`** 을 새 스켈레톤 대상으로 (`포즈 → Slot → Two Bone IK(hand_l) → Output`)
   - 포즈 입력을 `A_FP_Rifle_Pose`(PWAS) → **`FP_Rifle_Idle`**(우리 것)로. 이걸 바꿔야 PWAS 의존이 실제로 끊긴다
   - `Effector Location Space` = **World Space** · `Joint Target Location` 이 (0,0,0)이면 팔꿈치가 뒤집힌다
3. **소켓 눈 확인** — `(-4.37, 0.52, 3.34)` 은 손바닥 길이비 0.828 로 옛 값을 옮긴 **시작점**이다. 총을 물려 확정할 것
4. **`LeftHandGripOffset (1,3,5)`** 재조정 (옛 손 기준이었다)
5. PIE — 소매·손목·새끼 3증상 + 왼손 그립 + 내 그림자 왼팔 + 동료 화면 무기

## 다시 돌릴 때 (파이프라인 전체)

```bash
B="F:/Blender/blender.exe"; S="NeonV_scripts"
J="E:/Git_Project/FPSRoguelite/Saved/NeonV/fparms_skeleton.json"
D="C:/Users/koras/Desktop/작업/개발작업/발주/_Delivery_FPArm_Blu/Placeholder_FPArms_SK_FP_Manny_Simple.fbx"

"$B" -b "[완성본]/NeonV_work.blend"   -P "$S/fp_arms_extract.py"      -- NeonV_FPArms_01_extract.blend
"$B" -b NeonV_FPArms_01_extract.blend -P "$S/fp_arms_rebuild.py"      -- "[완성본]/SKM_Manny_Simple.FBX" "$J" NeonV_FPArms_02_rebuild.blend
"$B" -b NeonV_FPArms_02_rebuild.blend -P "$S/fp_arms_trim_sleeve.py"  -- NeonV_FPArms_03_trim.blend 10
"$B" -b NeonV_FPArms_03_trim.blend    -P "$S/fp_arms_reweight.py"     -- "$D" NeonV_FPArms_own.blend
"$B" -b NeonV_FPArms_own.blend        -P "$S/fp_arms_dump_spec.py"    -- "E:/.../Saved/NeonV/fparms_own_spec.json"
"$B" -b NeonV_FPArms_own.blend        -P "$S/fp_arms_export_fbx.py"   -- "E:/.../Saved/NeonV/SK_NeonV_FPArms.fbx" Jacket_FPArm Accessoris_FPArm
```
검증 = `fp_arms_verify_own.py -- <원본추출.blend> <옛결과.blend> <출력폴더>` (**절대 경로로** 넘길 것 — 상대 경로는 `C:\` 기준으로 풀린다).
UE = `Scripts/fparms_import_newskel.py` → `fparms_socket_physics.py` → `fparms_export_ref_poses.py` → (Blender `fp_arms_pose_transfer.py`) → `fparms_import_anims.py` → `fparms_verify.py`(읽기 전용).
전부 `UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<파일> -unattended -nopause -abslog=<로그>`.
> 이 스크립트들은 원래 `Saved/NeonV/` 에 있었는데 **`Saved/` 는 gitignore 라 clean 한 번에 사라진다.** 그래서 추적되는 `Scripts/` 로 옮겼다(이 프로젝트의 헤드리스 스크립트가 원래 사는 곳이다).

## 🪤 함정 (전부 실제로 당한 것)

새로 배운 것 = `Docs/Troubleshooting.md` **A7~A10**. 요약:

1. **"본 잔차 0/65"는 스켈레톤을 공유하는 동안 아무것도 증명 못 한다** — `get_socket_transform` 이 메시가 아니라 ref pose 를 돌려준다. 단위 100배 사고를 못 잡은 것도 같은 이유(A7 · F1-b)
2. **뼈 로컬 좌표도 Blender↔UE 는 `(x, -y, z)`** — UE 에서 잰 값을 Blender 축으로 분해하면 조용히 섞인다. **계산은 한쪽 공간에서 끝내라**(A10)
3. **두 리그의 손을 뼈 로컬 축으로 겹치지 마라** — 축 규약이 다르다(엄지 100~107°). **뼈 머리들로 Kabsch 강체정합**을 풀 것(A9)
4. **1프레임 애니는 길이 0** — 애님그래프가 0으로 나눈다. 같은 포즈를 **두 프레임**에 박아라(A8)
5. **표면거리를 총 거리로 게이트하지 마라** — NEON-V 팔이 마네킹보다 가늘어서 반지름 방향으로 1.7cm 떠 있는 게 정상이다. **길이방향만** 걸어라
6. `save_directory(only_if_is_dirty=False)` 는 **안 건드린 텍스처까지 재직렬화**해 git diff 를 더럽힌다
7. **`Skeleton.Sockets` 는 파이썬에서 protected** · `SkeletalMeshSocket.socket_name` 은 read-only → 메시의 `add_socket` + `rename_socket` + `set_socket_parent` 를 쓸 것
8. 라이브 에디터에 Python FBX 임포트 = 데드락. 임포트는 커맨드렛에서

## 참고

| | |
|---|---|
| 결정·게이트 실측 | `Docs/Architecture/0004-first-person-arms-own-skeleton.md` |
| 이전 결정(불변식 계승) | `Docs/Architecture/0003-first-person-arms-camera-anchored.md` |
| 경위 전문 | `Docs/WorkLog.md` 최상단 |
