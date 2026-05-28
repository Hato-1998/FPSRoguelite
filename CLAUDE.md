# FPSRoguelite — 프로젝트 지침 (Claude 자동 로드)

> **작업 시작 전 반드시 `DESIGN.md`를 먼저 읽을 것.** 그것이 확정 설계의 단일 진실 공급원이다.

## 진행 핸드오프 규칙 (필수 — 다른 세션/AI 인수인계)

- **작업 시작 전 `PROGRESS.md`를 먼저 읽어** 현재 진행 상황을 파악한다 (라이브 진행 문서).
- 큰 작업은 의미 있는 단계를 끝낼 때마다, 그리고 **세션 중단 전 반드시 `PROGRESS.md`를 갱신**(완료/진행중/다음순서/빌드·검증법/확정사항)하고 커밋한다.
- 커밋 메시지에 단계와 검증 결과를 명확히 남긴다. 상세 이력은 `git log --oneline`.
- 설계 변경은 `DESIGN.md`를 먼저 갱신한다.

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
