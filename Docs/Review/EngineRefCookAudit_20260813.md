# `/Engine/` 참조 쿡 생존 감사 — 2026-08-13

> **역할**: `Roadmap.md` §7-6 **M0 (d)** 의 산출물이자 **M0 Exit Criteria ③** 의 근거 정본.
> **범위**: `Content/` 전체(`.uasset`·`.umap`) + `Source/` + `Config/`. `Plugins/` 는 범위 밖.
> **판정 주체**: 사용자(§7-6 EC 판정 주체). 이 문서는 판정에 필요한 사실과 근거만 싣는다.

---

## 0. 한 줄 결론

**현 설정에서 쿡 탈락은 0건이다.** UE 5.7의 엔진 콘텐츠 쿡 제외는 `/Engine/Editor*`·`/Engine/VREditor` **두 접두사뿐**이고, 그 게이트인 `bSkipEditorContent` 가 **엔진 기본값 `false`** 이며 **프로젝트가 오버라이드하지 않는다.**
다만 **조건부 취약 2건**(에디터 전용 디렉터리를 *가드 없는 런타임 UPROPERTY* 로 하드 참조)이 있고, 그중 **1건은 로드맵에 등록돼 있지 않았다** → M2 등록.

| | 건수 |
|---|---|
| 실참조 고유 `/Engine/` 경로 | **112** (원시 686 − FBX 임포터 트랜지언트 574) |
| ⚠️ 조건부 취약 (에디터 전용 · 가드 없는 런타임 참조) | **2** |
| 에디터 전용이나 무해 (엔진이 스스로 차단) | 5 |
| 런타임 쿡 대상 | 101 |
| 패키지 참조가 아님 (Slate loose 리소스) | 4 |
| `Source/` C++ 하드코딩 `/Engine/` 경로 | **0** |
| 현재 실제 쿡 탈락 | **0** |

---

## 1. 감사 가능성 — LFS 실체 판정

`Content/` 는 LFS 관리 대상이라 **포인터 파일만 받은 클론에서는 감사가 성립하지 않는다**(grep이 조용히 0건을 낸다). 먼저 실체 여부를 확인했다.

| 항목 | 값 |
|---|---|
| `Content/` 내 `.uasset`/`.umap` | **4,314** |
| `git lfs ls-files` 등록 | 4,314 (전건) |
| 실체 다운로드됨 (`*` 플래그) | **4,314 / 4,314** |
| 포인터만 (`-` 플래그) | **0** |
| 200바이트 미만 파일 | **0** |

→ **감사 불가 항목 없음.** 전수 grep이 성립한다.

---

## 2. 판정 근거 — 엔진 실물 (추론 아님)

### 2-1. 쿡 제외는 두 접두사뿐이다

`D:\UnrealEngine\UE_5.7\Engine\Source\Editor\UnrealEd\Private\Cooker\CookSavePackage.cpp:292`

```cpp
// Don't save Editor resources from the Engine if the target doesn't have editoronly data.
else if (COTFS.IsCookFlagSet(ECookInitializationFlags::SkipEditorContent) &&
    (PackageName.StartsWith(TEXT("/Engine/Editor")) || PackageName.StartsWith(TEXT("/Engine/VREditor"))) &&
    !TargetPlatform->HasEditorOnlyData())
{
    SavePackageResult = ESavePackageResult::ContainsEditorOnlyData;
    const TCHAR* RejectedReason = TEXT("EngineEditorContent");
```

- 게이트 플래그 출처: `CookCommandlet.cpp` → `bSkipEditorContent` → UAT `-SkipCookingEditorContent`(`TurnkeySupportModule.cpp`)
- **`Engine/Config/BaseGame.ini:113` → `bSkipEditorContent=false`**
- 프로젝트 `Config/`·`Build/`·`Scripts/` 전체에 `bSkipEditorContent`/`SkipCookingEditorContent` 오버라이드 **0건**

> 🪤 **종전 가정 정정 2건.**
> ① **`DirectoriesToNeverCook` 은 존재하지 않는다** — 엔진 `Engine/Config/*.ini` 전체 grep 0건, 프로젝트 `Config/DefaultGame.ini` 에도 없다. 이 키로 제외를 설명하면 틀린다.
> ② `/Engine/EditorMaterials`·`EditorMeshes`·`EditorSounds`·`EditorLandscapeResources` 는 `/Engine/Editor` 접두사로 **커버되지만**, **`EngineDebugMaterials`·`MapTemplates`·`OpenWorldTemplate`·`Maps/Templates` 는 어떤 제외 규칙에도 걸리지 않는다** — 정상 쿡 대상이다("Debug"·"Template"이라는 이름에 속지 말 것).

