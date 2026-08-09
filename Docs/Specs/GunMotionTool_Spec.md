# 총모션 스튜디오 — 인게임 ImGui 저작 명세 (재설계 v1, 2026-08-09)

> 보드 행: "총 모션 클립 저작 파이프라인 + 에디터 탭 툴" · 브랜치 `phase/fparms-gunanchor-ik`
> **이전 명세(v1~v3, 에디터 탭 계열)는 전면 폐기됐다**(철거 커밋 `4228f37b`, 구명세는 git 히스토리).
> 구현 = Sonnet 축자, 검증 = Fable. 명세 이탈 금지, 갭 발견 시 중단·보고.
> 통합 대상: 보드 "1인칭 무기 뷰모델 애니메이션(홀스터+상태별 모션)" 행 — §6 스키마가 그 작업의 입력.

## 0. 원칙 (바꾸면 안 되는 전제)

- **저작 화면 = 판정 화면 = PIE 게임 화면.** 에디터 탭·프리뷰 씬·구도 캡처·라이브 링크 미러 금지(전부 이전 세대의 브릿지 — 존재 이유가 없다).
- 사용자 확정 흐름(축자): ①PIE에서 무기를 들면 그 무기 Idle이 베이스 ②타임라인 + 화면에서 부품/손 클릭 → 이동 기즈모(회전 전환), 무기 전체는 별도 모드 버튼 ③손은 [IK 붙이기]+소켓명 Text(없으면 노티) ④키 사이 보간 ⑤저장 시 이름·경로 지정.
- gun-anchor 구조(CopyBone hand_r→ik_hand_gun, 총공간 양손 IK, `727a4d7b`)는 불가침. `LeftHandIKWeight` 부재=1 규약·A16 고정화·ADS 불가침.
- 채널 분업: **총 전체 = hand_r 애디티브 본 트랙(런타임 0)** / **손·파츠 = FPGM 커브(최소 런타임 §4)** / **상태 포즈 = DA 수치(§6, 소비는 홀스터 행)**.

## 1. 모듈·파일 배치

### 런타임 `FPSRoguelite` (데이터 계약만)
- `Public/Anim/FPSRGunMotionCurves.h`(+.cpp) — §2 커브명 상수(네임스페이스 `FPSRGunMotionCurves`) + `MakePartCurveNames(FName)`. 리터럴 중복 금지의 단일 소스.
- `Public/Anim/FPSRGunMotionStudioData.h`(+.cpp) — `UFPSRGunMotionStudioData : UAssetUserData`(§2 키 영속, 클립 부착). 런타임 소비는 부착 메타(§2-3)만.
- `FPSRWeaponDataAsset.h` — ①`ArmsIdleAnim`(`TSoftObjectPtr<UAnimSequence>`, 1인칭 팔 Idle — 스튜디오 베이스 표시·검증용, ABP는 현행 유지) ②§6 상태 포즈 필드(FFPSRProceduralWeaponMotionProfile 확장).

### 에디터 `FPSRogueliteEditor` (스튜디오 본체 — PIE는 에디터 프로세스라 여기 있으면 출하 배제가 구조적)
- `Private/GunMotionStudio/FPSRGunMotionStudio.h/.cpp` — 세션 싱글턴(콘솔 명령 등록·틱·상태 소유).
- `Private/GunMotionStudio/FPSRGunMotionStudioUI.cpp` — ImGui 드로우(타임라인·패널). 로직과 분리.
- `Private/GunMotionStudio/FPSRGunMotionStudioBaker.h/.cpp` — §3 베이크(순수 로직, Slate/ImGui 의존 금지).
- `Private/GunMotionStudio/ImGuizmo/` — ImGuizmo 벤더링(단일 .h/.cpp, MIT — LICENSE 동봉). 업스트림 원본 그대로, 수정 금지.
- `FPSRogueliteEditor.Build.cs` — `ImGui` 모듈 의존 추가. 커브 쓰기용 의존은 엔진 소스 grep 후 최소로.

## 2. 데이터 계약

