# PlayerFeel — 카메라 / 생존·이동 / 게임필 (FPSRoguelite SSOT 분할)

> `Game.md`(SSOT 허브)의 분할 문서. **섹션 번호(§x)는 원본 그대로 보존** — 소스 주석·교차참조 호환.
> 작업 시작 전 허브 `Game.md` + `PROGRESS.md`를 먼저 읽고, 카메라/메시·플레이어 생존(DBNO)·대시·게임필/피드백/공간지각·HUD 관련 작업 시 본 파일을 연다.
> 담는 섹션: §2-9 카메라·메시 / §2-13 플레이어 생존·이동 / §2-14 게임필·피드백·공간 지각.

---

### 2-9. 카메라 / 메시
> ⚠️ **2026-07-29 전면 개편 — 상세·근거·기각안 = [ADR 0002](../Architecture/0002-true-first-person-shared-animation.md).** 아래는 요약이며, 충돌 시 ADR 0002가 우선.
> 이전 규칙(**Separated Arms** = 본인 FP팔`OnlyOwnerSee` + 타인 3P`OwnerNoSee` / **True First Person 풀바디 렌더링 사용 안 함**, 가독성·속도감 우선)은 **폐기**. 폐기 사유 = 1인칭·3인칭을 **3P 애니메이션 에셋 한 벌**로 덮기 위함(`Content/Rifle_01`).

- **True First Person**: 본인도 자기 몸을 본다. **1P 전용 팔 메시(`FirstPersonArms`)·PWAS(유료 구매분) 폐기**, 메시는 3P 바디 하나. 로컬 플레이어만 `HideBoneByName("head")` 로 머리를 숨긴다(자식 본=머리카락/눈/안경 자동 동반). 숨김 조건은 **"이 폰의 눈으로 보고 있는가"** — "로컬 플레이어인가"가 아니다(디버그 3P·DBNO 3P에서 머리 없는 캐릭터 방지).
- **회전은 두 층**: **캡슐 요 = 컨트롤 요(즉시, 현행 유지)** — 이동 입력·벽감지·슬라이드가 캡슐 방향 기준이라 여기를 분리하면 이동이 깨진다. 하체 지연 회전·제자리 돌기는 **메시 상대 요를 AnimBP가 반대로 돌려서** 만든다(순수 시각, 복제 없음).
- **카메라 = 눈 앵커**: 캡슐 소유. head 본은 **참고값**으로만 읽어 감쇠 후 **캡슐 안으로 클램프**. 감쇠 0 = 종전 고정 카메라와 동일.
  - **사격 원점 = 화면 카메라 그 자체**(`GetPlayerViewPoint`). 그래서 카메라가 캡슐을 못 나가는 것이 곧 "벽 너머 사격 불가"다. **ADS 잔여 오차 보정도 클램프 뒤에 온다.**
  - 전제: *살아 있는 플레이어의 뷰 타겟은 언제나 자기 눈 앵커다.* 킬캠·연출 카메라·살아 있는 채로 관전을 시핑에 넣으려면 ADR 0002 불변식 2를 먼저 다시 볼 것.
