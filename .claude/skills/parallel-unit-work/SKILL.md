---
name: parallel-unit-work
description: >-
  FPSRoguelite의 Docs/TaskPrompts_Master.md 유닛(V0~V3·U1~U16·W1)을 다중 세션 병렬로 진행할 때
  worktree 분리·코드 핫스팟 회피·콘텐츠 배타 배분·머지-전-검토(§B-2/§B-3)를 자동 적용하는 셋업 가이드.
  새 유닛 작업을 시작하거나 phase/ 브랜치를 새로 분기하려 할 때, 여러 유닛을 동시에 돌릴 때,
  또는 작업 실행 프롬프트의 맨 앞에서 "병렬 작업"을 상정해야 할 때 반드시 먼저 사용할 것.
  TaskPrompts_Master 유닛·phase 브랜치·worktree·다중 세션·동시 작업이 언급되면 트리거.
---

# 병렬 유닛 작업 셋업 (FPSRoguelite)

이 프로젝트는 `Docs/TaskPrompts_Master.md`의 유닛을 **여러 세션에서 동시에** 진행한다. 의존성상 독립이라도 ① 코드 핫스팟(같은 파일) ② 콘텐츠 바이너리(LFS 머지 불가) ③ 빌드/PIE 단일 병목 때문에 "그냥 동시 작업"은 머지 충돌·회귀·에셋 유실을 부른다. 이 스킬은 그걸 막는 셋업과 완료 규칙을 강제한다.

> **단일 진실 출처**: 상세 매트릭스는 `Docs/TaskPrompts_Master.md` §B-2(병렬 가이드)·§B-3(검토·머지 프로토콜). 이 스킬은 그 실행 절차다. 충돌·예외 판단이 필요하면 그 두 절을 연다.

## 0. 시작 전 필독
작업 착수 전 항상: `Game.md` + `PROGRESS.md` + 해당 유닛이 지정한 도메인 `Docs/SSOT/*.md` + **`Docs/TaskPrompts_Master.md` §B-2·§B-3**. 그다음 아래 절차.

## 1. worktree 분리 (절대 규칙)
여러 세션이 같은 작업 디렉터리를 공유하면 한 세션의 브랜치 전환이 다른 세션 워킹트리를 바꿔 버린다(특히 진행 중 미커밋 코드 파괴). **각 유닛은 자기 worktree에서:**

```bash
git worktree add ../FPSR-<키워드> -b phase/<유닛-브랜치> main
# 예) U16: git worktree add ../FPSR-spinup-lmg -b phase/p4c-spinup-lmg main
```
→ 그다음 그 폴더(`E:\Git_Project\FPSR-<키워드>`)에서 새 세션을 열고 작업. 한 워크트리에서 브랜치만 전환하면 새 UCLASS 다수로 매번 풀 리빌드(Live Coding 불가).

## 2. 코드 핫스팟 — 같은 파일이면 직렬 강제
아래는 여러 유닛이 동시에 건드리는 파일. **같은 그룹은 동시 세션 금지**(직렬로).

| 파일 | 유닛 | 처리 |
|---|---|---|
| `Weapon/FPSRWeaponFireComponent.{h,cpp}` | U16(스핀업)·V3(IsAiming getter)·U3a(발사 경로) | **발사계 직렬** |
| `AbilitySystem/.../FPSRGA_WeaponFire_Hitscan` + `Combat/FPSRCombatStatics.{h,cpp}` | U16·U3a | **발사계 직렬** |
| `Weapon/FPSRWeaponTypes.h`(FFPSRWeaponStatBlock) | U16 | 먼저 머지 후 타 유닛 rebase |
| `Core/FPSRGameMode.cpp`(EndRun) | U2(defeat)·U3(victory) | **U2 먼저 머지 → U3** |

**안전한 동시 조합**: 발사계 1개(U16 등) + 비발사계(V1 사각오디오 / U2 패배배선) = 파일 안 겹침 → OK.

