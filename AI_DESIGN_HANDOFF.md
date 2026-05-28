# FPSRoguelite 설계 보좌 인수인계

> 목적: 다른 AI/리뷰어에게 현재 프로젝트의 설계 방향, 실제 구현 상태, 보완이 필요한 지점을 빠르게 전달하기 위한 문서.
> 기준 문서: `DESIGN.md`가 단일 진실 공급원이며, 설계 변경은 반드시 `DESIGN.md`를 먼저 갱신해야 한다.

## 1. 프로젝트 정체성

이 프로젝트는 **1인칭 FPS x 뱀파이어 서바이벌 x 4인 협동 로그라이트**다.
레퍼런스는 The Spell Brigade에 가깝고, Overwatch/Valorant/Apex류 Hero Shooter가 아니다.

핵심 규모:

- 플레이어: 1~4명
- 적: 수백 마리, 목표 약 300~500
- 네트워크: 리슨서버 P2P, 서버 권위, Push Model 기본
- 런 구조: 로비 메타 강화 -> 약 30분 런 -> 보스/클리어 -> 보상
- 맵: 고정 맵, PCG 비채택

절대 임의 도입하지 말 것:

- Lyra 풀 fork
- 모든 적에 GAS/ASC
- 적별 StateTree/BehaviorTree + NavMesh 길찾기
- Iris 핵심 의존
- MassEntity 선도입
- Server-Side Rewind, Motion Matching, Bhop/Wall-run, True First Person 풀바디

GAS는 **플레이어 1~4명과 보스/엘리트 소수**에만 사용한다. 스웜 적은 `UHealthComponent` 기반 경량 데미지 처리.

## 2. 현재 실제 구현 상태

현재 코드는 P0/P1 초입 스캐폴드 수준이다.

이미 잡혀 있는 것:

- UE 5.7 타깃 설정
  - `Source/FPSRoguelite.Target.cs`
  - `Source/FPSRogueliteEditor.Target.cs`
- 주요 플러그인 enable
  - EnhancedInput, GameplayAbilities, ModularGameplay, GameFeatures, CommonUI, StateTree, GameplayStateTree, SignificanceManager
- Push Model 설정
  - `Config/DefaultEngine.ini`의 `net.IsPushModelEnabled=1`
- 기본 Core 클래스
  - `AFPSRGameMode`
  - `AFPSRGameState`
  - `AFPSRPlayerController`
  - `AFPSRPlayerState`
- 플레이어 ASC 구조
  - `AFPSRPlayerState`가 `UFPSRAbilitySystemComponent`를 소유
  - `AFPSRCharacter`가 `PossessedBy` / `OnRep_PlayerState`에서 ASC ActorInfo 초기화
- AttributeSet 초안
  - `UFPSRHealthSet`
  - `UFPSRCombatSet`
- GameplayTags 초안
  - Hero, Weapon, Card, Run, Enemy, Pickup, Mission, Boss, UI, Ability, GameplayEvent, Status
- 모듈 로드 Smoke Test
  - `Source/FPSRoguelite/Private/Tests/FPSRSmokeTest.cpp`

아직 없는 것:

- 실제 테스트 맵 / 프로젝트 기본 맵
- 프로젝트 GameMode의 config 기본 연결
- Enhanced Input 액션/매핑 자산 연결
- CommonUI viewport/input data 설정
- Separated Arms 카메라/메시 구성
- 무기/카드/런/픽업/적/보스/메타 프로그레션 실제 시스템
- 적 스웜 성능/복제 예산 구현
- Flow-Field, Pooling, Significance 실제 코드

## 3. 설계 방향에 대한 판정

`DESIGN.md`의 큰 방향은 최신 UE 흐름과 프로젝트 규모에 대체로 맞다.

특히 타당한 결정:

