# 외부 모델링 AI·툴 조사 — 복셀 적 메시를 더 높은 품질로 만들고 프로젝트로 가져오는 길 (2026-09-06)

> 배경: 복셀 유령 "쩝쩝이"(`a2f06fb6`)는 파이썬 절차 생성으로 직접 만들었다. 사용자 요청 = 전문 모델링 AI/툴로 품질을 올리고
> 그 결과를 현 프로젝트(단일 섹션 · 요소ID WPO · 스웜 예산)에 넣는 방법 조사. **조사만** — 계정 생성·결제는 사용자 몫.
> 로컬 GPU 실측: RTX 4070 Ti **12GB** (torch 미설치).

## 0. 이 프로젝트가 외부 메시에 요구하는 것 (통합 비용의 뿌리)
| 제약 | 근거 | 외부 툴 산출물과의 충돌 |
|---|---|---|
| 단일 섹션 · 공유 머티리얼 1개 | ADR 0007 다이나믹 인스턴싱 병합 | AI 메시는 텍스처 1세트라 OK. `usemtl` 여러 개면 섹션이 갈라짐 |
| **요소 ID(`floor(UV.u)`)** 로 부위 구분 → WPO 모션(턱 개폐·치마 물결·시선) | `gen_voxel_chomper.py` 계약 | AI 메시는 **모노블록** — 부위 정보 없음. 이게 가장 큰 갭 |
| 닫힌 입 안쪽(이빨·혀·코어)이 **지오메트리로 미리** 존재 | 평행이동 WPO 만으로 입 속이 보여야 함 | AI 는 보이는 표면만 만든다 — 입 속은 어떤 툴도 안 만들어 준다 |
| 트라이 예산(적 ~300 동시, 파일럿 4.6k) | Performance §5 | AI 원본 10k~100k+ → 리메시/데시메이션 필수 |
| 복셀 룩(격자가 보이는 크리스프 면) | 2026-09-03 아트 결정 | 일반 AI 메시는 유기적 곡면 → 복셀화 단계 필요 |
| Y 반전(OBJ 임포터) | Troubleshooting D12 | 비대칭 디테일은 임포트 후 좌우가 뒤집힌다 |

→ 결론 먼저: **어떤 툴을 쓰든 "형태"만 얻고, 요소ID·입 속·격자는 우리 쪽 변환기가 붙여야 한다.** 그 변환기의 가장 자연스러운 입력 = **`.vox`(MagicaVoxel 포맷)** — 팔레트 색 인덱스가 곧 요소 ID 가 될 수 있다.

## 1. 툴 지형도

### 1-A. 범용 AI 3D 생성(텍스트/이미지 → 메시) — 클라우드
| 툴 | 강점 | 게임/이 프로젝트 관련 기능 | 가격·라이선스 |
|---|---|---|---|
| **Tripo AI** | 게임 에셋 1순위 평가, 생성 ~8초, 쿼드 토폴로지 | **Segmentation v2**: 모노블록을 3~6 / 15+ 파트로 자동 분할·**파트별 export**(→ 요소ID 소스!) · **Stylize: voxel/LEGO/Voronoi**(`block_size` 파라미터, API `stylize_model`) · GLB/FBX/OBJ/STL | Free 200cr ≈ 8모델, **공개 + 비상업**. Pro $19.9/월부터 상업 사용. API 별도 과금 |
| **Meshy** | 대중적, 반복 빠름 | 아트 스타일에 **Voxel 프리셋** · Remesh 100면~100k, 쿼드/트라이 선택 · GLB/FBX/OBJ/USDZ/STL/BLEND | Free 100cr/월 — **CC BY 4.0(상업 가능, Meshy 크레딧 표기 필수)**. Pro $20/월 1,000cr, 무표기·비공개 |
| **Rodin(Hyper3D)** | 초고해상(500k tris), 사실적 인간 | 이 프로젝트 방향(복셀·저폴리)과 반대 | $0.3~0.4/생성 |
| Sloyd / Luma Genie | 저폴리 프리셋 / 무료 빠른 변형 | 스타일 제한 | 30 export 무료 / 무료 |

### 1-B. 복셀 네이티브 AI (.vox 를 바로 낸다)
| 툴 | 무엇 | 출력 | 가격·라이선스 |
|---|---|---|---|
| **VoxAI (voxelai.ai)** | 텍스트/이미지 → 복셀 모델, 온라인 .vox 편집기·변환기 포함 | **.vox / .glb / .obj / .json**(+FBX 변환), 다운로드 무제한(생성만 크레딧) | Free 티어 有. Pro **$14/월 800cr**(3D 생성 25cr → ~32개/월), Premium $49, Studio $199. **전 티어 상업 사용·소유권** |
| Scenario "Voxel Crafter 1.0" | 텍스트/이미지 → 블록 모델, 격자 16~256 | 출력 포맷 페이지에 미기재(3D 메시인지 확인 필요) | $15~125/월 |
| SupaVoxel | 로그인 없는 빠른 복셀 생성 | GLB/OBJ | 무료 |
| Meshy Voxel 프리셋 · Tripo Stylize(voxel) | 일반 메시를 복셀 스타일로 | GLB/OBJ (.vox 아님) | 위 표 |

