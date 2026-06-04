# P4-A 사용자 콘텐츠 가이드 — 라운드 스케줄 / 미션 / 런 흐름

> C++ 베이스는 완료(빌드+스모크+Codex 통과). 아래 콘텐츠(DataAsset/BP)를 에디터에서 만들어 연결하면 PIE에서 런 루프가 동작한다. **에셋 경로는 C++에 하드코딩하지 않으므로 전부 BP/DA로 연결**한다.

## 0. 전제
- 빌드된 에디터로 PIE. 기존 `BP_FPSRGameMode`(부모 `FPSRGameMode`, `/Game/Core/`)가 이미 있어야 함(P1 셋업).
- 권장 콘텐츠 폴더: `Content/Run/`(스케줄·미션 DA), `Content/Run/Missions/`(미션 BP).

## 1. 미션 BP — `BP_Mission_HoldZone`
- **부모 클래스**: `FPSRMission_HoldZone` (C++)
- 디테일에서 조정(에디터 노출): `ZoneRadius`(기본 400), `RequiredHoldSeconds`(기본 30)
- (선택) 루트에 시각용 메시/데칼을 붙여 존을 보이게 해도 됨 — 판정은 서버 거리체크라 메시 불필요. PIE에선 `ENABLE_DRAW_DEBUG` 초록 실린더로 존이 보임(호스트).
- ※ MissionClass에 C++ 클래스(`FPSRMission_HoldZone`)를 직접 지정해도 동작하지만(기본값 400/30), 수치 튜닝을 위해 BP 권장.

## 2. 미션 DA — `DA_Mission_HoldZone` (`UFPSRMissionDataAsset`)
- **에셋 생성**: Miscellaneous > Data Asset > `FPSRMissionDataAsset`
- 필드:
  - `DisplayName` / `Description` / `ObjectiveText`: 표시용(예 "구역 사수", "30초간 구역을 지켜라") — UI는 P4-D
  - `MissionClass` = `BP_Mission_HoldZone`
  - `TimeLimit`: 0 = 무제한, 또는 예 60(초) — 초과 시 실패 처리
  - `RewardCard`: 무기 모디파이어 보상 카드(weapon-scope). **P4-A는 클리어 시 카운트만 적립**(실제 보상 선택·적용은 P4-B). 테스트로 적립 동작만 보려면 비워둬도 됨(null이면 카드 참조 없이 카운트만 증가)

## 3. 라운드 스케줄 DA — `DA_RunSchedule` (`UFPSRRunScheduleDataAsset`)
- **에셋 생성**: Data Asset > `FPSRRunScheduleDataAsset`
- `Rounds` 배열 (⚠️ **테스트용 압축값** — 프로덕션은 5/10/15분):

| # | Duration(초) | TargetAliveCount | Mission | bBossRound |
|---|---|---|---|---|
| 0 | 120 | 50  | DA_Mission_HoldZone | ☐ |
| 1 | 120 | 80  | DA_Mission_HoldZone | ☐ |
| 2 | 60  | 120 | DA_Mission_HoldZone | ☐ |
| 3 | 0   | 0   | (none)              | ☑ (보스 게이트) |

- Mission은 라운드 내 **랜덤 시각(Duration의 10~80%)에 1회** 자동 출현. 라운드마다 같은 DA를 써도 되고 라운드별로 다른 미션 DA를 둬도 됨.
- Rounds를 비워두면 C++ 폴백 스케줄(120/120/60→보스, 미션 없음)로 동작.

## 4. GameMode BP 연결
- `BP_FPSRGameMode` 디테일 > `FPSR|Run` > **`RunSchedule` = `DA_RunSchedule`** 할당.
- (CardPool은 기존 P3 연결 유지)

## 5. PIE 검증 시나리오
런 시작 시 **자동**으로:
1. **오프닝 시드 2장** 자동 출현(클라 UI 준비되면 서버가 발급) → 2회 선택
2. **Combat 스폰 자동 시작**(라운드 TargetAliveCount만큼) — 더 이상 `FPSR.SpawnEnemies` 불필요
3. 라운드 내 랜덤 시각에 **미션(초록 존) 출현** → 존 안에서 30초 버티면 클리어(보상 적립 로그)
4. 라운드 시간 경과 → **Breather 진입**(스폰 정지 + 잔여 적 즉시 정리) + 레벨업 카드 선택
5. **전원 픽 소비 완료 시 자동으로 다음 라운드** 재개
6. 마지막 라운드 → **보스 게이트 로그**(보스 실물은 P6 스텁)

### 테스트 편의 콘솔(빠른 반복, shipping 제외)
- `FPSR.RunTimeScale 10` — 런 경과 10배속(5분 런을 30초에)
- `FPSR.SkipToBoss` — 보스 라운드로 점프
- `FPSR.NextRound` — 현재 라운드 강제 종료 → Breather
- `FPSR.MissionTrigger` / `FPSR.MissionClear` — 미션 즉시 발동 / 강제 클리어
- `FPSR.KillAllEnemies` — 활성 적 전부 정리
- `FPSR.RunDebug 1` — 화면에 R#/Phase/경과·잔여/미션/적수/배속 오버레이
- (기존 `FPSR.AddXP`, 1킬=1레벨 테스트값이라 적 1마리=카드 1픽)

## 6. ⚠️ 임시 테스트값 (프로덕션 전환 시 원복)
- 라운드 2/2/1분(프로덕션 5/10/15분) · 적 `XPReward=100` · 레벨당 요구 100(`XPPerLevel=0`, 1킬=1레벨)
- 전환 시점에 별도 보고 예정(메모리 `p4a-temp-test-values`).
