# 라이플 하드서피스 — 새 세션 실행 프롬프트 (절차적 생성 → 임포트 → 세팅 전 구간)

> 작성 2026-09-03, 개정 2026-09-04. **새 세션이 그대로 복붙해 착수**하는 실행 프롬프트다.
> 선행 = 아케이드 룩 프로토(브랜치 `proto/arcade-look`) + 포스트프로세싱 1패스(`401ae6f8`).
> 🔁 **이 문서는 `RifleVoxel_ResumePrompt.md` 의 후속이다.** 복셀 트랙은 사용자 판정(2026-09-03,
> *"너무 블럭 형태"*)으로 폐기됐고, **무기는 절차적 하드서피스**로 간다. 복셀에서 살아남은 결정과
> 죽은 결정을 §2 에 갈라 적었다 — 낡은 결정을 다시 집지 않게.

**복붙용 첫 지시문**
```
Docs/RifleHardSurface_ResumePrompt.md 를 읽고 진행한다.
브랜치 proto/arcade-look. 보드 클레임부터.
§2 결정은 전부 끝났다(하드서피스 · +X · 머티리얼 슬롯 5종 · 파츠 8분리 · 파일럿).
생성(§3-1)·임포트(§3-3)는 끝났다. 남은 것 = §3-2 C++ 가드(플랜 게이트) → §3-4 소켓 → §3-5 머티리얼 → §3-6 DA.
```

**진행 상태 (2026-09-04 기준)**

| 단계 | 상태 |
|---|---|
| §3-1 절차적 생성 | ✅ `4fc47fd7` — 파츠 8종 · 삼각 4,604 |
| §3-3 임포트 | ✅ `11e802aa` (3차) — 8/8 · `slots_ok=True` · **forward=+Y 게이트 pass** · `/Game/Assets/Weapons/RifleHS/`. 1차 `96fcdab8` 는 원점·축이, 2차 `5bd0b4d4` 는 Y 부호가 틀렸었다(D12) |
| §3-2 C++ 가드 확장 | ⏳ **플랜 게이트 대기** — 이게 없으면 파츠 7개가 안 붙는다 |
| §3-4 소켓 | ✅ `ffa6d5ca` — 몸통 8 · 총열 Muzzle · 조준경 2종 Aim, `sockets_ok=True` |
| §3-5 머티리얼 · §3-6 DA · §3-7 PIE | ⏳ |

---

## §0 세션 시작 방법

1. **보드 클레임 먼저**(하드 게이트) — `/board 클레임 라이플 하드서피스`.
   대상 행 = 「아케이드 룩 프로토 + 육안 게이트」(`3d03972d-dd88-81ff-87d6-f5730a06e7a4`)의 **2번째 스코프**
   (행 이름은 아직 "라이플 복셀"일 수 있다 — 같은 스코프다). 그 행은 **진행중**이다.
2. 브랜치 확인: `git branch --show-current`. ⚠️ **이 워크트리를 여러 세션이 공유한다** —
   2026-09-03 에 실제로 다른 세션이 `68da5543`·`fdfd4ade` 를 같은 브랜치에 커밋했다. 커밋 전 반드시 재확인.
3. 🔴 **에디터 상태가 단계마다 갈린다** — §3 각 단계에 "에디터 켜짐/꺼짐"을 명시했다. 임포트는 **에디터가 꺼져 있어야** 한다(§5-1).
4. 🔴 **런타임 검증(PIE)은 사용자가 실행한다.** Claude 는 게임을 직접 켜지 않는다.
5. **모델링은 Claude 가 한다** — `Scripts/gen_rifle_hardsurface.py` 가 순수 파이썬으로 OBJ 를 직접 쓴다
   (선례 `gen_enemy_proto_meshes.py`). Blockbench 는 이 트랙에서 **쓰지 않는다**(복셀 시절 잔재).
   형태 수정 = 파츠 함수 한 줄, 전역 파라미터 = 상단 3줄(`BEVEL`·`TUBE_SIDES`·`VENTS`).

---

## §1 지금 상태 — 무엇을 무엇으로 바꾸는가

### 1-1. 현행 라이플 (실측 2026-09-03)
`/Game/Weapons/DataTable/DA_Weapon_Rifle` (`UFPSRWeaponDataAsset`)

| 필드 | 현재 값 |
|---|---|
| `WeaponMesh` (스켈레탈) | `/Game/PolygonMilitary/Meshes/Weapons/Modular/Weapon_A/SK_Wep_Mod_A_Body_01` |
| `WeaponMeshStatic` | `None` |
| `WeaponAttachSocket` | `SOCKET_Weapon` (**바디 스켈레톤** 쪽) |
| `AimSocket` / `MuzzleSocket` | `SOCKET_Aim` / `SOCKET_Muzzle` — 🔴 **몸통에 없다. 파트(조준경/총열)에 있다**(§3-4) |
| `LeftHandSocket` / `RightHandSocket` | `SOCKET_LeftHand` / `SOCKET_RightHand` (몸통 메시에 실재 — 실측 확인) |
| `ADSAimRotationOffset` | **Yaw 90** — 이 팩의 총구 방향이 **+Y** 이기 때문 |
| `WeaponAnimInstanceClass` | **None** ← 중요, 아래 |
| `weapon_parts` | 7개(Synty 파츠, `SOCKET_Mount_*_0` 에 부착) |

