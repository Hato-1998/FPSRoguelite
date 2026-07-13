# Workflow — 작업 방식 / 규칙 / 리뷰 루프 (FPSRoguelite SSOT 분할)

> `Game.md`(SSOT 허브)의 분할 문서. **섹션 번호(§x)는 원본 그대로 보존** — 소스 주석·교차참조 호환.
> **모든 코드 작업 시 필독.** 환경/경로·빌드/검증·브랜치 전략·모델 정책·핸드오프 규칙·리뷰 루프를 담는다.
> 담는 섹션: §6 작업 방식·규칙(§6-1~6-8) / §10 리뷰 루프.

---

## 6. 작업 방식 / 규칙

### 6-1. 환경 / 경로
- 엔진: **UE 5.7** — `D:\UnrealEngine\UE_5.7`
  - UBT: `D:\UnrealEngine\UE_5.7\Engine\Build\BatchFiles\Build.bat`
  - GenerateProjectFiles: `D:\UnrealEngine\UE_5.7\Engine\Build\BatchFiles\GenerateProjectFiles.bat`
  - 엔진 소스/플러그인: `D:\UnrealEngine\UE_5.7\Engine\Source`, `D:\UnrealEngine\UE_5.7\Engine\Plugins`
- 프로젝트 루트: `E:\Git_Project\FPSRoguelite`
- 참조 세팅 템플릿: `E:\Git_Project\언리얼세팅`
- 모듈명: `FPSRoguelite`(Runtime), 게임 타깃 `FPSRoguelite`, 에디터 타깃 `FPSRogueliteEditor`
- VS 2022 (`.vsconfig` 워크로드 기준)

### 6-2. 프로덕션 방식 원칙 (필수)
- 코드/개발은 **프로덕션 품질**. 엔진 템플릿 편의 단축은 지양
- **C++ = 로직/베이스 클래스 / 콘텐츠 바인딩 = BP 서브클래스·DataAsset·config** (데이터 드리븐). **에셋 경로를 C++에 하드코딩 금지**(`ConstructorHelpers` 지양)
- 네트워크 = 서버 권위 + Push Model. 엔진 API는 소스 대조 후 사용
- 디버그/테스트 스캐폴딩(DrawDebug, 콘솔 커맨드, 임시 로그, 플레이스홀더 메시)은 **검증용으로만 허용** — 프로덕션 전환 대상임을 명시·추적(§8)하고 `#if !UE_BUILD_SHIPPING` 등으로 게이트하거나 제거
- 잠깐의 동작 테스트는 단축 가능하나, **남는 코드는 프로덕션 기준**

### 6-3. 코딩 / 빌드 규칙
- UE5 코딩 컨벤션 준수(Epic Coding Standard). `.editorconfig` 적용: C++ 탭 들여쓰기, eol=crlf
- 빌드 타깃 버전: `BuildSettingsVersion.V6`, `EngineIncludeOrderVersion.Unreal5_7`
- 엔진 API/매크로/플래그는 **추론 금지 — 엔진 소스에서 실제 사용례를 grep**해 대조 후 작성
- 네트워크: **P1부터 서버 권위 + Push Model**. 솔로 후 retrofit 금지
- 검증 없이 "완료" 보고 금지 — 빌드/스모크/`git diff` 중 하나로 자체 검증

### 6-4. 진행 핸드오프 규칙 (다른 세션/AI 인수인계)
- 작업 시작 전 **이 문서(Game.md) + `PROGRESS.md`** 를 먼저 읽는다
- 큰 작업은 단계 완료 시점과 **세션 중단 전 반드시 `PROGRESS.md`를 갱신**(완료/진행중/다음순서/빌드·검증법/확정사항)하고 커밋
- 커밋 메시지에 단계·검증 결과를 명확히. 상세 이력은 `git log --oneline`
- 설계 변경은 **이 문서(Game.md)를 먼저 갱신**
- **각 P 단계는 전용 `phase/` 브랜치에서 작업**(§6-7). 세션 중단 전 `PROGRESS.md`에 **현재 활성 브랜치명**을 명시