- 플레이어 중심 GAS 사용
- 적 스웜에 ASC/GAS 미사용
- 적 AI를 개별 고비용 AI가 아니라 Flow-Field + steering + batch update로 설계
- Lyra를 통째로 fork하지 않고 필요한 개념만 경량 재구현/체리픽
- Iris를 기본 의존으로 두지 않고 Push Model을 기본으로 사용
- MassEntity를 목표 규모 300~500에서는 선도입하지 않음
- CommonUI, EnhancedInput, GameplayTags, DataAsset 기반 설계 채택

단, 현재 설계 문서는 몇몇 지점에서 너무 선언적이다. 실제 구현 전에 아래 보완이 필요하다.

## 4. 우선 보완/수정 필요 사항

### 4.1 기본 프로젝트 연결

문제:

- `AFPSRGameMode`는 있지만 `DefaultEngine.ini`에 기본 GameMode 지정이 보이지 않는다.
- 기본 맵이 `/Engine/Maps/Templates/OpenWorld`로 남아 있다.

보완:

- P0/P1 테스트용 고정 맵을 만들고 기본 맵으로 지정.
- `GlobalDefaultGameMode=/Script/FPSRoguelite.FPSRGameMode` 지정 검토.
- 맵별 override가 필요하면 테스트 맵에서 명시.

설계 관점:

- 이 프로젝트는 고정 맵 기반 서바이버 게임이므로 OpenWorld 템플릿 기본값은 빠르게 제거하는 편이 좋다.

### 4.2 CommonUI 설정

문제:

- CommonUI 플러그인은 enable되어 있지만 viewport/input routing 설정이 없다.

보완:

- `CommonGameViewportClient` 또는 이를 상속한 프로젝트 viewport client 설정.
- CommonUI InputData, Back/Click action data, ControllerData 계획 추가.
- 카드 선택, 레벨업 프리즈, 미션 UI, 메타 UI를 Activatable Widget Stack 레이어로 나눌 것.

주의:

- 카드 선택 UI는 전원 동시 레벨업과 연결되므로 단순 위젯 표시가 아니라 RunPhase/서버 상태와 묶어야 한다.

### 4.3 Build.cs 의존성 정리

문제:

- `.uproject`에는 CommonUI, StateTree, SignificanceManager가 enable되어 있지만 `FPSRoguelite.Build.cs`에는 아직 관련 모듈 의존성이 없다.

보완:

- C++에서 사용하기 시작하는 Phase에 아래 모듈 추가 검토:
  - `CommonUI`
  - `CommonInput`
  - `StateTreeModule`
  - `GameplayStateTreeModule`
  - `SignificanceManager`
  - 필요 시 `GameFeatures`, `ModularGameplayActors`, `ReplicationGraph`

주의:

- 지금 당장 쓰지 않는 모듈을 모두 넣기보다는 Phase별 실제 사용 시점에 추가하는 것이 좋다.

### 4.4 적 스웜 네트워크/성능 예산

문제:

- `DESIGN.md`는 "적은 최소상태/Push Model"이라고 되어 있지만, 300~500 적을 어떻게 네트워크에 태울지 예산이 부족하다.

보완할 설계 항목:

- 최대 활성 적 수
- 클라이언트별 최대 관련 적 수
- 적 위치/상태 복제 주기
- NetCullDistanceSquared 기본값
- NetUpdateFrequency/MinNetUpdateFrequency
- Dormancy 사용 여부
- 적 사망/히트 피드백을 replicated actor 상태로 보낼지, gameplay message/cosmetic event로 보낼지
- XP pickup 최대 개수 및 병합 규칙
- 발사체 actor 최대 개수
- 적 공격 판정 주기와 서버 비용

권장 방향:

- 적 actor 자체 복제는 최소화한다.
- 모든 적을 고정 주기로 고정밀 복제하지 않는다.
- 근접/화면 내/위협도가 높은 적만 더 자주 갱신한다.
- 원거리 또는 낮은 significance 적은 낮은 빈도, 보간, cosmetic-only 처리 고려.
- P2 부하 테스트 후 Replication Graph를 평가한다.

Replication Graph 판단:

