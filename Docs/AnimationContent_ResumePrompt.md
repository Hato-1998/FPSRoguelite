# 애니메이션 콘텐츠 저작 — 새 세션 재개 프롬프트 (Rifle 우선 · from scratch · 데모 애셋 미사용)

> **왜 새 세션인가**: 애니 코드 인프라(A/B/C+보스+무기 노리쇠 훅)는 전부 main 머지·push 완료(`5655aaf`). 남은 건 **에디터 콘텐츠 저작**인데, 직전 세션은 VibeUE MCP가 **세션에 미연결**(에디터는 열려 있고 8088 LISTENING이나 MCP 툴 부재)이라 진행 불가 → 인계. 새 세션은 **에디터 열고 VibeUE MCP 연결 확인** 후 진행(또는 DA 배선·몽타주 생성은 에디터 닫고 headless commandlet).

---

## 복붙 재개 프롬프트

```
[애니메이션 콘텐츠 저작 — Rifle 우선 예시] 브랜치: main (또는 phase/anim-content 분기)

■ 먼저 읽기: PROGRESS.md 상단 핸드오프 + Docs/AnimationPass_ContentGuide.md + 이 문서(AnimationContent_ResumePrompt.md) 전체.

■ 사용자 결정 (필수 준수):
  - **처음부터**(이전 WIP 삭제됨) · **팩 데모 애셋 미사용**(LowPolyAnimatedModernGuns/BroBot의 데모 AnimBP·몽타주 참조 금지) · 있던 것(BCC_01_BroBot_AnimBlueprint 등)도 **새로 다시 제작**.
  - **전용 Animation 폴더에만 저작**: 무기=`Content/Weapons/Animation/`(존재), 캐릭터=`Content/Character/Animation/`(생성).
  - ⚠️ 단, 팩의 **raw 애니 시퀀스**(A_FP_PCH_*, A_FP_WEP_*, BCC_01_BroBot_Walk 등)는 몽타주/BlendSpace/AnimBP의 **소스로 참조 OK**(모캡 원본은 새로 못 만듦). 금지 대상은 팩이 데모용으로 만든 **AnimBP/Montage/BlendSpace 애셋**.
  - **Rifle(MAK12) 1종만** 완성해 템플릿으로. 나머지 무기는 이후 자식 AnimBP 복제.

■ 연결: 에디터 열고 VibeUE MCP(127.0.0.1:8088, 등록명 VibeUE-Claude — 새 세션 MCP 연결 확인). AnimBP 그래프/BlendSpace는 VibeUE로도 취약할 수 있으니, 어려우면 그 스텝만 사용자 수동 지시. DA 필드 배선·몽타주 생성은 에디터 닫고 headless commandlet(-ExecutePythonScript)도 안정적. [[vibeue-buildgraph-pie-worldleak]](편집 후 에디터 재시작 후 PIE).

■ 코드 계약(main 머지 완료 — 아래대로 채우면 코드가 자동 재생): 이 문서 §"코드 계약" 참조.

■ 작업(이 문서 §"작업 순서" 상세): 몽타주 생성 → BlendSpace → 팔 AnimBP → 무기 AnimBP → 3P 바디 AnimBP → DA 채우기.

■ 검증: 사용자 PIE — 발사(팔 반동+노리쇠 동기)·재장전(ReloadTime 정합)·장착·이동 로코모션·2인 시 3P. 콘텐츠 커밋은 사용자 확인 후 content(anim) 커밋.
```

---

## 코드 계약 (main 머지 완료 — 이 필드들을 채우면 코드가 재생)

**무기 DA (`Content/Weapons/DataTable/DA_Weapon_Rifle`) — 채울 필드**:

| DA 필드 | 대상/재생 시점 | 값 |
|---|---|---|
| `WeaponMesh1P` | 1P 무기 메시(스켈) | `SK_LPAMG_MAK12` |
| `ArmsAnimInstanceClass` | 장착 시 FirstPersonArms에 적용 | **새 팔 AnimBP**(Character/Animation) |
| `WeaponAnimInstanceClass` | 장착 시 WeaponMesh1P에 적용 | **새 무기 AnimBP**(Weapons/Animation) |
| `WeaponAttachSocket` | 팔의 무기 소켓 | 기본 `SOCKET_Weapon`(팔 스켈 소켓 확인) |
| `MuzzleSocket` | 총구(머즐 원점) | `SK_LPAMG_MAK12`의 총구 소켓명 |
| `EquipMontage` | 장착 시 **팔**에 재생 | 새 장착 몽타주 |
| `FireMontage` | 발사 시 **팔**에 재생 | 새 발사(팔) 몽타주 |
| `WeaponFireMontage` | 발사 시 **무기 메시**에 재생(노리쇠, 팔과 동기) | 새 발사(무기) 몽타주 |
| `ReloadMontage` | 재장전 시 **팔**에 재생(재생속도=길이/ReloadTime 자동) | 새 재장전(팔) 몽타주 |
| `WeaponReloadMontage` | 재장전 시 **무기 메시**에 재생(동기) | 새 재장전(무기) 몽타주 |
| `WeaponParts1P` | 장착 시 무기 메시 소켓에 부품 부착 | `SM_LPAMG_MAK12_{Magazine,Barrel_Default,Forend_Default,Ironsight_F}` + 소켓 + 오프셋 |
| `MuzzleFlash`/`FireSound` | 발사 코스메틱 | 선택 |

