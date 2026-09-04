# 1P 손만 표시(How to Fish 식 떠 있는 손) — 재개 프롬프트 (보류 트랙)

> **상태: 보류.** 사용자 결정 2026-09-04: *"손만 보여주는 건 할 건데, 지금 당장은 우선 여기서(총만 표시) 마무리하고 나중에 하겠다."*
> **현행 구조 = [ADR 0015](Architecture/0015-first-person-gun-only-hidden-arms-driver.md)** — 1인칭은 총만 그리고, 팔 컴포넌트는
> `Hidden in Game` 으로 숨긴 **모션 드라이버**로만 산다. 이 문서는 그 위에서 **손을 다시 그리기로 할 때** 어디서부터 시작하는지다.
> 보드 행 = **ARM2**(백로그). 착수 = 보드 클레임 후(§6-9).

## 0. 이 트랙이 무엇이고 무엇이 아닌가

- **목표**: 1인칭에 **손목 아래 손만** 떠 있게 그린다(레퍼런스 = How to Fish). 팔뚝·상완은 안 보인다. 애니는 PWAS 리타깃 기본 애니(Idle/ADS/Reload) 그대로.
- **비목표**: 손 IK 재도입(ADR 0015 I4 — 그립 소켓 None 이 기본). 손 위치가 총과 안 맞으면 IK 가 아니라 **무기 부착 오프셋**(DA 확장 후보) 쪽으로 푼다.
- **왜 보류인가**: 총만 보이는 화면이 사용자 눈에 더 좋았고, 하드서피스 무기 트랙(남은 무기 8종 + §3-5 머티리얼)이 우선이다.

## 1. 지금 남아 있는 실물 (2026-09-04 기준)

| 항목 | 상태 | 위치 |
|---|---|---|
| 마스크 마스터 | 저작 완료, **인게임 미검증** | `/Game/Character/FPArms/Materials/M_FPArms_HandsOnly` — 팩 마스터 `M_LPAMG_Character_Base` 복제, Masked·양면, `PreSkinnedPosition` 기준 팔별 손목 평면 → `Step` → OpacityMask |
| 마스크 인스턴스 | 저작 완료 | `MI_FPArms_HandsOnly` — 팩 스킨 `MI_LPAMG_Character_Skin_002` 복제·재부모(텍스처·색 동일). 파라미터 그룹 `HandsOnly`: `WristL/R`, `ForearmDirL/R`, `CutOffset`(cm, + = 손가락 쪽) |
| 저작 스크립트 | 멱등, 재실행 가능 | `Scripts/author_fparms_handsonly_material.py` (+ `run_author_fparms_handsonly_material.bat`, 헤드리스 D11). 켜진 에디터에서 exec 해도 안전(quit 은 `-unattended` 에서만) |
| BP 배선 | **유지 중** | `BP_FPSRPlayer` → `FirstPersonArms` Materials[0] = `MI_FPArms_HandsOnly`(사용자 배선). 팔이 숨겨져 있어 렌더 영향 없음. Element 1(Nails)은 팩 기본 |
| 팔 표시 | 숨김 | `FirstPersonArms.Hidden in Game = true`, 틱 옵션 = 항상 틱(ADR 0015 I1/I2) |
| 손 IK | 퇴역(데이터) | `DA_Weapon_Rifle` 양손 그립 소켓 None. `ABP_FP_Base` 총 앵커 Copy Bone Alpha = 1.0 고정(IK 알파와 분리, Troubleshooting E5) |
| 실측 수치 | 유효 | `SKEL_LPAMG_Character` 기준 포즈 컴포넌트 공간: hand_l (56.6, −0.3, 111.7), 전완 방향 (0.72, 0.43, −0.55), 오른쪽은 x 부호 반전. 어깨→손목 55.1cm. 메시 바운드 원점 (0, −2.8, 125.2) 반경 (68.5, 15.5, 32.3) |
| 커밋 | | 머티리얼·스크립트 `90b91f20` · 문서 `e977de95` · ADR 0015 `83dfd346`/`5d1b8967` |

## 2. 미검증 — 재개하면 **이것부터** (대조군 먼저, 메모리 verify-with-control-group)

관측: 마스크를 배선한 PIE(어두운 아케이드 맵)에서 **손이 전혀 보이지 않았다**. 가설은 둘이고 아직 못 갈랐다.
- (a) 마스크가 **전부** 잘라냈다 — 부호/공간 오류, 또는 `PreSkinnedPosition` 이 픽셀 셰이더에서 0/무효라 `d < 0` 전건 클립.
- (b) 손은 그려졌는데 조명 탓에 안 읽혔다(피부 PBR + 거의 무광 맵 + 붉은 기운).

