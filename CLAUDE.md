# FPSRoguelite — 프로젝트 지침 (Claude 자동 로드)

> **작업 시작 전 반드시 `DESIGN.md`를 먼저 읽을 것.** 그것이 확정 설계의 단일 진실 공급원이다.

## 현재 진행 현황 (Resume Point — 새 세션은 여기부터 / 2026-05-28)

> 상세 진행은 `git log --oneline` 참조. 설계는 `DESIGN.md` §13(로드맵)·§14(폴더).

- **완료(커밋됨)**: P0 스캐폴드 / P1-0 코어(GameMode·PC·PS·Character) / P1-1 GAS 글로벌 속성(HealthSet·CombatSet) / P1-2 EnhancedInput(IMC는 PlayerController에서 추가, **이동·시점·점프 PIE 검증 완료**) / P1-3 1인칭 카메라+Separated Arms
- **진행 중(WIP, 빌드 전)**: **P1-4 무기 기반** — `Source/FPSRoguelite/*/Weapon/`(WeaponTypes·WeaponDataAsset·WeaponInventoryComponent). 서버권위+PushModel, 장착 시 발사 GA 부여
- **다음 순서**: P1-5 발사 GA(`UFPSRGameplayAbility`+`GA_WeaponFire_Hitscan`, 히트스캔+디버그라인) → Character에 인벤토리 부착 + **IA_Fire/IA_EquipSlot1~3** 입력 배선(서버 RPC) → P1-6 근접 GA → P1-7 적(`AFPSREnemyBase`+`UEnemyHealthComponent`+추격+디버그스폰)·**데미지 브릿지**(플레이어 GAS 계산→적 HealthComponent.ApplyDamage) → **헤드리스 자체검증 → 1회 빌드 → 사용자 PIE 1회 테스트**
- **빌드 방법**: 에디터 닫고 `Build.bat FPSRogueliteEditor Win64 Development -Project=...`. 새 UCLASS 다수면 Live Coding 불가→풀빌드
- **확정 사항**: 무기 교체=숫자키 **1/2/3**(IA_EquipSlot1~3) / 사격장 맵=사용자가 `L_Sandbox`(File→New Level→Basic+PlayerStart) / **UE5.7 IMC 매핑은 Python `set_editor_property` 미반영 → 에디터 수동**(IA 생성은 Python OK) / 카드선택=전원대기 / 입력 진단 `[Input]` 로그는 P1-4 빌드 때 다운그레이드 예정
- **MCP 미사용**: unreal MCP는 인증 실패(Aura 프로젝트 전용). UBT 빌드 + 헤드리스 자동화로 검증

## ⛔ 장르 편향 방지 (가장 중요)

이 프로젝트는 **1인칭 FPS × 뱀파이어 서바이벌 × 4인 협동 로그라이트**다 (레퍼런스: The Spell Brigade).
**Hero Shooter(Overwatch/Valorant)가 아니다.** 적은 수백 마리(~300-500)의 싼 액터다.

다음을 **임의로 도입하지 말 것** (과거 Hero Shooter 커리큘럼 편향):
- ❌ Lyra 풀 fork (→ 경량 커스텀 모듈 + 엔진 플러그인 체리픽)
- ❌ 모든 적에 GAS/ASC (→ 적은 경량 `UHealthComponent` + 비-GE 데미지)
- ❌ 적별 StateTree/BehaviorTree + NavMesh 길찾기 (→ Flow-Field + 스티어링, 배치 처리)
- ❌ Iris를 핵심 의존 (→ 디폴트는 Push Model, Iris는 P5 평가용·OFF)
- ❌ MassEntity (목표 규모 ~500이라 풀액터로 충분)
- ❌ Server-Side Rewind, Motion Matching, Bhop/Wall-run, PCG, True First Person 풀바디

**GAS는 플레이어(1~4)와 보스/엘리트(소수)에만.** 스웜 적에는 절대 붙이지 않는다.

## 환경 / 경로

- 엔진: **UE 5.7** — `D:\UnrealEngine\UE_5.7`
  - UBT: `D:\UnrealEngine\UE_5.7\Engine\Build\BatchFiles\Build.bat`
  - GenerateProjectFiles: `D:\UnrealEngine\UE_5.7\Engine\Build\BatchFiles\GenerateProjectFiles.bat`
  - 엔진 소스/플러그인: `D:\UnrealEngine\UE_5.7\Engine\Source`, `D:\UnrealEngine\UE_5.7\Engine\Plugins`
- 프로젝트 루트: `E:\Git_Project\FPSRoguelite`
- 참조 세팅 템플릿: `E:\Git_Project\언리얼세팅`
- 모듈명: `FPSRoguelite` (Runtime), 게임 타깃 `FPSRoguelite`, 에디터 타깃 `FPSRogueliteEditor`
- VS 2022 (`.vsconfig` 워크로드 기준)

## 코딩 / 빌드 규칙

- UE5 코딩 컨벤션 준수 (Epic Coding Standard). `.editorconfig` 적용: C++ 탭 들여쓰기, eol=crlf
- 빌드 타깃 버전: `BuildSettingsVersion.V6`, `EngineIncludeOrderVersion.Unreal5_7`
- 엔진 API/매크로/플래그는 **추론하지 말고 엔진 소스에서 실제 사용례를 grep**해 대조한 뒤 작성
- 네트워크: **P1부터 서버 권위 + Push Model**로 작성. 솔로로 만들고 나중에 리플리케이션 retrofit 금지
- 검증 없이 "완료" 보고 금지 — 빌드/스모크/`git diff` 중 하나로 자체 검증

## 작업 흐름 (전역 HotL 지침 준수)

- 새 작업은 플랜 우선. HIGH_RISK(파일 생성/수정/삭제, 빌드, 커밋)는 승인 후 실행
- 구현은 Haiku 위임, 검증(빌드/diff/스모크)은 Opus 직접 수행
- 설계 변경 시 `DESIGN.md`를 먼저 갱신
