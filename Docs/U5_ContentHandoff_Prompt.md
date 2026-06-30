# U5 콘텐츠 저작 + PIE → main 머지 — 새 세션 실행 프롬프트

> **이 파일은 새 Claude 세션에 그대로 붙여넣어 U5 마무리를 이어받기 위한 자기완결 프롬프트다.**
> 전제: ① Unreal 에디터를 **먼저 연다**(FPSRoguelite.uproject) ② **VibeUE-Claude MCP가 등록**돼 있어야 한다(현재 `claude_code_config.json`엔 죽은 Aura `unreal_editor_local`만 있고 VibeUE 미등록 — `claude mcp add` 등으로 VibeUE-Claude @ 127.0.0.1:8088 등록 후 Claude를 시작할 것) ③ 그 다음 Claude 세션을 시작해 이 프롬프트를 붙여넣는다.

---

[작업] U5 — 원거리 적 콘텐츠 저작 + PIE 검증 → `phase/p4-ranged-enemy` → main 머지

■ 컨텍스트 (코드는 이미 완료·검증됨)
  U5 원거리 적 AI + 사전경고 생산자 **C++ 코드는 phase/p4-ranged-enemy 브랜치에 완료·머지대기**다.
  커밋: `a4d604d feat(U5)` + `080352a fix(U5, Codex P2)` + `b42942e docs(progress k)`.
  빌드 Succeeded + 헤드리스 스모크 Success + Codex 머지게이트 통과(P2 교정 완료). **남은 것 = 콘텐츠 + PIE뿐.**
  먼저 읽기: `PROGRESS.md` 핸드오프 (2026-06-30 k) + `Docs/SSOT/Enemy.md §2-6`(원거리 아키타입/공격토큰/혼합).

■ 만들어진 C++ (콘텐츠가 바인딩할 대상)
  - `AFPSRRangedEnemyBase`(`Source/.../Public/Enemy/FPSRRangedEnemyBase.h`) — 원거리 아키타입 베이스.
    EditDefaultsOnly 프로퍼티(전부 보수적 기본값 있음, **ProjectileClass만 필수**):
      ProjectileClass(TSubclassOf<AFPSRProjectile>, **필수**), RangedEngageRange=1400, RangedChargeTime=1.5,
      RangedFireCooldown=2.5, bRequireLineOfSight=true, ProjectileDamage=20, ProjectileSpeed=1800,
      ProjectileLifetime=4, ProjectileGravityScale=0, MuzzleOffset=(40,0,40). (StopDistance=900 생성자 기본)
  - `UFPSREnemyRosterDataAsset`(`.../Public/Enemy/FPSREnemyRosterDataAsset.h`) — 혼합 로스터.
      SpawnRules: TArray<Instanced UFPSREnemySpawnRule>. MVP 규칙 = `UFPSREnemySpawnRule_Static`{EnemyClass, Weight}.
  - `UFPSRRunScheduleDataAsset`에 신규 필드 `EnemyRoster`(TObjectPtr<UFPSREnemyRosterDataAsset>) — 디렉터 StartRun이 푸시.

■ 저작할 콘텐츠 (4개) — C++ 하드코딩 금지, 전부 BP/DataAsset
  1. **적 투사체 BP** — `AFPSRProjectile` 자식. 비주얼 메시(+VFX는 후속) 지정.
     ※ 급하면 기존 플레이어 투사체 BP를 임시로 ProjectileClass에 물려도 동작(Team은 코드가 Enemy로 강제).
  2. **BP_RangedEnemy** — `AFPSRRangedEnemyBase` 자식. **ProjectileClass = 위 ①** 지정(필수). 나머지 값은 기본 유지/튜닝.
     (메시는 베이스가 엔진 큐브 플레이스홀더 사용 — 적 VAT 메시는 콘텐츠 후속 [[vat-bake-inherited-component-wiring]])
  3. **DA_EnemyRoster** (`UFPSREnemyRosterDataAsset`) — SpawnRules에 `_Static` 2개:
       · {EnemyClass = 기존 근접 적 BP(현 EnemyClass — GameMode/스폰서브시스템이 쓰던 것), Weight = 3}
       · {EnemyClass = BP_RangedEnemy, Weight = 1}   (≈ 근접:원거리 = 3:1, 밸런스 후속 튜닝)
  4. **DA_RunSchedule**(현재 런이 쓰는 스케줄 에셋)의 **EnemyRoster = DA_EnemyRoster** 지정.
     ※ 기존 근접 적 BP·활성 DA_RunSchedule 경로는 GameMode(EnemyClass)·RunDirector(ActiveSchedule)에서 역추적.

