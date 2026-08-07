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

---

# 증보 v2 — 비주얼 저작: 1인칭 뷰포트 + 기즈모 키프레이밍 (2026-08-08 사용자 결정)

> 사용자 요구: "인게임 1인칭 뷰 가져온 다음, 각 프레임마다 총기의 위치를 기즈모로 잡고 그 사이를 보간".
> 숫자 키 목록(§4-4)은 미세조정용으로 유지 — 같은 `Keys` 데이터를 두 UI 가 편집한다.

## 7. 프리뷰 씬 (탭이 단독 소유)

- `FAdvancedPreviewScene` 을 **이 탭이 생성·단독 소유**한다. 어셈블러 씬 공유 금지 —
  `SFPSRWeaponAssemblerFPTab` 헤더의 수명주기 지뢰(낡은 씬 렌더) 주석 참조. 공유할 이유도 없다(저작
  대상이 무기 조립이 아니라 클립).
- 팔 = **`UDebugSkelMeshComponent`** (🚨 일반 SkeletalMeshComponent 금지 — 애디티브 프리뷰는
  `UAnimPreviewInstance`(bCanProcessAdditiveAnimations)라야 기준 포즈 위에 올바로 얹힌다. 이 프로젝트
  실사고: 단일노드 인스턴스는 A포즈 위에 차분을 얹었다). 메시 = 설정 `TargetCharacterBP` CDO 의
  `ArmsComponentName` 컴포넌트가 물고 있는 SkeletalMeshAsset.
- 무기 = 설정 신규 항목 `PreviewWeaponMesh`(`TSoftObjectPtr<UStaticMesh>`, 기본
  `/Game/Weapons/Meshes/Modular/Weapon_A/SM_Wep_Mod_A_Body_01`) + `PreviewWeaponAttachSocket`(기본
  `SOCKET_Weapon`) — 팔 메시의 그 소켓에 어태치. 게임의 gun-anchor 경로(ik_hand_gun)와 다르지만
  등가다: CopyBone(hand_r→ik_hand_gun)이 항등이 되는 조건에서 총은 hand_r 을 강체로 따라가므로,
  프리뷰에서 hand_r 소켓 부착 = 인게임 결과와 같은 상대 배치.
- 카메라: `AFPSRCharacter::GetFirstPersonViewSetup`(위상 무관 카메라-팔 상대 트랜스폼 + FOV) —
  `SFPSRWeaponAssemblerFPTab`/`FPSRWeaponAssemblerFPViewportClient` 가 쓰는 그 경로를 답습. 화면비
  고정 로직 포함(어셈블러 FP 탭의 프레이밍 코드 참조, 화면비 콤보는 생략하고 16:9 고정으로 시작).

## 8. 프리뷰 재생 모델 — "항상 구운 결과를 본다"

- 탭은 편집 세션 시작 시(클립 선택 시) 원본 클립을 **transient 패키지에 복제**(`DuplicateObject`)한
  "프리뷰 클립"을 만들고, DebugSkelMeshComp 의 PreviewInstance 에 그것을 튼다.
- **키가 바뀔 때마다**(기즈모 드래그 종료·숫자 편집·키 추가/삭제) `BakeGunMotion(프리뷰 클립, Keys)` 를
  다시 실행한다(1,500키 굽기는 ms 단위 — 실측 기준 부담 없음). 프리뷰는 언제나 "구운 결과"를 보여준다 —
  근사 시각화 금지(WYSIWYG 가 이 증보의 존재 이유).
- 타임라인: 스크럽 슬라이더(0..PlayLength) + [재생/정지] 토글 + 키 시각 마커. 스크럽 중에는
  PreviewInstance 를 해당 시각에 고정(SetPosition, 재생 정지). 엔진 사용례(Persona 계열)를 grep 해
  UAnimPreviewInstance 의 정확한 API(SetAnimationAsset/SetPosition/SetPlaying)를 확인 후 사용.
- [클립에 굽기] = 실제 에셋에 §3-2 실행(기존 그대로). 프리뷰 클립은 커밋과 무관한 스크래치.

## 9. 기즈모 키프레이밍

- 뷰포트 클라이언트는 `FFPSRWeaponAssemblerViewportClient` 의 기즈모 스택 패턴을 답습:
  `GetWidgetLocation`/`GetWidgetMode`/`SetWidgetMode`/`InputWidgetDelta`/`TrackingStarted(+Stopped)`
  오버라이드, ModeTools 미경유(같은 이유 — 그 파일 주석 참조). 대상은 항상 무기 컴포넌트 하나.
- **베이스라인 캐시**: 오프셋 0 상태(= Keys 를 전부 0 으로 한 임시 굽기 또는 고정화 직후)에서 무기
  컴포넌트의 월드 트랜스폼 `GunBase` 를 1회 캐시. 우측 체인이 상수라 시각 무관 상수다.
