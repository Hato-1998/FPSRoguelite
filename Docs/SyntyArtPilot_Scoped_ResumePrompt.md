# Synty 셀/툰 아트 파일럿 (스코프 축소판) — 재개 프롬프트

> **갱신 2026-07-14b (S1 검증·스폰 디버그 세션 — MCP 연결됨).** S2a + S0 완료. **S1 레벨 검증 통과**(VibeUE MCP 라이브 재현, 6종 중 5통과 + ⑥ 무해 경고=볼륨 바닥 minZ). **스폰 버그 해결**: L_GameFloor가 WhiteBox(3슬롯 멀티맵) 복제라 볼륨·스폰이 `Map.Center` 태그를 물려받아 단일맵에서 스폰 0였음 → **볼륨+스폰4 `map_id` Default 언태그**로 수정, **적 정상 스폰 확인**(로그 grid `66x66 (bounds volume)`, 디그너릿 경고 소거). 아레나 스코프 = **A안(132m 단일슬롯)**: 사용자가 North/East 잔재 12/16 삭제(마커 4개 PeriN_W/PeriE_* 잔존=무해). 함정 상세 = CityBuildGuide §7 / 메모리 `mapid-tag-single-map-spawn-block`. **다음 = 적이 건물 우회 추격 육안확인(② 마무리) → S3(SRS 셀) → S4(스웜 perf).**
> 파일럿 = throwaway 콘텐츠는 통과 전 커밋 금지. **단, S0 블록아웃 툴은 아트 채택과 무관한 실 에디터 인프라라 이미 main 커밋·푸시**(§2b).

---

## ▶ 새 세션 붙여넣기용 (이 블록만 붙여도 됨)

```
Synty 아트 파일럿(스코프 축소) 이어서 진행. 먼저 읽어라:
- Docs/SyntyArtPilot_Scoped_ResumePrompt.md (이 문서 — 전체 상태·결정·다음작업)
- Docs/SyntyArtPilot_S1_CityBuildGuide.md (S1 도시 제작 가이드 + 검증 뉘앙스 6건)
- Game.md + PROGRESS.md · Docs/SSOT/Performance.md §5

[상태] S2a + S0(main 4692f9b2) 완료. **S1 레벨 검증 통과 + 스폰 정상화 완료**(VibeUE MCP 라이브). L_GameFloor(WhiteBox=3슬롯 멀티맵 복제) = 132m 단일슬롯 아레나(A안). 6종 가드레일 5통과+⑥무해. **스폰 버그**: 볼륨·스폰이 `Map.Center` 태그 물려받아 단일맵에서 스폰 0 → **map_id Default 언태그로 수정, 적 정상 스폰 확인**(로그 grid `66x66 (bounds volume)`, 디그너릿 경고 소거). 실행 클론 = E:\Git_Project\FPSRoguelite. 에디터/PIE·맵제작=사용자, 코드/해석=Claude.
✅ **이 세션 Unreal/VibeUE MCP 연결됨** → 라이브 월드 인스펙션·로그·프로퍼티 편집 가능(트레이스/HitResult=`to_tuple`, 태그=`tag_name`, 편집=`set_editor_property`+`modify()`). 메모리 `vibeue-python-trace-hitresult`.

[다음] ① **적이 건물 우회 추격 육안확인**(② 마무리 = 플로우필드 장애물 인식; 스폰되니 금방 보임). no-floor 41%라 도로/플라자 여유 확인. ② 통과 후 **S3(SRS 셀 렌더 실측) → S4(스웜 perf)**. ⚠️**다음 CyberCity 맵은 WhiteBox 아니라 단일맵 베이스에서 복제**(MapId 함정 원천차단, CityBuildGuide §7 / 메모리 `mapid-tag-single-map-spawn-block`). 런북 = §5-S1.
```

---

## 0. 파일럿 개요 / 어디서
- **파일럿:** 스코프 축소 Synty 아트 스택 검증 = 단일 CyberCity 화이트박스 + 작업된 라이플 + 스웜 200 + **SRS 셀 렌더 실측**. 대량 채택 전 성능·룩 게이트. **채택분(아트 콘텐츠)만 LFS 커밋(사용자 확인), 아트 통과 전 콘텐츠 커밋 0.**
- **실행 클론 = `E:\Git_Project\FPSRoguelite`** (모든 에셋·코드·빌드·PIE). 이 세션 cwd는 `FPSRoguelite2`(doc)였고 절대경로로 FPSRoguelite 조작.
- **분담:** 에디터·PIE·육안판정·**맵 제작(S1)**=사용자 / C++·런북·수치해석·PM보고=Claude.

