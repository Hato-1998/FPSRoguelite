# PlayerFeel — 카메라 / 생존·이동 / 게임필 (FPSRoguelite SSOT 분할)

> `Game.md`(SSOT 허브)의 분할 문서. **섹션 번호(§x)는 원본 그대로 보존** — 소스 주석·교차참조 호환.
> 작업 시작 전 허브 `Game.md` + `PROGRESS.md`를 먼저 읽고, 카메라/메시·플레이어 생존(DBNO)·대시·게임필/피드백/공간지각·HUD 관련 작업 시 본 파일을 연다.
> 담는 섹션: §2-9 카메라·메시 / §2-13 플레이어 생존·이동 / §2-14 게임필·피드백·공간 지각.

---

### 2-9. 카메라 / 메시
- **Separated Arms**: 본인 = FP팔(`OnlyOwnerSee`) + 무기 / 타인 = 3P 캐릭터(`OwnerNoSee`)
- True First Person 풀바디 렌더링은 사용 안 함 (가독성/속도감 우선)

### 2-13. 플레이어 생존 / 이동 (협동, 확정 2026-05-30)
- **수동 부활(DBNO: Down But Not Out)**: 체력 0 시 즉시 사망하지 않고 쓰러진 상태로 전환. 생존 아군이 쓰러진 캐릭터 근처 일정 반경에 머물면 **부활 게이지**가 차오르고, 가득 차면 부활(서버 권위). 플레이어 상태기계 Alive / DBNO / Dead.
  - **미니설계 확정(2026-06-29, U9/Phase 1B — 사망모델 = 정식 DBNO 당겨옴)** → 상세 [`Docs/DBNO_MiniDesign.md`](../DBNO_MiniDesign.md). 기존 시임만 확장(신규 중앙클래스 0): `AFPSRPlayerState` `bIsDead`→`ELifeState{Alive,DBNO,Dead}`(복제·OnRep 입력게이트), `IsAlive()`=Alive(**DBNO=not-alive**)로 `GetLivingPlayerCount`/`AreAllPlayersDead`/`RefreshPauseState`(A4) 자동 정합, `HandleOutOfHealth`→DBNO 전환, 적 타겟선정 `IsAlive()` 필터(B17). 부활 = **신규 `UFPSRReviveComponent`**(근접 Alive 아군 반경 체류→`ReviveProgress` 복제 누적→완료 시 50%HP 복구; 전역 프리즈 중 정지).
  - **확정 규칙**: 다운 = **제자리 정지 + 살아있는 아군 시점 관전**(이동·시점·사격/점프/대시 차단; 부활 = 쓰러진 자리 기상) [변경 2026-06-30, PIE 후 — 크롤 폐기, B16 관전을 DBNO로 당겨옴: 서버 `SetViewTargetWithBlend`→가까운 Alive 아군, `UpdateDownedSpectate` 틱 유지, `PerformRevive`→본인 시점 복원] · 다운 중 **무피해+비타겟**(적 수백 즉사 방지, 압박은 와이프 위험으로) · **블리드아웃 = 시임만 기본 비활성**(부활/와이프까지 다운 지속, 값은 밸런스 후속) · 부활 = **근접 자동**(아군 1명+면 충전) · 부활 직후 **PostReviveInvuln**(기본 5s, 무적+적 충돌무시 — 갓-부활 즉시 재다운 방지, `PostReviveInvulnSeconds` 편집가능) · **카드선택 프리즈 종료 후 grace**(기본 3s 무적+적통과, `PostFreezeInvulnSeconds` — 포위된 채 재개 시 즉사 방지, 부활과 같은 `BeginGraceWindow` 재사용) · 부활시키는 사람도 **부활 게이지 HUD 노티**(생존자=게이지+설명만, 다운자=비네트+게이지) · 팀와이프 = 생존(Alive) 0 → 전원 Dead → `EndRun(Defeat)`(솔로 다운 = 즉시 Defeat). 블리드아웃 값·풀 관전 리그(타겟 순환)·다운 반격은 후속.
  - 구현 = **Phase 1B**(서버권위·복제 = Opus 직접), SSOT-우선 후 코드. (이전 "구현 P5"에서 앞당김 — `U1_PostGate_Fixes.md` 결정.)