- 드래그 중: 무기 컴포넌트에 델타를 즉시 반영(시각 피드백). **드래그 종료(TrackingStopped) 시** 현재 무기
  월드 트랜스폼 `GunNow` 에서 카메라 공간 오프셋을 역산해 현재 스크럽 시각의 키로 저장:
  ```
  CamRot   = 프리뷰 카메라의 월드 회전(FQuat)
  O_cam_t  = CamRot⁻¹ ⊗ (GunNow.T − GunBase.T)
  O_cam_q  = CamRot⁻¹ * (GunNow.R * GunBase.R⁻¹) * CamRot     (정규화)
  ```
  같은 시각(±1프레임)의 키가 있으면 갱신, 없으면 추가. 그 뒤 §8 재굽기 → 컴포넌트 임시 델타 해제(구운
  포즈가 대신한다).
- 키 편집 후 `FScopedTransaction` 으로 AssetUserData 반영(기존 §4-4 와 동일 규칙).
- 툴바: 이동/회전 토글(W/E 키 바인딩은 어셈블러 뷰포트 관례 따름), [현재 시각에 키]/[키 삭제].

## 10. v2 검증 (Fable)

1. 탭에 1인칭 구도 뷰포트가 뜨고 클립 스크럽으로 팔 포즈가 움직인다(애디티브가 idle 위에 얹혀 보임 —
   A포즈면 UDebugSkelMeshComponent 미사용 버그).
2. 기즈모로 총을 끌고 놓으면 키가 생기고, 스크럽하면 키 사이가 보간된다.
3. 저장 후 PIE 재생으로 인게임 결과 = 프리뷰 결과(카메라 기준 오프셋 실측 대조).

---

# 증보 v2.1 — 인게임 구도 캡처 + 자유시점 (2026-08-08 사용자 피드백)

> 실사용 피드백: 프리뷰 1인칭 구도가 실제 인게임과 다르다. 원인 = 프로브 스폰(BeginPlay 없음)의
> 구도는 **CDO 저작 배치**라 런타임 보정(시선 높이/스탠스 카메라 등)이 빠진다. 캐릭터를 통째로
> 프리뷰에 스폰해도 BeginPlay·빙의 없인 같은 값이다 — 그래서 스폰이 아니라 **실측 캡처**로 푼다.

## 11. [PIE 구도 캡처]

- 설정 추가(`FPSRGunMotionSettings`, Config):
  `FTransform CapturedCameraRelativeToArms` · `float CapturedFOV = 0` · `bool bHasCapturedComposition = false`
- 탭 버튼 **[PIE 구도 캡처]**: PIE 월드(GEditor->GetPIEWorldContext)의 로컬 `AFPSRCharacter` 에서
  `FirstPersonCamera`/`FirstPersonArms` 컴포넌트의 **실제 월드 트랜스폼**으로
  `CamRelArms = CamWorld.GetRelativeTransform(ArmsWorld)` 와 `GetCameraView(0, ...)` 의 FOV 를 읽어
  설정에 저장 → **`TryUpdateDefaultConfigFile()`** (🚨 `SaveConfig()` 금지 — CDO 메모리만 바뀌고 ini 에
  안 남는다, 프로젝트 실사고. 반환 bool 확인·실패 시 토스트) → 뷰포트 즉시 재적용.
- `RefreshCameraComposition` 분기: `bHasCapturedComposition` 이면 캡처값 사용, 아니면 기존 프로브 경로
  (최초 사용 전 폴백). 상태줄에 어느 소스인지 표시("캡처 구도" / "기본(BP) 구도 — PIE 캡처 권장").
- PIE 가 없으면 사유 토스트(기존 [PIE에서 재생] 버튼과 같은 처리).

## 12. 자유시점 토글

- 뷰포트 툴바 체크박스 + 단축키 **F**: ON 이면 `ApplyCameraComposition` 스킵(구도 잠금 해제, 표준
  에디터 뷰포트 네비게이션 — 회전/이동/줌), OFF 면 저장된 구도로 재잠금.
- ON 진입 시 현재 구도 카메라 위치에서 시작(뷰가 튀지 않게), OFF 복귀는 구도 값으로 스냅.
- 기즈모/키프레이밍은 두 모드 모두 동작(§9 역산의 `CamRot` 은 **잠금 구도의 카메라 회전**을 쓴다 —
  자유시점에서 끌어도 키는 인게임 카메라 기준으로 저장되어야 한다. 자유시점 뷰 회전을 쓰면 같은
  드래그가 뷰마다 다른 키를 낳는다).

## 13. v2.1 검증 (Fable)

1. PIE 켜고 캡처 → 설정 ini 에 값 실림(TryUpdate 반환 true + 파일 diff) → 프리뷰 구도가 PIE 화면과
   일치(카메라-팔 상대 트랜스폼 수치 대조).
2. F 토글로 자유시점 진입/복귀, 복귀 시 구도 정확 복원.
3. 자유시점에서 기즈모 드래그 → 키가 잠금 구도 카메라 기준으로 저장되는지 수치 확인.
