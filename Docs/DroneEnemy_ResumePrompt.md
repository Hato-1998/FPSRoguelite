# 복셀 드론 적 — 쩝쩝이 대체 · 실행 프롬프트 (새 세션용)

> **한 줄:** 사용자 제공 키아트(기준 `Docs/Concept/ArcadePixel_KeyArt_UserRef_2026-09-06_v2_HUD.png` · 색 정답 예시 `..._v3a_dense.png` 의 빨간 라이트 드론)의 **복셀 드론**을 일반 적 1종으로 저작해 쩝쩝이(복셀 유령)를 **대체**한다. 쩝쩝이 파이프라인(생성기 → Blender 프리뷰 → 헤드리스 임포트 → 머티리얼 저작 → `.bat`)을 그대로 재사용하고 **메시·요소ID 계약·WPO 동작만** 바꾼다.
> 사용자 결정(2026-09-06): *"내가 보고 있는 가장 이상적인 게임 모습이야. 최종적인 모습은 달라질 수 있지만 해당 아트를 방향성으로 하자. 쩝쩝이는 컨셉아트의 드론으로 변경."*
> 정본 = [ADR 0016](Architecture/0016-art-direction-retro-arcade-pixel.md) **정정(d)** · `Docs/SSOT/ArtDirection.md §B`. 이 문서는 실행 순서다.

---

## §0 세션 시작 방법

1. `Game.md` §0-1 라우팅 → `Docs/SSOT/Enemy.md`(적 구조) · `ArtDirection.md §A·§B`(색·형태) · ADR 0016 · [ADR 0007](Architecture/0007-enemy-swarm-render-path-cpd.md)(CPD 병합 자격) · [ADR 0009](Architecture/0009-hover-swarm-local-3d-flow-window.md)(부양 스웜) 만 읽는다.
2. **보드 클레임(§6-9)** — `/board 클레임 복셀 드론 적 — 쩝쩝이 대체`. 기존 행 「ART-후속 — 적 1종 복셀(아케이드) 메시·머티리얼 파일럿」(검증중, 쩝쩝이 3차 진행 중)은 **스코프가 이 드론으로 바뀐다** — 그 행에 로그를 붙이고 이어받을지 신규 행을 팔지는 보드 조사 결과로 판단(권장 = 기존 행 이어받기, 담당 갱신). 선행 「아트 방향 ADR 저작」은 검증중(컨셉 시트 승인 대기)이나 사용자 결정으로 병행 진행.
3. **에디터는 닫고** 시작한다(헤드리스 임포트·저장 공유위반 — 메모리 `ue-editor-file-locks-block-git`, `headless-editor-lingers-after-output`). 라이브 코딩 금지.
4. 같은 워킹트리에 다른 세션이 있을 수 있다 — `git status` 로 남의 미커밋(쩝쩝이 3차 `Scripts/gen_voxel_chomper.py` 등)을 확인하고 **건드리지 말 것**. 커밋은 자기 파일만 명시 경로로.

## §1 결정 (이 세션이 바꾸지 않는 것)

- **키아트 = 방향성.** 그대로 복제하는 게 아니라 `§A` 색 규칙으로 **번역**해서 만든다(아래 §2 번역표). 규칙을 바꾸려면 사용자 결정 + `ArtDirection.md` 개정이 먼저다 — 이 세션 범위 밖.
- **렌더 경로 불변**: 절차적 스태틱 메시 + 공유 base material + CPD 개체차(0007 · 0016 I3). per-actor MID 금지. `UFPSREnemyAnimProfile_Proc` 가 CPD 에 상태ID·진입시각·rate 를 쓰고, 읽는 쪽 = 이 머티리얼의 Custom HLSL WPO.
- **요소ID 계약 방식 유지**: `ElementId = floor(UV.u)`, 단일 섹션·단일 머티리얼, 색 = 요소ID LUT(텍스처 없음, 0016 I7).
- **격자 = 7.5 cm**(적 클래스, 0016 D4). 한 메시 한 격자(I4).
- ~~드론 = 부양 적~~ → **§5 결정 1: 지상.** 떠 있는 척(시각 고도)만. `HoverHeight` 필드는 시각 오프셋으로만 쓰거나 무시하고, ADR 0009 3D 창 필드는 사용하지 않는다.

## §2 키아트 → 규칙 번역표 (반드시 이대로)

