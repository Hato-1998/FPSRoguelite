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

---

# 증보 v2.2 — 탭-PIE 라이브 링크 (2026-08-08 사용자 결정: 최종 저작 형태)

> "조작은 에디터 기즈모(정밀), 판정은 진짜 인게임 화면." PIE 를 켜둔 채 탭에서 저작하면 PIE 의
> 실제 1인칭 팔이 실시간으로 같은 포즈를 미러링한다. 인게임 오버레이 저작안은 기각(게임 뷰에는
> 기즈모가 없어 조작 정밀도가 떨어짐 — 사용자 합의).

## 14. 라이브 미러

- 탭에 **[PIE 라이브 링크]** 토글(체크박스, 기본 ON). 조건: PIE 월드 존재 + 프리뷰 클립 유효.
  상태줄에 링크 상태 표시("PIE 링크 활성" / "PIE 없음").
- 메커니즘 — PIE 로컬 `AFPSRCharacter` 의 `FirstPersonArms` AnimInstance 에 **프리뷰 클립(transient)** 을
  `PlaySlotAnimationAsDynamicMontage(PreviewClip, PreviewSlotName, 0.1, 0.1)` 로 올리고:
  - 탭 스크럽 변경 → `Montage_SetPosition` + `Montage_Pause` (포즈 고정 동기화)
  - 탭 [재생] 중 → 미러 몽타주도 재생, 매 틱 위치 오차 0.1s 이상이면 재동기화
  - **재굽기(RebakePreview) 직후 → 미러 몽타주를 정지 후 재시작**(블렌드 0.05) — 재생 중인 몽타주가
    갱신된 압축 데이터를 확실히 다시 읽게 하는 가장 단순한 보장. 이후 현재 스크럽 위치로 복원.
- 수명주기: 폰/애님 인스턴스는 전부 `TWeakObjectPtr`. 매 틱 유효성 검사 — PIE 종료/리스폰 시 조용히
  해제(에러 금지), PIE 재시작 시 자동 재연결. 탭/클립 닫기 시 미러 몽타주 `Montage_Stop(0.1)` 정리.
- 🚨 슬롯 재생은 **클립 자신의 커브만** 흐른다 — 기존 IK 해제 커브(`LeftHandIKWeight`)는 정식 몽타주
  (AM_FP_Rifle_Reload) 에 실려 있어 미러에는 안 실린다. 처리: **커브를 클립으로 이관**한다(검증 단계에서
  스크립트로: 클립에 상수 0 커브 추가 + 정식 몽타주에서 제거 — 같은 이름 커브 이중 저작 금지). 툴은
  이관 여부를 판단하지 않는다(범위 밖) — 단 [총 고정화] 시 클립에 `LeftHandIKWeight` 커브가 없으면
  상태줄에 경고 한 줄("IK 해제 커브가 클립에 없음 — 미러에서 왼손이 그립에 고정됨").

## 15. v2.2 검증 (Fable)

1. PIE + 탭 동시: 스크럽 → PIE 팔 포즈가 따라온다(몽타주 위치 수치 대조).
2. 기즈모 드래그 커밋 → 1초 내 PIE 팔에 새 굽기 반영(총 위치 실측 변화).
3. PIE 종료→재시작 시 크래시/에러 로그 0, 자동 재연결.
4. 커브 이관 후: 미러 재생 중 왼손 IK 알파 1→0(장전 구간) 실측.

---

# 증보 v2.3 — 애니메이션 툴급 타임라인 (2026-08-09 사용자 요구)

> v2.2 실사용 확인 후 요구: "키프레임이 어디에 있는지 실제 애니메이션 툴처럼 타임라인 및 키프레임이
> 보여야 한다." §8 의 "키 시각 마커"를 전용 타임라인 위젯으로 격상한다.

## 16. 타임라인 위젯 (SFPSRGunMotionTimeline)

- 기존 스크럽 슬라이더를 **대체**하는 전용 Slate 위젯(GunMotion 폴더 신규 파일). 구성:
  - **시간 룰러**: 초 단위 주눈금 + 0.1s 보조눈금 + 숫자 라벨. 클립 길이(PlayLength)가 전체 폭.
  - **플레이헤드**: 현재 스크럽 시각 세로선 + 상단 핸들. 빈 곳 클릭/드래그 = 스크럽(기존 슬라이더와
    동일 경로로 — 스크럽 상태 소유는 기존 위치 유지, 위젯은 값을 읽고 변경 요청만 한다).
  - **키 마커**: 각 저작 키의 Time 위치에 다이아몬드(◆). 선택 키는 하이라이트 색.