판별 절차(순서대로, 한 번에 하나만 바꾼다):
1. `BP_FPSRPlayer` → `FirstPersonArms` → `Hidden in Game` **해제**(임시 — ADR 0015 I1 을 잠깐 어기는 것이므로 끝나면 되돌리거나 새 결정으로 갱신).
2. 밝은 장소(아레나 조명 아래) 또는 에디터 뷰포트에서 본다.
3. `MI_FPArms_HandsOnly` → `HandsOnly_CutOffset` 을 **−30** 으로: 팔 대부분이 **돌아오면** 마스크가 살아 있고 절단 위치 문제(→ 5번). 안 돌아오면 4번.
4. 마스터에서 `Step` 대신 상수 1 을 OpacityMask 에 임시 연결: 전체가 보이면 마스크 식(a) 문제, 여전히 안 보이면 머티리얼 컴파일 실패 → 머티리얼 에디터 Stats/에러 확인.
   - `PreSkinnedPosition` 의 픽셀 셰이더 유효성: 5.7 번역기는 외부 코드 청크만 추가한다(`HLSLMaterialTranslator.cpp:9510`) — 셰이더 단계 제약은 `.ush` 쪽에 있을 수 있다. 안 되면 **Vertex Interpolator** 노드로 VS→PS 전달.
5. 절단 위치 튜닝: `HandsOnly_CutOffset` 만(+1cm 가 기본, 손목 링 숨김용). 평면 자체가 어긋나면 `WristL/R`·`ForearmDirL/R` 값 — 근거 수치는 §1.
6. 절단면이 뚫려 보이면 양면 렌더가 꺼진 것(마스터 `Two Sided`).

## 3. 재개 절차 (판별이 끝난 뒤)

1. 룩 확정: 손 재질 — 아케이드 룩(어두운 맵)에서 피부 PBR 은 안 읽힌다. **무광 단색 또는 자체발광 림**을 `ArtDirection.md` 색 대역 안에서 검토(1P 손 색이 스웜 가독성 색과 충돌하지 않게).
2. 손 위치가 총과 안 맞으면(기본 애니는 PWAS 소총 기준): 무기 부착 오프셋(DA 필드 신설 = 코드, §6-5-2 게이트) 또는 `SOCKET_Weapon` 미세조정(ADR 0006 I3 — 팔 메시 소켓 하나가 진실원천).
3. 확정 시 **새 ADR**(0015 를 고치지 않는다 — README 규칙: 상태만 바꾸고 새 번호) + `PlayerFeel.md` 2-9 정정 + 보드 행 완료.
4. 선택 — 프로덕션 정리: Blender 로 전완 삭제 + 손목 캡 메시를 만들어 같은 스켈레톤에 재임포트(마스크·양면 비용 제거). FBX 왕복 함정 = Troubleshooting F1-b, 메모리 ue-import-overwrites-target-skeleton(스켈레톤 충돌 창에서 Done 금지), arms-socket-lives-on-skeleton.

## 4. 지키는 제약

- 팔 표시/숨김 스위치는 **`Hidden in Game` 하나**. `SetVisibility` 금지(`RefreshFirstPersonRendering` 이 자식=총까지 전파), 메시 슬롯 비우기 금지(1P/3P 분할 조건이 메시 유무). 틱 옵션은 항상 틱.
- 손 IK 는 기본적으로 재도입하지 않는다(ADR 0015 I4). 재도입하려면 별도 결정 + Copy Bone 알파 독립(E5) 유지.
- 3P(팀원 화면)는 이 트랙과 무관 — 바디 메시가 무기를 든다.
- 에디터 편집(BP 슬롯·DA)은 사용자 작업, 머티리얼·스크립트는 Claude 저작.

## 5. 참조

- [ADR 0015](Architecture/0015-first-person-gun-only-hidden-arms-driver.md) · [ADR 0006](Architecture/0006-first-person-arms-purchased-rig-retargeted.md) · [ADR 0003](Architecture/0003-first-person-arms-camera-anchored.md)
- `Docs/Troubleshooting.md` D10(머티리얼 핀 이름 규칙) · E5(총 앵커 알파 함정) · F4(손 소켓 회전 누락) · D11(헤드리스 .bat)
- 하드서피스 무기 트랙 = `Docs/RifleHardSurface_ResumePrompt.md`(§3-5 머티리얼이 1P 비주얼의 전부가 된 이유)
