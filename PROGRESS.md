# PROGRESS — 지금 하고 있는 것

> 다른 세션/다른 AI가 **즉시 이어받기** 위한 문서. **여기엔 진행 중인 것만 남긴다.**
> 끝난 작업 → `Docs/WorkLog.md` · 증상→원인→해결 → `Docs/Troubleshooting.md`
> 구조 결정 → `Docs/Architecture/*.md` · 확정 설계·기획 → `Game.md`(SSOT 허브) · 커밋 상세 → `git log --oneline`
>
> **작업을 끝낼 때마다, 그리고 중단 전 반드시 이 파일을 갱신하고 커밋한다.**
> 한 항목이 완료되면 **여기서 지우고 `Docs/WorkLog.md` 맨 위로 옮긴다.**

**최종 갱신: 2026-08-06 · 브랜치 `refactor/character` (+ 리팩토링 1회차는 `refactor/mechanical-cleanup`)**

---

# 🟠 대기 — 리팩토링 1회차가 올린 **사용자 결정 9건**

> 전문 = **[`Docs/Refactor_20260806_Report.md`](Docs/Refactor_20260806_Report.md) §7** ·
> 전체 구조 지도 = **[`Docs/ProjectStructure_Report.md`](Docs/ProjectStructure_Report.md)**
> 브랜치 `refactor/mechanical-cleanup`(커밋 `5f496e4f`)은 **검증 통과 상태로 머지 대기**.

코드 수정분(18파일, 동작 변경 0)은 끝났고 `-DisableUnity` 풀빌드로 검증했다.
남은 것은 **판단이 필요해서 일부러 안 고친 것들**이다. 우선순위 순:

1. 🔴 **패키지 빌드에서 Push Model 이 꺼진다** — 소스 엔진 빌드로 갈지 결정 (`FPSRoguelite.Target.cs`)
2. 🔴 **아무 클라가 파티 전원을 로비로 강제 이동** — 게이트를 무엇으로 할지 (`ServerRequestReturnToLobby`)
3. `ServerAckTopology` 상한 없는 단조 증가 — 클램프/무시/킥 정책
4. Server RPC 12개 `WithValidation` 없음 — "조용히 무시" ↔ "연결 끊기" 정책
5. `CachedRecoil` 반사 구멍 — `UPROPERTY` vs `TWeakObjectPtr`
6. 적 `bDead` 복제 — 계약을 문자 그대로 볼지
7. `GameplayMessageSubsystem` 브로드캐스트 배열 복사 — 재설계할지
8. `GetOwnedWeapons()` 로 스칼라 질문 2곳 — 새 헬퍼 추가할지
9. 매직 넘버 승격 후보 — 특히 `GlobalAliveCap=200`

**그리고 문서↔코드 드리프트 13건**(리포트 §8). 가장 시급 = `Game.md §1` 의 적 상한 서술이
실제(**200 / 단일맵 192**)와 다른 것. 문서를 코드에 맞출지 코드를 문서에 맞출지가 건마다 달라서 일괄 처리 불가.

---

# 🔴 현재 트랙 — 1인칭 팔 = **구매 리그(LPAMG) + PWAS 리타게팅** ([ADR 0006](Docs/Architecture/0006-first-person-arms-purchased-rig-retargeted.md))

> **먼저 읽을 것**: [ADR 0006](Docs/Architecture/0006-first-person-arms-purchased-rig-retargeted.md)(0004·0005 대체) ·
> 경위 전문은 `Docs/WorkLog.md` 최상단 · 함정은 `Docs/Troubleshooting.md` **A11 · D7~D9 · G7~G8**.

**자체 스켈레톤 트랙(ADR 0004/0005)은 접었다.** 사용자 실플레이 판정으로 NEON-V 팔을 버리고,
LPAMG 팩의 1인칭 팔로 갈아탔다. **에셋은 안 지웠다** — 배선만 되돌리면 복귀한다.

