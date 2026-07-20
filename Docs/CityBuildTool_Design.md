# City Build Tool — 설계 / 구현 스펙 (Trinity 참조, 블록아웃 확장)

> **작성 2026-07-20.** 사용자 요청 = Trinity Building Editor(Steam 2140810)를 참조한 도시/건물 디자인 에디터 툴.
> 용도 = **U22a A-4 밀도 저작 가속**(Map_CyberCity를 Synty 조각으로 빽빽하게). 브랜치 `phase/citytool-blockout`(main 기반).

## 0. 결론 (조사 5스트림 수렴)

- **Trinity 실체** = 절차생성·방감지·프리팹저장 **없음**. 카탈로그 방식: 조각 선택→배치→회전(단계)→길이·높이 늘림(**두께 X**)→**조각끼리 스냅**→우클릭 재질 패널. 코너=벽 스타일별 별도 메시(자동 미터링 X).
- **우리는 이미 80%를 가짐** = `Source/FPSRogueliteEditor/Blockout/`(팔레트 `SFPSRBlockoutTab` + 배치모드 `UFPSRBlockoutPlacementMode` + 설정 `UFPSRBlockoutSettings` + 검증기 `FFPSRBlockoutValidator`).
- **5스트림 전원 결론 = 확장하라, 새 모듈 금지** (CLAUDE.md 기존인프라우선 + 메모리 editor-module "새 모듈 금지"와 일치).

## 1. 아키텍처 결정 (제1원리 3줄, CLAUDE.md 원칙4)

1. **제1원리 근거**: 게임 예산은 **적 200-300**에 쏠려 있다 → 정적 도시 지오는 **싸야** 한다(드로우콜/프리미티브 최소). 수천 조각을 조각당 `AStaticMeshActor`로 두면 예산 잠식 → **Packed Level Actor 통합**(사용자 결정)으로 인스턴싱.
2. **UE 표준 대비**: UE 5.7엔 공식 "도시 조립" 툴 없음 → 팀들이 Modeling Mode/ISM/HISM/Packed Level Actor/PCG를 조합. 우리는 **기존 UEdMode 배치모드를 확장**(FActorPositioning + FScopedTransaction = 비-deprecated 정석). UInteractiveToolsFramework까지 갈 필요 없음(과설계).
3. **프로젝트 정합**: 새 툴은 **블록아웃 스택 확장**(같은 3단 등록 패턴·같은 `UFPSRBlockoutSettings`·같은 `FFPSRBlockoutValidator` 게이트). 출력은 플로우필드 계약 준수(WorldStatic 콜리전, 검증기 6규칙 통과). 재작업 경량 지점만 후속.

## 2. 기존 확장점 (실측 file:line)

| 대상 | 현재 | 확장 |
|---|---|---|
| `UFPSRBlockoutPlacementMode::SnapLocation` (cpp:122) | X/Y 그리드 스냅만 | + 회전 스냅, + 소켓/모서리 스냅(P4/P5) |
| `RebuildGhost`(cpp:76) / `SpawnAtCurrent`(cpp:227) / `SFPSRBlockoutTab::PlaceAsset` | **스폰 레시피 3중복** | **공용 헬퍼로 추출**(P1 선결) |
| 배치모드 상태 | 회전 상태 없음 | `CurrentRotation` + 입력([ ] 또는 마우스휠) + `RotationSnapDegrees` |
| `UFPSRBlockoutSettings`(h:39) | `PaletteFolders`·`PlacementGridSize` | + `RotationSnapDegrees`, + 카테고리 규칙 |
| `SFPSRBlockoutTab` 팔레트 | 폴더+타일 2단 | + Structure/Dressing 분류 |
| 등록 | 3단 패턴(module.cpp:30/54/96) | 재질 패널 탭 추가 시 동일 패턴 |

**그리드 단위(cm) = 미확정**. `SM_Bld_Block_NxNxN` 명명 + 플로우필드 셀 200cm → **200 추정**이나 코드 전 확정 불가(에디터 `GetBounds()` 필요). 대응: config 기본값 200으로 두고 **에디터 테스트에서 확정**(스냅은 config 구동이라 코드 재작성 불요).

## 3. 단계 (P1~P4, 사용자 승인)

### P1 — 기반: 회전 + 분류 + 공용 스폰 헬퍼
- **선결**: 3중복 스폰 레시피 → `FFPSRBlockoutSpawn::SpawnPiece(World, AssetData, Transform, bGhost)` 정적 헬퍼로 추출(mesh→WorldStatic BlockAll / BP→as-is / label / FolderPath="Blockout" / ScopedTransaction는 호출측). 이후 멀티피스는 이걸 N회 호출.
- **회전**: `UFPSRBlockoutPlacementMode`에 `CurrentRotation`(Yaw) 상태 + `[`/`]` 키(또는 마우스휠)로 `RotationSnapDegrees`(기본 90°) 증감 + 고스트/스폰에 적용. Render에 현재 각도 표시.
- **분류**: 팔레트를 **Structure**(Buildings/Base) vs **Dressing**(Props/Environment) 탭. 폴더/명명 휴리스틱(`Meshes/Base`·`SM_Bld_*`=Structure / `SM_Prop_*`·`SM_Env_*`=Dressing).
- **config**: `RotationSnapDegrees` 추가, `PlacementGridSize` 기본 100→200(추정, 테스트 확정).
- **판정**: 빌드+스모크 그린 · 에디터에서 벽 회전 배치 · 검증기 통과.

### P2 — 프리팹 그룹 (Level Instance 저작)
- 건물 하나를 저작해 재배치. **Packed Level Actor 경로**: 선택 액터들 → `ALevelInstance`로 묶어 저장(에셋) → 팔레트에 프리팹 엔트리로 등장 → 배치.
- 신규: 프리팹 목록을 `RefreshPalette` FARFilter에 추가(ClassPaths에 LevelInstance/PackedLevelActor).
- **판정**: 프리팹 저장·재배치 왕복.

### P3 — 밀도: Packed Level Actor 통합
- 반복 드레싱/건물 → **Packed Level Actor**로 인스턴싱 통합(ISM/HISM 자동). "선택 → Pack" 액션.
- ⚠️ 플로우필드 계약: 통합 후에도 **WorldStatic 콜리전 트레이스 가능**해야(검증기 규칙1). Packed Level Actor 콜리전 검증 필수.
- **판정**: 수백 조각 배치 후 perf(드로우콜↓) + 검증기 통과 + 플로우필드가 벽 인식.

### P4 — 재질 디자인 패널 (Trinity 우클릭)
- 배치된 액터 선택 → 패널에서 MID 색/메탈릭/러프니스 조절(확인/취소). Trinity의 Design Menu 등가.
- 신규 탭(3단 패턴) 또는 배치모드 우클릭 훅.
- **판정**: 배치 조각 색 변경 왕복.

## 4. 리스크 / 주의

- **에디터 개폐 댄스**: 그리드 확정·테스트=에디터 열림 / 빌드=에디터 닫힘. 번갈아.
- **Build.cs 의존 추가**: P2/P3 Packed Level Actor = `LevelInstanceEditor`(또는 관련) 모듈 추가 가능 → 확인 후.
- **VibeUE 컨테이너 위젯 위험**(메모리): 재질 패널은 append-only, 컨테이너 재구조화 금지.
- **검증기가 계약 게이트**: 각 P 산출은 `FFPSRBlockoutValidator::ValidateLevel` 통과 보장(WorldStatic·바운드볼륨1·셀예산).

## 5. 진행 로그
- (P1) 착수 2026-07-20 — 브랜치 `phase/citytool-blockout`.