### 1-2. 정적 메시로 갈 때 잃는 것 / 잃지 않는 것
`FPSRCharacter.cpp:2305-2350` 이 **두 경로를 모두 지원**한다 — 스켈레탈이 우선이고, 없으면
`WeaponMeshStatic` 으로 떨어진다(현재 나이프가 이 경로를 쓴다). `ActiveWeaponMesh` 가 실제 표시 중인
쪽을 추적해 **발사 코스메틱이 거기 붙는다.** 정적 메시 전용 컴포넌트도 이미 바디 소켓에 붙어 있고
가시성·`CustomDepth` 처리까지 받는다(`:120-123`, `:841`, `:1075`).

스켈레탈에만 있는 것이 **둘** 있다:

1. **무기 자체 AnimBP**(노리쇠·장전손잡이 몽타주, `:2317-2333`) — 🟢 **잃는 게 없다.**
   라이플의 `WeaponAnimInstanceClass` 가 지금 `None` 이라 **현재 쓰고 있지 않다.**
2. 🔴 **모듈러 파츠 시스템** — `:2526-2534` 의 가드가 **명시적으로 스켈레탈만 허용**한다:
   ```cpp
   // Parts attach to the SKELETAL weapon mesh only —
   // static/melee/preview weapons carry no modular parts
   if (!Weapon || !WeaponMesh || ActiveWeaponMesh != WeaponMesh)
   {
       RebuildPartsFromSelection(TArray<FFPSRWeaponPartAttachment>());  // 전부 제거
       return;
   }
   ```
   무기를 `WeaponMeshStatic` 으로 두면 **파츠가 조용히 0개**가 되어 진화/모듈러 연출이 통째로 죽는다.

→ **2번 때문에 코드 변경이 필요하다.** 사용자 결정(2026-09-03) = **가드를 확장**한다(§2-4).
파츠는 이미 `UStaticMeshComponent` 로 생성되므로(`:2600`) 스켈레탈이어야 하는 것은 **부모뿐**이다.

### 1-3. 무기를 건드리는 다른 시스템 (배선 후 전부 재확인 대상)
| 시스템 | 무엇을 통해 붙나 |
|---|---|
| 반동 = CrystalRecoil 어댑터 | 카메라. 메시 무관 |
| PWAS(절차적 무기 애니) | `Content/ProceduralWeaponAnimationSystem/.../DA_WPP_Rifle*` — **무기 전체**를 흔든다 |
| 크로스헤어(절차적 SDF) | spread 값. 메시 무관 |
| 총구 화염 | `MuzzleSocket` ← **총열 파트**에 있어야 한다(몸통 아님, §3-4) |
| 좌/우손 IK | `LeftHandSocket`/`RightHandSocket` ← **몸통**에 |
| ADS 정렬 | `AimSocket` + `ADSAimRotationOffset` ← §2-2 |
| HUD 무기 아이콘 | DA 의 `WeaponIcon` 소프트 참조(별도 2D 에셋) |
| 모듈러 파츠/진화 | `TSoftObjectPtr<UStaticMesh> Part` + 고정 `Socket` — **파츠는 원래 정적 메시다** |

### 1-4. 🔴 이미 죽은 결정 — 메모리를 그대로 믿지 말 것
- 메모리 `weapons-switching-to-synty-planned` 의 *"총기 = Synty Military 백본 **확정·락**"* → **폐기됨**
  (`Docs/SSOT/PlayerFeel.md:95`, 2026-09-03 아케이드 전환). Synty 메시가 DA 에 물려 있는 건 전환 전 잔재다.
- **"라이플 복셀 2.5cm"** (이 문서의 전신 `RifleVoxel_ResumePrompt.md`, 커밋 `a638d40a`) → **폐기됨**
  (2026-09-03 사용자 판정 *"너무 블럭"*, `4fc47fd7`). 복셀 스크립트·에셋은 전부 삭제됐다.
  `WorkLog.md`·보드 로그에 "복셀"이 남아 있어도 그건 경위지 현행이 아니다.
(같은 형태의 사고 = `WorkLog.md` U22a 절 — 낡은 메모리를 근거로 두 번 틀린 조언을 했다.)

---

## §2 결정 — 복셀에서 살아남은 것 / 죽은 것 / 새로 생긴 것

> **2026-09-04 현재 전부 확정. 다시 논의하지 말고, 뒤집을 이유가 생기면 근거부터 반박할 것.**
> ✅ 2-1 형태 언어 = **절차적 하드서피스**(모따기 0.3cm · 튜브 12각 · 슬랫) — 복셀 2.5cm 를 **대체**
> ✅ 2-2 정면 축 = **+X** · ✅ 2-3 색 = **머티리얼 슬롯 5종, 텍스처 0장** · ✅ 2-4 = **파츠 8분리 + C++ 가드 확장**
> ✅ 2-5 = **파일럿(라이플 1종)**

