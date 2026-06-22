# AssetWork 신규 팩(ModularSciFiStation + ParagonMinions) — 재개 노트 (새 세션용)

> **목적**: VibeUE MCP가 세션 중간에 연결 해제됨(서버는 살아있으나 클라 연결만 끊김). **에디터 켠 채 새 Claude 세션 시작**으로 재연결. 이 노트로 새 세션이 즉시 이어간다.
> **작성**: 2026-06-22. **브랜치**: `content/character-environment`(env 커밋 `aed9c50` 위). 원 작업 지시: 사용자 — "추가 에셋 작업 진행".

## 🔁 재시작 절차
1. **에디터 열린 채 새 Claude 세션 시작** → VibeUE-Claude(127.0.0.1:8088) 자동 재연결.
   - 진단됨(2026-06-22): UnrealEditor 실행 중(PID 20016) + 포트 8088 LISTENING. 서버 정상, **클라 세션 재시작만** 하면 붙음. 에디터 재시작 불요.
2. 새 세션: 이 노트 + `Game.md`/`PROGRESS.md` 읽고 아래 진행.
3. **착수 전 ParagonMinions 사용 계획부터 사용자와 확정**(아래 §B 결정사항).

## 📦 현재 상태 (디스크 확인됨, git untracked)
사용자가 Fab 팩 2개를 Content 루트에 임포트함(미처리, `?? Content/{ModularSciFiStation,ParagonMinions}/`):

| 팩 | 크기 | 에셋 | 성격 |
|---|---|---|---|
| **ModularSciFiStation** | 2.3GB | 323 uasset + 1 umap | 모듈러 SciFi 스테이션 환경 키트 + 템플릿 데모 |
| **ParagonMinions** | **4.8GB** | **2105 uasset** + 2 umap | Paragon 미니언/버프 **적 캐릭터**(스켈레탈+풀 애님) + FX |

---

## 🅰️ ModularSciFiStation — 환경 키트 (relocate 대상)
**구조**:
- `Environment/` (실콘텐츠): Door·Floor·Ladders·Lamps·Pipes·Props·Railing·Rocks·Scaffold·Stairs·Tunnel·Walls (모듈 키트)
- `Materials/`(Decals·Glass·Landscape·Light·Master·Metal·Pipes·Tiles·Trims) · `Textures/`(다수 카테고리) · `Particles/`
- **트림 대상(데모/템플릿)**: `ThirdPersonBP/`(ThirdPersonCharacter·GameMode·SK_Mannequin·UE4_Mannequin_Skeleton·애님 — UE 템플릿 잔여, 우리 무관) + `Level/SciFiStationExampleMap.umap`(데모 맵)

**플랜**(ZerinLabs 선례 그대로):
1. **트림 먼저**: `ThirdPersonBP/` 전체 + `Level/SciFiStationExampleMap`(+BuiltData) 삭제. 삭제 전 "남길 Environment/Materials가 이들 참조하는지" grep 확인(템플릿은 보통 무참조).
2. **안전 재배치**: per-asset `rename_asset` **≤40 청크**(머티리얼/텍스처 먼저=피참조, 메시 나중) → `/Game/Assets/Environment/ModularSciFiStation/`. **bulk rename_directory 금지**(메모리 함정).
3. **rename 후 강제저장 필수**: `save_directory(dst, only_if_is_dirty=False)` — rename이 referencer ref를 메모리만 갱신·dirty 미설정이라 일반저장 스킵 → 디스크 구경로 잔류. 강제저장 후 디스크 grep 구경로 0 확인.
4. 구 디렉터리 삭제 + 검증(카운트·샘플 로드·MI→부모 참조).
5. (선택) L_Sandbox 창고를 이 키트로 디테일업 — 후속, 사용자 지시 시.

---

## 🅱️ ParagonMinions — 적 캐릭터 (⚠️ 전부 옮기지 말 것)
**⚠️ 2105 에셋·4.8GB. bulk 이동 절대 금지 + 통째 relocate 비효율**(LFS 7GB 부담, 메모리 함정). **선별 relocate + VAT 베이크**가 원칙1 정합.

**캐릭터 종류**(스켈레톤 기준 distinct):
- `Characters/Buff/` — Buff 몬스터 5색: Black·Blue·Green·Red·White (각 전용 스켈레톤)
- `Characters/Minions/` — 레인 미니언:
  - `Down_Minions/` = Minion_Lane_Core · Minion_Lane_Siege
  - `Dusk_Minions/` = Minion_Lane_Siege
  - `Prime_Helix/` = Prime_Helix
  - `White_Camp_Minion/` = Minion(기본)
  - (+ `SK_Minion_Lane_Super` 메시 1종)
