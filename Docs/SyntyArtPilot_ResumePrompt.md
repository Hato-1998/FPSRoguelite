# Synty 애니 셀/툰 아트 스택 — 파일럿 게이트 실행 프롬프트 (복붙용)

> **근거**: 2026-07-10 아트 스택 재확정(전체 셀/툰 통일). SSOT = `Concept.md §1-C-9`·`Roadmap.md §8`·`Docs/AssetReplacement_Synty_ResumePrompt.md`(2026-07-10 갱신). 메모리 = `synty-anime-cel-art-pivot`.
> **목적**: 대량 채택·콘텐츠 저작 **전에** 이 아트 스택이 제1원리(적 200-300 싸게)를 지키는지 + 셀/툰 룩 결과물을 실측/육안 검증. 통과분만 채택(파일럿=throwaway, 채택분만 LFS 커밋).
> **어디서**: 활성 코드 클론 `E:\Git_Project\FPSRoguelite`. 에셋·PIE 작업이라 LFS 대용량 · 에디터 종료 상태서 임포트.
> 아래 코드블록을 새 세션에 붙여넣는다.

```
Game.md + PROGRESS.md 먼저 읽어. 그다음 이 파일럿의 근거를 읽는다:
- Docs/SSOT/Concept.md §1-C-9 (아트 방향=전체 셀/툰 통일)
- Docs/SSOT/Roadmap.md §8 (에셋 스택·파일럿 게이트)
- Docs/AssetReplacement_Synty_ResumePrompt.md (2026-07-10 재확정 블록 = 임포트 리스트·리스크)
- Docs/SSOT/Performance.md §5 (적 200-300 예산·U7 플로우필드 ECC_WorldStatic 의존)

[작업] Synty 애니 셀/툰 아트 스택 파일럿 검증. Synty 전량 보유(SyntyPass). 목표 = 대량 저작 전에 성능·룩을 실측/육안으로 게이트 통과시키는 것. 코드 변경 최소(에셋·머티리얼·PIE 위주). 플랜모드 우선·승인 후 진행. 통과 전 커밋 금지.

[임포트 (에디터 종료 상태 → Fab Add to Project → Content 루트 → Assets/ 재배치, [[marketplace-asset-import-relocate]] 절차)]
1. Synty POLYGON Sci-Fi Cyber City (맵1). UE5.4 상한 → 5.7 마이그레이션 또는 FBX 직임포트(스태틱+아틀라스라 저위험). ⚠️임포트 후 콜리전 확인(플로우필드 BuildObstacleMask는 ECC_WorldStatic 다운트레이스).
2. Synty POLYGON Military Pack (모듈 무기). 파츠(리시버/바렐/탄창/사이트) 분리 메시 확인.
3. 애니 캐릭터 'Anime Girl Blu' (사용자 구매분). ⚠️스켈레톤 확인 — Epic 마네킹 호환이면 리타겟·팔추출·팀원애님 공유 최상. 아니면 별도 리타겟 필요.
4. PWAS (Procedural Weapon Animation System, Fab 유료 ~$30-50, UE5.1-5.7 네이티브).
   ※ 셀/툰 렌더러는 아래 Step D에서 무료부터 시험(구매 전).

[Step A — 무기 모듈 조립 프루프 (U15 검증)]
- Military Pack에서 무기 1종을 U15 모듈파츠로 조립: 베이스=스켈레탈 리시버(소켓 저작) + 파츠(바렐/탄창/사이트)=WeaponParts1P 배열(FFPSRWeaponPartAttachment: Part/Socket/Offset). RefreshWeaponPartComponents가 장착 시 부착.
- 사이버 리스킨 머티리얼(네온/방출) 1개 시험 적용.
- 검증: DA IsDataValid(참조 소켓 존재) 통과 + 1P 뷰모델 표시 + 파츠 스왑 시 머즐/조준 소켓 이동.
- ⚠️Synty 무기=스태틱 프롭·애니0 → 베이스는 스켈레탈 리깅+소켓 필요(또는 통짜 스태틱=볼트애니 포기).

[Step B — FP 팔 + PWAS]
- Blu에서 팔 메시 추출(바디 숨기고 팔만, 또는 팔 서브메시) → FP 뷰모델 셋업.
- PWAS 설치 → Synty 무기 스태틱 메시에 LeftHandIK/Muzzle 소켓 붙이고 DA_WeaponProceduralPreset 지정 → 반동/스웨이/ADS 절차 생성 확인(무기 비종속).
- 검증: 발사/재장전/ADS 절차 동작 + 프리즈/DBNO 게이팅 무회귀(기존 반동 컴포넌트 관계 확인).

[Step C — 셀/툰 렌더러 육안 평가 (★결과물 결정 지점, 무료부터)]
- ⚠️ **inverted-hull(메시 복제) 아웃라인 절대 금지**(스웜 드로우콜 2배). **post-process/스크린스페이스만.** 전체 씬 아웃라인은 **순수 GBuffer Sobel**(per-mesh custom-depth 마스킹 회피 = per-mesh 0비용) 우선.
- 무료부터 순서대로 얹어 육안 비교(스크린샷 캡처):
  ① 빌트인 DIY: post-process 머티리얼로 WorldNormal+SceneDepth Sobel 아웃라인 + 씬컬러 포스터라이즈 밴딩(엔진 내장, 0원, 최고 제어). ② 무료 Cel Shader Pro(Fab). ③ UE_CelLit(MIT, 5.7 테스트; Substrate 프로젝트 끔 필요 주의).
- 무료로 룩 부족 시에만 유료 시험: Cel Shader Pro(Komodobit ~$25-35, 글로벌 PP) / Ultra Hybrid Toon(글로벌·per-object 0). SRS(~$45, 25스타일이나 per-mesh custom-depth).
- ⚠️ VAT 인터 라인 선명도: VAT 노멀 베이크(AnimToTexture BONE 모드)면 크리스프. 포지션-only VAT도 depth(외곽) 엣지는 나옴.
- 산출: 후보별 스크린샷 + 육안 판정 → PM에 보고(렌더러 확정용).

[Step D — 스웜 성능 게이트 (★제1원리 실측)]
- FPSR.SpawnEnemies 200~300(캡 500)로 VAT 스웜 스폰 + 선택 셀/툰 아웃라인 적용.
- 측정: stat unit / stat fps / stat rhi / stat scenerendering. 아웃라인 ON/OFF 비교 → **아웃라인이 고정비용(적 수 무관)인지 실증**(inverted-hull이면 적수 비례로 붕괴 = 후보 탈락).
- U7 플로우필드(FPSR.FlowField.Debug 1) 정상 + 20분 무크래시 + §5 가독성 5지표 유지.
- ⚠️ 애니 고폴리를 스웜에 리스킨하지 말 것 — 스웜=저폴리 VAT 유지, Blu는 플레이어/팀원 전용.

[다중맵 메모리 체크(선택)] Cyber City 상주 메모리(다중맵이 맵 언로드 안 함) — 초과 시 PM 보고.

[통과 기준]
- Step A: 모듈 무기 조립·파츠 스왑·IsDataValid 통과.
- Step B: PWAS로 Synty 무기+Blu 팔 절차 애니 동작.
- Step C: 셀/툰 룩 사용자 육안 합격 + 렌더러 1개 선정(무료 우선).
- Step D: 200-300 스웜서 아웃라인 고정비용 확인 + 프레임 예산 내 + 20분 안정.

[완료 시]
- 파일럿 수치(프레임·드로우콜·아웃라인 ON/OFF 델타)·채택 렌더러·채택 에셋 목록을 PM에 보고.
- PM이 SSOT 갱신(렌더러 확정 — Concept §1-C-9·Roadmap §8·AssetReplacement resume의 '렌더러 미정' 확정) + 채택분 LFS 커밋(사용자 확인).
- 불통 시: 원인별 대안(다른 셀 후보·무기 통짜 폴백·스웜 아웃라인 게이팅 조정) PM 협의.

[함정/주의]
- 셀/툰 렌더러 = post-process만(inverted-hull 금지) · Cel Shader Pro류는 deferred 렌더러 필요(UE 기본이라 OK).
- 에셋 경로 C++ 하드코딩 금지 · 신규 소스 빌드 시 에디터 종료→-NoXGE.
- Synty 임포트 대용량 → 채택분만 LFS 커밋(파일럿 미채택분 커밋 안 함).
```

---

## PM 메모 (파일럿 착수 순서·의존)
- **셀셰이더는 무료부터** — 빌트인 DIY Sobel(0원·5.7 네이티브)이 top 후보, 결과물 부족 시에만 유료. "결과물 확인 후 결정" 정합.
- **적 스웜 = 저코스트 VAT 유지**(애니 Blu 리스킨 금지, 제1원리). Blu는 플레이어/팀원.
- 셀셰이더 조사 상세(후보·아웃라인 방식·검증)는 이 세션 워크플로 산출 — 채택 후 필요시 메모리화.
- 파일럿 통과 → PM이 렌더러 확정 SSOT 반영 + 채택 LFS 커밋.
