# PWAS 1인칭 절차 무기애니 통합 — 인계/재개 프롬프트 (복붙용)

> **작성 2026-07-10** (Synty 아트 파일럿 후속, FP팔 스레드에서 조사 완료). 이 문서 = PWAS(Procedural Weapon Animation System) 절차 무기애니를 우리 `AFPSRCharacter`에 통합하는 **전용 세션용** 인계.
> **왜 전용 세션**: 조사 결과 PWAS는 "바로 붙이는 컴포넌트"가 아니라 **거대한 자기완결형 FP 시스템**이라, 통합이 며칠짜리 대공사 + 기존 CrystalRecoil/ADS 회귀 위험. 파일럿 후속으로 급히 할 게 아님.
> **선행 필독**: `Game.md` + `PROGRESS.md` + `Docs/SSOT/CombatWeaponCard.md`(§2-3~2-5 무기·사격감) + 메모리 `recoil-crystalrecoil-adapter`·`synty-anime-cel-art-pivot`·`weapon-da-pack-code-mapping`·`investigate-before-recommending`.

---

## 0. 목표
애니메이션 없는 **정적 Synty 무기**에 1인칭 절차 무기애니(발사 시 총 킥·스웨이·호흡·재장전 딥·이동 바운스)를 부여. Synty 무기는 스켈레톤/본 없는 정적 프롭이라 스켈레탈 몽타주(볼트 사이클) 불가 → **절차적 트랜스폼 애니**로 "살아있는 총" 구현.

## 1. 현재 상태 (2026-07-10 시점)
- **`BP_FPSRPlayer` FirstPersonArms = `SK_FP_Manny_Simple`(마네킹 팔, 로봇형) + `A_FP_Rifle_Pose`(single-node) + render_custom_depth ON(SRS 셀) + 플레이스홀더 무발광 머티리얼(`BasicShapeMaterial` 오버라이드).** = **동작하는 플레이스홀더**(커밋 안 됨).
- 무기 손 그립 소켓 미배선(무기가 손에 안 잡힘 — 마네킹은 `ik_hand_gun` 본 사용, 우리 `WeaponAttachSocketName`="SOCKET_Weapon").
- 반동/확산/ADS = 기존 CrystalRecoil/C++ 정상 동작.
- 3P 바디 = BroBot(SK_BCC_01_BroBot). ⚠️ FP=로봇마네킹 vs 3P=BroBot 불일치(원래 의도는 애니 Blu 통일).

## 2. ⚠️ 조사로 밝혀진 핵심 — PWAS 아키텍처 (통합 난이도의 근거)
`AC_ProceduralWeaponAnimationSystem`(ActorComponent) = **수십 개 per-tick 절차함수**:
`F_ProceduralRecoil`·`F_ProceduralCameraRecoil`·`F_ProceduralRecoilInfluence`·`F_ProceduralAim`·`F_ProceduralBreathing`·`F_ProceduralWalking`·`F_ProceduralRun`·`F_ProceduralWalk/Run`·`F_ProceduralMovement`·`F_ProceduralWallCollision`·`F_ProceduralHolsterSpring`·`F_CrouchSpring`·`F_ChangeStance`… + `F_ProceduralAll`(마스터) + `ApplyPreset(DA)` + `UpdateValues(...)` + `F CombinedProcedural`(출력 CombinedLoc/Rot).
- **캐릭터가 매 틱 입력을 먹여주고(MouseX/Y·이동·IsRunning·Aiming·Firing) 출력(CombinedLoc/Rot)을 팔·무기 트랜스폼에 재적용**해야 동작. ABP(`ABP_FPChar`, S_Mannequin 스켈)가 컴포넌트 출력을 읽음.
- 즉 **캐릭터를 PWAS 중심으로 재구성**하는 구조. 데모 `BP_FPCharacter`(SpringArm 카메라 리그 + SM_FPArms + AC 컴포넌트)가 그 형태.
- 프리셋 토글: `WeaponRecoil`/`CameraRecoil`/`RecoilInfluence`(bool) + 구조체 `S_ProceduralRecoil`/`S_ProceduralAnimations`/`S_ProceduralAdjuster`.

## 3. 기존 시스템과의 충돌점 (조사 확정, file:line)
현재 `AFPSRCharacter` 1P: `FirstPersonCamera`(캡슐 자식) → `FirstPersonArms`(스켈) → `WeaponMesh1P`/`WeaponMeshStatic1P`(별도 컴포넌트, `SOCKET_Weapon` 소켓 부착).
| PWAS 요소 | 우리 것(유지 대상) | 충돌·처리 |
|---|---|---|
| 카메라 절차 반동(`F_ProceduralCameraRecoil`) | **CrystalRecoil = 컨트롤 회전 반동**(서버 패리티, `CRRecoilComponent.cpp:184-204`) | 이중적용+서버 트레이스 desync → **프리셋 `CameraRecoil=OFF`** |
| ADS 조준 포즈(`F_ProceduralAim`) | **C++ `UpdateAimDownSights`** = 매 틱 `FirstPersonArms->SetRelativeLocationAndRotation`(`FPSRCharacter.cpp:1437`), `WeaponFire::TickComponent`서 호출(`FPSRWeaponFireComponent.cpp:551-553`) | 컴포넌트-space 하드라이트 vs ABP bone-space = 매 틱 싸움→지터. **하나가 aim 소유**(C++ 유지 권장, PWAS ADS OFF) |
| 스웨이/킥 | C++ ADS 스웨이+발사킥(`FPSRCharacter.cpp:1404-1435`) | 중첩 → PWAS 힙 스웨이만 살리고 ADS는 C++ |
| 확산 heat | **CrystalRecoil `GetHeatSpread`**(순수 데이터) | PWAS 확산 없음 → 무충돌, CrystalRecoil 유지 |
| 볼트 애니 | `FPSRWeaponAnimInstance.FirePartRecoilOffset`(무기 스켈) | Synty=정적이라 무의미, PWAS 무기-트랜스폼 킥으로 대체 |

