# P4-C 무기 콘텐츠 스펙시트 (따라하기 — 에디터 작업용)

> **전제: A2(Hitscan)·A3(AOE+ChargeLaser) 코드가 main에 머지됨(2026-06-10).** 에디터를 열면 아래 스탯 필드(`PelletCount`/`MaxPenetration`/`ProjectileSpeed`/`AOERadius`/`ChargeTime` 등)와 GA 클래스(`FPSRGA_WeaponFire_Projectile`/`_ChargeLaser`)가 모두 노출됩니다.
> 이 문서는 P4-C 무기 콘텐츠의 **시작 제안값을 실제 필드 단위로 확정**한 작업 시트입니다. 수치는 PIE 튜닝 대상(시작점).
> **MCP 미인증으로 AI가 에셋을 직접 생성할 수 없어**, 사용자가 에디터에서 아래대로 생성·연결합니다.

---

## 0. 공통 작업 흐름

1. **베이스 복제**: `Content/Weapons/DataTable/DA_Weapon_Rifle`를 복제(Ctrl+D)해 시작 → 반동/블룸/ADS 기본값을 물려받고 아래 표의 필드만 덮어쓰기. (Knife는 근접이라 베이스로 부적합.)
   - 또는 우클릭 > Miscellaneous > Data Asset > **`FPSRWeaponDataAsset`** 신규 생성 후 전 필드 입력.
2. 파일명/위치: `Content/Weapons/DataTable/DA_Weapon_<이름>` (기존 Rifle/Knife와 동일 폴더).
3. **DataAsset 최상위 필드**: `DisplayName`, `FireAbility`, `ProjectileClass`(AOE만), `BaseStats`. **`Archetype`은 `BaseStats` 안으로 이동**(2026-06-10) — `BaseStats.Archetype`에서 설정.
4. **⭐ 아키타입별 조건부 노출(2026-06-10)**: `BaseStats.Archetype`을 먼저 고르면, **그 아키타입에 관련된 필드만 나타납니다**. 예) Shotgun → `PelletCount` 표시 / Sniper → `MaxPenetration` / AOE → Projectile 5필드 / ChargeLaser → Charge 2필드 / Melee → Melee 2필드. `BurstCount`는 `FireMode=Burst`일 때만, ADS 세부필드는 `bHasADS=true`일 때만 표시. **→ Archetype·FireMode·bHasADS를 먼저 설정**한 뒤 나머지를 채우세요.
5. 투사체 무기(Bazooka/Grenade)는 §2의 `BP_Rocket`/`BP_Grenade`를 먼저 만들고 `ProjectileClass`에 지정.
6. 표기 규약: `BaseStats.X` = BaseStats 구조체 안의 X 필드. 표에 없는 필드는 **Rifle 베이스값 유지**.

> ⚠️ **FireAbility 값**: 에디터 드롭다운에서 클래스 선택 — `FPSRGA_WeaponFire_Hitscan` / `FPSRGA_WeaponFire_Projectile` / `FPSRGA_WeaponFire_ChargeLaser`.
> ⚠️ **기존 `DA_Weapon_Knife` 재설정 필요**: Archetype 필드 이동으로 Knife의 값이 FullAuto로 리셋됨 → `BaseStats.Archetype = Melee`로 다시 설정(안 하면 칼이 총처럼 동작). Rifle은 FullAuto 기본값이라 보통 무영향(확인 권장).

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

### 1-2. `DA_Weapon_Sniper` (단발 관통 **탄환/투사체** — 리드샷)
> 2026-06-11 변경: 스나이퍼 = **travel-time 탄환**(투사체)으로 전환 → 움직이는 적은 **리드샷** 필요. 히트스캔 아님. 투사체 임팩트 크릿+히트마커 코드 완료. **MaxPenetration(히트스캔용)이 아니라 `ProjectilePierce`로 관통**.
| 필드 | 값 |
|---|---|
| DisplayName | "Sniper" |
| Archetype | **Sniper** |
| FireAbility | **FPSRGA_WeaponFire_Projectile** (히트스캔 아님!) |
| **ProjectileClass** | **BP_Bullet** (§2) |
| BaseStats.FireMode | **Single** |
| BaseStats.Damage | 120 |
| BaseStats.FireRate | 1.2 |
| BaseStats.**AOERadius** | **0** (단일타격 탄환; >0이면 폭발) |
| BaseStats.**ProjectileSpeed** | **12000** (리드 체감용 — 빠르되 즉발 아님; PIE 튜닝) |
| BaseStats.ProjectileGravityScale | **0** (직선 탄도) |
| BaseStats.**ProjectilePierce** | **2** (관통 적 수; 0=첫 적 정지) |
| BaseStats.ProjectileLifetime | 3 |
| BaseStats.Range | 15000 |
| BaseStats.RecoilRecovery | **Auto** (단발 자동복구) |
| BaseStats.MagSize | 5 |
| BaseStats.ReloadTime | 2.5 |
| BaseStats.bHasADS | true |
| BaseStats.ADSFieldOfView | 30 (정밀 줌) |
| **— 반동 스타일(2026-06-11 지정: 묵직한 스코프 킥) —** | |
| BaseStats.**RecoilVertical** | **5.0** (강한 상방 킥/발) |
| BaseStats.RecoilHorizontal | 0.5 |
| BaseStats.**ADSVerticalScale** | **1.0** (스코프 시 풀 킥) |
| BaseStats.HipVerticalScale | 1.0 |
| BaseStats.ADSHorizontalRandom | 0.1 (정밀, 좌우 거의 없음) |
| BaseStats.RecoilRiseRate | 40 (날카로운 킥) |
| BaseStats.RecoilRecoveryRate | 5 (천천히 무겁게 복귀) |

