# 인계 — 무기 어셈블러 1인칭 트랙 (2026-08-06 갱신)

브랜치 `refactor/character`.

## 지금 상태 한 줄

**검증 트랙은 끝났다**(커밋 `9977af8a`). **1인칭 뷰 v1 은 코드·빌드까지 됐지만 에디터 검증을 안 했다.**
그리고 사용자가 v1 을 보고 **"별도 창 + 게임과 같은 화면비"** 를 요청했다 — 그 설계가 아래 §3 에 다 적혀 있다.

## 1. 끝난 것 (커밋 `9977af8a`)

에디터 실측으로 4항목 전부 통과. **판정은 전부 숫자로 했고 매번 대조군을 뒀다.**

| 항목 | 결과 |
|---|---|
| 손 위치 저장 → 재시작 잔존 | −12.5 → −17.0. `SKEL_LPAMG_Character`(**스켈레톤**)에 기록, 팔 메시 무변경 |
| 다른 무기(SMG) 부착 | `WeaponAttachScale` 1.0 정확 적용, 표시값=실측 |
| 애니 중 그립 불변 | `hand_r` 월드가 움직여도 그립 6칸 비트 고정 |
| 파츠 베이크 왕복 | 드리프트 **0.00004** |

검증 중 발견해 같이 고친 결함 4건은 커밋 메시지에 전문이 있다(애디티브 애니 · 공유 소켓 덮어쓰기 ·
그립 저장 누적 · 설정이 디스크에 안 남던 것).

## 2. 커밋했지만 **에디터 검증 안 한 것** — 1인칭 뷰 v1

빌드는 통과했다. 에디터에서 한 번도 값을 확인하지 않았다.

- 런타임: `AFPSRCharacter::GetFirstPersonViewSetup(FTransform& OutCameraRelativeToArms, float& OutFOV)`
  두 컴포넌트의 **실제 월드 트랜스폼끼리** 상대를 구한다 — 부착 위상을 가정하지 않는다.
  🚨 **CDO 가 아니라 스폰된 인스턴스에서 불러야** 컴포넌트 월드가 의미를 갖는다.
- 에디터: 설정 `PreviewCharacterClass`(기본 `BP_FPSRPlayer`, `Config/DefaultEditor.ini` 에 넣어 둠) ·
  "1인칭 뷰" 체크박스 · 프리뷰 씬에 **스폰 → 읽기 → 즉시 파괴** · 카메라 잠금(매 프레임 되돌리기) ·
  캔버스 조준 기준선.

**첫 검증 때 볼 것**: 읽어온 카메라↔팔 상대값을 실측 기대치 `(-21, 10, -165)`/yaw −95, FOV 90 과 대조.
🚩 그 기대치는 **옛 NeonV 팔 기준**이라 지금 LPAMG 팔과 다를 수 있다 — 다르면 그 자체가 발견이다.

## 3. 다음 작업 — 1인칭 뷰를 **별도 도킹 탭 + 화면비 고정**으로 (사용자 요청, 설계 확정)

> 원문: *"이런식의 뷰말고 따로 뷰포트 창을 열어서 보여줘야할거같네 실제 인 게임화면과 동일해야 맞출수
> 있으니까 해상도에 맞게 QHD, FHD 식으로 화면 비율에 맞게 설정할수있게 작업 뷰포트도 따로 만들고"*

이유: 도킹된 뷰포트는 화면비가 게임과 달라 **FOV 가 같아도 가장자리 기준 구도가 다르다.**

### 핵심 메커니즘 (엔진 소스 확인 완료)

에디터 뷰포트가 종횡비를 고정하는 **유일한** 경로는 "제어 액터 뷰인포"다:

```cpp
// EditorViewportClient.cpp:1158
bool bConstrainAspectRatio = bUseControllingActorViewInfo && ControllingActorViewInfo.bConstrainAspectRatio;
// EditorViewportClient.cpp:1190 부근 — 투영행렬이 여기서 만들어진다
FMinimalViewInfo::CalculateProjectionMatrixGivenView(ControllingActorViewInfo, AspectRatioAxisConstraint, Viewport, ViewInitOptions);
```