## ✅ 지금 돌아가는 것 (전부 실측 검증됨)

| | |
|---|---|
| 팔 | `SK_LPAMG_Arms_Base_Smooth` · `SKEL_LPAMG_Character` 79본(UE4 마네킹 규약) · `SOCKET_Weapon` 있음 |
| 리타게팅 | `IK_PWASManny` + `IK_LPAMGArms` → `RTG_PWAS_to_LPAMG` (체인 **15/15** 매핑) |
| 애니 | `Content/Character/FPArms/Anims_LPAMG/FP_Rifle_{Idle,ADS,Reload}` ← PWAS 원본에서 리타게팅 |
| 배선 | `ABP_FP_Base`(부모 `FPSRFirstPersonArmsAnimInstance`, 그래프 = 시퀀스→Slot→Root) → `BP_FPSRPlayer.FirstPersonArms` |
| 툴 | 무기 어셈블러 (`Tools > FPSR > 무기 파츠 조립기…`): **`팔 보기`** · 애니 선택/재생/스크럽 · 손 소켓 저작(**`손 위치 저장`**) — 2026-08-06 에디터 실측 검증 완료 |
| 툴 | **무기 1인칭 뷰** (`Tools > FPSR > 무기 1인칭 뷰…`): 조립기와 **같은 프리뷰 씬**을 플레이어 눈 시점 + 게임과 같은 화면비(레터박스)로. ⚠️ **에디터 미검증** |

**어셈블러 검증 결과(2026-08-06, 전부 숫자로 판정)** — 손 위치 저장 → 에디터 재시작 후 값 **잔존**
(−12.5 → −17.0, `SKEL_LPAMG_Character` 에 기록 · 팔 **메시**는 무변경) · SMG 부착(scale 1.0 정확 적용) ·
애니 재생 중 그립 **비트 고정**(hand_r 월드가 움직여도 그립 6칸 불변) · 파츠 베이크 왕복 드리프트 **0.00004**.

**검증 결과** — Idle(−23.07, 28.43, 149.70) · ADS(−12.72, 24.57, 154.64) · Reload@1.0s(−21.07, 29.45, 147.98).
Idle→ADS 변화 방향이 원본과 세 축 모두 일치. 재장전도 시점마다 원본과 같은 방향으로 변한다.
(2026-08-06 재실행 — Idle/ADS/Reload 세 값 모두 소수점까지 그대로 재현됨.)

> 🚩 **위 Reload 좌표는 "그 시점의 손 위치"가 아니다.** `FP_Rifle_Reload` 는 **애디티브**
> (`AAT_LOCAL_SPACE_BASE`, 기준 포즈 = `A_FP_Rifle_Pose` frame 0) 라서 트랙에 든 값이 절대 포즈가 아니라
> **차분**이다 — PWAS 원본 `A_FP_RifleReload` 도 같으므로 리타게팅이 속성을 올바르게 보존한 것이다.
> 원본↔리타게팅 **대조**로는 여전히 유효하지만(양쪽 다 차분), 포즈 좌표로 읽으면 안 된다.
> Idle/ADS 는 `AAT_NONE` 이라 그대로 절대 포즈다.
> 🪤 **"트랙이 있다"로 리타게팅을 검증하지 마라.** 트랙 79개가 다 있는데 값만 정적인 사고를 실제로 냈다.
> 재검증은 `Scripts/lpamg_control_test.py`(대조군) + `lpamg_verify_pose_values.py`(값)로.

## ⏭️ 다음 (순서대로)

> **상세 인계는 `dev/handoff-assembler.md`** — 1인칭 별도 탭 설계(엔진 API·수명주기 위험)까지 다 적혀 있다.

