# Roadmap — 구현 상태 / 로드맵 / 디버그 인벤토리 (FPSRoguelite SSOT 분할)

> `Game.md`(SSOT 허브)의 분할 문서. **섹션 번호(§x)는 원본 그대로 보존** — 소스 주석·교차참조 호환.
> 작업 시작 전 허브 `Game.md` + `PROGRESS.md`를 먼저 읽고, 진행 상황·로드맵·재미 게이트·플레이스홀더 전환 관련 확인 시 본 파일을 연다. **라이브 진행현황은 `PROGRESS.md`가 우선** — 여기는 단계 골격.
> 담는 섹션: §7 현재 구현 상태·로드맵(§7-1~7-5) / §8 디버그·플레이스홀더 인벤토리.

---

## 7. 현재 구현 상태 & 로드맵

### 7-1. 완료 (커밋·빌드 검증됨)
> ⚠️ 아래는 **P0~P1.5-A 초기 스냅샷**이다. 이후 P2~P8 전 구간 + 2차 트랙(U5~U20)·다중맵 U 아크·반동 CrystalRecoil·무기 전면개편·FPSR Data Editor가 **모두 main 머지됨**(§7-3 표 + `PROGRESS.md` '완료 이력'이 최신).
- **P0** 경량 C++ 스캐폴드 (UE5.7, 플러그인 enable, GameplayTags 초안, 빌드 OK, 스모크 테스트)
- **P1-0** 코어 프레임워크 / **P1-1** 플레이어 GAS 글로벌 속성(`UFPSRHealthSet`/`UFPSRCombatSet`)
- **P1-2** EnhancedInput(이동·시점·점프 PIE 확인) / **P1-3** 1인칭 카메라 + Separated Arms
- **P1-4** 무기 기반 — `Weapon/`: 3슬롯 서버권위, Push Model, 장착 시 발사 GA 부여
- **P1-5** 발사 GA — `UFPSRGA_WeaponFire_Hitscan`: 카메라 히트스캔 + 디버그 라인 + 크리티컬 + 적 데미지
- **P1-6** 근접 GA — `UFPSRGA_WeaponMelee`: 전방 구체 오버랩 다중 타격
- **P1-7** 적 — `AFPSREnemyBase`(경량 Pawn) + `UFPSREnemyHealthComponent`: 최근접 추격, 엔진 큐브 placeholder, 데미지 브릿지, 콘솔 `FPSR.SpawnEnemies [N]`
- **통합**: Character에 인벤토리 부착 + 기본무기 지급(서버) + IA_Fire/IA_EquipSlot1~3 배선
- **P1.5-A** 사격 코어 — `UFPSRWeaponFireComponent`: FullAuto 연사 루프 + 반동(카메라 킥) + 확산/블룸. 하드코딩 경로 제거(BP 참조 패턴) 리팩터 완료
- **빌드 성공 + 헤드리스 부팅·스모크 통과**(Fatal 0)

### 7-2. PIE 테스트 대기 / 사용자 BP 셋업 (블로킹)
PROGRESS.md '사용자 대기 작업' 참조. 요약: **BP 3종 생성 + 참조 할당** 필요
- `BP_FPSRGameMode`(**반드시 `/Game/Core/`**, 부모 `FPSRGameMode`): DefaultPawnClass / PlayerControllerClass
- `BP_FPSRCharacter`(부모 `FPSRCharacter`): IA 8개 + DefaultPrimary/SecondaryWeapon(DA_Weapon_Rifle/Knife)
- `BP_FPSRPlayerController`(부모 `FPSRPlayerController`): DefaultMappingContext=`IMC_Default`
- FireMode: Rifle=FullAuto / Knife=Single·무반동
- → 이후 full-auto PIE 테스트 → 통과 시 P1 완료, P1.5-B(탄약/재장전/ADS)

