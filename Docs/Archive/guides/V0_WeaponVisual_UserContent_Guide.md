# V0 무기 비주얼 — 사용자 콘텐츠 배선 가이드 (Infima 팩)

> **C++ 코드는 완료·검증됨**(브랜치 `phase/p6-weapon-visuals`, 빌드+스모크 통과). 이 문서는 에디터에서 사용자가 채울 **콘텐츠(메시·사운드·머즐플래시·AnimBP)** 배선 안내다. VibeUE MCP 미연결로 자동 배선 대신 수동 가이드로 작성(2026-06-13).
> 신규 DA 필드(전부 기본 null=무회귀)는 `UFPSRWeaponDataAsset` `Weapon|Visual`/`Weapon|Audio` 카테고리에 노출된다(재빌드된 모듈 로드 후).

---

## 1. 신규 DA 필드 (무기 DA마다)

| 필드 | 타입 | 용도 |
|---|---|---|
| `WeaponMesh1P` | SkeletalMesh(소프트) | 1P 무기 스켈레탈 메시(총기). 설정 시 우선. |
| `WeaponMeshStatic1P` | StaticMesh(소프트) | 1P 무기 스태틱 메시(칼). `WeaponMesh1P`가 비었을 때만 사용. |
| `ArmsAnimInstanceClass` | AnimInstance 클래스(소프트) | 무기별 팔 AnimBP override(옵션). 장착 시 `FirstPersonArms`에 적용. |
| `WeaponAttachSocket` | FName | 팔 메시상의 무기 부착 소켓(비우면 컴포넌트 루트). |
| `MuzzleSocket` | FName | **무기 메시**상의 머즐 소켓(플래시 원점, 코스메틱 전용). 비우면 무기 메시 원점. |
| `EquipMontage` | AnimMontage(소프트) | 장착 시 팔에 재생(옵션). |
| `FireMontage` | AnimMontage(소프트) | 발사마다 팔에 재생(옵션). |
| `MuzzleFlash` | ParticleSystem(Cascade, 소프트) | 발사마다 머즐 소켓에 스폰. |
| `FireSound` | SoundBase(소프트) | 발사마다 재생(로컬 1P). |

> **게임플레이 무관**: 트레이스/탄도/데미지는 카메라 뷰포인트 기준 유지. 위 필드는 전부 시각/청각 코스메틱이며 `BaseStats`에 영향 없음.

---

## 2. 팔 메시 (BP_FPSRPlayer) — 1회만

`Content/Character/Player/BP_FPSRPlayer` 열기 → 컴포넌트 `FirstPersonArms`(USkeletalMeshComponent) 선택:
1. **Skeletal Mesh Asset** = `/Game/Assets/LowPolyAnimatedModernGuns/Art/Characters/Meshes/SK_LPAMG_Arms_Gloves`
   - (장갑 없는 버전 원하면 `SK_LPAMG_Arms_Base`)
2. **Anim Class** = 팔 AnimBP(아래 §4에서 생성). 없으면 일단 비움(메시만 T-포즈로 보임).
- `WeaponMesh1P`/`WeaponMeshStatic1P` 컴포넌트는 **C++가 자동 생성·관리**하므로 BP에서 건드리지 말 것(장착 시 메시가 코드로 주입됨).

---

## 3. 무기 DA 8종 배선 (`Content/Weapons/DataTable/`)

> **모듈러 무기 결정(2026-06-14)**: Infima 무기는 모듈러라 `SK_LPAMG_<W>`(스켈레탈)는 **리시버 본체만**이고 배럴/포어그립/사이트/탄창이 별도 부품이다. V0는 **부품이 합쳐진 통합 Preview 스태틱 메시(`SM_LPAMG_<W>_Preview`)를 `WeaponMeshStatic1P`에 사용**한다(옵션 A) — 무기 1덩어리·완전한 모습·코드 0. 무기 자체 미세 애님(볼트 등)은 없지만 팔 애님으로 충분. 모듈러 SK+부품 부착 시스템은 부착물 교체 게임플레이가 생길 때(범위 밖) 도입. **그래서 8종 모두 `WeaponMeshStatic1P` 필드를 쓰고 `WeaponMesh1P`(스켈)는 비운다.**

각 DA 열어 아래 값 입력. 경로 접두사 공통: `/Game/Assets/LowPolyAnimatedModernGuns/`. **메시는 전부 `WeaponMeshStatic1P` 필드에 넣고 `WeaponMesh1P`는 비울 것.**

