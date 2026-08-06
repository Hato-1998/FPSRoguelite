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
- **작업을 먼저 2갈래로 나눈다** (판정 기준 = §6-5-2 트리거 표):

| 갈래 | 플랜·설계 | 구현 | 검증·판정 |
|---|---|---|---|
| **코어 / 구조 설계 / 리팩토링** | **Fable**(`claude-fable-5`) | Sonnet 위임 | **Fable** |
| 그 외(기능 추가·버그 수정·콘텐츠 배선 등) | Opus | Sonnet 위임(`claude-sonnet-5`) | Opus 직접 |

- 코어 갈래 상세 = **§6-5-2**(도입 2026-08-07, 사용자 결정). 그 외 갈래는 종전과 동일(2026-07-02 위임 기본 Haiku→Sonnet 5 전환)
- **불변 규칙**: 검증·판정을 **하위 모델(Sonnet/Haiku)에 위임 금지**. 검증 권한은 **Fable(코어) 또는 Opus(그 외)에만** 있다
- 단순 한 줄 수정·읽기 전용 조회는 모델 분리 없이 즉시 처리

#### 6-5-1. 멀티에이전트 팬아웃 예산 (상한 필수)
서브에이전트/워크플로를 띄울 때 **에이전트 수에 반드시 상한**을 건다. (도입 2026-07-15 — 상한 없는 팬아웃으로 에이전트 71개·서브에이전트 토큰 552만을 태워 세션 사용 한도를 소진시킨 사고. 지출의 ~90%가 검증 단계였고 검증 65건이 실제로 잡은 교정은 1건.)

- **총 에이전트 ≤ 12개**가 기본 상한. 초과하려면 **먼저 규모·예상 비용을 사용자에게 말하고 승인**받는다.
- **차원(dimension)당 검증 에이전트 ≤ 2개.** 조사 결과가 낸 주장마다 검증자를 1:1로 붙이는 패턴 **금지** — 주장 수는 예측 불가(실측 10~14개/차원)라 상한이 없으면 폭발한다. 검증 대상은 **결론을 뒤집을 수 있는 주장만** 1~2개로 추린다.
- **설계 체크**: 스크립트를 쓰기 전에 "최대 몇 개가 뜨는가"를 계산한다. **답이 상수가 아니고 앞 단계 출력 길이에 비례하면 그 자체가 설계 결함** → `.slice(0, N)`으로 자르고, 자른 사실을 `log()`로 남긴다(조용한 절삭 금지).
- **모델/사고량 계층화**: 사실 확인·grep 대조 등 값싼 단계는 `model:"sonnet"`, 최고 난도 판정에만 상향. 전 단계에 Opus+`effort:"high"`를 깔지 않는다.
- **규모 정합**: grep 몇 방으로 판정되는 질문(호출처 census, "이 경로가 도달 가능한가")은 에이전트 3~5개로 충분하며 대개 워크플로 자체가 불필요하다. ultracode가 켜져 있어도 이 상한은 유효하다(ultracode = "철저히"이지 "무한히"가 아님).

#### 6-5-2. 코어 / 구조 작업 = Fable 주도 파이프라인
**게임의 뼈대를 정하는 작업은 Fable이 설계하고, 지시하고, 통과 여부를 판정한다. Sonnet은 그 명세대로 구현만 한다.**
(도입 2026-08-07, 사용자 결정. 근거: 구조 결정은 한 번 틀리면 되돌리는 비용이 크고, 이 프로젝트는 "적 수백을 싸게"라는 제1원리 위에서 복제·수명주기가 얽히기 때문에 **가장 깊게 추론하는 모델에 설계와 판정을 몰아준다**.)

##### (1) 이 파이프라인을 타는가 — 트리거
아래 중 **하나라도** 해당하면 코어 갈래다.

| # | 조건 |
|---|---|
| T1 | 새 서브시스템·클래스 트리를 **신설** |
| T2 | 여러 클래스에 걸친 **구조 변경 / 리팩토링** (기계적 치환 포함) |
| T3 | **복제·수명주기·GAS·성능 경로**가 얽힌 설계 |
| T4 | 확장 스키마 설계(카드·무기·적 규약 등 "새 규약을 데이터로 추가" 구조) |
| T5 | **코어 도메인** 헤더를 만짐 — `Source/FPSRoguelite/{Public,Private}/` 하위 `Core`·`Hero`·`Combat`·`Weapon`·`Enemy`·`Director`·`Run`·`AbilitySystem`·`Card`·`Boss`·`Messages` |

