# AssetWork 신규 팩(ModularSciFiStation + ParagonMinions) — 재개 노트 (새 세션용)

> **목적**: VibeUE MCP가 세션 중간에 연결 해제됨(서버는 살아있으나 클라 연결만 끊김). **에디터 켠 채 새 Claude 세션 시작**으로 재연결. 이 노트로 새 세션이 즉시 이어간다.
> **작성**: 2026-06-22. **브랜치**: `content/character-environment`(env 커밋 `aed9c50` 위). 원 작업 지시: 사용자 — "추가 에셋 작업 진행".
>
> ## ✅ 상태 업데이트 (2026-06-22, 실행 세션) — **🅾️🅰️🅱️ + 🅲️relocate 완료(6커밋), 🅲️기능(④b)만 남음**
> 아래 🅾️🅰️🅱️ 본문은 **실행 전 플랜**(완료됨, 참고용). 실제 결과·커밋·다음할일은 **PROGRESS.md 최상단** 참조.
> - 🅾️ 보스이동 `26288c4` · 🅰️ ModularSciFiStation(289) `2025021` · 🅱️ ParagonMinions relocate(367) `e58ea55` + 미니언 VAT(Melee스웜+Siege엘리트)+BP_EnemyBase 배선 `e0654dc` · 🅲️ Crosshair relocate(20) `1c939c0` 완료.
> - **진척(2026-06-22 실행 세션 계속)**: ✅ 구 ParagonMinions 트림 완료(에디터 닫고 rm 3.5G). ✅ **④b 크로스헤어 C++ 완료·풀빌드 Succeeded·커밋 `5b0d2de`**(DataAsset 필드+FireComponent 게터3+Hitscan DRY). **남은 것 = ④b WBP_RunHUD 4방향 라인 위젯**(콘텐츠 MCP, 에디터 재오픈 필요) → §🅲️ "처리 순서" 2-마지막 글머리. + 로비 idle 수정 미커밋(`BP_LobbyDisplayPawn`). 보스 메시 배선(Prime_Helix→BP_Boss)은 후속.

## 🔁 재시작 절차
1. **에디터 열린 채 새 Claude 세션 시작** → VibeUE-Claude(127.0.0.1:8088) 자동 재연결.
   - 진단됨(2026-06-22): UnrealEditor 실행 중(PID 20016) + 포트 8088 LISTENING. 서버 정상, **클라 세션 재시작만** 하면 붙음. 에디터 재시작 불요.
2. 새 세션: 이 노트 + `Game.md`/`PROGRESS.md` 읽고 아래 진행. **팩별 적용 방침은 사용자 확정 완료**(🅰️🅱️🅲️).
3. **🅲️ 크로스헤어는 코드+UI 기능 → 착수 시 플랜 우선**(C++ `FPSRWeaponDataAsset` 필드 + WBP). 나머지(🅾️🅰️🅱️)는 에셋 작업이라 바로 진행.

## 📦 현재 상태 (디스크 확인됨, git untracked/uncommitted) — 사용자가 에디터에서 다수 작업 진행 중
**신규 임포트 팩(미처리)**:

| 팩 | 크기 | 에셋 | 성격 |
|---|---|---|---|
| **ModularSciFiStation** | 2.3GB | 323 uasset + 1 umap | 모듈러 SciFi 스테이션 환경 키트 + 템플릿 데모 |
| **ParagonMinions** | **4.8GB** | **2105 uasset** + 2 umap | Paragon 미니언/버프 **적 캐릭터**(스켈레탈+풀 애님) + FX |
| **CrosshairFreePack** | 소 | 20 텍스처 | 크로스헤어(T_CH)·히트마커(T_HM)·킬인디(T_KI)·아머(T_AH). UE5.0–5.6 표기지만 텍스처라 5.7 무관 |

**사용자 에디터 작업(미커밋, 워킹트리)**: 보스 콘텐츠를 기존 `Content/Character/` 계층으로 정리 이동 완료(아래 🅾️). `L_Sandbox.umap`·`L_MainMenu.umap` 재dirty(스푸리어스 추정).

---

## 🅾️ 즉시 처리 — 보스 이동 커밋 (가벼움)
사용자가 에디터에서 `/Game/Boss/*` → `/Game/Character/Boss/`로 이동(`Character/Player`·`Character/Enemy` 기존 구조에 보스 합류). **검증됨(2026-06-22)**: DA_RunSchedule 참조=신 경로만, 전역 grep 구 `/Game/Boss/` 0건, BuiltData 잔여 0. git=`D Content/Boss/{BP_Boss,DA_BossDefinition,WBP_BossHealthBar}` + `?? Content/Character/Boss/` + `M DA_RunSchedule`.
→ **할 일**: `git add Content/Boss Content/Character/Boss Content/Game/Data/DA_RunSchedule.uasset` 후 `content(boss): /Game/Boss → /Game/Character/Boss 정리 이동` 커밋(사용자 확인 후). 신규 작업 아님, 정리 마감.

