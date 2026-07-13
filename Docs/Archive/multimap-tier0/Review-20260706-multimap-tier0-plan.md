# 컨설트: #3 다중맵 심리스 Tier 0 구현 플랜 하드닝 (2026-07-06)

> 백엔드(Claude, 시스템/넷코드 렌즈) × 클라이언트(Codex, 독립 아키텍처/구현 리뷰 렌즈) 5라운드 토론. 원본 = `Docs/Review/_raw/20260706-*-multimap-tier0-plan-r{1..5}.md`.
> **자문 전용**(ConsultLoop §6) — 채택 설계는 구현 단계에서 코드로 반영(승인된 플랜 `plans/adaptive-zooming-kernighan.md` v2). SSOT(Performance §5·Architecture §3-4·RunFlow §2-1)는 이미 Tier 구조·명제 반영됨(원칙3 충족). 이 리포트는 **구현 플랜의 아키텍처 하드닝** 근거.

## 범위 / 읽은 컨텍스트
- `Game.md §1`(제1원리=적 200-300 싸게, Hero Shooter 아님) · 승인 플랜 `plans/adaptive-zooming-kernighan.md` + 내부 적대적 5렌즈 리뷰 하드닝 설계 · 활성 소스 `E:\Git_Project\FPSRoguelite2\Source`(FlowFieldSubsystem/EnemySpawnSubsystem/RunDirector/GameState/PlayerState/Door). Codex는 read-only 샌드박스로 리포 직접 대조 + UE5.7 엔진 사실(LWC·LevelStreaming·ReplicateStreamingStatus·NetCull relevancy·CMC 예측·Timer/GC) 확인.
- 전제(토론 대상 아님): 이 세션=C++ 코어만(PIE 불가=맵 LFS 포인터+화이트박스 미저작), 전역 공유 캡, 문=health-based `AFPSRDoor`(Chaos 아님), 적=persistent 레벨 스폰.

## 🔧 백엔드 렌즈 핵심 입장 (Claude)
- 단일맵 전역 가정(최근접 플레이어·KillZ·스폰포인트 캐시·bake-once·전역 캡)을 **전부 map-gate**. per-map 플로우필드 레지스트리·map-aware allocator(전역 캡, per-map 캡 금지)·NetCull(적=persistent라 유일 크로스맵 relevancy 레버)·서버권위 스트리밍(엔진 가시성 자동복제 위임)·점유 복제=PlayerState.
- 이 세션 자동검증=빌드+헤드리스 스모크만(콘텐츠 독립), 나머지 사용자 PIE.

## 🎮 클라이언트 렌즈 핵심 입장 (Codex)
- **플로우필드는 `const struct&` 재바인딩보다 `UFPSRFlowFieldComputer`(UObject) 인스턴스 분해가 회귀 안전** + worldless 코어 분리로 **합성 헤드리스 유닛테스트**가 PIE 공백을 메움(이 세션 자동검증 확장).
- **Tier 경계 사수**: 점유맵 능동 drain=Tier1(silent recycle). Tier0=빈맵 하드드레인+자연감소+소형 seed reserve. grace/점유는 **committed vs grace 2채널**로 분리(전투·fill=committed 엄격, flow·drain보호=grace).
- boundary blocker=**복제 필수**(서버전용이면 원격 CMC 예측이 뚫었다 튕김), 단 적 누수 주방어는 flow유계+MapId, 물리 blocker=최후방어.

## 토론 로그 요약 (라운드별 핵심)
- **R1**: 방향 채택. Codex가 (1)UObject field-computer 분해(헬퍼 struct& 재바인딩보다 저회귀) (2)합성 지오메트리 `FlowField.Unit` 테스트로 PIE 공백 해소 (3)Tier 경계=점유맵 능동 drain은 Tier1, seed=0~3s "스폰 시작"(완전충전 아님) (4)offset=max(NetCull+무기range+오디오atten+separation)~수km, origin rebasing 금지 (5)크로스맵 무기 데미지 가드 제기.
- **R2**: worldless 코어 분리가 헤드리스 테스트의 관건(RecomputeField 안 world/LOS 남으면 무의미). `FFPSRFlowFieldSurfaceData` value struct·단일 스케줄러(per-computer 타이머 금지)·seed reserve 상시 Clamp(Ceil(Cap*0.03~0.05),4,10)·boundary blocker 복제·점유=명시 MapId AABB(streaming ULevel 신뢰X)·구현순서 S1a→S1b→S2a→S2b→S3.
- **R3**: worldless 프로덕션-only 잔여={stream collision-ready·bounds/registry 수집·pawn 열거·capsule footZ·trace bake·LOS snap}. **CanAffectTarget 공통판정을 damage AND knockback 앞**(폭발 damage 0도 knockback 샘). 적 MapId=현 AABB fast-skip+벗어난 소수만 재판정+hysteresis(문 트리거 신뢰X). A/B 둘다 서브레벨+런시작 ready gate 재사용. Default 무회귀 2커밋. MapStreamSubsystem=Map/ 폴더. blocker QueryOnly만으론 불충분(적 SetActorLocation 보정경로), 주방어=flow/MapId.
- **R4**: 경계 jam은 grid extent(bounds volume)+MapId 게이트로 이미 차단, blocker=낙하/보이드 방어로 축소. late-join: ReplicateStreamingStatus는 클라 로드완료 장벽 아님→뜬 적 방지 위해 ready gate에 클라 relevancy 포함. CanAffectTarget=인터페이스+ResolveDamage/ApplyExplosion/projectile 진입점. 프리즈=시뮬 정지+진행중 stream 완료+ready flag 갱신. 구조 미해결 없음.
- **R5(최종·조건부 Go)**: 3개 불변식 추가 요구 — (1)**grace 채널 분리**(전투·fill 제외, flow·drain보호만) (2)**점유 2채널**(Committed=fill/미션/seed, Grace=drain보호/flow, seed=reserve서 1회성 idempotent·Hamilton 미혼입) (3)**Computer* 캐시 무효화**(Generation/WeakPtr·active count 0 전 evict 금지). ready gate=공통 predicate+호출자 분기(late-join=per-client `ClientMapReady` ack). **투사체 MapId=spawn 고정**(AABB 재계산 금지). 렌즈별 Go/Go/Conditional Go(late-join ack)/Conditional Go(투사체 고정·grace-전투 정합).

