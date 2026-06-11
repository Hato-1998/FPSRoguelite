# P4-C 사용자 콘텐츠 가이드 — 무기 6종 (Hitscan 3 + AOE 2 + ChargeLaser 1)

> C++ 베이스 완료(빌드+스모크+Opus 검증 통과, Codex는 머지 시점 일괄). 아래 콘텐츠(DataAsset/BP)를 에디터에서 만들어 연결하면 PIE에서 신규 무기가 동작한다. **에셋 경로는 C++에 하드코딩하지 않으므로 전부 DA/BP로 연결**한다.
> **무기는 순수 DataAsset**(`DA_Weapon_*`). 별도 "무기 BP"는 불필요(기존 Rifle/Knife도 DA만 존재). **AOE 무기만** 별도 투사체 actor BP가 필요.

---

## 0. 전제 / 작업 순서 (중요)

신규 무기 DA를 작성하려면 **에디터에 해당 코드가 컴파일되어 있어야** 한다(예: 샷건 `PelletCount` 필드는 A2 코드, 레이저 `ChargeTime`은 A3 코드). 이 코드들은 **2개의 미머지 브랜치**에 나뉘어 있다:
- `phase/p4c-hitscan` (A2): Burst/Sniper/Shotgun + `PelletCount`/`MaxPenetration` 스탯
- `phase/p4c-aoe-charge` (A3): AOE 투사체 GA + ChargeLaser + 투사체/차징 스탯, `OnProjectileSpawn`/`ModifyChargeTime` 훅

**권장 순서 = 코드 먼저 머지 → main에서 콘텐츠 일괄**:
1. **`phase/p4c-hitscan` → main 머지** (Codex `-Base main` 일괄 리뷰 후 `--no-ff`).
2. **`phase/p4c-aoe-charge` → main 머지** (Codex 리뷰 + **PROGRESS reconcile**: 두 브랜치가 PROGRESS/`FFPSRWeaponStatBlock`을 각각 갱신 → 충돌 해소 필요).
3. main에서 **6종 무기 DA + `BP_Rocket`/`BP_Grenade` 작성** (모든 스탯 필드·GA 클래스가 한 곳에).
4. PIE 검증 → 필요 시 미션(`phase/p4b3-missions`)도 동일 절차.

> 머지 없이 브랜치별로 콘텐츠를 만들면 A2/A3 무기가 다른 브랜치에 흩어지고 `FFPSRWeaponStatBlock` 머지 충돌이 콘텐츠 작성 후로 미뤄진다 → **머지 먼저** 권장.

권장 콘텐츠 폴더: `Content/Weapons/DataTable/`(무기 DA, 기존 Rifle/Knife와 동일), `Content/Weapons/Projectiles/`(투사체 BP).

---

## 1. 무기 DataAsset 6종 (`UFPSRWeaponDataAsset`)

각 무기 = `Content/Weapons/DataTable/`에 DataAsset 생성(우클릭 > Miscellaneous > Data Asset > `FPSRWeaponDataAsset`). 공통 필드:
- **DisplayName**: UI 표시명
- **Archetype**: 아래 표
- **FireAbility**: 아래 표의 GA 클래스
- **ProjectileClass**: AOE만(아래 §2의 BP)
- **BaseStats**: 핵심값(아래). 나머지 필드(반동/블룸/ADS)는 기존 `DA_Weapon_Rifle` 복제 후 조정 권장.
- (선택) **AvailableModifiers**: 미션보상 fragment 카드(현재 MultiShot/BonusDamage만 존재 → 후속)

> ⚠️ 아래 수치는 **시작 제안값**일 뿐. PIE 튜닝 대상.

### 1-1. Burst Rifle (점사)
| 항목 | 값 | 비고 |
|---|---|---|
| Archetype | **Burst** | |
| FireAbility | `UFPSRGA_WeaponFire_Hitscan` | |
| FireMode | **Burst** | (BaseStats) |
| BurstCount | 3 | 1회 클릭당 발수 |
| Damage / FireRate | ~12 / ~12 | 점사 간 빠른 케이던스 |
| MagSize | 24 | 3의 배수 권장 |
| bHasADS | true(약) | ADSSpreadMultiplier ~0.6 |

