# PROGRESS — 진행 현황 (라이브 핸드오프 문서)

> 다른 세션/다른 AI가 **즉시 이어받기** 위한 단일 진행 현황 문서.
> **작업 단계를 끝낼 때마다, 그리고 중단 전 반드시 이 파일을 갱신하고 커밋한다.**
> 확정 설계는 `DESIGN.md`, 상세 이력은 `git log --oneline`.

**최종 갱신: 2026-05-28**

## 한 줄 요약
P1(1인칭 전투 수직 슬라이스) 진행 중. **이동/시점/점프 PIE 검증 완료.** 무기 시스템(P1-4) 작성됨(빌드 전).

## 완료 (커밋·검증됨)
- **P0** 스캐폴드 — UE5.7 경량 C++, 플러그인 enable, GameplayTags, Git/LFS, 빌드 OK, 스모크
- **P1-0** 코어 — GameMode/GameState/PlayerController/PlayerState, ASC는 PlayerState 소유
- **P1-1** GAS 글로벌 속성 — `UFPSRHealthSet`, `UFPSRCombatSet` (DOREPLIFETIME_CONDITION_NOTIFY)
- **P1-2** EnhancedInput — IMC는 `AFPSRPlayerController::SetupInputComponent`에서 추가. **이동·시점·점프 PIE 확인됨**
- **P1-3** 1인칭 카메라 + Separated Arms — FP팔 `OnlyOwnerSee` / 3P바디 `OwnerNoSee`

## 진행 중 (WIP, 빌드 전)
- **P1-4 무기 기반** — `Source/FPSRoguelite/{Public,Private}/Weapon/`
  - `FPSRWeaponTypes.h` (`EFPSRWeaponArchetype`, `FFPSRWeaponStatBlock`)
  - `FPSRWeaponDataAsset` (Archetype / BaseStats / FireAbility)
  - `FPSRWeaponInventoryComponent` (3슬롯, 서버권위, Push Model, 장착 시 발사 GA 부여)
  - ⚠️ **컴파일 검증 안 됨** — P1-5~7 작성 후 1회 빌드 예정 (HEAD가 빌드 안 될 수 있음)

## 다음 단계 (순서대로)
1. **P1-5 발사 GA**: `UFPSRGameplayAbility` 베이스 + `GA_WeaponFire_Hitscan` (카메라 히트스캔 트레이스 + 디버그 라인 + 히트 로그). 데미지는 P1-7 브릿지에서 연결
2. **입력 배선**: Character에 `UFPSRWeaponInventoryComponent` 부착 + 기본 무기 지급, `IA_Fire`→발사 GA 활성, `IA_EquipSlot1~3`→슬롯 전환 (클라→서버 RPC)
3. **P1-6 근접 GA**: `GA_WeaponMelee` (스윕 오버랩)
4. **P1-7 적 + 데미지 브릿지**: `AFPSREnemyBase`(경량 Pawn) + `UEnemyHealthComponent`(Push Model) + 최근접 플레이어 추격 + 디버그 스폰(콘솔커맨드). 데미지 브릿지 = 플레이어 GAS 계산 → 적 `HealthComponent.ApplyDamage`(비-GE)
5. **헤드리스 자체검증**(데미지 브릿지/인벤토리) → **1회 빌드** → **사용자 PIE 1회 테스트**

## 빌드 / 검증 방법
- 빌드(에디터 닫고): `"D:\UnrealEngine\UE_5.7\Engine\Build\BatchFiles\Build.bat" FPSRogueliteEditor Win64 Development -Project="E:\Git_Project\FPSRoguelite\FPSRoguelite.uproject" -WaitMutex`
- 헤드리스 검증: `"D:\UnrealEngine\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" <uproject> -unattended -nopause -nullrhi -nosplash -nosound -ExecCmds="Automation RunTests FPSRoguelite.Smoke.ModuleLoads" -TestExit="Automation Test Queue Empty" -abslog=...`
- 새 UCLASS 다수면 Live Coding 불가 → 풀빌드 (에디터 닫아야 함, DLL 잠김)
- 입력 에셋(IA) 생성은 Python 커맨드릿 가능: `Scripts/gen_input_assets.py`

## 확정 사항 / 주의점
- 무기 교체 = 숫자키 **1/2/3** (`IA_EquipSlot1~3`)
- 사격장 맵 = 사용자가 `L_Sandbox` 생성 (File→New Level→Basic + PlayerStart) — **사용자 대기 작업**
- **UE5.7 IMC 매핑은 Python `set_editor_property("mappings")` 미반영 → 에디터 수동** (IA 에셋 생성은 Python OK)
- 카드 선택 = 전원 대기 (타임아웃 없음)
- 입력 `[Input]` 진단 로그(Warning) → P1-4 빌드 때 다운그레이드 예정
- CommonUI `LogUIActionRouter` 에러 → P3에서 `CommonGameViewportClient` 설정 시 해결 (현재 무해)
- **MCP(unreal) 인증 실패로 미사용** → UBT 빌드 + 헤드리스 자동화로 검증
- 모델 정책: 구현=Haiku 위임 / 검증(빌드·diff·스모크)=메인(Opus) 직접
