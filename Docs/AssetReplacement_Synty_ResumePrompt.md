# B — Synty 에셋 전체교체 (Path A) 새 세션 실행 프롬프트 (복붙용)

> **근거**: 컨셉 피벗(2026-07-03) 에셋 방향 = Path A 통일 로우폴리 Synty(`Roadmap §8`·`Concept §1-C-9`). 검증된 슬롯별 추천 = **Artifact 구매 플랜**(`https://claude.ai/code/artifact/c9e1de4e-6766-49b0-8abd-c2cba057efc7`, 없으면 WebFetch로 열람) + 19-에이전트 소스 검증 서베이.
> **디렉터 확정(2026-07-05)**: 획득=**SyntyPass**(상용 라이선스 확인됨) / **적 교체 확정**(Paragon→Synty, 교체 완료 후 Paragon 제거) / 총기=Infima 유지 / 플레이어(BroBot)=별도 트랙 / UI·오디오=별도 트랙(Synty 미커버).
> **어디서**: 활성 코드 클론 `E:\Git_Project\FPSRoguelite`(no-2). 이 문서/PM 클론(FPSRoguelite2) 아님. 에셋·PIE 작업이라 LFS 대용량.
> 아래 코드블록을 새 세션에 붙여넣는다.

```
Game.md + PROGRESS.md 먼저 읽어. 그다음 이 계획의 근거를 읽는다:
- Docs/SSOT/Roadmap.md §8(환경 에셋 Path A + 파일럿 게이트)
- Docs/SSOT/Concept.md §1-C-9(사이버펑크 다이브·맵마다 테마 다름=의도된 디제시스)
- Docs/SSOT/Performance.md §5(적 200-300 예산·U7 플로우필드 ECC_WorldStatic 의존)
- Synty 추천 리스트업(Artifact: https://claude.ai/code/artifact/c9e1de4e-6766-49b0-8abd-c2cba057efc7 — 슬롯별 검증 톱픽/가용성/가격)

[작업] Path A(통일 로우폴리 Synty POLYGON)로 환경·적·프롭을 전체교체. 총기=Infima 유지, 플레이어(BroBot)·UI·오디오=별도 트랙(건드리지 않음). SyntyPass로 취득.

- 플랜모드 우선·승인 후 진행. 구현=Sonnet 위임 가능, 임포트/파일럿 판정=Opus 직접.
- ⚠️ 통과 전엔 커밋 금지(파일럿은 throwaway). 채택분만 LFS 커밋(사용자 확인 후).

[Step 1 — 파일럿 검증 (게이트, 반드시 먼저)]
후보 1팩(map-1 = POLYGON Sci-Fi Cyber City)을 **빈 UE5.7 프로젝트에 임포트** → 적 300 스폰 + U7 플로우필드 bake + 20분 런 프레임 실측.
- UE5.7 호환: Synty 빌드는 ~5.3-5.4 상한이라 임포트 시 머티리얼/물셰이더/FX 재컴파일 루틴 예상(스태틱메시·MI 기반이라 블로커 아님).
- 통과 기준: 화이트박스 대비 프레임 Δ가 적300 예산 내 + §5 가독성 5지표 유지 + 20분 무크래시.
- **다중맵 메모리 체크(신규, #3 잔존 정책 때문에 중요)**: 3~4맵을 동시 상주(언로드 안 함, LOD컬)해도 메모리가 견디는지 측정 — #3 다중맵이 맵을 언로드하지 않으므로(RunFlow §2-1) 상주 메모리가 관건. 초과 시 PM에 보고(잔존 정책 재검 필요).
- 통과분만 Path A 채택. 불통 시 Artifact 차순위(예: City Pack + 네온 오버레이) 또는 Path A 재검토.

[Step 2 — 환경 3맵 적용 (파일럿 통과 후)]
- Map 1 사이버펑크 시티 = POLYGON Sci-Fi Cyber City / Map 2 숲 = POLYGON Nature Biomes S1(+Nature Pack) / Map 3 우주 = POLYGON Sci-Fi Space. (Artifact 톱픽.)
- 각 맵은 #3 다중맵의 스트림 대상 레벨이 됨(Docs/MultiMap_Tier0_ResumePrompt.md와 합류) — 단, B는 에셋만, 다중맵 코드는 A(#3 Tier 0). B 단계에선 개별 맵을 authored 레벨로 구성.

[Step 3 — 적 교체 (Paragon → Synty) ⚠️ U20 VAT 베이크 전에]
- 스웜 본체 = POLYGON City Zombies(50종, 공유 UE4-마네킹 스켈). 우주맵 적 = Sci-Fi Space 동봉(에일리언/병사/로봇). 숲 크리처/미니보스 = Fantasy Rivals(Fab 미이관=Epic Vault/Store).
- **핵심 이점**: 전부 공유 UE4-마네킹 스켈 → Mixamo→마네킹→Synty 리타겟 1회로 **하나의 VAT/애니셋이 전 호드 구동**(제1원리 cheap-per-actor). ⚠️ 비균일 바디(Fantasy Rivals 골렘 등)는 개별 애니셋.
- ⚠️ **시퀀싱**: U20 VAT 베이크 콘텐츠 저작을 **이 교체 후**에 해야 함(지금 Paragon으로 베이크하면 재베이크). 진행 중 애니 콘텐츠 트랙과 조율.
- 교체·검증 완료 후 **Paragon 미니언 제거**(디렉터 결정).

[Step 4 — 갭 (Synty 미커버, 별도)]
- 4방향 거대 전환문 = Synty에 없음 → **커스텀 1메시 자작**(로우폴리 스타일, 맵별 머티리얼 스왑, 재사용=저드로우콜, "월드 다이브" 모티프). 판타지 게이트 금지.
- UI/HUD·오디오 = Synty 밖(커스텀 UMG는 이미 크로스헤어/설정/히트마커 보유 / 오디오=Fab·Sonniss). 별도 트랙.

[⚠️ 함정]
- **콜리전 필수**: U7 플로우필드 BuildObstacleMask는 ECC_WorldStatic 다운트레이스(Performance §5-2). Synty 모듈러 조각에 WorldStatic 콜리전 없으면 필드가 안 구워짐 → 임포트 콜리전 확인.
- **Nanite OFF**(로우폴리엔 오버헤드>이득) · 머티리얼 아틀라스/머지 · 폴리지 바이옴(Jungle/Meadow)=HISM/Foliage 인스턴싱+거리컬 · Synty 물/늪 투명=최고비용, 스웜맵 전 실측.
- **적 리스킨**: City Zombies를 코스메틱 변주로(공유 스켈 위), 개별 메시 다수 임포트 금지(per-actor 비용).
- **가용성**: Fantasy Rivals·Sci-Fi City original = Fab 미이관(Epic Vault/Store). 일부 Store Sold Out → SyntyPass로 취득. SyntyPass 취득분 상용 라이선스=디렉터 확인 완료(참고: 프로덕션 후 비주얼 성숙 시 에셋 교체 가능성 있음).
- **LFS**: 임포트 에셋 대용량 → 채택분 커밋 시 LFS. 파일럿 미채택분은 커밋 안 함.

[검증]
- 파일럿: 프레임(stat unit/fps/rhi)·드로우콜·20분 안정·다중맵 상주 메모리 수치 → PM 보고.
- 적 교체: 300 스웜 리스킨 렌더·VAT 파이프라인·플로우필드 정상(FPSR.FlowField.Debug 1).
- 사용자 PIE: 맵 룩·적 실루엣 판독(1인칭 가독성).

[완료 시]
파일럿 수치·채택 목록 보고 → PM이 Roadmap §8에 확정 스택 기록 → 채택 에셋 LFS 커밋(사용자 확인). 적 교체 완료 후 Paragon 제거 커밋. U20 VAT 베이크는 적 교체 확정 후 애니 트랙에서.
```

---
## PM 메모 (B 착수 순서·의존)
- **A(#3 Tier 0)와 병행 가능**: Tier 0는 화이트박스 2맵으로 코드 검증(에셋 독립). B의 Synty 맵은 나중에 Tier 0 스트리밍 대상으로 합류.
- **B의 적 교체 → U20 VAT 베이크 선행**: 재베이크 방지 위해 적 교체를 애니 콘텐츠 저작보다 먼저.
- 확정 스택·가격·가용성 = Artifact 구매 플랜. SyntyPass 취득.