- `Characters/Global/` — 공유(Eyes·FX·MaterialFunctions·MaterialLayers·ParameterCollections·NotForShip)
- **트림**: `Characters/Maps/`(`Lighting_Background.umap`·`Minions.umap` 데모) + 대용량 `FX/`(쓸 것만 선별)
- 애님셋: Melee·Attack·Jog·Death 등 풀 세트(미니언당)

**🔑 결정사항(착수 전 사용자 확정)**:
1. **어떤 캐릭터를 적으로?** — 스웜 잡몹 1~2종(예: White_Camp Minion 기본형) + 가능하면 엘리트용 별도 1종. 전부 안 씀.
2. **역할별 처리**:
   - **스웜 잡몹** → BroBot 방식 **VAT 베이크**(애님 1종=Jog/Walk만 32×32 본텍스처, 최소 머티리얼 `MF_BoneAnimation`, `MI_*_Enemy` 빨강). `BP_EnemyBase.Mesh`(상속 StaticMeshComponent)에 배선. → 원칙1(적 수백 경량) 정합. 레시피=[[vat-bake-inherited-component-wiring]].
   - **엘리트/보스급**(소수) → 스켈레탈+애님 유지 + GAS(원칙1 허용 영역). 선택지.
3. **선별분만** `/Game/Assets/Characters/Paragon<Name>/`로 relocate(per-asset ≤40 청크). 나머지 2000여 에셋은 미임포트/트림.

**방법**: 선별 캐릭터의 스켈레탈메시+스켈레톤+필요 머티리얼/텍스처만 골라 이동. Buff/Minion은 Global 공유 머티리얼레이어 참조 가능 → 의존성 grep으로 필요분 파악 후 동반 이동.

---

## ⚠️ 재배치 공통 함정 — [[marketplace-asset-import-relocate]]
- **bulk `rename_directory` 금지**(대용량 타임아웃+부분실패+레지스트리 손상). per-asset `rename_asset` ≤40 청크.
- **co-located 리다이렉터 = 손상 아님**(팩 내장 옛이름 별칭). 디스크 파일수·크기로 클린 판별(reals ≥5KB).
- **rename 후 `save_directory(only_if_is_dirty=False)` 강제저장 필수** → 디스크 grep으로 구경로 0 확인.
- 참조자(데모 맵) 선삭제 → 이동이 리다이렉터 무잔존으로 깨끗.
- VAT 베이크: `lod_index=0`, BONE 모드 머티리얼함수=`MF_BoneAnimation`, 최소 머티리얼 신축([[vat-bake-inherited-component-wiring]]).

## 🗂️ git 상태 (커밋 제외/주의)
- **미커밋 스푸리어스**: `Content/Maps/L_MainMenu.umap`(로드 dirty)·`Content/Maps/L_Sandbox.umap`(env 커밋 후 재dirty — **커밋 전 의미변경인지 확인**, 스푸리어스면 `git checkout`)·`Config/DefaultEditor.ini.localbak`·`Docs/reviews/`.
- 신규 팩(`Content/{ModularSciFiStation,ParagonMinions}/`)은 **처리(트림·relocate) 후 선별분만 커밋**. 원본 통째 커밋 금지(LFS 7GB).
- 커밋: `content(env)`(스테이션)·`content(char/enemy)`(미니언). `*_BuiltData`는 gitignore, LFS 포인터 확인.

## 📋 새 세션 복붙용 재개 프롬프트
```
Game.md + PROGRESS.md 먼저 읽고, Docs/AssetWork_NewPacks_Resume.md대로 신규 팩 작업 진행해. 에디터 열려있어 VibeUE 8088 연결됨.
① ModularSciFiStation: ThirdPersonBP·Level 데모 트림 → per-asset ≤40 청크(bulk rename 금지)로 /Game/Assets/Environment/ModularSciFiStation/ 이동 + rename 후 강제저장.
② ParagonMinions(2105·4.8GB): 전부 옮기지 말고, 어떤 미니언을 적으로 쓸지 먼저 같이 정한 뒤 선별분만 relocate + BroBot 방식 VAT 베이크. 착수 전 §B 결정부터.
```
