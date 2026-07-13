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
  - 구현 = **Phase 1B**(서버권위·복제 = Opus 직접), SSOT-우선 후 코드. (이전 "구현 P5"에서 앞당김 — `Docs/Archive/gates/U1_PostGate_Fixes.md` 결정.)
- **충돌무시 대시(Dash)**: 적·아군 충돌을 무시하고 통과하는 짧은 거리 회피기(모든 캐릭터 기본 제공). 적에게 완전 포위돼 카메라가 막히는 상황 탈출용. (쿨다운/차지는 밸런스 후속) — §3 무브먼트, 구현 P2
- **최대체력 증가 = 즉시 회복(확정 2026-06-02)**: `MaxHealth`가 증가하면(체력 카드 등) **증가분만큼 현재 `Health`도 함께 증가**(서버 권위, `UFPSRHealthSet::PostAttributeChange`에서 처리·새 최대치로 clamp). +체력 업그레이드 선택의 즉각적 체감. 감소 시는 다음 Health 변경 때 clamp로 캡.

### 2-14. 게임필 / 피드백 / 공간 지각 (확정 2026-05-30)
1인칭 스웜 특성상 사각지대(등 뒤·측면) 무방비를 해소하고 대량학살 쾌감을 극대화한다.
- **공간 지각**: 사각지대(특히 등 뒤) 접근 시 괴성/경고 사운드 + **화면 테두리 방향성 위협 인디케이터(Threat Indicator) UI**(시야각 밖에서 적 접근/공격 판정 거리 진입 시)
- **오디오**: 몬스터 발소리 구체화, 위협/경고 사운드(Significance 티어 연동 — §5-1)
- **타격감(Juice)**: 크로스헤어 **히트마커** + 피격 사운드 / 적 처치 시 **경량 파편(Gibs)·팝 연출**(과도한 연산 금지) / 크리티컬·처치 시 **가볍고 맑은 핑(Ping)** 사운드
- 구현: 핵심(히트마커·핑·위협 인디케이터·기본 오디오) **P4**, 폴리시 **P7**
- **구현 상태(P4-D)**: `UFPSRPlayerFeedbackComponent`(로컬·비복제·이벤트형) + PC Client RPC. ① **히트마커**(서버권위 Hit/Crit/Kill, 활성화당 1회, Unreliable) ② **피격 방향 인디케이터**(CoD식: `ApplyContactDamage`→오너클라, 카메라 기준 각도) ③ **원거리 타겟 사전경고**(다수소스 id별 추적·각도배열·추적Tick·Reliable; 생산자=원거리 적 AI는 후속, 디버그 `FPSR.TestDamageDir`/`FPSR.TestRangedWarn`). WBP(GameHUD 컨테이너+RunHUD+HitMarker+ThreatIndicator). **설계 정제(2026-06-09)**: 근접/사각지대 위협의 *상시 시각 표시*는 번잡으로 제외 → **사운드 등 타 방식으로 이전**(오디오 단계). 핑/Gibs/사각오디오는 후속. **히트마커 최종 연출**은 크로스헤어/발사체 작업 후 재확인.
- **크로스헤어 크기 설정 (U17, 2026-07-03)**: 로컬 플레이어 설정으로 크기 조절(0.5~2.5배, 기본 1.0, 세션 간 영속·인게임 실시간 반영). **적용 대상=런HUD 머티리얼 크로스헤어**(`UFPSRRunHUDWidget::CrosshairImage`, WBP_RunHUD)에 RenderTransform Scale — 슬라이더 드래그 시 `UFPSRGameUserSettings.OnCrosshairSettingsChanged` 델리게이트로 즉시 재적용(런HUD가 Construct 구독/Destruct 해제). 설정 UI=`FPSRSettingsWidget`, 영속=`UFPSRGameUserSettings`(§4-1 Settings). ⚠️ V3 `WBP_BasicCrosshair`는 고아(어떤 위젯도 미참조) — 실제 인게임 크로스헤어는 CrosshairImage. 범위=크기만(색/두께/불투명도는 후속).
- **파라메트릭 크로스헤어 시스템 (U12, 2026-07-03 설계)**: 기존 "4팔 든 한 장 텍스처(T_CH001)를 UV warp해 벌리기" 방식은 packed-texture fold + 축소 밉 프린지로 최대확산서 대각선 고스트·중앙 뭉침 아티팩트 유발(3패치로 완화했으나 근절 불가) → **실제 FPS(CS2/발로란트/Apex)의 파라메트릭 방식으로 재구축**: 크로스헤어=독립 요소를 코드/절차적으로 그리고 **분산도로 요소 간격(gap)만 이동**(텍스처 warp 아님). **kind마다 전용 절차적 SDF 머티리얼**(`fwidth` 아날리틱 AA → 임의 크기 선명, 밉/폴드 아티팩트 원리상 소멸, 오프라인=인게임 렌더 일치로 자가검증 가능). 각 머티리얼이 기존 `Spread` 파라미터를 자기 기하로 해석. **무기(군)별 할당 = 기존 `UFPSRWeaponFireComponent::GetEquippedCrosshairMaterial()` 그대로**(무기별 크로스헤어 머티리얼 반환, 신규 배선 0), **U17 RenderScale(크기설정)·런HUD Spread 세팅 그대로 유효**. **중앙 스위치 0**(새 kind=SDF 머티리얼 저작+데이터 할당, C++ enum/switch 없음 — [[extensibility-first-designer-tooling]]), 파라미터=MI 오버라이드(기획자 무기별 튜닝: 팔 길이/두께/둥글기·링 반경·박스 크기·점 배치·색·외곽선). **초기 4 kind**: ①**Cross**(축 4팔 짧고 굵음, gap=분산도) ②**샷건 Ring**(상·하 뚫린 원형=좌우 아크, 반경=펠릿 분산) ③**발사기 BoxDots**(코너 사각형+안쪽 점, 분산도 커지면 바깥 사각형만 벌어지고 점 고정) ④**Dot**(중앙 점, 근접, 정적). 기존 텍스처 크로스헤어+warp 머티리얼=은퇴(원하면 정적 옵션 병존).
- **사각 위협 오디오 당김 (확정 2026-06-10)**: 등 뒤/사각 접근 경고는 1인칭 스웜의 *코어 플레이 가능성*에 직결(폴리시 아님 — 시야 밖에서 죽으면 불공정 체감) → **최소 사각 경고 오디오(방향성 괴성/경고음)를 P4-D 말~P5로 당겨 코어 루프·재미 게이트(§7-5)와 함께 검증**. 풀 오디오/이펙트 폴리시는 P7 유지.
- **사각 오디오 Z축/높이 (B9, 2026-06-29)**: V1 사각 경고가 수평면(2D)만 다뤄 머리 위/아래 적(높은 플랫폼·비행)을 못 잡던 것을 **3D 탐지 + 피치 변조**로 확장. 3D 거리 컬(직상방 적도 감지) + 수평 콘(등 뒤/측면) **또는** 가파른 고도각(`VerticalBlindspotAngleDeg`=화면 상/하 이탈)을 사각으로 판정. 스테레오 패닝이 못 전하는 **고도는 큐 피치로 전달**(위=고음 `AboveThreatPitch`, 아래=저음 `BelowThreatPitch`, 고도각 선형 보간). `UFPSRBlindspotAudioComponent`(로컬·비복제). 전후 분간(HRTF)은 U13 잔존.
- **HUD 위협 큐 원칙 (컨설트 2026-07-01 F3 — `Docs/Review/20260701-concept-conclusions.md`)**: 1인칭×수백 적 가독성의 핵심 = *"잡몹은 오디오/압력으로 집계, 특수·원거리·DBNO·아군위험만 HUD로 선명하게"*. **잡몹 개별 시각 표시 금지**. 시각 위협 큐 하드캡 = **동시 ≤3 + 후방 집계 1**(45도 섹터 병합, 개수는 밝기/펄스로 표현). **오디오 경고 동시 1·쿨다운 1.0~1.5s**. 우선순위 = 즉시성(<1.5s) > 특수/핀/원거리 > DBNO/아군위험 > 근접 잡몹. **팀 인지**(협동 필수): 아군 실루엣/거리/다운 위치 + 시선 부채꼴(미니 컴퍼스), **핑**(수동 1키 + 위험 자동 핑=원거리 차징·DBNO·특수 피격·과도 FF). ⚠️ 화살표 수십 개 = 협동 아니라 HUD 소음 = 실패. (USP 검증 게이트=Performance §5 F6)