→ `bUseControllingActorViewInfo = true` + `ControllingActorViewInfo` 에 Location/Rotation/FOV/AspectRatio/
`bConstrainAspectRatio = true` 를 채우면 엔진이 레터박스까지 넣어 준다. **v1 의 "매 프레임 카메라
되돌리기"보다 정확하고 카메라 잠금도 이걸로 자연히 된다** — v1 의 그 방식은 이걸로 교체할 것.

### 🚩 먼저 알 것: FHD 와 QHD 는 구도가 같다

1920×1080 과 2560×1440 은 **둘 다 16:9** 라 구도가 완전히 동일하다. 가장자리 구도를 바꾸는 건 해상도가
아니라 **종횡비**다. 프리셋은 해상도 이름으로 보여주되 **동작은 종횡비로 묶을 것**
(16:9 = FHD·QHD·4K / 21:9 울트라와이드 / 16:10). 해상도 자체는 픽셀 단위 크기를 볼 때만 의미가 있다.

### 파일 구성 (신규 4 + 수정 3)

- `FPSRWeaponAssemblerHelpers` 에 `ReadFirstPersonSetup(UWorld*, FTransform&, float&, FString&)` **추출**
  (지금은 `FFPSRWeaponAssemblerViewportClient::ReadFirstPersonSetupFromCharacter` 에 있다) — 두 클라이언트가 공유
- `FFPSRWeaponAssemblerFPViewportClient` — 종횡비 고정 + 조준 기준선 전담
- `SFPSRWeaponAssemblerFPViewport` / `SFPSRWeaponAssemblerFPTab` — 뷰포트 + 종횡비 콤보
- 모듈(`FPSRogueliteEditorModule.cpp`)에 **두 번째 노매드 탭** 등록 + `Tools > FPSR` 메뉴 항목

### 🚨 수명주기 — 여기가 제일 위험하다

프리뷰 씬은 `SFPSRWeaponAssemblerTab` 이 `TSharedPtr<FAdvancedPreviewScene>` 로 소유한다(`.h:346`).
`FEditorViewportClient` 는 씬을 **raw 포인터**로 받으므로 씬이 먼저 죽으면 매달린다.

- 1인칭 탭이 씬을 **공동 소유(`TSharedPtr`)** → 매달림 원천 차단
- 조립기 탭 자체는 **`TWeakPtr`** 로 참조
- 🚩 조립기 탭은 노매드 탭이라 닫았다 다시 열면 **새 씬**이 생긴다. 1인칭 탭은 자기가 쥔 씬이 현재
  조립기 탭의 씬과 다르면 안내 문구를 띄우고 렌더를 멈출 것(스테일 빈 씬을 보여주면 안 된다)

## 4. 그 뒤 순서

1. **`SOCKET_Weapon` 실제 저작** — 현재 `(-13, 4, 1)/(p-10,y60,r-15)` 은 **임시**(저장 경로 검증용).
   1인칭 뷰가 제대로 된 뒤 다시 잡는다. ⚠️ 전용 소켓이 없는 무기 **전부**가 공유하는 소켓이다.
2. **왼손 IK** — 정지 소총 자세에서 `hand_l` ↔ 무기 `SOCKET_LeftHand` **28.13cm**(실측).
   🚩 **1번을 먼저 끝내야 한다** — 소켓은 무기에 달렸고 무기는 오른손에 매달리므로, 오른손 그립이
   틀어지면 왼손 소켓도 통째로 끌려간다. 남은 것: `Maintain Effector Rel Rot` 켜기(Details → `IK` 섹션,
   `Allow Stretching` 바로 아래 / `Allow Twist` 바로 위) · Joint Target 축 · 손가락 포즈 ·
   `LeftHandIKWeight` 커브(없어서 코드가 1.0 폴백 → 지금 `NOT bIsReloading` 곱셈으로 우회 중)
3. **PIE 교차확인** — 툴 그립 = 인게임 그립인지, 총 크기가 두 번 곱해지지 않았는지

## 5. 실측으로 확인된 함정 (다시 재지 말 것)