- 4인/500 적이면 필수라고 단정할 필요는 없다.
- 하지만 "다수 replicated actor + 연결별 relevancy"가 병목이면 가장 먼저 검토할 후보.
- Iris보다 RepGraph가 이 프로젝트의 즉시성 있는 스케일링 문제에 더 직접적일 가능성이 있다.

### 4.5 Significance Manager는 자동 최적화가 아님

문제:

- 플러그인 enable만으로 성능이 좋아지지 않는다.

보완:

- 적, VFX, SFX, animation tick, mesh update, health bar visibility 등을 significance 단계별로 낮추는 정책 필요.
- 예:
  - S0: 가까운 위협 적, full update
  - S1: 근거리 적, 낮은 빈도 update
  - S2: 중거리 군집 적, animation/VFX 축소
  - S3: 원거리 적, coarse movement + no cosmetic

주의:

- Significance는 렌더/코스메틱뿐 아니라 AI update budget에도 연결할 수 있다.

### 4.6 레벨업 프리즈 설계

문제:

- `TimeDilation≈0`은 간단하지만 네트워크, 타이머, AbilityTask, 발사체, 이펙트, UI 입력에서 부작용이 생길 수 있다.

보완:

- 전역 TimeDilation 0에 의존하기보다 `RunPhase.LevelUpPause` 기반으로 gameplay progression을 gate하는 구조 권장.
- 적 이동/스폰/공격/무기 발사/픽업 흡수는 RunPhase로 정지.
- UI와 네트워크 RPC, 카드 선택 timeout은 정상 tick 유지.
- 카드 선택 제한 시간/자동 선택 정책 추가.

기획 보완:

- 모든 플레이어가 선택할 때까지 무기한 대기는 협동 게임 템포를 해칠 수 있다.
- 10~15초 후 자동 선택, 또는 미선택 플레이어에게 랜덤/최상위 가중치 선택 필요.

### 4.7 카드/Attribute 문구 정정

문제:

- `DESIGN.md`에는 "새 스탯 추가 = Attribute 1 + GE 1 + DataAsset 1 (코드 변경 0)"라고 되어 있다.
- Unreal GAS Attribute는 C++ AttributeSet 확장이 필요한 경우가 많다. 완전히 새로운 Attribute를 추가하는 것은 일반적으로 코드 변경 0이 아니다.

권장 수정:

- "이미 정의된 Attribute 범위 안에서 새 카드 추가 = GE + DataAsset 추가로 코드 변경 0"
- "완전히 새로운 Attribute 추가 = AttributeSet 코드 추가 + GE + DataAsset"

추가 제안:

- 글로벌 스탯과 무기별 스탯의 경계를 더 명확히 할 것.
- 무기별 스탯은 ASC Attribute가 아니라 `FWeaponStatBlock`으로 유지하는 현재 방향이 맞다.

### 4.8 Weapon Modifier Fragment 상세화

문제:

- 게임의 핵심 재미라고 명시되어 있지만, fragment 충돌/순서/네트워크 적용 규칙이 아직 부족하다.

보완할 항목:

- Modifier 적용 순서
- 같은 modifier 중첩 가능 여부
- modifier 간 충돌 해결 규칙
- 서버 권위 적용 위치
- 클라이언트 예측 가능 범위
- 저장/런 종료 시 소멸 여부
- UI 표시 방식
- `OnProjectileSpawn`, `OnHitActor`, `PostFire` 훅의 호출 순서

권장:

- Fragment를 UObject 상속 asset으로 둘지, Instanced UObject로 둘지, 순수 DataAsset + runtime struct로 둘지 결정 필요.
- 300~500 적 타격 시 `OnHitActor`가 과도한 virtual dispatch/alloc을 만들지 않게 주의.

### 4.9 적 이동: Flow-Field 범위 명확화

문제:

- Flow-Field를 "고정맵 사전계산"으로 잡은 것은 좋지만, 4인 협동에서는 목표가 1명인지, 가장 가까운 플레이어인지, 파티 중심인지가 중요하다.

보완:

- 적 목표 선택 규칙:
  - 가장 가까운 플레이어
  - 위협도/딜량 기반
  - 파티 중심 추격
  - 미션 목표물 추격
