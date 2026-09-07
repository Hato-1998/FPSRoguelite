# 이미지 AI 설계도 → Meshy → 임포트 파이프라인 테스트 — 원거리 소형 드론 (새 세션 실행 프롬프트)

> 작성 2026-09-07(Fable, 복셀 드론 트랙 세션). **새 세션이 그대로 복붙해 착수**하는 실행 문서다.
> 사용자 결정(2026-09-07): 절차 생성으로 만든 소형 원거리 드론 복셀 모델은 **폐기**. 대신 다음 파이프라인을 이 드론으로 **시험**한다 —
> ① 프로젝트를 아는 Claude + 사용자가 **설계도 프롬프트**를 짠다 → ② 이미지 생성 AI 가 **설계도(3면도)** 를 그린다 → ③ **Meshy** 가 그 설계도로 3D 모델을 만든다 → ④ 프로젝트에 **임포트**한다.
> 결과물의 채택 여부보다 **파이프라인이 성립하는지·어디서 깨지는지**를 답하는 것이 이 행의 목적이다.

**복붙용 첫 지시문**
```
Docs/MeshyDronePipeline_ResumePrompt.md 를 읽고 진행한다. main 에서 직접 작업(트렁크). 보드 클레임부터(§0-2).
§1 전제(무엇이 살아남고 무엇이 깨지는지)를 먼저 읽고, §2 순서대로. 설계도 프롬프트(§3)는 초안이다 — 사용자와 함께 다듬어 확정한 뒤 이미지 AI 에 넘긴다.
Meshy·이미지 AI 실행은 사용자(계정). 결과 파일을 받아 §4 임포트·§5 판정을 한다. 판정 결과는 §6 표에 채워 넣고 보드에 기록한다.
```

---

## §0 세션 시작 방법
1. `Game.md` §0-1 라우팅 → `Docs/SSOT/ArtDirection.md §A·§B`(색·형태 규칙) · [ADR 0016](Architecture/0016-art-direction-retro-arcade-pixel.md)(아트 방향, D1 형태 = 복셀 · D7 제작 경로 3종 · I3/I7) · [ADR 0007](Architecture/0007-enemy-swarm-render-path-cpd.md)(공유 머티리얼 + CPD) · `Docs/Research/ExternalModelingTools_2026-09-06.md`(툴 조사·라이선스) · `Docs/DroneEnemy_ResumePrompt.md` §0-A(쿼드 드론 = 비교 기준선, 특히 "BP 교체 함정 1~3"·D13).
2. **보드 클레임(§6-9)** — 기존 행 「복셀 원거리 드론(소형 단일 프로펠러) — BP_EnemyRangedBase 대체 메시」(대기, 이 문서 작성 시 스코프를 "Meshy 파이프라인 테스트"로 재정의해 둠)를 이어받는다. 담당 갱신 후 재조회로 경합 확인.
3. **에디터는 사용자가 켠다/닫는다.** 임포트는 헤드리스 정식 에디터(에디터 닫힘 필수, `.bat` + 절대 경로 + try/finally — 메모리 `headless-editor-use-bat-runners`, Troubleshooting D11). Meshy 산출 GLB/FBX 는 **정적 메시**라 헤드리스 임포트 가능(리그드 GLB 크래시 사례는 스켈레탈 — `glb-import-crash-use-fbx`).
4. `git status` 로 다른 세션 미커밋(HUD 재스킨·상태이상 C++)을 확인하고 **건드리지 말 것**. 커밋은 자기 파일만 명시 경로로.

## §1 전제 — 이 파이프라인에서 살아남는 것 / 깨지는 것 (착수 전에 사용자와 합의)

쿼드 드론(절차 생성)이 만족하는 계약을 Meshy 결과물은 **자동으로는 하나도 만족하지 못한다.** 테스트는 "어디까지 되고 어디서 사람 손이 드는가"를 재는 것이다.

