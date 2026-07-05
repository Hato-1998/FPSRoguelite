# 무기 팩 통합 규약 — LowPolyAnimatedModernGuns

> [AssetIntegration_Protocol.md](AssetIntegration_Protocol.md)의 **무기 구체판**. 이 팩(`Content/Assets/LowPolyAnimatedModernGuns`)에서 무기를 저작할 때 이 규약에 맞춰 기계적으로 작업한다. 실측 근거: 2026-07-04 Rifle=AG14W 저작 세션.

## 스켈레톤 · 메시
- **팔 스켈**: `SKEL_LPAMG_Character` (전 무기 공유, UE 마네킹형 92본). FirstPersonArms 메시 = `SK_LPAMG_Arms_Gloves`. (Base/Gloves × _Smooth 4종)
- **무기 스켈**: `SKEL_LPAMG_<CODE>` (무기별). 무기 메시 = `SK_LPAMG_<CODE>`.
- 무기 메시 머티리얼 슬롯 = Body/Text/Grip/Details/Rail 등 — **배럴/탄창/포어스톡/조준경은 별도 모듈 static mesh 파트**(리시버에 스킨 안 됨).

## 애니 additive 규약 ⚠️
- 팔 `Actions/`·`Movement/` 원본(`A_FP_PCH_*`)은 **additive**(`AAT_LOCAL_SPACE_BASE`). **슬롯 몽타주·BlendSpace엔 `_NoAdditive` 변형**(풀포즈)을 소스로 써야 함.
- 무기 `A_FP_WEP_*`는 이미 풀포즈(`AAT_NONE`).
- 저작 파이프라인 상세 = 메모리 [[anim-content-authoring-vibeue-pipeline]].

## ⚠️ BlendSpace 파이썬 저작 함정 (중요)
파이썬(`BlendSpaceFactory1D`+`sample_data` set)으로 만든 BlendSpace는 **런타임 보간 데이터가 안 빌드돼 무효 포즈(NaN)를 낸다** → 본 NaN → 메시 바운드 NaN → **1P 팔/무기가 컬링되어 사라짐**(장착 몽타주 끝나고 베이스=블렌드스페이스로 복귀하는 순간). 에디터에서 컴파일하면 in-session만 정상(비영속).
- **수정**: ①완전 로드된 에셋에서 `sample_data` 재설정 + `AssetEditorSubsystem.open_editor_for_assets([bs])`로 강제 리빌드 + 저장 + **참조 AnimBP 재컴파일+저장**, 또는 ②**에디터에서 직접 생성**(권장 — 이 버그 없음, AnimBP는 경로 참조라 그대로 작동).
- **검증**: 저작 후 반드시 **COLD PIE**(수동 재컴파일 없이)로 팔/무기가 애니 이후에도 유지되는지 확인.

## 소켓 규약 (attach 포인트 — 다수가 `SOCKET_*` 이름의 **본**; 런타임 attach는 본/소켓 무관)
- **팔**: `SOCKET_Weapon`(무기 부착), `SOCKET_Weapon_L`, `SOCKET_Knife`, `hand_l/r`.
- **무기 리시버**: `SOCKET_Forestock` 또는 `SOCKET_Barrel`(전방 파트), `SOCKET_Magazine` + `SOCKET_Magazine_Reserve`(2탄창 리로드), `SOCKET_Scope`, `SOCKET_Default`, `SOCKET_Eject`, `Grip`.
- **전방 파트(배럴/포어스톡) 메시 자체**: `SOCKET_Muzzle`, `SOCKET_Ironsight_F`, `SOCKET_Laser`, `SOCKET_Grip`.
- **사이트 파트(아이언사이트/조준경) 메시 자체**: `SOCKET_Aim`(우리 저작 — 절차적 ADS 조준선, 아래 참조). 팩 `SOCKET_Ironsight_F`는 회전이 우리 +X전방 규약과 불일치 → 재사용 말고 `SOCKET_Aim` 별도 저작.

## 총구 = **파트 소켓** (핵심)
- 총구는 **배럴(MAK12)/포어스톡(AG14W) 파트 메시**의 `SOCKET_Muzzle`에 있다(리시버 아님). 파트 변형(Short/Extended)마다 위치가 맞게 authored → **파트를 바꾸면 총구가 따라감**.
- **코드**(`AFPSRCharacter`): `RefreshWeaponPartComponents`가 `MuzzleSocket`을 가진 **파트 컴포넌트**를 `CachedMuzzleComponent`로 해석, 없으면 리시버 폴백. `PlayWeaponFireCosmetics`/`MulticastFireCosmetics`가 거기 플래시 부착. **관례 기반**(DA 필드 추가 0 — `MuzzleSocket` 이름이 곧 조회 키).
- **DA**: `MuzzleSocket = "SOCKET_Muzzle"`(언더스코어! 공백 금지), `WeaponMesh1P`엔 이 소켓 넣지 말 것(파트가 정본).
- **플래시 방향**: 파트 메시의 `SOCKET_Muzzle` **회전**이 결정. 팩은 identity로 심어둠 → 우리 플래시(`PS_LPAMG_Muzzle_Flash`)가 소켓 +X로 분사하는데 배럴은 +Y라, **yaw=90**으로 소켓 회전 보정(소켓 +X→배럴 +Y). Socket Manager에서 조정(파트 변형마다 동일 값 복제). ⚠️ `unreal.Rotator(pitch,yaw,roll)` positional 순서 주의 — yaw는 keyword로.