### 6-5. 모델 / 작업 흐름 정책 (전역 HotL 준수)
- 새 작업은 **플랜 우선**. HIGH_RISK(파일 생성/수정/삭제, 빌드, 커밋)는 승인 후 실행
- **구현(코드 작성·파일 수정) → Sonnet 위임(현 최신 Sonnet 5 `claude-sonnet-5`, 2026-07-02 전환 — 기존 Haiku 기본에서) / 검증(빌드·diff·스모크·UI) → 메인(Opus) 직접**. 검증은 하위 모델(Sonnet/Haiku)에 위임 금지
- 단순 한 줄 수정·읽기 전용 조회는 모델 분리 없이 즉시 처리

### 6-6. 빌드 / 검증 방법
- 빌드(에디터 닫고 · **현 코드 빌드 대상 클론 = FPSRoguelite2**; 양 클론 공유 문서 = 경로 중립, 빌드하는 클론의 `.uproject` 사용):
  `"D:\UnrealEngine\UE_5.7\Engine\Build\BatchFiles\Build.bat" FPSRogueliteEditor Win64 Development -Project="<작업 클론>\FPSRoguelite.uproject" -WaitMutex`
- 헤드리스 검증:
  `UnrealEditor-Cmd.exe <uproject> -unattended -nopause -nullrhi -nosplash -nosound -ExecCmds="Automation RunTests FPSRoguelite.Smoke.ModuleLoads" -TestExit="Automation Test Queue Empty" -abslog=...`
- 새 UCLASS 다수면 Live Coding 불가 → 풀빌드(에디터 닫아야 함). 입력 IA 생성은 `Scripts/gen_input_assets.py`
- **MCP(unreal) 인증 실패로 미사용** → UBT 빌드 + 헤드리스 자동화로 검증
- **Codex 코드리뷰 게이트**: 단계 완료·커밋 직전 `Scripts/codex-review.ps1` 실행 → 외부 AI(Codex gpt-5.5)가 Game.md 원칙 기준으로 diff 리뷰. 기본 `-Base main`(브랜치 diff) / `-Uncommitted`(작업트리). 비대화(approval never; Windows codex review는 workspace-write 샌드박스 → 신뢰 로컬 리포 전용). 결과 `Docs/codex-reviews/`(gitignore; 컨설팅 `Docs/Review/`와 Windows 대소문자 충돌 회피용 분리). 호출·판독은 Opus가 직접

### 6-7. 브랜치 전략 (Phase 단위 워크플로)
로드맵(§7-3)의 각 **P 단계**는 `main`에 직접 커밋하지 않고 **전용 브랜치에서 작업 → 검증 통과 후 `main`에 머지**한다. (도입 2026-05-30. 이전 main 히스토리는 소급 적용하지 않음.)

- **브랜치명**: `phase/<단계소문자>-<핵심키워드>` (예: `phase/p2-spawndirector`, `phase/p1.5-b-ammo-reload`)
- **머지**: `--no-ff` 머지 커밋(phase 경계가 main 히스토리에 보이도록)
- **원격**: phase 브랜치도 `origin`에 push(백업/리뷰 협업)

라이프사이클:
1. **분기** — `git checkout main` → `git checkout -b phase/<단계>-<키워드>` → `git push -u origin phase/<단계>-<키워드>`
2. **작업** — 해당 phase 구현·문서를 이 브랜치에서만 커밋(구현=Sonnet 위임 / 검증=Opus 직접, §6-5)
3. **검증(머지 전 필수)** — 빌드(§6-6) → 헤드리스 스모크 → `Scripts/codex-review.ps1 -Base main`(브랜치 diff 리뷰). 검증 없이 머지 금지(§6-3)
4. **핸드오프** — phase 완료/세션 중단 전 `PROGRESS.md` 갱신·커밋(활성 브랜치명 명시, §6-4)
5. **머지** — `git checkout main` → `git merge --no-ff phase/<단계>-<키워드> -m "merge(phase): <단계> <요약> — 검증 통과"` → `git push origin main`
6. **정리** — `git push origin --delete phase/<단계>-<키워드>` + `git branch -d phase/<단계>-<키워드>`