### 1-3. `DA_Weapon_Shotgun` (산탄)
| 필드 | 값 |
|---|---|
| DisplayName | "Shotgun" |
| Archetype | **Shotgun** |
| FireAbility | **FPSRGA_WeaponFire_Hitscan** |
| BaseStats.FireMode | Single (또는 FullAuto) |
| BaseStats.**PelletCount** | **8** (1탄약당 8펠릿) |
| BaseStats.Damage | 10 (펠릿당; 근접 합산 큼) |
| BaseStats.**SpreadDegrees** | **8** (산탄 원 = 크로스헤어 원 크기) |
| BaseStats.**BloomPerShot** | **0** (원 크기 고정 — 블룸 성장 없음) |
| BaseStats.Range | 3000 (짧게) |
| BaseStats.MagSize | 6 |
| BaseStats.FireRate | 1.5 |
| **— 반동 스타일(2026-06-11 지정: 강한 펀치 킥) —** | |
| BaseStats.**RecoilVertical** | **3.5** (강한 상방 펀치/발) |
| BaseStats.RecoilHorizontal | 0.8 |
| BaseStats.**HipVerticalScale** | **1.0** (힙 사격 풀 킥 — 기본 0.4는 너무 약함) |
| BaseStats.HipHorizontalRandom | 0.6 |
| BaseStats.RecoilRiseRate | 35 (빠른 펀치) |
| BaseStats.RecoilRecovery | **Auto** (단발 자동 스냅백) |
| BaseStats.RecoilRecoveryRate | 7 |

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
| BaseStats.**SpreadDegrees** | **4** (로켓 랜덤 산포 원 = 크로스헤어 원; 0이면 정확히 직선) |
| BaseStats.**BloomPerShot** | **0** (원 크기 고정) |
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

## 2. 투사체 actor BP (AOE + 탄환)

`BP_Rocket`, `BP_Grenade`, **`BP_Bullet`** 모두:
1. `Content/Weapons/Projectiles/`에서 우클릭 > Blueprint Class > **부모 = `FPSRProjectile`** (C++).
2. (선택) `MeshComp`에 메시 지정 — 없으면 엔진 기본 Sphere/Cylinder(작게)로도 기능 OK. **임팩트/폭발 VFX·사운드는 C++ 훅 없음 → 후속**(현재 데미지만, 시각 없음). 기능 검증엔 불필요. **탄환은 얇고 작게**(가는 실린더/작은 구), 트레이서 트레일은 BP에 직접 붙이면 됨.
3. (선택) 콜리전 구 반경 조정(기본 16cm; 탄환은 더 작게 권장).
4. §1-2/1-4/1-5 DA의 `ProjectileClass`에 각각 지정(Bullet→Sniper, Rocket→Bazooka, Grenade→Grenade).

> 직선/포물선/탄환 차이는 BP가 아니라 **DA 스탯**(`ProjectileGravityScale` 0 vs 1.0, `AOERadius` 0 vs >0, `ProjectileSpeed`)로 결정됨. BP는 동일 부모로 충분.
> **투사체 크릿·히트마커 = 코드 완료**(2026-06-11): 임팩트 시 서버가 크릿 롤 + 발사 플레이어에 히트마커(Kill>Crit>Hit). 탄환=관통마다, AOE=폭발당 1회. 로켓/유탄도 자동 적용.

---

## 3. 테스트 (무기 픽업 미구현 → 시작 슬롯으로)

`Content/Character/Player/BP_FPSRPlayer` 열기 → Details에서 **`DefaultPrimaryWeapon`**(또는 `DefaultSecondaryWeapon`)에 테스트할 신규 DA 임시 지정 → PIE.

**확인 포인트:**
- **BurstRifle**: 1클릭 = 3발 점사.
- **Sniper(탄환/투사체)**: 단발·고데미지·**탄환이 날아감(travel time)** → **움직이는 적은 리드샷 필요**. 일렬 적 `ProjectilePierce`(2)마리 관통, 벽에서 정지. ADS 줌. **명중 시 히트마커 표시**(크릿/킬 포함). 빠른 적엔 앞을 조준.
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
- **동적 스프레드 크로스헤어(원 레티클)** — 샷건·바주카 등 산포 무기의 크로스헤어에 현재 스프레드 크기만큼 원을 그림. **발사(원 안 랜덤)는 이미 구현됨**(`SpreadDegrees`+`VRandCone`); 신규는 **시각 표시만**. ① 코드: `UFPSRWeaponFireComponent`에 유효 스프레드 게터 노출(`GetCurrentSpreadDegrees()` = 해석 SpreadDegrees + 블룸, ADS 배수 반영; 기존 `GetCurrentBloom` 옆). ② 콘텐츠: WBP 크로스헤어에 원 이미지/머티리얼, 반지름 = `tan(SpreadDegrees)×화면상수`로 스케일. `BloomPerShot=0`이면 고정 원, >0이면 사격 중 확대. 전 산포 무기 범용(샷건 전용 아님). → 별도 HUD 유닛(가이드 §4 차징 게이지 HUD와 함께 묶을 수 있음).

---

## 5. 머지 체크리스트 진행

- [x] `phase/p4c-hitscan`(A2) Codex → P2 교정 → `--no-ff` main 머지 (2026-06-10)
- [x] `phase/p4c-aoe-charge`(A3) Codex → P1×2+P2×2 교정 → reconcile → `--no-ff` main 머지 (2026-06-10)
- [x] post-merge 통합 빌드+스모크 통과
- [ ] 6종 무기 DA + `BP_Rocket`/`BP_Grenade` 작성 (이 문서)
- [ ] `BP_FPSRPlayer` Default 슬롯으로 각 무기 PIE 검증(§3)
- [ ] (선택) 콘텐츠 동반 커밋 → main