| 계약 | 절차 생성(쿼드 드론) | Meshy 결과물(예상) | 테스트에서 할 일 |
|---|---|---|---|
| 형태 = 복셀 (0016 D1·I4 한 메시 한 격자 7.5cm) | 격자 그대로 | 매끈한 저폴리 또는 Meshy "Voxel" 스타일(격자 크기 제어 불가) | **두 갈래 다 뽑는다**: (a) Meshy 아트 스타일 Voxel (b) 일반 저폴리 → Blender Remesh(Blocks, 7.5cm) 로 복셀화(0016 D7 ③). 어느 쪽이 격자 규칙에 가까운지 육안 |
| 단일 섹션·공유 base material (0007·0016 I3) | 섹션 1, `M_FPSREnemyVoxelDrone` 공유 | 텍스처 1세트·머티리얼 1개(대개 섹션 1) | 임포트 후 `sections==1` 확인. 텍스처 기반이면 I7(텍스처 없음) 위반 → **테스트 기간엔 허용**, 채택 시 색 LUT 로 재저작 필요를 기록 |
| 요소ID(`floor(UV.u)`) → 색·WPO | 생성기가 UV 에 인코딩 | 없음(UV 는 텍스처용) | **로터 회전 불가**(요소 6 없음). 옵션: ① 정적 로터로 테스트 ② Blender 에서 로터 파트 분리 + UV.u 타일링 스크립트(신규, 이 행 범위 밖 후보) ③ Meshy 파트 분할 기능이 있으면 파트별 export 후 태깅 |
| 코어 = 약점 읽힘점(§B-5) | 요소 3 이미시브 | 텍스처의 주황 사각형(발광 없음) | 이미시브 없음 = 텔레그래프 불가. 기록만 |
| 폴리 예산(적 300, 쿼드 1,246 tris) | 1.2k | 수천~수만 → Meshy Remesh 로 목표 3k | Meshy 리메시 target 3k 로 export, 임포트 로그 tris 기록 |
| 정면 = +X, 원점 = 중심, 단위 cm | 생성기 보장 | Meshy 는 Y-up·m 단위·정면 임의 | Blender 에서 회전/스케일 정규화 후 FBX(cm, +X 정면, Z-up). **D12(OBJ Y 반전)는 OBJ 만** — FBX/GLB 는 Interchange 가 처리하나 바운드 min/max 로 정면 확인 |
| 라이선스 | 자체 | Meshy Free = CC BY 4.0(크레딧 표기), Pro $20 무표기 | 테스트는 Free 로. 채택 시 Pro 재생성 또는 표기 |

→ 솔직한 예측: **테스트 결과물은 게임에 그대로 못 들어간다**(로터·이미시브·격자 계약). 이 행의 가치는 "설계도 방식이 Meshy 에서도 부위 개수·비율을 지키는가"와 "복셀화 경로 (a)/(b) 중 어느 쪽이 쓸만한가"의 답이다. 쿼드 드론 경험: Tripo 텍스트 → 팔 4·발 생김(부위 개수 실패) / Gemini 재스타일 → 마스코트화(표정 실패) / 프리미티브 블록아웃 그림 → 프리미티브 모델(형태 정보 부족). **설계도가 곧 결과**다.

## §2 순서

1. **설계 확정(Claude+사용자)**: §3 초안을 함께 다듬는다. 부위 목록·비율·색·정면 정의를 **숫자로** 못박는다(아래 §3-0 스펙이 정본이 되도록).
2. **이미지 AI 설계도 생성(사용자)**: Gemini(Nano Banana 2) 1순위 · ChatGPT(GPT Image 2) 대안(`Docs/Research/…` §1-D 근거: 참조 구조 보존력). **한 장에 정면·측면·평면 3뷰**(같은 배율·같은 배경) 또는 3장 분리. 결과를 `Docs/Handoff/MeshyDrone/` 에 저장.
3. **설계도 검수(Claude)**: 부위 개수(프로펠러 1·기둥 1·코어 1·핀 1·팔 0·발 0), 3뷰 높이 일치, 시안/파랑 없음, 배경 단색. 틀리면 2 로 되돌린다(이미지 AI 에 "제거/유지" 지시 — 재스타일 요청은 금지, 표정·형태가 흐른다).
4. **Meshy(사용자)**: Image to 3D. 입력 = 설계도(다시점 지원 시 3뷰). 두 번 뽑는다 — (a) Art Style **Voxel** (b) 기본 스타일 + Remesh 3k. PBR/텍스처 옵션은 켜도 됨(테스트). export = **FBX + GLB** 둘 다. 파일도 `Docs/Handoff/MeshyDrone/`.
5. **정규화(Claude, Blender 헤드리스)**: `F:\Blender\blender.exe -b -P` — 임포트 → 스케일 cm(목표 60×45×45 근처, 사용자와 합의한 치수) → 정면 +X → 원점 중심 → (b) 갈래는 Remesh Blocks 7.5cm 추가 → FBX export(`apply_scale_options` 함정: 메모리 `fbx-metre-cm-armature-scale-trap` — 정적 메시라 아마추어 없음, 그래도 임포트 후 바운드로 확인).
6. **임포트(Claude, 헤드리스)**: `Scripts/import_meshy_drone.py`(신규, `import_voxel_drone.py` 복제·FBX 경로) + `.bat`. 로그 = sections·tris·bbox min/max.
7. **비교 렌더**: 쿼드 드론 옆에 놓은 Blender 렌더 3앵글(사용자 판정용) + 에디터 육안(사용자).
8. **판정 기록(§6)** → 보드 로그 → `Docs/WorkLog.md`.

