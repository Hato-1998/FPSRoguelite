# AssetWork CharEnv — 재개 노트 (세션 재시작용)

> **목적**: VibeUE MCP가 이 세션에 안 붙어(에디터 닫힌 채 세션 시작) **에디터+Claude 세션 재시작**으로 전환. 이 노트 하나로 새 세션이 즉시 STEP 1부터 이어간다. 원본 작업 지시 = `Docs/AssetWork_CharEnv_Prompt.md`.
> **작성**: 2026-06-21. **브랜치**: `content/character-environment` (bd44939 베이스). `fix/w1-audit-corrections`는 절대 미접촉.

## 🔁 재시작 절차 (사용자)
1. (완료) `.uproject`에 **AnimToTexture 플러그인 활성**됨 — STEP 0.
2. **에디터 재시작** → AnimToTexture 로드(코드 모듈이라 재시작 필수). 빌드 프롬프트 뜨면 컴파일(엔진 소스빌드+precompiled 존재라 보통 즉시).
3. **Claude 세션을 에디터 열린 채 새로 시작** → VibeUE-Claude(127.0.0.1:8088) 자동 연결.
4. 새 세션: 이 노트(`Docs/AssetWork_CharEnv_Resume.md`) + `Game.md`/`PROGRESS.md` 읽고 **STEP 1**부터.

## ✅ 결정 (locked)
- **순서**: ① 적+플레이어(공용 `BroBot`, 색/머티리얼만 차별화) → ② 보스 → ③ 환경.
- **적 스웜 = VAT**(AnimToTexture, **Bone 모드**=엔진 기본·스켈레톤 공유 시 권장). 워크 루프 머티리얼 자가재생(bAutoPlay) → **액터별 로직 0**, StaticMeshComponent 유지(§2-6 "인스턴싱/VAT" 의도 정합, 적500 perf 준수).
- **플레이어 3P = 풀 스켈레탈+AnimBP**(단일~4인이라 무관). 1P 팔(V0)은 별개 유지.
- **분담**: 사용자 임포트 / AI 재배치(루트→Content/Assets/)+트리밍+배선+LFS 커밋.
- **VibeUE 방식 채택**(사용자 결정) — Python 콘솔 대안 폐기.