**해당 없음** → 기존 §6-5 흐름(구현=Sonnet / 검증=Opus). 한 줄 수정·기존 패턴 반복·콘텐츠 배선·단순 버그 수정은 코어 폴더를 만져도 코어 갈래가 아니다(T5는 *구조를 건드릴 때* 걸리는 조건이다).
애매하면 **코어로 올린다** — 설계를 얕게 해서 재작업하는 비용이 Fable 한 번보다 크다.

##### (2) 운영 모드 — Fable을 어디에 놓는가
| 모드 | 방식 | 언제 |
|---|---|---|
| **A (기본)** | **세션 자체를 Fable로 시작**한다(앱 모델 선택에서 전환). Fable이 사용자와 직접 플랜을 주고받고, 구현만 `Agent(model="sonnet")`으로 내리고, 빌드·스모크 결과를 직접 읽고 판정한다 | 코어 작업인 걸 알고 시작할 때 |
| **B (폴백)** | Opus 세션을 유지한 채 **Fable을 서브에이전트로** 띄운다. Fable = 설계·명세·통과 판정 / Opus = 도구 실행과 전달만(판정 권한 없음). 결과·`PROGRESS.md`에 "다음 세션은 Fable로 시작"을 남긴다 | 이미 Opus 세션 중에 코어 작업이 튀어나왔을 때 |

> 모드 B가 폴백인 이유: 서브에이전트는 사용자와 직접 대화할 수 없어 **플랜 승인 왕복이 안 되고**, 빌드 로그·diff를 통째로 넘겨야 해서 판정이 비싸고 부정확해진다. 가능하면 A로 간다.

##### (3) 4단계
| 단계 | 담당 | 하는 일 | 산출물 |
|---|---|---|---|
| **C0 조사** | Fable(값싼 조사는 Sonnet 위임) | 기존 인프라 전수조사, 엔진 소스 대조(전역 ENGINE SOURCE FIRST), 관련 SSOT·메모리 수집 | 컨텍스트 정리 |
| **C1 설계·명세** | **Fable** | 구조 확정 + **헤더 수준 인터페이스**까지. `.cpp` 본문은 쓰지 않는다 | `Docs/Specs/<유닛ID>_<키워드>.md` (템플릿 = `Docs/Specs/_TEMPLATE.md`) — **커밋한다** |
| **C2 구현** | **Sonnet 위임** | 명세를 **축자적으로** 구현. 시그니처·필드·클래스의 추가/변경 금지 | 코드 |
| **C3 검증·판정** | **Fable** | 명세 대비 diff 대조 → 빌드(§6-6) → 헤드리스 스모크 → Codex 게이트(§6-6). **통과/반려를 Fable이 판정** | 통과 시 머지(§6-7) / 반려 시 C2로 |

- **명세 갭**: C2에서 명세에 없는 판단이 필요해지면 Sonnet은 **추측해서 채우지 말고 멈추고 보고**한다. 갭은 C1으로 돌아가 Fable이 명세를 고친다.
- **명세는 살아 있는 문서다**: C3 통과 후 실제 구현과 어긋난 부분이 있으면 명세를 실제에 맞춰 갱신하고 상태를 `구현완료`로 바꾼다.

##### (4) 프롬프트 작성 규칙 (모델마다 정반대다)
**Fable에게 — 처방하지 말고 맥락을 줘라.**
- ❌ 단계별 지시("먼저 X 조사하고, 그다음 Y 설계하고…"). 구형 모델용 스캐폴딩은 **Fable의 출력 품질을 오히려 떨어뜨린다.**
- ✅ **목표 + 제약 + 성공 기준**만 주고, 대신 **컨텍스트를 두껍게** 실어라(해당 `Docs/SSOT/` 도메인 파일, 관련 메모리, 기존 코드 인용, 제1원리).
- 아래 3줄은 고정으로 붙인다:
  - `진행·완료 주장은 이 세션의 도구 결과와 대조해서만 보고할 것. 검증 안 된 것은 "미검증"이라고 명시할 것.`
  - `요청 범위 밖의 정리·리팩토링·추상화 추가 금지. 하지 않기로 한 것은 비목표에 적을 것.`
  - `턴을 끝내기 전 마지막 문단을 확인할 것. 계획·질문·다음 단계 약속이면 지금 도구로 실행할 것.`

