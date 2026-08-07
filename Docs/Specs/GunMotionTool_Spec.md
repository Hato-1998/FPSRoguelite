# 총 모션 저작 툴 — 에디터 탭 명세 (C1, 2026-08-08)

> 보드 행: "총 모션 클립 저작 파이프라인 + 에디터 탭 툴" · 브랜치 `phase/fparms-gunanchor-ik`
> 규약 실증 = 커밋 `30a97953`(장전 총모션 1호, 파이썬 레시피). 이 명세는 그 레시피를 C++ 탭 툴로 옮긴다.
> 구현 = Sonnet 축자, 검증 = Fable. **명세 이탈 금지, 갭 발견 시 중단·보고.**

## 0. 배경 — 저작 모델 (바꾸면 안 되는 전제)

- 총은 `ik_hand_gun` 뼈에 앵커, ABP가 매 프레임 `CopyBone(hand_r → ik_hand_gun)`.
  양손 IK 타깃은 총-공간 상수 → **클립의 hand_r 델타만 저작하면 총+양손이 자동 추종**. ABP 수정 없음.
- 총 모션 클립은 애디티브(`AAT_LOCAL_SPACE_BASE`, base=`ABPT_ANIM_FRAME` frame 0). 오른팔 체인은
  "베이스 포즈 상수"로 중립화(=총 고정)한 뒤, hand_r 에만 의도된 델타를 얹는다.
- 🚨 함정 2개는 `Docs/Troubleshooting.md` **A16** 참조(트랙 삭제 금지 · 베이스 직접 샘플 금지 —
  **base = delta⁻¹∘raw 역산**만 정답).

## 1. 산출물 (신규 파일)

### 런타임 모듈 `FPSRoguelite` (데이터만, 로직 0)
- `Public/Anim/FPSRGunMotionAuthoringData.h` (+ 최소 .cpp)
  - `USTRUCT FFPSRGunMotionKey { float Time; FVector CamOffset; FRotator CamRotation; }`
    - CamOffset = 카메라 공간 (X=앞, Y=오른쪽, Z=위), cm. CamRotation = 카메라 공간 pitch/yaw/roll(도).
  - `UCLASS UFPSRGunMotionAuthoringData : public UAssetUserData`
    - `UPROPERTY(EditAnywhere) TArray<FFPSRGunMotionKey> Keys;`
    - `UPROPERTY(VisibleAnywhere) bool bSanitized = false;`
  - 용도: 저작 키의 영속화(클립 에셋에 부착 — 사이드카 파일 금지). 런타임 소비 없음.

### 에디터 모듈 `FPSRogueliteEditor`
- `Public/GunMotion/FPSRGunMotionSettings.h` (+.cpp) — `UDeveloperSettings` (기존 `FPSRWeaponAssemblerSettings` 패턴 답습)
  - `TSoftObjectPtr<UBlueprint> TargetCharacterBP` 기본 `/Game/Character/Player/BP_FPSRPlayer`
  - `TArray<FName> RightChainBones` 기본 `[clavicle_r, upperarm_r, lowerarm_r, hand_r, upperarm_twist_01_r, lowerarm_twist_01_r]`
  - `FName GripSourceBone = hand_r` · `FName PreviewSlotName = DefaultSlot`
  - `FName ArmsComponentName = FirstPersonArms` · `FName CameraComponentName = FirstPersonCamera`
- `Private/GunMotion/FPSRGunMotionBaker.h/.cpp` — 순수 로직(§3 수학). Slate 의존 금지(커맨드릿에서 재사용 가능하게).
- `Private/GunMotion/SFPSRGunMotionTab.h/.cpp` — Slate 탭(§4 UI).
- `FPSRogueliteEditorModule.cpp` — 노마드 탭 등록. 기존 탭 등록 패턴(어셈블러) 그대로: 탭 ID `FPSRGunMotionTab`,
  메뉴 라벨 "FPSR 총 모션 저작", Tools 메뉴 기존 위치에 병기.
- `FPSRogueliteEditor.Build.cs` — 필요한 모듈 추가. **엔진 소스 우선 원칙**: `UAnimPoseExtensions`·
  `IAnimationDataController` 실제 사용례를 `D:\UnrealEngine\UE_5.7\Engine\Source`에서 grep 해 모듈명·
  헤더·시그니처를 원본과 대조할 것(추론 금지).

## 2. 핵심 함수 (FPSRGunMotionBaker — 전부 static)

```cpp
// 우측 체인 중립화(총 고정). A16 레시피의 C++ 이식.
static bool SanitizeRightChain(UAnimSequence* Seq, const TArray<FName>& Bones, FText& OutError);

// 저작 키 → hand_r 트랙 굽기. bSanitized 가 아니면 실패(OutError).
static bool BakeGunMotion(UAnimSequence* Seq, const TArray<FFPSRGunMotionKey>& Keys, FText& OutError);

// 카메라→팔컴포넌트 회전 M. TargetCharacterBP CDO 의 두 컴포넌트에서 어태치 체인을 걸어 올라가며
// GetRelativeTransform 을 합성해 루트 기준 회전을 만들고, M = ArmsRot.Inverse() * CamRot.
static bool GetCamToCompRotation(FQuat& OutM, FText& OutError);
```

## 3. 수학 (실증 완료 — 축자 이식)