- **충돌무시 대시(Dash)**: 적·아군 충돌을 무시하고 통과하는 짧은 거리 회피기(모든 캐릭터 기본 제공). 적에게 완전 포위돼 카메라가 막히는 상황 탈출용. (쿨다운/차지는 밸런스 후속) — §3 무브먼트, 구현 P2
- **최대체력 증가 = 즉시 회복(확정 2026-06-02)**: `MaxHealth`가 증가하면(체력 카드 등) **증가분만큼 현재 `Health`도 함께 증가**(서버 권위, `UFPSRHealthSet::PostAttributeChange`에서 처리·새 최대치로 clamp). +체력 업그레이드 선택의 즉각적 체감. 감소 시는 다음 Health 변경 때 clamp로 캡.

### 2-14. 게임필 / 피드백 / 공간 지각 (확정 2026-05-30)
1인칭 스웜 특성상 사각지대(등 뒤·측면) 무방비를 해소하고 대량학살 쾌감을 극대화한다.
- **공간 지각**: 사각지대(특히 등 뒤) 접근 시 괴성/경고 사운드 + **화면 테두리 방향성 위협 인디케이터(Threat Indicator) UI**(시야각 밖에서 적 접근/공격 판정 거리 진입 시)
- **오디오**: 몬스터 발소리 구체화, 위협/경고 사운드(Significance 티어 연동 — §5-1)
- **타격감(Juice)**: 크로스헤어 **히트마커** + 피격 사운드 / 적 처치 시 **경량 파편(Gibs)·팝 연출**(과도한 연산 금지) / 크리티컬·처치 시 **가볍고 맑은 핑(Ping)** 사운드
- 구현: 핵심(히트마커·핑·위협 인디케이터·기본 오디오) **P4**, 폴리시 **P7**
- **구현 상태(P4-D)**: `UFPSRPlayerFeedbackComponent`(로컬·비복제·이벤트형) + PC Client RPC. ① **히트마커**(서버권위 Hit/Crit/Kill, 활성화당 1회, Unreliable) ② **피격 방향 인디케이터**(CoD식: `ApplyContactDamage`→오너클라, 카메라 기준 각도) ③ **원거리 타겟 사전경고**(다수소스 id별 추적·각도배열·추적Tick·Reliable; 생산자=원거리 적 AI는 후속, 디버그 `FPSR.TestDamageDir`/`FPSR.TestRangedWarn`). WBP(GameHUD 컨테이너+RunHUD+HitMarker+ThreatIndicator). **설계 정제(2026-06-09)**: 근접/사각지대 위협의 *상시 시각 표시*는 번잡으로 제외 → **사운드 등 타 방식으로 이전**(오디오 단계). 핑/Gibs/사각오디오는 후속. **히트마커 최종 연출**은 크로스헤어/발사체 작업 후 재확인.
- **사각 위협 오디오 당김 (확정 2026-06-10)**: 등 뒤/사각 접근 경고는 1인칭 스웜의 *코어 플레이 가능성*에 직결(폴리시 아님 — 시야 밖에서 죽으면 불공정 체감) → **최소 사각 경고 오디오(방향성 괴성/경고음)를 P4-D 말~P5로 당겨 코어 루프·재미 게이트(§7-5)와 함께 검증**. 풀 오디오/이펙트 폴리시는 P7 유지.
- **사각 오디오 Z축/높이 (B9, 2026-06-29)**: V1 사각 경고가 수평면(2D)만 다뤄 머리 위/아래 적(높은 플랫폼·비행)을 못 잡던 것을 **3D 탐지 + 피치 변조**로 확장. 3D 거리 컬(직상방 적도 감지) + 수평 콘(등 뒤/측면) **또는** 가파른 고도각(`VerticalBlindspotAngleDeg`=화면 상/하 이탈)을 사각으로 판정. 스테레오 패닝이 못 전하는 **고도는 큐 피치로 전달**(위=고음 `AboveThreatPitch`, 아래=저음 `BelowThreatPitch`, 고도각 선형 보간). `UFPSRBlindspotAudioComponent`(로컬·비복제). 전후 분간(HRTF)은 U13 잔존.