- **조작**:
  - 키 클릭 = 선택 + 스크럽을 그 키 시각으로 점프(숫자 목록 행 선택과 동기화).
  - 키 좌우 드래그 = Time 변경(클립 프레임레이트로 스냅). 드래그 종료 시에만 커밋 —
    `FScopedTransaction` + 기존 단일 훅(RebakeViewportPreview → 미러 재싱크) 경유.
  - 키 우클릭 = 컨텍스트 메뉴 [키 삭제].
  - 빈 곳 더블클릭 = 그 시각에 키 추가(현재 보간값으로 — EvalKeys 결과를 초기값으로 넣어 더블클릭이
    포즈를 튀게 하지 않는다).
- **구현 방식**: 엔진 Persona 계열 `SScrubWidget`/`SAnimTimeline` 재사용을 먼저 검토(grep)하되,
  의존이 무겁거나 모델이 안 맞으면 **커스텀 OnPaint**(FSlateDrawElement 라인/박스)로 직접 그린다 —
  어느 쪽을 택했는지와 이유를 보고에 1줄.
- 키 데이터 소유는 기존 그대로(AssetUserData) — 타임라인은 뷰+조작만. 숫자 키 목록은 유지(정밀 입력).

## 17. v2.3 검증 (Fable)

1. 키 3개 이상일 때 다이아몬드가 정확한 시각 위치에 그려진다(픽셀↔시간 환산 검증).
2. 키 드래그 → Time 변경이 굽기 결과에 반영(스냅 포함), 언두 동작.
3. 더블클릭 추가 키가 그 시각의 보간값을 그대로 이어받는다(포즈 무점프).
4. 룰러·플레이헤드·마커가 탭 리사이즈에 정확히 스케일된다.

---

# v3 — 완전 저작 툴: 커브 채널 아키텍처 (2026-08-09 사용자 결정)

> 요구 4개: ①양손 loc/rot ②파츠별 loc/rot ③총 전체 loc/rot ④손 IK 의 파츠 부착 전환.
> 기존 "베이스 클립에 hand_r 본 트랙 베이크"는 오른손이 총에서 떨어지는 순간 총이 딸려가는
> 구조적 결합(CopyBone) 때문에 탈락 — **모든 저작을 애니메이션 커브 채널로** 바꾼다. Idle 위에서
> 0부터 저작 가능(베이스 클립 불요). 몽타주·클립 양쪽 베이크는 A17 규약 유지(툴이 자동화).

## 18. 커브 채널 계약 (P1 — 코어)

단위 cm/도. 이름 접두 `FPGM_`. 커브가 없으면 해당 채널 무동작(기존 커브 부재=1 규약과 달리
**부재=0 오프셋** — 채널은 가산 오프셋이므로 0 이 중립).

| 커브 | 의미 | 공간 |
|---|---|---|
| `FPGM_Gun_TX/TY/TZ` | 총 이동 오프셋 | **카메라 공간**(X=앞 Y=오른쪽 Z=위) |
| `FPGM_Gun_RP/RY/RR` | 총 회전(pitch/yaw/roll), 피벗=ik_hand_gun(손) | 카메라 공간 축 |
| `FPGM_HL_*` / `FPGM_HR_*` (TX..RR) | 왼/오른손 IK 타깃 오프셋(기본 그립 기준) | **총 공간** |
| `FPGM_HL_Blend` / `FPGM_HR_Blend` | 손 IK 타깃 공간 블렌드: 0=총 그립 ⊕ 오프셋, 1=**부착 파츠** 프레임 ⊕ 오프셋 | 0..1 |
| `FPGM_P_<부착소켓명>_TX..RR` | 해당 파츠의 추가 상대 트랜스폼(기본 소켓+Offset 위에 가산) | 파츠 로컬 |

- 손별 **부착 파츠**(④)는 클립 메타(AssetUserData 확장): `FName LeftHandAttachPartSocket` /
  `RightHandAttachPartSocket` — 클립당 손별 1개(장전=왼손↔탄창이면 충분). 한 클립에서 여러 파츠를
  순차 부착하는 것은 후속 확장(시간 구간 리스트)으로 명시만 해 둔다.
