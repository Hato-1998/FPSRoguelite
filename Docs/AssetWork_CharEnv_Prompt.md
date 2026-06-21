# 캐릭터·배경 애셋 작업 — 새 세션 실행 프롬프트

> 아래 코드블록을 **새 세션에 그대로 복붙**한다. (작성: 2026-06-21, 브랜치 `content/character-environment` 분기 직후)

```
Game.md + PROGRESS.md 먼저 읽어. 추가로 Docs/SSOT/Architecture.md §4(폴더/모듈 구조)·Docs/SSOT/Workflow.md §6(빌드/브랜치/커밋/모델정책)·Docs/SSOT/Roadmap.md §8(플레이스홀더 인벤토리)·Docs/SSOT/Enemy.md §2-6(적 렌더링=인스턴싱/VAT)·Docs/SSOT/Performance.md §5/§5-1(적500 예산·Significance)을 읽어. 이 작업은 **캐릭터·배경 애셋 추가(콘텐츠 작업)**다 — 게임 플로우/코드 변경 아님.

[⚠️ 브랜치 상태 — 먼저 확인]
- `git branch` 로 확인. **현재 작업 브랜치 = `content/character-environment`**(main `bd44939`에서 분기). 이 브랜치에서만 진행한다.
- ⚠️ **`fix/w1-audit-corrections`(`94e175a`) = W1 전수검증 교정 완료·빌드/스모크/Codex/diff 검증통과·대기 중**(사용자 PIE 후 `--no-ff` main 머지 예정). **이 브랜치는 절대 건드리지 말 것.** 상세 = `Docs/codex-reviews/full-audit-2026-06-21.md`(P2 4건+P3 교정 적용분). content 브랜치는 W1 픽스가 없는 깨끗한 main 베이스(애셋=코드픽스와 독립이라 의도된 분리). 두 브랜치 머지 순서는 사용자 판단.

[목표] 캐릭터·배경 애셋 4종을 임포트·정리·배선해 플레이스홀더(엔진 큐브/Manny)를 실제 아트로 교체:
① 적 스웜 메시(큐브 교체) ② 플레이어 3P 바디 메시 ③ 보스 메시 ④ 배경·환경(L_Sandbox 드레싱 또는 신규 맵)

[분담 — 사용자 확정]
사용자가 **에디터에서 직접 Fab/마켓 팩 임포트** → AI가 **재배치(루트→Content/Assets/)·트리밍·BP 배선·LFS 커밋** 지원.
권장: **한 카테고리씩**(적→플레이어→보스→환경) 순차로 임포트·배선·커밋(검증/문제 격리). 카테고리마다 빌드 불필요(콘텐츠), PIE로 시각 확인.

[배선맵 — 임포트 후 AI가 처리 (소스 코드 확인 완료)]
| 카테고리 | 배선 지점(BP/맵) | 컴포넌트 | 설계 주의 |
|---|---|---|---|
| 적 스웜 메시 | `Content/Character/Enemy/BP_EnemyBase` | `Mesh`(**StaticMeshComponent**) | 적500이라 **정적 로우폴리 또는 VAT**가 §1/§5 의도. ⚠️**스켈레탈 애니 적은 perf 위반** — 원하면 VAT 베이크(static+머티리얼) 또는 C++ SkeletalMeshComponent 추가는 §1/§5 재검토 후 별도 결정(맹종 금지) |
| 플레이어 3P 바디 | `BP_FPSRPlayer`(상속 `GetMesh()`) + `BP_LobbyDisplayPawn`.`BodyMesh` | SkeletalMesh + AnimBP | GetMesh()는 이미 Skeletal·OwnerNoSee 설정됨(C++) → BP에서 메시+AnimBP 할당만. 현 Manny 플레이스홀더 교체. 1P 팔(V0 `SK_LPAMG_Arms`)은 별개 유지 |
| 보스 메시 | `Content/Boss/BP_Boss` | `BodyMesh`(StaticMeshComponent, 큐브) | 정지 스캐폴드라 정적 OK. **애니 보스 원하면 BP_Boss에 SkeletalMeshComponent를 BP 레벨에서 추가**(C++ 변경 불요, 보스=단일액터라 perf 무관) + 기존 BodyMesh 큐브 숨김 |
| 배경·환경 | `Content/Maps/L_Sandbox`(드레싱) 또는 신규 `L_*.umap` | StaticMesh 액터 | ⚠️**바닥=Mobility Static + Collision Block 필수**(적 지면트레이스 ECC_WorldStatic만 쿼리 — WorldDynamic 추가 금지=비행투사체 착지 부작용). 스폰포인트(BP_EnemySpawnPoint)·보스스폰(BossSpawnPoint) 위치 재확인 |

[임포트 워크플로 + 함정 — 메모리 [[marketplace-asset-import-relocate]] 준수]
1. **임포트는 에디터 닫은 상태에서** Fab "Add to Project"(열린 채면 파일잠금→BuiltData만 떨어지는 부분실패). 항상 Content **루트**로만 떨어짐.
2. **재배치(루트→Content/Assets/<팩>)**: 디스크 `mv` 금지(2천 에셋 패키지경로 참조 깨짐). VibeUE `EditorAssetLibrary.rename_directory('/Game/X','/Game/Assets/X')`로 참조보정. 대용량은 MCP 타임아웃 가능→디스크 폴링으로 완료확인. 부분실패(SK "failed to save" 모달) 주의.
3. **트리밍**: 애니(AnimSequence)는 프리뷰 메시로 `Demo/Mannequin`을 대량참조(에디터전용)→**Demo/Mannequin만 남기고** Maps(용량 대부분)/Core/Data 제거. 삭제 전 "남길세트가 삭제세트 참조하는지" grep.
4. **고아 정리**: 에디터 delete API는 dirty/로드 패키지 삭제 거부→잠금없으면 디스크 `rm -rf`가 확실. 정리 후 **에디터 재시작(Don't Save)**로 stale 레지스트리 비움.
5. **무결성 검증(에디터 무관)**: dst .uasset 바이너리를 옛 패키지경로 문자열로 grep→0건이면 자기완결.

[VibeUE MCP — 메모리 [[vibeue-mcp-capabilities]]]
- 에디터 **열림+플러그인 활성** 시에만 연결(127.0.0.1:8088, 등록명 VibeUE-Claude). 끊기면 코드/레시피만 가능. `mcp__unreal_editor__*`(죽은 Aura 잔상) 쓰지 말 것.
- BP에 컴포넌트 추가/메시 할당/머티리얼 배선 가능. 위젯 getter는 컴파일 후, FText는 Conv_StringToText, config는 ini 직접.
- ⚠️ MCP로 BP 반복 재컴파일 후 **PIE 전 에디터 재시작**(Undo 버퍼 World Leak 크래시 방지).

[커밋 — 메모리 [[phase-end-commit-user-content]]]
- `.gitattributes`: `*.uasset`/`*.umap`=LFS / `.gitignore`: `*_BuiltData.uasset`·`Saved/`·`Intermediate/` 제외(이미 설정됨, git-lfs 3.5.1).
- 스테이징 후 **LFS 포인터(diff 3줄)·BuiltData 0개 확인**. 커밋 컨벤션 `content(<scope>): <요약>`(§6-8). 카테고리별 커밋 권장.

[검증]
- 콘텐츠 작업이라 빌드 불필요(코드 무변경 시). **PIE 시각 확인**: 적 메시 표시·이동/분리 무회귀 / 3P 바디 타인뷰·로비 포디움 / 보스 메시·약점존 위치 / 환경 바닥 적 안 빠짐(WorldStatic).
- 코드 변경(예: 적 SkeletalMeshComponent 추가)이 생기면 → 플랜 우선·빌드+스모크·구현 Haiku/검증 Opus(§6-5).

[완료 기준] 카테고리별 임포트·배치·배선·PIE 확인·content 커밋. 4종 완료 시 PROGRESS 갱신(애셋 추가 이력) + 사용자에게 다음(W1 fix 머지/PIE 또는 추가 폴리시) 보고. **임시구조·하드코딩 경로 금지(§6-2), 플레이스홀더는 §8 인벤토리에서 제거 표시.**

[시작] 사용자에게: 어떤 팩을 쓸지(카테고리별 Fab 팩명, 없으면 로우폴리/VAT친화 추천 요청) + 임포트 순서 확인 → **에디터 닫고 첫 팩 임포트 → 에디터 열고 알림** → 재배치·배선·커밋 착수.
```

