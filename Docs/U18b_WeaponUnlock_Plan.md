# U18b — 무기 해금 + 추첨 라우팅 재편 (구현 계획, 승인됨 2026-06-20)

> 설계 SSOT: `Docs/SSOT/CombatWeaponCard.md` §2-3-3/4/5. U18a 카드 v2 토대 위 신설.
> 브랜치 `main → phase/u18b-weapon-unlock`, 2서브유닛 2커밋 각자 검증 → 1회 `--no-ff` 머지.
> 구현=Haiku 위임 / 효과·RPC·픽카운터·마일스톤·보안배선=Opus 직접 / 검증=Opus 직접.

## 사용자 확정 결정 (2026-06-20)
- **D4-a**: `EFPSROfferType::MissionReward` + 머신러리 **완전 제거**(MissionRewardPicksPending / GrantMissionReward / DrawWeaponModifierOffer / PendingMissionRewardCards / Add·ConsumeMissionRewardPick / 관련 분기). 미션 클리어 = WeaponUnlock 오퍼로 전환. per-mission `RewardCard` 오버라이드는 WeaponUnlock 단일 지정 해금카드로 의미 보존.
- **D4-b 기능해금 콘텐츠**: U18b에서는 **전무기 "탄도 +1"(UFPSRFragment_MultiShot, MaxStacks=3)만**. 차징 후 연사 / LMG OnMiss 탄창리필 / 샷건·바주카 OnKill 재장전 = **U18c**(OnMiss·OnKill 훅이 U18c라 호출 지점 없음).

## 핵심 설계 결정 (제1원리)
- **D1 WeaponUnlock = 신규 오퍼타입 + 신규 픽 카운터**(MissionReward 오버로드 금지). reroll 차단·full슬롯 특수처리·트리거 2종(미션+마일스톤)이 별 수명 → 분리. 서버권위 인덱스-선택 불변.
- **D2 통합 WeaponUnlock 오퍼 = 새무기 후보(WeaponUnlockCards, 미보유+빈슬롯) + 기능해금 후보(보유무기 UnlockableFeatures, 스택게이트) 혼합.** 미션 클리어 & 레벨 20/30/40 둘 다 이 오퍼. 3슬롯 full = 새무기 후보만 비제시(기능해금 계속).
- **D3 Fragment 스택게이트를 DrawCards flatten 루프로 이식 + CardHasBehaviorEffect 가드 해제.** 공용 헬퍼 `GetCardBehaviorFragment` 추출. `Inv` 함수스코프 호이스트.
- **D5 마일스톤 = 데이터 구동 config** `AFPSRGameState::WeaponUnlockMilestones {20,30,40}`. AddSharedXP 루프 (PrevLevel, NewLevel] 교차 시 전원 unlock 픽. 레벨업 픽 + unlock 픽 = 같은 프리즈 누적, 우선순위 OpeningSeed>WeaponUnlock>LevelUp 순차 2모달(데드락 없음).

## 코드 맵 (확정 사실)
- `EFPSROfferType`={OpeningSeed,LevelUp,MissionReward} `FPSRCardTypes.h:23-29` — WeaponUnlock 신설. `ECardGroup::WeaponUnlock` 예약됨(활용).
- `ApplyCard`(`FPSRCardSubsystem.cpp:207-302`) 효과타입-무지 → GrantWeapon 추가에 본문 무수정. 오퍼타입 게이트/소비 분기만 손댐.
- `AddFragment`(`FPSRWeaponInstance.cpp:62-72`) MaxStacks self-gate(no-op). `WeaponBehavior::CanApply`는 스택 미확인 → 스택게이트는 추첨에 둬야 함.
- PlayerState 픽카운터: `MissionRewardPicksPending`(=OnRep_CardPicksPending 공유, `FPSRPlayerState.h:177`)이 WeaponUnlock 템플릿.
- `DrawCards`(`FPSRCardSubsystem.cpp:67`) `Inv` 블록스코프(L100-103) → 호이스트. flatten 가드 L124-128, family dedup L191-201.
- `GatherCandidatePool`(`:470-539`) ActivePool->Cards + 보유무기 WeaponCards(L513), Group==Weapon이면 TargetWeapon 태깅.
- 미션 성공 `FPSRRunDirectorSubsystem.cpp:314` `PC->GrantMissionReward(RewardCard)`.