- **ADS = 혼합**: 팩 조준 포즈를 기본으로 두고 **사이트 잔여 오차만** 보정. 보정 주체(카메라 vs 상체 본)는 리타게팅 후 오차를 보고 결정(연기).
- **3P/관전 카메라**: 스프링암 리그. **각도·거리는 보는 쪽(관전자 PlayerController)이 소유**하고, **대상이 없어도 리그는 남는다**(팀 와이프 직전 시체 고정 방지). 리그는 시핑 포함(관전용), **"살아 있는 자기 폰을 본다"는 디버그 모드만 개발 빌드 한정**(`#if ENABLE_DRAW_DEBUG`).
- **무기 메시 = 1벌**: 무기 DA의 1P/3P 이원화 필드(`WeaponMesh3P`·`FireMontage3P`·`ReloadMontage3P`·`WeaponAttachSocket3P`) 통합 — 조사 결과 3P 필드는 **어느 DA에도 저작된 적 없음**(=현재 원격 관측자에게 아군 무기가 안 보임). 통합 시 잃을 데이터 0.
- **1P 무기 = 모듈러 파츠**(W-U1): 무기 메시에 파츠(배럴/사이트/스톡…)를 자식 부착, 스탯/프래그먼트로 진화 교체. 사이트 파츠가 절차적 ADS `AimSocket` 앵커 제공. 상세·격리계약 = CombatWeaponCard §2-4-3(코스메틱·복제0). **True FPS 전환 후에도 유지**되며, 파츠 컴포넌트의 `SetOnlyOwnerSee(true)`만 제거(모두가 봄).
- **애니메이션 배선 규칙**: 노티파이가 게임플레이 상태를 바꾸지 않는다(재장전 완료·발사·장착 완료의 주인은 코드). 애니 상태는 복제하지 않는다. 런타임 코드는 본 이름을 알지 않는다(데이터로). 카메라 위치는 프레임당 한 번만 쓴다.

### 2-13. 플레이어 생존 / 이동 (협동, 확정 2026-05-30)
- **수동 부활(DBNO: Down But Not Out)**: 체력 0 시 즉시 사망하지 않고 쓰러진 상태로 전환. 생존 아군이 쓰러진 캐릭터 근처 일정 반경에 머물면 **부활 게이지**가 차오르고, 가득 차면 부활(서버 권위). 플레이어 상태기계 Alive / DBNO / Dead.
  - **미니설계 확정(2026-06-29, U9/Phase 1B — 사망모델 = 정식 DBNO 당겨옴)** → 상세 [`Docs/DBNO_MiniDesign.md`](../DBNO_MiniDesign.md). 기존 시임만 확장(신규 중앙클래스 0): `AFPSRPlayerState` `bIsDead`→`ELifeState{Alive,DBNO,Dead}`(복제·OnRep 입력게이트), `IsAlive()`=Alive(**DBNO=not-alive**)로 `GetLivingPlayerCount`/`AreAllPlayersDead`/`RefreshPauseState`(A4) 자동 정합, `HandleOutOfHealth`→DBNO 전환, 적 타겟선정 `IsAlive()` 필터(B17). 부활 = **신규 `UFPSRReviveComponent`**(근접 Alive 아군 반경 체류→`ReviveProgress` 복제 누적→완료 시 50%HP 복구; 전역 프리즈 중 정지).
  - **확정 규칙**: 다운 = **제자리 정지 + 살아있는 아군 시점 관전**(이동·시점·사격/점프/대시 차단; 부활 = 쓰러진 자리 기상) [변경 2026-06-30, PIE 후 — 크롤 폐기, B16 관전을 DBNO로 당겨옴: 서버 `SetViewTargetWithBlend`→가까운 Alive 아군, `UpdateDownedSpectate` 틱 유지, `PerformRevive`→본인 시점 복원] · 다운 중 **무피해+비타겟**(적 수백 즉사 방지, 압박은 와이프 위험으로) · **블리드아웃 = 시임만 기본 비활성**(부활/와이프까지 다운 지속, 값은 밸런스 후속) · 부활 = **근접 자동**(아군 1명+면 충전) · 부활 직후 **PostReviveInvuln**(기본 5s, 무적+적 충돌무시 — 갓-부활 즉시 재다운 방지, `PostReviveInvulnSeconds` 편집가능) · **카드선택 프리즈 종료 후 grace**(기본 3s 무적+적통과, `PostFreezeInvulnSeconds` — 포위된 채 재개 시 즉사 방지, 부활과 같은 `BeginGraceWindow` 재사용) · 부활시키는 사람도 **부활 게이지 HUD 노티**(생존자=게이지+설명만, 다운자=비네트+게이지) · 팀와이프 = 생존(Alive) 0 → 전원 Dead → `EndRun(Defeat)`(솔로 다운 = 즉시 Defeat). 블리드아웃 값·풀 관전 리그(타겟 순환)·다운 반격은 후속.
  - 구현 = **Phase 1B**(서버권위·복제 = Opus 직접), SSOT-우선 후 코드. (이전 "구현 P5"에서 앞당김 — `Docs/Archive/gates/U1_PostGate_Fixes.md` 결정.)