## 1. 전체 순서 (plan of record v5)
`S2a(무기애니)✅ → S0(블록아웃 툴)✅ → S1(맵, 사용자) → S3(SRS 셀) → S4(스웜 perf)`

## 2. ✅ 완료 = S2a 무기 애니메이션 (Path B)
- **Path B = 네이티브 절차모션 복구 + PWAS 장전 몽타주.** 검증(사용자 PIE) 통과. 상세는 이전 이력(이 문서 이전 버전) — 발사킥·걷기밥·룩스웨이·장전·ADS·CrystalRecoil·프리즈/DBNO 드리프트 0.
- **미커밋 잔재(§2c)**: `FPSRCharacter.h`(3줄 BlueprintReadOnly)·`DA_Weapon_Rifle.uasset`·`DA_RunSchedule.uasset`·`L_GameFloor.umap`·`DefaultEditor.ini`(블록아웃 설정 로컬) = 사용자 테스트분/S2a 잔재, **커밋 안 함**.

## 2b. ✅ 완료 = S0 블록아웃 에디터 툴 (커밋·푸시됨)
> **main 커밋 `4692f9b2`(merge, --no-ff) ← `22f8342b`(feat), origin/main 푸시 완료.** 11파일 1739+줄. 실 에디터 인프라(아트 채택 무관 유지) → 파일럿 규칙 예외로 커밋.

**무엇:** `FPSRogueliteEditor`의 3번째 C++ Slate nomad-tab = **`Tools > FPSR > 블록아웃 툴`**. config 기반 모듈러 맵 팔레트 + 블록아웃 가드레일.

**기능 (슬라이스 ①~⑧ + UI 재설계 + 뷰포트 배치 모드):**
- **팔레트 config** = `UFPSRBlockoutSettings`(Project Settings > FPSR > FPSR Blockout, `Config=Editor`/DefaultEditor.ini). `PaletteFolders`(폴더 추가=재빌드 불필요) + `PlacementGridSize`(격자 cm).
- **스캔·UI** = AssetRegistry로 폴더 재귀 스캔(StaticMesh + **액터 BP** — 비-액터 BP 필터). **2분할 카드 레이아웃**: 왼쪽 폴더 리스트 | 오른쪽 큰 썸네일 카드 그리드(`STileView`) + 검색 + 타입 배지(메시/BP) + 콜리전 배지(?/✓/✗, '상태 검사' 온디맨드 로드).
- **배치 2방식**:
  - **버튼 즉시배치**('선택 배치') = 카메라 전방 500cm.
  - **⭐ 심시티식 뷰포트 모드**('뷰포트 배치' 버튼) = `UFPSRBlockoutPlacementMode`(UEdMode). 커서→바닥 레이캐스트(`FActorPositioning::TraceWorldForPosition`) → **격자 스냅** + **END키식 바닥 스냅**(스냅셀 다운트레이스 ECC_WorldStatic + 바운딩박스 바닥 정렬) → 고스트 프리뷰(임시 액터) 추종 + 노란 스냅박스/파란 격자선 렌더 → **좌클릭 배치·연속 배치·ESC 종료**. 격자 크기 = 툴바 '격자(cm)' 칸 라이브 조절.
- **배치물 규칙(K4=B)**: 메시 = **자동 WorldStatic BlockAll**(FlowField 계약). 액터 BP = **as-is 스폰**(자동 콜리전변환 없음 — 디자이너 수동). 겹침 preflight 가드(이미 배치된 Blockout 조각과 겹치면 취소). `Blockout` 아웃라이너 폴더 + Undo 트랜잭션 + 자동 선택.
- **레벨 검증**('레벨 검증' 버튼, `FFPSRBlockoutValidator`) = **6종 가드레일** → `FPSRBlockout` Message Log: ①콜리전(비-WorldStatic 블로킹 메시) ②지면(WorldStatic floor 존재) ③스폰Z(SpawnPoint ~바닥+100) ④볼륨(FlowFieldBoundsVolume 정확히 1개) ⑤셀예산(≤40000/256) ⑥볼륨 중심 클리어(오앵커 경고).

**결정 (확정):** ⑨ opt-in BP 구조변환 = **드롭**(자동 BP 수정 위험 > 이득; 안 맞는 BP는 디자이너 수동 수정). ⑩ 컬렉션 = **생략**(툴 자체가 팔레트 브라우저).