■ 함정 / 주의 (VibeUE)
  - 컨테이너 위젯이 아니라 **BP 서브클래스 + DataAsset 저작**이라 [[vibeue-render-target-gpu-hazard]] 모달행/크래시 위험은 낮음. 단 instanced 배열(SpawnRules) 추가는 VibeUE로 까다로울 수 있음 — 안 되면 헤드리스 commandlet(`-run=pythonscript`, [[headless-gas-content-authoring]])로 우회하거나 에디터 디테일 패널 수동 저작.
  - **EnemyClass 폴백 무회귀**: DA_EnemyRoster를 안 물리거나 비우면 기존 단일 EnemyClass로 폴백(현행 근접 스웜 동작 유지)이라 안전.
  - VibeUE 크래시/지속 실패 시: 사용자에게 디테일 패널 수동 저작 가이드로 전환(아래 ■수동).

■ PIE 검증 (리슨서버 권장; 솔로도 대부분 확인 가능)
  콘솔: `FPSR.TravelGame` → 런 시작 후 원거리 적이 섞여 스폰되는지.
  체크: ① 원거리 적이 사거리에서 **정지→차징** 시 화면 테두리 **방향 위협 경고**(기존 ThreatIndicator 위젯) 표시
        ② **가시 투사체 발사** + 피격 시 데미지 ③ FF OFF에서 적탄이 **다른 적·아군 무오발**(IsHostileTarget)
        ④ **카드 프리즈 중** 차징·발사·투사체 **정지**(누산기) ⑤ 차징 중 타겟 **DBNO화 시 경고 해제**
        ⑥ **닫힌 문 뒤 플레이어에겐 발사 안 함**(Codex P2; 문 부수면 통과) ⑦ 원거리 적 **약점** 피격
        ⑧ 한 플레이어 대상 **동시 차징이 3 초과 안 함**(held 토큰).
  경고 소비자만 먼저 보려면 콘텐츠 없이도 콘솔 `FPSR.TestRangedWarn 90 1 1`로 확인.

■ 통과 후 머지 (Workflow §6-7)
  `git checkout main` → `git merge --no-ff phase/p4-ranged-enemy -m "merge(phase): p4-ranged-enemy U5 원거리 적 — 검증 통과"`
  → `git push origin main` → 브랜치 정리(`git branch -d`, 원격도) → `Docs/TaskPrompts_Master.md` §B **U5 ✅** → PROGRESS 핸드오프 갱신.
  (밸런스 수치는 콘텐츠 밸런싱/U14 perf에서 조정. 적탄 perf=§5 미측정 U14 이월.)

■ 수동 저작 (VibeUE 안 될 때 사용자 가이드)
  콘텐츠 브라우저에서: ① 우클릭→Blueprint Class→부모 검색 `FPSRProjectile`→메시 지정 ② 부모 `FPSRRangedEnemyBase`→
  디테일 FPSR|Enemy|Ranged에서 ProjectileClass=① ③ 우클릭→Miscellaneous→Data Asset→`EnemyRosterDataAsset`→
  SpawnRules에 + 두 개, 각 인스턴스 클래스 `Static Weight` 선택→EnemyClass/Weight 입력 ④ DA_RunSchedule 열어 EnemyRoster=③.

보고 및 질문은 한국어로.
