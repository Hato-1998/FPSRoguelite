# FPSRoguelite — 밸런스 튜닝 레퍼런스 (조절 가능한 전체 수치)

> 생성 2026-06-24. 게임 시작~마무리 루프타임·카드효과·데미지·몬스터체력 등 **조절 가능한 모든 수치**를 도메인별로 모은 편집용 카탈로그.
> 코드 스윕(C++ `EditDefaultsOnly`/`const` + 관련 SSOT·PROGRESS)으로 추출. **실제 저작값(.uasset 바이너리)은 에디터에서 확인/수정** — 본 문서는 *어디에 무엇이 있는지* + *현재값(코드기본/문서확정)* 을 매핑한다.

## 0. 읽는 법 — 편집 위치(★중요)

프로젝트 규칙: **C++ = 테스트/폴백 기본값, 실제 밸런스값 = DataAsset/config에 저작(override)**. 그래서 같은 노브라도 "어디를 고쳐야 라이브값이 바뀌는지"가 다르다.

| 표기 | 의미 | 편집 방법 |
|---|---|---|
| **DA-저작** | DataAsset이 C++ 기본값을 덮어씀 (라이브값=DA) | 에디터에서 해당 `DA_*.uasset` 열어 수정 |
| **C++ (라이브)** | C++ 값이 곧 라이브 (덮는 DA 없음) | `.h/.cpp` 수정 → 빌드 |
| **C++ const** | `static constexpr` (에디터 노출 안 됨) | 코드 수정 → 빌드 필수 |
| **BP-override 가능** | C++ `EditDefaultsOnly` → BP 기본값에서 덮을 수 있음 | BP 열어 디테일 패널에서 수정(빌드 불요) |

> ⚠️ = **임시 테스트값(미원복)** — 프로덕션 전환 전 원복 필요. 🔲 = 실값이 .uasset에 저작되어 에디터 확인 필요.

---

## 1. ⏱️ 전체 루프 타임 / 타임라인

| 항목 | 현재값 | 편집 위치 | 비고 |
|---|---|---|---|
| **보스 출현 시각** BossTime | **300s** | DA-저작 `DA_RunSchedule` (폴백 RunDirector.h:112) | ⚠️ 프로덕션 1200s(약 20분), 미원복 |
| **미션 발생 시각** MissionWindows[Min~Max] | **~60/120/180s** | DA-저작 `DA_RunSchedule.MissionWindows[]` | ⚠️ 프로덕션 300/600/900s, 미원복. 윈도우별 [Min,Max] 랜덤 |
| 미션 후보 풀 MissionPool | (윈도우별) | DA-저작 `DA_RunSchedule` | 윈도우 발동 시 풀에서 1개 균등랜덤 |
| 미션 제한시간 TimeLimit | 0 (무제한) | DA-저작 `DA_Mission_*` | 0=실패없음 |
| 시작 적 수 BaseAliveCount | 40 | DA-저작 `DA_RunSchedule` | 스폰 강도 베이스 |
| 분당 적 증가 AliveCountPerMinute | 30/min | DA-저작 `DA_RunSchedule` | 시간 스케일링 |
| 동시 적 상한 MaxAliveCount | 250 | DA-저작 `DA_RunSchedule` | 운영 상한(하드캡 500과 별개) |
| 디렉터 틱 DirectorInterval | 0.25s | C++ const (RunDirector.h:107) | 런클럭/스폰 갱신 주기 |
| 오프닝시드 대기 OpeningSeedWaitTimeout | 5s | C++ const | 카드선택 대기 anti-deadlock |
| 런종료→로비 PostRunTravelDelay | 3s | C++ (GameMode.h:67) BP-override | 결과창 비트 |
| 로비 레디 카운트다운 ReadyStartCountdown | 3s | C++ (LobbyGameMode.h:41) BP-override | 전원레디→런시작 |
| 전역 프리즈 지속 | (고정시간 없음) | — | 전원 픽 0까지(이벤트 기반) |

