# 다중맵 Tier 0 — 화이트박스 콘텐츠 저작 체크리스트 (사용자 PIE 게이트)

> Tier 0 **C++ 코드는 완료·빌드+헤드리스 유닛 검증됨**(브랜치 `phase/p8-multimap-tier0`). 실동작(스트림·추격·드레인·전역200·late-join) 검증은 **아래 화이트박스 콘텐츠를 에디터에서 저작한 뒤 사용자 PIE**로만 가능하다(맵 콘텐츠는 코드로 만들 수 없음). 이 문서 = 그 콘텐츠 저작 + PIE 검증 가이드.

## 0. 선행 (콘텐츠 로드)
- `E:\Git_Project\FPSRoguelite2`에서 맵 LFS 블롭이 포인터 상태다. PIE 전에 최소한 맵을 smudge:
  `GIT_LFS_SKIP_SMUDGE=0 git lfs pull -I "Content/Maps/**"` (전체 1.2GB smudge는 피하고 맵만).
- ⚠️ 활성 클론 `E:\Git_Project\FPSRoguelite`의 에디터 **Live Coding이 켜져 있으면 이 클론 빌드가 막힌다**(엔진 전역 락). 빌드 필요 시 해제.

## 1. 콘텐츠 계약 (불변 — 코드가 이 전제에 의존)
1. **persistent 레벨이 게임플레이를 호스팅**: 문·boundary blocker는 persistent에. 적은 persistent에 스폰됨(코드). **화이트박스 지오메트리(맵A·맵B)만** 스트리밍 서브레벨.
2. **맵은 수평(XY) 분리**: offset ≥ max(NetCull 200m, 무기 max range, 오디오 감쇠, separation) + 여유. **시작값 ~500m(50000cm)**. 공통 Z대역(수직 스택 금지 — WorldKillZ/ground-trace/DistSquaredXY가 단일 바닥 가정). origin rebasing 금지.
3. **MapId=FGameplayTag** 교차 키. 각 맵의 bounds volume·spawn point·(문 TargetMapId)가 **같은 MapId 태그**를 공유. 미태그=Default 단일맵(무회귀).

## 2. 저작 항목
### 2-1. persistent 런 레벨 (예: 기존 L_Sandbox를 persistent로 쓰거나 신규 L_RunPersistent)
- [ ] persistent 레벨의 **Levels 패널**에 서브레벨 2개 추가: `L_MapA`(Initially Loaded+Visible=ON, 런 시작 로드), `L_MapB`(Initially Loaded/Visible=OFF, 문 파괴 시 스트림).
  - ⚠️ 서브레벨을 persistent의 **StreamingLevels 배열**(Levels 패널)에 넣어야 `LoadStreamLevel`이 이름으로 찾는다.
- [ ] 맵A·맵B를 **XY로 ~500m 떨어뜨려** 배치(서브레벨 Transform offset).
- [ ] persistent에 **PlayerStart**(맵A 안, 런 시작 스폰) 배치.

### 2-2. 각 맵(A·B) 서브레벨 안에
- [ ] 화이트박스 바닥/벽 지오메트리(**WorldStatic 콜리전** — 플로우필드 bake가 ECC_WorldStatic 다운트레이스로 바닥을 잡음).
- [ ] `AFPSRFlowFieldBoundsVolume` 1개: 박스를 플레이 영역에 맞춤 + **MapId 태그 설정**(맵A=`Map.A` 등, 맵B=`Map.B`).
- [ ] `AFPSREnemySpawnPoint` 여러 개: **같은 MapId 태그**. 측후방(플레이어 시야 밖 선호는 MinPlayerDistance로). 각 맵에 최소 3~4개.
- [ ] (선택) 맵B에도 PlayerStart 1개(런시작 ready gate 대칭용, 없어도 bounds 박스 중심 다운트레이스로 Z앵커).

### 2-3. 경계 문 (persistent, 맵A↔맵B 사이)
- [ ] `AFPSRDoor`(또는 BP 자식) 배치: **TargetMapId=`Map.B`**, **TargetLevelName=`L_MapB`**(서브레벨 패키지 short name), Durability 설정(혼자 부수기 어렵게 크게).
- [ ] `AFPSRBoundaryBlocker` 배치(문 바로 뒤, persistent): **TargetMapId=`Map.B`**, 박스를 통로에 맞춤. (플레이어만 막음 — 스트림 준비까지 낙하 방지.)

### 2-4. Cook 등록
- [ ] `Config/DefaultGame.ini` [ProjectPackagingSettings] MapsToCook에 persistent + `L_MapA` + `L_MapB` 추가(패키지 빌드 시).

## 3. PIE 검증 (코옵 2인 권장 — `net.AllowPIESeamlessTravel=1`)
- [ ] **문 파괴→스트림**: 맵A 경계 문을 쏴서 파괴 → 맵B가 심리스 스트림-in, 로그 `[MapStream] map 'Map.B' READY`. boundary blocker 해제되어 통과 가능.
- [ ] **새 맵 적 출현(공백 0)**: 맵B 진입 후 **0~3초 내** 스폰포인트(측후방/문밖)에서 적 출현(눈앞 팝업 아님).
- [ ] **추격 연속(경계 grace)**: 한 명이 맵B로, 한 명이 맵A에 남을 때 각 맵 적이 자기 맵 플레이어만 추격(크로스맵 beeline 없음·벽 통과 없음).
- [ ] **빈 맵 드레인**: 전원이 맵A→맵B로 이동하면 맵A 적이 grace(3s) 후 서서히 디스폰(예산 회수). 픽업/문/XP는 잔존.
- [ ] **전역 200 유지**: 적 총합이 맵 수와 무관하게 ~200 상한(맵 수 곱 아님). `FPSR.FlowField.Debug 1`로 per-map 필드 확인.
- [ ] **크로스맵 데미지 0**: 맵A 플레이어가 (물리적으로 불가하지만) 맵B 적을 못 때림(offset 계약+게이트).
- [ ] **late-join**: 늦게 합류한 클라가 이미 뚫린 문 상태 + 스트림된 맵B + 양 맵 적을 정상 수신(뜬 적 1~2프레임은 알려진 minor).
- [ ] **분산 스트레스**: 1/1/1/1·2/2·3/1 배분에서 본대(2인+) 전선이 솔로보다 조밀한지(allocator 임시 가중치) 확인.
- [ ] **단일맵 무회귀**: 기존 L_Sandbox(미태그) 단독 플레이가 종전과 동일(전역 캡이 200으로 조정된 점만 다름 — 필요 시 `GlobalAliveCap` 튜닝).

## 4. 튜닝값 (전부 실측 후 조정 — 코드 상수)
`FPSREnemySpawnSubsystem`: GlobalAliveCap=200 · SeedReserve=8 · MapGroupBonus=1 · EmptyMapDrainPerTick=4 · MapDrainGraceSeconds=3.
`AFPSREnemyBase`: NetCullDistance=200m. 맵 offset(콘텐츠)=~500m. `AFPSRMapStreamSubsystem`: StreamTimeout=20s.

## 5. 후속(Tier 1 / 문서화된 minor)
- late-join per-client `ClientMapReady` ack(뜬 적 완전 제거) · 점유맵 원거리 적 silent recycle(안전게이트) · 전환 추적자(문 넘은 플레이어를 소수 적이 실제 추격) · 콘텐츠-aware allocator(미션/보스/엘리트를 2인+ 그룹에 집중) · rally pad · dedicated occupancy volume(Priority/grace).
