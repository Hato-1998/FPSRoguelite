# 플랜: 무기 모듈러 슬롯 + 규칙 기반 진화 + 저격 스코프 오버레이

> v3 (2026-07-10, Codex 적대 게이트 R1·R2 수렴 반영). 3서브시스템 조사 + Codex 2R 근거. **자문/설계 — 승인 전 코드 무변경.** 채택 시 `Docs/SSOT/`(CombatWeaponCard §2-3~2-5, PlayerFeel §2-9/2-14) 먼저 갱신 후 구현. 구현=Sonnet / 검증=Opus. 원시 Codex=`Docs/Review/_raw/…weapon-modular-plan-r1/r2.md`. 관련: [[extensibility-first-designer-tooling]]·[[dataasset-conditional-field-visibility]]·[[card-pool-routing]].

## 0. 설계 의도
1. **무기 모듈러 슬롯**: Synty Military 파츠(공유원점 조립)를 슬롯(`FGameplayTag`)으로, 같은 슬롯 상호배타(교체).
2. **파츠 진화(순수 선택기)**: 무기 외형 파츠가 **해결스탯+획득능력을 읽어** 선택됨 — 연사↑→긴배럴·능력→스코프·배율↑→큰스코프. **파츠 시스템은 읽기전용 소비자**(카드/프래그먼트/세이브/복제를 절대 변경 안 함 — §2-A 격리계약).
3. **저격 스코프**: 강FOV줌 + 풀스크린 오버레이(레티클+비네트) + 1P총숨김(비PiP). 스코프모드=사이트 파츠 데이터.
4. **3P 진화 가시성**: 팀원이 진화 무기 봄(2차 슬라이스).
5. (완료) **P1 절차 무기모션**: 빌드·스모크 통과, 무기무관.

> **슬라이스 순서(Codex R1)**: 1P 코어(슬롯+선택기)를 먼저 증명 → 3P·스코프·ADSZoom은 2차. 첫 슬라이스가 실패원인을 흐리지 않게.

## 1. 조사 근거 (핵심, file:line)
- 해결스탯: `GetResolvedStats()`(FPSRWeaponInstance.h:38/44), 축 `EFPSRWeaponStat`{MagSize/FireRate/Recoil/Damage/Spread/ReloadTime}(WeaponTypes.h:173), `RecomputeResolved` switch(FPSRWeaponInstance.cpp:195/243), OnRep_Modifiers(:93)/OnRep_ActiveFragments(:160)서 캐시무효(현재 무효화만, 리빌드 콜백 없음).
- 프래그먼트: `ActiveFragments`(Replicated+OnRep, FPSRWeaponInstance.h:118), 서버권위(ApplyCard→AddFragment, FPSRCardEffect.cpp:267). 카드효과 폴리모픽 `UFPSRCardEffect`(FPSRCardEffect.h:46)+ApplyCard 루프(FPSRCardSubsystem.cpp:213).
- 파츠: `FFPSRWeaponPartAttachment{Mesh,Socket,Offset}`(WeaponDataAsset.h:26), `RefreshWeaponPartComponents`(FPSRCharacter.cpp:1148-1218) WeaponMesh1P에 부착·복제0·**장착시 전량 destroy/create**(1151-1158). aim/muzzle=소켓명 first-in-order 스캔(1192-1217)→**이미 순수장식 아님**(AimSocket/MuzzleSocket이 파츠서 해석, ADS정렬·발사연출 위치 좌우, WeaponDataAsset.h:210). ⚠️3P 파츠 경로 전무(1P OnlyOwnerSee만, FPSRCharacter.cpp:1054/107).
- ADS: bIsAiming 비복제, `CurrentADSAlpha`(FPSRCharacter.cpp:1421, getter없음), reload가 ADS 강제드롭(:1375), `FirstPersonArms->SetVisibility(propagate)` 1P리그 한방숨김. 현 크로스헤어도 raw `IsAiming()`만 봄(FPSRRunHUDWidget.cpp:65)→**동일 결함(reload시 빈화면)**. HUD=WBP_GameHUD 자식위젯(별도 Activatable 금지). LoadSynchronous 다수(FPSRCharacter.cpp:1014/1173, WeaponDataAsset.cpp:186).

## 2. 아키텍처 결정
### 2-A. 격리 계약 (★롤백 저비용 보장 — Codex R2)
파츠 진화는 **읽기전용 소비자**로 가둔다:
- **입력**: `Source`·`ActiveFragments`·resolved stat·equipped slot **까지만**.
- **출력**: "선택된 part signature" + 부착 컴포넌트 **뿐**.
- **금지(계약)**: ①`UFPSRCardEffect`에 `GrantPart`류 추가 금지 ②프래그먼트가 파츠상태 변경 금지(읽기힌트만) ③`WeaponInstance` 복제상태에 selected part 저장 금지 ④SaveGame에 part id 금지(현 세이브 중립 placeholder, RogueliteSaveGame.h:16) ⑤크로스헤어/ADS/FOV가 파츠를 **gameplay 소스로** 읽기 금지("비주얼 사이트 파츠가 ADS 앵커 제공"까지만).
- **폐기 안전판**: 파츠엔진 off(빈 PartRules/컴파일플래그)여도 사격·프래그먼트·세이브·카드선택·ADS(receiver fallback) 정상. → §9 falsifiable 게이트.