### 1-1. XP / 레벨 곡선
| 항목 | 현재값 | 편집 위치 | 비고 |
|---|---|---|---|
| 레벨1 필요 XP XPBaseRequired | 100 | C++ (GameState.h:131) BP-override | 곡선=Base+(L-1)×PerLevel (선형 placeholder, UCurveFloat 후속) |
| 레벨당 XP 증가 XPPerLevel | 50 | C++ (GameState.h:135) BP-override | L1→2=100, 2→3=150, 3→4=200… |
| 무기해금 마일스톤 WeaponUnlockMilestones | {20, 30, 40} | C++ (GameState.h:139) BP-override | 파티레벨 도달 시 전원 무기해금 픽 |

### 1-2. 미션별 타이머/파라미터 (C++ `EditDefaultsOnly`, BP child override 가능)
| 미션 | 항목 | 값 |
|---|---|---|
| HoldZone | 존 반경 / 점령시간 | 400cm / 30s |
| StandStill | 정지 유지시간 / 정지판정 속도 | 15s / 50cm/s |
| MovingZone | 존 반경 / 점당 점령시간 | 400cm / 30s |
| CollectOrbs | 오브 수(폴백) / 줍기 반경 | 3개 / 100cm |
| CarryNoHit | 운반 유지시간 / 운반높이 | 20s / 120cm |
| DefeatFleeing | 도주속도 / 발동거리 / 타깃체력 | 350cm/s / 900cm / **50HP**🔲(placeholder) |
| LimitedVision | 버티기 시간 | 20s |

---

## 2. 🔫 무기 / 데미지

**무기 9종의 실스탯은 전부 `Content/Weapons/DataTable/DA_Weapon_*.uasset`에 저작(🔲).** 아래는 `FFPSRWeaponStatBlock`의 **C++ 기본값**(=폴백; 각 DA가 무기별로 덮음). 최종뎀 = `Damage × GlobalDamageMultiplier × 약점배수 × 크릿 × (FF시 0.5)`.

| 항목 | 기본값 | 위치 (FPSRWeaponTypes.h) | 카드축? |
|---|---|---|---|
| 데미지 Damage | 10 | :51 | ✅ Damage |
| 발사모드 FireMode | FullAuto | :54 | |
| 연사속도 FireRate (shots/s) | 8 | :57 | ✅ FireRate |
| 스핀업 bHasSpinup / Start / Ramp | false / 2 / 1.5s | :60·63·66 | (LMG=true) |
| 점사 발수 BurstCount | 3 | :69 | (BurstRifle) |
| 펠릿 수 PelletCount | 1 | :72 | (Shotgun) |
| 최대 관통 MaxPenetration | 1 | :75 | (Sniper) |
| 사거리 Range (cm) | 10000 | :78 | |
| 탄창 MagSize | 30 | :81 | ✅ MagSize |
| 재장전 ReloadTime (s) | 1.5 | :84 | ✅ ReloadTime |
| 기본 확산 SpreadDegrees (base half-angle) | 1.0 | :88 | ✅ Spread |
| **동적 확산 = heat 모델** (CrystalRecoil 이관 `6f1a981`; 레거시 Bloom* 필드 제거 `2c91ab7`) | MaxRecoilHeat 100 · CooldownDelay 0.5s · heat→spread 커브 | `Plugins/CrystalRecoil/…/CRRecoilSpreadComponent.h:82~94` + 무기별 heat 곡선(DA) | |
| 수직반동 RecoilVertical | 1.0 | :92 | ✅ RecoilVertical |
| 수평반동 RecoilHorizontal | 0.3 | :95 | |
| 반동회복 모드/속도 | Auto / 10 | :98·101 | |
| 수평패턴빈도 / Hip 수직배율 | 0.6 / 0.4 | :104·107 | |
| **반동 패턴 = CrystalRecoil `RP_*`** (무기별 비주얼 에디터 저작·재장전 초반 리셋) | — | 무기 DA `RecoilPattern`(`UCRRecoilPattern`) | |
| 근접 반경 / 공격딜레이 | 175cm / 0.5s | :120·123 | (Knife) |
| ADS 사용 / FOV / 확산배율 | false / 55 / 0.4 | :127·129·133 | (Sniper·Charge) |
| 투사체 속도/AOE반경/수명/관통 | 3000 / 0 / 5s / 0 | :141~153 | (Bazooka·Sniper) |
| 차징 시간/틱뎀/틱간격 | 0 / 2.0 / 0.12s | :160~166 | (ChargeLaser) |
| 무기교체 쿨다운 EquipFireCooldown | 0.2s | InvComp.h:29 | |
| 최대 무기 슬롯 MaxSlots | 3 | InvComp.h:23 (const) | |