### 1-2. Sniper (단발 스나이퍼)
| 항목 | 값 | 비고 |
|---|---|---|
| Archetype | **Sniper** | |
| FireAbility | `UFPSRGA_WeaponFire_Hitscan` | |
| FireMode | **Single** | |
| Damage | 높게(~120) | 한 방 |
| FireRate | 낮게(~1.2) | |
| **MaxPenetration** | **2~3** | 관통(1=첫 적 정지). 일렬 적 관통 |
| RecoilRecovery | **Auto** | 단발 자동복구(이미 지원) |
| bHasADS / ADSFieldOfView | true / 25~35 | 정밀 줌 |
| MagSize / ReloadTime | 5 / 2.5 | |

### 1-3. Shotgun (샷건)
| 항목 | 값 | 비고 |
|---|---|---|
| Archetype | **Shotgun** | |
| FireAbility | `UFPSRGA_WeaponFire_Hitscan` | |
| FireMode | Single (or FullAuto) | |
| **PelletCount** | **8** | 1탄약당 펠릿 수(핵심) |
| Damage(펠릿당) | ~10 | 근접 합산 큼 |
| SpreadDegrees | 높게(~8) | 산탄 |
| Range | 짧게(~3000) | |
| MagSize / FireRate | 6 / 1.5 | |

### 1-4. Bazooka (AOE 로켓)
| 항목 | 값 | 비고 |
|---|---|---|
| Archetype | **AOE** | |
| FireAbility | **`UFPSRGA_WeaponFire_Projectile`** | |
| **ProjectileClass** | **`BP_Rocket`** (§2) | 미지정 시 base(메시 없음)로 동작 |
| FireMode | Single | |
| Damage | ~80 | 폭발 데미지 |
| **AOERadius** | **>0** (~400) | 폭발 반경(0이면 단일타격) |
| ProjectileSpeed | ~3000 | 직선 로켓 |
| ProjectileGravityScale | 0 | 직선 |
| MagSize / ReloadTime | 1~4 / 2.0 | |

### 1-5. Grenade (AOE 유탄, 포물선)
| 항목 | 값 | 비고 |
|---|---|---|
| Archetype | **AOE** | |
| FireAbility | **`UFPSRGA_WeaponFire_Projectile`** | |
| ProjectileClass | **`BP_Grenade`** (§2) | |
| **ProjectileGravityScale** | **>0** (~1.0) | 포물선 낙하 |
| AOERadius | >0 (~300) | |
| ProjectileSpeed | 낮게(~1800) | 던지는 느낌 |
| Damage | ~70 | |

### 1-6. ChargeLaser (차징 관통 빔)
| 항목 | 값 | 비고 |
|---|---|---|
| Archetype | **ChargeLaser** | |
| FireAbility | **`UFPSRGA_WeaponFire_ChargeLaser`** | |
| FireMode | Single | |
| Damage | ~30 (기본) | alpha 0=기본, 1=×FullMult |
| **ChargeTime** | **~1.5** | 풀차징까지 초 |
| **ChargeFullDamageMultiplier** | **~3.0** | 풀차징 데미지 배수 |
| Range | 길게(~15000) | 관통 빔 |
| bHasADS | true | 정밀 |
| MagSize | ~10 | 1발=1탄약 |

> 동작: **누르고 있으면 충전, 떼면 발사**(충전량 비례 데미지). 벽 전까지 **모든 적 관통**. 서버권위 충전(안티치트). 빔 시각·차징 게이지는 §4(후속 코드 필요).

---

## 2. 투사체 actor BP — `BP_Rocket` / `BP_Grenade` (AOE 전용)

- **부모 클래스**: `AFPSRProjectile` (C++).
- 위치: `Content/Weapons/Projectiles/`.
- 작업:
  1. **MeshComp**(이미 있는 컴포넌트, 미할당)에 메시 지정 — 없으면 엔진 기본 `Sphere`/`Cylinder`로도 기능 OK(작게).
  2. (선택) 콜리전 구 반경 조정(기본 16cm).
  3. **DA의 `ProjectileClass`에 이 BP 지정**.