0. 🔴 **1인칭 뷰 = 별도 탭 + 화면비 고정. 빌드는 통과, 에디터 검증 안 함.** 켜서 계기 숫자를 대조할 것 —
   `Tools > FPSR > 무기 1인칭 뷰…`(또는 조립기 탭의 `1인칭 뷰 열기` 버튼). **판정 핵심 = 화면비 프리셋을
   바꿔도 "세로"가 58.72° 에서 안 움직이는 것.** 전체 기대치 표는 인계문서 §3.
   - v1 의 구도 계산 자체는 **검증 끝**(대조군 2경로, 차이 0.000000) — 카메라↔팔 = `(8.13, 21.79, 165)`/yaw +95.
     🚩 BP 에 적힌 팔 값 `(-21, 10, -165)`/yaw −95 와는 **서로 역행렬**이다(규약 두 개, 헷갈리지 말 것).
   - 🚨 이전 인계문서가 틀렸던 3건은 고쳐서 반영했다 — ①화면비 고정은 카메라를 **안** 잠근다(되돌리기 유지)
     ②프리셋은 종횡비만 바꾸면 안 되고 FOV 재계산 필요 ③축 제약을 명시 안 하면 사람마다 구도가 달라진다.
     근거·엔진 파일·행번호는 인계문서 §2.

1. ~~**어셈블러에 1인칭 뷰 + 조준 기준선**~~ ← 완료(v1 → 별도 탭으로 교체, 미검증). 원 요청 맥락만 유지:
   *"핸드 위치를 조정했지만 그건 1인칭 화면 기준이 아니라서 PIE 해보니 이상해"* ·
   *"그립 및 총기 위치는 1인칭 뷰 + 크로스헤어가 되어있어야 수정가능해"* ·
   *"실제 인 게임화면과 동일해야 맞출수 있으니까 해상도에 맞게 … 화면 비율에 맞게"*
   - ADS 구도(`UpdateAimDownSights` 재현)는 여전히 다음 단계로 미룬다.
2. **`SOCKET_Weapon` 실제 저작** — 현재 값 `(-13, 4, 1)/(p-10,y60,r-15)` 은 **임시**다(저장 경로 검증용).
   1번이 생긴 뒤 1인칭 구도로 다시 잡아야 한다. ⚠️ 이 소켓은 전용 소켓이 없는 무기 **전부**가 공유한다.
3. **왼손 IK 마무리** — 정지 소총 자세에서 `hand_l` ↔ 무기 `SOCKET_LeftHand` 거리 **28.13cm**(2026-08-06 실측).
   단, 소켓은 무기에 달려 있고 무기는 `SOCKET_Weapon` 으로 오른손에 매달리므로 **2번을 먼저 끝내야** 한다
   (오른손 그립이 틀어지면 왼손 소켓도 통째로 끌려간다). 남은 것:
   `Maintain Effector Rel Rot` 켜기(Details → `IK` 섹션, `Allow Stretching` 바로 아래 / `Allow Twist` 바로 위) ·
   Joint Target Location 축 · 손가락 포즈 저작 · `LeftHandIKWeight` 커브(없어서 코드가 1.0 폴백 →
   지금은 `NOT bIsReloading` 곱셈으로 우회 중)
4. **PIE 검증** — 팔이 소총 자세로 서는가 / 손가락이 늘어나 보이지 않는가 / 내 그림자 왼팔 /
   동료 화면 무기 / 다운→관전. 툴에서 본 그립과 인게임 그립이 같은지도 여기서 교차확인

## 🪤 어셈블러 — 규명 못 한 잔여 2건 (2026-08-06)

- **조건부 리시트가 실행되지 않았다.** 베이크 직후 바디를 소켓에 다시 앉히는 코드를
  "부착 부모/소켓명이 일치하면"이라는 조건으로 걸었는데, 측정상 조건 3개가 모두 참인데도 동작하지 않았다
  (바이너리·objs 타임스탬프로 빌드 반영은 확인함). **원인 미상인 채로 우회**했다 — 지금은 조건 없이
  "굽기 전 그립을 떠 뒀다가 굽고 나서 되세운다"로 바꿨고, 게이트가 저장 버튼과 같은 술어(`HasGripFrame()`)라
  버튼이 눌렸으면 반드시 성립한다. 비슷한 증상이 또 나오면 추측 말고 로그부터 심을 것.