1. **`SOCKET_Weapon` 은 팔 메시가 아니라 스켈레톤(`SKEL_LPAMG_Character`)에 산다.** 메시 패키지만 저장하면
   "저장됨" 뜨고 재시작하면 사라진다. `AddSocket(.., bAddToSkeleton=true)` 는 소켓을 **둘로 복제**하니 금지.
2. **애디티브 애니는 단일노드 인스턴스로 못 본다.** `FP_Rifle_Reload` = `AAT_LOCAL_SPACE_BASE`
   (기준 포즈 `A_FP_Rifle_Pose` frame 0, PWAS 원본도 동일 = 설계대로다). 기준 포즈를 까는 분기가
   `bCanProcessAdditiveAnimations` 로 갈리는데 단일노드 프록시는 false, 프리뷰 프록시는 true
   (`AnimSingleNodeInstanceProxy.h:55,79` / `AnimPreviewInstance.h:39,48`). → `UDebugSkelMeshComponent`
   + `EnablePreview` 로 해결됨. 🚨 그 뒤 `SetAnimationMode(AnimationSingleNode)` 를 부르면 원상복구되니 금지.
3. **`FP_Rifle_Idle`/`ADS` 는 2프레임(0.033초)** 이라 재생해도 안 움직인다. 동작 확인은 `FP_Rifle_Reload`(2.5초).
4. **트랙 79개가 다 있어도 아무것도 증명 못 한다** — 애셋을 의심하기 전에 `additive_anim_type` 부터 읽고,
   "클립에서 계산한 값" ↔ "라이브 프리뷰 값"을 대조하면 애니 문제인지 툴 문제인지 즉시 갈린다.
5. **파츠 소켓은 무기 7종이 공유한다**(같은 바디 메시 + 같은 소켓 이름). 이제 베이크가 기존 소켓을
   안 건드리고 차이를 DA 의 `Offset` 에 쓰므로 안전하지만, **소켓을 직접 고치는 코드를 새로 쓰면 다시 터진다.**
6. **`DefaultConfig` 설정은 `SaveConfig()` 로 저장되지 않는다** → `TryUpdateDefaultConfigFile()`.
   (부작용: ini 주석이 지워진다. 설명은 UPROPERTY 주석에 둘 것.)

## 6. 규명 못 한 채 남긴 것

- **조건부 리시트가 실행되지 않았다.** 베이크 직후 바디를 소켓에 다시 앉히는 코드를 "부착 부모/소켓명
  일치" 조건으로 걸었는데, 측정상 조건 3개가 모두 참인데도 동작하지 않았다(빌드 반영은 objs·DLL
  타임스탬프로 확인함). 원인 미상인 채 **조건을 안 쓰는 방식으로 우회**했다(굽기 전 그립을 떠 뒀다가
  되세움). 비슷한 증상이 또 나오면 추측하지 말고 **로그부터 심을 것.**
- **첫 저장에서 한 번 튀는 현상**(두 번째부터 멱등, 오차 0.000008). 유력 가설 = 그립 숫자 칸이
  `AllowSpin` 이라 **키 입력마다 커밋**된다(`-13` 을 치면 `-1` 시점에 한 번 튐) — 저장이 아니라 입력 위젯
  동작. **미확인.**

## 7. 커밋 안 된 콘텐츠 (이번 트랙이지만 내용 검증 안 함)

`ABP_FPArms` · `ABP_FP_Base`(신규) · `BP_FPSRPlayer` · `L_MainMenu` · `TestWorld` ·
`ABP_FirstPerson` 삭제. 사용자 결정으로 **어셈블러 관련 애셋만** 먼저 커밋했다(`9977af8a`).

## 8. 잡건

- `dev/null/` 은 Git LFS 훅이 잘못된 경로에 설치된 쓰레기다(`>dev/null` 리다이렉트 흔적). 지워도 된다.
- 작업 지침: **에디터·툴 작업은 정확성 우선**(비용을 근거로 덜 정확한 안을 고르지 말 것).
  트레이드오프 논의는 인게임 영향이 있을 때만. — 사용자 지시 2026-08-06