- ⚠️ **폭발/임팩트 VFX·사운드는 C++ 베이스에 훅 없음 → 후속**(현재 데미지만 적용, 시각 없음). 기능 검증엔 불필요.

---

## 3. 테스트 방법 (무기 픽업 시스템 미구현)

현재 무기는 시작 슬롯(`DefaultPrimaryWeapon`/`DefaultSecondaryWeapon`)으로만 들어간다(획득/픽업 후속).
- `Content/Character/Player/BP_FPSRPlayer` 열기 → Details의 **`DefaultPrimaryWeapon`**(또는 Secondary)에 테스트할 신규 DA 임시 지정 → PIE.
- 확인 포인트:
  - **Burst**: 1클릭=3발. **Sniper**: 단발·고데미지·일렬 적 관통·ADS 줌. **Shotgun**: 1발=8펠릿 산탄.
  - **Bazooka/Grenade**: 투사체 비행→폭발 반경 내 다수 적 데미지(유탄=포물선). (시각은 디버그/기본 메시)
  - **ChargeLaser**: 짧게 탭=약함, 길게 홀드=강함. 일렬 적 전부 관통. (빔은 `ENABLE_DRAW_DEBUG` 시안색 선)
- 멀티 검증(서버권위): 2-client PIE에서 클라가 쏠 때 서버 데미지/차징 정상.

---

## 4. 시각/오디오 (전량 신규 — 일부는 후속 코드 필요)

현재 프로젝트에 **메시/VFX/Niagara/머티리얼/사운드 없음**(HoldZone 데칼 1개 제외). 기능과 분리된 폴리시 단계.
- **순수 콘텐츠로 가능**: 무기 1P 메시, 투사체 메시, (BP에 직접 붙이는) 트레일.
- **후속 코드 배선 필요(콘텐츠만으론 불가)**:
  - 투사체 **임팩트/폭발 VFX·사운드** 훅(`AFPSRProjectile::HandleImpact`에 콜백 추가).
  - ChargeLaser **빔 Niagara**(현재 디버그선만) + **차징 게이지 HUD**(alpha를 위젯에 노출하는 게터/이벤트 배선).
  - → 이 둘은 별도 후속 유닛(코드)로 처리 후 콘텐츠 연결.

---

## 5. 기타 보류 콘텐츠 (별도)

- **미션 5종** (`phase/p4b3-missions`): StandStill/MovingZone/CollectOrbs/CarryNoHit/DefeatFleeing — BP+DA 작성 + `DA_RunSchedule.MissionEvents[]` 등록 + PIE. (LimitedVision=비주얼 설계 미결, 코드 미착수.) 트리거 `FPSR.MissionTrigger [0-4]`.
- **임시 테스트값 원복**(메모리 `p4a-temp-test-values`): `Content/Game/Data/DA_RunSchedule`의 미션 60/120/180s→5/10/15분, 보스 300s→20분. **프로덕션 전환 시**.
- **스폰포인트 PIE**: `L_Sandbox`에 `BP_EnemySpawnPoint` 배치·검증(코드 main).
- **적/HUD 메시·스타일**: `BP_EnemyBase`=엔진 큐브 플레이스홀더, WBP HUD=레이아웃 플레이스홀더(이미 존재).

---

## 6. 검증/머지 체크리스트

- [ ] `phase/p4c-hitscan` Codex `-Base main` → 통과 → `--no-ff` main 머지
- [ ] `phase/p4c-aoe-charge` Codex `-Base main` → PROGRESS/`FFPSRWeaponStatBlock` reconcile → `--no-ff` main 머지
- [ ] 6종 무기 DA + `BP_Rocket`/`BP_Grenade` 작성
- [ ] `BP_FPSRPlayer` Default 슬롯으로 각 무기 PIE 검증(위 §3 포인트)
- [ ] (선택) 콘텐츠 동반 커밋 → main