**Sonnet에게 — 문자 그대로 따르게 써라.**
- Sonnet 5는 지시를 **축자적으로** 이행한다. "전부 적용" 같은 말은 **대상을 열거**한다.
- 명세 파일 경로를 주고: `이 명세를 축자적으로 구현. 시그니처·필드·클래스의 추가/변경 금지. 명세에 없는 판단이 필요하면 멈추고 "명세 갭"으로 보고.`
- 위임은 항상 최신 Sonnet(`model="sonnet"` = `claude-sonnet-5`).

**effort**: Fable 설계·판정 = `high`(최고 난도만 `xhigh`) / Sonnet 구현 = `high` / 대조·census 같은 값싼 단계 = `low`.

##### (5) 비용·지연 가드 (필수)
- **Fable은 출력 100만 토큰당 $50** — Opus 5의 2배, Sonnet 5의 약 3.3배. 모드 A에서는 **세션의 모든 턴이 그 단가**다.
- 따라서 Fable 세션 안에서도 **grep·호출처 census·파일 나열 같은 값싼 조사는 `model="sonnet"` 서브에이전트로 내린다.** Fable 본체는 설계와 판정에만 쓴다.
- **Fable 턴은 수 분 걸릴 수 있다.** 대기를 전제로 진행하고, 끝났는지 반복 확인(폴링)하지 않는다.
- **재설계 반복 2회 초과 시 사용자에게 보고** — 3회째부터는 명세가 아니라 요구가 흔들리고 있다는 신호다.
- §6-5-1 팬아웃 상한(**총 에이전트 ≤ 12**)은 이 파이프라인에서도 그대로 유효하다.
- **폴백**: Fable이 거부(refusal)하거나 쓸 수 없으면 **Opus 5로 설계를 진행하고, 그 사실을 명세 메타에 기록**한다.

### 6-6. 빌드 / 검증 방법
- 빌드(에디터 닫고 · **현 코드 빌드 대상 클론 = FPSRoguelite2**; 양 클론 공유 문서 = 경로 중립, 빌드하는 클론의 `.uproject` 사용):
  `"D:\UnrealEngine\UE_5.7\Engine\Build\BatchFiles\Build.bat" FPSRogueliteEditor Win64 Development -Project="<작업 클론>\FPSRoguelite.uproject" -WaitMutex`
- 헤드리스 검증:
  `UnrealEditor-Cmd.exe <uproject> -unattended -nopause -nullrhi -nosplash -nosound -ExecCmds="Automation RunTests FPSRoguelite.Smoke.ModuleLoads" -TestExit="Automation Test Queue Empty" -abslog=...`
- 새 UCLASS 다수면 Live Coding 불가 → 풀빌드(에디터 닫아야 함). 입력 IA 생성은 `Scripts/gen_input_assets.py`
- **MCP(unreal) 인증 실패로 미사용** → UBT 빌드 + 헤드리스 자동화로 검증
- **Codex 코드리뷰 게이트**(조건부): **§6-5-2 코어 갈래(T1~T5)일 때만** 머지 직전 `Scripts/codex-review.ps1` 실행 → 외부 AI(Codex gpt-5.5)가 `AGENTS.md` 원칙 기준으로 diff 리뷰. **그 외 갈래(콘텐츠·수치·BP배선·에셋·국지 버그수정)는 게이트를 건너뛰고** 빌드 + 헤드리스 스모크 + `git diff` 자기비판으로 끝낸다(범위 규칙 SSOT = `Docs/ConsultLoop.md` §0-1, 2026-08-07 사용자 결정).
  - 기본 `-Base main`(브랜치 diff) / `-Uncommitted`(작업트리) / `-Commit <sha>`. 비대화(approval never; Windows codex review는 workspace-write 샌드박스 → 신뢰 로컬 리포 전용). 결과 `Docs/codex-reviews/`(gitignore; 컨설팅 `Docs/Review/`와 Windows 대소문자 충돌 회피용 분리). 호출·판독·판정 주체 = §6-5 표(코어=Fable / 그 외=Opus)
  - ⚠️ **이 게이트엔 레드팀 페르소나가 안 들어간다**(2026-08-07 실증): ①`codex review`는 scope 플래그와 커스텀 프롬프트를 동시에 못 받고(`error: the argument '--base <BRANCH>' cannot be used with '[PROMPT]'`, 3개 플래그 전부) ②`AGENTS.md` 안내 줄도 대조군에서 리뷰 형식을 바꾸지 못했다. **게이트 결과를 "레드팀 검증됨"으로 표기하지 말 것** — 적대 페르소나는 토론 채널(`consult-codex.ps1`)에서만 실효(§10)
  - ⚠️ **빈 출력 = 통과 아님.** 인자 에러·인증 실패·사용량 한도면 stdout이 비고 exit 0처럼 보인다. 래퍼가 빈 결과를 저장하지 않고 exit 1로 경고한다(2026-08-07 실사고). **5분 워치독** — 무출력 5분이면 종료·스킵

