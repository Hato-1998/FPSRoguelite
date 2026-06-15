# V3 — 기본 크로스헤어 HUD 유저 콘텐츠 작업 가이드

> **유닛**: V3 (기본 크로스헤어 HUD / 정적 레티클) — `Docs/TaskPrompts_Master.md` §C V3
> **브랜치**: `phase/p4d-crosshair` (main 분기, §6-7)
> **성격**: **순수 콘텐츠(WBP)**. C++ 변경 없음.
> **읽은 SSOT**: PlayerFeel §2-14(게임필/HUD), CombatWeaponCard §2-4-2(확산/ADS), 메모리 vibeue-mcp-capabilities.

## 핵심 확정 (조사 결과)
- **C++ 변경 불필요** — `IsAiming()`이 이미 `BlueprintPure`(`Source/FPSRoguelite/Public/Weapon/FPSRWeaponFireComponent.h:38-39`). 위젯에서 `Get Component By Class`로 FireComponent를 얻어 바로 호출 가능.
- HUD 인프라: `WBP_GameHUD`(Game 레이어, 중앙)가 PlayerController `EnsurePrimaryLayout`에서 `UI.Layer.Game`에 push. `WBP_HitMarker`도 같은 중앙. 크로스헤어는 그 캔버스에 중앙 자식으로 추가, **Z는 히트마커보다 아래**.
- FireComponent는 `AFPSRCharacter`의 컴포넌트(`WeaponFire`). 위젯에서 `Owning Player Pawn → Get Component By Class(FPSRWeaponFireComponent)`로 접근.

---

## STEP 0 — 브랜치 (작업 격리) — ✅ 완료됨

병렬 세션이 없으므로 워크트리 없이 **단일 작업 폴더에서 브랜치 체크아웃**으로 진행한다. 이미 셋업 완료:

```powershell
# (이미 적용됨) main 기준 V3 브랜치 생성 + 체크아웃
git checkout -b phase/p4d-crosshair main   # 또는 이미 있으면: git checkout phase/p4d-crosshair
```

현재 작업 폴더 `E:/Git_Project/FPSRoguelite`가 `phase/p4d-crosshair` 브랜치다. **에디터를 `FPSRoguelite.uproject`로 그대로 열어** 작업하면 저장되는 `.uasset`이 이 브랜치에 들어간다.
> ⚠️ V3는 콘텐츠 전용(C++ 리빌드 없음)이라 브랜치 체크아웃이 가벼움. 나중에 병렬 세션이 생기면 그때 워크트리로 분리.

---

## STEP 1 — `WBP_BasicCrosshair` 위젯 신설

`Content/UI/HUD/` 우클릭 → **User Interface → Widget Blueprint** → 부모 `UserWidget` → 이름 `WBP_BasicCrosshair`.
> 별도 위젯으로 두는 이유: U12(동적 스프레드)가 이 위젯만 확장하면 되고 `WBP_GameHUD` 레이아웃을 안 건드림(캡슐화).

### 위젯 트리 (디자이너)
```
[Root]
└─ Overlay  "CrosshairRoot"   ← Is Variable ✔ (ADS 바인딩 대상)
   ├─ Image  "Bar_Top"
   ├─ Image  "Bar_Bottom"
   ├─ Image  "Bar_Left"
   ├─ Image  "Bar_Right"
   └─ Image  "Dot_Center"
```

- 루트에 **Overlay** 하나, 이름 `CrosshairRoot`, **Is Variable** 체크.
- Overlay 안에 Image 5개. 각 Image 슬롯(Overlay Slot) **Horizontal/Vertical Alignment = Center**.

### 각 Image 속성 (플레이스홀더 — 흰 십자 + 점)
브러시는 텍스처 없이 단색: 각 Image → **Appearance → Brush → Draw As = `Image`**, **Tint = 흰색(1,1,1,1)**.

