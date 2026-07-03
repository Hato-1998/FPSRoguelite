# 통합 애니메이션 패스 — 콘텐츠 저작 가이드 (A/B/C)

> **역할 분담(사용자 결정 2026-07-03)**: 코드 인프라(훅·필드·드라이버)=완료(`phase/p6-animation-pass`), **콘텐츠 저작=사용자**. 이 문서는 세 도메인의 애니를 실제로 "보이게" 만드는 에디터 작업 순서다. 코드는 전부 **null-safe·휴면(dormant)** 이라 콘텐츠를 채우기 전까지 게임플레이·렌더에 무영향.
> ⚠️ 에디터 편집 후 같은 세션 PIE = World Leak 크래시 → **편집 후 에디터 재시작(Don't Save) 후 PIE** (메모리 [[vibeue-buildgraph-pie-worldleak]]). 컨테이너 위젯 프로그래매틱 저장 금지(크래시).

---

## 도메인 A — 1P 무기 애니메이션

### 코드가 제공하는 것 (이미 완료)
- `UFPSRWeaponDataAsset` 1P visual 블록에 신규 필드: `ReloadMontage`(팔 재장전 몽타주), `WeaponParts1P`(모듈러 부품 배열 = `Part`/`Socket`/`Offset`).
- `RefreshFirstPersonWeaponVisual`이 장착 시 SK 무기(`WeaponMesh1P`)에 부품을 런타임 부착 + `ReloadMontage` 캐싱.
- 재장전 트리거: `UFPSRWeaponInstance::bReloading`(서버권위) → `OnRep_Reloading` → 오너 클라 1P 팔에 `ReloadMontage` 재생(재생속도=몽타주길이/해석 ReloadTime 자동 정합·프리즈 게이트).

### 사용자 저작 (에디터)
무기 DA(`Content/Weapons/DataTable/DA_Weapon_*`)마다:
1. **`WeaponMeshStatic1P`(Preview) → `WeaponMesh1P`로 전환**: `SK_LPAMG_<CODE>` 스켈레탈 무기 메시 지정(정적 Preview는 비움). 칼(Melee)은 정적 유지.
2. **`ArmsAnimInstanceClass`**: 무기별 팔 AnimBP(스켈 `SKEL_LPAMG_Character`). idle 로코모션 + 발사/재장전 몽타주 슬롯 포함.
3. **몽타주 3종**: `FireMontage`(노리쇠 A_FP_WEP_<CODE>_Fire와 팔 A_FP_PCH_<CODE>_Fire 동기), `EquipMontage`, `ReloadMontage`(A_FP_PCH_<CODE>_Reload / Reload_Empty). **재장전 몽타주 길이 ≠ ReloadTime이어도 코드가 재생속도로 자동 스케일** — 몽타주는 자연스러운 길이로 제작.
4. **`WeaponParts1P`**: 배럴/포어스톡/탄창/조준경 = `SM_LPAMG_<CODE>_{Forestock/Barrel/Forend/Magazine/Ironsight}` → 각 항목 `Part`+무기 스켈 `Socket`+미세 `Offset`.
5. **무기 AnimBP**(스켈 `SKEL_LPAMG_<CODE>`): idle + 노리쇠(WEP_Fire) + 재장전(WEP_Reload/Reload_Empty).

**무기별 그립** = `ArmsAnimInstanceClass`(무기별 팔 AnimBP)로 처리(코드 이미 지원). BlendByEnum 별도 구현 금지(DESIGN-FIRST — 중복).

⚠️ **에셋 커버리지 갭**(git ls-files 실측, 해당 무기는 몽타주 비워두면 null-safe 무애님 폴백):
| 무기 | 코드 | 발사 팔애님 | 재장전 | Reload_Empty |
|---|---|---|---|---|
| Bazooka | X13 | ✓ | **없음** | 없음 |
| Grenade | HVG7 | ✓ | **없음** | 없음 |
| Sniper | LRAF9 | ✓ | ✓ | **없음** |
| ChargeLaser | RC425 | **없음** | 없음 | 없음 |
| Melee | — | Attack만 | — | — |
(AG14W/MAK12/SP60/MR22 = 풀 커버)

### 검증 (single-client PIE)
발사 시 팔↔무기 노리쇠 동기 · 재장전 애님↔ReloadTime 정합 · 부품 정확 부착 · 무기교체 갱신 · 칼=정적 폴백 · **프리즈 중 몽타주 정지** · 게임플레이 수치(반동/탄약/데미지/연사·점사·차징) 무회귀. **1종 먼저 검증 후 확대**(롤백 안전).

---

## 도메인 B — 3P 팀원 캐릭터 애니메이션 (협동 가시성)

### 코드가 제공하는 것 (이미 완료)
- `UFPSRWeaponDataAsset` 3P visual 블록: `WeaponMesh3P`(스켈)·`WeaponAttachSocket3P`(3P 손 소켓)·`FireMontage3P`·`ReloadMontage3P`. 전부 null-safe.
- `WeaponMesh3P` 컴포넌트(생성자, `GetMesh()` 부착·`SetOwnerNoSee(true)`=원격만 가시). `RefreshFirstPersonWeaponVisual`(all-clients)이 3P 손 소켓에 부착.
- 3P 발사 = `MulticastFireCosmetics`(서버 발사확정)가 원격 프록시에 `FireMontage3P` 재생(자기=1P만). 3P 재장전 = `OnRep_Reloading` 원격 분기가 `ReloadMontage3P` 재생(**AnimBP 폴링 불요** — 이벤트 구동). `IsReloading()` BlueprintPure도 노출(AnimBP가 재장전 포즈 블렌드에 쓰려면).

### 사용자 저작 (에디터)
1. **`BCC_01_BroBot_AnimBlueprint` 확장**(이미 `BP_FPSRPlayer.CharacterMesh0`에 배선됨):
   - 발사/재장전 **몽타주 슬롯** 추가(`FireMontage3P`/`ReloadMontage3P`가 여기 재생됨).
   - **Aim Offset**: `GetBaseAimRotation()`(상하 조준, `RemoteViewPitch16` 복제) 기반. ⚠️ **로코모션은 복제 `Velocity`만 사용**(Acceleration은 시뮬레이트 프록시서 무효 — 참조 금지).
   - `IdleRun_Blendspace`가 복제 Velocity 구동인지 점검.
2. **BroBot 발사/재장전 몽타주** — ⚠️**부재**. (i) `Content/Assets/Characters/Mannequins/Anims/Rifle`에서 BroBot 스켈로 **리타깃**, 또는 (ii) 신규 저작. 없으면 우선 로코모션만 검증.
3. **DA_Weapon 3P 필드 채우기**: `WeaponMesh3P`(3P용 무기 스켈)·`WeaponAttachSocket3P`(BroBot 손 소켓명)·`FireMontage3P`·`ReloadMontage3P`. 자원 있는 무기부터.

### 검증 (2-client PIE, `net.AllowPIESeamlessTravel=1`)
(a) 상대 3P 바디 상호 가시 (b) 이동 로코모션 (c) 발사 시 3P 반응 (d) 재장전 시 3P 재장전 (e) 자기 화면=1P 팔만(3P 무기 안 보임) (f) **DBNO 관전 시 아군 1P 뷰 무회귀 + 1P/3P 오버랩·관전 muzzle 중복 흉함 판정**("흉하면 v2 관전카메라"를 여기서 결론 — 후속 등재) (g) 상하 조준 3P 반영.

---

## 도메인 C — 적 애니메이션 (VAT 확장) + 보스 스켈

> ⚠️ **적 드라이버는 `AnimProfile` 미할당 시 전체 휴면(zero-cost)**. 현재 큐브/VAT 렌더 무회귀. 아래 Stage 2(에디터 파라미터 확인)→Stage 3(콘텐츠 베이크+프로파일 할당) 순서로 활성화.

### 코드가 제공하는 것 (이미 완료)
- `EFPSRAnimState`(Idle/Walk/Attack/Death) + `UFPSREnemyAnimProfile`(폴리모픽 EditInlineNew) + `UFPSREnemyAnimProfile_VAT`(상태→클립인덱스/재생속도/phase를 MID 스칼라로 기록).
- `AFPSREnemyBase`: `AnimProfile` 슬롯(EditDefaultsOnly Instanced, **null 기본=휴면**)·상태소스(권위=서버 배치패스, 클라=`PostNetReceiveLocationAndRotation`)·거리 LOD 정지·근접 공격 휴리스틱·`OnDeathCosmetic`→Death·풀 재사용 리셋.
- `FPSRVATAnimParams.h` = C++↔머티리얼 **계약 헤더**(파라미터명·클립인덱스·CPD슬롯·freeze 반경).

### Stage 2 — ⚠️ 에디터 파라미터 확인 (헤드리스 불가, 선행 필수)
`Content/Assets/Characters/BroBot/VAT/M_BroBot_VAT` + `MF_BoneAnimation`을 에디터에서 열어 확인:
1. **실제 스칼라 파라미터명** (애니 인덱스 / 프레임 / 재생속도) → `FPSREnemyAnimProfile.cpp`의 `NAME_AnimationIndex`/`NAME_PlayRate`/`NAME_Phase`를 실제 이름으로 교체(현재 플레이스홀더, 틀리면 no-op).
2. **선택-인덱스 GPU 자가재생 가능 여부**: `AnimationIndex`를 바꾸면 GPU가 해당 클립을 Time으로 자가재생하는가? (안 되면 = per-frame 프레임 구동 필요 → 대안 = MPC 공유 클럭 1회/프레임 전역 기록).
3. **사망 per-clip loop/hold**(death는 1회 재생 후 마지막 프레임 고정) + **재생속도 0 정지** 지원 여부.
4. **MID vs CustomPrimitiveData 결정**: per-actor MID 300개는 드로우콜 배칭을 깬다. `M_BroBot_VAT`를 Custom Primitive Data 노드로 재저작하면(슬롯 = `FPSRVATAnim::CPDSlot_*`) 배칭 보존 + 인덱스 기록(더 쌈). 재저작 시 `UFPSREnemyAnimProfile_VAT`를 CPD 경로로 전환.

### Stage 3 — 콘텐츠 베이크 + 활성화
1. **VAT 다중 시퀀스 베이크**: `DA_Minion_Melee_VAT`/`DA_Minion_Siege_VAT`의 `AnimSequences[]`에 idle/attack/death 추가(`UAnimToTextureBPLibrary::AnimationToTexture` 재베이크) + `bAutoPlay=false`(C++ 구동 전환) + `SampleRate` 확인.
2. **`FPSRVATAnimParams.h` 클립 인덱스 매핑**: `ClipIndex_Idle/Walk/Attack/Death`를 실제 `AnimSequences[]` 순서에 맞춤.
3. **프로파일 할당(활성화 스위치)**: 적 아키타입 BP(`BP_EnemyBase` 등)의 `AnimProfile`에 `UFPSREnemyAnimProfile_VAT` 인스턴스 지정 → 드라이버 활성. **엘리트 Siege=VAT 유지**(스켈 승격 안 함).
4. **사망 death-dwell**(후속): 현재 `HandleDeath`→즉시 `ReleaseEnemy`(숨김)라 death 애니 미가시. 서버 death-dwell 타이머로 클립 길이만큼 릴리즈 지연 + 소수 dying 액터 한정 프레임 구동(병목은 정상상태 ×300이지 과도 ×소수 아님).
5. **공격 애니 지속**(후속): 현재 Attack은 transient(다음 패스 Walk/Idle 복귀). 실제 attack 클립 길이 기반 지속 = Stage 3 튜닝.

### 보스 (사용자 "보스만 스켈")
- 코드: `AFPSRBossBase::PlayBossMontage(UAnimMontage*)`(스켈 `GetMesh()` 몽타주 재사용 훅) + `DeathMontage` 필드 + `OnDeathCosmetic`/`HandleDeath` 배선. **idle+death 이번**, 어빌리티 몽타주는 보스 전투(U3/U4) 랜딩 시 `PlayBossMontage` 호출로 확장.
- 사용자 저작: 보스 BP의 **상속 Mesh(`GetMesh()`)에 Prime_Helix 스켈 메시 + AnimBP 지정**(idle 로코모션) + `DeathMontage` 지정. (현 `BodyMesh` 정적 박스는 숨기거나 유지 — 스켈 메시가 주 비주얼.)

### 검증 (PIE)
먼저 baseline VAT 재생 확인 → 프로파일 할당 후 이동/공격/사망 상태전환·이동속도별 재생속도·거리 LOD(원거리 정지)·200-300 스폰 hitch 관찰. **정량 perf 실측 = U14 perf 패스 이월**(§5 가드레일). GMS 힙할당 우려 없음(이 패스 리스너 0).

---

## 공통 주의
- 에셋 경로 C++ 하드코딩 금지 — 전부 DA soft ref(콘텐츠에서 지정).
- Demo/Mannequin 폴더는 배포 콘텐츠에 쓰지 말 것(리타깃 소스로만).
- 각 도메인 콘텐츠 저작 후 `git status`로 untracked 확인 → Phase 종료 시 동반 커밋(메모리 [[phase-end-commit-user-content]]).