## ■ U18b1 — 무기 해금 코어 (커밋 1)
1. `Card/FPSRCardTypes.h`: `EFPSROfferType::WeaponUnlock`(reroll 차단 주석).
2. `Weapon/FPSRWeaponInventoryComponent.h/.cpp`: `bool HasFreeSlot() const`(null 슬롯 존재 — 3캡 캡슐화).
3. `Card/FPSRCardEffect.h/.cpp`: `UCardEffect_GrantWeapon`(4번째): `TObjectPtr<UFPSRWeaponDataAsset> WeaponToGrant`. `RequiresWeapon()=false`. `CanApply=Inventory && HasFreeSlot() && !GetOwnedWeapons().Contains(WeaponToGrant)`. `Apply→Inventory->AddWeapon`. `GetDescription="Unlock: <DisplayName>"`. `ValidateEffect`=null체크.
4. `Card/FPSRCardPoolDataAsset.h`: `TArray<TObjectPtr<UFPSRCardDataAsset>> WeaponUnlockCards`(EditDefaultsOnly).
5. `Core/FPSRPlayerState.h/.cpp`: `WeaponUnlockPicksPending`(OnRep_CardPicksPending 공유)+Add/Consume/Get+DOREPLIFETIME+ResetRunState 클리어(MissionReward 1:1).
6. `Card/FPSRCardSubsystem.h/.cpp`: `DrawWeaponUnlockOffer(ForPlayer,Count=3)` 새무기 후보(WeaponUnlockCards, 미보유+HasFreeSlot). ApplyCard WeaponUnlock 게이트+소비 분기.
7. `Core/FPSRPlayerController.h/.cpp`: `GrantWeaponUnlock(Override=nullptr)`. PresentNextOfferIfNeeded WeaponUnlock 분기(OpeningSeed 다음). RequestCardOffer WeaponUnlock case. 빈오퍼 릴리스+HasPendingSelection+ServerRerollOffer reroll차단+ServerSelectCard bookkeeping.
8. `Core/FPSRGameState.h/.cpp`: `WeaponUnlockMilestones {20,30,40}` config. AddSharedXP 마일스톤 훅.
9. `Run/FPSRRunDirectorSubsystem.cpp:314`: GrantMissionReward→GrantWeaponUnlock.
+ **MissionReward 완전 제거**(D4-a): 위 신설과 동시 또는 b2 정리. (b1에서 미션→WeaponUnlock 전환 시 MissionReward 경로 dead → 같이 제거.)

검증: 풀빌드(-WaitMutex) Succeeded + 헤드리스 스모크 Result={Success}.

## ■ U18b2 — 라우팅 재편 + 기능 해금 (커밋 2)
1. `FPSRCardSubsystem.cpp`: `GetCardBehaviorFragment` 추출. DrawCards: Inv 호이스트 + 가드 해제 → 스택게이트(behavior 카드는 `Inst->GetFragmentStackCount(Frag) < max(MaxStacks,1)`일 때만).
2. `FPSRCardSubsystem.cpp`: DrawWeaponModifierOffer→`GatherFeatureUnlockCandidates`(UnlockableFeatures 읽음). DrawWeaponUnlockOffer가 새무기+기능해금 혼합 N장.
3. `Weapon/FPSRWeaponDataAsset.h`: `UnlockableFeatures[]` 신설. AvailableModifiers 제거(콘텐츠 이동 후). WeaponCards 역할 주석.

콘텐츠(에디터 종료 git + resave `Scripts/u18b_*.py`):
- 4 DA_CardModifiers_*: AvailableModifiers→WeaponCards 이동(Group=Weapon 확인).
- 신규 DA_CardUnlock_<Weapon>(GrantWeapon)→WeaponUnlockCards[].
- 신규 탄도+1 기능해금 카드(MultiShot, MaxStacks=3) 전무기 UnlockableFeatures[].

