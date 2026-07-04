# #3 다중맵 Tier 0 — 새 세션 실행 프롬프트 (복붙용)

> **작성 근거**: 컨셉 피벗(2026-07-03) → #3 다중맵 아키텍처 = Codex 5라운드 컨설트 수렴(`Docs/Review/20260705-multimap-budget-regroup.md`). 이 문서 = 그 수렴안의 **Tier 0(첫 세션)** 실행 프롬프트.
> **어디서**: 활성 코드 클론 `E:\Git_Project\FPSRoguelite`(no-2). 이 문서/PM 클론(FPSRoguelite2)이 아니라 **코드 클론에서 실행**.
> 아래 코드블록을 새 세션에 그대로 붙여넣는다.

```
Game.md + PROGRESS.md 먼저 읽어. 그다음 Docs/Review/20260705-multimap-budget-regroup.md(설계 수렴 전문)를 읽는다.
읽을 SSOT: Docs/SSOT/Performance.md §5(전역 예산·NetCull·U7 플로우필드), Docs/SSOT/Architecture.md §3-4(레벨/스트리밍/SpawnSubsystem/GameState 런상태), Docs/SSOT/RunFlow.md §2-1/§2-8(다중맵 심리스·맵점유 미션/스폰), Docs/SSOT/Workflow.md §6.

[작업] #3 다중맵 심리스 아키텍처 Tier 0 = 코어 인프라 + 생존조건. 현재 게임은 단일 맵만 있고 다중맵 코드가 0이다. 이 세션은 "문 파괴→인접 맵 스트림-in→맵점유→per-map 플로우필드→전역 예산 배분→새 맵에 적 출현"까지 성립시킨다. Tier 1/2(예산 게임필·rally·인센티브)는 후속 세션.

- 브랜치: main에서 phase/p8-multimap-tier0 분기(§6-7). 플랜모드 우선·승인 후 진행.
- 모델정책: 구현=Sonnet 위임 가능하나 **복제/서버권위 배선(NetCull·스트리밍 서버권위·문 파괴 복제·map-aware allocator)=Opus 직접**(하위모델 위임 금지). 검증=Opus 직접.
- 검증 없이 "완료" 보고 금지. 완료=빌드(-WaitMutex)+헤드리스 스모크 통과 후 사용자 검토. 게임플레이(다중맵 스트림·추격 연속성)=사용자 PIE 게이트.

[설계 명제 — 모든 결정의 기준]
"다중맵=자유. 본게임 압력·보상·burst는 2명 이상 같은 전선에서 선명하게 켜진다. 솔로 분산=정찰 모드. USP(back-to-back)는 2인+ 같은 전선에서 완전히 산다. 하드 테더 없음."

[Tier 0 산출물 — 이번 세션 스코프]
1. 레벨 스트리밍: 맵 4방향 끝 거대문을 쏴서 파괴(기존 Chaos 파괴 재사용) → 인접 맵 심리스 스트림-in. 배치=persistent world의 서로 다른 월드좌표(공간분리, 하드 트래블 아님). 검증용 화이트박스 2맵으로 시작. UE 표준 LoadStreamLevel(서브레벨) 우선(authored 결정성·per-map bake 제어); WP Data Layer는 대안 평가만.
2. per-map 플로우필드 레지스트리: 현 단일 U7 플로우필드를 ULevel* 키 레지스트리로 확장. 맵 stream-in 시 해당 맵 BuildObstacleMask 1회 bake, stream-out 시 evict. 적은 자기 맵 필드만 샘플.
3. 전역 예산 캡(잠정 200) + map-aware allocator v0: 단일 UFPSREnemySpawnSubsystem을 map-aware로. 전역 alive-budget을 점유맵에 배분. **임시 정책: "2인+ 맵 > 솔로 맵" 가중치**(완성 정책=Tier 1, 지금은 hook+임시 가중치). per-map 캡 금지(전역 공유).
4. NetCull 구현: per-enemy NetCullDistance(현재 미구현, 엔진 기본 ~2.25km) → 맵 간 relevancy cull. (RepGraph는 NetCull이 부족할 때 후속 평가 — Tier 0 아님.)
5. 미션/스폰 대상맵 파라미터화: RunDirector/스폰이 점유맵 기준으로 출현(빈 맵=미출현).
6. 최소 진입 시드: 새로 점유된 맵은 nonzero allocation → 수초 내 스폰포인트에서 적 출현(눈앞 팝업 금지, 기존 FOV+거리 게이트 스폰 재사용 = 측후방/문밖에서). "즉시 풀"이 아니라 "0~3초 압력".
7. 빈 맵 target=0 + 빈 맵 **적만** 하드 드레인: 아무도 점유 안 한 맵의 적은 디스폰(예산 회수·적=시뮬+복제 비쌈). ※ 점유맵 원거리 적 recycle(안전게이트·drain rate)은 Tier 1. ※ **맵은 언로드 안 함(LOD 컬만)·픽업/XP/문/상자는 잔존**(디렉터 결정 2026-07-05: 이관/소멸 로직 불요; 단 dormant/HISM 경량 잔존이라야 메모리/복제 안전, 풀액터 유지 금지). 크로스맵 텔포 목적지 맵이 항상 로드돼 재집결 공짜. 파일럿 메모리 게이트.
8. 문 파괴 상태 = 서버권위 복제 + late-join 반영(늦게 합류한 클라도 이미 뚫린 문 상태 수신).
9. 전환 중 점유맵 판정 grace: 문 넘는 순간 몇 초간 old/new map 이중 점유 허용(경계에서 적/필드 뚝 끊김 방지).
10. stream-in 실패/지연 fallback: 문 파괴 후 다음 맵 콜리전/스폰/플로우필드 준비 전 플레이어 낙하·빈 전장 방지(로드 완료까지 게이트/임시 바닥).

[⚠️ 함정 — 코드/설계에서 확인된 gotcha]
- **per-map 플로우필드 bake = ECC_WorldStatic 다운트레이스**(U7 BuildObstacleMask, Performance §5-2). 스트림된 맵 지오메트리에 WorldStatic 콜리전 없으면 필드가 안 구워진다. stream-in 완료(콜리전 등록) 후 bake 순서 보장.
- **런 상태(bRunPaused/SharedXP/PartyLevel)=AFPSRGameState**(Push Model, 서버권위). WorldSubsystem은 복제 불가라 다중맵 점유상태·allocator 결과 중 복제 필요분은 GameState/PlayerState 경유(Architecture §4). 전역 프리즈는 맵 무관 유지(이미 정합).
- **RunDirector=UFPSRRunDirectorSubsystem(server-only, 단일)·런클럭 전역**. 다중맵에서 단일 유지 + 미션/스폰 대상맵만 파라미터화(런클럭 쪼개지 말 것).
- **allocator "최대 그룹 크기 기준" 배분**은 Tier 1 정책이나 hook은 Tier 0에 미리. 없으면 3/1·2/2에서 본대 전선이 식어 명제 검증 불가(임시 가중치로 최소 커버).
- **호스트 최악 케이스**: 리슨서버=자기렌더+전 적 서버시뮬. 다중맵이라도 전역 200이 상한(맵 수 곱하기 금지).
- 문 파괴=기존 balance/pass2 Chaos 파괴 재사용(신규 파괴 시스템 금지). 스트리밍 트리거만 문 파괴 이벤트에 훅.

[검증]
- 빌드(-WaitMutex) Succeeded + 헤드리스 스모크 ModuleLoads Success.
- 다중맵 스모크(가능하면): 2맵 스트림-in/out·per-map 플로우필드 bake·allocator 배분·NetCull relevancy·빈 맵 드레인 로직 단위 확인.
- 사용자 PIE(코옵 2인 권장): 문 파괴→새 맵 스트림→새 맵에 적 출현(공백 0)·기존 맵 적 추격 연속(경계 grace)·빈 맵 드레인·전역 200 유지·late-join 문상태. FPSR.FlowField.Debug 1로 per-map 필드 확인.

[완료 시]
빌드+스모크 통과 → Codex 머지게이트(Scripts/codex-review.ps1 -Base main) → 사용자 PIE → 승인 시 --no-ff main 머지 + PROGRESS/TaskPrompts §B ✅ + 브랜치 삭제. 콘텐츠(화이트박스 2맵) 동반 커밋 여부 질문.
Tier 1(예산 게임필: burst reserve·silent recycle 안전게이트·local pressure floor·전환 추적자·allocator 정책 확정)은 다음 유닛으로 인계.

[⚠️ SSOT 정식반영 확인]: 이 프롬프트 실행 전, PM이 Performance §5·Architecture §3-4·RunFlow §2-1에 Tier 구조·명제를 반영해 두었는지 확인(원칙3). 미반영이면 설계 먼저.
```