| 위젯 | 크기(px) | 위치(Overlay Slot Padding) |
|---|---|---|
| `Bar_Top` | 2 × 8 | 위로 (Padding Bottom 12) |
| `Bar_Bottom` | 2 × 8 | 아래로 (Padding Top 12) |
| `Bar_Left` | 8 × 2 | 왼쪽 (Padding Right 12) |
| `Bar_Right` | 8 × 2 | 오른쪽 (Padding Left 12) |
| `Dot_Center` | 2 × 2 | 중앙(패딩 0) |

> 가독성 팁(선택): 어두운 외곽선이 필요하면 각 바 뒤에 1px 큰 검정 Image를 깔거나 폴리시(후속)로 미룬다. **지금은 플레이스홀더면 충분.**
> 갭(12)·두께(2)는 PIE에서 보며 미세조정.

---

## STEP 2 — ADS 시 숨김 (핵심 배선)

`WBP_BasicCrosshair` 그래프에서 `CrosshairRoot`의 **Visibility를 바인딩**한다.

1. 디자이너 `CrosshairRoot` 선택 → Details **Behavior → Visibility** 옆 **Bind → Create Binding**.
2. 생성된 함수(`Get_CrosshairRoot_Visibility`) 그래프:

```
Get Owning Player Pawn
      └─► Get Component By Class  (Component Class = FPSRWeaponFireComponent)
                  └─► [Is Valid?]  ──(Valid)──► IsAiming()  ──► Branch
                          │                                        │ True  → Return: Hidden (또는 Collapsed)
                          │                                        │ False → Return: Visible
                          └──(Not Valid)──────────────────────────► Return: Visible
```

- `Get Component By Class`의 **Component Class** = `FPSRWeaponFireComponent`.
- `IsAiming`은 `BlueprintPure`라 바로 사용(추가 C++ 없음).
- 반환 타입 **ESlateVisibility**: 조준 중 `Hidden`, 평상시 `Visible`.

> Visibility 바인딩은 매 프레임 평가되나 위젯 1개 룩업이라 비용 무시 가능. (원하면 `Event Construct`에서 FireComponent를 변수로 캐싱 — 선택.)

**컴파일 + 저장.**

---

## STEP 3 — `WBP_GameHUD`에 배치

`WBP_GameHUD` 열기:

1. `WBP_HitMarker`가 들어있는 같은 **Canvas Panel**에 `WBP_BasicCrosshair`를 자식으로 추가.
2. 슬롯 **Anchors = 중앙(0.5,0.5)**, **Alignment = (0.5,0.5)**, **Position X/Y = 0** → 화면 정중앙.
3. **Z-Order**: `WBP_BasicCrosshair`를 `WBP_HitMarker`보다 **아래**(트리에서 히트마커 위에 배치 → 나중 그려지는 히트마커가 위로).
4. 컴파일 + 저장.

---

## STEP 3-B — 히트마커 색상 구분 (명중=흰 대각선 / 처치=빨강)

> ⚠️ **새로 만들지 말 것.** 흰색 대각선 히트마커는 **이미 존재**한다(`WBP_HitMarker`, §2-14, P4-D 완료 — 스크린샷의 중앙 흰 X). 이 단계는 기존 위젯에 **MarkerType별 색 분기만 추가**한다. 순수 콘텐츠, **C++ 변경 없음**.

