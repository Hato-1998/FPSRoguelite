# PROGRESS — 진행 현황 (라이브 핸드오프 문서)

> 다른 세션/다른 AI가 **즉시 이어받기** 위한 단일 진행 현황 문서.
> **작업 단계를 끝낼 때마다, 그리고 중단 전 반드시 이 파일을 갱신하고 커밋한다.**
> 확정 설계·기획·코드구조·규칙은 `Game.MD`(SSOT), 상세 이력은 `git log --oneline`.

**최종 갱신: 2026-05-30**

## 한 줄 요약
P1 전투 슬라이스 + **P1.5-A 사격코어(FullAuto/반동/확산)** 코드 완료·빌드 통과. **하드코딩 경로 제거(BP 참조 패턴) 리팩터 완료.** **문서 통합 완료(Game.MD = SSOT).** → **사용자가 BP 3종 생성 후 full-auto PIE 테스트 대기**, 이후 P1.5-B.

## 🔴 새 세션 우선 작업

**1) `Game.MD` 작성 + 문서 통합 — ✅ 완료 (2026-05-30)**
   - `Game.MD`를 단일 SSOT 본문으로 작성(기획·설계·기술스택·코드구조·규칙·구현현황·로드맵·디버그인벤토리).
   - `DESIGN.md`·`AI_DESIGN_HANDOFF.md` → 내용 흡수 후 **stub**("Game.MD로 이전됨").
   - `CLAUDE.md`·`AGENTS.md` → **포인터 축소**(Game.MD·PROGRESS.md 읽기 + 절대금지 3원칙).
   - `PROGRESS.md` 분리 유지. → **AI가 읽는 본문 = Game.MD + PROGRESS.md 2개.**
**2) 리뷰 루프 1회차 — ✅ 완료 (2026-05-30)** — `GameConfirm.md`(외부 AI 작성) 9개 변경안 검토 → 전부 장르 방향성 부합·안티편향 위반 없음 확인 → **Game.MD에 반영 + 로드맵 재구성 완료**. 사용자 결정 2건 반영: ① 초반 페이싱=오프닝 카드 시드, ② 미션 모디파이어 해금을 정비시간 카드로 선택.
   - **반영 요약**: 프리즈 폐지→레벨업 스택+정비시간(§2-2) / 무한 예비탄약(§2-4-2) / 무기 버림·교체 없음(§2-4) / 아군 오사 10%+호스트 토글(§2-10) / 적 이속 ±10% 편차·원거리 투사체 규격·공격토큰(§2-6) / 수동부활 DBNO·충돌무시 대시(§2-13) / 게임필·위협 인디케이터(§2-14). 로드맵 P1.5~P7 재구성(§7-3).
   - **다음 리뷰 회차**: 새 `GameConfirm.md`가 오면 동일 절차로 비교·반영.

**in-flight(병행/이후):** P1.5-A 빌드 완료 → 사용자 BP 3종 셋업(아래 '사용자 대기 작업') → full-auto PIE 테스트 → P1.5-B(탄약/재장전/ADS)
**git:** 최신 커밋 `0ab8c07`. 사용자 콘텐츠(L_Sandbox 맵, DA_Weapon_Rifle/Knife @ `Content/Weapons/DataTable/`)는 디스크 존재·**미커밋**(untracked)

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

## 사용자 대기 작업 (PIE 테스트 전)
- ✅ L_Sandbox 맵 / 무기 DataAsset 2개(현재 `Weapons/DataTable/`) — 생성됨
- **BP 3종 생성 + 참조 할당** (프로덕션 BP 참조 패턴 — C++ 경로 하드코딩 제거됨):
  - `BP_FPSRGameMode` (**반드시 `/Game/Core/`**, 부모 `FPSRGameMode`): DefaultPawnClass=`BP_FPSRCharacter`, PlayerControllerClass=`BP_FPSRPlayerController`
  - `BP_FPSRCharacter` (부모 `FPSRCharacter`): IA 8개 + DefaultPrimary/SecondaryWeapon(DA_Weapon_Rifle/Knife) 할당
  - `BP_FPSRPlayerController` (부모 `FPSRPlayerController`): DefaultMappingContext=`IMC_Default`
  - 무기 DataAsset은 위치 무관(BP 하드참조). FireMode: Rifle=FullAuto / Knife=Single·무반동

## 다음 단계
- **PIE 테스트 통과 → P1 완료**
- **P1.5-B 사격/이동 감각** — Game.MD §2-4-2. **MagSize+재장전(예비 탄약 무한)** + ADS. (GameConfirm 반영: ReserveAmmo 삭제로 범위 축소)
- **P2**: SpawnDirector + Flow-Field + Pooling + Significance (적 300+ 안정) + **적 이속 ±10% 편차** + **적 근접 데미지·공격토큰 baseline** + **충돌무시 대시**. Game.MD §5·§7-3
- **P3**: 공유XP + **레벨업 스택(프리즈 폐지)** + **정비시간 RunPhase** + **오프닝 카드 시드** + Card UI + 동적풀 + Rarity + 리롤 (정비시간 트리거는 P3 디버그→P4 미션 연동)

## 빌드 / 검증 방법
- 빌드(에디터 닫고): `"D:\UnrealEngine\UE_5.7\Engine\Build\BatchFiles\Build.bat" FPSRogueliteEditor Win64 Development -Project="E:\Git_Project\FPSRoguelite\FPSRoguelite.uproject" -WaitMutex`
- 헤드리스 검증: `UnrealEditor-Cmd.exe <uproject> -unattended -nopause -nullrhi -nosplash -nosound -ExecCmds="Automation RunTests FPSRoguelite.Smoke.ModuleLoads" -TestExit="Automation Test Queue Empty" -abslog=...`
- 새 UCLASS 다수면 Live Coding 불가 → 풀빌드(에디터 닫아야 함). 입력 IA 생성은 `Scripts/gen_input_assets.py`

## 확정 사항 / 주의점
- 무기 교체 = 숫자키 **1/2/3** (`IA_EquipSlot1~3`) / 사격=좌클릭(클릭당 1발; full-auto 연사 cadence는 후속)
- **UE5.7 IMC 매핑은 Python `set_editor_property` 미반영 → 에디터 수동** (IA 에셋 생성은 Python OK)
- 카드선택 = **정비시간(Breather)에 무제한 대기** — 교전 중 프리즈 폐지, 미션 클리어 안전구간 소비 + 오프닝 시드 1~2장(Game.MD §2-2)
- 잔여 로그: PlayerController `[Input] Added DefaultMappingContext`(Warning, 1회성) — 다음 빌드 때 Verbose로 다운그레이드
- CommonUI `LogUIActionRouter` 에러 → P3에서 `CommonGameViewportClient` 설정 시 해결(현재 무해)
- **MCP(unreal) 인증 실패로 미사용** → UBT 빌드 + 헤드리스 자동화로 검증
- 모델 정책: 구현=Haiku 위임 / 검증(빌드·diff·스모크)=메인(Opus) 직접
- **프로덕션 방식 원칙**(CLAUDE.md/AGENTS.md): 콘텐츠는 BP/DataAsset/config 바인딩, C++ 경로 하드코딩 금지. 디버그 스캐폴딩은 검증용·전환대상
- 디버그/플레이스홀더(프로덕션 전환 대상): 발사/근접 DrawDebug, `FPSR.SpawnEnemies` 콘솔, 적 큐브 메시, FP팔/3P 메시 미할당, 적 추격=단순 스티어링(P2 Flow-Field 교체)