- Flow-Field를 플레이어별로 둘지, 파티 중심/목표점별로 둘지 결정.
- 장애물/문/동적 차단물이 있으면 고정 사전계산만으로 충분한지 검토.
- separation batch update 주기와 충돌 처리 범위 정의.

권장:

- P2에서는 가장 단순한 "고정맵 grid + 목표점 field + local steering"으로 시작.
- 동적 장애물은 나중에 비용 대비 필요성을 보고 추가.

### 4.10 메타 프로그레션 저장

문제:

- SaveGame 방향은 맞지만 버전/마이그레이션/프로필 정책이 아직 없다.

보완:

- Save data version
- 슬롯명 정책
- Steam Cloud 대상 파일
- 저장 실패 처리
- 해금 데이터가 삭제/이름 변경됐을 때 fallback
- 런 중 저장 여부와 로비 저장 여부 구분

권장:

- `UGameInstanceSubsystem` 기반 save manager를 두고 UI/Actor가 직접 SaveGame을 만지지 않게 한다.

## 5. 기획 관점 보완

### 5.1 런 길이

30분 런은 최종 목표로 적절하지만 초기 개발/데모 검증에는 길다.

권장:

- 데모/테스트 런: 10~12분
- 정식 기본 런: 20~30분
- 30분 보스 런은 목표로 유지

### 5.2 협동 카드 선택 템포

전원 동시 레벨업과 개별 카드 선택은 차별점이 있지만, 한 명이 늦으면 전체 템포가 멈춘다.

권장:

- 선택 제한 시간
- 자동 선택
- 선택 완료 상태 표시
- 호스트/파티원이 재촉할 수 있는 UI
- AFK 대응

### 5.3 적 이동속도 불변

시간 경과로 HP/공격력/종류/수만 증가시키고 이동속도를 불변으로 두는 것은 가독성 측면에서 좋다.

보완:

- 대신 압박감을 올릴 수단을 명시:
  - 스폰 밀도
  - 원거리 적 비율
  - 특수 적 패턴
  - 미션 목표 압박
  - 보스 phase pressure

## 6. 다른 AI에게 요청할 작업 제안

다른 AI가 설계 보좌를 한다면 다음 순서가 좋다.

1. `DESIGN.md`의 문구 보정
   - 카드/Attribute "코드 변경 0" 문구 정정
   - 레벨업 프리즈를 RunPhase 게이트 중심으로 재서술
   - 적 네트워크/성능 예산 섹션 추가

2. P0/P1 실행 준비 설계
   - 테스트 맵/기본 GameMode/CommonUI viewport 연결 항목 추가
   - EnhancedInput, Separated Arms, ASC init 검증 체크리스트 작성

3. P2 스웜 성능 설계
   - active enemy budget
   - spawn budget
   - pooling rules
   - significance tiers
   - replication relevancy/frequency rules
   - RepGraph 평가 조건

4. P3 카드/레벨업 UX 설계
   - 서버 권위 상태 흐름
   - 카드 선택 timeout
   - 다중 레벨업 queue
   - UI layer 상태

5. P4 무기 모디파이어 상세 설계
   - fragment hook contract
   - modifier stacking/conflict rules
   - predicted vs authoritative responsibilities

## 7. 최우선 결론

현재 프로젝트는 방향이 잘못된 것이 아니라, **좋은 방향의 초반 스캐폴드**다.
가장 위험한 것은 Hero Shooter식 과설계가 아니라, 반대로 "적 500마리 협동 게임"에 필요한 성능/복제 예산을 문서에 아직 수치화하지 않은 점이다.

따라서 다음 설계 보완의 핵심은 다음 세 가지다.

1. P0/P1에서 실행 가능한 기본 연결을 확정한다.
2. P2 전에 적 스웜 성능/네트워크 예산을 수치화한다.
3. 레벨업 프리즈와 카드 선택을 네트워크 친화적인 RunPhase 상태 기계로 재정의한다.

