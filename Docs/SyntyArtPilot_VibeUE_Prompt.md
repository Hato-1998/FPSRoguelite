# Synty 애니 셀/툰 아트 파일럿 — VibeUE 실행 프롬프트 (복붙용)

> **전제**: 2026-07-10 아트 스택 재확정(전체 셀/툰). 에셋은 이미 FPSRoguelite로 Migrate 완료(아래 '전제 상태'). 이 문서 = 그 파일럿을 **VibeUE MCP**로 인스펙션·저작·검증 준비까지 수행하는 실행판.
> **일반 파일럿 게이트**(비-VibeUE 포함) = `Docs/SyntyArtPilot_ResumePrompt.md`.
> **VibeUE 필수**: 에디터 열림 + VibeUE 플러그인 활성(127.0.0.1:8088, 등록명 VibeUE-Claude). 연결/실행 불가 시 `editor-bridge` 스킬로 headless commandlet 시도 → 핸드오프.
> 아래 코드블록을 VibeUE 연결된 세션에 붙여넣는다.

```
[규칙 — 항상 준수]
- **모든 보고·질문·현황 설명은 한국어로 한다**(어려운 조어/직역투 금지, 쉬운 말로).
- 플랜모드 우선: 비자명 작업은 계획 → 사용자 승인 후 실행. HIGH_RISK(삭제·커밋·머지)는 승인 후.
- 파일럿 = throwaway → **통과분만 나중 LFS 커밋(지금 커밋 금지)**. 검증 없이 "완료" 보고 금지.
- 모델 정책: 구현=Sonnet 위임 가능 / 검증=Opus 직접.

[먼저 읽기]
- Game.md + PROGRESS.md
- SSOT: Docs/SSOT/Concept.md §1-C-9, Docs/SSOT/Roadmap.md §8, Docs/AssetReplacement_Synty_ResumePrompt.md, Docs/SyntyArtPilot_ResumePrompt.md
- 메모리: synty-anime-cel-art-pivot, unreal-editor-mcp-vibeue, vibeue-mcp-capabilities, vibeue-render-target-gpu-hazard, vat-bake-inherited-component-wiring, weapon-da-pack-code-mapping, asset-integration-protocol, ue-editor-file-locks-block-git
- 스킬: editor-bridge

[목표] FPSRoguelite의 Synty 애니 셀/툰 아트 파일럿을 VibeUE로 실행한다. 대량 저작 전에:
 ① 열린 질문 해소(Blu 스켈레톤·PWAS 마운트 계약·Military 모듈성·Cyber City 콜리전)
 ② 최소 저작 프루프(무기 1P 표시 · SRS 셀 렌더)
 ③ 사용자 PIE 성능/룩 게이트 준비(적 200-300 셀 스웜)

[전제 상태 — 이미 완료 (Migrate 5.3→5.7, 전부 git untracked, 커밋 금지)]
- Content/PolygonCyberCity(1641) · PolygonMilitary(2590) · PolygonParticleFX(256) · Synty=Soldier HUD(1826) · ProceduralWeaponAnimationSystem=PWAS(145)
- Content/Assets/Anime_Girl_Character_-_Blu-6ccdbbe7 (애니 캐릭터) · Content/StylizedRenderingSystem (SRS 셀셰이더)
- 잔여물 Content/__ExternalActors__ · __ExternalObjects__ (PWAS 데모맵 World Partition, 무해 → 나중 트림)

[Phase 0 — VibeUE 연결 확인 (착수 전 필수)]
- 127.0.0.1:8088 핸드셰이크. ⚠️ **반드시 이 트리(E:/Git_Project/FPSRoguelite)에 연결됐는지** 프로젝트 경로/에셋 존재로 확인 — 옆 클론 FPSRoguelite2에 붙으면 이 에셋들이 없다(과거 실사고). 죽은 mcp__unreal_editor__*(Aura 잔상) 금지.
- 확인 결과 한국어로 1줄 보고 후 진행.

[Phase 1 — 에셋 인스펙션 (최우선) · 각 항목 "확인 → 판정 → 산출"]
1. **Blu 캐릭터 스켈레톤** (= GLB/리타겟 질문의 답)
   - 확인: Content/Assets/Anime_Girl_Character_-_Blu-6ccdbbe7 아래 SkeletalMesh → 스켈레톤 에셋 + 본 이름 나열.
   - 판정: Epic UE5 마네킹 호환? (hand_r/spine_03/pelvis/clavicle=Epic · J_Bip_*=VRM · mixamorig:*=Mixamo · Bip01_*=3dsMax) → 리타겟 필요 여부.
   - 산출: 단일 바디 vs 모듈(바디+팔 분리)? 모프타깃 유무? 머티리얼(셀 전용/PBR)? → FP 팔 추출 가능성.
2. **PWAS 마운트 계약**
   - 확인: ProceduralWeaponAnimationSystem 아래 팔 메시·AnimBP·프리셋 DA(DA_*Preset류)·예제 무기·프리셋 위젯.
   - 판정: 무기 부착 본/소켓(ik_hand_gun/hand_r)·프리셋 DA 필수 필드(LeftHandIK/Muzzle 소켓)·PWAS 팔 스켈레톤이 Blu와 같나(다르면 리타겟).
3. **Military 무기 1종**(AR류)
   - 확인: 파츠 메시(바디/리시버·바렐·탄창·사이트·스톡) 분리 여부 + 이름.
   - 판정: U15 모듈파츠(WeaponParts1P)로 조립 가능한가. ⚠️ Synty 무기 = 스태틱·무소켓·무애니.
4. **Cyber City 콜리전**: 메시 1~2개 콜리전 프리셋 확인(ECC_WorldStatic — U7 플로우필드 BuildObstacleMask 다운트레이스 전제). 없으면 보고.
5. **SRS**: 액터 BP + post-process 머티리얼 위치, 아웃라인 모드 확인(post-process가 기본; inverted-hull/material-based per-mesh 아웃라인은 스웜 금지).
→ **Phase 1 결과 5항목을 한국어 표로 PM에 보고** + 결정 필요 지점(리타겟 방식·무기 모듈 경로 등)을 한국어로 질문.

[Phase 2 — 최소 저작 프루프 (VibeUE 가능 범위)]
- **무기 프루프**: 기존 게임 무기 DA(UFPSRWeaponDataAsset, Content/Weapons/DataTable/DA_Weapon_*) 참고. 필드 = WeaponMesh1P(스켈 soft)·WeaponMeshStatic1P(static soft)·WeaponParts1P(FFPSRWeaponPartAttachment: Part/Socket/Offset)·MuzzleSocket.
  - **1차 프루프**(VibeUE): 테스트 DA에 Synty 무기(조립 통짜)를 WeaponMeshStatic1P로 지정 → 1P 뷰모델 표시 검증(볼트애니·모듈스왑 없이). DA IsDataValid(참조 소켓 존재) 통과 확인.
  - **모듈 경로**(후속): WeaponParts1P는 static 파츠를 **스켈레탈 리시버 소켓**에 부착 → Synty 정적 리시버를 **스켈레탈로 변환 + 소켓 저작**이 선행(정적→스켈 = DCC/headless, VibeUE 범위 밖 → editor-bridge headless 시도 or 핸드오프).
- **셀 프루프**: 테스트 맵에 SRS post-process 적용 → 씬이 셀/툰으로 렌더되는지 스크린샷.
  - ⚠️ VibeUE 오프라인 렌더 한계(vibeue-render-target-gpu-hazard): draw_material_to_render_target/read_render_target는 실시간 밉/TAA 미재현·클리어색만 반환 → **셀 룩 실판정은 PIE 스크린샷(사용자)**. VibeUE는 세팅까지만.

[Phase 3 — 사용자 PIE 게이트 (VibeUE는 준비만, 판정=사용자)]
- FPSR.SpawnEnemies 200~300(캡500) VAT 스웜 + SRS 아웃라인 → stat unit/fps/rhi로 **아웃라인 ON/OFF 델타 실측**(고정비용=적 수 무관 확인; 적 수 비례로 늘면 후보 탈락).
- 셀/툰 룩 육안 합격 + U7 플로우필드(FPSR.FlowField.Debug 1) 정상 + 20분 무크래시 + 가독성 5지표.
- ⚠️ 애니 고폴리를 스웜에 리스킨 금지(스웜=저폴리 VAT, Blu=플레이어/팀원). VibeUE는 맵/스폰/SRS 오버라이드 준비까지.

[VibeUE 함정 / 폴백]
- 컨테이너 위젯(자식 WBP 임베드) 프로그래매틱 compile/save = 모달행 + 크래시 + .uasset 손상 위험 → Soldier HUD 위젯 조립은 UMG 수동, save 비대화.
- 위젯 바인딩 getter는 컴파일 후 접근. 대용량 에셋 op는 MCP 타임아웃(엔진 백그라운드 완주 → 디스크 폴링으로 완료 판정).
- 에디터 열린 채 git 조작 금지(파일 락 → merge 실패, ue-editor-file-locks-block-git).
- VibeUE 불가 작업(정적→스켈 변환·복잡 머티리얼 실측) = editor-bridge headless commandlet → 그래도 안 되면 핸드오프 프롬프트 작성.

[완료 시]
- Phase 1 인스펙션 표 + Phase 2 프루프 스크린샷 + (사용자)PIE 수치를 **한국어로 PM에 보고**.
- PM이 확정 반영: 렌더러(SRS or 대안)·Blu 리타겟 방식·무기 모듈 경로 → SSOT(Concept §1-C-9·Roadmap §8·AssetReplacement resume의 '렌더러 미정' 확정) + 채택분 LFS 커밋(사용자 확인).
- throwaway 원칙: 통과분만 커밋, 실패분 폐기.
```

---

## PM 메모
- **VibeUE 최대 가치 = Phase 1 인스펙션**(Blu 스켈레톤·PWAS 계약·Military 모듈·콜리전) — 열린 질문을 한 번에 해소.
- **VibeUE 한계 = 정적→스켈레탈 변환·셀 룩 실판정** → 각각 editor-bridge headless / 사용자 PIE로.
- 파일럿 통과 후 PM이 SSOT '렌더러 미정' 확정 + LFS 커밋.
- **프롬프트 표준**: 상단 [규칙]의 "모든 보고·질문은 한국어로" 항목은 이후 모든 실행 프롬프트에 상시 포함(사용자 지시 2026-07-10, 메모리 report-in-korean).
