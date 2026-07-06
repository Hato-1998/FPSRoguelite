# 컨설트: 무기·미션 데이터 편집 툴 P0 — FPSRogueliteEditor 모듈 + 데이터 검증·카탈로그 시임 (2026-07-07)

> 백엔드(Claude, 시스템/유지보수 렌즈) × 클라이언트(Codex, 기획자-툴링 렌즈) 5라운드 수렴 토론. 원시 응답 = `Docs/Review/_raw/20260707-*-data-tooling-p0.md`.

## 범위 / 읽은 컨텍스트
- 스카우트 워크플로 조사(무기/미션 데이터 모델·기존 툴링 인프라·ToolingBacklog·SSOT 설계의도, 4 리더).
- 소스 대조: `FPSRWeaponDataAsset`/`FPSRWeaponTypes`·`FPSRCardDataAsset`/`Effect`/`Pool`·`FPSRRunScheduleDataAsset`/`FPSRMissionDataAsset`·`FPSRGameMode.h`(앵커)·`FPSRGameFlowSettings.h`·`FPSRCardSubsystem.cpp`(라우팅)·`FPSRoguelite.uproject`/`FPSRogueliteEditor.Target.cs`/`FPSRoguelite.Build.cs`(모듈 부재 확인).
- 엔진 대조(UE5.7): DataValidation 플러그인 `UEditorValidatorBase`(`CanValidateAsset_Implementation`/`ValidateLoadedAsset_Implementation`/`FDataValidationContext::GetValidationUsecase`).
- 사용자 확정: 툴 형태 = **C++ 에디터 모듈 신설**, 착수 = **검증 시임(P0) 먼저**.

## 🔧 백엔드 렌즈 핵심 입장 (Claude)
에디터 전용 검증 코드는 Runtime 모듈에 두면 부채 → 클린 Editor 모듈. 교차규칙은 앵커-에셋 `UEditorValidatorBase`에 두어 **인에디터 Validate Data + 헤드리스 CI 단일소스**. 저장 시엔 싼 검증, 명시/커맨드릿에서만 교차. 캐노니컬-단일 앵커의 맵로드/설정중복/드리프트를 피해 **앵커 타입 전수 열거**로 발견.

## 🎮 클라이언트 렌즈 핵심 입장 (Codex)
P0는 예쁜 편집기가 아니라 "왜 이 카드가 안 나오지 / 왜 보상 안 주지"를 잡는 **파손 게이트**. Slate/리포트창/자동수정/CSV/시뮬은 P0 과설계. 미션이 무기보다 기획 체감 고통 크나(DA/BP CDO/월드배치 분산), 그건 P1+. CI non-zero는 내장 경로가 보장 안 하니 **커스텀 커맨드릿이 소유**. "현재 코드 우연"을 불변식으로 굳히지 말 것(라우팅).

## 토론 로그 요약
- **R1**: 모듈-지금 vs WITH_EDITOR-런타임. Codex=지금 클린 모듈(단 Slate 없이 검증기+헤드리스만). CI exit-code 자동매핑 안 됨 캐치. 앵커기반·순서(미션 체감고통>무기).
- **R2**: P0에 read-only 리포트창? Codex=넣지 마라(stale/필터/탭수명 부채) → 메뉴명령만. 미션튜닝 DA통합=별도 RunFlow액션. CI=커스텀 커맨드릿.
- **R3**: 교차규칙 위치=앵커 검증기(H1, 인에디터+CI 단일소스). 저장=싼검증/명시=교차(usecase 게이트). 고아=경고전용·타입제한·1회스캔·Dev/Test제외. 앵커=명시 설정.
- **R4**: 앵커 3종(CardPool/RunSchedule/LoadoutPool)·무기 sanity 보강 적정·월드의존(SpawnPointTag)은 P0제외. **캐치: 라우팅 스펙 코드↔SSOT 드리프트로 "누수 검증" 확정 불가.** RunSchedule/EnemyRoster 내부 체크 추가.
- **R5**: 앵커=타입 전수열거(캐노니컬-단일 각하, 발견용 AssetRegistry+로드 후 그래프 순회, **앵커 0개=에러**). 라우팅누수=P0제외→SSOT액션. CardId유일=서로 다른 에셋 동일 CardId만 에러. orphan=경고(return0). **수렴.**