> 3개 팩 적용 = 🅰️ 스테이션(맵 키트) · 🅱️ 미니언(적/보스) · 🅲️ 크로스헤어(기능). **🅲️는 단순 에셋 아닌 코드+UI 기능** → 착수 시 플랜 우선.

---

## 🅰️ ModularSciFiStation — 환경 키트 (relocate 대상)
**용도(사용자 확정)**: **맵 제작용 추가 에셋**. 새 맵/기존 L_Sandbox 디테일업의 모듈 빌딩블록으로 보유. 특별 배선 없음 — relocate + 트림 후 레벨 디자인에 사용 가능 상태로 두면 끝(즉시 배치는 사용자 지시 시).

**구조**:
- `Environment/` (실콘텐츠): Door·Floor·Ladders·Lamps·Pipes·Props·Railing·Rocks·Scaffold·Stairs·Tunnel·Walls (모듈 키트)
- `Materials/`(Decals·Glass·Landscape·Light·Master·Metal·Pipes·Tiles·Trims) · `Textures/`(다수 카테고리) · `Particles/`
- **트림 대상(데모/템플릿)**: `ThirdPersonBP/`(ThirdPersonCharacter·GameMode·SK_Mannequin·UE4_Mannequin_Skeleton·애님 — UE 템플릿 잔여, 우리 무관) + `Level/SciFiStationExampleMap.umap`(데모 맵)

**플랜**(ZerinLabs 선례 그대로):
1. **트림 먼저**: `ThirdPersonBP/` 전체 + `Level/SciFiStationExampleMap`(+BuiltData) 삭제. 삭제 전 "남길 Environment/Materials가 이들 참조하는지" grep 확인(템플릿은 보통 무참조).
2. **안전 재배치**: per-asset `rename_asset` **≤40 청크**(머티리얼/텍스처 먼저=피참조, 메시 나중) → `/Game/Assets/Environment/ModularSciFiStation/`. **bulk rename_directory 금지**(메모리 함정).
3. **rename 후 강제저장 필수**: `save_directory(dst, only_if_is_dirty=False)` — rename이 referencer ref를 메모리만 갱신·dirty 미설정이라 일반저장 스킵 → 디스크 구경로 잔류. 강제저장 후 디스크 grep 구경로 0 확인.
4. 구 디렉터리 삭제 + 검증(카운트·샘플 로드·MI→부모 참조).
5. (선택) L_Sandbox 창고를 이 키트로 디테일업 — 후속, 사용자 지시 시.

---

## 🅱️ ParagonMinions — 적 캐릭터 (⚠️ 전부 옮기지 말 것)
**⚠️ 2105 에셋·4.8GB. bulk 이동 절대 금지 + 통째 relocate 비효율**(LFS 7GB 부담, 메모리 함정). **선별 relocate + VAT 베이크**가 원칙1 정합.

**캐릭터 종류**(스켈레톤 기준 distinct):
- `Characters/Buff/` — Buff 몬스터 5색: Black·Blue·Green·Red·White (각 전용 스켈레톤)
- `Characters/Minions/` — 레인 미니언:
  - `Down_Minions/` = Minion_Lane_Core · Minion_Lane_Siege
  - `Dusk_Minions/` = Minion_Lane_Siege
  - `Prime_Helix/` = Prime_Helix
  - `White_Camp_Minion/` = Minion(기본)
  - (+ `SK_Minion_Lane_Super` 메시 1종)
- `Characters/Global/` — 공유(Eyes·FX·MaterialFunctions·MaterialLayers·ParameterCollections·NotForShip)
- **트림**: `Characters/Maps/`(`Lighting_Background.umap`·`Minions.umap` 데모) + 대용량 `FX/`(쓸 것만 선별)
- 애님셋: Melee·Attack·Jog·Death 등 풀 세트(미니언당)