**결정(제1원리)**: **CrystalRecoil = 카메라/조준 반동+확산(서버권위, 유지) · C++ = ADS 정밀정렬 · PWAS = 메시-space 코스메틱 필(무기 킥·스웨이·호흡·재장전 딥·이동 바운스)만.** 겹치는 seam 2개(`FPSRCharacter.cpp:1437`, `FPSRWeaponFireComponent.cpp:551-553`)를 게이트/조율.

## 4. 두 구현 경로 (평가 후 택1)
- **경로 A — PWAS 그래프트**: `AC_ProceduralWeaponAnimationSystem` 컴포넌트를 `BP_FPSRPlayer`에 추가 + `ABP_FPChar`를 FirstPersonArms animClass로 + 캐릭터 Tick서 PWAS 함수에 입력 먹이고 출력 적용 + `CameraRecoil=OFF` + ADS/입력 충돌 해소. **대공사·취약**(PWAS BP 그래프 역설계·재배선). 데모 `BP_FPCharacter` EventGraph가 레퍼런스.
- **경로 B — C++ 네이티브 경량(권장 평가)**: PWAS 수학을 통째 이식하지 말고, **원하는 코스메틱 효과(무기-메시 스웨이·바운스·호흡·발사 킥)만 C++로 경량 재구현**해 기존 FP 시스템에 확장. 무기 메시 컴포넌트에 스프링 오프셋 적용(이동속도·마우스델타·발사이벤트 입력). **우리 아키텍처에 정합·충돌 없음·유지보수 쉬움.** PWAS는 파라미터/느낌 레퍼런스로만.

> 권고: **경로 B 먼저 타당성 평가**. PWAS 전체를 커스텀 캐릭터에 그래프트하는 건 제1원리(프로덕션 구조·유지보수)에 안 맞을 공산 큼. B로 "총이 살아있는" 필의 80%를 훨씬 싸게 얻을 수 있는지 프로토타입.

## 5. 무기 손 그립 (별도 선결)
Synty 무기가 손에 잡히려면: 마네킹 `ik_hand_gun` 본에 무기 부착(우리 `WeaponAttachSocketName`을 `ik_hand_gun`으로 or 스켈레톤에 `SOCKET_Weapon` 소켓 추가—헤드리스 소켓추가는 protected라 스켈레톤 에디터 UI 필요) + 무기 그립-원점 오프셋 튜닝. Synty 병합무기 `/Game/_SyntyPilot/SM_SM_SyntyRifle_Assembled`(Muzzle/Aim 소켓 있음).

## 6. 애니 Blu 팔 대안 (별개 트랙, 원한다면)
FP를 **Blu 애니 팔**로 통일하려면 = **Blender 무료 리스킨**(Blu 팔 지오메트리를 마네킹 스켈에 바인딩). ⚠️ Blu **Blender 리그가 UE 리타겟을 이김**(본 축 규약 불일치 → auto-align 과보정·수동회전 shear, 스케일은 정상). 리타겟(A) 경로는 사실상 막힘. `RTG_Manny_to_Blu`(FK-only) + `IK_Blu`/`IK_Manny` 스캐폴드가 `/Game/_SyntyPilot`에 있음(리타겟은 팔 늘어남). Blender 미설치(winget 설치 가능). 상세=[[synty-anime-cel-art-pivot]].

## 7. 에셋 경로
- PWAS = `/Game/ProceduralWeaponAnimationSystem/`(`Components/AC_ProceduralWeaponAnimationSystem`·`Animations/AnimBlueprints/ABP_FPChar`·`Data/DataAssets/Presets/DA_WPP_{Rifle,SMG,LMG,Shotgun,Sniper,Pistol}`·`Demo/FPManny/SK_FP_Manny_Simple`+`S_Mannequin`).
- 파일럿 스로어웨이 = `/Game/_SyntyPilot/`(gitignore, RTG/IK/병합무기/테스트DA/셀맵). 채택 스택은 이미 main 커밋(`ff191fec`·`1902fd11`·`2c396f6e`).
- 무기 DA = `/Game/Weapons/DataTable/DA_Weapon_*`.

## 8. 착수 프롬프트 (새 세션 복붙)
```
Game.md + PROGRESS.md 읽고, Docs/PWAS_FPArm_Integration_ResumePrompt.md 전체를 읽어라.
[작업] 1인칭 절차 무기애니(정적 Synty 무기용). 플랜모드 우선.
[선결] §2-4 정독 — PWAS는 자기완결 FP시스템이라 그래프트가 대공사. §4 경로 B(C++ 네이티브 경량)부터 타당성 프로토타입 권장. §3 충돌점(CrystalRecoil 카메라반동·C++ ADS cpp:1437·WeaponFire:551) 반드시 유지·게이트.
[방식] 조사 먼저(memory investigate-before-recommending). 구현=Sonnet 위임 가능·검증=Opus. 서버권위 반동/확산(CrystalRecoil)·ADS(C++) 회귀 0. 커밋은 검증·승인 후.
[선결2] 무기 손 그립(§5) 배선 먼저 = 눈으로 검증 가능.
```
