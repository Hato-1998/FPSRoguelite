# PROGRESS — 지금 하고 있는 것

> 다른 세션/다른 AI가 **즉시 이어받기** 위한 문서. **여기엔 진행 중인 것만 남긴다.**
> 끝난 작업 → `Docs/WorkLog.md` · 증상→원인→해결 → `Docs/Troubleshooting.md`
> 구조 결정 → `Docs/Architecture/*.md` · 확정 설계·기획 → `Game.md`(SSOT 허브) · 커밋 상세 → `git log --oneline`
>
> **작업을 끝낼 때마다, 그리고 중단 전 반드시 이 파일을 갱신하고 커밋한다.**
> 한 항목이 완료되면 **여기서 지우고 `Docs/WorkLog.md` 맨 위로 옮긴다.**

**최종 갱신: 2026-08-05 · 브랜치 `refactor/character`**

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
| 툴 | 무기 어셈블러에 **`팔 보기`** 토글 + **`손 위치 저장`** 버튼 (`Tools > FPSR > 무기 파츠 조립기…`) |

**검증 결과** — Idle(−23.07, 28.43, 149.70) · ADS(−12.72, 24.57, 154.64) · Reload@1.0s(−21.07, 29.45, 147.98).
Idle→ADS 변화 방향이 원본과 세 축 모두 일치. 재장전도 시점마다 원본과 같은 방향으로 변한다.
> 🪤 **"트랙이 있다"로 리타게팅을 검증하지 마라.** 트랙 79개가 다 있는데 값만 정적인 사고를 실제로 냈다.
> 재검증은 `Scripts/lpamg_control_test.py`(대조군) + `lpamg_verify_pose_values.py`(값)로.

## ⏭️ 다음 (순서대로)

1. **어셈블러 툴에 애니 선택 UI** ← 사용자 요청(2026-08-05). 지금은 프리뷰 포즈를
   `Config/DefaultEditor.ini` 의 `PreviewArmsPose` 로만 지정한다. 툴바에서 고를 수 있어야 한다.
   - ⚠️ 지금 구조는 **정지 포즈 전제**다(무기를 손에 얹는 배치가 토글/무기변경 시점에 한 번만 일어남).
     움직이는 애니를 고를 수 있게 하려면 "기즈모 편집을 유지한 채 손을 따라가는" 방식이 필요하다 —
     소켓 기준 **오프셋**으로 들고 매 틱 재적용하는 식. 설계부터 해야 한다
2. **`SOCKET_Weapon` 조정** — 지금 값은 LPAMG 총 기준이라 Synty 모듈러 소총과 안 맞을 가능성이 크다.
   위 툴에서 `팔 보기` + `전체 이동` 으로 잡고 `손 위치 저장`
3. **구도** — `FirstPersonArms` rest `(-21, 10, -165)` yaw −95 는 **옛 NeonV 팔 기준**이다. 팔 비율이
   달라 어긋날 가능성이 크다. 이건 툴이 아니라 PIE/렌더로 봐야 한다
4. **PIE 검증** — 팔이 소총 자세로 서는가 / 손가락이 늘어나 보이지 않는가(리타게터가 비율을 흡수하지만
   **아직 눈으로 확인 안 했다**) / 내 그림자 왼팔 / 동료 화면 무기 / 다운→관전

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