**🔑 선택 확정(사용자, 2026-06-22)** — 아래 3종 + **각각의 연동(의존성) 에셋 전부 포함**. Buff 5색·White_Camp_Minion·기타는 제외:
| 폴더 | 메시 | 역할 | 처리 |
|---|---|---|---|
| **Down_Minions** | Minion_Lane_Core · Minion_Lane_Siege | **스웜 적 + 엘리트/강화 몬스터** | 스웜=VAT 베이크 / 엘리트·강화=같은 소스 메시 변형(스케일↑·MI 틴트·체력↑, 데이터드리븐) |
| **Dusk_Minions** | Minion_Lane_Siege(Dusk 변종) | **스웜 적 + 엘리트/강화 몬스터** | 동일(Dusk=어두운 변종 → 강화/엘리트 톤에 적합) |
| **Prime_Helix** | Prime_Helix | **보스용** | 스켈레탈+애님 유지 + GAS/StateTree(보스=단일액터 헤비스택 허용, 원칙1). `BP_Boss`(Character/Boss) 또는 보스 변종에 배선 |

**처리 원칙**:
- **스웜 잡몹** → BroBot 방식 **VAT 베이크**(애님 1종=Jog/Walk만, 32×32 본텍스처, 최소 머티리얼 `MF_BoneAnimation`, `MI_*_Enemy` 틴트). `BP_EnemyBase.Mesh`(상속 StaticMeshComponent) 배선. 원칙1(적 수백 경량) 정합. 레시피=[[vat-bake-inherited-component-wiring]].
- **엘리트/강화 몬스터** → 동일 VAT 메시 재사용 + 데이터(스케일·체력·MI 색)로 구분(별도 베이크 불요, perf 절약). 능력 부여 시 소수라 GAS 옵션.
- **보스(Prime_Helix)** → VAT 아님. 스켈레탈+풀 애님(Attack/Melee/Death)+피직스 유지. 보스는 이동·스킬 쓰는 단일 액터(U3 스캐폴드 `AFPSRBossBase` 소비처).

**relocate 방법**: 3종 폴더 + **의존성 클로저** = `get_dependencies(recursive)`로 각 메시/AnimBP/머티리얼이 참조하는 **Global 공유분(MaterialFunctions·MaterialLayers·ParameterCollections·Eyes·스켈레톤·애님·텍스처) 전부 포함**해서 `/Game/Assets/Characters/Paragon/{DownMinion,DuskMinion,PrimeHelix}/`(또는 Global은 `Paragon/Shared/`)로 per-asset ≤40 청크 이동. 클로저 밖(Buff·미사용 FX·데모맵)은 미이동/트림. **연동 누락 0** 검증=이동 후 메시 로드 시 머티리얼/스켈레톤 무결.

---

---

## 🅲️ CrosshairFreePack — 크로스헤어 교체 + 동적 분산 + 무기별 (⚠️ 기능 작업 = 코드+UI, 단순 에셋 아님)
**텍스처 20**(`Content/CrosshairFreePack/Textures/`): T_CH001~009(크로스헤어)·T_HM_001~004(히트마커)·T_KI_001~002(킬인디)·T_AH001~004(아머/조준보조).

**사용자 요구 3기능**:
1. **현재 크로스헤어 교체** → 새 T_CH 텍스처로.
2. **동적 분산 크로스헤어** → 쏠수록 분산(bloom) 커지고 **크로스헤어 갭도 맞춰 벌어짐**, 멈추면 회복.
3. **무기별 크로스헤어** → 장착 무기에 따라 다른 크로스헤어.

**✅ 조사 완료 — 바인딩할 기존 인프라(이미 존재, 재사용)**:
- **분산도 소스**: `UFPSRWeaponFireComponent::GetCurrentBloom()` (BlueprintPure, **도 단위**, 지속사격 증가·`BloomRecoveryRate`로 회복). 사격 GA가 트레이스 콘에 이미 사용.
- **무기 스탯**(`FFPSRWeaponStatBlock` @ `Weapon/FPSRWeaponTypes.h:86~`): `SpreadDegrees`(base 1.0)·`BloomPerShot`(0.3)·`MaxBloom`(4.0)·`BloomRecoveryRate`(6.0/s)·`ADSSpreadMultiplier`(0.4). → **총분산(도) = (SpreadDegrees + GetCurrentBloom()) × (IsAiming() ? ADSSpreadMultiplier : 1)**.
- **조준**: `UFPSRWeaponFireComponent::IsAiming()` (BlueprintPure). 둘 다 플레이어 폰 컴포넌트.
- **현재 HUD**: `Content/UI/HUD/WBP_RunHUD`(전용 WBP_Crosshair 없음 → 크로스헤어는 여기 서브요소이거나 미구현, 확인 필요). 히트마커는 **`WBP_HitMarker`(U3a) + `UFPSRPlayerFeedbackComponent::OnHitMarker`/`NotifyHitConfirmed`** 이미 작동.