### 1-C. 오픈소스 로컬 모델 — **이 머신에선 사실상 불가**
| 모델 | VRAM | 라이선스 | 판정 |
|---|---|---|---|
| Hunyuan3D 2.1 | 형상 10GB / 텍스처 21GB / 전체 29GB, Linux·CUDA 12.4 | Community License — **EU·영국·대한민국 제외 지역에서만 허용** | 🔴 **한국에서 사용 불가**(라이선스). 기술적으로도 12GB 로는 형상만 겨우 |
| TRELLIS(MS) | 16GB 최소, Linux | MIT | 🔴 12GB 미달 |
| TripoSR | ~6GB, CPU 폴백 | MIT | 🟡 돌아가지만 버텍스 컬러만·품질 낮음 — 형태 스케치 용도 정도 |

### 1-D. 수작업 복셀 에디터(무료) + UE 연결
| 툴 | 역할 | 비고 |
|---|---|---|
| **MagicaVoxel** | 무료 복셀 에디터의 표준. `.vox` 저작, OBJ(+MTL+PNG 팔레트)·PLY 등 9종 export | 외부 메시 임포트는 **FileToVox**(무료 CLI, obj→vox) 경유. 최신 0.99.7 은 포맷 변경 有 |
| **Blockbench** | 무료 저폴리/큐브 에디터, glTF/OBJ export | 마인크래프트 계보. UE 는 Blender 경유 권장 |
| **VOX4U**(UE 플러그인, MIT) | `.vox` 드래그 → StaticMesh(monotone decomposition 으로 면 병합) | UE 5.1+ 명시(5.7 미검증), **MagicaVoxel ≤0.99.6.4 포맷만** |
| Blender Decimate(Planar) / Avoyd | 복셀 메시 면 병합 최적화 → glTF | 우리 격자선(UV frac)은 면 병합 시 깨짐 — 병합은 어두운 면에만 |

## 2. 파이프라인 후보와 실제 통합 비용

### 안 A (권장) — "형태는 툴, 계약은 우리": `.vox` → 기존 생성기 계약으로 변환
```
[Tripo/Meshy/VoxAI/손 저작] → .vox (VoxAI 직접 / MagicaVoxel / FileToVox(glb→vox))
   → Scripts/vox2chomper.py (신규, ~200줄): .vox 파싱 → 셀 dict → 팔레트 인덱스 → 요소ID·그룹
   → 기존 extract_faces(그룹 경계면 포함) → OBJ → run_voxel_chomper_pipeline.bat (임포트·머티리얼 그대로)
```
- **팔레트 규약이 곧 요소 계약**: 팔레트 슬롯 1=머리, 2=턱, 3=흰자 … 12=턱쪽 어두운면. 저작자가 MagicaVoxel 에서 그 색으로 칠하면 끝.
  입 속(이빨·혀·코어)도 그냥 복셀로 칠해 넣으면 우리 추출기가 **그룹 경계면을 자동으로 살려** 닫힘 밀봉/열림 노출이 성립한다.
- 비용: 변환기 1개(.vox 포맷은 단순 청크 포맷, 서드파티 라이브러리 불필요) + 팔레트 규약 문서. **머티리얼·임포트·검증 전부 재사용**.
- 품질 향상 지점: 형태·실루엣·디테일을 **사람 눈으로 다듬는 에디터 루프**(MagicaVoxel)가 생긴다. AI 는 초안 생성기로.
- 한계: 해상도가 곧 트라이 수. 32³ 급이면 파일럿(16×22) 대비 3~5배 → LOD 또는 `VOXEL` 상향으로 상쇄.

### 안 B — AI 메시를 그대로(비복셀 저폴리) + Tripo Segmentation 으로 요소ID
```
Tripo 이미지→3D → Remesh 3k tri → Segmentation v2(파트 export) → Blender 스크립트: 파트별 UV.u += 요소ID → 단일 머티리얼 OBJ
```
- 장점: 유기적 실루엣, 툴 품질을 직접 쓴다. 단점: **복셀 아트 방향과 충돌**, 입 속 지오메트리는 Blender 에서 손 저작, 텍스처 기반이면 요소 색 LUT 와 이중 구조. 스타일라이즈(voxel)를 걸면 격자는 나오지만 격자선 UV 계약은 없다.
- 비용: Blender 스크립트 1개 + 파트↔요소 매핑 수작업 + 입 속 수작업. A 보다 크다.