**크로스헤어 (WBP_RunHUD 저작):** 크기 96px · 동적 확산 푸시 SpreadToPush 0.25 · 최대확산 0.18. 스타일 = **U12 진실 크로스헤어 시스템**(파라메트릭, per-weapon; 레거시 `bUseDynamicCrosshair` 대체, `PlayerFeel §2-14`).

**행동 Fragment (DA_Frag_*/DA_CardModifiers_* 저작🔲):** MultiShot 추가발수 1 · OnHitBonus +10뎀 · ExplosiveRounds 반경150/뎀20/넉백0 · AmmoOnMiss 리필1 · ReloadOnKill 즉시충전 · 공통 MaxStacks 1.

**무기 목록:** Rifle · BurstRifle · Sniper · Shotgun · LMG · Bazooka · Grenade · ChargeLaser · Knife (+ `DA_LoadoutPool` 시작무기 선택풀, Knife 제외 8종).

---

## 3. 🃏 카드 / 효과

| 항목 | 현재값 | 편집 위치 |
|---|---|---|
| 레벨업 제안 카드 수 | 3 | C++ (CardSubsystem.h:33) |
| 오프닝 시드 카드 수 | 2 | C++ (PlayerController.cpp:50) |
| 무기해금 오퍼 후보 | 3 | C++ |
| 기본 리롤 충전 DefaultRerollCharges | 3 | C++ (PlayerState.h:177) BP-override |
| 등급 가중치 Common/Rare/Epic/Legendary | 1.0 / 0.5 / 0.2 / 0.05 | DA-저작 `DA_Character_CardPool` (폴백 PoolDataAsset.h:27~36) |
| Luck 편향 스케일 LuckScale | 0.1 | DA-저작 `DA_Character_CardPool` |
| 카드별 추첨 가중치 Weight | 1.0 | DA-저작 각 `DA_Card_*` |
| **★효과별 등급 magnitude** | 🔲 .uasset 저작 | DA-저작 각 `DA_Card_*.Effects[].RarityTiers[].Magnitude` |
| 라이프스틸 흡혈비율 HealRatio | 0.05 (5%) | C++ (PassiveAbility.h:65) BP-override / GA BP |

**캐릭터 카드 (DA_Card_*):** CritChance · CritMult · Damage · Luck · MaxHealth · PickupRadius · XPGain · HealthRegen · Lifesteal — 각 카드의 **등급별 증가량(magnitude)은 .uasset에 저작🔲** (★핵심 튜닝면).
**무기 스탯 카드:** FireRate · MagSize · RecoilVertical × (AllWeapon / ThisWeapon) — Stat/Op(가산·%)·magnitude DA 저작.
**무기 해금 카드 (DA_CardUnlock_*):** 8종(부여무기 지정).
**전역 전투 베이스(카드 GE가 가산, FPSRCombatSet):** 크릿확률 5% · 크릿배수 ×2.0 · 데미지배수 ×1.0 · Luck 0 · 픽업반경 ×1.0 · XP획득 ×1.0 · 이동속도배수 ×1.0.

---

## 4. 👾 적 / 몬스터 체력 / 스폰 / 성능

**스웜 적 스탯은 전용 DA가 없음** → `FPSREnemyBase` C++ 기본값 또는 **`BP_EnemyBase`에서 override**(스웜은 `InitializeMaxHealth` 미호출이라 컴포넌트 기본값이 곧 라이브값🔲).

