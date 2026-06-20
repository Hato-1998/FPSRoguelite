# U4 — 보스 콘텐츠 실행 레시피 (VibeUE MCP)

> **U3 코드 베이스(2026-06-21 main 머지 `0181ed0`) 위의 콘텐츠 단계.** 이 문서는 **에디터 열고 시작한 세션**에서 VibeUE MCP로 그대로 실행하는 레시피다. 브랜치=**main 직접**(사용자 결정). 끝나면 PIE 체크리스트(맨 아래)로 전체 플로우 검증 → PROGRESS ✅ → (콘텐츠 동반 커밋).
> 전제: [[vibeue-mcp-capabilities]] · [[headless-gas-content-authoring]] · [[marketplace-asset-import-relocate]] · 보스 코드 표면은 `Source/FPSRoguelite/Public/Boss/*` + PROGRESS U3 섹션.

## 세션 시작 (필수 순서)
1. **언리얼 에디터를 먼저 켠다**(FPSRoguelite.uproject) — 새 보스 C++ 클래스 로드 + VibeUE 플러그인이 MCP 서버 기동(127.0.0.1:8088).
2. 그 다음 **Claude 세션 시작** → VibeUE-Claude 자동 연결(`~/.claude.json`). `mcp__unreal_editor__*`(죽은 Aura)는 쓰지 말 것.
3. ⚠️ MCP로 BP 반복 재컴파일 후 **PIE 전 에디터 재시작 필수**(REINST world-leak 크래시 예방, [[vibeue-buildgraph-pie-worldleak]]).

## C++ 표면 (이미 있는 것 — 신규 코드 0 목표)
| 클래스 | 필드/함수 (정확명) |
|---|---|
| `AFPSRBossBase` (Boss/) | 부모로 상속. `DefaultMaxHealth`(EditDefaultsOnly, 1000=폴백 테스트값) / 캡슐 ECC_Pawn·`UFPSREnemyHealthComponent`·`BodyMesh` 내장 / OnDeath→승리 자동 |
| `UFPSRBossDefinitionDataAsset` (Boss/) | `BossClass`(TSubclassOf<AFPSRBossBase>) · `MaxHealth`(10000 기본, BP 폴백 override) · `bUseBossSpawnPoint`(기본 true) · `GetDescription()` |
| `AFPSRBossSpawnPoint` (Boss/) | `Weight`(float) · `bEnabled`(bool) — 맵 배치용, 빨간 에디터 화살표 |
| `UFPSRWeakpointComponent` (Combat/) | `DamageMultiplier`(>=1.0, 기본 2.0) — BP에 add component, 뷰포트서 위치·크기 |
| `UFPSREnemyHealthComponent` (Enemy/) | `GetHealth()` · `GetMaxHealth()` · `IsDead()` (전부 BlueprintPure) — 체력바 바인딩 |
| `UFPSRRunScheduleDataAsset` | `BossDefinition`(TObjectPtr) — DA_RunSchedule에 보스정의 지정 |

## 산출물 단계

### 1. BP_Boss (부모 = AFPSRBossBase)
- 경로 예 `/Game/Boss/BP_Boss`(폴더 신설 OK). 부모 클래스 = `FPSRBossBase`.
- **약점 존(U3a 인프라 소비)**: `UFPSRWeakpointComponent`를 1개 이상 add component → 캡슐 상단(머리)에 배치(상대 위치 z↑), `SphereRadius`·`DamageMultiplier`(예 2.0~3.0) 지정. 코어 부위 추가 가능(다중 약점=명중 시 최대 배수). **신규 약점 코드 작성 금지**(컴포넌트 add만).
- 메시: `BodyMesh`(내장 큐브)로 충분(플레이스홀더). 실보스 메시는 장기 백로그.
- ⚠️ **콜리전 손대지 말 것** — 캡슐 ECC_Pawn은 C++ 생성자가 이미 설정(WorldStatic 금지=자기 몸 탄 차단 함정). BP에서 캡슐 오브젝트타입 변경 금지.
- 컴파일 0err.

### 2. DA_BossDefinition (UFPSRBossDefinitionDataAsset)
- 경로 예 `/Game/Boss/DA_BossDefinition`. `BossClass = BP_Boss`. `MaxHealth` = 실보스 체력(예 10000, PIE 반복이 길면 낮춰 테스트 후 복원). `bUseBossSpawnPoint = true`.
- IsDataValid 경고 없도록 BossClass 지정 확인.

### 3. DA_RunSchedule에 BossDefinition 지정
- 기존 `DA_RunSchedule`(GameMode가 주입) 열어 `BossDefinition = DA_BossDefinition` 지정. (미지정 시 C++ 폴백 보스가 뜸 — 동작은 하나 약점/실체력 없음.)
- ⚠️ **BossTime 300s 임시값 유지**(원복 금지, 빠른 반복용 — 원복은 U14, [[p4a-temp-test-values]]).

### 4. L_Sandbox에 AFPSRBossSpawnPoint 배치
- L_Sandbox 열어 `AFPSRBossSpawnPoint`를 **맵 중앙**(P7 §3-5 "맵 가운데 박스")에 배치. **Z=바닥+캡슐 half-height(200) 보정**(공중/매몰 방지 — gravity off 스캐폴드라 스폰 위치에 그대로 정지).
- ⚠️ **보스 발밑 바닥 = Mobility=Static + Collision Block(WorldStatic)** 유지(적 바닥 gotcha와 동일; 실보스 이동 활성 시 필요). WorldDynamic 금지.
- 배치하면 디렉터 `SelectBossSpawnTransform`이 플레이어-폴백 대신 이 지점 사용(로그 경고 사라짐).

### 5. 보스 체력 표시 (간이)
- **솔로 우선**: WBP(ProgressBar) 또는 디버그 텍스트를 보스 `HealthComponent->GetHealth()/GetMaxHealth()`에 바인딩. 화면상단 바 또는 보스 위 WidgetComponent.
- ⚠️ **MP 주의**: `MaxHealth`는 현재 서버전용(복제 안 함) — **솔로 PIE(리슨/스탠드얼론)에선 정상**, 원격 클라에선 Max가 stale. MP-정확 체력바는 `UFPSREnemyHealthComponent`에 MaxHealth 복제 추가(소량) 또는 정식 HUD=U12/U13로 이연. U4 검증은 솔로 PIE로 충분.
- VibeUE 한계: 위젯 getter는 컴파일 후 / FText=`Conv_StringToText` / config=ini 직접([[vibeue-mcp-capabilities]]).

## PIE 검증 체크리스트 (전체 플로우)
① 메뉴→Play→런 시작 ② 미션 발생·클리어·카드 보상 ③ BossTime(300s) 도달 **또는** `FPSR.SkipToBoss`→보스 스폰(중앙 스폰포인트) ④ 전 무기 8종 보스 데미지 + **약점 명중=추가뎀·Weak 마커 / 본체=기본뎀** ⑤-a 보스 처치→VICTORY→로비 복귀(U11a 자동) ⑤-b (별도 런) 전원 사망→DEFEAT ⑥ 결과창→재시작 상태리셋 무결.
- 약점 PIE 확인 시 `FPSR.Debug.WeaponDraw 1`로 트레이스 시각화 가능.
- 완료 시: PROGRESS ✅ + TaskPrompts U4 ✅ + 콘텐츠 커밋(맵/BP/DA — LFS 주의) → 다음 = **W1 전체 검증**.