## ✅ 합의 권고 — 최종 P0 스펙
1. **`FPSRogueliteEditor` 신규 Editor 모듈** (Slate 위젯/편집 UI 0). `.uproject` 모듈추가 + Target ExtraModuleNames + DataValidation 플러그인 활성.
2. **앵커 교차검증기 3종** (`UEditorValidatorBase`, usecase 게이트: Save=싼검증만 / Manual·Commandlet=교차순회):
   - `CardPoolValidator`: CardId 전역유일(**서로 다른 에셋이 같은 Id일 때만 에러**)·rarity coverage·CardFamily 중복충돌·오퍼 성립성(family de-dupe 후 후보수 ≥ 오퍼크기·전 rarity weight≠0)·surface별 비어있지 않음.
   - `RunScheduleValidator`(얕게): 윈도우 Min≤Max·풀 null아님/유효 미션참조·BossTime>0·BossDef/Roster set·AliveCountByLevel 오름차순/무중복·Max/Interval>0·BossTime↔윈도우 충돌(경고). **BP CDO 깊은 순회 제외.**
   - `LoadoutPoolValidator`: SelectableWeapons non-empty·null없음·중복없음·유효 무기DA.
3. **per-asset IsDataValid 보강** (저장 시 싼검증): 무기 DA에 "죽는 값" sanity(Damage/FireRate/Range>0, 비근접 MagSize/ReloadTime>0, 샷건 Pellet>0, 발사체 Speed/Lifetime>0, 근접 Radius/Delay>0). EnemyRoster에 rules/EnemyClass null아님·전 weight≠0. 미션DA SpawnPointTag `meta=(Categories=...)` 오타 guard.
4. **얇은 서비스** `FFPSRAnchoredValidationService`: AssetRegistry로 {CardPool·RunSchedule·LoadoutPool} 타입 전수 발견(Dev/Test 루트 제외) → 로드 → typed UPROPERTY 그래프로 reachable 잎(무기/카드/미션/프래그먼트) 캐시. 앵커 검증기·고아패스·커맨드릿 공용.
5. **고아 경고 패스**: 잎 타입 전수 스캔 1회 vs reachable diff → **경고(exit 0)**. 자동수정/삭제/CSV 없음. 향후 `ExcludedRoots` 탈출구.
6. **`UFPSRValidateAnchoredDataCommandlet`**: 앵커 로드→ValidateAssetsWithSettings→**NumInvalid>0 return 1**(orphan은 return 0), **앵커 0개=에러**(false-green 방지). 규칙로직 0.
7. **`Scripts/validate-data.ps1`**: 커맨드릿 호출 + `$LASTEXITCODE` 전달만.
8. **`Tools > FPSR > Validate Anchored Data`** 메뉴명령(ToolMenus): 앵커 검증 실행 + Message Log 포커스. **창 아님.**
- 가시표면 = 내장 Validate Data / Message Log / Content Browser + 위 메뉴명령 + CI 실패코드.

## ⚖️ 미해결 쟁점
- 없음(설계 수렴). trade-off는 전부 "P0 범위 밖 후속"으로 정리됨.

## 🙋 사용자 결정 필요
1. **라우팅 스펙 정합** (선행): 행동프래그먼트 / feature-unlock(`UnlockableFeatures`) / 무기-unlock(`WeaponUnlockCards`) / 레벨업(`Cards`) surface를 **코드(`FPSRCardSubsystem.cpp:131`)와 SSOT(`CombatWeaponCard.md §2-3-4`)·메모리 [[card-pool-routing]] 간 확정**. → 확정 후에만 "라우팅 누수" 검증 추가. (지금은 코드가 "행동프래그먼트=레벨업 draw 합류"라 메모리와 상충.)
2. **미션 튜닝 DA 통합** 여부: BP CDO의 ZoneRadius/HoldSeconds 등을 DA-소유로 올려 단일편집+`ServerActivate(Data)` 주입할지. RunFlow SSOT 설계 결정. (P0=검증만.)
3. **착수 순서 P1+**: Codex 권고 = 미션(스케줄 타임라인 read-only 프리뷰) > 무기 밸런스 매트릭스. (무기 스키마는 Synty 교체에도 생존하나, 무기 저작 UI는 교체 후.)

## 📌 액션 아이템
- **[구현] P0 시임** = 위 ✅ 스펙. `phase/editor-tooling-seam` 브랜치. 완료 시 `ToolingBacklog.md` "Batch DataAsset Validator" 행을 이 구현으로 ✅ + 범위 명시(중복방지).
- **[SSOT] `CombatWeaponCard.md §2-3-4`** 카드 surface 라우팅 정합(선행 결정 #1) → 이후 라우팅 누수 검증 추가.
- **[SSOT] `RunFlow.md §2-8`** 미션 튜닝 DA-소유 필드 결정(결정 #2).
- **[백로그] `ToolingBacklog.md`** P1 후보 = Run Schedule Timeline(read-only 프리뷰 먼저) → Mission Scaffolder → Card/Weapon Balance Matrix(이 검증 서비스 재사용).