| 항목 | 현재값 | 편집 위치 |
|---|---|---|
| **몬스터 체력 MaxHealth** | **50** | `BP_EnemyBase` HealthComp default (폴백 HealthComponent.h:52) 🔲 |
| 이동속도 MoveSpeed (±10% 편차) | 250cm/s | FPSREnemyBase.h:72 (BP override) |
| 정지거리 / 공격사거리 | 120 / 150cm | h:75·78 |
| 공격 데미지 / 간격 | 8 / 1.0s | h:81·84 |
| XP 보상 XPReward | 5 | h:91 (BP override) |
| 중력/지면스냅/캡슐 | 1800 / 60cm / r40·h90 | h:99·104, cpp:22 |
| 넉백 감쇠시간 | 0.18s | h:137 (const) |

**스폰 / 성능 (대부분 `static constexpr` — 코드 수정+빌드):**
| 항목 | 값 | 위치 |
|---|---|---|
| **활성 적 하드캡 MaxActiveEnemies** | **500** | SpawnSubsystem.h:150 (const) — perf 예산 |
| 운영 상한 MaxAliveCount | 250 | DA_RunSchedule |
| 틱당 최대 스폰 / 스폰 간격 | 10 / 0.1s | h:153·156 |
| 링 스폰 내/외반경 | 1200 / 1500cm | h:159·162 |
| 공격 토큰 상한 | 10 | h:111 |
| 공격 수직사거리 / 월드킬Z | 150 / -10000 | h:115·120 |
| Significance S0/S1/S2 반경 | 1500/3500/6000cm | h:101~103 |
| LOD stride / NetFreq | 1/2/4/8 · 30/10/5/2Hz | cpp:226~231 (하드코딩) |
| 분리 반경 / 강도 | 120cm / 1.5 | h:107·108 |
| 스폰포인트 시야콘 / 폴오프 | ~~70° / 2500cm~~ **폐지**(시야 게이트 제거 2026-06-29, 거리폴오프 2026-06-25) | — |
| **플로우필드** 셀크기/반경/재계산 | 200 / 10000 / 0.2s | FlowField.h:46~48 ⚠️(볼륨 데이터화 후속) |
| 적 스폰포인트 Weight/MinDist/bEnabled | 1.0 / 0 / true | `BP_EnemySpawnPoint`+L_Sandbox 인스턴스 |

---

## 5. 👑 보스

| 항목 | 현재값 | 편집 위치 |
|---|---|---|
| **보스 체력 MaxHealth** | **3000** | DA-저작 `DA_BossDefinition` 🔲 (C++ 폴백 1000) |
| 폴백 체력 DefaultMaxHealth | 1000 | C++ (BossBase.h:60) — DA 미할당 시만(FPSR.SkipToBoss용) |
| 보스 스폰포인트 사용 | true | DA `DA_BossDefinition` |
| **약점 Weak_Head 배수** | **×2.5** (Z+150,r60) | `BP_Boss` 약점컴포넌트 인스턴스 🔲 |
| **약점 Weak_Core 배수** | **×2.0** (Z0,r80) | `BP_Boss` 약점컴포넌트 인스턴스 🔲 |
| 약점 컴포넌트 기본배수 | 2.0 | C++ (WeakpointComponent.h:23) — 범용(적/보스 공용) |
| 보스 스폰포인트 Weight/bEnabled | 1.0 / true | L_Sandbox 인스턴스 (0,0,200) |
| 무 스폰포인트 폴백 오프셋 | 전방800 / 위200cm | C++ (RunDirector.cpp:454) |

> BossTime(300s ⚠️임시)은 §1 타임라인 참조.

---

## 6. 🧍 플레이어 / 전역 전투