### 2-1. 형태 언어 — ✅ **결정됨: 절차적 하드서피스** (사용자, 2026-09-03) — 복셀 폐기
사용자가 2.5cm 복셀 조립도를 보고 *"너무 블럭 형태"* 로 판정했다. 레퍼런스(청색 리시버·검정 총몸의
스타일라이즈드 소총 2종)는 애초에 **매끈한 하드서피스**였다 — 복셀은 레퍼런스의 *구성*만 빌린 재해석이었고,
그 재해석이 기각된 것이다.

**하드서피스의 자동 생성 가능성**은 복셀보다 낮지만 충분하다. 핵심은 **CSG(불리언)를 쓰지 않는 것** —
전부 *더하는* 프리미티브로만 조립하면 파라미터를 어떻게 흔들어도 메시가 깨지지 않는다:
- **모따기 프리즘**(44삼각) — 어느 축으로든, 양 끝 단면이 달라도 됨(테이퍼·기울기 공짜)
- **N각 튜브** — 림도 모따기해 자른 단면이 안 보임
- **슬랫 배열** — 통기구·피카티니 레일·리코일패드 홈·소음기 방열링 전부 "구멍"이 아니라 "사이 간격"

**치수 기준선은 복셀 때 실측한 값 그대로다**(2026-09-03, Synty 라이플 조립 AABB) — 손 IK·조준 정렬이
여기 걸려 있어 형태 언어가 바뀌어도 유지한다:

| | cm |
|---|---|
| **전장** | **94.9** → 생성 결과 95.6 |
| 높이 | 33.4 → 34.4 |
| 폭 | 9.3 → 10.3 |

파츠별 길이(비율 기준선): 총열 50.2 · 핸드가드 31.1 · 개머리판 24.0 · 그립 11.7 · 조준경 10.9 · 탄창 10.4 · 트리거 3.2.

### 2-2. 총구가 향하는 축 — ✅ **저작 = +X, 내보내기 = 엔진 프레임 +Y** (정정 2026-09-04)
🔁 **정정.** 종전 문서는 *"+X 정면으로 생성하므로 `ADSAimRotationOffset` 을 0 으로 되돌린다"* 고 적었다.
**틀렸다.** 이 프로젝트의 무기 프레임은 Synty 관례 = **+Y 정면**이고(DA 주석 *"this pack's weapon-forward
is +Y"* · `SK_Wep_Mod_A_Body_01` 바운드 장축 Y 42.5 · 캐릭터 `SOCKET_Weapon`(hand_r, P/Y/R 0/75/−17)이
그 관례로 튜닝됨 — 2026-09-04 실측). +X 메시를 그대로 붙이면 **총이 90° 옆을 본다.**

→ **저작 공간은 +X 유지**(사람이 읽기 쉽다), **생성기가 내보낼 때 +90° yaw 를 건다**:
`author(x,y,z) → engine(−y, x, z)` (det +1, 미러 없음). OBJ·`manifest.json` 소켓 좌표 전부 엔진 프레임이다.
→ 무기 전체가 Synty 와 같은 프레임에 있으므로 **`ADSAimRotationOffset = Yaw 90` 은 그대로 둔다.**
PIE §6-2 에서 ADS 가 90° 틀리면 그때 0 으로 — 그 반대가 아니다.

🔴 **한 겹 더 (2026-09-04 실측): UE OBJ 임포터가 Y 를 부호 반전한다**(`Troubleshooting.md` **D12**).
그래서 생성기는 두 사상을 갈라 쓴다 — `to_engine()` = 엔진에 있어야 하는 좌표(**소켓·manifest**, 파이썬이 직접 찍음) /
`to_file()` = 그 Y 를 미리 뒤집은 것(**OBJ 정점**, 임포터가 되돌림) + 삼각 인덱스 반전(파일이 정상 오른손 OBJ 가 되게).
임포트 검증은 **`size` 로 하면 안 된다** — 두 번 통과시킨 뒤에야 `min/max` 로 잡았다. 기대값 = 몸통 `X −5.2..5.1 · Y −8.0..22.6 · Z −4.1..11.7`.

### 2-3. 색 — ✅ **결정됨: 머티리얼 슬롯, 텍스처 0장** (사용자, 2026-09-03) → 슬롯 **5종**으로 확장 (2026-09-04)
지금 아케이드 룩은 **텍스처가 한 장도 없다** — 바닥·벽·적 전부 절차적 머티리얼이다
(`MI_Pac_FloorGuide`·`MI_Pac_WallEdge`·`MI_EnemyProto_AtomCubes`). 무기만 텍스처를 쓰면
**혼자 다른 재질 언어**가 되고, 발광·림을 텍스처에 구워야 해서 셰이더가 내는 아케이드 효과와 어긋난다.

| 슬롯 | 쓰임 |
|---|---|
| `Body` | 유채색 본체 — 리시버·핸드가드·조준경 프레임 |
| `Grip` | 어두운 총몸 — 그립·개머리판·탄창·트리거가드 |
| `Barrel` | 금속 — 총열·소음기 |
| `Emissive` | 액센트 발광 — **소면적만**(§4-2). 리시버 앞 1점·탄창 잔탄 슬릿·2x 프레임 안쪽 띠·라이트바 |
| `Reticle` | **조준 표식** — 1x 조준점·기둥 끝 / 2x 눈금 |