### 2-2. 프로젝트 쿡 진입점

`Config/DefaultGame.ini` §`ProjectPackagingSettings`

```
+MapsToCook              = /Game/Maps/{L_MainMenu, L_Lobby, L_Transition, L_Map1_City}
+DirectoriesToAlwaysCook = /Game/{Cards, Mission, Weapons, Game, Character, Actors, SpawnPoints, Input, UI, Audio}
+DirectoriesToAlwaysCook = /Engine/BasicShapes          ← 기존 완화 조치
```

`Content/StylizedRenderingSystem`·`Synty`·`ProceduralWeaponAnimationSystem`·`Synthwave_city`·`LevelPrototyping` 등 마켓플레이스 트리는 이 진입점에서 **도달 불가** → 쿡되지 않는다. 112건 중 대부분이 여기에 속한다.

---

## 3. ⚠️ 조건부 취약 2건 — 둘 다 쿡 범위 안

두 건의 공통 성격: **참조 대상이 에디터 전용 디렉터리인데, 참조하는 프로퍼티가 `WITH_EDITOR`/`WITH_EDITORONLY_DATA` 가드 밖의 평범한 런타임 UPROPERTY** 다. 그래서 §4의 무해 5건과 달리 **엔진이 대신 끊어 주지 않는다.**

### ① `BP_FPSRPlayer.WarningSound` → `/Engine/VREditor/Sounds/UI/Camera_Shutter`

| | |
|---|---|
| 참조 에셋 | `Content/Character/Player/BP_FPSRPlayer.uasset` (**유일**) |
| 프로퍼티 | `UFPSRBlindspotAudioComponent::WarningSound` — `Source/FPSRoguelite/Public/Hero/FPSRBlindspotAudioComponent.h:56` |
| 런타임 소비 | `FPSRBlindspotAudioComponent.cpp:176,184` → `UGameplayStatics::SpawnSoundAtLocation(this, WarningSound, …)` |
| 에디터 가드 | **없음** |
| 엔진 실물 | `Engine/Content/VREditor/Sounds/UI/Camera_Shutter.uasset` 존재 |
| 유래 | `da761449`(사각 오디오 V1) — 플레이스홀더로 꽂힌 것 |

**영향**: 탈락 시 §7-5 **G1 ③(사각 위협 체감) 판정 근거가 패키지 빌드에서 무효**가 된다. 지금은 탈락하지 않지만, `bSkipEditorContent` 를 켜거나 UAT에 `-SkipCookingEditorContent` 가 들어오는 순간 **무음**이 된다.
**처리**: M2 "사각 오디오 프로덕션 사운드 교체"에 **이미 등록됨**(`Roadmap.md` §7-6 M2).

### ② 🆕 `BP_MissionPointSet` Billboard `Sprite` → `/Engine/EditorResources/Spawn_Point`

**종전 미등록 — 이번 감사의 신규 발견.**

| | |
|---|---|
| 참조 에셋 | `Content/SpawnPoints/BP_MissionPointSet.uasset` (**유일**) |
| 부모 클래스 | `/Script/FPSRoguelite.FPSRMissionPointSet` |
| 경로 | BP에서 저작한 `UBillboardComponent` 의 `Sprite` 프로퍼티. `FPSRMissionPointSet.h:11` 주석이 *"In the BP, add Scene (or Billboard for visibility) components"* 라 이 저작을 명시한다 |
| 쿡 범위 | `/Game/SpawnPoints` = `DirectoriesToAlwaysCook` **안** |

**결정적 근거** — `Engine/Source/Runtime/Engine/Classes/Components/BillboardComponent.h:23-24`:

```cpp
UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Sprite)
TObjectPtr<class UTexture2D> Sprite;
```

**`#if WITH_EDITORONLY_DATA` 블록 밖이다** — 순수 런타임 UPROPERTY다. (에디터 가드는 `BillboardComponent.cpp` 의 *생성자 기본값 대입*에만 걸려 있고, BP가 저작한 값에는 걸리지 않는다.)

**컴포넌트도 스트립되지 않는다**: `grep -a -c 'bIsEditorOnly' Content/SpawnPoints/BP_MissionPointSet.uasset` → **0**.