---

## (참고) 이 프롬프트가 담은 사전조사 — 새 세션은 위 블록만으로 충분하나, 근거는 아래

- **배선 지점은 소스 코드로 확인됨**: `AFPSREnemyBase.Mesh`(StaticMeshComponent, VisibleAnywhere)·`AFPSRBossBase.BodyMesh`(StaticMeshComponent, 큐브 placeholder)·`AFPSRCharacter` 상속 `GetMesh()`(Skeletal, OwnerNoSee). `UHeroDataAsset` **미구현** → 3P 바디는 BP 직접 바인딩.
- **BP/맵 실재 확인**: `Content/Character/Enemy/BP_EnemyBase`·`Content/Boss/BP_Boss`·`Content/Character/Player/BP_FPSRPlayer`·`BP_LobbyDisplayPawn`·`Content/Maps/L_Sandbox`.
- **커밋 파이프라인 준비됨**: LFS(uasset/umap)·gitignore(BuiltData) 설정 확인, `Content/Assets/`에 기존 팩(Decal·LowPolyAnimatedModernGuns) 전례.
- **StaticMesh vs Skeletal 판단**(제1원리): 적=정적/VAT(적500 §1/§5), 보스=BP에 Skeletal 추가 가능(단일액터), 플레이어 3P=이미 Skeletal. 스켈레탈 적은 §1 위반이라 의도적 경고.
