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

## STEP 4 — 검증 (PIE)

- [ ] 크로스헤어가 화면 **정중앙** 표시.
- [ ] 발사해도 위치 정합(정적이라 중앙 고정이 정상).
- [ ] **우클릭 ADS 중 숨김**, 떼면 다시 표시.
- [ ] 히트마커가 크로스헤어 **위에** 겹쳐 보임.
- [ ] **메뉴(Menu 레이어)에선 미표시** — Game 레이어 전용이라 자동 충족(확인만).

---

## STEP 5 — 커밋 / 머지 (§B-3)

PIE 통과 후:
```powershell
git add Content/UI/HUD/WBP_BasicCrosshair.uasset Content/UI/HUD/WBP_GameHUD.uasset
git commit -m "content(V3): 기본 정적 크로스헤어 + ADS 숨김 (WBP_BasicCrosshair, WBP_GameHUD 중앙 배치)"
git push -u origin phase/p4d-crosshair
```
- 코드 변경 없음 → Codex 머지게이트는 콘텐츠 바이너리라 diff 무관(스킵 가능).
- 사용자 검토(PIE) 승인 → `--no-ff` main 머지 → PROGRESS 갱신 + TaskPrompts §B에 V3 ✅.

---

## 하지 말 것 (범위 경계)
- ❌ 동적 스프레드(원 확대/축소) — **U12** 범위. `GetCurrentBloom()`/`SpreadDegrees` 읽지 말 것.
- ❌ 무기별 다른 크로스헤어(샷건 원 등) — U12/폴리시.
- ❌ 에셋 경로 C++ 하드코딩 — 해당 없음(순수 WBP).