### 2-1. 커브 (단위 cm/도, 부재=0. 총 채널은 없다 — 총은 본 트랙)
| 커브 | 의미 | 공간 |
|---|---|---|
| `FPGM_HL_TX..RR` / `FPGM_HR_TX..RR` | 왼/오른손 IK 타깃 오프셋(그립 기준) | 총 공간 |
| `FPGM_HL_Blend` / `FPGM_HR_Blend` | 0=그립⊕오프셋, 1=부착 파츠 프레임⊕오프셋 | 0..1 |
| `FPGM_P_<부착소켓>_TX..RR` | 파츠 추가 상대 트랜스폼(기본 Socket+Offset 위 가산) | 소켓 프레임(이동)·저작오프셋 좌곱(회전) |

### 2-2. 저작 키 (AssetUserData `UFPSRGunMotionStudioData`)
- `FFPSRStudioKey { float Time; FVector Loc; FRotator Rot; }` — 공간은 대상이 정한다(총=카메라 공간, 손=총 공간, 파츠=소켓 프레임).
- 필드: `GunKeys` · `LeftHandKeys`/`RightHandKeys` · `TMap<FName, FFPSRStudioTrack> PartTracks`(키=`FFPSRWeaponPartAttachment::Socket`).
- 손 부착: `FFPSRStudioAttachSpan { float Start; float End; FName PartSocket; }` 배열(손별) — [IK 붙이기]가 만든다. 베이크가 Blend 커브 램프(스팬 경계 ±BlendTime, 기본 0.1s)로 변환.
- 보간 = smoothstep(a²(3−2a)) + Slerp. 첫/마지막 키 밖 = 그 키 값. (검증된 §3-3 수학 — 철거 전 `FPSRGunMotionBaker::EvalChannelKeys`, git 히스토리 `2c78cb73` 참조 이식.)

### 2-3. 런타임이 읽는 것 (전부 여기까지)
커브(§2-1) + AUD의 손별 부착 소켓(스팬이 아니라 **베이크 시점에 클립당 손별 대표 소켓 1개로 축약** — `LeftHandAttachPartSocket`/`RightHandAttachPartSocket`). 키·스팬 원본은 툴 전용.

## 3. 베이크 (FPSRGunMotionStudioBaker, 저장 시 1회)

### 3-1. 신규 클립 생성
팔 스켈레톤 기준, 본 트랙 0, 30fps, `AAT_LocalSpaceBase` + `ABPT_RefPose`(트랙 없는 본 = 델타 항등 0 — A16의 "트랙 삭제" 함정과 무관, 구조적으로 0). 길이 = 타임라인 길이. 이름·경로 = 저장 다이얼로그 값(`_GunMotion` 접미 강제).

### 3-2. 총 → hand_r 본 트랙
프레임별 t: 카메라 공간 키 평가 `(O_t, O_q)` → **라이브 변환**(에디터 탭 세대의 CDO 근사 아님):
```
M = ArmsComp 월드회전⁻¹ * Cam 월드회전          (PIE 실측 — 저작 세션 시작 시 1회 캐시)
P = lowerarm_r 컴포넌트 공간 회전(Idle 포즈)      (동 시점 1회 캐시)
Ocomp = M⊗O_t,  Oq' = M·O_q·M⁻¹
d_rot = P⁻¹·Oq'·P,  d_t = P⁻¹⊗Ocomp
track_rot[i] = d_rot · refpose_rot(hand_r)  /  track_t[i] = refpose_t + d_t
```
`IAnimationDataController::SetBoneTrackKeys`(하나의 bracket). 캐시 시점 = 저작 세션 시작(Idle 정지 상태) — 세션 중 불변.

### 3-3. 손·파츠 → 커브
키/스팬을 클립 프레임 격자로 §2-2 보간 샘플 → `FPGM_*` 커브 기록. 키 0개 채널 = 커브 삭제(부재=0, 죽은 커브 금지). **클립+참조 몽타주 양쪽**(A17): 몽타주는 애셋 레지스트리 referencer 중 단일세그먼트·PlayRate1·시작0만 지원, 그 외 스킵+사유 보고.