### 이미 배선된 것 (조사 확인)
- 마커 종류 enum: `EFPSRHitMarkerType { Hit, Crit, Kill }`(`Source/.../Hero/FPSRFeedbackTypes.h`).
- 전 무기 경로(Hitscan/Melee/ChargeLaser/Projectile/CombatStatics)가 서버권위로 Hit/Crit/**Kill**을 산출 → `AFPSRPlayerController`(`FPSRPlayerController.cpp:391`)가 `UFPSRPlayerFeedbackComponent::NotifyHitConfirmed(MarkerType)` 호출 → **`OnHitMarker(MarkerType)` 델리게이트 브로드캐스트**.
- 즉 **Kill 타입이 이미 클라이언트까지 도달**한다 — `WBP_HitMarker`가 색만 분기하면 끝.

### 작업: `WBP_HitMarker` 편집
1. `Content/UI/HUD/WBP_HitMarker` 열기.
2. **`OnHitMarker` 이벤트 핸들러**를 찾는다(현재 흰 X 펄스를 트리거하는 곳). 이 이벤트는 파라미터 **`MarkerType`(EFPSRHitMarkerType)** 을 받는다.
   - 만약 핸들러가 없다면(흰 X가 PlayerController 직접 호출이면): `Event Construct`에서 오너 폰의 `FPSRPlayerFeedbackComponent`를 `Get Component By Class`로 얻어 **`OnHitMarker` → Bind Event**(AddDynamic)로 커스텀 이벤트 `ShowHitMarker(MarkerType)`에 연결.
3. 마커 비주얼(대각선 X Image, 예: `Img_Marker`)의 색을 **MarkerType로 분기**:
   - `Switch on EFPSRHitMarkerType`(MarkerType 입력) →
     - **Hit** → `SetColorAndOpacity` = 흰색 `(1,1,1,1)`
     - **Crit** → 흰색(또는 폴리시로 노랑 — 선택. 지금은 흰색 권장, 처치만 빨강 요구)
     - **Kill** → `SetColorAndOpacity` = 빨강 `(1,0,0,1)`
   - 색 적용 후 **기존 펄스 애니메이션 재생**(이미 있는 `PlayAnimation`)으로 연결.
   - 대상이 여러 Image(대각선 4개)면 모두 같은 색을 적용하거나, 공통 부모 패널의 `SetColorAndOpacity`로 일괄 틴트.
4. 컴파일 + 저장.

> 설계 메모(§2-14): "히트마커 최종 연출은 크로스헤어/발사체 작업 후 재확인" — V3 시점에 색 구분을 확정하는 게 그 재확인에 해당. **틴트만** 추가(레이아웃/펄스 타이밍은 기존 유지). 약점(헤드샷) 전용 마커는 U3a/U12 범위 — 여기선 Hit/Kill 색만.

---

## STEP 4 — 검증 (PIE)

- [ ] 크로스헤어가 화면 **정중앙** 표시.
- [ ] 발사해도 위치 정합(정적이라 중앙 고정이 정상).
- [ ] **우클릭 ADS 중 숨김**, 떼면 다시 표시.
- [ ] 히트마커가 크로스헤어 **위에** 겹쳐 보임.
- [ ] **적 명중 시 흰색 대각선** 마커 펄스.
- [ ] **적 처치 시 빨간색** 마커 펄스(Kill 색 분기 동작).
- [ ] **메뉴(Menu 레이어)에선 미표시** — Game 레이어 전용이라 자동 충족(확인만).

---

## STEP 5 — 커밋 / 머지 (§B-3)

PIE 통과 후:
```powershell
git add Content/UI/HUD/WBP_BasicCrosshair.uasset Content/UI/HUD/WBP_GameHUD.uasset Content/UI/HUD/WBP_HitMarker.uasset
git commit -m "content(V3): 기본 정적 크로스헤어 + ADS 숨김 + 히트마커 색상 구분(명중 흰/처치 빨강)"
git push -u origin phase/p4d-crosshair
```
- 코드 변경 없음 → Codex 머지게이트는 콘텐츠 바이너리라 diff 무관(스킵 가능).
- 사용자 검토(PIE) 승인 → `--no-ff` main 머지 → PROGRESS 갱신 + TaskPrompts §B에 V3 ✅.

---

## 하지 말 것 (범위 경계)
- ❌ 동적 스프레드(원 확대/축소) — **U12** 범위. `GetCurrentBloom()`/`SpreadDegrees` 읽지 말 것.
- ❌ 무기별 다른 크로스헤어(샷건 원 등) — U12/폴리시.
- ❌ 에셋 경로 C++ 하드코딩 — 해당 없음(순수 WBP).
