# AssetWork 환경(창고) — 재개 노트 (새 세션용)

> **목적**: VibeUE가 에디터 재시작 후 이전 세션에 재연결 안 됨 → **에디터 열린 채 새 Claude 세션 시작**으로 전환. 이 노트로 새 세션이 즉시 이어간다.
> **작성**: 2026-06-22. **브랜치**: `content/character-environment`(BroBot 커밋 `41866d1` 위). 원 작업 지시: 사용자 — "ZerinLabs_lowpolyPack_SciFi를 Assets로 이동 + L_Sandbox를 거대한 창고처럼 직사각형 블록아웃(형태만)".

## 🔁 재시작 절차
1. (완료) 사용자가 에디터 재시작 + Fab로 `ZerinLabs_lowpolyPack_SciFi` 재임포트 → `/Game/ZerinLabs_lowpolyPack_SciFi`(Content 루트).
2. **에디터 열린 채 새 Claude 세션 시작** → VibeUE-Claude(127.0.0.1:8088) 자동 연결.
3. 새 세션: 이 노트 + `Game.md`/`PROGRESS.md` 읽고 아래 STEP 1부터.

## 📦 현재 상태 (디스크 확인됨)
- 팩 = `/Game/ZerinLabs_lowpolyPack_SciFi` : **124 uasset(전부 정상 reals) + 2 umap, ~103 고유 메시**. 클린·완전(재임포트 직후).
- 서브폴더: `Materials`(16: MI_col_A/B/C/D·MI_outbound 등) · `Meshes/Struc`·`Meshes/Decos` · `Textures`(3) · `Scenes`(4 = 데모 `SCN_sciFiPack_demoScene`·`SCN_sciFiPack_pieces` umap + 각 BuiltData).
- `Content/Assets/Environment/` = 빈 디렉터리(이전 클린업). 여기로 이동.
- **BroBot(1차) PIE는 아직 미검증** — 사용자에게 권유만 한 상태(적 빨간BroBot walk·플레이어 3P). 별개.

## ⚠️ 재배치 함정 (반드시 준수) — [[marketplace-asset-import-relocate]]
- **`EditorAssetLibrary.rename_directory`로 이 팩 전체를 한 번에 옮기지 말 것.** 이전 세션서 124-asset 팩에 bulk rename_directory 했다가 **부분실패**(리다이렉터 스텁 다수 + 레지스트리 손상, 28s 타임아웃) → 디스크/레지스트리 불일치로 복구에 재임포트까지 필요했음.
- **안전법 = per-asset `rename_asset`을 ≤40개 배치로**(서브폴더별 또는 청크). 각 청크 후 `does_asset_exist`로 검증. 신선한 세션(클린 레지스트리)이라 per-asset은 안정적. 머티리얼/텍스처 먼저(피참조), 메시 나중.
- 타임아웃 시 디스크 폴링. 부분실패 의심 시 디스크 파일크기(>5KB=real, ~1.5KB=리다이렉터)로 판별.

## 🛠️ STEP 1 — 재배치 + 트림 (안전 배치)
1. `/Game/Assets/Environment` 존재 확인(있음).
2. per-asset rename, 청크로: `Materials/*`(16) → `Textures/*`(3) → `Meshes/Struc/*` → `Meshes/Decos/*`. dst = src.replace("/Game/ZerinLabs_lowpolyPack_SciFi", "/Game/Assets/Environment/ZerinLabs_lowpolyPack_SciFi"). 각 청크 후 카운트 검증.
3. **트림**: 데모 Scenes 4종 제거(`SCN_sciFiPack_demoScene`·`_pieces` + BuiltData) — 이동 전 OLD에서 지우거나 이동 후 NEW에서. (메시 참조자가 데모뿐이라 트림이 고아 정리도 됨.)
4. 구 디렉터리 `/Game/ZerinLabs_lowpolyPack_SciFi` 잔여(리다이렉터) 정리 + `fixup_redirectors`.
5. 검증: NEW 카운트 ≈ 124(트림 후 ~122), 샘플 메시 load + 머티리얼 참조 무결(MI_col_*).

