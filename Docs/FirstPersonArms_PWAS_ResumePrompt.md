# 재개 프롬프트 — 1인칭 팔을 PWAS로 전환 (ADR 0003)

> 작성 2026-08-03 · 브랜치 `refactor/character` · 직전 커밋 `b1eebd34`

## 한 줄 목표
1인칭 팔을 **카메라에 붙이고 PWAS(`ABP_FPChar`)로 자체 구동**하도록 바꾼다. 3인칭 바디는 지금 그대로 둔다.

## 먼저 읽을 것
1. `PROGRESS.md` 최상단 — "1인칭 표현 방식 재결정" 절
2. `Docs/Architecture/0002-true-first-person-shared-animation.md` — **뒤집을 대상**. 축 4개 중 2개(1인칭 표현·무기 단일화)가 바뀐다
3. Blender repo `Docs/HANDOFF_NEONV_FPARMS_RESULT.md` (`C:\Users\koras\Desktop\작업\개발작업\블랜더\`) — 팔 메시를 어떻게 만들었고 뭐가 함정이었는지

## 왜 바꾸나 (사용자 판정, 2026-08-03)
**"3인칭 애니를 1인칭으로 쓰니 부자연스럽다."** ADR 0002의 핵심 전제(*"3P 애니가 PWAS 역할을 차지한다"*)가 실플레이로 깨졌다. 관측된 증상 3개가 전부 같은 뿌리다:
- 팔이 카메라 기준으로 너무 낮다
- 팔이 근접 클립면(**10cm**, `CoreGlobals.cpp:260`)을 뚫어 쐐기 모양으로 잘린다 — 실측 `upper_arm_r`이 카메라에서 **5.5cm**, `hand_l` **4.0cm**
- 시선과 몸 방향이 어긋난다(사용자 체감 20°)

**결정 = 안 나-1**: PWAS를 `S_Mannequin` 위에 그대로 두고, 팔 메시만 NeonV 룩으로. Blu 리타게팅(안 다)은 기각 — PWAS 커널이 마네킹 본 이름에 묶여 있다(`clavicle_l`→`shoulder_L`, `upperarm_l`→`upper_arm_L`, `lowerarm_l`→`lower_arm_L`).

## 🎁 이미 있는 것 — 다시 만들지 말 것
`Content/Character/FPArms/NeonV_FPArms` (커밋 `2d6f57ac`, 2026-07-26). ADR 0002가 폐기 결정했지만 **파일은 안 지워졌다.**

| 검증 항목 | 값 |
|---|---|
| 스켈레톤 | `S_Mannequin` (PWAS와 동일) |
| 본 | 161 |
| 섹션 | Body / Jacket / Accessoris (소매를 손목 10cm 위 절단, 손 노출) |
| 바운드 extent | (55.22, 21.80, 31.97) — `SK_FP_Manny_Simple` (55.44, 19.90, 31.20)와 거의 일치 |
| 현재 참조자 | 자기 PhysicsAsset뿐 (놀고 있음) |

PWAS 본체도 생존: `Content/ProceduralWeaponAnimationSystem` **145 에셋** — 무기군 6종 포즈 · FP 재장전 몽타주 · 커널(`ABP_FPChar`: AnimGraph 22 / F_ProceduralAnimations 15 / EventGraph 38) · 프리셋 DA.

## 할 일 (순서)
1. **ADR 0003 작성** — 코드 먼저 만지지 말 것. ADR 0002의 어느 불변식이 죽고 어느 게 사는지 먼저 확정한다
2. `AFPSRCharacter`: `FirstPersonArms`를 **카메라에 attach**(현재는 바디) · `SetLeaderPoseComponent` 제거 · 메시를 `NeonV_FPArms`로
3. 팔 AnimBP = `ABP_FPChar` 자체 구동 (`SetAnimInstanceClass`)
4. **무기 이중화 복원** — 1P(카메라측) + 3P(월드). 무기 DA에 `ArmsAnimInstanceClass`·FP 재장전 몽타주 필드 복원
5. ADS 정렬 재정의 — 무기가 카메라 공간에 오면 `UpdateAimDownSights`의 월드 정렬 전제가 바뀐다
6. 왼손 IK(`Two Bone IK`, `hand_L`)를 어느 그래프에 둘지 결정
7. `Blu_FP_Arms`(Blu 스켈레톤·자동컷, 2026-08-03 제작) **폐기** — 참조 0 확인 후

## 🪤 함정 (전부 실제로 당한 것)
1. **UE는 스켈레톤을 공유하면 레퍼런스 포즈도 공유한다.** 메시 바인드 포즈가 스켈레톤 ref pose와 다르면 그 부위가 강제로 늘어난다 — 실측 48.8cm 메시가 에디터에 **157cm**로 뭉갰다. `NeonV_FPArms`는 이 조건을 이미 만족하므로 **건드리지 말 것**
2. **🚨 마네킹 FP 팔을 Blu 스켈레톤에 임포트하지 말 것.** 2026-08-03에 이 조합으로 **Blu 스켈레톤이 108본 → 65본으로 통째로 교체**됐다(541개 에셋이 참조). "Skeleton Conflicts" 창은 소스가 틀렸다는 신호다 — Done 누르지 말 것. 복구 = 에디터 닫고 `git checkout`
3. **"값이 정상"은 애니가 도는 증거가 아니다.** `AlwaysTickPose`는 그래프 Update만 돌린다 — 변수 프로브가 전부 초록이어도 포즈는 얼어 있을 수 있다. **판정은 뼈 좌표의 시간 변화(span)로, 반드시 움직이는 상태에서**
4. **문서를 출발점으로만 쓸 것.** 이번 세션에 문서를 믿었다가 3번 틀렸다 — P3 "상태기계 가설"(엔진 소스가 부정) · "3P 애니가 PWAS를 대체한다"(실플레이가 부정) · "1인칭 팔 새로 저작 필요"(이미 있었다). 전부 **실제 파일을 열어보니 다른 얘기**였다
5. **BP·애님그래프 노드 편집은 사용자 작업**(2026-08-03 지시). Claude는 조회·진단·단계 가이드까지

## 환경 / 검증
- 엔진 `D:\UnrealEngine\UE_5.7` · 빌드는 **에디터 닫고**:
  `"D:\UnrealEngine\UE_5.7\Engine\Build\BatchFiles\Build.bat" FPSRogueliteEditor Win64 Development -Project="E:\Git_Project\FPSRoguelite\FPSRoguelite.uproject" -WaitMutex`
- 스모크: `UnrealEditor-Cmd.exe <uproject> -unattended -nopause -nullrhi -nosplash -nosound -ExecCmds="Automation RunTests FPSRoguelite.Smoke.ModuleLoads" -TestExit="Automation Test Queue Empty" -abslog=...`
- PIE 중 라이브 계측은 `unreal.register_slate_post_tick_callback`으로 샘플러를 걸고 `builtins`에 결과를 쌓아 다음 호출에서 읽는다(PIE를 블로킹하지 않는다). ⚠️ 샘플러 만료 시간을 넉넉히 줄 것 — 짧게 잡아 사용자가 Play 누르기 전에 꺼진 적 있다
- ⚠️ **PIE 중에는 `AnimGraphService`·`BlueprintService`가 죽는다.** 그래프 조회는 PIE 끄고

## 미해결 잔여 (이번 건과 독립)
- **적 그림자 LOD PIE 실측** — 코드는 커밋됨(`cb5a612d`), 빌드·스모크 통과, 거리별 on/off 실측만 남음
- `wall_topout` f0/f12 포즈 판단
- 4b — 8방향 시작·정지 전환(총 내린 자세 폐기로 저작량 절반)