**파일:** `Source/FPSRogueliteEditor/{Public,Private}/Blockout/` = `FPSRBlockoutSettings`·`FPSRBlockoutValidator`·`SFPSRBlockoutTab`·`FPSRBlockoutPlacementMode`. 모듈 등록 = `FPSRogueliteEditorModule`. Build.cs = `EditorFramework`/`LevelEditor` 의존 추가.

## 2c. ⚠️ 미커밋 워킹트리 (FPSRoguelite, main)
S2a/사용자 테스트 잔재 + S1 맵/머티리얼(전부 미커밋): `M Config/DefaultEditor.ini` · `M Content/Game/Data/DA_RunSchedule.uasset` · `M Content/Weapons/DataTable/DA_Weapon_Rifle.uasset` · `M Source/FPSRoguelite/Public/Hero/FPSRCharacter.h`(S2a 3줄, 이 세션 미변경) · `?? Content/Maps/L_GameFloor.umap` · `?? Content/_SyntyPilot/DevBlockout/*`(머티리얼) · `?? Scripts/gen_blockout_materials.py`. 파일럿 규칙 = 아트 통과 전 콘텐츠 커밋 0(스크립트·문서는 인프라라 커밋 가능·사용자 승인 후).

## 2d. ✅ 이 세션 (2026-07-14 S1 제작) = 맵 + 파일럿 머티리얼/가이드
- **런북 대조 검증**: `§5-S1`을 셔십된 S0 툴 실코드와 전수 대조 → 일치. **뉘앙스 6건** 발견(격자↔셀 구분·겹침가드 범위·지면 먼저·BP대신 StaticMesh·PIE GameMode 오버라이드·가드레일 심각도) = `Docs/SyntyArtPilot_S1_CityBuildGuide.md §7`에 정리.
- **`Scripts/gen_blockout_materials.py`**(exec 클론): `M_Dev_Blockout`(월드그리드+틴트) 마스터 + `MI_Dev_*` 8종(건물=오렌지/도로=차콜/지면=회색/존=발광). `/Game/_SyntyPilot/DevBlockout/`에 생성. 그리드 실패 시 플랫폴백. **사용자 실행·머티리얼 확인 완료.**
- **`Docs/SyntyArtPilot_S1_CityBuildGuide.md`**(doc 클론): 게임 제1원리 근거 도시 제작 가이드(가독성·back-to-back·플로우필드 계약·셀예산·스폰·2층·4문) + 권장 "컴팩트 링 시티" 레이아웃 + 11단계 빌드순서 + 6종 가드레일 + 함정.
- **S1 맵 `L_GameFloor` 디스크 스캔**(`.umap` 문자열, non-WP 인라인 액터라 개별 파일 없음): StaticMeshActor 다수 · CyberCity 메시 참조 · EnemySpawnPoint 다수 · FlowFieldBoundsVolume 참조(참조수≠인스턴스수, **정확히 1개인지 '레벨 검증'으로 확인 필요**) · DevBlockout 머티리얼 적용됨. **authoritative 검증 = 툴 '레벨 검증'**(라이브, MCP/에디터 필요).

## 3. 빌드/검증 (S0에서 확립)
- 빌드 = **FPSRoguelite 클론, 에디터 닫고, Live Coding OFF, `-MaxParallelActions=2`**: `"D:\UnrealEngine\UE_5.7\Engine\Build\BatchFiles\Build.bat" FPSRogueliteEditor Win64 Development -Project="E:\Git_Project\FPSRoguelite\FPSRoguelite.uproject" -WaitMutex -MaxParallelActions=2`
- 검증 = Opus 직접(빌드 Succeeded + diff 자기비판). 구현 = 정착 코드슬라이스는 Opus 직접 편집/Sonnet 위임 혼용. UI·모드 동작 = 사용자 스모크(Claude는 에디터 실행 불가).

## 4. ▶ 다음 = S1 CyberCity 화이트박스 (사용자 에디터 작업)
**목표:** 단일 throwaway 맵에 CyberCity 블록아웃 = 전 건물 WorldStatic + WorldStatic 지면 Z=0 + FlowFieldBoundsVolume 1개 + 실 스폰포인트. **S0 툴로 제작.**