**대조군(올바른 패턴)** — 프로젝트 자체 C++ 은 제대로 하고 있다:
`Source/FPSRoguelite/Private/Enemy/FPSRFlowFieldBoundsVolume.cpp:28` → `EditorBillboard->bIsEditorOnly = true;`

**처리**: **M2 신규 등록** — `/Game` 아이콘 텍스처로 교체하거나, 시각화가 에디터 전용이면 `bIsEditorOnly = true` 를 설정한다(후자가 싸고 의도에 맞다).

---

## 4. 에디터 전용이나 무해 — 5건

엔진 C++ 이 `#if WITH_EDITOR` + `ESoftObjectPathCollectType::EditorOnlyCollect` / `!Ar.IsCooking()` 으로 **참조 수집 자체를 차단**한다. §3의 2건과 갈리는 지점이 정확히 여기다.

| 경로 | 참조하는 `/Game` 에셋 | 무해 근거 |
|---|---|---|
| `/Engine/EditorBlueprintResources/StandardMacros` | BP/WBP 90개 (쿡범위: `BP_LobbyDisplayPawn`·`WBP_BasicCrosshair`·`WBP_BossHUDBar`·`WBP_HitMarker`·`WBP_ThreatIndicator`·`WBP_Lobby`) | `UBlueprint` 그래프 데이터에서만 참조. `Blueprint.cpp` → `UBlueprint::NeedsLoadForClient() { return false; }` → **`UBlueprint` 자체가 쿡되지 않는다**(쿡되는 것은 `UBlueprintGeneratedClass`). 모든 UE 프로젝트 공통 |
| `/Engine/EditorResources/LightIcons/S_LightError` | `BP_JumpPad`·`L_Lobby`·`L_Map1_City`·`TestWorld`·PWAS·SRS 데모 | `LightComponent.cpp` — `#if WITH_EDITOR` + 로드 시 `FCookLoadScope(ECookLoadType::EditorOnly)` + 참조수집기 `!Ar.IsCooking()` & `EditorOnlyCollect` |
| `/Engine/EditorResources/S_KBSJoint` | `BP_WobbleTarget` | `PhysicsConstraintComponent.cpp` — `UpdateSpriteTexture()` 전체 `#if WITH_EDITOR` + `FCookLoadScope(EditorOnly)`, `Serialize` 수집도 `!Ar.IsCooking()` |
| `/Engine/EditorResources/S_KHinge` | `BP_WobbleTarget` | 동상 |
| `/Engine/EditorResources/S_KPrismatic` | `BP_WobbleTarget` | 동상 |

---

## 5. 런타임 쿡 대상 — 101건 (무해)

