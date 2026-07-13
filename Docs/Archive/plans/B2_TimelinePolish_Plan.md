# B2 폴리시 — 런 타임라인 바: 미션 윈도우 밴드 + 보스 끝 아이콘 (VibeUE 저작 계획)

> **상태: 저작 대기 (에디터+VibeUE 연결 필요).** C++ 데이터는 전부 복제 완료·검증됨(B2 verdict: correct). 이 문서는 `WBP_RunHUD` 그래프 저작 스펙.
> 대상: `Content/UI/HUD/WBP_RunHUD.uasset`(이미 런 타임라인 바 존재 — `a17ae58`, ClockText→바). 신규 C++ **0**.

---

## 1. 데이터 (전부 복제됨, BlueprintPure)
`AFPSRGameState`에서:
- `GetRunSchedule()` → `UFPSRRunScheduleDataAsset*` (**null 가드 필수** — 첫 OnRep 일시 null 가능, 폴백 런 = null).
- 스케줄의 `MissionWindows` (배열, `FFPSRMissionWindow{ MinTime, MaxTime, MissionPool }`) — 밴드 = `[MinTime, MaxTime]` **범위**(서버가 런시작에 뽑는 실제 발동시각 `WindowTriggerTimes`는 비복제 → 클라는 범위만 그림. 이게 의도된 데이터, B2 verdict 확인).
- `GetRunTotalDuration()` → `BossTime`(스케줄) 또는 폴백 300. **바 전체 길이 = 이 값.**
- `GetRunClockSeconds()` → 플레이헤드(현재 진행).
- (보스) `BossTime / GetRunTotalDuration() == 1.0` → **보스 아이콘 = 바 맨 오른쪽 끝 고정.**

## 2. 레이아웃 수식 (분수 0..1)
```
Total = GetRunTotalDuration()              // = BossTime
for each W in MissionWindows:
    x0 = clamp(W.MinTime / Total, 0, 1)    // 밴드 시작 분수
    x1 = clamp(W.MaxTime / Total, 0, 1)    // 밴드 끝 분수
    band: 캔버스 X = x0 * BarPixelW,  너비 = (x1 - x0) * BarPixelW
BossIcon: 바 오른쪽 끝(x = 1.0)에 앵커
Playhead/fill = clamp(GetRunClockSeconds() / Total, 0, 1)   // 기존 바 채움 유지
```

## 3. 위젯 구조 (WBP_RunHUD 내부)
기존 타임라인 바 영역 위에 **오버레이**:
- `CanvasPanel`  `TimelineMarkerCanvas` — 기존 바(ProgressBar/Border)와 **동일 크기·동일 위치**로 겹침(Overlay 슬롯 or 같은 Canvas 상의 동일 Rect). 마커의 좌표계 기준.
- 밴드 = **런타임 생성 `Image`(또는 `Border`)** 를 캔버스 자식으로 AddChild (반투명 색, 예 미션색 40% 알파). **디자인타임에 자식 WBP 임베드 금지**(컨테이너 임베드 compile/save = 크래시, [[vibeue-render-target-gpu-hazard]]) → 단순 Image를 그래프에서 생성.
- `Image` `BossEndIcon` — 캔버스 오른쪽 끝 앵커(보스 아이콘 텍스처). 디자인타임 배치 OK(단일 이미지, 임베드 아님).

## 4. 그래프 로직 (이벤트 구동, Tick 금지)
기존 `OnRunStateUpdated`(= `OnRunStateChanged` 바인딩 BIE, 호스트 직접 + 클라 OnRep 양쪽 발화) 안에서:
```
// 1) 스케줄 변경 시에만 밴드 재구성 (per-tick churn 방지 + 첫 null→resolved 자동 처리 + 재런 안전)
if (GetRunSchedule() != LastSchedule):
    LastSchedule = GetRunSchedule()
    RebuildMarkers()
// 2) 매 호출: 플레이헤드 채움 갱신
SetTimelineFill( clamp(GetRunClockSeconds() / GetRunTotalDuration(), 0, 1) )
```
`RebuildMarkers()`:
```
TimelineMarkerCanvas->ClearChildren()
Sched = GetRunSchedule(); if (!Sched) { BossEndIcon visible(폴백도 보스끝 표시); return }
Total = GetRunTotalDuration(); BarW = TimelineMarkerCanvas 실제 픽셀 너비(또는 고정 디자인폭)
for W in Sched->MissionWindows:
    x0=clamp(W.MinTime/Total,0,1); x1=clamp(W.MaxTime/Total,0,1)
    Img = CreateWidget(Image) ; canvas AddChild
    slot.Position = (x0*BarW, BandY) ; slot.Size = ((x1-x0)*BarW, BandH)  // 캔버스 슬롯 Position/Size만 사용(헤드리스 slot API는 padding/zorder/offset 미반영)
BossEndIcon: visible, 오른쪽 끝
```
> **왜 "스케줄 변경 시에만 재구성"인가**: `OnRunStateChanged`는 디렉터 매틱(클럭/진행) 발화 → 매번 ClearChildren+재생성은 낭비. 스케줄 ref는 런당 1회 set이라 `!= LastSchedule` 비교가 (a)첫 null→자산 resolved 전이 (b)재런(새 GameState) 시점만 정확히 트리거 → 재구성. (B2 verdict의 "rebuild on every OnRunStateChanged + null-guard" 요구를 ref-변경 캐싱으로 충족하며 효율화.)

## 5. VibeUE 저작 순서 (에디터 연결 후)
1. `WBP_RunHUD` 열기 → 기존 타임라인 바 Rect 확인(이름/크기).
2. 동일 Rect에 `CanvasPanel TimelineMarkerCanvas` 추가(Overlay 또는 동일 부모) → compile.
3. `Image BossEndIcon` 추가, 오른쪽 끝 앵커, 보스 텍스처(플레이스홀더 OK).
4. 그래프: `LastSchedule` 변수(ObjectRef) + `RebuildMarkers` 함수 + `OnRunStateUpdated`에 §4 로직 배선(`Conv` 노드·`clamp`·`CreateWidget`/`AddChildToCanvas`·`SetPositionInViewport` 대신 슬롯 setter).
5. compile 0err → **save_packages 비대화**(컨테이너 모달 회피). PIE에서 §6 확인.

## 6. PIE 확인 (저작 후)
- 런 시작 → 바에 미션 윈도우 밴드 N개가 [MinTime,MaxTime] 위치/폭으로, 보스 아이콘이 맨 끝에. 플레이헤드가 좌→우 진행.
- 클라(리슨서버): 호스트와 동일 밴드(첫 프레임 null이어도 1틱 내 resolved 재구성). 호스트 바도 채워짐(직접 broadcast).
- **주의(데이터)**: 현재 authored 값은 **P4-A 임시**(미션 60/120/180s, 보스 300s) → 밴드/끝이 임시값 반영. 프로덕션(300/600/900, 보스1200) 전환 시 자동 반영 [[p4a-temp-test-values]].

## 7. 기록처
저작 완료 → `PROGRESS.md` 핸드오프(B2 폴리시 완료) + 커밋 `content(U1): B2 타임라인 미션밴드+보스아이콘`. C++ 무변경.