## 3. 콘텐츠(.uasset/.umap) 배타 배분
LFS 바이너리는 3-way 머지 불가 → 같은 에셋을 두 세션이 건드리면 한쪽이 통째 유실. 에셋 단위로 한 세션만 점유.
- `WBP_GameHUD`/크로스헤어 = V3 (U12는 V3 머지 후 확장)
- 보스 BP/DA·L_Sandbox 보스 배치 = U4
- **`BP_FPSRPlayer` = 충돌 1순위, 단독 점유**
- 새 LMG `DA_Weapon_*` = U16 (신규 파일이라 충돌 적음)

## 4. 완료의 정의 = 머지가 아니다 (§B-3)
세션이 main에 머지하는 것이 완료가 아니다. **완료 = 자체 빌드 + 헤드리스 스모크 통과 + phase push + 완료 보고/PR**. main 머지는 **사용자 검토·승인 후**에만.

```bash
# 빌드 (에디터 닫고)
"D:/UnrealEngine/UE_5.7/Engine/Build/BatchFiles/Build.bat" FPSRogueliteEditor Win64 Development -Project="<worktree>/FPSRoguelite.uproject" -WaitMutex
# 헤드리스 스모크
"D:/UnrealEngine/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "<worktree>/FPSRoguelite.uproject" -unattended -nopause -nullrhi -nosplash -nosound -ExecCmds="Automation RunTests FPSRoguelite.Smoke.ModuleLoads" -TestExit="Automation Test Queue Empty" -abslog="<worktree>/Saved/smoke.log"
```
빌드 산출물은 worktree별로 분리되니 동시 빌드해도 `-WaitMutex`로 안전 직렬화된다.

## 5. 검토·머지 (머지 전 검토 모델)
```
구현 → 자체 빌드+스모크 통과 → phase push + PR 생성 → 사용자 검토(코드=PR diff/Codex, 콘텐츠=worktree PIE)
→ 수정은 같은 브랜치 재push → 승인 → --no-ff main 머지 → 머지 후 미머지 phase 브랜치 git rebase main
```
- 머지는 한 번에 하나. 권장 순서: `U16 → U2 → U3a → V1 → V3 → U3 → U4 → W1`.
- **문서/PROGRESS 변경을 작업 코드 브랜치에 섞지 말 것** — `--ff-only`로 문서를 올리면 그 브랜치의 미검증 작업 커밋이 딸려 올라간다(실측 사고). 문서는 main 직접 또는 별도 `docs/` 브랜치.
- Codex 머지게이트: `Scripts/codex-review.ps1 -Base main` (머지 시점 일괄).

## 6. 모델 정책 / 검증 책임
- 구현 = Haiku 위임 / **검증(빌드·스모크·diff·Codex) = Opus 직접**(§6-5). 단 U11(멀티 루프)·서버권위 RPC 등은 Opus 직접 구현.
- "검증 없이 완료 보고 금지"(CLAUDE.md). 에셋 경로 C++ 하드코딩 금지.

## 체크리스트 (작업 시작 시 그대로 따라가기)
1. [ ] Game.md + PROGRESS.md + §B-2/§B-3 + 유닛 도메인 SSOT 읽음
2. [ ] `git worktree add ../FPSR-<키워드> -b phase/<브랜치> main`으로 별도 폴더 분리
3. [ ] §2 핫스팟·§3 콘텐츠 점유 확인 (동시 작업 중인 다른 유닛과 안 겹치는지)
4. [ ] 플랜 우선 → 사용자 승인 → 구현
5. [ ] 자체 빌드 + 헤드리스 스모크 통과
6. [ ] phase push + PR/완료 보고 (main 머지 안 함)
7. [ ] 사용자 검토·승인 후 `--no-ff` 머지 + PROGRESS·TaskPrompts §B ✅ + 콘텐츠 동반 커밋 질문