> 전역 `claude/` 브랜치 클린업 정책과 prefix(`phase/`)로 분리되어 충돌 없음.

### 6-8. 커밋 메시지 컨벤션
형식: **`type(scope): <한 줄 요약>`** — Conventional Commits 콜론 형식. `[type]` 대괄호 금지(commitlint·git-cliff 등 툴 호환 + 기존 히스토리 일관).

- **type**(필수):

| type | 용도 |
|------|------|
| `feat` | 새 기능 추가 |
| `fix` | 버그 수정 |
| `content` | BP·DataAsset·umap 등 **콘텐츠** 변경(C++ 로직 아님 — §6-2 데이터 드리븐 경계) |
| `perf` | 성능 개선(기능 변경 없음 — 적 수백·성능예산이 제1원리, 원칙1) |
| `refactor` | 리팩토링(기능 변경 없음) |
| `docs` | 문서(`PROGRESS.md`·`Docs/SSOT/`·README 등) |
| `style` | 포맷팅·세미콜론 등(코드 동작 변경 없음) |
| `test` | 테스트(자동화·스모크) 추가·수정 |
| `chore` | 빌드·설정·툴(`.uproject`·`*.Build.cs`·gitignore·스킬·슬래시커맨드) |
| `plan` | 계획·신규 유닛 기록(플랜모드 산출물) |
| `merge` | `--no-ff` 검증 통과 통합 지점 마커(§6-7) |
| `revert` | 커밋 되돌림 |

- **scope**(권장): 작업 단위 ID(`U11a`·`V3`·`P7`) 또는 서브시스템(`camera`·`pm`·`gitignore`).
- **config 분류**: 게임플레이 값(`DefaultGame.ini` 밸런스 등) = `content` / 빌드·플러그인 설정(`DefaultEngine.ini`·`*.Build.cs`) = `chore`.
- **머지 커밋**: `merge(phase): <단계> <요약> — 검증 통과`(§6-7 라이프사이클 5와 동일).

예: `feat(U11a): 로비 허브/Steam 세션/Seamless 트래블` · `content(V0): 무기 DA 8종 비주얼/사운드 배선` · `perf(spawn): 플로우필드 갱신 주기 분할`.

---

## 10. 리뷰 루프 (외부 AI 협업)
- 사용자 + 다른 AI가 이 문서를 읽고 추가/수정점을 **`GameConfirm.md`**(다른 AI 작성, **우리는 만들지 않음**)에 정리
- 이후 세션이 `GameConfirm.md`를 불러와 현재 프로젝트와 비교 → (a) 타당한 추가/보완은 문서 갱신 + 작업계획 반영, (b) 사용자 판단·결정 필요한 것은 사용자에게 정리 보고
- **코드 리뷰(Codex CLI)**: 문서 리뷰와 별개로, 구현 검증 단계에서 `Scripts/codex-review.ps1`로 Codex(gpt-5.5)에 diff 코드리뷰를 받는다(§6-6). 지적은 Opus가 판독해 수정 여부 결정. **분리 원칙: 문서 제안=`GameConfirm.md`(외부 AI 작성) / 코드 리뷰=`Docs/codex-reviews/`(Codex 산출) / 컨설팅 토론=`Docs/Review/`.**
- **컨설팅 토론(제3 채널, ConsultLoop)**: 위 둘과 별개로, 사용자가 주제를 지목하면 백엔드(Claude)×클라이언트(Codex) **라이브 토론**으로 설계/콘텐츠를 자문받는다. 프로토콜=`Docs/ConsultLoop.md`, 호출=`Scripts/consult-codex.ps1`(`codex exec`), 트리거=`/consult <주제>`, 산출=`Docs/Review/`(추적, **프롬프트 매니저 `TaskPrompts_Master.md` §E가 읽어 백로그 인입**). **자문 전용**(코드 무변경) — 채택 설계는 이 문서/도메인 SSOT를 먼저 갱신 후 구현(§6-4). ※ 코드리뷰 덤프 저장폴더는 `Docs/codex-reviews/`로 분리(Windows 대소문자 충돌 회피).
