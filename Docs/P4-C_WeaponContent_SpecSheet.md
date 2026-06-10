# P4-C 무기 콘텐츠 스펙시트 (따라하기 — 에디터 작업용)

> **전제: A2(Hitscan)·A3(AOE+ChargeLaser) 코드가 main에 머지됨(2026-06-10).** 에디터를 열면 아래 스탯 필드(`PelletCount`/`MaxPenetration`/`ProjectileSpeed`/`AOERadius`/`ChargeTime` 등)와 GA 클래스(`FPSRGA_WeaponFire_Projectile`/`_ChargeLaser`)가 모두 노출됩니다.
> 이 문서는 [Docs/P4-C_UserContent_Guide.md](P4-C_UserContent_Guide.md)의 **시작 제안값을 실제 필드 단위로 확정**한 작업 시트입니다. 수치는 PIE 튜닝 대상(시작점).
> **MCP 미인증으로 AI가 에셋을 직접 생성할 수 없어**, 사용자가 에디터에서 아래대로 생성·연결합니다.

---

## 0. 공통 작업 흐름

1. **베이스 복제**: `Content/Weapons/DataTable/DA_Weapon_Rifle`를 복제(Ctrl+D)해 시작 → 반동/블룸/ADS 기본값을 물려받고 아래 표의 필드만 덮어쓰기. (Knife는 근접이라 베이스로 부적합.)
   - 또는 우클릭 > Miscellaneous > Data Asset > **`FPSRWeaponDataAsset`** 신규 생성 후 전 필드 입력.
2. 파일명/위치: `Content/Weapons/DataTable/DA_Weapon_<이름>` (기존 Rifle/Knife와 동일 폴더).
3. **DataAsset 최상위 필드**(4개): `DisplayName`, `Archetype`, `FireAbility`, `ProjectileClass`(AOE만). 나머지 게임플레이 수치는 전부 **`BaseStats`**(=`FFPSRWeaponStatBlock`) 안에 있음.
4. 투사체 무기(Bazooka/Grenade)는 §2의 `BP_Rocket`/`BP_Grenade`를 먼저 만들고 `ProjectileClass`에 지정.
5. 표기 규약: `BaseStats.X` = BaseStats 구조체 안의 X 필드. 표에 없는 필드는 **Rifle 베이스값 유지**.

> ⚠️ **FireAbility 값**: 에디터 드롭다운에서 클래스 선택 — `FPSRGA_WeaponFire_Hitscan` / `FPSRGA_WeaponFire_Projectile` / `FPSRGA_WeaponFire_ChargeLaser`.

---

## 1. 무기 6종 DataAsset

### 1-1. `DA_Weapon_BurstRifle` (점사)
| 필드 | 값 |
|---|---|
| DisplayName | "Burst Rifle" |
| Archetype | **Burst** |
| FireAbility | **FPSRGA_WeaponFire_Hitscan** |
| ProjectileClass | (비움) |
| BaseStats.FireMode | **Burst** |
| BaseStats.BurstCount | **3** |
| BaseStats.Damage | 12 |
| BaseStats.FireRate | 12 |
| BaseStats.MagSize | 24 |
| BaseStats.bHasADS | true |
| BaseStats.ADSSpreadMultiplier | 0.6 |

### 1-2. `DA_Weapon_Sniper` (단발 관통)
| 필드 | 값 |
|---|---|
| DisplayName | "Sniper" |
| Archetype | **Sniper** |
| FireAbility | **FPSRGA_WeaponFire_Hitscan** |
| BaseStats.FireMode | **Single** |
| BaseStats.Damage | 120 |
| BaseStats.FireRate | 1.2 |
| BaseStats.**MaxPenetration** | **3** (일렬 적 3마리 관통; 1=첫 적 정지) |
| BaseStats.Range | 15000 |
| BaseStats.RecoilRecovery | **Auto** (단발 자동복구) |
| BaseStats.MagSize | 5 |
| BaseStats.ReloadTime | 2.5 |
| BaseStats.bHasADS | true |
| BaseStats.ADSFieldOfView | 30 (정밀 줌) |

### 1-3. `DA_Weapon_Shotgun` (산탄)
| 필드 | 값 |
|---|---|
| DisplayName | "Shotgun" |
| Archetype | **Shotgun** |
| FireAbility | **FPSRGA_WeaponFire_Hitscan** |
| BaseStats.FireMode | Single (또는 FullAuto) |
| BaseStats.**PelletCount** | **8** (1탄약당 8펠릿) |
| BaseStats.Damage | 10 (펠릿당; 근접 합산 큼) |
| BaseStats.SpreadDegrees | 8 (산탄 퍼짐) |
| BaseStats.Range | 3000 (짧게) |
| BaseStats.MagSize | 6 |
| BaseStats.FireRate | 1.5 |

### 1-4. `DA_Weapon_Bazooka` (AOE 로켓)
| 필드 | 값 |
|---|---|
| DisplayName | "Bazooka" |
| Archetype | **AOE** |
| FireAbility | **FPSRGA_WeaponFire_Projectile** |
| **ProjectileClass** | **BP_Rocket** (§2) |
| BaseStats.FireMode | Single |
| BaseStats.Damage | 80 (폭발) |
| BaseStats.**AOERadius** | **400** (>0=폭발 반경; 0이면 단일타격) |
| BaseStats.ProjectileSpeed | 3000 (직선 로켓) |
| BaseStats.ProjectileGravityScale | **0** (직선) |
| BaseStats.ProjectileLifetime | 5 |
| BaseStats.MagSize | 4 |
| BaseStats.ReloadTime | 2.0 |