## §3 설계도 프롬프트 초안 (사용자와 함께 확정할 것)

### §3-0 대상 스펙(정본 — 프롬프트는 이걸 그림으로 옮기는 것)
- **정체**: 원거리 소형 드론(초단사거리 돌진형 쿼드 드론의 원거리 짝). 작고 가벼운 정찰기 느낌. 적 = 뜨거운 색 대역.
- **부위(정확히 이것뿐)**: 상자형 몸통 1 · 정면 스크린(어두운 프레임 안 **주황 코어 1개 = 약점**) · 몸통 위 짧은 기둥(허브) 1 · 그 위 **프로펠러 바 1개(2엽, 넓적한 막대)** · 뒤쪽 작은 꼬리 핀 1 · 밑면 라이트 2~4. **팔 없음·다리 없음·바퀴 없음·카메라 짐벌 없음·프로펠러 2개 이상 금지.**
- **비율**: 몸통 폭 : 깊이 : 높이 = 6 : 6 : 4. 프로펠러 바 길이 = 몸통 폭 × 1.3, 두께 얇게. 기둥 높이 = 몸통 높이 × 0.25. 전체 높이 = 몸통 + 기둥 + 바.
- **색**(ArtDirection §A·§B-10 번역표): 몸통 어두운 보라 `#3A2748`, 옆·밑면 더 어둡게 `#2A1E36`, 프로펠러 밝은 자주 `#B34A70`, 코어 주황 `#FF6B2C`(발광), 라이트 빨강 `#FF3B4E`. **시안·파랑·하늘색 절대 금지**(아군 예약색).
- **스타일**: 레트로 아케이드 **복셀**(3D 픽셀, 격자 하나, 모서리 각짐), 텍스처·노이즈·사진질감 없음, 플랫 컬러 + 얇은 어두운 격자선.
- **설계도 형식**: 정면(코어가 보이는 면)·측면·평면 3뷰, 직교, 같은 배율, 같은 높이 정렬, 회색 단색 배경(#7F7F7F 근처), 그림자 없음, 글자·치수선·화살표 없음.

### §3-1 이미지 AI 프롬프트 초안 — 한 장 3뷰
**한국어**
> 게임 에셋 설계도, 직교 3면도(정면·측면·평면)를 한 장에 나란히, 같은 배율과 같은 높이 정렬, 회색 단색 배경, 그림자 없음, 글자·치수선 없음.
> 대상: 레트로 아케이드 복셀(3D 픽셀) 스타일의 작은 정찰 드론. 상자형 몸통(폭:깊이:높이 = 6:6:4), 정면에 어두운 사각 프레임과 그 안의 주황색 발광 코어 하나, 몸통 위 짧은 기둥 하나, 그 위에 넓적한 2엽 프로펠러 막대 하나(몸통 폭보다 조금 길고 얇음), 뒤쪽에 작은 꼬리 핀, 밑면에 작은 빨간 라이트.
> 색: 몸통 어두운 보라, 프로펠러 밝은 자주, 코어 주황, 라이트 빨강. 시안·파랑 계열 금지.
> 표면은 각진 복셀 격자, 플랫 컬러, 텍스처·노이즈·그라데이션 없음. 팔·다리·바퀴·카메라·추가 프로펠러 없음. 단일 오브젝트.

**영어**
> Game asset blueprint sheet: three orthographic views (front, side, top) side by side on one canvas, same scale, aligned to the same height, flat mid-gray background, no shadows, no text, no dimension lines.
> Subject: a small scout drone in retro arcade voxel (3D pixel) style. Box body (width:depth:height = 6:6:4), a dark square frame on the front face with a single glowing orange core inside, one short pillar on top of the body, one wide two-blade propeller bar on the pillar (slightly longer than the body width, thin), a small tail fin at the back, small red lights on the underside.
> Colors: body dark violet, propeller bright magenta-pink, core orange, lights red. No cyan, no blue.
> Blocky voxel surfaces with a single grid, flat colors, no texture, no noise, no gradients. No arms, no legs, no wheels, no camera gimbal, no extra propellers. Single object.

### §3-2 판정 체크(설계도 단계)
- [ ] 3뷰 배율·높이 일치 · [ ] 프로펠러 1개(2엽) · 기둥 1 · 코어 1 · 핀 1 · [ ] 팔·다리·바퀴 0 · [ ] 시안/파랑 픽셀 0 · [ ] 배경 단색·글자 없음 · [ ] 정면 뷰에서 코어가 중앙.
- 실패 시 지시 예: *"이 그림에서 X 만 제거하고 나머지는 한 픽셀도 바꾸지 마라"* (제거·유지 편집이 이미지 AI 가 가장 잘하는 종류, 재스타일 요청은 형태를 흐린다).

## §4 임포트·정규화 절차(Claude)
- Blender 헤드리스 정규화 스크립트 `Scripts/normalize_meshy_mesh.py`(신규): GLB/FBX 임포트 → 바운드 측정 → 균일 스케일(목표 폭 60cm) → 정면 판정(코어 텍스처/색으로 앞면 찾기 — 자동이 어려우면 사용자에게 "정면이 어느 축인가" 묻기) → +X 로 회전 → 원점 = 바운드 중심 → (b) 갈래 Remesh Blocks 7.5 → FBX(cm, Z-up).
- 임포트: `Scripts/import_meshy_drone.py` + `Scripts/run_meshy_drone_import.bat`(ASCII). 결과 = `/Game/Assets/Characters/EnemyVoxel/SM_EnemyMeshy_DroneRanged_{voxel|remesh}`.
- 검증 로그: `sections`, `tris`, `bbox min/max`(정면 +X 확인 = max.x 쪽에 코어).

## §5 판정 기준(사용자 + Claude)
- 설계도 충실도(부위 개수·비율) / 복셀 룩(격자 일관성, (a) vs (b)) / 실루엣 가독(6~10m 거리 렌더) / 폴리 / 소요 시간·크레딧 / 사람 손 개입 횟수.
- 비교 기준선 = 절차 생성 소형 드론(폐기했지만 렌더 기록은 `Docs/WorkLog.md` 2026-09-07 항목에 수치로: 60×45×45cm, 456 tris, 생성 ~1분).

## §6 결과 표(새 세션이 채운다)
| 항목 | (a) Meshy Voxel 스타일 | (b) 저폴리 → Remesh Blocks | 절차 생성(기준) |
|---|---|---|---|
| 부위 개수 정확 | | | ✅ |
| 격자 일관(7.5cm) | | | ✅ |
| tris | | | 456 |
| 단일 섹션 | | | ✅ |
| 로터 회전 가능 | | | ✅(UV 계약) |
| 코어 이미시브 | | | ✅ |
| 소요(사람 시간) | | | ~5분 |
| 판정 | | | |

## §7 범위 밖
- 채택 시 필요한 후속(별도 행): 요소ID 태깅 스크립트(파트 → UV.u 타일), 텍스처 → 색 LUT 재저작, 로터 WPO 계약 이식, `BP_EnemyRangedBase` 교체(오버라이드 리셋·Pitch −90→0·Absolute Rotation 해제 — 쿼드 드론과 같은 함정 3종).
- 컨셉 시트 갱신은 채택 이후.

## §8 함정 요약(이미 밟은 것)
- 헤드리스 에디터 = `.bat`+절대경로+try/finally, 에디터 닫고(D11) · OBJ 임포터 Y·V 반전(D12/D14, FBX 는 별도 확인) · BP 컴포넌트 오버라이드/상대회전/절대회전 잔존(`DroneEnemy_ResumePrompt.md` §0-A) · 라이브 에디터에 파이썬 임포트 = 데드락.