**구현 스펙**:
1. **신규 C++ 필드 1개** → `UFPSRWeaponDataAsset`(`Weapon|Visual` 카테고리, WeaponMesh1P 옆) `TSoftObjectPtr<UTexture2D> CrosshairTexture`(null=기본). 기존 소프트레프 패턴 그대로. **⚠️ C++ 변경 = 빌드 필요(content-only 아님)**.
2. **WBP_RunHUD 크로스헤어 위젯**:
   - 4방향 라인(또는 스케일 Image), 중앙 갭 = `MinGap + 총분산도 × PxPerDeg`(PxPerDeg 튜너블). 매프레임 `GetCurrentBloom`+스탯 폴링(로컬 코스메틱·위젯1개 → 틱/프로퍼티바인딩 허용; §2-14 피드백컴포넌트 no-tick과 별개 관심사).
   - 텍스처 = 장착무기 `CrosshairTexture`(없으면 기본 T_CH). 무기 교체 갱신(인벤토리 `OnRep_CurrentSlotIndex` 훅 또는 폴링).
   - 데이터 접근: 폰의 `UFPSRWeaponFireComponent` + 인벤토리 현재 `UFPSRWeaponInstance` resolved 스탯. resolved가 BP 미노출이면 BlueprintPure 게터 1개 추가.
3. **히트마커/킬인디 재사용**: T_HM→`WBP_HitMarker` 스타일 교체(OnHitMarker 소비 기존), T_KI→킬마커. T_AH=역할 확인 후 후속.

**처리 순서**:
1. ✅ **relocate 완료**(2026-06-22, 커밋 `1c939c0`): 20텍스처 → `/Game/Assets/UI/Crosshair/`(평탄화), 원본 삭제. (가벼움, 먼저 = 끝)
2. **④b 기능 = 다음 세션(플랜 확정됨, 2026-06-22 사용자 승인)**. ⚠️**선행**: 에디터 닫힌 상태에서 `rm -rf Content/ParagonMinions`(구 팩 3.5GB 트림, 에디터 열린 채는 파일잠금). 그 다음 C++:
   - **사용자 결정**: 크로스헤어 스타일 = **4방향 동적 라인**(상하좌우 4 Image, 중앙 갭=분산). ④b는 이 세션 핸드오프(U-crosshair 유닛).
   - **분산 공식 SSOT(조사확정)**: `FPSRGA_WeaponFire_Hitscan.cpp:73~115` = `SpreadDegrees = Stats->SpreadDegrees + FireComp->GetCurrentBloom(); if (IsAiming() && Stats->bHasADS) SpreadDegrees *= Stats->ADSSpreadMultiplier;` → `VRandCone(deg→rad)`. **위젯에 공식 중복 금지** → C++ 정적 헬퍼로 추출.
   - **C++ 변경(Haiku 위임/Opus 검증, 빌드 필요)**: ⓐ `UFPSRWeaponDataAsset`(헤더 Line 64~ `Weapon|Visual`, WeaponMesh1P 옆)에 `class UTexture2D;` fwd-decl + `UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Weapon|Visual") TSoftObjectPtr<UTexture2D> CrosshairTexture;`(null=기본). ⓑ `UFPSRWeaponFireComponent`(기존 `ComputeShotRecoilDelta`/`ComputeSpinupFireRate` 정적 패턴 따라): `static float ComputeSpreadDegrees(const FFPSRWeaponStatBlock&, float Bloom, bool bAiming)` + `UFUNCTION(BlueprintPure) float GetCurrentSpreadDegrees() const`(=`GetInventory()->GetCurrentInstance()->GetResolvedStats()` + CurrentBloom/bIsAiming → ComputeSpreadDegrees) + `UFUNCTION(BlueprintPure) UTexture2D* GetEquippedCrosshairTexture() const`(=현재 Instance `GetSource()->CrosshairTexture.LoadSynchronous()`, null=nullptr). **Hitscan GA를 ComputeSpreadDegrees 호출로 DRY 리팩터**(무회귀 — 동일 결과). 데이터접근 기존: `Inventory->GetCurrentInstance()`(C++) → `GetResolvedStats()`(서버+클라 계산)·`GetSource()`(BlueprintPure).
   - **WBP_RunHUD 4방향 라인(콘텐츠 MCP)**: 중앙 Canvas에 상하좌우 4 Image(텍스처=`GetEquippedCrosshairTexture()`, 없으면 기본 `T_CH00x`), 각 라인 오프셋 = `MinGap + GetCurrentSpreadDegrees()×PxPerDeg`(클램프, PxPerDeg/MinGap 튜너블). 매프레임 갭 갱신(로컬 코스메틱·위젯1개 틱 허용), 무기교체 시 텍스처 재취득(폰 FireComponent 폴링 또는 인벤토리 `OnRep_CurrentSlotIndex`). 순수 클라(서버 무관).