### 3-1. base 역산 (본 b, 시각 t=0 고정)
`UAnimPoseExtensions::GetAnimPoseAtTime` 2회 — ①`RetrieveAdditiveAsFullPose=false` → delta,
②`=true` → raw. (엔진 `FAnimationRuntime::ConvertTransformToAdditive` 정의와 대조: delta_rot = raw_rot * base_rot⁻¹)
```
base_rot = delta_rot⁻¹ * raw_rot          (정규화)
base_t   = raw_t − delta_t
base_s   = raw_s / (delta_s + 1)          (성분별, |분모|<1e-8 → 1.0)
```
Sanitize = 각 본의 base 를 **상수 키 (NumFrames+1)개**로 `SetBoneTrackKeys` (하나의 bracket).
완료 시 AssetUserData `bSanitized=true`, 패키지 dirty.

### 3-2. 굽기 (hand_r)
```
P     = Sanitize 된 클립 raw full pose t=0 에서 lowerarm_r 의 컴포넌트 공간 회전
        (UAnimPoseExtensions::GetBonePose, Space=World — 애님 에셋 평가라 World≡컴포넌트)
base  = hand_r 의 base (3-1)
M     = GetCamToCompRotation()

프레임 i (0..NumFrames), t = i / fps:
  (Ocam_t, Ocam_q) = EvalKeys(Keys, t)                    // §3-3
  Ocomp_t = M ⊗ Ocam_t          Ocomp_q = M * Ocam_q * M⁻¹
  d_rot   = P⁻¹ * Ocomp_q * P   (정규화)                   d_t = P⁻¹ ⊗ Ocomp_t
  clip_rot[i] = d_rot * base_rot (정규화)                  clip_t[i] = base_t + d_t
  clip_s[i]   = base_s
SetBoneTrackKeys("hand_r", ...) — 하나의 bracket, 패키지 dirty.
```
검증치(장전 1호): M ≈ Quat(0, 0, 0.7373, 0.6756). GetCamToCompRotation 결과가 이와 5° 이상
어긋나면 구현 오류로 간주하고 중단·보고.

### 3-3. EvalKeys — 키 보간
- Keys는 Time 오름차순 정렬 후 평가. t가 첫 키 이전/마지막 키 이후면 그 키 값 그대로.
- 인접 키 (A,B) 사이: `a = smoothstep((t−A.Time)/(B.Time−A.Time))` (a²(3−2a)).
  - 이동: 성분별 lerp(A,B,a) · 회전: `FQuat::Slerp(Aq, Bq, a)` (각 키의 CamRotation→FQuat 변환 후)
- 기본 템플릿(버튼): 길이 L인 클립에 `[(0, 0), (min(0.3,L*0.2), Full), (L−0.5 클램프, Full), (L, 0)]`
  Full = 사용자가 입력 중인 "풀 오프셋" 값(기본 R+3/U+6cm, P12/R10도).

## 4. UI (SFPSRGunMotionTab)

세로 스택, 기존 `SFPSRDataEditorWidget`·어셈블러 탭의 Slate 관례 답습:
1. **클립 선택** `SObjectPropertyEntryBox`(UAnimSequence). 선택 시 AssetUserData 로드 → 키 목록 채움.
2. **상태 줄**: 애디티브 여부 / bSanitized / 길이·프레임수. 비애디티브 클립이면 전 기능 비활성+사유 표시.
3. **[총 고정화]** 버튼 → SanitizeRightChain(설정의 RightChainBones). 성공/실패 토스트.
4. **키 목록**: 행 = Time(s)·Right·Up·Fwd(cm)·Pitch·Yaw·Roll(도), `SNumericEntryBox` + 행 추가/삭제.
   **[기본 템플릿]** 버튼. 편집 즉시 AssetUserData 에 반영(트랜잭션 `FScopedTransaction`).
5. **[클립에 굽기]** → BakeGunMotion → 성공 시 저장 여부 확인 다이얼로그(`UEditorAssetLibrary::SaveLoadedAsset`).
6. **[PIE에서 재생]** → PIE 월드가 있으면 로컬 `AFPSRCharacter`의 `FirstPersonArms` AnimInstance에
   `PlaySlotAnimationAsDynamicMontage(Seq, PreviewSlotName, 0.1, 0.1)`. PIE 없으면 사유 토스트.
   (뷰포트 프리뷰는 후속 — 기존 `SFPSRWeaponAssemblerFPTab` 인프라와 통합 예정, 이번 범위 아님)

## 5. 경계·금지

- **이 툴은 1인칭 팔 클립 전용** (3인칭 바디 클립은 풀바디 몽타주 — 범위 밖).
- 원본 팩 클립을 직접 수정하지 않도록: 클립 경로가 `Anims_LPAMG` 밖이거나 `_GunLocked`/`_GunMotion`
  접미가 없으면 경고 다이얼로그(계속/취소). 강제 차단은 안 함(사용자 판단).
- 에셋 경로 하드코딩 금지 — 전부 §1 Settings 로. 로그 카테고리 기존 `LogFPSR`... 에디터 모듈 기존
  카테고리 확인 후 그것을 쓸 것(신규 카테고리 금지).
- 성능·복제 무관(에디터 전용). 수명주기: 탭 위젯이 클립 강참조를 들지 말 것(TWeakObjectPtr).

## 6. 검증 (Fable 직접)

1. 빌드 0 에러 · 탭 열림 · 설정 기본값 표시.
2. `GetCamToCompRotation` == 실측 M(±5°).
3. 더미 사본 클립: 고정화 → AnimPose 델타 재계측 우측 체인 0.000 · 굽기 → 장전 1호와 동일 키 재현
   (파이썬 산출물과 clip_t/clip_rot 수치 대조).
4. PIE 재생 버튼 → 총 이동 실측(카메라 기준) 후 0 복귀.