| 이미지에서 보이는 것 | 이 게임에서 | 근거 |
|---|---|---|
| 드론 라이트 = **시안** + 마젠타 (v2) — **v3a 의 빨간 라이트 드론이 정답 예시** | **뜨거운 쪽만**: 코어·라이트 `#FF3B4E`(림/기본) · 공격 텔레그래프 `#FF6B2C` · 엘리트 `#FF1E7A`. **시안 금지** | 🔒 `§A-3-4/5` — 시안 = 아군. 4인 협동 × 적 200~300 에서 무너지면 복구 불가 |
| 드론 몸통 = 회색/검정 | 캐릭터 대역(V 25~70 · S 30~60) 안의 **어두운 자주·회보라**(예 `#3A2748` 몸통 · `#2A1E36` 어두운 면 · `#6E2E44` 팔). 환경(청보라)과 색상이 갈려야 실루엣이 산다 | `§A-1` 캐릭터 행 · 0016 D2 |
| 로터 4개(십자 쿼드) | 유지. 로터 = 별도 요소 그룹, WPO 로 회전(§4) | 형태 = 이 적의 정체성 |
| 중앙 스크린 눈(시안) | **코어 1개**(뜨거운 색, 이미시브). 공격 텔레그래프 = 코어 펄스 + 밑면 라이트 | `§B-5` "읽힘점 = 발광 코어 1개" |
| 크기 | 폭(로터 포함) **약 120 cm(16칸)** · 높이 **약 45 cm(6칸)** · 시각 고도 120~180 cm(지상 길찾기 + 메시 오프셋) — 제안 초기값, PIE 조정 | 쩝쩝이 폭 120 과 동일 실루엣 예산 |

## §3 파이프라인 (쩝쩝이 것을 복제해서 이름만 바꾼다)

| 쩝쩝이 | 드론 (신규) | 비고 |
|---|---|---|
| `Scripts/gen_voxel_chomper.py` | `Scripts/gen_voxel_drone.py` | 파이썬 → `Saved/EnemyVoxel/SM_EnemyVoxel_Drone.obj` + 프리뷰 obj/mtl. **ASCII 스프라이트를 단일 소스로**(쩝쩝이 3차 방식): 정면·측면·평면 3뷰 맵을 `Scripts/gen_concept_sheet_arcade_pixel.mjs` 에 `DRONE_*` 로 먼저 넣고 생성기가 그걸 읽는다 |
| `Scripts/render_voxel_chomper_preview.py` | `render_voxel_drone_preview.py` | Blender 5앵글 육안 |
| `Scripts/import_voxel_chomper.py` | `import_voxel_drone.py` | 헤드리스 **정식 에디터**(`-ExecCmds py`, try/finally quit_editor — 메모리 `headless-editor-use-bat-runners`) |
| `Scripts/author_voxel_chomper_material.py` | `author_voxel_drone_material.py` | `M_FPSREnemyVoxelDrone` · `MI_EnemyVoxel_Drone` — LUT 12 + 격자선 + 코어 이미시브 + **로터 회전 WPO** |
| `Scripts/run_voxel_chomper_pipeline.bat` | `run_voxel_drone_pipeline.bat` | ASCII 전용(cmd OEM 코드페이지 함정 D11) |
| `Content/Assets/Characters/EnemyVoxel/*Chomper*` | `*Drone*` | 쩝쩝이 에셋은 **삭제하지 않는다**(3차 세션 소유·기록). 대체 = BP 슬롯 교체 |

## §4 요소ID · WPO 계약 (제안 — 생성기 헤더가 정본이 된다)

| ID | 요소 | 그룹 | 색(LUT 기본) |
|---|---|---|---|
| 0 | 몸통 윗면 | BODY | `#3A2748` |
| 1 | 몸통 측·밑면(어두운 면) | BODY | `#2A1E36` |
| 2 | 코어 프레임(눈 테두리) | BODY | `#1A1024` |
| 3 | **코어**(이미시브) | CORE | `#FF3B4E` → 텔레그래프 시 `#FF6B2C` |
| 4 | 팔 ×4 | BODY | `#6E2E44` |
| 5 | 로터 허브 ×4 | HUB | `#2A1E36` |
| 6 | 로터 날개 ×4 | **ROTOR** | `#8A3A52` |
| 7 | 밑면 라이트 ×4(이미시브) | LIGHT | `#FF3B4E` |
| 8 | 꼬리 핀 | BODY | `#6E2E44` |
| 9~11 | 예비(엘리트 장식·2번 코어 등) | — | — |

- **ROTOR 회전 WPO**: 요소 6 정점을 자기 허브 중심 축(+Z) 둘레로 `angle = Time × RotorRate(CPD rate) + hubIndex × 90°` 회전. 허브 중심은 정점의 UV.v 나 정점색에 인코딩(쩝쩝이가 JAW 그룹을 ID 만으로 가른 것과 같은 방식 — **정점 위치로 그룹을 가르지 말 것**).
- **텔레그래프**: CPD 상태ID = 공격 진입 → 코어·라이트 이미시브 펄스(`§A-2` Damage Spell 등급: 높음·선명·짧게). 이동 = 로터 rate 변조. 죽음 = 기존 죽음 경로(bDead RepNotify) — 신규 복제 0.
- 밀봉/노출 단언(쩝쩝이 플러드필 방식)은 **로터 회전 전후 모두** 자기교차 0 을 검사하도록 바꾼다.