전건 `D:\UnrealEngine\UE_5.7\Engine\Content\` 에 실물 존재 확인, `/Engine/Editor*`·`/Engine/VREditor` 접두사 **비해당** → 정상 쿡.

| 카테고리 | 대표 경로 | 참조 에셋 수 |
|---|---|---|
| `Functions/` (63) | `Engine_MaterialFunctions01/02/03`, `MaterialLayerFunctions`, `UserInterface/GetUserInterfaceUV` | 93 |
| `EngineMaterials/` (12) | `WorldGridMaterial`, `DefaultPhysicalMaterial`, `EmissiveMeshMaterial`, `FlatNormal`, `DefaultNormal`, `BlendFunc_Def*` | 52 |
| `Animation/` (4) | `DefaultAnimBoneCompressionSettings`, `DefaultAnimCurveCompressionSettings`, `DefaultVariableFrameStrippingSettings`, `DefaultRecorderBoneCompression` | 648 |
| `BasicShapes/` (7) | `Cube`·`Sphere`·`Cone`·`Cylinder`·`Plane`·`BasicShapeMaterial` | 13 |
| `EngineDebugMaterials/` (3) | `BlackUnlitMaterial`, `T_1x1_Grid` | 6 (쿡범위: `SM_SpawnGate`) |
| `EngineResources/` (2) | `DefaultTexture`, `WhiteSquareTexture` | 13 (쿡범위: `WBP_RunHUD`·`WBP_DownedOverlay`) |
| `EngineSky/` (3) | `M_SimpleSkyDome`, `SM_SkySphere`, `VolumetricClouds/m_SimpleVolumetricCloud_Inst` | 4 (쿡범위: `L_Lobby`) |
| `Maps/` (3) | `Entry`, `Templates/OpenWorld`, `Templates/HLODs/HLODLayer_Instanced` | 5 (쿡범위: `L_Lobby`) |
| `MapTemplates/` (2) | `SM_Template_Map_Floor`, `Sky/SunsetAmbientCubemap` | 3 |
| `OpenWorldTemplate/` (1) | `LandscapeMaterial/MI_ProcGrid` | 1 (`TestWorld` — 쿡범위 밖) |
| `EngineFonts/` (1) | `RobotoDistanceField` | 1 |

**`/Engine/Content/Slate/*` 4건은 패키지 경로가 아니다** — `FSlateBrush` 의 loose-file 리소스명(`ProgressBar_Background.png`·`ProgressBar_Fill.png`·`ProgressBar_Marquee.png`·`wide-chevron-down.svg`). 쿡 대상이 아니라 UAT가 loose 스테이징하는 엔진 Slate 리소스이고, 참조 10건이 전부 `Content/Synty/*` + `EUW_ProceduralEditor` 라 **쿡 범위 밖**이다.

---

## 6. 쿡 범위 한정 뷰 (가장 실무적인 목록)

`MapsToCook` 4맵 + `DirectoriesToAlwaysCook` 10디렉터리에서만 추출한 고유 `/Engine/` 참조 = **16건**:

```
/Engine/Animation/DefaultAnimBoneCompressionSettings          런타임
/Engine/Animation/DefaultAnimCurveCompressionSettings         런타임
/Engine/Animation/DefaultVariableFrameStrippingSettings       런타임
/Engine/BasicShapes/BasicShapeMaterial(.BasicShapeMaterial)   런타임 (+AlwaysCook 명시)
/Engine/BasicShapes/Cone | Cube | Sphere                      런타임 (+AlwaysCook 명시)
/Engine/EditorBlueprintResources/StandardMacros               에디터전용 · 무해(§4)
/Engine/EditorResources/Spawn_Point                           ⚠️ 취약 ②
/Engine/EngineDebugMaterials/BlackUnlitMaterial(.…)           런타임
/Engine/EngineMaterials/DefaultPhysicalMaterial               런타임
/Engine/EngineMaterials/EmissiveMeshMaterial                  런타임
/Engine/EngineResources/WhiteSquareTexture                    런타임
/Engine/VREditor/Sounds/UI/Camera_Shutter                     ⚠️ 취약 ①
```

맵별:

| 맵 | `/Engine/` 참조 |
|---|---|
| `L_MainMenu.umap` | `EngineMaterials/WorldGridMaterial` |
| `L_Lobby.umap` | `EditorResources/LightIcons/S_LightError`(무해) · `EngineMaterials/WorldGridMaterial` · `EngineSky/VolumetricClouds/m_SimpleVolumetricCloud_Inst` · `Maps/Entry` |
| `L_Transition.umap` | **없음** |
| `L_Map1_City.umap` | `BasicShapes/Plane` · `EditorResources/LightIcons/S_LightError`(무해) · `EngineMaterials/WorldGridMaterial` |

---

## 7. `Source/` · `Config/`

**`Source/` C++ 하드코딩 = 0건.**

```
grep -rn '/Engine/' Source/ --include=*.cpp --include=*.h                                  → 0
grep -rn 'ConstructorHelpers|LoadObject<|StaticLoadObject|FSoftObjectPath(TEXT' Source/     → 0
```

종전 `ConstructorHelpers` 기반 BasicShapes 하드코딩은 config 소프트패스로 이관 완료(`FPSRPlaceholderVisualSettings`).

`Config/` 의 `/Engine/` 참조(Content 스캔과 별개):

| 위치 | 값 | 판정 |
|---|---|---|
| `DefaultGame.ini` `XPGemMesh` | `/Engine/BasicShapes/Sphere.Sphere` | 런타임 · `+DirectoriesToAlwaysCook=/Engine/BasicShapes` 로 이미 보증 |
| `DefaultGame.ini` `EnemyPlaceholderMesh`/`BossPlaceholderMesh` | `/Engine/BasicShapes/Cube.Cube` | 동상 |
| `DefaultEngine.ini` `ServerDefaultMap` | `/Engine/Maps/Entry.Entry` | 엔진 기본값 · 런타임 쿡 |
| `DefaultInput.ini` | `/Engine/MobileResources/HUD/DefaultVirtualJoysticks` | 엔진 기본값 · 모바일 전용 |
| `DefaultEditor.ini` | `/Engine/EditorMaterials/AssetViewer/…` | 에디터 AssetViewer 프로필 · `DefaultEditor.ini` 자체가 비쿡 |

---

## 8. 방법론 한계 (정직 기록)

- **UTF-16 누락 검증 완료**: `grep -a -c -P '/\x00E\x00n\x00g\x00i\x00n\x00e\x00/\x00'` → 전 파일 0건. FName이 ASCII를 ANSI로 저장하므로 `grep -a` 로 전수 포착된다.
- 🪤 **`strings` 를 쓰면 안 된다** — 이름 테이블을 놓쳐 오판한다(이 건이 실제로 그렇게 오판된 적 있다). `grep -a -o` 를 쓴다.
- **미포착 가능 케이스**: BP에서 문자열 결합으로 런타임 조립되는 경로. `Source/` 하드코딩이 0건이고 프로젝트 관례상 그런 패턴이 없어 실질 위험은 낮다.
- **`Plugins/` 미조사** — 이번 범위 밖. 필요하면 같은 절차로 별도 수행.
- **쿡 빌드를 실제로 돌리지는 않았다.** 판정은 *엔진의 제외 조건 + 프로젝트 설정* 이라는 정적 근거 위에 서 있다(§2). 실제 패키지 쿡 검증은 M5 빌드에서 자연히 이뤄지며, 그때 §3의 2건이 이미 처리돼 있으면 재현할 위험 자체가 없다.

---

## 9. 재현 명령어 (전부 리포지토리 루트 · Git Bash)

```sh
# [1] LFS 실체 판정 — 이걸 먼저 통과하지 않으면 아래는 전부 무의미하다
git lfs ls-files | wc -l
git lfs ls-files | awk '$2=="-"' | wc -l      # 포인터만 → 0 이어야 한다
find Content -type f \( -name '*.uasset' -o -name '*.umap' \) -size -200c | wc -l   # → 0

# [2] 전수 추출
find Content -type f \( -name '*.uasset' -o -name '*.umap' \) -print0 \
  | xargs -0 grep -a -o -h '/Engine/[A-Za-z0-9_/.-]*' | sort -u > eng_uniq.txt   # 686
grep -v '^/Engine/Transient\.' eng_uniq.txt > eng_real.txt                        # 112
cut -d/ -f3 eng_real.txt | sort | uniq -c | sort -rn

# [3] 역매핑
for p in '/Engine/VREditor/Sounds/UI/Camera_Shutter' '/Engine/EditorResources/Spawn_Point'; do
  echo "### $p"
  find Content -type f \( -name '*.uasset' -o -name '*.umap' \) -print0 | xargs -0 grep -a -l "$p"
done

# [4] 쿡 범위 한정
find Content/Cards Content/Mission Content/Weapons Content/Game Content/Character \
     Content/Actors Content/SpawnPoints Content/Input Content/UI Content/Audio \
     -type f \( -name '*.uasset' -o -name '*.umap' \) -print0 \
  | xargs -0 grep -a -o -h '/Engine/[A-Za-z0-9_/.-]*' | grep -v '^/Engine/Transient\.' | sort -u
for m in Content/Maps/*.umap; do echo "### $m"; \
  grep -a -o -h '/Engine/[A-Za-z0-9_/.-]*' "$m" | grep -v '^/Engine/Transient\.' | sort -u; done

# [5] 컴포넌트 editor-only 판정
grep -a -c 'bIsEditorOnly' Content/SpawnPoints/BP_MissionPointSet.uasset   # → 0 (스트립 안 됨)

# [6] Source / Config
grep -rn '/Engine/' Source/ --include=*.cpp --include=*.h                  # → 0

# [7] 엔진 실물 검증
sed -n '286,300p' /d/UnrealEngine/UE_5.7/Engine/Source/Editor/UnrealEd/Private/Cooker/CookSavePackage.cpp
grep -n 'bSkipEditorContent' /d/UnrealEngine/UE_5.7/Engine/Config/BaseGame.ini          # → false
grep -rn 'DirectoriesToNeverCook' /d/UnrealEngine/UE_5.7/Engine/Config/                 # → 0 (존재하지 않는 키)
sed -n '14,30p' /d/UnrealEngine/UE_5.7/Engine/Source/Runtime/Engine/Classes/Components/BillboardComponent.h
```

---

## 10. 후속 (M2 등록분)

| # | 항목 | 상태 |
|---|---|---|
| ① | `BP_FPSRPlayer.WarningSound` — 프로덕션 사운드로 교체 | M2 **기등록**(`Roadmap.md` §7-6 M2 오디오 항목) |
| ② | `BP_MissionPointSet` Billboard `Sprite` — `/Game` 아이콘 교체 또는 `bIsEditorOnly=true` | M2 **신규 등록**(보드) |
