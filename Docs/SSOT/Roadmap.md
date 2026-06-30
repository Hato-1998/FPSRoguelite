# Roadmap — 구현 상태 / 로드맵 / 디버그 인벤토리 (FPSRoguelite SSOT 분할)

> `Game.md`(SSOT 허브)의 분할 문서. **섹션 번호(§x)는 원본 그대로 보존** — 소스 주석·교차참조 호환.
> 작업 시작 전 허브 `Game.md` + `PROGRESS.md`를 먼저 읽고, 진행 상황·로드맵·재미 게이트·플레이스홀더 전환 관련 확인 시 본 파일을 연다. **라이브 진행현황은 `PROGRESS.md`가 우선** — 여기는 단계 골격.
> 담는 섹션: §7 현재 구현 상태·로드맵(§7-1~7-5) / §8 디버그·플레이스홀더 인벤토리.

---

## 7. 현재 구현 상태 & 로드맵

### 7-1. 완료 (커밋·빌드 검증됨)
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
| **P1** (마무리중) | Net-aware 1P 캐릭터(Separated Arms) + 무기 2종 + 적 1종 + 데미지 브릿지 |
| **P1.5** | 사격/이동 감각 (A: 연사/반동/확산 ✅, B: **MagSize+재장전(예비 무한)**/ADS) |
| P2 | SpawnDirector + Flow-Field + Pooling + Significance (적 300+ 안정) + **적 이속 ±10% 편차** + **적 근접 데미지·공격토큰 baseline** + **충돌무시 대시** |
| P3 | 공유XP + 파티레벨 + **레벨업 스택(프리즈 폐지)** + **정비시간 RunPhase** + **오프닝 카드 시드** + Card UI + 동적 카드풀 + Rarity + 리롤 |
| P4 | **P4-A**(재설계) 런 디렉터(시간 미션 스케줄+보스타임) + 확장형 미션 프레임워크+스폰포인트 + **레벨업/미션클리어 전역 프리즈**(라운드제 폐지)+레퍼런스 미션 1종+오프닝시드 / **P4-B** Weapon Modifier Fragment+weapon-scope 카드+미션 보상 실적용 / **P4-C** 무기 7종 / **P4-D** 게임필(히트마커·핑·위협 인디케이터·사각 오디오)+PickupRadius/XPGain+HUD위젯. (+원거리 적 규격·공격토큰 확장) |
| P5 | 4인 협동 + 세션(Steam) + Iris 평가 + NetProfiler + **아군 오사(10%+호스트 토글)** + **수동 부활(DBNO)** |
| P6 | 메타 프로그레션 + 보스 + 클리어 플로우 |
| P7 | CommonUI 폴리시 + **오디오/이펙트(게임필) 폴리시** + Insights + README + 빌드 |

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
  - **▶ 판정 결과 (2026-06-30) = ✅ G1 합격** (사용자 결정): ① 스웜손맛·② 프리즈(**B안 baseline-only**, 하이브리드 미평가)·③ 사각(V1)·④ 적500 체감 합격. ⑤ 카드 빌드분기 = G2 보류 유지. **⚠️ §5 적500 정량 측정은 미실시(보류)** → 하드캡 잠정값 유지, 콘텐츠밸런싱/U14 perf 패스로 이월(Performance §5). **U1 양산 해금 → 2차 트랙(U5/U6/U7/U8/U10/U15) 개시 허용.** 판정 시트 = [`Docs/U1_GateSheet.md`](../U1_GateSheet.md) §D.
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
| FP팔/3P 바디 메시 미할당 | 빈 SkeletalMesh | HeroDataAsset 메시 바인딩 |
| 적 추격 = 단순 스티어링 | 최근접 추격 | P2 Flow-Field + separation + 배치 교체 |
| PlayerController `[Input] Added DefaultMappingContext` Warning | 1회성 로그 | 다음 빌드 시 Verbose 다운그레이드 |
| CommonUI `LogUIActionRouter` 에러 | 무해 | P3 `CommonGameViewportClient` 설정 시 해결 |