`Reticle` 을 `Emissive` 에서 **갈라낸 이유**: 레퍼런스에서 조준점(빨강)과 프레임 액센트(청록)가 다른 색이다.
한 슬롯이면 UE 에서 색을 따로 못 준다.
⚠️ **빨강 조준점은 §4-1 A-3-4(적 전용 고채도 빨강)와 충돌할 수 있다.** 무기 위 0.8cm 점이라 실제 위험은
작지만, 색은 사용자가 알고 골라야 한다. 슬롯이 분리돼 있으니 액센트와 독립적으로 바꿀 수 있다.

**끝단까지 실측 확인 (2026-09-04, `96fcdab8`)**: 임포트 후 UE 섹션 수 = 슬롯 수.
`Body` 3 · `Mag` 2 · `SightRed` 2 · `Sight2x` 3 · 나머지 1. `slots_ok=True`.
→ `usemtl` 은 우리가 OBJ 를 직접 쓰므로 완전히 통제된다. Blockbench 왕복 문제는 **존재하지 않는다.**

### 2-4. 파츠 — ✅ **결정됨: 8분리 + C++ 가드 확장** (사용자, 2026-09-03)
몸통 · 개머리판 · 그립 · 탄창 · 핸드가드 · 총열(+소음기) · **조준경 1x** · **조준경 2x**.
조준경 2종은 **같은 소켓(`SOCKET_Mount_Reddot_0`)에 붙는 교체 파츠**다 — 저격 진화 카드가 2x 를 끼우는 그림.

**단, §1-2 대로 현재 코드는 정적 무기에 파츠를 안 붙인다.** 검토한 세 갈래 중 사용자가 **가드 확장**을 골랐다:
- ❌ 스켈레탈로 승격(Blender 로 최소 스켈레톤) — 코드 무수정이지만 무기마다 Blender 단계가 붙는다
- ✅ **C++ 가드 확장** — 파츠 부모를 `WeaponMesh` → `ActiveWeaponMesh` 로
- ❌ 파츠 포기

→ **작업 내용은 §3-2.** 코어 무기 코드 변경이므로 **플랜 게이트를 거친다.**

### 2-5. 파일럿인가 전량인가 — ✅ **결정됨: 파일럿** (사용자, 2026-09-03)
라이플 1종만 먼저 만들어 판정한다(U21 Synty 파일럿과 같은 방식). 나머지 8종
(`DA_Weapon_{Shotgun,SMG,LMG,Sniper,Bazooka,ChargeLaser,Knife,Unarmed}`)은 판정 통과 후 §7 로 이월.
⚠️ 파일럿 기간에는 **룩이 섞인다**(하드서피스 라이플 + Synty 나머지) — 의도된 중간 상태이지 결함이 아니다.
판정할 때 "다른 무기와 안 어울린다"를 실패 사유로 쓰지 말 것.

---

## §3 작업 순서

### 3-1. 절차적 생성 — ✅ 완료 (`4fc47fd7`)
```
python Scripts/gen_rifle_hardsurface.py    →  Saved/RifleHardSurface/
    SM_RifleHS_{Body,Barrel,Handguard,Stock,Mag,Grip,SightRed,Sight2x}.obj
    manifest.json      ← 파츠별 mount·소켓 좌표(cm) · 몸통 소켓 8개 좌표
    preview.svg        ← 1x 조립, 3/4 축측투상 + 플랫셰이딩
    preview_2x.svg     ← 같은 총에 2x 교체
```
| | 값 |
|---|---|
| 파츠 · 삼각 · 정점 | 8 · **4,604** · 2,490 (1x 조립 4,208 / 2x 교체 +396) |
| 전역 파라미터 | `BEVEL=0.3` · `TUBE_SIDES=12` · `VENTS=6` |
| 총기 부품 | 피카티니 레일(리시버 12 · 핸드가드 8) · 탄피배출구 림 · 장전손잡이+노브 · 조정간 · 탄창멈치 · 트리거가드 3변 고리 · 버퍼튜브 · 치크레스트 · 리코일패드 홈 · 가스블록 · 가늠쇠 · 멜빵고리 ×4 · 플로어플레이트 · 소음기 방열링 ×5 · 총구 캡 |
| 조준경 1x | 프레임 두 기둥이 **좌우로** 벌어져 서고(위로 갈수록 바깥 기울기) 가운데 뚫림 + **조준점 하나** + 기둥 끝 발광 |
| 조준경 2x | 같은 언어, 더 높은 기둥 + 안쪽 모서리 발광 띠 + **세로/가로 눈금** + 하부 라이트바 |

`Saved/` 는 gitignore 라 OBJ/SVG 는 커밋되지 않는다 — **소스는 스크립트**다. 다시 뽑으려면 위 한 줄.

🔴 **원점·프레임 규약 (2026-09-04 실측으로 정한 것, 어기면 붙는 자리가 틀린다)**
- **몸통 원점 = 그립 마운트 지점**(저작 공간 (30, 0, 15.5)). Synty 몸통이 그렇다(`SOCKET_Mount_Grip_0` = 본 원점).
  캐릭터 `SOCKET_Weapon` 이 그 관례로 튜닝돼 있어, 원점을 개머리판 끝에 두면 총 전체가 ~30cm 앞·15cm 아래로 밀려 붙는다
  (1차 임포트 `96fcdab8` 가 그 상태였다 — 바운드 x22~52.6 이 증거. 재임포트로 고쳤다).