## 무회귀/머지 게이트
- 중복 제시 0(Fragment=WeaponCards 단일출처, 미션 Fragment 미부여).
- MaxStacks 유지(레벨업 스택게이트).
- CanApply 트랜잭션(GrantWeapon full/이미보유→픽 미소모).
- 서버권위(WeaponToGrant authored, TargetWeapon 서버세팅).
- 2프리즈 데드락 없음(누적+우선순위 순차).
- 디버그 커맨드 stale 점검(FPSR.GrantMissionRewardPick 제거, FPSR.ApplyCard).

## Codex 플랜 게이트 교정 반영 (2026-06-20, 통과·강화)
- **C1 RewardCard 오버라이드 제거**: per-mission override 모호(미션 DA `RewardCard`=임의 카드라 WeaponUnlock 아닌 카드 오제시 위험). → `GrantWeaponUnlock()` **오버라이드 없음**. 미션=표준 WeaponUnlock 풀 추첨. `FPSRMissionDataAsset.RewardCard`(`.h:40`) **deprecate/제거**(미션 DA resave로 클리어).
- **C2 MissionReward 제거 명시목록**: GrantMissionReward·PendingMissionRewardCards·RequestCardOffer/ServerRerollOffer/ServerAbandonOffer/ServerSelectCard의 MissionReward 분기·PS 카운터(AddMissionRewardPick/Consume/Get/필드/DOREPLIFETIME/ResetRunState)·ApplyCard 게이트(`FPSRCardSubsystem.cpp:239,296`)·**HUD 표시 `FPSRCharacter.cpp:133-137`(RewardPicks)**·헤더 주석 `FPSRCardSubsystem.h:39`·debug `FPSR.GrantMissionRewardPick`(`FPSRPlayerController.cpp` + Saved/Config Input.ini 히스토리는 무시 가능). 전부 제거.
- **C3 WeaponBehavior::CanApply에 MaxStacks 체크 추가**: 추첨 게이트만으론 draw-후 상태변화/중복카드 시 픽 소모·효과0. → `CanApply = Fragment && Inst && GetFragmentStackCount(Fragment) < max(MaxStacks,1)`. 추첨 스택게이트 + CanApply 이중방어.
- **C4 DrawWeaponUnlockOffer dedup**: 새무기 후보 `WeaponToGrant` 기준 중복 제거(같은 무기 2회 제시 금지).
- **C5 빈오퍼 릴리스 케이스 고정(검증항목)**: WeaponUnlock ① 빈 후보 ② full슬롯+전기능max ③ — 각 케이스 `WeaponUnlockPicksPending` 정확 소비·프리즈 해제(하드락 없음).
- **C6 탄도 중복출처 정리**: 4 DA_CardModifiers_* 내용 확인 → MultiShot이 레벨업(WeaponCards)에 있으면, 기능해금 탄도카드는 **같은 Fragment 에셋 래핑**(스택카운트 공유 → MaxStacks=3 캡이 양 경로 합산, 무한스택 불가)하거나 한쪽만 둠. 구현 시 4 카드 inspect 후 확정.
- **C7 콘텐츠 이동 명시 게이트**: U18a식 헤드리스 migcheck 재실행 — 콘텐츠 이동(AvailableModifiers→WeaponCards, UnlockableFeatures[], WeaponUnlockCards[]) 후 7캐릭터·6무기stat·4Fragment의 로드수/Group/Effects/OfferRarities/stack-gate 재검사(텍스트 grep 불충분).
- **C8 debug FPSR.ApplyCard**: OpeningSeed로 pending 우회 → WeaponUnlock 디버그 추가 시 해금검증 흐리지 않게 점검.

## 시퀀싱
1. (승인 후) Codex 플랜↔목표 게이트(5분 워치독). ✅ 통과(위 C1-C8 반영).
2. 구현 Haiku / 효과·RPC·픽카운터·마일스톤=Opus 직접.
3. 검증 Opus: b1·b2 빌드+스모크 → 콘텐츠 → 사용자 PIE(①새무기 미션/레벨20·30·40, full시 미제시 ②탄도 기능해금 ③Fragment 레벨업 등장 ④U18a 무회귀) → codex-review.ps1 -Base main.
4. PROGRESS/TaskPrompts 갱신 + 콘텐츠 동반커밋 질문 + --no-ff 머지. 다음=U18c.