## ✅ 합의 권고 (양 렌즈 동의)
1. **플로우필드**: `UFPSRFlowFieldComputer`(UObject shell·flat TArray) — worldless 코어(`BuildFromSurfaceData`/`RunBFS(SourceSurfaces)`/`Sample`) ↔ 프로덕션(`BuildFromWorldTrace`/`ResolveSourcesProduction`) 분리. subsystem=`UPROPERTY(Transient) TMap<Tag,TObjectPtr<Computer>>`+단일 스케줄러. 적은 MapId 갱신 시 Computer*+Generation 캐시. NumLayers=2/Surf-키 BFS/EdgeMask 보존. 헤더 rank 주석을 구현(nearest valid rank) 정렬 후 추출. **`FPSRoguelite.FlowField.Unit` 합성 골든 테스트**(이 세션 자동검증).
2. **allocator**: 단일 SpawnSubsystem 유지·GlobalAliveCap=200·SeedReserve≈8. 불변식 `DirectorTarget≤Cap-Reserve`·`Sum(MapTargets)≤DirectorTarget`·`EmptyMapTarget==0 after grace`·seed는 reserve서 1회성·Hamilton 배분. 빈맵 하드드레인(bounded·grace-gated), **점유맵 능동 drain 없음**(Tier1). 증분 카운터·스폰포인트 MapReady 재캐시. allocator 불변식 유닛테스트.
3. **점유 2채널**: Committed(정착 MapId, Push replicate-all 저-churn)=allocator fill/미션/seed. Grace(committed∪최근이탈, 서버전용)=old맵 drain보호+flow 경계 clamp seed. void=LastCommitted. 겹침=Priority tie-break.
4. **이동/전투 map-gate**: 이동패스 최근접/폴백/NetFreq=same-MapId(∪grace, flow만). **전투=committed MapId 엄격**(grace 제외). `UFPSRMapBoundInterface::GetMapId`+`FPSRCombat::CanAffectTarget`을 ResolveDamage/ApplyExplosion(damage+knockback)/projectile 진입점에. 투사체 MapId=spawn 고정.
5. **NetCull**: enemy `NetCullDistanceSquared`(Activate)·NetCull≥같은맵 교전거리·offset>NetCull. 적=persistent라 필수.
6. **스트리밍**: A/B 둘다 서브레벨(A=런시작 로드·B=문파괴). `EnsureMapReady(MapId,Reason)` 공통 predicate(Loaded+Visible+CollisionReady+BoundsReady+FlowBakeReady)+호출자 분기(런시작=deferred possess·문파괴=blocker 해제·late-join=per-client ack). 클라 가시성=엔진 자동복제. `AFPSRBoundaryBlocker`(persistent·복제·QueryOnly). 실패 타임아웃=blocker 유지+피드백. MapStreamSubsystem=Map/.
7. **불변**: 단일 RunClock·프리즈 전역(시뮬 정지+진행중 stream 완료). Default 무회귀 2커밋. 구현순서 S1a→S1b→S2a→S2b→S3.

## ⚖️ 미해결 쟁점 (튜닝으로 수렴)
- SeedReserve % / offset 크기(무기 max range 확인 후) / grace 초 / NetCull 반경(≥같은맵 교전거리) / hysteresis(+min dwell) = 전부 시작값 후 실측 튜닝(사용자 PIE).
- world-trace bake 회귀는 헤드리스 유닛으로 못 잡음(프로덕션 trace smoke=별도, PIE 영역) — 정직히 남김.

## 🙋 사용자 결정 필요
- 없음(취향/범위 결정은 앞 AskUserQuestion에서 완료: 코드전용+콘텐츠 체크리스트 / Tier0 전체 S0~S3). 튜닝값은 PIE 후.

## 📌 액션 아이템
- 플랜 v2(`plans/adaptive-zooming-kernighan.md`)에 위 3개 불변식(grace 채널·점유 2채널·Computer* 캐시 무효화)+ready gate 분기+투사체 고정 MapId 반영 → 사용자 재승인 후 구현.
- 구현 완료 후 SSOT 갱신 후보: Performance §5(GlobalAliveCap=200 코드 enforce·seed reserve 모델), Architecture §4(FlowFieldComputer 분해·MapStreamSubsystem·MapBoundInterface).
