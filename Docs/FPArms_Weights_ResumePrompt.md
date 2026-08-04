# 1인칭 팔 (NEON-V) — Blender 파이프라인 · 확정판

> 갱신 2026-08-04 · 브랜치 `refactor/character`
> 앞선 판(스킨 웨이트 재개 프롬프트)은 **완료됐고, 그 문서가 적어둔 진단 하나가 틀려서** 이 문서로 대체한다.
> 증상→원인 색인은 `Docs/Troubleshooting.md` A1 · A1-b · A1-c · A1-d.

## 한 줄

**팔은 완성됐다.** `Saved/NeonV/NeonV_FPArms_v2.fbx` — 뼈 0/65 · 웨이트 완전 · 새끼손가락 복구.
남은 것은 UE 재임포트 하나이고, 그건 **사용자가 에디터에서** 한다(아래 §4).

## 1. 결함은 5개였다

| # | 결함 | 원인 | 해결 |
|---|---|---|---|
| 1 | rest pose Y축 통째 반전 | 옛 `fp_arms_retarget.py`가 UE 좌표를 Blender에 그대로 씀 | `fp_arms_rebuild.py`가 Manny 아마추어를 **그대로** 쓰도록 재작성 |
| 2 | 전 본 롤 90° | **UE의 glTF 임포터**가 FBX와 본 축 규약이 다름 | **FBX로 내보낸다**(Blender roll로는 못 고친다 — 임포터가 다시 넣는다) |
| 3 | 본 66개(65+1) | Blender FBX가 아마추어 오브젝트를 노드로 씀 → UE가 본으로 읽음 | 아마추어를 `root`로 개명 + 기존 `root` 본 삭제 |
| 4 | 스킨 웨이트 미완성 | `rebuild.py` Phase 3이 버텍스 그룹 **이름만** 바꾼다 → Blu에 없는 본은 빈칸 | 기준 메시에서 **통째 전이**(`fp_arms_reweight.py`) |
| 5 | 새끼손가락 실종 | `fp_arms_extract.py` 접두사에 `pinky`(Manny 이름)를 적음 — **Blu는 `little`** | 접두사에 `little` 추가 + 파이프라인 재실행 |

**4번이 왜 컸나** — Manny 리그는 팔뚝 표면을 **주로 twist가 굴린다**. 기준 메시에서 `upperarm_twist_02`가 2307정점으로 `lowerarm`(1372)보다 많다. 우리 팔은 twist 8개·metacarpal 8개가 전부 0이었고, 게다가 소매·팔·장식 **1385정점이 `clavicle` 하나에 통짜로** 물려 있었다(= "소매가 조각으로 갈라진다"의 정체).

**5번이 왜 안 잡혔나** — "본이 무가중" 이라 웨이트 문제로 읽었다. 실제로는 **살이 없었다.** 손을 한 장 렌더했으면 즉시 보였다(손가락이 3개였다).

## 2. 파이프라인 — 이 순서 그대로

Blender = `F:\Blender\blender.exe` (5.1.2) · 스크립트 = `C:\Users\koras\Desktop\작업\개발작업\블랜더\NeonV_scripts\`
작업 디렉터리 = 그 상위(`블랜더\`). 원본 = `[완성본]\NeonV_work.blend`.

```bash
B="F:/Blender/blender.exe"; S="NeonV_scripts"
J="E:/Git_Project/FPSRoguelite/Saved/NeonV/fparms_skeleton.json"
D="C:/Users/koras/Desktop/작업/개발작업/발주/_Delivery_FPArm_Blu/Placeholder_FPArms_SK_FP_Manny_Simple.fbx"