## ADS 조준 소켓 = **사이트 파트 소켓** (핵심, 총구와 동형)
- 절차적 ADS는 무기 `AimSocket`(관례상 `SOCKET_Aim`)의 **변환(위치+회전)**을 카메라 전방중앙선에 정렬한다(고정 카메라라 사이트를 카메라로 가져옴). 조준선은 물리적으로 **사이트 파트(아이언사이트/조준경)** 위에 있으므로 `SOCKET_Aim`을 **사이트 파트 메시**에 심는다(리시버 아님) → **사이트 파트를 바꾸면 조준선이 따라감**(총구가 배럴 파트 따라가는 것과 동일 관례).
- **코드**(`AFPSRCharacter`): `RefreshWeaponPartComponents`가 `SOCKET_Aim`을 가진 **파트 컴포넌트**를 `CachedAimComponent`로 해석(WeaponParts1P 순서상 첫 매치), 없으면 리시버(`WeaponMesh1P`) 폴백. `UpdateAimDownSights`가 거기서 조준선을 읽음. **관례 기반**(DA 필드 추가 0 — `AimSocket` 이름이 곧 조회 키).
- **소켓 회전 규약**: `SOCKET_Aim`은 **+X=사이트 전방(총구 방향), +Z=위**로 authored. DA `bADSAlignRotation=true`(기본)면 이 프레임을 카메라에 완전 정렬(hip 기울기 제거·사이트 수평). 회전이 애매하면 `bADSAlignRotation=false`로 translation-only 폴백.
- **DA**: `AimSocket="SOCKET_Aim"`, `bHasADS=true`, `ADSSightDistance`(눈~사이트 거리, 40~70 권장). **IsDataValid**가 리시버+전 파트에서 `SOCKET_Aim` 존재를 검증(오타 시 경고). 여러 사이트 파트를 넣으면 목록 첫 매치가 조준선(총구와 동일 순서 규칙).

## 재장전 = **2탄창 스왑** (핵심)
- 리로드 애니(`A_FP_WEP_<CODE>_Reload`)가 **두 탄창 본**을 구동:
  - `SOCKET_Magazine`: 매그웰 → 아래로 빠져 튕겨나감(빠지는 탄창)
  - `SOCKET_Magazine_Reserve`: 손/옆 → 매그웰로 상승·삽입(**새 탄창**)
- → **두 소켓 모두에 탄창 메시를 부착**해야 새 탄창이 보인다. 하나만 붙이면 삽입 탄창이 "투명/안 보임".
- ⚠️ idle(ref 포즈)에서 `SOCKET_Magazine_Reserve`는 매그웰 **아래**(z≈−30)에 위치 → 예비 탄창이 총 아래로 보일 수 있음. 거슬리면 **무기 AnimBP에 ModifyBone**으로 idle 때 예비 본을 매그웰에 겹쳐 숨기고(재장전 몽타주가 재생 중엔 override) 처리.

## 부품 → 소켓 매핑 (AG14W 예)
| 파트 | 소켓 | 비고 |
|---|---|---|
| Forestock_Default | `SOCKET_Forestock` | 총구 소스(SOCKET_Muzzle 보유) |
| Magazine_Default | `SOCKET_Magazine` | 빠지는 탄창 |
| Magazine_Default | `SOCKET_Magazine_Reserve` | 삽입 탄창 |
| Ironsight_F | `SOCKET_Scope` | 팩 의도는 포어스톡의 `SOCKET_Ironsight_F` — 단, 현 파트 시스템은 파트를 **리시버**에만 부착(파트-위-파트 중첩 미지원) → 후속. **이 사이트 파트 메시에 `SOCKET_Aim`(+X전방·+Z위) 저작 → ADS 조준선**(위 ADS 조준 소켓 참조) |

## 무기 ↔ 팩 코드 매핑
전체 표 = 메모리 [[weapon-da-pack-code-mapping]]. 요약: **Rifle=AG14W**(사용자 결정), MAK12=샷건용 보류, Sniper/ChargeLaser=MR22, LMG=HVG7, Bazooka=LRAF9, Grenade=RC425, Shotgun=SP60(프리뷰). ⚠️ **새 무기 저작 전 이 표로 팩 코드 확인**, 애매하면 사용자 확인.

## 무기별 저작 체크리스트 (1종당)
1. 팩 코드 확인(위 매핑) → 스켈/메시/애니 존재 확인
2. 몽타주(팔 Fire/Reload/Equip=NoAdditive, 무기 Fire/Reload) + BlendSpace(Idle/Walk/Sprint NoAdditive) + 팔 AnimBP + 무기 AnimBP
3. DA 배선: WeaponMesh1P·두 AnimBP·5몽타주·`MuzzleSocket="SOCKET_Muzzle"`·부품(포어/배럴 + **2탄창** + 조준경)
4. 총구: 파트에 `SOCKET_Muzzle` 있는지 확인 → 없으면 추가, **회전 보정**(yaw 등)
5. **IsDataValid 확인**(소켓 존재 자동 검증 통과) + **조립 렌더로 총구·부품·탄창 육안 검증**(오프라인, 삭제)
6. 사용자 PIE(발사 팔↔노리쇠·재장전 2탄창·총구 방향·로코모션)

## 에셋 커버리지 갭 (몽타주 비우면 null-safe 폴백)
| 무기 | 코드 | 발사 팔 | 재장전 | Reload_Empty |
|---|---|---|---|---|
| Bazooka | X13 | ✓ | 없음 | 없음 |
| Grenade | HVG7 | ✓ | 없음 | 없음 |
| Sniper | LRAF9 | ✓ | ✓ | 없음 |
| ChargeLaser | RC425 | 없음 | 없음 | 없음 |
| Melee | — | Attack만 | — | — |
(AG14W/MAK12/SP60/MR22 = 풀 커버)