### 6-7. 브랜치 전략 (Phase 단위 워크플로)
로드맵(§7-3)의 각 **P 단계**는 `main`에 직접 커밋하지 않고 **전용 브랜치에서 작업 → 검증 통과 후 `main`에 머지**한다. (도입 2026-05-30. 이전 main 히스토리는 소급 적용하지 않음.)

- **브랜치명**: `phase/<단계소문자>-<핵심키워드>` (예: `phase/p2-spawndirector`, `phase/p1.5-b-ammo-reload`)
- **머지**: `--no-ff` 머지 커밋(phase 경계가 main 히스토리에 보이도록)
- **원격**: phase 브랜치도 `origin`에 push(백업/리뷰 협업)

라이프사이클:
1. **분기** — `git checkout main` → `git checkout -b phase/<단계>-<키워드>` → `git push -u origin phase/<단계>-<키워드>`
2. **작업** — 해당 phase 구현·문서를 이 브랜치에서만 커밋(모델 배분 = §6-5. **코어/구조/리팩토링이면 §6-5-2 Fable 주도 4단계**)
3. **검증(머지 전 필수)** — 빌드(§6-6) → 헤드리스 스모크 → **(코어 갈래일 때만)** `Scripts/codex-review.ps1 -Base main`(브랜치 diff 리뷰). 그 외 갈래는 Codex 게이트 대신 `git diff` 자기비판으로 대체(§6-6). 검증 없이 머지 금지(§6-3). **통과/반려 판정 주체 = §6-5 표**(코어=Fable / 그 외=Opus) — 하위 모델 위임 금지
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
> **⚠️ Codex 호출은 전부 조건부다** — 코어/리팩토링/설계·구조(§6-5-2 `T1~T5`) 사안에만 부른다. 범위 규칙 SSOT = `Docs/ConsultLoop.md` §0-1(2026-08-07 사용자 결정). 아래 두 채널 다 이 게이트를 먼저 통과해야 한다.

- **코드 리뷰(Codex CLI)**: 문서 리뷰와 별개로, 구현 검증 단계에서 `Scripts/codex-review.ps1`로 Codex(gpt-5.5)에 diff 코드리뷰를 받는다(§6-6). 지적은 §6-5 판정 주체(코어=Fable / 그 외=Opus)가 판독해 수정 여부 결정. **분리 원칙: 문서 제안=`GameConfirm.md`(외부 AI 작성) / 코드 리뷰=`Docs/codex-reviews/`(Codex 산출) / 컨설팅 토론=`Docs/Review/`.**
- **컨설팅 토론(제3 채널, ConsultLoop)**: 위 둘과 별개로, 사용자가 주제를 지목하면 **안건 소유자(Claude) × 웹/앱 적대 레드팀(Codex)** 라이브 토론으로 설계·구조를 자문받는다. 프로토콜=`Docs/ConsultLoop.md`, 페르소나 원문=`Docs/CodexRedTeamPersona.md`, 호출=`Scripts/consult-codex.ps1`(`codex exec` — 래퍼가 페르소나를 자동 prepend), 트리거=`/consult <주제>`·`/plan-consult <작업>`, 산출=`Docs/Review/`(추적, **프롬프트 매니저 `TaskPrompts_Master.md` §E가 읽어 백로그 인입**).
  **최종 결정권은 안건 소유자(Claude)** — 언제든 토론을 끝낼 수 있으나 `종료 사유` + `기각 원장`(근거 = 제1원리 조항/코드 인용/실측치)을 남겨야 성립(ConsultLoop §3-1). **자문 전용**(코드 무변경) — 채택 설계는 이 문서/도메인 SSOT를 먼저 갱신 후 구현(§6-4)하며, HIGH_RISK는 여전히 사용자 승인 뒤. ※ 코드리뷰 덤프 저장폴더는 `Docs/codex-reviews/`로 분리(Windows 대소문자 충돌 회피).