- **내보내기 프레임 = 엔진 +Y 정면**(§2-2). 저작은 +X 로 보고, OBJ/manifest 만 `to_engine()` 으로 돌린다.
  `manifest.json` 의 `frame` 키가 이걸 명시한다. 미리보기 SVG 는 저작 공간 그대로다.

**형태 수정 방법**: 파츠 함수(`p_body` 등)의 `box(...)`/`prism(...)`/`tube(...)`/`slats(...)` 한 줄이 부품 하나다.
좌표는 공유 무기 공간(cm, 0 = 개머리판 뒤끝, 총구 = +X, `BORE=20` = 총열 축 높이). 미리보기로 확인하고 재임포트.
- 🪤 **와인딩은 손으로 맞추지 않는다** — 삼각마다 내부 기준점을 등지도록 자동 반전한다(`Mesh.tri`).
- 🪤 **좌우 대칭은 `sy=±1` 로 찍되 rect 가 뒤집혀도 된다** — `prism()` 이 정규화한다(안 하면 모따기가 바깥으로 나간다. 1차에 실제로 났던 버그).
- 🪤 **모따기는 가장 얇은 치수의 1/3 로 자동 제한** — 얇은 슬랫에 0.3 을 그대로 주면 면이 뒤집힌다.

### 3-2. C++ 가드 확장 — 정적 무기도 파츠를 받게 (§2-4) 🔒 **플랜 게이트 대상** — ⏳ 다음 착수
**이게 없으면 임포트한 파츠 7개(몸통 제외)가 하나도 안 붙는다.** 지금 가장 먼저 할 일.

**변경 지점 3곳** (실측 2026-09-03, `Source/FPSRoguelite/Private/Hero/FPSRCharacter.cpp` — 줄 번호는 재확인):
| 줄 | 현재 | 바꿀 것 |
|---|---|---|
| `:2529` | `if (!Weapon \|\| !WeaponMesh \|\| ActiveWeaponMesh != WeaponMesh)` | 스켈레탈 강제를 풀고 `!ActiveWeaponMesh` 로 |
| `:2573` | `if (!WeaponMesh) { return; }` | `ActiveWeaponMesh` 기준으로 |
| `:2608` | `PartComp->AttachToComponent(WeaponMesh, ...)` | 부모를 `ActiveWeaponMesh` 로 |

**이미 확인된 것 (다시 조사하지 말 것)**
- 🟢 **순서 위험 없음** — `ActiveWeaponMesh` 는 `:2348` 에서 채워지고 `RefreshWeaponPartComponents` 는
  `:2448` 에서 불린다. 가드는 항상 최신값을 읽는다. 무기 없는 경로도 `:2272`(null) → `:2302` 로 정합.
- 🟢 **파츠는 이미 `UStaticMeshComponent`** (`:2600`) — 부모만 바뀌면 된다. 정적 메시의 소켓은
  `UStaticMesh::Sockets` 로 지원되고 `AttachToComponent(..., SocketName)` 이 그대로 해석한다.
- 🟢 **ADS 조준 해석은 이미 정적 대응** — `:2472-2476` 이 *"파트 우선, 없으면 `ActiveWeaponMesh`"* 로
  이미 짜여 있다. 여기는 손댈 필요 없다.
- 🟢 **복제 영향 없음** — 파츠는 복제되지 않는다. 각 클라이언트가 복제된 무기/프래그먼트 상태
  (`WeaponInventory->GetCurrentInstance()`)에서 **로컬로 재생성**한다. 즉 **코스메틱 로컬 변경**이라
  4인 협동에서 새로 생기는 네트워크 표면이 없다. (그래도 §6 판정은 **리슨 호스트 + 원격 클라 양쪽**에서 —
  메모리 `event-halves-authority-vs-client`.)

**게이트**: 코어 무기 코드 변경이므로 CLAUDE.md 모델 배분을 따른다 —
`Opus 조사·플랜 → Fable 플랜 게이트 → 사용자 승인 → Sonnet 구현 → Opus 검증 → Fable 머지 게이트`.
조사는 위 표로 이미 끝나 있으니, 플랜은 **"스켈레탈 전용을 가정한 다른 지점이 더 없는지 훑는 것"** 이
본론이다(위 4건 외에 `CachedLeftHandComponent`/`CachedRightHandComponent`·렌더 태그 상속 경로).

**검증**: 빌드는 `-DisableUnity -ForceUnity` 로(메모리 `nonunity-build-is-67-seconds`,
`Troubleshooting.md` G13 — 그냥 `Succeeded` 는 유니티 충돌을 검증하지 못한다).
회귀 확인 = **나이프**(기존 정적 무기)가 파츠 0개인 채로 여전히 정상인지, **Synty 라이플**(스켈레탈)의
파츠가 그대로 붙는지 둘 다.