| 항목 | 현재값 | 편집 위치 |
|---|---|---|
| **플레이어 체력 MaxHealth/Health** | **100** | 🔲 플레이어 Init GE 저작 (생성자 폴백 100) |
| 대시 속도 DashSpeed | 2000cm/s | C++ (FPSRCharacter.h:218) BP-override |
| 대시 충돌무시 시간 DashDuration | 0.2s | h:221 |
| 대시 쿨다운 DashCooldown | 2.0s | h:224 |
| 기본 보행속도 BaseWalkSpeed | 600cm/s | h:228 BP-override |
| 접촉피해 무적시간 i-frame | 0.25s | h:239 |
| 크릿확률 / 크릿배수 / 데미지배수 | 5% / ×2.0 / ×1.0 | CombatSet.cpp:11~13 (카드 GE 가산) |
| Luck / 픽업반경 / XP획득 / 이속배수 | 0 / ×1.0 / ×1.0 / ×1.0 | CombatSet.cpp:14~17 |
| **친선사격 배율 FriendlyFireDamageScale** | **0.5 (50%)** | C++ (FPSRGameState.h:272) BP-override · ⚠️GameConfirm의 10%는 stale |
| 친선사격 활성 기본 | false (OFF) | FPSRGameState.h:242 |
| 폭발 넉백 상향바이어스 | 0.35 | C++ const (CombatStatics.cpp:27) |
| 비전제한 비네트 강도 | 1.4 | C++ (FPSRCharacter.h:139) |
| **DBNO 부활(반경/시간/블리드아웃)** | **미구현(U9)** | — 현재 즉사 처리만 |

---

## 7. ⚠️ 임시값 원복 체크리스트 (프로덕션 전환 전)

| 값 | 현재(임시) | 프로덕션 | 위치 |
|---|---|---|---|
| BossTime | 300s | **1200s** | DA_RunSchedule |
| 미션 윈도우 시각 | 60/120/180s | **300/600/900s** | DA_RunSchedule.MissionWindows |
| 플로우필드 CellSize/HalfExtent | 200/10000 placeholder | 볼륨 데이터화 | FlowField.h(코드) |
| FleeTarget 체력 | 50(placeholder) | "고HP" 저작 | BP child of FleeTarget |
| 보스 폴백 체력 1000 | (테스트용) | DA 3000이 라이브 | — |

> XP 곡선(100/50)은 이미 원복 완료(프로덕션). 적/XP/보스 메시는 플레이스홀더 큐브·스피어.

---

## 8. 🔲 .uasset 저작값 — 정확한 현재값은 에디터 확인 (또는 덤프 가능)

코드 스윕으로 안 잡히는 "에디터에서 실제 저작된 숫자"들 (가장 중요한 튜닝면):
1. **무기 9종 실스탯** — `DA_Weapon_*` (Damage/FireRate/MagSize/Spread/Recoil/ADS/Projectile/Charge 무기별 값)
2. **카드 효과 등급별 magnitude** — `DA_Card_*.Effects[].RarityTiers[].Magnitude` (Common/Rare/Epic/Legendary 증가량)
3. **카드 등급 가중치/LuckScale** — `DA_Character_CardPool` (C++ 폴백 덮는지)
4. **미션 윈도우 실시각·풀** — `DA_RunSchedule.MissionWindows[]`
5. **Fragment 수치** — `DA_Frag_*`/`DA_CardModifiers_*`
6. **플레이어 시작 체력** — 플레이어 Init GE
7. BP override 여부 — `BP_EnemyBase`(적 스탯), `BP_FPSRCharacter`(대시/이속), `BP_FPSRGameState`(XP/FF)

> 원하면 **헤드리스 커맨드릿으로 이 .uasset들의 실제 저작값을 전부 덤프**해서 위 표의 🔲를 실수치로 채워드릴 수 있습니다(에디터 닫은 상태 필요).

## 9. 미구현 / 후속 튜닝 슬롯
- **UEnemyScalingProfile** (적 HP/공격력 시간스케일 커브) — 설계만 존재, C++ 미구현. 적 난이도 시간스케일의 핵심 슬롯.
- **DBNO 부활** (U9) — 부활 반경/시간/블리드아웃 전부 미존재.
- **XP UCurveFloat** — 현재 선형, 곡선 데이터화 후속.
- **라이프스틸/리젠 등급 스케일링** — 현재 GA 스칼라(등급 무관), 후속 폴리시.

---
*추출: 코드 전수 스윕(5도메인 병렬) + SSOT/PROGRESS 대조. 실수치(🔲)는 에디터/커맨드릿 확인.*