### 7-3. Phase 로드맵
| Phase | 산출물 |
|---|---|
| **P0** ✅ | 경량 C++ 스캐폴드 + Git/LFS + 플러그인 + GameplayTags + 빌드 OK + 스모크 |
| **P1** ✅ | Net-aware 1P 캐릭터(Separated Arms) + 무기 2종 + 적 1종 + 데미지 브릿지 |
| **P1.5** ✅ | 사격/이동 감각 (A: 연사/반동/확산, B: MagSize+재장전(예비 무한)/ADS) — 이후 반동/확산=CrystalRecoil heat 모델로 이관(`6f1a981`) |
| **P2** ✅ | SpawnDirector + Flow-Field + Pooling + Significance (적 300+ 안정) + **적 이속 ±10% 편차** + **적 근접 데미지·공격토큰 baseline** + **충돌무시 대시** |
| **P3** ✅ | 공유XP + 파티레벨 + **레벨업 스택(프리즈 폐지)** + **정비시간 RunPhase** + **오프닝 카드 시드** + Card UI + 동적 카드풀 + Rarity + 리롤 |
| **P4** ✅ | **P4-A**(재설계) 런 디렉터(시간 미션 스케줄+보스타임) + 확장형 미션 프레임워크+스폰포인트 + **레벨업/미션클리어 전역 프리즈**(라운드제 폐지)+레퍼런스 미션 1종+오프닝시드 / **P4-B** Weapon Modifier Fragment+weapon-scope 카드+미션 보상 실적용 / **P4-C** 무기 7종 / **P4-D** 게임필(히트마커·핑·위협 인디케이터·사각 오디오)+PickupRadius/XPGain+HUD위젯. (+원거리 적 규격·공격토큰 확장) |
| **P5** ✅ | 4인 협동 + 세션(Steam, 2-PC E2E) + **아군 오사(50%, 기본 ON 설계확정 2026-07-01·호스트 OFF 토글; 기본값 ON 전환=코드 후속)** + **수동 부활(DBNO=근접 자동부활)** — 완료 / Iris 평가=**비채택**(Push Model 유지)·NetProfiler(적500 정량)=**U14 이월** |
| **P6** ✅ | 메타 프로그레션(U10 SaveGame) + 보스(U4) + 클리어 플로우(P6-A 셸) |
| **P7** (부분) | CommonUI ✅ · 오디오 MVP ✅ / Insights·README·빌드 폴리시 = 잔여 |
| **P8** ✅ | 다중맵 심리스 **U 연속필드**(P-0~P-H `34b5eea`) + 반동 **CrystalRecoil** 어댑터(`6f1a981`) + **무기 전면개편**(투사체화·SMG·유탄런처 제거 `3adc945`) + **FPSR Data Editor** P0~P2 + 통합 애니 패스 인프라 · U5~U20 2차 트랙 — 전부 main 머지(상세 `PROGRESS.md` 완료 이력) |

### 7-4. P0/P1 잔여 연결 항목 (착수 전 확인)
- 기본 맵/GameMode config 연결(OpenWorld 템플릿 기본값 제거), `GlobalDefaultGameMode` 지정 검토
- CommonUI viewport/input data 설정 (P1 후반~P3): ViewportClient, InputData·Back/Click·ControllerData, Activatable Widget Stack 레이어(Game/GameMenu/Menu/Modal)

### 7-5. 코어 재미 게이트 (확정 2026-06-10 / **2026-06-16 G1·G2 2분할** — [Review/20260616-volumeup-design.md](../Review/20260616-volumeup-design.md))
로그라이트의 생사 = **30초 루프(스웜 사격 + 카드 선택)가 재밌는가**. 기능 단위 로드맵(§7-3)만으로는 이 판정이 누락되므로 **명시적 게이트**를 둔다.
**왜 2분할인가(컨설트 2026-06-16 수렴)**: 빌드 다양성(②) 판정은 시너지 축(상태이상) 부재 시 *거짓 판정* 위험("손맛이 죽은 건지 축이 없어서인지" 분리 불가) → 손맛/페이싱/성능(G1)과 빌드다양성/시너지(G2)를 분리한다.

- **G1 — 손맛 / 페이싱 / 성능 (양산 강제 관문)**
  - **시점**: 무기 8종(P4-C + U16) 도달이라 표본 충족. **현 로드맵(보스 U3/U4 + 로비 U11a) 진행 후 착수**(사용자 결정 2026-06-16 = 현 로드맵 유지). G1 판정 *전*에 **프리즈 하이브리드 최소 프로토**를 넣어 전역프리즈 baseline vs 하이브리드를 A/B 동시 판정(아니면 이미 아는 페이싱 리스크 재확인에 그침).
  - **판정 항목**: ① 스웜 사격 손맛(타격감·반동·학살 쾌감, §2-4-2) ② 프리즈/하이브리드 페이싱 수용성(§2-2 — 일반 수치카드 비동기 Q/E 전환 필요성 판단) ③ 1인칭 사각 위협 체감(§2-14 사각 오디오) ④ 적 300~500 체감 성능(§5).
  - **통과 = 콘텐츠 양산(무기/카드/적/미션) 개시 허용**. 불합격 = 다음 콘텐츠 전 §2-2(프리즈)/§2-4(사격)/§5(성능) 재설계 우선.
  - **▶ 판정 결과 (2026-06-30) = ✅ G1 합격** (사용자 결정): ① 스웜손맛·② 프리즈(**B안 baseline-only**, 하이브리드 미평가)·③ 사각(V1)·④ 적500 체감 합격. ⑤ 카드 빌드분기 = G2 보류 유지. **⚠️ §5 적500 정량 측정은 미실시(보류)** → 하드캡 잠정값 유지, 콘텐츠밸런싱/U14 perf 패스로 이월(Performance §5). **U1 양산 해금 → 2차 트랙(U5/U6/U7/U8/U10/U15) 개시 허용.** 판정 시트 = [`Docs/Archive/gates/U1_GateSheet.md`](../Archive/gates/U1_GateSheet.md) §D.