### 3-3. 임포트 (🔴 **에디터 꺼짐** — §5-1) — ✅ 완료 (3차 `11e802aa`; 1차 `96fcdab8` 원점·축 오류 → 2차 `5bd0b4d4` Y 부호 오류 → 3차 min/max 검증 통과)
```
Scripts\run_import_rifle_hardsurface.bat
```
목적지 = `/Game/Assets/Weapons/RifleHS/` (적 프로토 `/Game/Assets/Characters/EnemyProto/` 와 대칭).
판정은 **종료 코드가 아니라 `ALLDONE` 마커 + 섹션 수 + 바운드**로(§5-2). 결과 = 8/8 · `slots_ok=True` ·
크기 cm 그대로(몸통 30.6 = x22~52.6, 총열 25.6 = x70~95.6). 형태를 고치면 **같은 배치를 다시 돌린다**
(`replace_existing=True`).

🔴 **반드시 `.bat` 으로 실행할 것. 직접 명령을 치면 두 가지로 조용히 실패한다**
(둘 다 2026-09-03 실제로 밟음 → `Troubleshooting.md` **D11**):
1. **PowerShell 로 `-ExecCmds` 를 넘기면 따옴표가 벗겨진다** — 에디터는 멀쩡히 뜨고 **아무것도 안 한다.**
   로그에 `Cmd:` 줄도 `LogPython` 도 없다. "느린 것"과 구별이 안 되니 **로그로 판정할 것.**
2. **상대 경로는 엔진 바이너리 폴더 기준으로 풀린다** — `Could not load Python file
   'D:/…/Engine/Binaries/Win64/Scripts/…'`. 절대 경로로 줘야 한다.
⚠️ `.bat` 주석은 **ASCII 로**. 한글을 넣으면 cmd 가 OEM 코드페이지로 읽어 파스가 깨진다.

### 3-4. 소켓 (에디터 켜짐 또는 헤드리스) — ✅ 완료 (`ffa6d5ca`)
실측 부수 사실: `replace_existing` 재임포트가 정적 메시 소켓을 **보존**한다(3차 재임포트 뒤 12개 전부 `upd`, `add` 0).
형태를 고쳐 재임포트해도 소켓은 다시 찍지 않아도 되지만, 스크립트가 멱등이라 돌려도 해가 없다.
🔴 **소켓을 전부 몸통에 만들면 안 된다.** 현행 라이플 실측(2026-09-03)이 규약을 보여준다 —
몸통 `SK_Wep_Mod_A_Body_01` 의 소켓은 **9개뿐**이고, 그중 **`SOCKET_Aim`·`SOCKET_Muzzle` 은 없다**.
**`SOCKET_Muzzle` 은 총열 파트에, `SOCKET_Aim` 은 조준경 파트에 산다.** `Docs/WeaponPack_Integration.md` 의
규약(*"총구 = 파트 소켓"* · *"ADS 조준 = 사이트 파트 소켓"*)이고, 코드도 그 전제다(`FPSRCharacter.cpp:2472-2476`
*"파트 우선, 없으면 `ActiveWeaponMesh`"*). 총열/조준경을 바꾸면 총구·조준선이 **따라 움직여야** 하므로 구조적으로 맞다.

**좌표는 전부 `Saved/RifleHardSurface/manifest.json` 에 계산돼 있다**(파츠 로컬 cm, mount 가 원점,
**엔진 프레임 +Y 정면** — §2-2). 찍는 스크립트 = **`Scripts/author_rifle_hs_sockets.py`**(켜진 에디터에서
VibeUE `execute_python_code` 로 `exec(open(path).read())`; 멱등, `find_socket` 재조회로 검증, `sockets_ok=True`/`ALLDONE` 마커):

| 메시 | 소켓 | 비고 |
|---|---|---|
| `SM_RifleHS_Body` | `SOCKET_Mount_{Stock,Grip,Mag,Handguard,Barrel,Reddot}_0` · `SOCKET_LeftHand` · `SOCKET_RightHand` | `manifest.body_sockets` |
| `SM_RifleHS_Barrel` | `SOCKET_Muzzle` | `parts.Barrel.sockets` |
| `SM_RifleHS_SightRed` | `SOCKET_Aim` = 조준점 위치 | `parts.SightRed.sockets` |
| `SM_RifleHS_Sight2x` | `SOCKET_Aim` = 눈금 교차점 | `parts.Sight2x.sockets` |

Synty 의 `SOCKET_Mount_Trigger_0` 은 만들지 않는다 — 트리거는 몸통에 흡수했다(파츠 7 → 이 총은 6 + 조준경).

⚠️ 정적 메시의 소켓은 `UStaticMesh::Sockets` 이고 스켈레탈과 API가 다르다.
⚠️ **바디 쪽 `SOCKET_Weapon` 은 여전히 캐릭터 스켈레톤에 있다** — 건드리지 않는다.
⚠️ 파이썬으로 스켈레탈 소켓 **읽기**: `sk.num_sockets()` + `get_socket_by_index(i)` + `find_socket_info(name)`.
`sockets` 프로퍼티는 protected 라 `get_editor_property` 로는 못 읽는다(2026-09-03 실측).