- 파츠 식별 = `FFPSRWeaponPartAttachment::Socket`(툴이 굽는 안정 id `SOCKET_Mount_<id>`, 진화 불변).

## 19. 런타임 소비 (P1 — 코어, C++ 3곳 + ABP 1노드)

1. **`UFPSRFirstPersonArmsAnimInstance`** (기존 게시 구조 확장):
   - `GetCurveValue` 로 §18 커브를 읽어 게시 변수 추가: `GunOffsetLocation/GunOffsetRotation`
     (**컴포넌트 공간** — 카메라 공간 값을 런타임 M(팔 컴포넌트↔카메라 실제 월드 회전, 라이브라 정확)로
     변환), 손 타깃은 기존 `Left/RightGripInGun*` 게시값에 **총 공간 오프셋 합성 + Blend 에 따라 부착
     파츠 프레임으로 재기저**(파츠 프레임 = 캐릭터가 게시하는 파츠 상대 배치, 아래 3).
   - 부재 커브 = 0 규약. 전 계산 owner-local(1인칭 팔은 소유자 전용) — 복제 0, MP 영향 0.
2. **ABP_FP_Base**: CopyBone(hand_r→ik_hand_gun) 뒤에 **TransformBone(ik_hand_gun) 1노드 추가** —
   Component 공간 additive, Translation/Rotation 을 위 게시 변수에 바인딩. 양손 IK 타깃이 총 공간이라
   총 오프셋에 손이 자동 추종(기존 구조 보존). 배선은 AnimGraphService 스크립트(검증된 경로)로.
3. **`AFPSRCharacter`**: ①파츠 조회 API — 부착 소켓명→파츠 컴포넌트/총-공간 프레임(§18 재기저용,
   그립 캐시와 같은 장착 시점 갱신) ②**파츠 커브 적용** — bFirstPersonSplitActive 일 때 Tick 에서
   팔 AnimInstance 의 `FPGM_P_*` 커브값을 파츠 컴포넌트 상대 트랜스폼(기본 소켓+Offset ⊕ 커브)에 적용.
   커브 전무 시 완전 무비용 조기 반환(성능 예산: 파츠 ≤8, owner 1명 — 무시 가능).
4. 3인칭/원격: 변경 없음(기존 바디 몽타주 경로). 서버 로직 접점 0.

## 20. 툴 확장 (P2)

- **멀티채널**: 타임라인에 채널 레인(총/왼손/오른손/파츠×N — 파츠 레인은 현재 무기 DA 의 파츠
  목록에서 자동). 채널별 키 배열은 AssetUserData 확장(`FFPSRGunMotionKey` 에 채널 id 추가가 아니라
  **채널별 키 배열 맵** — 채널마다 독립 타이밍).
- **기즈모 대상 선택**: 뷰포트에서 총/손/파츠 클릭(히트 프록시) 또는 채널 레인 클릭으로 활성 채널
  전환 — 기즈모는 활성 채널의 대상을 조작. 손 채널 기즈모 = IK 타깃 프록시(구체 표시).
- **부착 드롭다운**: 손 채널에 부착 파츠 선택(없음/파츠 목록) + Blend 키.
- **베이크** = §18 커브를 **클립+몽타주 양쪽**에 기록(A17 자동화). 본 트랙은 더 이상 쓰지 않는다
  (기존 hand_r 베이크 경로는 유지하되 v3 클립에서는 미사용 — 마이그레이션: 장전 1호의 hand_r
  베이크는 사용자가 새 툴로 재저작하며 자연 폐기).
- **[새 액션 클립]** 버튼: 길이 지정 → 본 트랙 없는 애디티브 클립(델타 0 = Idle 그대로) 생성 —
  "Idle 기준 0부터 저작"의 진입점. (몽타주 생성+DA 배정 자동화는 후속 — 지금은 기존 몽타주 재사용.)

### 20-1. 데이터 — AssetUserData 확장 (채널별 키 배열 맵)

- 신규 USTRUCT(런타임 모듈 `FPSRGunMotionAuthoringData.h`에 병기 — AUD 클래스가 거기 있으므로):
  - `FFPSRGunMotionChannelKey { float Time; FVector Loc; FRotator Rot; }` — 공간은 채널이 정한다
    (§18: 총=카메라 공간, 손=총 공간, 파츠=파츠 로컬).
  - `FFPSRGunMotionScalarKey { float Time; float Value; }` — Blend 등 스칼라 채널용.
  - `FFPSRGunMotionChannelTrack { TArray<FFPSRGunMotionChannelKey> Keys; }`