## 📦 임포트 팩: `Content/BCC_01_BroBot/` (로봇 "BroBot", 자체 스켈레톤)
- **Mesh/**: `SK_BCC_01_BroBot`(스켈레탈) · `_Skeleton` · `_PhysicsAsset`
- **Animations/**: `Idle` `Walk` `Run` `Loop` `Jump` `Jump_Start` · `IdleRun_Blendspace` · `BCC_01_BroBot_AnimBlueprint`(자체 AnimBP)
- **Materials/**: `M_BCC_01_BroBot`(마스터) · `MI_BCC_01_BroBot_01`~`_10`(색 변형 10종) · `MF_BT_RGB_Masks01` · `MF_Height_To_Normal`
- **Textures/**: `T_BCC_01_Mask00/01/02` · `T_BCC_01_Normal` · `T_BCC_01_R_M_A`
- **Blueprints/**: `BP_BCC_01_BroBot`(데모 폰) · `GM_BCC_01_BroBot`(데모 GM — **트리밍**) · 위 MF 2종
- **maps/**: `Overview.umap`(+`Overview_BuiltData` — gitignore/트리밍) — 데모, 미사용

> 자체 스켈레톤이지만 **자체 AnimBP 동봉** → 플레이어 3P 리타겟 불요. VAT는 스켈레톤 무관 베이크.

## 🎯 배선 타깃 (C++ 컴포넌트 타입 — 소스 확인 완료)
| BP | 컴포넌트 | 타입 | 현재 | 비고 |
|---|---|---|---|---|
| `BP_FPSRPlayer` | `GetMesh()` | USkeletalMeshComponent | Manny | 스켈레탈+AnimBP, OwnerNoSee(소스). 플레이어 색 MI |
| `BP_LobbyDisplayPawn` | `BodyMesh` | **새 세션서 확인** | Manny | 포디움 표시(스켈레탈 추정). +`WeaponMeshStatic`(U11a 추가) |
| `BP_EnemyBase` | `Mesh` | UStaticMeshComponent | 엔진 큐브 | `FPSREnemyBase.cpp:32` NoCollision·RelLoc(0,0,-90)·Scale(0.8,0.8,1.8) → **실측 보정 필요**. VAT SM+적 VAT MI |
| `BP_Boss` | `BodyMesh` | UStaticMeshComponent | 큐브 | (보스=웨이브2, 별도) |

## 🛠️ 실행 플랜 (STEP 1~6, 새 세션)
**STEP 1 — 재배치/트리밍** (VibeUE `rename_directory` 참조보정)
- `/Game/BCC_01_BroBot/` → `/Game/Assets/Characters/BroBot/` (`Content/Assets/` 현재 비어있음 → 생성)
- 트리밍: `GM_BCC_01_BroBot`·`maps/Overview`(+BuiltData) 제거. **데모 BP(`BP_BCC_01_BroBot`)는 애니/머티리얼 프리뷰 참조 가능성 → 일단 보존**, 고아 확인 후 결정.
- 대용량 rename 타임아웃 시 디스크 폴링(메모리 marketplace-asset-import-relocate).

**STEP 2 — VAT 베이크** (AnimToTexture, Bone 모드)
1. `AnimToTextureBPLibrary.ConvertSkeletalMeshToStaticMesh(SK_BCC_01_BroBot, "/Game/Assets/Characters/BroBot/VAT/SM_BroBot_VAT", -1)` → 베이스 SM 생성.
2. 빈 Texture2D 3종 생성: `T_BroBot_BonePos` `T_BroBot_BoneRot` `T_BroBot_BoneWeight`(Bone 모드 텍스처).
3. `UAnimToTextureDataAsset`(`DA_BroBot_VAT`) 생성·설정: `SkeletalMesh`=SK · `StaticMesh`=SM_BroBot_VAT · `Mode`=Bone · 위 3텍스처 매핑 · `AnimSequences`=[{AnimSequence=Walk, bEnabled=true}] · `SampleRate`=30 · `UVChannel`=1 · `bAutoPlay`=true · `AnimationIndex`=0.
4. `AnimToTextureBPLibrary.AnimationToTexture(DA_BroBot_VAT)` → 베이크(텍스처 기록 + SM에 UV채널/바운드 주입).
5. **VAT 머티리얼**: 엔진 템플릿 복제 후 BroBot 표면 결합 →
   - 엔진 자산: `/AnimToTexture/Characters/Mannequin/Materials/VertexAnimation/M_Body_VertexAnimation`·`MI_Body_VertexAnimation`, `/AnimToTexture/Materials/MaterialFunctions/MF_VertexAnimation`, `/AnimToTexture/Materials/ML_VertexAnimation`(머티리얼 레이어), 예시 메시 `SM_Mannequin_VertexAnimation`, 예시 DA `DA_VertexAnimation`.
   - 신규 `M_BroBot_VAT` = BroBot 표면(마스크 틴트/Normal/R_M_A, `MF_BT_RGB_Masks01` 재사용) + `MF_VertexAnimation`(WorldPositionOffset+노멀). → `MI_BroBot_VAT_Enemy`(적 색).
   - `AnimToTextureBPLibrary.UpdateMaterialInstanceFromDataAsset(DA_BroBot_VAT, MI_BroBot_VAT_Enemy, association)` 로 정적스위치/파라미터 주입.
   - ⚠️ 새 세션서 엔진 예시(`MI_Body_VertexAnimation`)를 먼저 열어 파라미터/스위치 구조 확인 후 미러(추론 금지).

**STEP 3 — 적 배선**
- `BP_EnemyBase.Mesh`: StaticMesh=`SM_BroBot_VAT`, Material[0]=`MI_BroBot_VAT_Enemy`. NoCollision 유지.
- 큐브용 RelLoc(0,0,-90)/Scale(0.8,0.8,1.8) → 실측 보정(캡슐 half-height 기준 발바닥 정렬, 스케일 1.0 기준 재조정).

**STEP 4 — 플레이어 3P 배선**
- `BP_FPSRPlayer` 메시: SkeletalMesh=`SK_BCC_01_BroBot`, AnimClass=`BCC_01_BroBot_AnimBlueprint`, Material=플레이어 색 MI(예 `MI_..._02`). **AnimBP가 폰 속도로 로코모션 구동되는지 검증**(안 되면 IdleRun_Blendspace로 미니 AnimBP).
- `BP_LobbyDisplayPawn.BodyMesh` 동일(타입 확인 후). 포디움 idle 표시.

**STEP 5 — 검증** (에디터 재시작으로 World Leak 방지 후 PIE)
- 적: VAT 워크 재생 · 이동/분리/플로우필드 무회귀 · 지면 안 빠짐 · 약점(U3a) 무관.
- 플레이어: 3P 타인뷰 표시+애니, 1P 팔 정상.
- 로비: 포디움 BroBot 표시.

**STEP 6 — 커밋** `content(char): BroBot 적 VAT + 플레이어 3P 배선 (Manny 플레이스홀더 교체)`
- LFS 포인터 확인(uasset/umap) · BuiltData 0개 · `.uproject`(AnimToTexture)는 `chore`로 분리 or 동반.
- PROGRESS.md 갱신(플레이스홀더 인벤토리 §8: 적 큐브·3P 바디 → 교체 기록).

## 🔑 AnimToTexture API (엔진 소스 확인 — `D:\UnrealEngine\UE_5.7\Engine\Plugins\Experimental\AnimToTexture`)
- `UAnimToTextureBPLibrary`(WITH_EDITOR): `AnimationToTexture(DataAsset)→bool` · `ConvertSkeletalMeshToStaticMesh(SK, PackageName, LODIndex=-1)→UStaticMesh*` · `SetLightMapIndex(SM, LOD, LightmapIndex=1, bGenLightmapUVs=true)` · `UpdateMaterialInstanceFromDataAsset(DA, MIC, association)`.
- `UAnimToTextureDataAsset`(UPrimaryDataAsset): `SkeletalMesh`/`SkeletalLODIndex` · `StaticMesh`/`StaticLODIndex` · `UVChannel`(def1) · `NumDriverTriangles`(def10)/`Sigma` · `MaxHeight/MaxWidth`(4096)/`bEnforcePowerOfTwo` · `Precision`(8/16bit) · **`Mode`(Vertex/Bone, def Bone)** · Vertex텍스처 2종 / Bone텍스처 3종(`BonePositionTexture`/`BoneRotationTexture`/`BoneWeightTexture`) · `RootTransform`/`AttachToSocket` · `SampleRate`(30) · `AnimSequences[]`(FAnimToTextureAnimSequenceInfo{bEnabled,AnimSequence,bUseCustomRange,Start/EndFrame}) · `bAutoPlay`/`AnimationIndex`/`Frame` · `NumBoneInfluences`(One/Two/Four, def Four).
- Python: `unreal.AnimToTextureBPLibrary.*`(snake_case), `unreal.AnimToTextureDataAsset`, `unreal.AnimToTextureMode.BONE`, `unreal.AnimToTextureAnimSequenceInfo`.
- ⚠️ Experimental 플러그인 — 콘텐츠 비주얼용이라 수용. 코어 게임플레이엔 미사용.

## 함정 (메모리)
- 임포트=에디터 닫고(BuiltData) / VibeUE=에디터 열림+세션시작시 연결 / BP 반복재컴파일 후 PIE 전 에디터 재시작(World Leak) / 디스크 mv 금지(rename_directory) / 트리밍 Demo·Mannequin 애니참조 보존 / 고아=디스크 rm+재시작 / LFS·BuiltData gitignore 확인.