### 안 C — 전량 수작업(MagicaVoxel) + 변환기(안 A 의 변환기 동일)
- AI 없이도 안 A 의 변환기가 있으면 성립. AI 는 "레퍼런스 이미지 생성"(2D)만 써도 충분할 수 있다.

### 판정
- **안 A 를 추천.** 이유 3줄: ① 제1원리(스웜 예산·요소ID WPO·닫힌 입 계약)를 변환기 한 곳에서 지킨다 ② 기존 인프라(임포트·머티리얼·검증) 무변경 ③ 어떤 소스(AI 3종·손 저작)든 `.vox` 로 수렴하므로 툴을 바꿔도 파이프라인이 안 바뀐다.
- 대안 B 는 아트 방향이 복셀에서 벗어날 때만.

## 3. 라이선스 요약(상업 배포 기준)
- Meshy Free = CC BY 4.0 → **크레딧 표기하면 상업 가능**, 표기 없이 쓰려면 Pro($20). Tripo Free = **비상업** → Pro($19.9) 필수. VoxAI = **전 티어 상업 OK**.
- Hunyuan3D = **한국 사용 불가**(라이선스 지역 제외). TRELLIS/TripoSR = MIT.
- 생성물 저작권 귀속은 각 사 ToS 기준 — 출시 전 재확인.

## 4. 다음 단계 제안
1. `Scripts/vox2chomper.py` 작성(안 A 변환기) + 팔레트 규약 → 기존 쩝쩝이를 `.vox` 로 역변환해 왕복 검증(대조군).
2. 사용자: VoxAI(무료 티어) 또는 Meshy(Voxel 프리셋, 무료)로 "데포르메 유령 + 입" 초안 5~10개 생성 → 마음에 드는 실루엣 선택.
3. MagicaVoxel 에서 팔레트 규약대로 부위 칠하기 + 입 속 복셀 추가 → 변환기 → 임포트 → `JawOpen` 확인.

## 출처
- [Best AI 3D Model Generators 2026 (Medium)](https://medium.com/ideas-with-wings/best-ai-3d-model-generators-in-2026-tripo-ai-vs-meshy-rodin-kaedim-and-more-7eea7b05eb11) · [RapidDirect 8 tools compared](https://www.rapiddirect.com/blog/best-8-ai-3d-model-generators/) · [TRELLIS vs Meshy vs Tripo](https://trellis2.app/blog/best-ai-3d-model-generator)
- [Meshy pricing](https://www.meshy.ai/pricing) · [Meshy free plan](https://help.meshy.ai/en/articles/15696428-what-is-included-on-the-free-plan) · [Meshy commercial use](https://help.meshy.ai/en/articles/16102098-can-i-use-meshy-assets-commercially) · [Meshy low-poly](https://www.meshy.ai/features/low-poly) · [Meshy text-to-3D styles](https://help.meshy.ai/en/articles/9996858-how-to-use-meshy-text-to-3d)
- [Tripo pricing](https://costbench.com/software/ai-3d-generation/tripo-ai/) · [Tripo license guide](https://www.tripo3d.ai/game-development/3d-assets-license-game-development) · [Tripo Segmentation v2](https://www.tripo3d.ai/blog/tripo-segmentation-v2) · [Tripo stylization](https://www.tripo3d.ai/features/ai-model-stylization) · [Tripo Python SDK API](https://github.com/VAST-AI-Research/tripo-python-sdk/blob/master/docs/API.md)
- [VoxAI](https://www.voxelai.ai/) · [VoxAI export formats](https://www.voxelai.ai/docs/features/export-formats) · [Voxel AI reviews/pricing](https://www.ai.cc/app/voxel-ai/) · [Rosebud voxel AI tool stack 2026](https://lab.rosebud.ai/blog/ai-tool-stack-voxel-games-2026) · [Scenario Voxel Crafter](https://www.scenario.com/models/voxel-crafter-10)
- [Hunyuan3D vs TRELLIS vs TripoSR](https://triposr.org/blog/hunyuan3d-vs-trellis) · [Hunyuan3D-2.1 LICENSE](https://github.com/Tencent-Hunyuan/Hunyuan3D-2.1/blob/main/LICENSE) · [HN: EU/UK/South Korea exclusion](https://news.ycombinator.com/item?id=42786403)
- [VOX4U UE plugin](https://github.com/mik14a/VOX4U) · [MagicaVoxel export](https://www.megavoxels.com/learn/how-to-export-from-magicavoxel/) · [MagicaVoxel importing models (FileToVox)](https://thebitcave.gitbook.io/magicavoxel-resources/exporting-models/importing-3d-models) · [Optimising voxel meshes in Blender](https://www.enkisoftware.com/devlogpost-20230918-1-Optimising-Voxel-Meshes-for-Games-Using-Blender) · [Blockbench guide](https://godotawesome.com/blockbench-complete-guide-godot/)
