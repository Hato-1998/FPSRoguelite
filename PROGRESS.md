# PROGRESS — 진행 현황 (라이브 핸드오프 문서)

> 다른 세션/다른 AI가 **즉시 이어받기** 위한 단일 진행 현황 문서.
> **작업 단계를 끝낼 때마다, 그리고 중단 전 반드시 이 파일을 갱신하고 커밋한다.**
> 확정 설계는 `DESIGN.md`, 상세 이력은 `git log --oneline`.

**최종 갱신: 2026-05-28**

## 한 줄 요약
**P1(1인칭 전투 수직 슬라이스) 코드 전부 완료 + 빌드·헤드리스 부팅 검증 통과.** → **사용자 PIE 테스트 대기** (무기/적은 PIE 미확인; 이동·시점·점프는 확인됨).

## 완료 (커밋·빌드 검증됨)
- **P0** 스캐폴드 / **P1-0** 코어 / **P1-1** GAS 글로벌 속성 / **P1-2** EnhancedInput(이동·시점·점프 PIE 확인) / **P1-3** 1인칭 카메라+Separated Arms
- **P1-4** 무기 기반 — `Weapon/`(Types/DataAsset/InventoryComponent): 3슬롯 서버권위, Push Model, 장착 시 발사 GA 부여
- **P1-5** 발사 GA — `AbilitySystem/Abilities/`(FPSRGameplayAbility 베이스 + GA_WeaponFire_Hitscan): 카메라 히트스캔 + 디버그 라인 + 크리티컬 + 적 데미지
- **P1-6** 근접 GA — GA_WeaponMelee: 전방 구체 오버랩 다중 타격
- **P1-7** 적 — `Enemy/`(FPSREnemyBase 경량 Pawn + FPSREnemyHealthComponent): 최근접 추격, 엔진 큐브 placeholder, 데미지 브릿지(GAS 계산→HealthComponent.ApplyDamage), 콘솔커맨드 `FPSR.SpawnEnemies [N]`
- **통합**: Character에 인벤토리 부착 + 기본무기 지급(서버) + IA_Fire(클릭당 1발)/IA_EquipSlot1~3(서버 RPC) 배선
- **빌드 성공 + 헤드리스 부팅·스모크 통과** (Fatal 0). 무기 DataAsset 미존재 에러는 예상된 것(아래 사용자 작업)

## ⏳ PIE 테스트 대기 (사용자 확인 필요 항목)
- 좌클릭 사격 → 노란 디버그 라인 + 적 처치 / 근접(칼 장착) → 청색 구체 + 처치 / 1·2 무기 전환 / `FPSR.SpawnEnemies 5` 적 스폰·추격

## 사용자 대기 작업 (PIE 테스트 전 필요)
1. **`L_Sandbox` 맵**: File→New Level→Basic, PlayerStart 배치, 기본맵 설정(또는 그 맵에서 Play)
2. **무기 DataAsset 2개** (`/Game/Weapons/`, 우클릭→Miscellaneous→Data Asset→`FPSRWeaponDataAsset`):
   - `DA_Weapon_Rifle`: Archetype=FullAuto, BaseStats.Damage 예 12, **FireAbility=GA_WeaponFire_Hitscan**
   - `DA_Weapon_Knife`: Archetype=Melee, BaseStats.Damage 예 30, MeleeRadius 175, **FireAbility=GA_WeaponMelee**
   - 생성하면 Character 생성자 에러(Failed to find DA_Weapon_*) 사라지고 기본무기 지급됨

## 다음 단계
- **PIE 테스트 통과 → P1 완료**
- **(신규) P1.5 사격 메커니즘 / 슈팅 감각** — DESIGN §4-2. 데이터 드리븐: FullAuto hold-to-fire 루프, 반동(상하/좌우)+패턴, 확산/블룸, 탄약/재장전, ADS. **타이밍 사용자 결정 대기(P2 전 vs P4)**
- **P2**: SpawnDirector + Flow-Field + Pooling + Significance (적 300+ 안정). DESIGN §13·§15
- (P2에서) 적 이동을 Flow-Field+배치로 교체, 풀링 도입, 데미지/체력 numbers 튜닝

## 빌드 / 검증 방법
- 빌드(에디터 닫고): `"D:\UnrealEngine\UE_5.7\Engine\Build\BatchFiles\Build.bat" FPSRogueliteEditor Win64 Development -Project="E:\Git_Project\FPSRoguelite\FPSRoguelite.uproject" -WaitMutex`
- 헤드리스 검증: `UnrealEditor-Cmd.exe <uproject> -unattended -nopause -nullrhi -nosplash -nosound -ExecCmds="Automation RunTests FPSRoguelite.Smoke.ModuleLoads" -TestExit="Automation Test Queue Empty" -abslog=...`
- 새 UCLASS 다수면 Live Coding 불가 → 풀빌드(에디터 닫아야 함). 입력 IA 생성은 `Scripts/gen_input_assets.py`

## 확정 사항 / 주의점
- 무기 교체 = 숫자키 **1/2/3** (`IA_EquipSlot1~3`) / 사격=좌클릭(클릭당 1발; full-auto 연사 cadence는 후속)
- **UE5.7 IMC 매핑은 Python `set_editor_property` 미반영 → 에디터 수동** (IA 에셋 생성은 Python OK)
- 카드선택 = 전원 대기(타임아웃 없음)
- 잔여 로그: PlayerController `[Input] Added DefaultMappingContext`(Warning, 1회성) — 다음 빌드 때 Verbose로 다운그레이드
- CommonUI `LogUIActionRouter` 에러 → P3에서 `CommonGameViewportClient` 설정 시 해결(현재 무해)
- **MCP(unreal) 인증 실패로 미사용** → UBT 빌드 + 헤드리스 자동화로 검증
- 모델 정책: 구현=Haiku 위임 / 검증(빌드·diff·스모크)=메인(Opus) 직접
