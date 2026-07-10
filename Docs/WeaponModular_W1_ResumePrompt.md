# 무기 모듈러 슬롯 + 진화 + 스코프 (W-U1~) — 재개/착수 프롬프트

> 작성 2026-07-10. **에셋 적용 세션 마무리 핸드오프.** 다음 세션 = W-U1(모듈러 슬롯+선택기) 구현 착수.
> **선행 필독**: `Game.md` + `PROGRESS.md` + **`Docs/WeaponModular_FragmentEvolution_Scope_Plan.md`(플랜 v3, 결정 잠금)** 전체 + 메모리 `weapon-modular-evolution-scope-plan`·`extensibility-first-designer-tooling`·`recoil-crystalrecoil-adapter`·`card-pool-routing`·`headless-gas-content-authoring`.

## 1. 이번 세션(에셋 적용) 완료 상태
- **P1 절차 무기모션 C++ 완료** (커밋됨): 빌드 -NoXGE **Succeeded** · 스모크 **ModuleLoads Success**. `FFPSRProceduralWeaponMotionProfile`(FPSRWeaponDataAsset.h)+힙 sway/bob/kick를 `AFPSRCharacter::UpdateAimDownSights`(단일 seam FPSRCharacter.cpp:1437)에 (1−ADSalpha) 페이드로 합성. idle 0오프셋·무기무관·owner-local·복제0. **PIE 필 검증만 남음**(그립이 되면 확인 가능).
- **라이플 DA 배선**(`DA_Weapon_Rifle`): WeaponMesh1P=`SK_Wep_Mod_A_Body_01`(Synty Military 모듈 바디), WeaponParts1P=**7파츠**(`SM_Wep_Mod_A_` Barrel_01·Handguard_03·Stock_03·Mag_01·Grip_01·Trigger_01·Ironsight_Front_01, 전부 Socket=None·Offset=identity). WeaponAttachSocket=`SOCKET_Weapon`·AimSocket=`SOCKET_Aim`·MuzzleSocket=`SOCKET_Muzzle`. AG14W→Synty 전환(구 AnimBP·ADSAimRotationOffset 클리어).
- **소켓 저작(사용자)**: 팔 `SK_FP_Manny_Simple`에 `SOCKET_Weapon`(ik_hand_gun), Synty 파츠 다수에 `SOCKET_Muzzle`(배럴 6종)·`SOCKET_Aim`(사이트/스코프)·기타 저작. (git에 `PolygonMilitary/…` 다수 M.)

## 2. ⚠️ 핵심 미해결 = 모듈 파츠 조립 실패 (W-U1 선결 과제)
- **"공유원점 identity 드롭인"이 실패**: 7파츠를 Socket=None·identity로 부착하니 **한 덩어리로 뭉침**(힙에서 아무것도 안 보일 만큼). 바운즈는 펼침(배럴 origin +10.5Y·스톡 −23.7Y·핸드가드 +15.5Y)을 시사했으나 **실제 런타임은 클럼프** → Synty 파츠 pivot/조립 규약이 단순 공유원점이 아님(각 파츠 pivot이 자기 부착점, 또는 파츠-위-파츠[사이트→레일]).
- **W-U1이 먼저 풀 것 = 정확한 조립 규약 규명 + 재현**:
  1. **프리셋 역산**: `SK_Wep_Preset_A_Rifle_01`(Synty 완성 조립 단일 메시)의 각 파츠 위치를 뜯어 per-part Offset 도출(가장 유력). 또는
  2. 파츠가 **특정 body 본/소켓**에 붙는지(현 body 본=DustGuard/Switch/Charge/Slide만, 부착소켓 없음 확인됨) 재검토.
  3. **파츠-위-파츠**(사이트가 핸드가드 레일에 붙는 케이스) — 현 `RefreshWeaponPartComponents`(FPSRCharacter.cpp:1148)는 **리시버(WeaponMesh1P)에만 부착**(Codex "파츠-위-파츠 미지원, WeaponPack_Integration.md:51" 지적) → 슬롯 파츠 컴포넌트에 부착하는 확장 필요.