3. 빌드(에디터 닫고) + 헤드리스 스모크 + Codex 플랜게이트 + **머지 시 Codex 게이트(C++)** + PIE: 쏠때 갭↑·회복·조준시↓·무기별 텍스처 전환·기본 폴백.
4. (후속) T_HM 히트마커 텍스처 → `WBP_HitMarker` 교체, T_KI 킬인디, T_AH 역할확인.

> **유닛 기록 권장**: C++ 포함이라 content 아님 → 로드맵 새 유닛(예: `U-crosshair`, §2-5 사격감각/§2-14 HUD)으로 등록 + 머지 시 Codex 게이트. TaskPrompts_Master 반영.

---

## ⚠️ 재배치 공통 함정 — [[marketplace-asset-import-relocate]]
- **bulk `rename_directory` 금지**(대용량 타임아웃+부분실패+레지스트리 손상). per-asset `rename_asset` ≤40 청크.
- **co-located 리다이렉터 = 손상 아님**(팩 내장 옛이름 별칭). 디스크 파일수·크기로 클린 판별(reals ≥5KB).
- **rename 후 `save_directory(only_if_is_dirty=False)` 강제저장 필수** → 디스크 grep으로 구경로 0 확인.
- 참조자(데모 맵) 선삭제 → 이동이 리다이렉터 무잔존으로 깨끗.
- VAT 베이크: `lod_index=0`, BONE 모드 머티리얼함수=`MF_BoneAnimation`, 최소 머티리얼 신축([[vat-bake-inherited-component-wiring]]).

## 🗂️ git 상태 (커밋 제외/주의)
- **커밋 가능(완료, 사용자 확인 후)**: 보스 이동 = `Content/Boss`(D3) + `Content/Character/Boss`(신규) + `DA_RunSchedule`(M). 🅾️ 참조.
- **미커밋 스푸리어스**: `Content/Maps/L_MainMenu.umap`(로드 dirty)·`Content/Maps/L_Sandbox.umap`(env 커밋 후 재dirty — **커밋 전 의미변경인지 확인**, 스푸리어스면 `git checkout`)·`Config/DefaultEditor.ini.localbak`·`Docs/reviews/`.
- 신규 팩(`Content/{ModularSciFiStation,ParagonMinions}/`)은 **처리(트림·relocate) 후 선별분만 커밋**. 원본 통째 커밋 금지(LFS 7GB). CrosshairFreePack은 relocate 후 커밋.
- 커밋 scope: `content(boss)`(보스이동)·`content(env)`(스테이션)·`content(enemy)`(미니언)·`content(ui)`(크로스헤어). `*_BuiltData`는 gitignore, LFS 포인터 확인.

## 📋 새 세션 복붙용 재개 프롬프트
```
Game.md + PROGRESS.md 먼저 읽고, Docs/AssetWork_NewPacks_Resume.md대로 진행해. 에디터 열려있어 VibeUE 8088 연결됨. git status로 워킹트리 먼저 확인. 팩별 적용방침은 확정됨.
① (가벼움) 보스 이동(Content/Boss→Character/Boss, 검증완료) 커밋.
② ModularSciFiStation(맵 키트): ThirdPersonBP·Level 데모 트림 → per-asset ≤40 청크(bulk rename 금지)로 /Game/Assets/Environment/ModularSciFiStation/ 이동 + rename 후 강제저장.
③ ParagonMinions: Down_Minions·Dusk_Minions(스웜+엘리트/강화)·Prime_Helix(보스) + 의존성 클로저만 선별 relocate(Buff·나머지 제외) → /Game/Assets/Characters/Paragon/. 스웜=VAT 베이크(BroBot 방식), 보스=스켈레탈 유지.
④ CrosshairFreePack: /Game/Assets/UI/Crosshair/ relocate 후 — 크로스헤어 교체 + 동적 분산(UFPSRWeaponFireComponent::GetCurrentBloom 바인딩) + 무기별(FPSRWeaponDataAsset에 CrosshairTexture 신규) 기능. **플랜 우선·C++ 빌드 포함**.
```