- `UFPSRGunMotionAuthoringData` 추가 필드(기존 `Keys`/`bSanitized`/부착소켓 2필드는 **보존** —
  v2 레거시 + 부착 메타는 계속 런타임이 읽는다):
  - `FFPSRGunMotionChannelTrack GunTrack; LeftHandTrack; RightHandTrack;`
  - `TArray<FFPSRGunMotionScalarKey> LeftHandBlendKeys; RightHandBlendKeys;`
  - `TMap<FName, FFPSRGunMotionChannelTrack> PartTracks;` — 키 = 안정 부착소켓 id
    (`FFPSRWeaponPartAttachment::Socket`, 예 `SOCKET_Mount_3`)
- 키 보간 = 기존 §3-3(smoothstep + Slerp)을 채널 무관 공용 함수로 일반화해 재사용. 스칼라도 동일 규칙.

### 20-2. 커브 베이크 (본 트랙 대체 · A17 자동화)

- `FPSRGunMotionBaker` 신규: `static bool BakeCurveChannels(UAnimSequence* Seq,
  const UFPSRGunMotionAuthoringData& Data, FText& OutError)`
  - 각 채널 트랙 → §18 커브명으로 클립에 커브 기록. **커브명은 전부 `FPSRGunMotionCurveNames`
    상수 + `MakePartCurveNames`(런타임 모듈) — 리터럴 중복 금지.**
  - 커브 키 = 저작 키를 클립 프레임레이트로 §3-3 평가해 샘플(툴 프리뷰·런타임 GetCurveValue 가
    같은 모양을 보게). 키 0개 채널 = 해당 커브 **삭제**(부재=0 규약 — 죽은 커브 잔존 금지).
  - 커브 쓰기 API 는 엔진 grep(`IAnimationDataController` AddCurve/SetCurveKeys,
    `FAnimationCurveIdentifier`) — 커브 이관 스크립트(커밋 `406448de` 언저리 Scripts/) 사용례 대조.
- **몽타주 자동 기록(A17)**: 클립 베이크 성공 후 애셋 레지스트리 referencer 로 이 클립을 세그먼트로
  무는 `UAnimMontage` 를 찾아 같은 커브를 몽타주에도 기록.
  - 지원 = 세그먼트 1개·PlayRate 1·시작 0(현 AM_FP_Rifle_Reload 형태): 시간 매핑 항등.
  - 그 외(다중 세그먼트/비-1 레이트/오프셋) = 그 몽타주 스킵 + 사유를 결과에 명시(조용한 불일치 금지).
  - 기록/스킵한 몽타주 목록을 토스트·로그로 보고.
- 부착 드롭다운은 AUD 의 `LeftHand/RightHandAttachPartSocket` 에 쓴다(베이크와 무관, 런타임이 직접 읽음).
- hand_r 본 트랙 베이크(`BakeGunMotion`)·[총 고정화]는 **코드 유지**, v3 채널 저작에선 호출하지 않음 —
  UI 에서 "레거시(v2)" 접힘 섹션으로 이동.

### 20-3. 채널 레인 타임라인 + 활성 채널

- `SFPSRGunMotionTimeline` 확장: 레인 = 총 / 왼손(+Blend 하위 얇은 레인) / 오른손(+Blend) / 파츠×N.
  파츠 레인 = 설정 신규 `PreviewWeaponData`(`TSoftObjectPtr<UFPSRWeaponDataAsset>`, 기본
  `/Game/Weapons/DataTable/DA_Weapon_Rifle`)의 `WeaponParts` 에서 자동(Part=null·Socket=None 제외,
  라벨 = DisplayLabel 우선, 없으면 Socket).
- 활성 채널은 항상 1개(레인 하이라이트). 레인 클릭 = 활성 전환. 키 조작(클릭 선택/드래그 스냅/우클릭
  삭제/더블클릭 보간값 추가/언두)은 v2.3 그대로 레인별 동작 — 커밋은 **기존 MutateKey 단일 훅을 채널
  id 로 파라미터화**해 경유(새 커밋 경로 금지, 재굽기→미러 재싱크 체인 보존).
- 숫자 키 목록(§4-4)은 활성 채널의 키를 표시·편집(Blend 레인은 Time·Value 2열).