### 1-5. `DA_Weapon_Grenade` (AOE 유탄, 포물선)
| 필드 | 값 |
|---|---|
| DisplayName | "Grenade" |
| Archetype | **AOE** |
| FireAbility | **FPSRGA_WeaponFire_Projectile** |
| **ProjectileClass** | **BP_Grenade** (§2) |
| BaseStats.FireMode | Single |
| BaseStats.Damage | 70 |
| BaseStats.**AOERadius** | **300** |
| BaseStats.**ProjectileGravityScale** | **1.0** (포물선 낙하) |
| BaseStats.ProjectileSpeed | 1800 (던지는 느낌) |
| BaseStats.ProjectileLifetime | 5 |
| BaseStats.MagSize | 2 |
| BaseStats.ReloadTime | 2.0 |

### 1-6. `DA_Weapon_ChargeLaser` (차징 관통 빔)
| 필드 | 값 |
|---|---|
| DisplayName | "Charge Laser" |
| Archetype | **ChargeLaser** |
| FireAbility | **FPSRGA_WeaponFire_ChargeLaser** |
| BaseStats.FireMode | Single |
| BaseStats.Damage | 30 (alpha 0=기본) |
| BaseStats.**ChargeTime** | **1.5** (풀차징까지 초) |
| BaseStats.**ChargeFullDamageMultiplier** | **3.0** (풀차징 데미지 배수) |
| BaseStats.Range | 15000 (관통 빔) |
| BaseStats.MagSize | 10 (1발=1탄약) |
| BaseStats.bHasADS | true |

> 동작: **누르면 충전, 떼면 발사**(충전량 비례 데미지). 벽 전까지 모든 적 관통. 서버권위 충전(안티치트). 빔 시각/차징 게이지 HUD는 후속 코드(가이드 §4).

---

## 2. 투사체 actor BP (AOE 전용)

`BP_Rocket`, `BP_Grenade` 둘 다:
1. `Content/Weapons/Projectiles/`에서 우클릭 > Blueprint Class > **부모 = `FPSRProjectile`** (C++).
2. (선택) `MeshComp`에 메시 지정 — 없으면 엔진 기본 Sphere/Cylinder(작게)로도 기능 OK. **임팩트/폭발 VFX·사운드는 C++ 훅 없음 → 후속**(현재 데미지만, 시각 없음). 기능 검증엔 불필요.
3. (선택) 콜리전 구 반경 조정(기본 16cm).
4. §1-4/1-5 DA의 `ProjectileClass`에 각각 지정.

> 로켓/유탄의 직선 vs 포물선 차이는 BP가 아니라 **DA의 `ProjectileGravityScale`**(0 vs 1.0)로 결정됨. BP는 동일 부모로 충분.

---

## 3. 테스트 (무기 픽업 미구현 → 시작 슬롯으로)

`Content/Character/Player/BP_FPSRPlayer` 열기 → Details에서 **`DefaultPrimaryWeapon`**(또는 `DefaultSecondaryWeapon`)에 테스트할 신규 DA 임시 지정 → PIE.

**확인 포인트:**
- **BurstRifle**: 1클릭 = 3발 점사.
- **Sniper**: 단발·고데미지·**일렬 적 3마리 관통**·ADS 줌. (벽 뒤 적은 안 맞음 — Visibility 벽판정 교정 반영됨)
- **Shotgun**: 1발 = 8펠릿 산탄.
- **Bazooka**: 직선 로켓 비행 → 폭발 반경(400) 내 다수 적 데미지. **근접 벽에 쏘면 벽면에서 폭발**(관통 안 됨 — 머즐 클램프 교정 반영).
- **Grenade**: 포물선 낙하 → 폭발(300).
- **ChargeLaser**: 짧게 탭=약함, 길게 홀드=강함(최대 ×3). 일렬 적 전부 관통. 빔=`ENABLE_DRAW_DEBUG` 시 시안색 선. **홀드 중 무기 교체 후 복귀해도 차징 안 쌓임**(뱅킹 차단 교정 반영).
- **멀티(서버권위)**: 2-client PIE에서 클라가 쏠 때 서버 데미지·차징 정상. 특히 ChargeLaser 빠른 탭/홀드가 클라·서버 일관(RPC 순서 교정 반영).

---

## 4. 후속(콘텐츠만으론 불가 — 별도 코드 유닛)

- 투사체 **임팩트/폭발 VFX·사운드** 훅(`AFPSRProjectile::HandleImpact` 콜백).
- ChargeLaser **빔 Niagara** + **차징 게이지 HUD**(alpha 게터/이벤트 배선).
- 무기별 `AvailableModifiers` 미션보상 fragment 카드 확장(현재 MultiShot/BonusDamage만).

---

## 5. 머지 체크리스트 진행

- [x] `phase/p4c-hitscan`(A2) Codex → P2 교정 → `--no-ff` main 머지 (2026-06-10)
- [x] `phase/p4c-aoe-charge`(A3) Codex → P1×2+P2×2 교정 → reconcile → `--no-ff` main 머지 (2026-06-10)
- [x] post-merge 통합 빌드+스모크 통과
- [ ] 6종 무기 DA + `BP_Rocket`/`BP_Grenade` 작성 (이 문서)
- [ ] `BP_FPSRPlayer` Default 슬롯으로 각 무기 PIE 검증(§3)
- [ ] (선택) 콘텐츠 동반 커밋 → main