### 3-5. 머티리얼 (에디터 켜짐 — 라이브 생성·편집은 안전) — ⏳
**텍스처를 만들지 않는다.** 슬롯 5종에 절차적 MI 를 물린다 —
레시피는 `MI_Pac_WallEdge`(모서리 발광)·`MI_EnemyProto_AtomCubes`(프레넬 실루엣 림 +
노멀 변화율 하드 엣지)를 그대로 가져다 쓴다. 하드서피스는 **모따기 면이 곧 엣지**라 노멀 변화율 하드 엣지
레시피가 특히 잘 맞는다 — 모따기 0.3cm 띠가 밝은 선으로 읽히게 된다.

색은 §4-1 대역(**V 25–70 · S 30–60**) 안에서, 🔒 예약색 회피. `Reticle` 색은 §2-3 의 A-3-4 경고를 보고 결정.
머티리얼 검증은 **`get_statistics` 를 쓰지 말고** `get_material_diagnostics()` + `export_material_graph()` 로(§5-3).

### 3-6. DA 배선 (에디터 켜짐) — ⏳
`DA_Weapon_Rifle` 에서:
- `WeaponMesh` 를 **비우고** `WeaponMeshStatic` = `SM_RifleHS_Body` (스켈레탈이 우선이라 **비우지 않으면 안 보인다**, `:2306-2308`)
- `ADSAimRotationOffset` = **Yaw 90 그대로** (§2-2 정정 — 메시가 이미 엔진 +Y 프레임으로 내보내진다. PIE 에서 ADS 가 90° 틀리면 그때 0)
- `weapon_parts` 7항목의 `Part` 를 `SM_RifleHS_*` 로, `Socket` 은 기존 `SOCKET_Mount_*_0` 유지.
  트리거 슬롯은 비운다. 조준경 슬롯 = `SM_RifleHS_SightRed`(기본) — 2x 는 진화 카드 쪽에서 교체.
- 소켓 이름 5종(`SOCKET_Aim`·`Muzzle`·`LeftHand`·`RightHand`·`Weapon`)은 이름을 그대로 썼으므로 변경 없음.
> ⚠️ DataAsset 값 저작은 원래 **사용자 작업**이다(메모리 `da-edits-are-user-work`).
> 값과 위치를 정리해 넘기고, 사용자가 직접 넣게 할 것.

### 3-7. 검증
- 정적: 임포트된 메시 섹션 수 · 소켓 존재 · 바운드 · DA 참조 무결
- 동적(**사용자 PIE**): §6 판정 기준. **리슨 호스트 + 원격 클라 양쪽.**

---

## §4 🔒 지켜야 하는 것

### 4-1. 색 대역 — `ArtDirection.md` A-1
무기는 **캐릭터** 대역이다: **명도 V 25–70 · 채도 S 30–60**.
🔒 **절대 규칙**: 0% 도 100% 도 쓰지 않는다(순백·순흑·순색 금지 — 환경이나 UI 로 오독된다).
🔒 **A-3-4(적 = 뜨거운 쪽) · A-3-5(플레이어·아군 = 차가운 쪽)의 고채도·고명도 색을 무기 메시에 쓰지 말 것.**
`#4FD8FF`·`#2E9BFF`·`#8B6BFF` 는 아군 아웃라인/인디케이터 예약색이다 — 무기가 그 색을 띠면
4인 협동에서 아군 표식과 섞인다. 가이드 p26 의 유일한 절대 규칙이다.
⚠️ 레퍼런스의 **청색 리시버**는 채도가 이 대역 위다. 그대로 옮기지 말고 대역 안으로 낮춘다.

### 4-2. 스웜 가독성이 무기 화려함보다 위다
`ArtDirection A-5` = *"적 메시를 화려하게 만들지 말고 VFX 에 헤드룸을 남긴다"*.
1인칭 무기는 **화면 하단의 상시 점유물**이다. 여기가 밝고 화려하면 적 200~300 을 보는 대역을 깎아먹는다.
발광은 **작은 면적에 국소적으로** — 생성기의 `Emissive`/`Reticle` 배치가 이미 그렇게 돼 있다. 늘리지 말 것.

### 4-3. 외부 팩 통합 절차를 건너뛰지 말 것
`Docs/AssetIntegration_Protocol.md` 의 4단계(DISCOVER → MAP INTENT → AUTHOR → VERIFY)가 상시 절차다.
소켓·총구·ADS 규약의 선례는 `Docs/WeaponPack_Integration.md` 에 있다(LPAMG 팩 기준이라 **팩 고유
내용은 무효**지만, *"총구 = 파트 소켓"*·*"ADS 조준 = 사이트 파트 소켓"* 같은 **구조 규약은 살아 있다**).

---

## §5 함정 — 이 트랙에서 반드시 밟게 되는 것

### 5-1. 🔴 임포트는 **실행 모드를 틀리면 죽는다** (그리고 정답이 있다)
| 실행 모드 | 결과 |
|---|---|
| **라이브 에디터**(MCP/Python) | ❌ **게임 스레드 데드락.** 강제 종료 외 방법 없음 (`Troubleshooting.md` D1-a) |
| **`-run=pythonscript` 커맨드렛** | ❌ 임포트 직후 Slate 없어 어설션 즉사 (D1-b) |
| ✅ **헤드리스 정식 에디터 via `.bat`** | ✅ `run_import_rifle_hardsurface.bat` — D11 |