## §5 사용자 결정 — ✅ 결정됨 (2026-09-06, 아트 세션에서 사용자 답변. 다시 묻지 말 것)

| # | 결정 | 내용 |
|---|---|---|
| 1 | **지상** | 떠 있는 척만 한다 — 땅 길찾기(2D 플로우필드) 그대로, 고도는 연출(`HoverHeight` 시각값·바운스 WPO). ADR 0009 3D 창 필드는 쓰지 않는다. §1 의 "드론 = 부양 적" 문장은 이 결정으로 **무효** |
| 2 | **돌진 → 초근접 사격** | 드론은 **일반 몹 1종**으로 먼저 저작(다른 몹은 나중에 별도 저작). 공격 = 돌진해 와서 초근접에서 사격 — 베이스는 `BP_EnemyRangedBase` + `BP_EnemyProjectile` 에 **짧은 사거리**, 접근 단계는 돌진형 이동. 엘리트 변형은 이 트랙 범위 밖 |
| 3 | **대체** | 쩝쩝이 사용 중단, 에셋 존치(둥근 패밀리 필요 시 복구) |

(아래는 결정 전 원문 — 기록용)


1. **부양 vs 지상**: 드론이니 부양(0009 국소 3D 창)이 자연스럽다. 대가 = 부양 경로는 창 필드 비용이 붙고 스웜 밀도 실측이 다시 필요. 지상에 묶으면(고도만 연출) 싸지만 "드론"이 아니다. **권장 = 부양, 단 스폰 덱(ADR 0014)에서 부양 비율을 저작값으로.**
2. **공격 유형**: 근접 돌진(`BP_EnemyMeleeBase`) vs 원거리(`BP_EnemyRangedBase` + `BP_EnemyProjectile`). 키아트는 판단 불가. 권장 = 일반 드론은 돌진, 엘리트 변형이 원거리.
3. **쩝쩝이 처분**: 대체(BP 슬롯 교체, 에셋 존치) vs 2종 병행(구 + 침 실루엣 패밀리). 사용자 원문 = "변경" → 기본 = 대체.

## §6 검증 (완료 판정 — 검증은 Opus, 하위 모델 위임 금지)

- [ ] 생성기 단언: 회전 0°·45°·90° 에서 자기교차 0, 격자 7.5 정수배, 단일 섹션.
- [ ] Blender 5앵글: 정면·측면·평면·3/4·밑면 — 실루엣이 "쿼드 드론"으로 읽히는가, 코어 1개가 읽힘점인가.
- [ ] 헤드리스 임포트 로그 `[import]` OK · 머티리얼 로그 OK · `save_asset` 반환값 True(에디터 꺼진 상태).
- [ ] 색 판정: 뜨거운 쪽 외 이미시브 0. 시안·파랑 픽셀이 LUT 에 없다(grep).
- [ ] 적 300 병합 실측(0007 A/B 방식, `stat RHI`): 드로우콜이 쩝쩝이 대비 증가 0 — ROTOR WPO 가 인스턴싱을 깨지 않는다.
- [ ] PIE(사용자): 시각 고도·돌진→초근접 사격 텔레그래프 가독·4인 화면. 보드 = `검증중`.
- [ ] 컨셉 시트 갱신: `gen_concept_sheet_arcade_pixel.mjs` 의 라인업·키 비주얼·통로 뷰에서 CHOMPER → DRONE 스프라이트 교체, 재조립 후 **같은 URL 로 재게시**(`Artifact url=https://claude.ai/code/artifact/449d869e-de7f-4a42-ac9f-816935535a5c`, `contract 0.1.31`, `capabilities` 생략).

## §7 범위 밖
- 환경(벽 격자선·통로 라인)·무기·HUD 재스킨 — 각각 별도 행(ADR 0016 정정(d) 후속).
- 아레나 위상·타일 크기 — ADR 0017.
- 색 규칙 개정(시안 적 허용 등) — 사용자 결정 + `ArtDirection.md` 개정 선행.

## §8 함정 (이미 밟은 것)
- 헤드리스 자동화는 `.bat` + 개별 호출, 판정은 stdout(메모리 `automation-abslog-truncated-read-stdout`, `automation-multi-test-plus-hangs`).
- 에디터 종료 후 1~2분 더 산다 — 연속 호출 시 저장 공유위반(32).
- `delete_asset` 은 에디터를 모달로 멈춘다 — 스크래치 에셋은 에디터 끄고 파일로.
- HLSL 예약어(`line` 등)로 변수명 쓰면 머티리얼이 조용히 Default 로 떨어진다(`gen_arcade_floor_guideline.py` 헤더).
- 적 베이스에 새 UPROPERTY 이름이 BP 컴포넌트와 충돌하면 BP 컴파일이 깨진다(메모리 `cpp-uproperty-name-collides-with-bp`) — 이 트랙은 C++ 변경 0 이 목표.