"$B" -b "[완성본]/NeonV_work.blend"  -P "$S/fp_arms_extract.py"     -- NeonV_FPArms_01_extract.blend
"$B" -b NeonV_FPArms_01_extract.blend -P "$S/fp_arms_rebuild.py"    -- "[완성본]/SKM_Manny_Simple.FBX" "$J" NeonV_FPArms_02_rebuild.blend
"$B" -b NeonV_FPArms_02_rebuild.blend -P "$S/fp_arms_trim_sleeve.py" -- NeonV_FPArms_03_trim.blend 10
"$B" -b NeonV_FPArms_03_trim.blend    -P "$S/fp_arms_reweight.py"   -- "$D" NeonV_FPArms_manny.blend
"$B" -b NeonV_FPArms_manny.blend      -P "$S/fp_arms_export_fbx.py" -- "E:/Git_Project/FPSRoguelite/Saved/NeonV/NeonV_FPArms_v2.fbx"
```

- **소매 트림 오프셋 = 10**(손목에서 10cm 팔쪽). 지금 에셋에서 역산한 값이다
- **`fp_arms_reweight.py`를 빼먹지 마라.** rebuild만으로는 twist·metacarpal이 빈 채로 나간다
- **`fp_arms_rebind.py`는 옛 경로다**(rebuild가 대체). 쓰지 말 것
- ⚠️ 출력 이름에 `NeonV_fp_arms.blend`를 쓰지 마라 — `anim_make_fp_arms.py`(로코모션 FP뷰 렌더용, **Blu 아마추어 108본짜리 별개 계보**)가 같은 이름을 쓴다. 실제로 한 번 덮어써서 이 계보의 소스를 잃었다

**웨이트 공여 메시** `Placeholder_FPArms_SK_FP_Manny_Simple.fbx` = `SK_FP_Manny_Simple`을 FBX로 뽑은 것. 애니가 저작된 바로 그 웨이트라 이걸 옮기는 게 정답이다. 전이가 잘 먹히는 이유는 rebuild Phase 2가 NEON-V 지오메트리를 **Manny 뼈 비율에 맞춰 구워 놓기** 때문(실측: 본 잔차 0.001cm, 맨살 표면거리 중앙 0.36cm).

## 3. 검증 — 눈으로 하지 않는다

| 게이트 | 방법 | 통과 기준 |
|---|---|---|
| 본 정합 | `reweight.py`가 자동으로 잰다 | 잔차 최대 < 0.5cm (실제 0.001) |
| 웨이트 | 메시별 **물린 본 목록** + 정점 수 | 무가중 0 · 합 1 · twist 8/8 · metacarpal 8/8 · `pinky_01/02/03` 물림 |
| 포즈 이탈량 | 기준 팔과 우리 팔에 **같은 월드 회전** → rest의 최근접 기준 삼각형을 `barycentric_transform`으로 예측 → 실제와 비교 | **대조군(기준 자신) = 0.00** 먼저 확인. 손·손가락 < 0.1cm, 소매 < 2cm |
| 재현 충실도 | 옛 메시 정점마다 새 메시 최근접 거리(KDTree) | 기존 부분 ~0 · 늘어난 정점은 전부 의도한 부위 |
| 지오메트리 | 경계 엣지·구멍·비매니폴드 카운트 | 소매 비매니폴드 0 |
| 손가락 개수 | **손등 쪽에서 렌더 한 장** | 엄지+4 |

**UE 쪽 본 잔차**(웨이트만 바꿨으면 0/65가 유지돼야 한다): 임시 `SkeletalMeshActor` 2개를 **열린 레벨에** 스폰(`set_mobility(MOVABLE)` → `set_skeletal_mesh_asset`) → `get_socket_transform(bone, RTS_COMPONENT)` 비교 → `destroy_actor`. **언등록 컴포넌트는 트랜스폼이 전부 0**이라 반드시 스폰해야 한다. 대조군(같은 메시 두 번)을 같이 잴 것.

## 4. UE 임포트 — 사용자가 에디터에서

`S_Mannequin`을 대상으로 하는 임포트라 **헤드리스로 하지 않는다.** 헤드리스엔 "Skeleton Conflicts" 창이 없어서 소스가 틀려도 조용히 스켈레톤을 덮어쓴다(541개 에셋이 물린 전례).

1. `Saved/NeonV/NeonV_FPArms_v2.fbx` → `Content/Character/FPArms/NeonV_FPArms` 재임포트
2. **"Skeleton Conflicts" 창이 뜨면 Done 금지** — 소스가 틀렸다는 신호다. 안 뜨면 **저장**(안 하면 에디터 닫을 때 통째로 날아간다)
3. 본 잔차 0/65 재측정 + `git status`로 `S_Mannequin` 무결 확인
4. PIE에서 소매·손목·새끼 3증상 확인

**되돌리기**: 옛 `Saved/NeonV/NeonV_FPArms.fbx` 재임포트, 또는 에디터 닫고 `git checkout`(에셋은 커밋 `839abef0`).

## 🪤 이미 당한 것

1. **바운드가 맞으면 rest pose가 맞다** — 틀렸다. 바운드는 미러에 불변이라 Y축 반전을 못 잡는다. 9일을 갔다
2. **"무가중 본" = 웨이트 결함** — 틀렸다. 살이 없을 수도 있다(§1 결함 5). **본 자리에 정점이 있는지부터 세라**
3. **커맨드렛에서 `AssetTools.import_asset_tasks`** — 임포트를 끝내고 콘텐츠 브라우저를 동기화하다 죽는다(`Assertion: CurrentApplication.IsValid()`). `InterchangeManager.import_asset`를 직접 쓸 것
4. **재임포트 후 저장을 잊지 말 것**
5. **`use_triangles=True`로 내보내지 마라** — 소매 단면 n-gon을 Blender가 삼각화하면 비매니폴드 엣지 9개가 생긴다(실측). n-gon 그대로 두고 UE 임포터에 맡기는 쪽이 깨끗하다

## 참고 파일

| | |
|---|---|
| 최종 산출물 | `Saved/NeonV/NeonV_FPArms_v2.fbx` (옛 것 = `NeonV_FPArms.fbx`, 롤백용으로 남겨둠) |
| Blender 체인 | `블랜더\NeonV_FPArms_0{1,2,3}_*.blend` → `NeonV_FPArms_manny.blend` |
| 원본 캐릭터 | `블랜더\[완성본]\NeonV_work.blend` |
| 설계 | `Docs/Architecture/0003-first-person-arms-camera-anchored.md` |