⚠️ `Docs/ArcadePostProcess_ResumePrompt.md` §4-1 의 *"세 번째 우회를 찾지 말 것"* 은 틀렸다 — 세 번째 우회는
이미 존재하고 문서화돼 있었다(`Troubleshooting.md` D1-b 말미). **텍스처가 필요해도 사용자에게 떠넘기지 말 것.**

### 5-2. 커맨드렛/헤드리스는 **종료 코드로 판정하면 안 된다**
`Troubleshooting.md` C4 — 작업은 끝났는데 마지막에 UI 동기화로 죽어 exit 3 이 나오는 경우가 많다.
→ 스크립트가 찍는 **완료 마커**와 **`does_asset_exist`**, 그리고 **`git status`** 로 판정.
`save_asset` 은 기본이 `only_if_is_dirty=True` 라 **로그엔 성공, 디스크는 그대로**가 된다 →
`only_if_is_dirty=False` + git 대조.

### 5-3. 머티리얼 검증에 `get_statistics` 를 쓰지 말 것
그래프를 바꿔도 **같은 숫자를 계속 돌려준다**(생성 시점 값 고정) — `Troubleshooting.md` **G14**.
→ `get_material_diagnostics()`(`.is_compiled_ok`/`.compile_errors`) + `export_material_graph()`(연결 전량).
배선은 전부 `link()` 헬퍼로 감싸 **실패를 수집**할 것 — `connect_material_expressions` 는 이름을 못 찾으면
**조용히 False** 다(**D10**).

### 5-4. 스케일·부호 — 임포트 직후 바운드 **min/max** 를 cm 로 대조할 것 (size 아님)
메모리 `fbx-metre-cm-armature-scale-trap`(100배) + `Troubleshooting.md` **D12**(Y 부호 반전).
`size = max − min` 은 **부호를 못 본다** — Y 가 통째로 뒤집혀도 크기는 같다. 두 번 통과시킨 뒤에야 잡았다.
→ **기대 min/max 를 먼저 적어 두고** 임포트 결과와 대조. 기준 = 몸통 `X −5.2..5.1 · Y −8.0..22.6 · Z −4.1..11.7`,
전장 ≈95cm. "들어왔다"는 "맞게 들어왔다"가 아니다. `import_rifle_hardsurface.py` 가 이제 min/max 를 찍는다.

### 5-5. 스켈레톤을 대상으로 하는 임포트는 하지 말 것
기존 스켈레톤을 지정해 임포트하면 뼈가 안 맞을 때 **그 스켈레톤 에셋을 통째로 갈아엎는다**
(메모리 `ue-import-overwrites-target-skeleton`). 이 트랙은 정적 메시라 해당 없다. 기각된 갈래(스켈레탈 승격)로
되돌아가면 그때 임포트는 **사람이 에디터에서**.

### 5-6. 열린 에디터가 `.uasset` 을 잠근다
임포트/머지 전 **에디터 종료**. 메모리 `ue-editor-file-locks-block-git`.

---

## §6 판정 기준 (사용자 PIE)

1. **크기가 맞는가** — 화면에서 총이 차지하는 면적이 종전 Synty 라이플과 비슷한가
2. **ADS 에서 조준선이 화면 중앙에 오는가** — §2-2 축 결정과 `ADSAimRotationOffset` 이 맞물렸는지. 1x·2x 둘 다
3. **총구 화염이 총구 끝에서 나는가** — `MuzzleSocket` 위치
4. **양손이 총을 잡고 있는가** — 좌/우손 IK 소켓
5. **적이 여전히 먼저 눈에 들어오는가** — §4-2. 무기가 시선을 뺏으면 실패다
6. **하드서피스로 읽히는가** — 모따기 엣지가 인식되는지, 피카티니·통기구가 디테일로 읽히는지
7. **파츠가 붙어 있는가** — §3-2 가드 확장이 실제로 먹었는지. 몸통만 보이면 가드가 아직 스켈레탈 전용이다

---

## §7 미결 / 이 트랙 밖

- **나머지 무기 8종** — §2-5 파일럿 통과 후. 같은 생성기에 파츠 함수만 갈아끼우는 구조라 재사용률이 높다.
- **곡률·모아레**(포스트프로세싱 트랙 잔여) — `MI_PP_ArcadeCRT` 의 `Curvature` 가 0으로 꺼져 있고
  스캔라인 모아레는 1080p 에서만 확인했다. 하드서피스 모따기 엣지·피카티니 슬랫이 **스캔라인과 간섭할 수 있다** —
  이 트랙 판정 때 같이 봐 두면 이득이다.
- **HUD 무기 아이콘** — DA 의 `WeaponIcon` 은 별도 2D 에셋이다. 새 룩에 맞는 아이콘이 따로 필요하다.
- **셀↔아웃라인 stencil 규약** — ADR 0007 미결. 무기에 아웃라인을 걸지 여부가 여기 걸린다.
- **무기 자체 AnimBP(노리쇠)** — 정적 메시로 가면서 닫힌 문. 파츠 분리라 `Body` 의 장전손잡이를 별도 파츠로 빼면
  소켓 애니로 흉내낼 수 있다. 필요해지면 그때.
