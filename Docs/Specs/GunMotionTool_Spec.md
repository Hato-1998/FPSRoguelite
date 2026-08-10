# 총모션 데이터·런타임 계약 (2026-08-10 — 저작 툴 폐기 후 생존 계약만)

> **커스텀 저작 툴은 전면 폐기됐다**(에디터 탭 v1~v3 + 인게임 ImGui 스튜디오, 2026-08-10 사용자 결정 —
> 4세대 제작에도 결과물 0, 객관 평가 후 종결. 경위 = 보드 행·git 히스토리). **저작은 표준 파이프라인**을 쓴다:
> 팔/손 키프레임 = Blender(프로젝트 검증 파이프라인) 또는 UE Sequencer+Control Rig → 애디티브 클립 베이크,
> 파츠/손 커브 = Persona 커브 에디터에서 아래 계약대로 직접 키잉.
> 이 문서는 **살아있는 데이터·런타임 계약만** 기술한다(구현 커밋 `75db21f7`, PIE 실측 검증 의미론).
> 교훈 규약: 착수 전 필요성·가능성·대안 검사(메모리 feasibility-check-before-work).

## 1. 커브 계약 (단위 cm/도, 부재=0. 총 전체 채널은 없다 — 총 모션은 hand_r 본 델타로 클립에 직접 저작)

이름 단일 소스 = `Source/FPSRoguelite/Public/Anim/FPSRGunMotionCurves.h` (`FPSRGunMotionCurves` 네임스페이스 +
`MakePartCurveNames`). **리터럴 중복 금지.**

| 커브 | 의미 | 공간 |
|---|---|---|
| `FPGM_HL_TX..RR` / `FPGM_HR_TX..RR` | 왼/오른손 IK 타깃 오프셋(그립 기준) | 총 공간(ik_hand_gun 본-부모) |
| `FPGM_HL_Blend` / `FPGM_HR_Blend` | 0=그립⊕오프셋, 1=부착 파츠 프레임⊕오프셋 | 0..1 (소비 측 클램프) |
| `FPGM_P_<부착소켓>_TX..RR` | 파츠 추가 상대 트랜스폼 | 이동=소켓 프레임 가산, 회전=저작 오프셋 좌곱 |

- 🚨 커브는 **클립+몽타주 양쪽**에 있어야 정식 재생에서 흐른다(`Docs/Troubleshooting.md` A17). 값이 갈라지면
  두 경로가 다르게 논다 — 한쪽을 소스로 정하고 복사 스크립트로 동기화.
- 총 전체 모션(장전 들기 등) = 클립의 hand_r 애디티브 델타(A16 gun-locked 규약 위에서) — CopyBone 구조가
  총+양손을 자동 추종시킨다. 저작 수학 참조가 필요하면 git `30a97953`(파이썬 레시피)·`a26c29a3^` 참조.

## 2. 클립 메타 (AssetUserData)

`Source/FPSRoguelite/Public/Anim/FPSRGunMotionStudioData.h` — `UFPSRGunMotionStudioData : UAssetUserData`.
런타임이 읽는 것은 **손별 부착 소켓 2필드뿐**(`LeftHandAttachPartSocket`/`RightHandAttachPartSocket` —
Blend=1에서 재기저할 파츠의 안정 id, `FFPSRWeaponPartAttachment::Socket`). 키/스팬 배열 필드는 폐기된 툴의
잔재로 현재 소비자 없음 — 다음 스키마 정리 때 제거 후보(콘텐츠에 부착 인스턴스가 없어 안전).

## 3. 런타임 소비 (구현 완료·검증됨 — 이것이 전부)

1. `UFPSRFirstPersonArmsAnimInstance::ComputeHandIKTarget`(손별): §1 커브를 읽어 게시값
   `Left/RightGripInGun*`에 합성 — **이동=총 공간 가산, 회전=좌곱**. Blend>0이면 부착 파츠의 **라이브 프레임**
   으로 재기저(빠지는 탄창을 따라감 — `라이브 = Result·Base⁻¹·Cached`). 커브 전무 = 기존 경로와 완전 동일.
2. `AFPSRCharacter::ApplyWeaponPartCurves`: `FPGM_P_*`를 파츠 상대 트랜스폼에 적용 — **매 프레임 저작
   Base(장착 시 스냅샷)에서 새로 합성**(누적 금지), 6커브 중 하나라도 있으면 적용(TX 단독 게이트 금지),
   커브 소멸 시 Base 복원(무상태), 전무 시 조기 반환. 커브명은 장착 시 1회 캐시.
3. 틱 자리 = `UFPSRWeaponFireComponent::TickComponent`(팔 애님 후행 순서 보장).
4. owner-local 1인칭 전용 — 복제 0, 서버 접점 0, 3인칭/원격 무변경.

## 4. DA 상태 포즈 스키마 (데이터만 — 소비는 홀스터/상태별 모션 보드 행)

`FFPSRWeaponStatePose { FVector Offset; FRotator Tilt; }` —
`FFPSRProceduralWeaponMotionProfile`에 `SlidePose(+SlideBobScale=0)` / `AirbornePose(+AirborneBobScale=1)` /
`HolsterPose(+HolsterDuration=0.25)`. 저작 = 에디터 Details(필요 시 후속으로 PWAS식 라이브 튜닝 패널 검토 —
그 결정은 홀스터 행에서). `UFPSRWeaponDataAsset::ArmsIdleAnim` = 무기별 1인칭 팔 Idle 소프트 참조(확장 대비,
ABP는 현행 하드코딩 유지).

## 5. 검증 기준 (표준 파이프라인 저작물에 적용)

1. 커브 저작 클립 → PIE: 손 재기저(탄창 추종)·파츠 이동·종료 즉시 복귀·무커브 클립 회귀 0.
2. 클립·몽타주 커브 값 동일(A17) — diff 로 확인.
3. hand_r 델타 클립 → 총 이동 실측 후 0 복귀(카메라 기준).