| DA | 팩 무기 | `WeaponMeshStatic1P` (`…/Art/Weapons/<W>/Meshes/`) | `FireSound` (`…/Audio/Cues/Weapons/<W>/`) | `MuzzleFlash` (`…/Art/Effects/Particles/`) |
|---|---|---|---|---|
| `DA_Weapon_Rifle` | MAK12 | `SM_LPAMG_MAK12_Preview` | `SC_LPAMG_WEP_MAK12_Fire` | `PS_LPAMG_Muzzle_Flash` |
| `DA_Weapon_BurstRifle` | AG14W | `SM_LPAMG_AG14W_Preview` | `SC_LPAMG_WEP_AG14W_Fire` | `PS_LPAMG_Muzzle_Flash` |
| `DA_Weapon_Shotgun` | SP60 | `SM_LPAMG_SP60_Preview` | `SC_LPAMG_WEP_SP60_Fire` | `PS_LPAMG_Muzzle_Flash_Shotgun` |
| `DA_Weapon_Sniper` | LRAF9 | `SM_LPAMG_LRAF9_Preview` | `SC_LPAMG_WEP_LRAF9_Fire` | `PS_LPAMG_Muzzle_Flash` |
| `DA_Weapon_Bazooka` | X13 | `SM_LPAMG_X13_Preview` | `SC_LPAMG_WEP_X13_Fire` | `PS_LPAMG_Muzzle_Rocket` |
| `DA_Weapon_Grenade` | HVG7 | `SM_LPAMG_HVG7_Preview` | `SC_LPAMG_WEP_HVG7_Fire` | `PS_LPAMG_Muzzle_Rocket` |
| `DA_Weapon_ChargeLaser` | RC425 | `SM_LPAMG_RC425_Preview` | `SC_LPAMG_WEP_RC425_Fire` | *(비움 — 빔 VFX는 U13)* |
| `DA_Weapon_Knife` | _Melee | `…/Art/Weapons/_Melee/SM_LPAMG_Knife` | `…/Knife/SC_LPAMG_WEP_Knife_Attack` | *(없음)* |

> Preview 메시 이름이 다르면(`_Preview` 접미사 확인) 각 무기 `Meshes/` 폴더에서 통합본을 고른다.
> 팩 총기는 8정, 총기 DA는 7종 → **MR22 미사용**(향후 무기 추가 시 사용).
> 참고: `WeaponMesh1P`(스켈레탈) 필드/컴포넌트는 코드에 남겨둠 — 향후 무기 자체 애니메이션이 필요하면 SK 본체+모듈러 부품으로 전환할 때 사용.

### `MuzzleSocket` (각 무기 메시)
Preview 스태틱 메시에 머즐 소켓이 있으면 그 이름을 `MuzzleSocket`에 입력. **모르면 비워둬도 됨** — 무기 메시 원점에서 플래시가 나옴(대략 총 위치). PIE로 보고 어긋나면 메시에 소켓을 추가하거나 정확한 소켓명 입력. (머즐/사운드는 코드가 **활성 무기 메시 = `WeaponMeshStatic1P`**에 자동 부착)

### `WeaponAttachSocket` (팔 메시)
**비워두세요** → 코드가 캐릭터 기본값 `SOCKET_Weapon`(팔 메시 소켓)에 부착합니다. 무기별로 다른 소켓이 필요할 때만 입력. 그립 정렬은 BP에서 `WeaponMeshStatic1P` 컴포넌트의 상대 트랜스폼 + 팔 AnimBP(§4)로 다듬는다.

---

## 4. 팔 애니메이션 (선택 — 단계적)

팩은 **AnimSequence만** 제공(`…/Art/Characters/Animations/<W>/Actions/A_FP_PCH_<W>_Fire` 등), AnimBP·Montage는 **없음**. 그래서 애니메이션은 별도 콘텐츠 작업이다.

- **V0 최소(권장 1차)**: §2~§3만 → **무기 메시가 손에 보이고, 발사 시 머즐 플래시+사운드** 재생(애님 없이 정적 포즈). 코어 비주얼 확보.
- **애니메이션 추가(2차/폴리시)**:
  - 팔 AnimBP 1개 생성(스켈레톤 `SKEL_LPAMG_Character`) → idle/fire 상태. 무기별로 다르면 무기 DA `ArmsAnimInstanceClass`에 무기별 AnimBP 지정(장착 시 자동 적용).
  - `FireMontage`/`EquipMontage`: `A_FP_PCH_<W>_Fire`/`_Unholster`에서 **몽타주 생성** 후 DA에 지정(코드가 발사/장착 시 팔에 재생).
  - 무기 자체 애님(볼트 등)은 무기 메시의 AnimBP/몽타주 — V0 범위 밖(폴리시).

> 애니메이션은 손맛 게이트(U1) 전 폴리시로 충분. 1차는 메시+머즐+사운드로 "총이 보이고 쏘면 반응" 확보가 목표.

---

## 5. PIE 검증 체크리스트

1. 무기 8종 슬롯 교체(1/2/3 + 카드로 추가) 시 **해당 무기 메시가 손에 표시**(칼=스태틱).
2. 발사 시 **머즐 플래시 + 발사 사운드**(ChargeLaser는 플래시 없음, 칼은 사운드만).
3. **무회귀 확인**: 반동/탄약/데미지/연사·점사·차징 동작이 비주얼 적용 전과 동일.
4. 빈 탄창/쿨다운 중에는 플래시·사운드 미재생(발사 안 될 때 코스메틱 없음).
5. (애님 적용 시) 발사/장착 몽타주 재생.

검증 후 알려주시면 머지 전 Codex 리뷰(`codex-review.ps1 -Base main`) → PROGRESS 갱신 → `--no-ff` 머지를 진행합니다.

---

## 6. 범위 밖(후속 기록)
- **3P 무기 메시**(타 플레이어 시점): 같은 DA 필드 재사용하는 후속(U11 전 권장).
- **멀티 코스메틱**(다른 클라에서 보이는 발사 이펙트): U13(GMS 이후).
- **ChargeLaser 빔 VFX / 차징 게이지**: U13/U12.
- **Demo/Mannequin 폴더**: 사용 금지(데모용, 디스크 보존만).