### 5-S1. S1 런북 (사용자 실행 순서)
1. **맵 복제** — `L_U_Whitebox`(또는 현 테스트 맵) 복제 → `Content/_SyntyPilot/L_SyntyPilot_CyberCity`(throwaway). *(`_SyntyPilot` 폴더 이미 있음.)*
2. **팔레트 설정** — `Project Settings > FPSR > FPSR Blockout > Palette Folders`에 `/Game/PolygonCyberCity/Meshes` 확인(기본값). 격자 크기(예: 100~300cm) 취향껏.
3. **건물 배치** — `Tools>FPSR>블록아웃 툴` → 카드 선택 → **'뷰포트 배치'** → 커서로 바닥에 붙여 좌클릭 연속 배치(자동 WorldStatic). CyberCity 매스로 화이트박스 구성. *(건물 메시 793개 = 스태틱, BlockAll/WorldStatic.)*
4. **지면** — CyberCity엔 지면 메시 없음 → **WorldStatic 지면**(큰 Cube 또는 바닥 메시) Z=0 배치(BlockAll 확인).
5. **FlowFieldBoundsVolume** — `AFPSRFlowFieldBoundsVolume` 1개, 플레이 영역 전체를 감싸게 BoundsBox 크기 조정. **박스 중심에 콜리전 액터 두지 말 것**(grid origin Z 오앵커, 메모리 `flowfield-volume-center-collision-floortrace`). MapId/bUnifiedExtent 미설정(단일맵).
6. **스폰포인트** — `AFPSREnemySpawnPoint` 배치, **바닥+100**(메모리 `enemy-spawnpoint-z-floor-offset`; 캡슐 반높이 90). MinPlayerDistance 1500~3000, ZoneTag/MapId 미설정(링스폰 폐기).
7. **검증** — 블록아웃 툴 **'레벨 검증'** → Message Log 6종 가드레일 통과 확인(비-WorldStatic·지면·스폰Z·볼륨·셀예산·중심). 경고 해소.
8. **PIE 확인** — `FPSR.EnemyTarget 200`(director 살아있으면) 또는 빈 GameMode 오버라이드로 스폰, 적이 건물 돌아가는지(플로우필드 장애물 인식).

## 5. S3 / S4 요지 (참조)
- **S3 SRS:** post-process(**고정비용**, C++ 0). **싼 룩 kill-check 먼저**(SceneColor 아웃라인+글로벌 셀, 스웜 CustomDepth OFF, per-mesh 머티리얼 무변경) → 부족 시만 VAT custom-depth 고스트 테스트. Substrate OFF·`r.CustomDepth=3`. **inverted-hull 스웜 절대 금지.**
- **S4 perf:** 실 스폰 ~192 정상상태 → **SRS ON/OFF × 스웜 CustomDepth × 0/200 매트릭스**(고정해상도·VSync off·워밍업 제외). 20분 무크래시 + Performance §5 가독성 5지표. **하드캡 `GlobalAliveCap=200`.**

## 6. 핵심 사실·갓차
- **SRS** = 콘텐츠전용(C++ 0). 아웃라인·셀 = `BP_StylizedRenderingSystem` 글로벌 bUnbound PostProcess blendable.
- **적 스웜** = VAT(`SM_BroBot_VAT`+`M_BroBot_VAT`). SRS 셀 호환=**partial**(VAT WPO가 custom-depth 패스 평가되는지 실측 = S3/S4 핵심).
- **FlowField** `BuildObstacleMask`=`ECC_WorldStatic` 다운트레이스(`FPSRFlowFieldComputer.cpp`). `MaxTotalCells=40000`/`MaxGridDimPerAxis=256`. 셀상수 접근 = `UFPSRFlowFieldComputer::GetMaxTotalCells()/GetMaxGridDimPerAxis()`.
- **스폰:** director = 스폰포인트 전용·균일랜덤. ground-snap 안 함·capsule hh 90 → **바닥+100**. `FPSR.EnemyTarget N`. 적=`BP_EnemyMeleeBase`. `FPSR.RunDebug 1`/`FPSR.KillAllEnemies`.
- **CyberCity** 건물 매스=스태틱 메시(WorldStatic, 793). `/Game/PolygonCyberCity/{Meshes,Blueprints,...}`. 지면 메시 없음(S1에서 추가).
- **빌드/에디터:** 빌드=FPSRoguelite·에디터 닫고·LC OFF·`-MaxParallelActions=2`. 편집 후 같은세션 PIE=World Leak → 편집 후 에디터 재시작.

## 7. 열린 항목
- 무기 앰비언트 breathing(idle/ADS)·장전 탄창 detach = 후속 폴리시.
- S0 툴 폴리시(선택): 고스트 반투명 머티리얼, BP 겹침가드, 뷰포트 배치 시 회전(surface normal align), 컬렉션.
- 파일럿 아트 통과 후: PM이 SSOT 갱신(SRS 렌더러 확정) + 채택 아트 LFS 커밋(사용자 확인).