### 20-4. 뷰포트 — 대상 클릭 선택 + 조작 프리뷰 (판정은 PIE)

- 프리뷰 씬 추가 구성: 파츠 스태틱메시 컴포넌트 ×N(무기 메시의 Socket+Offset 부착 — DA 값 그대로),
  손 IK 타깃 프록시 = 구체 컴포넌트 2개. 그립 기준점은 캐릭터 그립 캐시와 같은 소스(무기 메시의 그립
  소켓 — `AFPSRCharacter::ComputeGripInGunFrame` 이 읽는 그 소켓명 경로를 grep 해 동일 소켓 사용).
  소켓이 없으면 총 원점 폴백 + 상태줄 한 줄(저작값은 오프셋이라 조작엔 지장 없음).
- 히트 프록시(어셈블러 뷰포트 클라이언트의 HActor 계열 패턴)로 총몸/파츠/손 프록시 클릭 → 활성 채널
  전환(타임라인 하이라이트와 동기).
- 기즈모 = 활성 채널 대상 컴포넌트 조작. 드래그 종료 시 **그 채널의 공간으로 역산**(총 = 잠금 구도
  카메라 공간 §9·§12 그대로 / 손 = 총 공간 / 파츠 = 파츠 로컬) → 현재 스크럽 시각 키 커밋.
- **뷰포트 포즈 반영 = 평가값 직접 적용**: 스크럽·키 변경 시 §3-3 평가 결과를 프리뷰 컴포넌트 상대
  트랜스폼에 가산 적용(총=무기 컴포넌트, 파츠=파츠 컴포넌트, 손=프록시 구체). 런타임과 같은 수학이라
  근사가 아니라 등가 — 단 팔 스킨 포즈(ABP·IK)는 뷰포트에 없으므로 **판정은 PIE 라이브 미러**(v2.2
  유지: transient 프리뷰 클립 재굽기가 커브를 실으므로 미러가 총/손/파츠 전부 실제로 보여준다).

### 20-5. [새 액션 클립]

- 버튼 → 다이얼로그(에셋 이름 + 길이 초). 생성: 팔 스켈레톤(설정 TargetCharacterBP 팔 메시 기준)
  UAnimSequence, **본 트랙 0개**, 30fps, 애디티브 `AAT_LOCAL_SPACE_BASE` +
  `RefPoseType=ABPT_REF_POSE`(트랙 0 → raw=refpose=base → 델타 항등 0 = Idle 그대로.
  A16 함정은 "기존 트랙 삭제" 케이스 — 무트랙+refpose base 는 구조적으로 0. 구현 후 AnimPose 델타
  재계측 0.000 으로 실증, 0 아니면 중단·보고).
- 저장 경로 = `Anims_LPAMG` 하위 + `_GunMotion` 접미 강제(§5 경고 규약 통과 위치).
- 생성 직후 탭이 그 클립을 열고 AUD 부착, `bSanitized=true` 로 시작(본 트랙이 없어 고정화 불요).
- 몽타주 생성·DA 배정 자동화는 범위 밖(§20 원문 유지).

### 20-6. P2 검증 (Fable — §21-2 구체화)

1. 빌드 0 에러(에디터 닫힌 상태) + 신규/변경 리플렉션 필드 명세 대조.
2. 코드 리뷰: 커브명 리터럴 0(전부 상수/MakePartCurveNames) · MutateKey 단일 훅 유지 ·
   TWeakObjectPtr 수명주기 · 몽타주 스킵 사유 보고 경로.
3. 에디터 실사용(사용자와): 레인 표시·대상 클릭 전환·기즈모 3공간 저작·베이크 후 클립+몽타주 커브
   대조·[새 액션 클립] 델타 0·PIE 미러 판정.

## 21. v3 검증 (Fable)

1. P1: 커브를 파이썬으로 수동 저작한 테스트 클립 → PIE 실측 — 총/양손/파츠가 각 채널 커브대로
   움직이고, Blend=1 구간에서 왼손이 파츠를 따라간다. 커브 전무 클립 = 기존과 완전 동일(회귀 0).
2. P2: 툴에서 채널 전환·기즈모 저작·베이크 → 재생 결과가 프리뷰=PIE 동일.
3. 성능: 파츠 적용 경로가 커브 전무 시 조기 반환(프로파일 확인 불요 수준임을 코드로).