**코드 동작 요점**:
- 발사 훅(`PlayWeaponFireCosmetics`, 오너 로컬)이 `FireMontage`(팔)+`WeaponFireMontage`(무기)를 **동시** 재생 → 반동↔노리쇠 동기. 관전자(DBNO)는 `MulticastFireCosmetics` 뷰타깃 게이트에서 동일.
- 재장전 훅(`OnRep_Reloading`→`HandleReloadStateChanged`, 오너)이 `ReloadMontage`(팔)+`WeaponReloadMontage`(무기)를 **재생속도=몽타주길이/해석ReloadTime**로 재생.
- `ArmsAnimInstanceClass`→FirstPersonArms, `WeaponAnimInstanceClass`→WeaponMesh1P에 장착 시 자동 SetAnimInstanceClass.
- ⚠️ **몽타주가 화면에 반영되려면 AnimBP AnimGraph에 그 몽타주 슬롯과 같은 `Slot` 노드가 포즈 체인에 있어야 함**(슬롯 없으면 재생돼도 무영향).

---

## 소스 애니 시퀀스 (raw만 참조 — 데모 AnimBP/몽타주 금지)

- **팔(스켈 `SKEL_LPAMG_Character`)**, `Content/Assets/LowPolyAnimatedModernGuns/...MAK12/`:
  `A_FP_PCH_MAK12_Fire` · `_Reload` · `_Reload_Empty` · `_Idle_Loop` · `_Sprint_F_Loop` · `_Jump_Start/_Jump/_Jump_End`(있으면) · `_Unholster`(장착) · `_Inspect`(선택)
- **무기(스켈 `SKEL_LPAMG_MAK12`)**:
  `A_FP_WEP_MAK12_Fire_Bolt` · `_Reload` · `_Reload_Empty` · `_Unholster`
- **3P 바디(BroBot)**, `Content/Assets/Characters/BroBot/Animations/`:
  `BCC_01_BroBot_Idle` · `_Walk` · `_Run` · `_Loop` · `_Jump` · `_Jump_Start` (+ 발사/재장전 3P는 부재 → Mannequin Rifle 리타깃 또는 신규)

---

## 작업 순서 (Rifle 예시)

### A) 캐릭터 Animation 폴더 (`Content/Character/Animation/`, 생성)
1. **로코모션 BlendSpace** `BS_Arms_MAK12`(1D, 축=속도): Idle_Loop(0) ↔ Sprint_F_Loop(최대). (1P는 방향 블렌드 불필요.)
2. **팔 몽타주** (슬롯 예 `UpperBody`): `AM_Arms_MAK12_Fire`(←Fire) · `AM_Arms_MAK12_Reload`(←Reload) · `AM_Arms_MAK12_Equip`(←Unholster).
3. **팔 AnimBP** `ABP_Arms_MAK12`(스켈 SKEL_LPAMG_Character): AnimGraph = `BS_Arms_MAK12`(속도 구동) → 점프 스테이트 → **`Slot 'UpperBody'`** → Output. EventGraph에서 속도/스프린트 변수 갱신. **부모/자식 패턴**: 이걸 부모로 두고 다른 무기는 자식 AnimBP(에셋 오버라이드)로 확장.
4. **3P 바디 AnimBP** `ABP_Body_BroBot`(새로, 데모 것 대체): BroBot 로코모션(Idle/Walk/Run BlendSpace) + 상체 몽타주 슬롯(발사/재장전 = Layered Blend Per Bone으로 하체 로코모션 위에) + AimOffset(`GetBaseAimRotation`, 복제 Velocity만·Accel 금지). → `BP_FPSRPlayer`의 Mesh를 이 AnimBP로 재배선.

### B) 무기 Animation 폴더 (`Content/Weapons/Animation/`, 존재)
5. **무기 몽타주** (슬롯 예 `WeaponSlot`): `AM_Wep_MAK12_Fire`(←WEP_Fire_Bolt) · `AM_Wep_MAK12_Reload`(←WEP_Reload).
6. **무기 AnimBP** `ABP_Wep_MAK12`(스켈 SKEL_LPAMG_MAK12): 최소 그래프 = idle 포즈(또는 참조 포즈) → **`Slot 'WeaponSlot'`** → Output. (로코모션 불필요.)

### C) DA 배선
7. `DA_Weapon_Rifle`에 위 §코드계약 표대로 전부 지정.

---

## 검증 (사용자 PIE)
발사 시 팔 반동+**노리쇠 왕복 동기** · 재장전 애님↔ReloadTime 정합 · 장착 몽타주 · 이동 시 팔 로코모션(Idle↔Sprint) · 부품 부착 · (2인) 3P 바디/무기 발사·재장전 · 프리즈 중 몽타주 정지 · 게임플레이 수치 무회귀.

## 완료 후
Rifle 검증되면 나머지 7종 = 자식 AnimBP(클립 오버라이드)+DA 몽타주로 복제. content(anim) 커밋(사용자 확인).