- **G2 — 빌드 다양성 / 시너지**
  - **시점**: 경량 적 상태이상 서브시스템 + 상태축 2~3개(Burn/Shock/Frost/Rupture 중) 프로토 구현 *후*.
  - **판정 항목**: ⑤ 카드 선택의 의미(빌드가 달라지는 체감, §2-3 시너지) ⑥ Fragment × 상태축 물림(상호작용 시너지).
  - **통과 = 빌드 축 확정 → 시너지/카드 콘텐츠 양산 허용**. 불합격 = §2-3 시너지 축 재설계.

---

## 8. 디버그 / 플레이스홀더 인벤토리 (프로덕션 전환 대상)

| 항목 | 현재 | 전환 계획 |
|---|---|---|
| 발사/근접 DrawDebug 라인·구체 | 검증용 시각화 | `#if !UE_BUILD_SHIPPING` 게이트 / VFX 교체 |
| `FPSR.SpawnEnemies [N]` 콘솔 커맨드 | 적 스폰 테스트 | P2 SpawnDirector로 대체, shipping 제외 |
| 적 큐브 placeholder 메시 | 엔진 기본 큐브 | 실제 적 메시 + 인스턴싱/VAT |
| XP 픽업 placeholder 스피어(`AFPSRXPPickup`, ConstructorHelpers) | 엔진 기본 스피어 | 실제 XP 오브 VFX + 풀링/배칭(P3-B 후속) |
| FP팔/3P 바디 메시 | (현행 Infima 팔·BroBot) | **애니 셀 베이스('Anime Girl Blu') 리스킨** — FP 팔 추출 + PWAS 절차 애니(§1-C-9) |
| 적 추격 = 단순 스티어링 | 최근접 추격 | P2 Flow-Field + separation + 배치 교체 |
| 환경/레벨 지오메트리 | L_Sandbox 화이트박스(테스트 지오) | **Synty POLYGON Sci-Fi Cyber City**(맵1 베이스; 2맵 Nature·3맵 Space=다중맵 단계). **전체 셀/툰 통일**(§1-C-9). 다중맵 심리스(`RunFlow.md §2-1`) |
| PlayerController `[Input] Added DefaultMappingContext` Warning | 1회성 로그 | 다음 빌드 시 Verbose 다운그레이드 |
| CommonUI `LogUIActionRouter` 에러 | 무해 | P3 `CommonGameViewportClient` 설정 시 해결 |

> **환경 에셋 방향 = Path A (통일 로우폴리 Synty POLYGON 패밀리) 확정 (피벗 2026-07-03, `Concept.md §1-C-9`)**. 근거(제1원리): 로우폴리 = 드로우콜/텍스처 최소 → 적 200-300 프레임예산 보존 + 기존 로우폴리 에셋 정합 + 벤더 통일 = 통일감 자동. (Path B 리얼리스틱 = 무겁·톤충돌로 기각.)
> ⚠️ **착수 전 필수 = 파일럿 검증**: Synty 후보 1팩을 **UE5.7 임포트 + 적 300 스폰 + U7 플로우필드 + 20분 런 프레임 실측** → 통과분만 채택. **Fab 등재 여부 팩별 확인**(예: POLYGON Sci-Fi City는 Fab 미이관·Epic 볼트만일 수 있음).
>
> **🔄 2026-07-10 아트 스택 재확정 (사용자 결정)** — 로우폴리 유지하되 **전체 셀/툰(애니) 통일 룩**으로 상향. 상세·임포트 리스트 = `Docs/AssetReplacement_Synty_ResumePrompt.md`(2026-07-10 갱신):
> - **환경** Synty Cyber City(맵1) · **무기** Synty **Military Pack 모듈 백본 + 사이버 리스킨**(Infima 교체) · **캐릭터** 애니 셀 'Anime Girl Blu' 리스킨(플레이어/팀원) · **적 스웜** 별도 저코스트 VAT(애니 리스킨 금지) · **FP 팔** Blu 팔 추출 + **PWAS** 절차 애니 · **UI** Synty **Sci-Fi Soldier HUD** · **VFX** Synty Particle FX + Epic Niagara(무료) · **오디오** Synty 밖(Sonniss·Kenney·Fab).
> - **렌더러 = 미정**(결과물 시각검증 후 결정; 후보 Stylized Rendering System / Celes Anime Shader).
> - ⚠️ **제1원리 리스크**: 셀 아웃라인=post-process(inverted-hull 금지=스웜 드로우콜 2배) · 셀×VAT 스웜 정합 실측 · 애니 고폴리를 스웜에 리스킨 금지 · Synty 5.4→5.7 마이그레이션.