### 2-B. 조건 = 폴리모픽 `UFPSRWeaponPartCondition` (✅사용자 결정: extensibility-first)
- 조건 = **폴리모픽 EditInlineNew 서브클래스** `UFPSRWeaponPartCondition`(Abstract): `Always` / `StatThreshold{EFPSRWeaponStat Axis; ECmp Cmp; float Value}` / `HasFragment{FGameplayTag Tag; int32 MinStacks}`. 기존 `UFPSRCardEffect` 폴리모픽 패턴 재사용 — 새 조건=서브클래스 1개·중앙수정0([[extensibility-first-designer-tooling]]).
- (Codex R2는 닫힌 struct/YAGNI를 권장했으나 사용자가 확장성-우선 지침 고수 결정. 저작/검증/로드 표면↑은 수용.) 파츠 시스템은 그래도 §2-A 격리계약 준수(순수 읽기전용 소비자·gameplay 무변경).
- 슬롯=`FGameplayTag`(중앙수정0). 슬롯 상호배타가 aim/muzzle first-in-order를 결정론화.

### 2-C. 저장소 = inline→Profile 승격 (Codex R3)
- 무기DA inline PartRules로 시작, **2무기까지 허용**. 3번째 무기서 동일 룰셋 3+ 복붙 발생 시 별도 `PartEvolutionProfile` DataAsset으로 승격(무기DA는 base 메시·소켓·호환카탈로그만).

## 3. 스키마 변경
| # | 변경 | 위치 | 목적 |
|---|---|---|---|
| S1 | `FGameplayTag Slot` | `FFPSRWeaponPartAttachment`(WeaponDataAsset.h:26) | 슬롯 상호배타 키(빈=구조파츠 다수) |
| S2 | `TArray<FFPSRWeaponPartRule> PartRules` | 무기 DA (후엔 Profile) | 슬롯별·조건부·티어 선택 |
| S2a | `FFPSRWeaponPartRule{FGameplayTag Slot; FFPSRWeaponPartAttachment Part; TObjectPtr<UFPSRWeaponPartCondition> Condition; int32 Tier; int32 Priority}` | 신규 struct | 규칙 1개 |
| S2b | `UFPSRWeaponPartCondition`(Abstract/EditInlineNew) + 서브클래스 `Always`·`StatThreshold`·`HasFragment` (§2-B, 폴리모픽) | 신규 폴리모픽 | 조건 |
| S3 | 스코프모드 디스크립터(bScopeOverlay·ScopeReticle·ScopeVignette·**ScopeFOVOverride**·bHideWeaponWhileScoped) | **사이트 파츠** | 저격 스코프 |
| S4 | `CurrentADSAlpha` getter + `IsADSVisualActive()` | `AFPSRCharacter` | 오버레이/총숨김/크로스헤어 통일 트리거 |
| S5 | 3P 파츠(WeaponParts3P 또는 signature 재사용, sight/barrel/muzzle 슬롯만) | DA+Character | 팀원 가시성(2차) |
| S6(보류) | `EFPSRWeaponStat::ADSZoom` 축 | WeaponTypes.h | **인터림=사이트파츠 ScopeFOVOverride**; 카드가 실제 줌 수정 확인 시 축 추가(파급: enum·RecomputeResolved:195·카드설명:183·FOV:545) |

## 4. 코드 변경 (file:line)
- **C1 순수 선택기 + 슬롯 병합**: `RefreshWeaponPartComponents`(FPSRCharacter.cpp:1148-1218)를 — DA `WeaponParts1P`(기본) + `PartRules`를 `GetResolvedStats()`+ActiveFragments로 평가, **슬롯당 최고티어 만족** 1개(tie-break: **Tier desc→Priority desc→RuleIndex asc→AssetPath**), `TMap<FGameplayTag,PartDef>`로 상호배타. 파츠선택 코드는 `Card/` include 금지(§2-A).
- **C2 signature-diff 리빌드(churn 방지)**: OnRep에 풀 destroy/create 직결 금지. `OnRep_ActiveFragments`/`OnRep_Modifiers`는 "선택 signature 재계산 → **바뀐 경우에만**, equipped 한정, **next-tick 1회 코얼레스**" 리빌드 트리거. (현 OnRep은 캐시무효만: FPSRWeaponInstance.cpp:155/160.)
- **C3 스코프 트리거·총숨김(alpha 통일)**: `IsADSVisualActive()`(=alpha≥임계) 하나로 **오버레이+총숨김+크로스헤어** 전부 게이트(raw bIsAiming 금지, reload 드롭 반영 — 현 크로스헤어 FPSRRunHUDWidget.cpp:65도 이걸로 교정). 총숨김=`FirstPersonArms->SetVisibility`. 해제/재장전/스왑 복원.
- **C4 스코프 오버레이 UI**: WBP_GameHUD 자식 레티클+비네트, alpha·스코프모드 토글([[umg-hidden-widgets-dont-tick]]).
- **C5 async 파츠 로드**: LoadSynchronous(FPSRCharacter.cpp:1173) 제거/봉인 — 장착시 후보 파츠 preload 또는 soft-ref async resolve+fallback(픽업 히치 방지).
- **C6 3P(2차)**: 1P **선택 signature 재사용**(원격서 규칙 재평가 안 함), sight/barrel/muzzle 슬롯만, WeaponMesh3P 부착·SetOwnerNoSee.
- **C7 IsDataValid 강화**: ①동slot/tier/priority 복수만족=**ERROR** ②**선택가능한 각 사이트 룰 결과가 AimSocket 보유하는지 ERROR**(빠지면 조준감 조용히 회귀 — Codex R2 C). 기존 소켓검증(WeaponDataAsset.cpp:186) 옆.