- (우회 참고) 급하면 프리셋 단일 메시로 베이스 가능(SOCKET_Aim/Muzzle 재저작 필요)하나 **사용자는 모듈 진행 선택**. 절대 프리셋으로 도피 말 것 — 조립 규약을 코드/데이터로 제대로 풀어라.

## 3. W-U1 구현 범위 (플랜 §3~6, 결정 잠금)
- **S1** `FGameplayTag Slot`(FFPSRWeaponPartAttachment) + **S2** `TArray<FFPSRWeaponPartRule> PartRules`(무기 DA) + **S2b** 폴리모픽 `UFPSRWeaponPartCondition`(Abstract/EditInlineNew: `Always`/`StatThreshold`/`HasFragment`, 기존 `UFPSRCardEffect` 패턴).
- **C1** 순수 선택기: DA 기본파츠 + PartRules를 `GetResolvedStats()`+`ActiveFragments`로 평가 → 슬롯당 최고티어(tie-break Tier→Priority→RuleIndex→AssetPath) → `TMap<Slot,PartDef>` 상호배타. **+ 위 조립 규약(Offset/부착) 반영**.
- **C2** signature-diff 리빌드(OnRep_Modifiers/ActiveFragments에 풀 destroy/create 직결 금지 → 선택 signature 바뀔 때만·equipped·next-tick 1회). **C7** IsDataValid(동slot/tier 복수=ERROR, 선택가능 사이트 AimSocket 누락=ERROR).
- **격리 계약(§2-A) 필수**: 파츠=순수 읽기전용 소비자 — 카드효과 GrantPart 금지·프래그먼트 파츠변경 금지·복제/세이브에 selected part 금지·크로스헤어/ADS가 파츠를 gameplay소스로 읽기 금지. 5 falsifiable 게이트(§9): ①파츠코드 `Card/` 미include ②WeaponInstance에 SelectedPart 복제/세이브 없음 ③카드 10연속→리빌드≤next-tick1 ④파츠엔진 off여도 사격/프래그먼트/세이브/카드 통과 ⑤사이트 제거시 ADS receiver fallback.
- **후속 슬라이스**(W-U1 후): W-U2 저격 스코프(사이트파츠 스코프모드+오버레이+총숨김+CurrentADSAlpha getter), W-U3 3P 가시성, ADSZoom 축(카드가 줌 수정 확인 시).

## 4. 방식
플랜모드 우선. 구현=Sonnet 위임/검증=Opus 직접. `main`→`phase/w-u1-weapon-modular-slot` 분기. 빌드(-NoXGE)+스모크+PIE, Codex 머지게이트, `--no-ff` 머지. 서버권위·복제0(코스메틱). 헤드리스 DA 저작 시 `FFPSRWeaponPartAttachment.Part`는 EditDefaultsOnly라 **struct kwargs 생성자**(`unreal.FPSRWeaponPartAttachment(part=…)`) 사용(set_editor_property 인스턴스 블록 우회).

## 5. 착수 프롬프트 (새 세션 복붙)
```
Game.md + PROGRESS.md + Docs/WeaponModular_FragmentEvolution_Scope_Plan.md 전체 읽고, Docs/WeaponModular_W1_ResumePrompt.md 읽고, 메모리 weapon-modular-evolution-scope-plan 확인.
[작업] W-U1 무기 모듈러 슬롯+선택기. 플랜모드 우선.
[선결] Synty 모듈 파츠가 "공유원점 identity 드롭인"으로 조립 안 됨(힙에서 뭉침) — 실제 조립 규약부터 규명(프리셋 SK_Wep_Preset_A_Rifle_01 역산으로 per-part Offset 도출 유력 / 본·소켓 / 파츠-위-파츠). RefreshWeaponPartComponents(FPSRCharacter.cpp:1148) 확장. 프리셋 도피 금지.
[구현] 플랜 §3~6: FGameplayTag Slot·폴리모픽 UFPSRWeaponPartCondition·순수 선택기(슬롯 상호배타·티어·tie-break)·signature-diff 리빌드·IsDataValid. 격리계약 §2-A(파츠=읽기전용, 카드/세이브/복제 무변경) + 5 falsifiable 게이트 준수.
[방식] 구현=Sonnet/검증=Opus. 서버권위·복제0. phase/w-u1-weapon-modular-slot 브랜치. 커밋은 검증·승인 후.
```
```
