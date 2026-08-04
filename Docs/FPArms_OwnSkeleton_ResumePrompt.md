# 재개 프롬프트 — 1인칭 팔 자체 스켈레톤 전환

> 작성 2026-08-04 · 브랜치 `refactor/character`
> **이 문서 + [ADR 0004](Architecture/0004-first-person-arms-own-skeleton.md) 만 읽고 시작할 수 있게 썼다.**

## 한 줄

**팔 메시를 마네킹 뼈에 맞춰 구부리던 것을 그만두고, 뼈를 메시에 맞춘다.** 본 이름·계층은 Manny 65본 그대로, 위치만 NEON-V 해부로.

## 왜 (요약 — 근거는 ADR 0004)

PWAS 포즈를 그대로 재생하지 않기로 했으므로(애니 직접 저작) `S_Mannequin` 에 묶일 이유가 사라졌다. 묶여 있던 대가가 **손 왜곡**이다 — 손바닥 +9~21%, 손가락 −7~10%, 손 폭 +9%. Manny 는 손바닥 안에 손허리뼈가 있어 손가락 뿌리가 1.0~1.6cm 더 멀고, rebuild 가 그만큼 늘렸다. **포즈로는 못 고친다.**

## 🚨 착수 전 결정 하나 — 어디까지 되돌릴지

| | 원본 NEON-V | 현재(마네킹) |
|---|---|---|
| 팔 도달 | **41.54cm** | 55.02cm |
| 어깨 간격 | 28.09cm | 38.02cm |

**팔까지 되돌리면 소총을 두 손으로 못 잡는다** — 왼손 그립이 오른손에서 43cm 앞인데 도달이 41.5cm다.

**권장 A: 손만 NEON-V, 팔 길이·어깨는 마네킹 유지.** 사용자가 지적한 건 손이고 팔 길이는 1P 에서 거의 안 보인다. 선택지 B/C 는 ADR 0004 참조. **사용자에게 확인부터 받을 것.**

## 지금 있는 것

### 파이프라인 (블랜더 repo `NeonV_scripts/`)
| 스크립트 | 역할 |
|---|---|
| `fp_arms_extract.py` | 원본에서 팔만 분리. 🚨 접두사에 `little`(Blu 새끼) 필수 — 빠뜨려 482정점을 잃은 적 있다 |
| `fp_arms_rebuild.py` | **이번에 고칠 것.** 지금은 메시를 Manny 뼈로 구부린다 → 뼈를 메시에 맞추는 쪽으로 뒤집는다 |
| `fp_arms_trim_sleeve.py` | 소매 손목 −10cm 절단 + 캡. `holes_fill` + 삼각화 |
| `fp_arms_reweight.py` | 기준 메시에서 웨이트 통째 전이. **기준이 마네킹이면 새 비율에선 안 맞을 수 있다 — 재검토 필요** |
| `fp_arms_export_fbx.py` | FBX 출력. 🚨 metre→cm 우회 필수(아래 함정 1) |
| `fp_arms_make_authoring.py` | 저작 씬 조립(총·카메라·그립 타깃) |
| `fp_arms_pose_from_ref.py` | 견본 포즈 얹기 + IK. **회전은 비율이 달라도 옮겨지므로 새 스켈레톤에서도 유효** |

### 저작 씬 (블랜더 repo 루트)
`NeonV_FPArms_manny.blend`(현 소스) · `NeonV_FPArms_authoring.blend`(총+카메라+그립) · `NeonV_FPArms_idle.blend`(견본 포즈 얹은 것)
→ **새 스켈레톤이 나오면 전부 다시 만들어야 한다.**

### 확보된 수치 (다시 안 재도 된다)
| | |
|---|---|
| `SOCKET_Weapon` | 지금 `hand_r` **identity** = 손목. 손잡이가 손바닥에서 **6.03cm** 뜬다. hand_r 로컬 기준 **(−5.45, 0.75, 2.45)cm** 옮기면 손바닥. **새 스켈레톤엔 처음부터 손바닥에 박을 것** |
| `SOCKET_LeftHand` | 핸드가드 파츠가 소유 · 핸드가드 로컬 (4, 30, 0) · 오른손에서 약 43cm 앞 |
| 무기 조립 | 리시버 `SK_Wep_Mod_A_Body_01` + 파츠 7개, 마운트 소켓 전부 오프셋 0. 부착 스케일 **0.85** |
| 1P 카메라 | 팔 컴포넌트 기준 **(0, 10, 160)cm** (UE) / FOV **90** / 근평면 1cm |
| 팔 컴포넌트 | `FirstPersonCamera` 기준 `(-10, 0, -160)`, yaw −90, **스케일 1** |
| `LeftHandGripOffset` | BP 에 `(1, 3, 5)` 저작됨 |