- **충돌무시 대시(Dash)** — ⚠️ **구현 폐기(2026-07-28), 기획은 유효**: 적·아군 충돌을 무시하고 통과하는 짧은 거리 회피기(모든 캐릭터 기본 제공). 적에게 완전 포위돼 카메라가 막히는 상황 탈출용. (쿨다운/차지는 밸런스 후속) — §3 무브먼트
  - **폐기 사유**: 구 구현(`Input_Dash` → `ServerDash` RPC → `LaunchCharacter`)이 [ADR 0001](../Architecture/0001-player-movement-state-ownership.md) **불변식 2(클라 예측 + 서버 확인)를 위반** — 서버 응답을 기다렸다 움직이므로 ping 46ms 기준 러버밴딩. 지속 상태(슬라이드 등)로 확장 불가.
  - **재제작 예정**: 신규 `UFPSRCharacterMovementComponent` 위에서 커스텀 플래그 + 예측 방식으로 다시 만든다. 대시의 **적·아군 통과 창(collision-ignore)** 자체는 부활/프리즈 후 grace 및 DBNO와 `RefreshPawnCollisionResponse`를 공유하던 구조라, 재제작 시 같은 헬퍼에 항을 다시 추가하면 된다.
  - **폐기 시 남긴 것**: `Content/Input/IA_Dash` + `IMC_Default` 키 매핑(재사용 예정, 현재는 눌러도 무동작). **제거한 것**: C++ 전체 · `Ability.Movement.Dash` 게임플레이 태그(참조 0건이었음).