### 3-4. 몽타주 배정 (저장 다이얼로그 체크박스)
체크 시: `AM_<클립명>` 몽타주 생성(DefaultSlot) 또는 기존 선택 → 현재 무기 DA의 `ArmsReloadMontage`에 배정(장전 용도일 때 — 대상 필드 콤보: ArmsReloadMontage/ArmsEquipMontage/없음). 미체크 = 클립만 저장.

## 4. 런타임 소비 (P2 — 코어, 이것이 전부)

1. `UFPSRFirstPersonArmsAnimInstance`: `ComputeHandIKTarget`(손별) — §2-1 커브 읽어 `Left/RightGripInGun*` 게시값에 합성: **이동 = 총 공간 가산, 회전 = 좌곱**. Blend>0이면 부착 파츠의 **라이브 프레임**으로 재기저(파츠가 커브로 움직이는 중이면 그 결과 — `라이브 = Result·Base⁻¹·Cached`, §21-1 실측 교훈). 부재 커브 = 0, 전 계산 owner-local(복제 0).
2. `AFPSRCharacter`: ①파츠 조회(부착 소켓→컴포넌트/총공간 프레임 캐시 — 장착 시 갱신) ②`ApplyWeaponPartCurves` — `FPGM_P_*`를 파츠 상대 트랜스폼에 적용(**이동=저작 Base에 가산, 회전=Base에 좌곱** — 매 프레임 Base에서 새로, 누적 금지), 커브 소멸 시 Base 복원(무상태), 커브 전무 시 조기 반환. 6커브 중 하나라도 있으면 적용(TX 단독 게이트 금지 — 실측 교훈).
3. 틱 자리 = `UFPSRWeaponFireComponent::TickComponent`(팔 애님 후행 보장 — UpdateAimDownSights와 같은 블록).
4. ABP·3인칭·서버 접점 0. (철거된 v3과 같은 의미론이지만 총 채널·카메라 M·TransformBone 노드가 없다 — 이 명세가 소유하는 새 작성.)

## 5. 스튜디오 세션 (인게임 ImGui)

- **토글**: 콘솔 명령 `FPSR.GunMotionStudio`(에디터 빌드 전용). PIE 로컬 `AFPSRCharacter`+장착 무기 없으면 사유 로그 후 무동작.
- **ImGui**: `FImGuiModule::Get().FindOrCreateSessionContext()` → 틱마다 `ImGui::SetCurrentContext` 후 드로우(플러그인 README·데모 사용례 grep 후 정확한 프레임 훅 확정). ImGuizmo는 같은 컨텍스트에 `ImGuizmo::BeginFrame` + `Manipulate`(view/proj = PIE 카메라 — `APlayerController::GetPlayerViewPoint`+FOV로 구성, 엔진 좌표계↔ImGuizmo 행렬 변환 명시 구현).
- **세션 상태**: 진입 시 ①월드 일시정지(`UGameplayStatics::SetGamePaused`) ②WIP 클립(§3-1 transient 사본)을 팔 AnimInstance에 슬롯 다이내믹 몽타주로 재생+`Montage_Pause` ③M/P 캐시(§3-2) ④입력 모드 UI(마우스 커서 표시, ImGui가 입력 소비). 종료/PIE 종료 시 몽타주 정지·일시정지 해제·전 참조 해제(전부 `TWeakObjectPtr`, 매 틱 유효성 검사 — 에러 금지).
- **스크럽** = `Montage_SetPosition` + 유지 Pause. 키 변경 시 WIP 클립 재베이크(§3 전체, transient — ms 단위) → 몽타주 재시작(블렌드 0.05) 후 위치 복원(실증 기법).
- **대상 선택**: 화면 클릭 → 파츠 컴포넌트들·손 IK 타깃(총공간 그립 위치를 월드로 투영한 구체 반경)·무기 바디를 스크린 투영 근접 판정(콜리전 무관 — 파츠는 NoCollision). 선택 대상 하이라이트 = CustomDepth 또는 ImGui 오버레이 마커(구현 재량, 1줄 보고).
- **기즈모 커밋**(드래그 종료 시): 월드 델타를 대상 공간으로 역산 — 총=카메라 공간(M 캐시), 손=총 공간(무기 컴포넌트 현재 월드회전), 파츠=소켓 프레임(무기 월드회전·소켓 상대회전 — **저작 오프셋 회전은 기준에서 배제**, §4-2 합성과 정합) → 현재 시각 키 갱신(±1프레임)/추가. **증분 커밋**(드래그 시작 스냅샷 대비 델타를 기존 평가값에 가산).
- **[IK 붙이기]**: 손 선택 상태에서 소켓명 Text + 버튼 → 무기 메시(+파츠)에 소켓 존재 검증, 없으면 ImGui 노티. 성공 시 현재 시각부터 AttachSpan 시작, [떼기]로 종료.
- **타임라인**: 룰러+플레이헤드+선택 대상의 키 다이아(전 대상 키는 흐리게 병기) + 스팬 바(부착 구간). 클릭 스크럽·키 드래그(프레임 스냅)·우클릭 삭제·재생/정지. ImGui 커스텀 드로우(DrawList) — 외부 시퀀서 위젯 의존 금지(단일 파일 유지).
- **무기 전체 모드**: 툴바 토글 버튼 — 기즈모 대상을 무기 바디(=총 채널)로 강제.