## 🪤 함정 — 전부 실제로 당한 것

1. **FBX 메시가 UE 에서 100배 작게 들어온다.** UE 는 metre→cm ×100 을 **아마추어 스케일**로 얹는데, 기존 스켈레톤에 붙이면 그게 버려진다. **본 잔차 검증이 이걸 구조적으로 못 잡는다**(스켈레톤 ref pose 를 읽으므로). 판정 = 메시 에디터 **`Approx Size`**. 해결 = 지오메트리 ×100 + **부모** 오브젝트 스케일 0.01 + `FBX_SCALE_NONE`. → `Docs/Troubleshooting.md` **F1-b**
2. **UE 뼈에는 꼬리 개념이 없다** — FBX 임포터가 아무 방향·길이나 붙인다(lowerarm 꼬리가 손목에서 32.8cm). **Blender IK 를 그대로 못 쓴다.** 꼬리가 손목인 **비변형 팁 뼈**를 달고 회전을 잠글 것(`fp_arms_pose_from_ref.py` 에 구현돼 있다)
3. **`Unreal Take` 견본은 root 에 월드 위치가 실려 있다** — 그대로 복사하면 팔이 **84m** 날아간다. 포즈는 **회전만** 옮긴다
4. **정면 렌더 한 장으로 판정하지 말 것** — 원근이 망가진 포즈를 숨긴다. **위·옆·정면·1인칭 4각도**로 볼 것
5. **숫자가 거짓말을 한다** — "왼손↔그립 0.00cm" 인데 손이 허공을 잡고 있던 적이 있다(그립 타깃이 총을 안 따라감). 거리뿐 아니라 **표면까지의 거리**도 잴 것
6. **라이브 에디터에 Python 으로 FBX 임포트 = 데드락.** 임포트는 커맨드렛에서. 내보내기는 `AssetExportTask` 에 `options=FbxExportOption()` 을 줘야 대화상자가 안 뜬다
7. **`S_Mannequin` 을 대상으로 하는 임포트는 사람이 에디터에서.** "Skeleton Conflicts" 창이 뜨면 Done 금지

## 할 일 순서

1. **결정 받기** — 위 "어디까지 되돌릴지" (권장 A)
2. **`fp_arms_rebuild.py` 뒤집기** — 메시를 구부리는 대신, Manny 이름·계층으로 **NEON-V 해부 위치에 뼈를 세운다**. 손허리뼈는 손바닥 안 제자리로
3. **재추출 → 트림 → 웨이트 → export** — 웨이트 전이의 기준 메시를 새 비율에서도 쓸 수 있는지 재검토
4. **UE 임포트** — **새 스켈레톤을 만드는 임포트**다(기존 `S_Mannequin` 대상이 아니다). 새 경로 결정 필요(예: `/Game/Character/FPArms/SK_NeonV_FPArms`)
5. **배선** — 팔 AnimBP 를 새 스켈레톤 대상으로 · `SOCKET_Weapon` 을 손바닥에 저작 · `FirstPersonArms` 컴포넌트 메시 교체
6. **검증 재정의** — 0/65 는 무의미해진다. ADR 0004 "미결 2" 참조
7. **애니 저작** — idle 부터. 견본은 `A_FP_Rifle_Pose`(회전만 옮기면 새 비율에서도 유효)

## 참고

| | |
|---|---|
| 결정 근거 | `Docs/Architecture/0004-first-person-arms-own-skeleton.md` |
| 이전 결정 | `Docs/Architecture/0003-first-person-arms-camera-anchored.md` (불변식 계승) |
| 증상→원인 | `Docs/Troubleshooting.md` A1-c · A1-d · D1-a · F1-b |
| 견본 포즈 FBX | 스크래치패드에 뽑아 뒀으나 세션 종료 시 사라진다 — UE 에서 다시 뽑을 것(`AnimSequenceExporterFBX`) |
| 현 출고 메시 | `Content/Character/FPArms/NeonV_FPArms` (몸만, 재킷·장식 제외) · 소스 `Saved/NeonV/NeonV_FPArms_v5.fbx` |
