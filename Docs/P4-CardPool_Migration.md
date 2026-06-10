# P4 카드 풀 재구성 — 콘텐츠 마이그레이션 가이드 (에디터 작업)

> **코드 완료**(`phase/p4-card-weapon-pools`, 빌드+스모크 통과). 아래대로 카드를 재배치하면 "칼 들고 레벨업 시 탄약/연사 카드(꽝)" 문제가 사라지고, 무기별 카드가 그 무기에만 적용됩니다.
> MCP 미인증으로 AI가 .uasset을 못 만지므로 **사용자가 에디터에서** 진행. 코드는 이미 `main`에 머지될 준비 완료(이 작업 후 머지 권장).

---

## 새 규칙 (코드가 이렇게 동작함)

| 카드 종류 | 어디에 두나 | 적용 대상 | 레벨업 노출 조건 |
|---|---|---|---|
| **Character** (체력/럭/이속 등) | **중앙 풀** (`DA_Character_CardPool`) | 캐릭터(ASC) | 항상 |
| **AllWeapons** (전 무기 연사/탄창 등) | **중앙 풀** | 보유 전 무기(PlayerState) | 무기 보유 시 |
| **ThisWeapon** (그 무기 전용 강화) | **각 무기 DA의 `WeaponCards`** | **그 무기**(소스 무기, 장착 무관) | 그 무기 **보유** 시 |
| **Fragment** (MultiShot/BonusDamage 등) | **각 무기 DA의 `AvailableModifiers`** | 그 무기 | **미션 보상** 때만 (레벨업 아님) |

**핵심**: ThisWeapon 카드는 이제 **그 카드가 들어있는 무기**에 적용됩니다. 그래서:
- 칼의 `WeaponCards`에 탄약/연사 카드를 **안 넣으면** → 칼 들고 레벨업해도 그 카드가 안 뜸(꽝 원천 차단).
- 스나이퍼 `WeaponCards`의 카드는 스나이퍼에만 적용(다른 무기 장착 중이어도).

> 💡 **같은 카드 에셋을 여러 무기에서 재사용 가능**: `DA_Card_MagSize_ThisWeapon` 하나를 Rifle·Sniper·Shotgun의 `WeaponCards`에 각각 넣으면, 각 무기에 알아서 귀속됩니다(무기별 중복 에셋 불필요). 수치를 무기마다 다르게 하고 싶을 때만 별도 에셋.

---

## 작업 절차

### 1. 중앙 풀 정리 — `Content/Cards/Character/DA_Character_CardPool`
`Cards` 배열에서 **`*_ThisWeapon` 카드 3개를 제거**(아래로 이동):
- ❌ 빼기: `DA_Card_MagSize_ThisWeapon`, `DA_Card_FireRate_ThisWeapon`, `DA_Card_RecoilVertical_ThisWeapon`
- ✅ 남기기: 모든 **Character** 카드 + **AllWeapons** 카드(`DA_Card_*_AllWeapon`)

### 2. 각 무기 DA의 `WeaponCards` 채우기 (그 무기 전용 ThisWeapon 카드)
무기 성격에 맞게:
- **Rifle / BurstRifle / Sniper / Shotgun**(사거리 무기) → `DA_Card_MagSize_ThisWeapon`, `DA_Card_FireRate_ThisWeapon`, `DA_Card_RecoilVertical_ThisWeapon` 넣기(공유 에셋 재사용 OK).
- **Knife**(근접) → 탄약/연사/반동 카드 **넣지 말 것**. 근접용 ThisWeapon 카드(예: 근접 데미지)가 있으면 그것만, 없으면 비워둠.
- **Bazooka/Grenade/ChargeLaser** → 취향껏(데미지·차징 등 그 무기에 의미 있는 것만).

### 3. 각 무기 DA의 `AvailableModifiers` 채우기 (미션 보상 fragment)
- 그 무기에 줄 fragment 카드(`DA_CardModifiers_MultiShot`, `DA_CardModifiers_BounsDamage` 등)를 무기별로 배치.
- 미션 클리어 시 **보유한 모든 무기**의 AvailableModifiers를 통합해 랜덤 3개(부족하면 그만큼) 제시 → 고르면 그 무기에 적용.

---

## PIE 검증 포인트
1. **칼만 들고** 레벨업(`FPSR.AddXP`) → 탄약/연사 ThisWeapon 카드가 **안 뜨는지**(중앙 풀에서 뺐고 칼 WeaponCards에 없으므로).
2. **사거리 무기 보유** 상태로 레벨업 → 그 무기 카드가 뜨고, 골랐을 때 **그 무기에** 적용(`FPSR.DumpWeaponStats`로 확인).
3. **여러 무기 보유** → 각 무기의 ThisWeapon 카드가 다 후보에 뜨고 각자 자기 무기에 적용.
4. **미션 클리어**(`FPSR.MissionTrigger`/`FPSR.GrantMissionRewardPick`) → 보유 무기들의 fragment 중 랜덤 3개, 부족하면 그만큼.
5. **2-client PIE**: 클라가 골라도 서버가 소스 무기로 정확히 적용(안티치트: 미보유 무기 타깃 거부).

---

## 머지
콘텐츠 재배치 + PIE 통과 후 → 코드 `phase/p4-card-weapon-pools`를 Codex `-Base main` 게이트 후 `--no-ff` main 머지. (콘텐츠 동반 커밋 여부는 그때 결정.)