## 6. 상태 포즈 모드 (통합 — 저작만, 소비는 홀스터 행)

- `FFPSRProceduralWeaponMotionProfile` 확장 필드(DA, 전부 EditAnywhere·툴이 기록):
  `FFPSRWeaponStatePose { FVector Offset; FRotator Tilt; }` — `SlidePose`(+`float SlideBobScale=0`) · `AirbornePose`(+`float AirborneBobScale`) · `HolsterPose`(+`float HolsterDuration=0.25`).
- UI: 모드 콤보(액션 클립 / 상태 포즈). 상태 포즈 선택 시 타임라인 숨김, 상태 콤보(슬라이드/공중/홀스터) + 무기 전체 기즈모만 — 잡은 트랜스폼을 [DA에 저장] 버튼으로 해당 필드에 기록(`FScopedTransaction`+패키지 dirty, DA는 애셋이라 저장 다이얼로그 불요 — 저장 여부 확인만).
- 소비(UpdateAimDownSights 상태 레이어·홀스터 게이트·발사 동기화)는 홀스터 행 범위 — 이 명세는 스키마와 저작 UI까지.

## 7. 함정·금지 (구현 전 필독)

- `Docs/Troubleshooting.md` A16(트랙 삭제≠델타0·base 직접 샘플 금지)·A17(커브는 클립+몽타주 양쪽).
- PIE 중/직후 `delete_asset`·Force Delete 금지(에디터 즉사). transient WIP 클립은 파괴하지 않고 참조 해제만.
- 커브명 리터럴 금지(전부 §1 상수). 에셋 경로 하드코딩 금지(경로류는 상수가 아니라 다이얼로그/DA에서).
- 에셋 쓰기는 `FScopedTransaction`+`Modify()`. 저장은 사용자 확인 다이얼로그 경유.
- ImGui 입력 소비 중 게임 입력 차단(캡처 플래그 확인), 세션 밖 틱 비용 0(콘솔 토글 OFF = 완전 무비용).
- 이전 세대 코드 재도입 금지 — 참조는 git 히스토리 열람까지만(파일 부활 금지).

## 8. 검증 (Fable)

1. P2 런타임: 파이썬 수동 커브 테스트 클립 → PIE 실측(손 재기저·파츠 이동·복귀 0·무커브 회귀 0).
2. 스튜디오: PIE에서 토글 → 오버레이·기즈모 표시(P0b 스모크) → 의도 흐름 5단계 엔드투엔드(사용자): 무기 들기→부품 클릭 저작→[IK 붙이기]→보간 확인→저장(몽타주 배정)→R키 장전에서 재생 = 저작 그대로.
3. 상태 포즈: 슬라이드 틸트 1건 DA 수치 기록 확인(diff).
4. 성능: 토글 OFF 상태 오버헤드 0(코드로 증명), 런타임 커브 경로 조기 반환.