- **첫 저장에서 한 번 튀는 현상**이 사용자 관측으로 남아 있다(두 번째부터는 멱등, 실측 오차 0.000008).
  유력 가설은 그립 숫자 칸이 `AllowSpin` 이라 **키 입력마다 커밋**되어(`-13` 을 치면 `-1` 시점에 한 번 튐)
  저장이 아니라 입력 위젯 동작이라는 것 — 미확인.

## 🧹 정리 대상 (알고 남겨 둔 것)

- `BP_FPSRPlayer.WeaponMeshStatic` 에 **옛 PWAS 데모총 `SM_M4`** 가 박혀 있다. Synty 모듈러로 갈아탄 뒤
  잔재. 지금은 `WeaponMesh`(스켈레탈)가 쓰이므로 화면엔 안 나올 가능성이 크지만 확인 필요
- `Content/Character/Player/ABP_FPArms` 가 **ObjectRedirector** 로 남아 있다 (Fix Up Redirectors 대상)
- 옛 자체 스켈레톤 에셋 — `Content/Character/FPArms/SK_NeonV_FPArms*` · `NeonV_FPArms*` ·
  `Anims/FP_Rifle_{Idle,ADS}`(죽은 스켈레톤). **롤백용으로 의도적으로 남김.** 확정되면 정리
- `Anims_LPAMG` 폴더명 — 내용은 PWAS 애니라 오해를 부른다(실제로 한 번 물어봄). `Anims/` 로 합칠지 결정 필요

## ⚠️ 되돌리기 비용이 다른 것 하나

**LPAMG 팩은 git 미추적으로 들어왔다.** 정리하며 지운 2,261개는 **git 으로 복구가 안 된다**(팩 재다운로드).
남긴 팔 40개는 커밋에 넣어 뒀다. 나머지 팩은 전부 추적 중이라 `git checkout` 으로 복구된다.

---

# ⏳ 대기 목록 (코드는 끝났고 사람 손/확인만 남은 것)

각 항목의 전체 맥락은 `Docs/WorkLog.md`의 같은 제목 절에 있다.

| 항목 | 남은 것 |
|---|---|
| **4c 슬라이드·벽 포즈** | 사용자 재저작 — 슬라이드 저자세 · `wall_topout` f0/f12 판단 |
| **적 그림자 LOD** | 코드 커밋됨(`cb5a612d`)·빌드/스모크 통과, **거리별 on/off PIE 실측**만 |
| **에어 스트레이프** | PIE (`WallJumpMaxSpeed` 1400이 BP 오버라이드에 안 먹히는지 포함) |
| **근접 3번 슬롯** | 맨손 손맛 세트(몽타주·사운드·VFX, 칼도 비어 있음) + PIE 10항목 |
| **BP 그래프 정리** | 에디터에서 눈으로 확인 (`WBP_Lobby`·`WBP_RunHUD`·`BP_Door`·`ABL_Blu_W2_Rifle`) |
| **폐루프 디렉터 P0a-0** | PIE 2-client 라이브훅 게이트 → 통과 시 P0a-1 |
| **키 바인딩** | 휠 행 키 종류(`MouseWheelAxis` 축 키 ↔ `IA_Jump` Boolean) |
| **4b 로코모션** | 8방향 시작·정지 전환(클립 40개+) + Split_Jumps 108 리타게팅 |
| **무기 DA 8개** | `BodyAnimLayerClass`·`WeaponAttachScale`·`LeftHandSocket` 미배정(필요할 때) |
| **`Blu_FP_Arms` 폐기** | 참조 0 확인 후 삭제 (`Content/Characters/Blu/SkeletalMeshes/Blu_FP_Arms/`) |