- **플레이어 이동 = `UFPSRCharacterMovementComponent` 단일 소유 (구조 확정 2026-07-28)** — 구조·불변식 9개·기각안 상세 = [ADR 0001](../Architecture/0001-player-movement-state-ownership.md). 요지: 이동 상태의 주인은 무브먼트 컴포넌트 하나이고, 사격/애님BP/HUD는 `CanFireInCurrentState()`·`GetSpreadMultiplier()`·`IsSliding()`·`IsOnWall()` 4개로 **질의만** 한다(새 이동 상태가 무기 코드로 번지지 않게). 모든 상태는 **클라 예측 + 서버 재시뮬**(서버 응답 대기형 이동 금지). 수치는 전부 `EditDefaultsOnly`/커브 에셋.
  - **구현 완료(1·2단계)**: 앉기(엔진 `bWantsToCrouch` 재사용) · **슬라이드** · 사격 확산 배수 제공. **커스텀 네트워크 슬롯 0개**(진입 의도=크라우치 플래그 재사용, 파생 상태는 `FSavedMove_FPSR` 로컬 재생용) → 스톡 CMC 대비 대역폭 증가 없음.
  - **슬라이드 규칙**: 달리기 중 앉기 입력 + 최소속도(기본 450) 이상 → 진입(속도 ×1.5 부스트). 종료 = 키 해제 / 최소속도(250) 미달 / 시간상한(1.6s) / 공중 이탈 / 프리즈·DBNO. **모든 종료 경로에 쿨다운**(기본 0.8s, 무한 슬라이드 방지). 슬라이드 중 **사격 가능**(확산 ×1.3) · **점프로 취소 가능**(속도 유지 — 공중 마찰/감속이 엔진 기본 0). **공중에서는 앉기·슬라이드 불가**(엔진은 허용하지만 설계상 금지).
  - **속도 곡선(선택)** = `/Game/Config/Character/Curve_GroundSpeed`(X=초, Y=0→600 cm/s) · `Curve_SlideSpeed`(X=초, Y=900→300 cm/s, **실제 진입속도로 상한**). 값은 literal cm/s이며 카드로 `MaxWalkSpeed`가 오르면 `SpeedCurveReferenceSpeed` 대비 비율로 곡선 전체가 스케일된다. 미할당 시 등가속·등감속 폴백(곡선은 정제 수단이지 필수 아님).
  - **확산 배수**(§2-14 heat 시스템이 소비 — 배선은 후속): 앉기 ×0.8 / 공중 ×1.6 / 슬라이드 ×1.3. 상태는 곱하지 않고 **배타 선택**(공중 > 슬라이드 > 앉기).
  - **걷기 속도 = 층 합성, 기록자는 무브먼트 컴포넌트 하나 (확정 2026-07-29)** — `MaxWalkSpeed`를 쓰는 곳이 여러 군데면 서로를 지운다(장비 보너스를 켠 뒤 속도 카드를 먹으면 보너스가 날아감). 그래서 `RefreshWalkSpeedCap()` **한 함수만** `MaxWalkSpeed`에 쓴다:
    `MaxWalkSpeed = (다운? DownedWalkSpeed : (장비 WalkSpeed>0 ? 장비값 : 저작 기본값)) × MoveSpeedMultiplier`
    | 층 | 값 | 소유 | 밀어넣는 쪽 |
    |---|---|---|---|
    | 저작 기본값 | `AFPSRCharacter::BaseWalkSpeed` = 600 | 캐릭터 BP(EditDefaultsOnly) | `SetAuthoredBaseWalkSpeed()` |
    | 장비 | 무기 DA `WalkSpeed`(0 = 기본값 사용) — 근접/맨손 700 | 무기 DataAsset | 장착 시 `SetLoadoutWalkSpeed()` |
    | 런타임 배수 | `MoveSpeedMultiplier`(카드) | GAS 어트리뷰트 | `SetMoveSpeedMultiplier()` |
    | 다운 | `DownedWalkSpeed` = 0 | 무브먼트 컴포넌트 | `SetDownedLocomotion()` |
    - 프로퍼티는 **옮기지 않는다** — 캐릭터가 저작값을 밀어넣기만 한다(ADR 0001 "GAS는 무브먼트에 수치만 밀어넣는다"와 같은 방향). BP 값·hip sway 정규화 기준·GE 참조가 그대로 유지된다.
    - 부수 수정: 이전에는 **DBNO 중 속도 카드가 적용되면** `MaxWalkSpeed`가 0에서 되살아나 쓰러진 플레이어가 움직였다. 단일 기록자가 되며 소멸.
    - 장비 속도 변경은 **예측하지 않는다**(서버 `EquipSlot` + 클라 `OnRep`에서만 적용) → 슬롯 전환 시 1 RTT 지연. 상한은 가속 램프가 접근하는 값이라 체감되지 않고, 예측하면 서버가 장착을 거절했을 때 클라가 빠른 속도에 고착되는 구멍이 생긴다. 실제로 보정이 보이면 그때 예측을 넣는다.
  - **슬라이드 진입 상한 = 900 → 1000 (2026-07-29)**: 진입 임펄스는 `min(max(V, min(V×1.5, SlideMaxEntrySpeed)), SlideMaxSpeed)`로 **그 순간 속도 V에서 파생**된다. 상한만 1000으로 올리면 걷기 600 → 900(무변화), 걷기 700 → 1000. **무기 DA에 슬라이드 값을 따로 두지 않는 이유**: 걷기 속도를 바꿀 때마다 슬라이드 상한도 맞춰 고쳐야 하는 이중 저작이 되고, "장착 시점"이 아니라 "그 순간 velocity"(이미 예측·보정되는 상태)에서 파생돼야 클라/서버가 항상 같은 값을 본다.
    - ⚠️ **영향 구간 = V ∈ (600, 1000)**: 속도 카드·내리막 모멘텀·벽점프 착지 등으로 이 구간에서 슬라이드하면 진입속도가 오른다(예: V=650이면 900→975). V≤600과 V≥1000은 무변화. 되돌리려면 `SlideMaxEntrySpeed` 한 값만 900으로.
  - **후속**: 벽 매달리기·등반·벽점프(3단계) · GAS 어트리뷰트 연결(카드로 이동 수치 변경) · `GetSpreadMultiplier()`의 heat 시스템 소비 배선 · 대시 재제작.
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