---

## ✅ 디렉터 결정 반영됨 (2026-07-05, 리포트 §✅ / 컨설트 R6)
- 솔로=정찰(약한 억제, per-map 미션이 이미 억제) · 2인+=효율 좋음(강제 아님; **"2인+"=뭉침 임계값 2·3·4 전부, 게임 1~4인**).
- **그룹 버프 전면 폐기**(근접 버프·자석 smoothing·큐 공유 없음) → 뭉침 유인은 오직 **콘텐츠 배분**. ⚠️ **allocator가 심장**: Tier 1 allocator는 적 예산뿐 아니라 **미션/이벤트/보스 배치를 2인+ 그룹에 집중**시켜야 #2("뭉치면 효율")가 성립(애매 시 1/1/1/1 파밍이 최적→붕괴). Tier 0 임시 가중치 위, Tier 1에서 콘텐츠-aware로 확정.
- 텔레포터(Tier 2) = 장치 활성화→인터랙트→**대상 플레이어 지정**→텔레포트, 쿨타임 + 도착 앵커·짧은 채널(0.75~1.5s, 피격/DBNO 취소)·전투중 즉시탈출 제한·**개인+장치/팀 shared 쿨타임**(개인 쿨타임만이면 4인 번갈아 남용).
- 문 = 혼자선 부수기 어려운 체력(soft group-gate). 맵 = 언로드X·LOD컬·잔존(위 7번).
- 은근한 비효율(Tier 2, 명시 페널티/그룹 버프 아님): 최대 그룹 우선 미션 큐·솔로맵 정찰 태그·전투 열기 누적·연쇄 미션 거리·고가치 이벤트 최소인원·보스 추적선 등. 상세·금지선 = 리포트 §✅.