## 🛠️ STEP 2 — L_Sandbox 창고 블록아웃 (게임플레이 액터 전부 보존)
**측정값(확인됨)**:
- `Floor` StaticMeshActor: loc(0,0,0) scale(5,5,20) → **5000×5000×1000, 윗면 z=0**(보스/적 스폰 높이 기준 — **z=0 유지 필수**). `Floor4`(0,0,-1000, 100000² backdrop) 건드리지 말 것.
- 적 스폰 4개 `BP_EnemySpawnPoint`: (±2310,±230,400)·(±10/80,±2330,400) — **플레이 영역 ±2330 z400**. `BossSpawnPoint`(0,0,200). 미션스폰 5개(넓게 ±10000 z-1000). `PlayerStart` 1.
- 보존: 위 전부 + 조명(DirectionalLight·SkyAtmosphere·SkyLight·ExponentialHeightFog·VolumetricCloud) + Brush 4.
- 벽 메시: `SM_wallSingle_A` = 110(두께X)×200(폭Y)×300(높)·**base z=0**(바닥에 바로 섬). `SM_wallDouble_A/B` = 110×400×300. (angle/코너 `SM_wallAngleStraightIn` 300³.) 기본 방향 = Y로 뻗는 벽(폭이 Y축), 면 법선 ±X.
- 바닥타일: `SM_floorTile_A_single` 100×100, `_double` 200×200(×13.5 두께). 천장: `SM_ceiling_closed`(평면 솔리드)·`SM_ceiling_angle` 300×300×75.
- 환경 제약(§P7/§6): **바닥=Mobility Static + Collision Block(ECC_WorldStatic)** 필수(적 지면트레이스가 WorldStatic만 쿼리). 벽=Static+Block(솔리드). 바닥에 WorldDynamic 추가 금지.

**플랜**:
- 직사각형 **8000(X)×5000(Y)** 중심 원점, 바닥 윗면 z=0.
- **바닥**: 기존 `Floor` 유지+X스케일 5→8(8000×5000, top z0 유지, 충돌 보존). 회색이라도 블록아웃 OK(벽이 창고감 정의). (원하면 SciFi 바닥타일 후속.)
- **벽**: `SM_wallDouble_A`(400폭) 둘레 타일. Y변(X=±4000, Y로 뻗음): 기본방향, Y=-2500..+2500 = 5000/400≈13개/변. X변(Y=±2500, X로 뻗음): yaw90, X=-4000..+4000 = 8000/400=20개/변. **~66개/층**. 높이 ~600(2층 z=0,300 스택) 또는 z-scale로 ~750~900(1층). 안쪽 향하게 회전. Static+Block.
  - ⚠️ 새 세션서 벽 **XY 피벗** 먼저 확인(중심 vs 코너) 후 타일 간격/오프셋 계산. 면 방향(안쪽)은 배치 후 시각검증.
- 천장: 블록아웃은 오픈탑(생략) 또는 `SM_ceiling_closed` 높이 z~600+ 타일. FPS 가시성 고려해 일단 생략 권장.
- ⚠️ MCP로 액터 다수 배치 후 PIE 전 에디터 재시작(World Leak).

## 🛠️ STEP 3 — 검증 + 커밋
- 게임플레이 액터 무손상(스폰/보스/조명/PlayerStart 카운트·위치 동일) · 바닥 z=0·충돌 · 벽이 ±2330 플레이영역 포위 · 스폰이 벽 안쪽.
- 커밋 `content(env): ZerinLabs SciFi 팩 Assets 이동 + L_Sandbox 창고 블록아웃`. LFS 포인터·BuiltData 0 확인. **스푸리어스 `L_MainMenu.umap`(이전부터 modified)·`Docs/reviews`·`*.localbak` 커밋 제외.**
- PROGRESS 갱신.

## 재사용 레시피
- 상속 컴포넌트 배선·VAT = [[vat-bake-inherited-component-wiring]]. VibeUE 능력/한계 = [[vibeue-mcp-capabilities]].