## 5. 콘텐츠 저작
- 소켓(사용자): 팔 `SOCKET_Weapon`(hand_r) · 사이트/스코프별 `SOCKET_Aim` · 배럴/머즐장치별 `SOCKET_Muzzle`. 구조파츠=소켓 불요(공유원점).
- 무기: Synty Military 프리셋/모듈바디+파츠, 슬롯 태그·PartRules 저작.
- 스코프 아트: Synty `SM_Wep_Crosshair_*`=정적메시(UI 아님)→LPAMG 레티클텍스처/레티클머티리얼+비네트.
- **진화 전이 연출**(Codex Q4): owner 짧은 snap/scale/fade, remote 1회 flash. 무거운 Niagara 금지.

## 6. 단계
- **W-U1 슬롯+선택기(1P 코어)**: S1+S2+S2a/b + C1 + C2(signature-diff) + C7. 검증=연사 카드로 스탯↑→긴 배럴 자동 교체·능력→스코프·슬롯 상호배타·조준선 이동.
- **W-U2 저격 스코프**: S3(ScopeFOVOverride 인터림)+S4+C3+C4. 검증=저격 ADS→강줌+오버레이+총숨김, 재장전/해제 드롭·크로스헤어 통일.
- **W-U3 3P 진화 가시성**: S5+C6(signature 재사용). 검증=원격 클라서 진화 파츠 보임.
- **(보류) ADSZoom 축(S6)**: 카드가 실제 줌 수정 필요 확인 시. 그 전 ScopeFOVOverride로.
- **콘텐츠 트랙(병행·사용자)**: 소켓·무기/파츠 슬롯·규칙·스코프 아트·전이 연출.
- 병행: C5 async 로드는 W-U1에 포함.

## 7. 결정 확정 (사용자)
1. ✅ **조건 모델 = 폴리모픽** `UFPSRWeaponPartCondition`(§2-B). extensibility-first 지침 고수 — Codex의 닫힌-struct YAGNI 권고 대신 사용자가 확장성-우선 선택(저작/검증 표면↑ 수용).
2. ✅ **슬라이스 = 1P 코어 먼저**(W-U1) → 3P(W-U3)·ADSZoom은 2차. 첫 검증 표면 축소·손맛/성능 먼저 증명(Codex 권장 채택). 3P·ADSZoom은 미루는 것일 뿐 설계에 포함.
3. ✅ (기결) 스코프모드=사이트 파츠 · 스코프=오버레이(비PiP) · zoom 인터림=ScopeFOVOverride(축은 카드가 실제 줌 수정 확인 시).

## 8. 리스크
- 3P 파츠 신설(2차) — WeaponMesh3P 스켈 소켓·signature 재사용.
- signature-diff 없이 OnRep 직결 시 픽업 churn(전 destroy/create) — C2 필수.
- 파츠-위-파츠 미지원(리시버에만, WeaponPack_Integration.md:51) — 레일 위 스코프 후속.
- LoadSynchronous 히치 — C5.
- **조준감 회귀(조용한)**: 사이트 파츠가 AimSocket 빠뜨리거나 배럴과 소켓명 충돌 → ADS 앵커 이동. C7 IsDataValid ERROR로 차단.
- 이산 교체가 "순간 바뀜"으로 튐 — 전이 연출(§5).

## 9. 검증 (falsifiable 게이트 — Codex)
빌드(-NoXGE)+스모크+PIE. **격리 게이트**: ①파츠선택 코드 `Card/` 미include ②WeaponInstance에 SelectedPart*/PartSignature/Granted 복제·세이브 필드 없음 ③카드 10연속 후 리빌드 ≤ next-tick 1회(equipped) ④파츠엔진 off여도 사격/프래그먼트/세이브/카드 통과 ⑤사이트 제거/교체 시 ADS receiver fallback·트레이스/데미지 불변. **필/성능**: 2-client PIE 원격 10m/25m 진화 식별, 4인 10연속 파츠변화 히치0, reload시 스코프/크로스헤어/총숨김 빈화면0.

## 10. P1 관계
P1(절차 무기모션) 완료·무기무관·독립. 무기가 손에 잡히면 얹힘. P1 커밋 별도 경계.
