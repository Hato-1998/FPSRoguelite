# NEON-V 3인칭 캐릭터 — 스플래툰 체형 × 아케이드 복셀 삼면도 → Meshy (새 세션 실행 프롬프트)

> 작성 2026-09-07(Sonnet, 문서 갱신 세션). **새 세션이 그대로 복붙해 착수**하는 실행 문서다.
> **트랙 재정의**: 원래 Tripo 다시점 생성 트랙(구 파일명 `NeonV_Tripo_ResumePrompt.md`)이었으나, 3D화 도구가 **Meshy Image-to-3D**로 확정되고 체형·격자·피부·리본·복셀화 트랙 5건이 사용자 결정을 마치면서(ADR 0016 「사용자 결정 기록 — 2026-09-07」) 이 문서로 재정의됐다.
> 이번 행의 산출물은 **삼면도 생성용 이미지AI 프롬프트까지**다(§4). Meshy 프롬프트 자체는 삼면도가 나온 뒤 **별도 세션**에서 토론한다(§8①).
> 보드 = 「NEON-V 3인칭 캐릭터 — 삼면도 설계 프롬프트 → Meshy 3D화 (Tripo 트랙 재정의)」.

**복붙용 첫 지시문**
```
Docs/NeonVCharacter_ResumePrompt.md 를 읽고 진행한다. main 에서 직접 작업(트렁크). 보드 클레임부터(§0).
§1 전제(스켈레탈 메시·캡슐 162cm 근거·함정 6종)를 먼저 읽는다. §2 스펙 정본(높이·폭 표, 색 번역표)이 이 문서 전체의 진실원천이다.
이번 행의 산출물 = §4 삼면도 이미지AI 프롬프트(한/영 2벌) 확정 → 사용자가 이미지 생성 → §6 판정 → §7 크롭까지.
복셀화 트랙 A/B′ 결정과 Meshy 프롬프트 토론은 범위 밖(§8) — 여기서 착수하지 않는다.
```

## §0 세션 시작 방법

1. `Game.md` §0-1 라우팅 → `Docs/SSOT/ArtDirection.md §A`(색 대역) · `§B-3`(격자 계층, 3.75cm 행) · `§B-13`(피부 대역, 신설) · [ADR 0016](Architecture/0016-art-direction-retro-arcade-pixel.md)(D4·D7·I1~I8, 「사용자 결정 기록 — 2026-09-07」) · [ADR 0015](Architecture/0015-first-person-gun-only-hidden-arms-driver.md)(1P = 총만) · `Docs/MeshyDronePipeline_ResumePrompt.md`(같은 파이프라인의 선행 사례 — 프롬프트·크롭 절차의 형식 본보기, §2 4단계·§6 결과표 = 트랙 A/B′ 비교의 선행 답이 나올 자리, 이 문서 작성 시점 미기입).
2. **보드 클레임(§6-9)** — 행 「NEON-V 3인칭 캐릭터 — 삼면도 설계 프롬프트 → Meshy 3D화 (Tripo 트랙 재정의)」를 이어받는다(구 행 「NEON-V 3인칭 캐릭터 — Tripo 다시점 생성 → Blu 리그 통합 검토」의 재정의). 담당 갱신 후 재조회로 경합 확인.
3. **에디터는 사용자가 켠다/닫는다.** 이미지 생성·Meshy 실행도 사용자 계정(§9). 이 세션은 프롬프트 저작·크롭·문서만 한다.
4. `git status` 로 다른 세션의 미커밋 작업을 확인하고 **건드리지 말 것**. 커밋은 자기 파일만 명시 경로로.

## §1 전제 — 적(정적 메시)과 다르다: 캐릭터는 스켈레탈 메시

- **3인칭 몸체 = Blu 스켈레톤(108본) 스켈레탈 메시.** 슬라이드 4클립·벽 4자세·로코모션이 전부 이 스켈레톤 위에 있다(`Docs/Architecture/0002*`, 메모리 `blender-locomotion-anim-authoring`). Meshy 결과는 새 메시(+선택적 자체 리깅)라 **임포트만으로는 못 쓴다** — 스키닝·웨이트 전이가 반드시 필요하다(§8③).
- **1P = 총만 표시**([ADR 0015](Architecture/0015-first-person-gun-only-hidden-arms-driver.md)) → 이 몸체는 **협동 팀원 시점(3P)** 과 로비/초상화에서만 보인다. 투자 규모를 여기에 맞춰 판단한다.
- **아트 방향 = 확정.** 구 트랙(Tripo) 당시엔 미결이었지만, 2026-09-03 레트로 아케이드 픽셀로 전환 → [ADR 0016](Architecture/0016-art-direction-retro-arcade-pixel.md) 채택 → 2026-09-07 NEON-V 3P 캐릭터 결정 5건(체형·격자·피부·리본·트랙)까지 끝났다. 이 몸체의 룩 = **스플래툰 비례 실루엣 + 복셀 3.75cm 격자**(§2). 이제 이 트랙이 아트 방향을 선점하는 게 아니라 — **ADR 0016 이 정한 규칙을 따르는 쪽**이다.

🔴 **키 근거(캡슐 = 162cm, 무변) — 이 문서의 모든 cm 수치가 여기서 출발한다.**
C++ `AFPSRCharacter` 는 `InitCapsuleSize(34.0f, 88.0f)`(=176cm, `Source/FPSRoguelite/Private/Hero/FPSRCharacter.cpp:66` 실측 확인)지만 **BP 가 오버라이드해 게임 실제 값은 162cm**다(적 진영에도 같은 패턴 전례가 있다 — `FPSREnemyMetricsSubsystem.cpp` 주석: C++ 은 `InitCapsuleSize(40, 90)`을 주장하지만 `BP_EnemyMeleeBase` 가 30/80 으로 덮어쓴다). 교차검증 = `Docs/Troubleshooting.md`(F1) "리그는 Blender에서 **키 184.0cm**인데 UE는 `CharacterMesh0`를 **0.8806배**로 넣어 162cm를 만든다" + 캡슐 **162 ÷ 0.8806 = 184cm**(리그 키와 일치). 레퍼런스 시트(`NeonV_CharacterSheet_source.png`)의 신장 표기 162cm 와도 이미 통일돼 있다.
162 ÷ 3.75 = 43.2 로 정수가 아니므로 **161.25cm(43층)** 으로 잡는다(0.75cm 차, 캡슐 안, 적 격자의 1/2 유지). **이 트랙은 캡슐·카메라·StepHeight·`WeaponAttachScale`·3P 애니 8종을 건드리지 않는다** — 키를 실제로 줄이는 "진짜" 축소는 이것들을 전부 리타깃해야 해서 ADR 0016 I2(룩 교체는 렌더 메시·머티리얼·스프라이트·UI 스킨만) 위반이라 채택하지 않았다.

**기존 함정 6종**(전부 이 경로에 있다):
1. FBX 미터→cm 100배 축소(`fbx-metre-cm-armature-scale-trap`)
2. OBJ 임포트 Y축 반전(`Docs/Troubleshooting.md` D12) — **OBJ 전용**. FBX/GLB 는 Interchange 가 처리하지만 그래도 바운드 min/max 로 정면 축을 반드시 확인할 것(Meshy export 는 FBX/GLB)
3. 기존 스켈레톤을 지정한 임포트가 그 스켈레톤 에셋을 통째로 덮어씀(`ue-import-overwrites-target-skeleton`)
4. 리그드 GLB 헤드리스 임포트 = 에디터 크래시(`glb-import-crash-use-fbx`) — Meshy 산출물이 리깅까지 포함되면 해당
5. IK 리타게터 무음 실패(`ik-retargeter-op-chain-mapping`)
6. Blender 1cm ≠ UE 1cm, 환산 0.8806배

## §2 스펙 정본

> 이 절이 이 문서 전체의 **진실원천(source of truth)**이다 — §4 프롬프트의 모든 수치는 여기서 나온다. 값을 바꿀 일이 있으면 여기부터 고친다.

### §2-0 격자 3.75cm 선정 근거

병목은 머리가 아니라 사지 굵기다 — 복셀에서 팔이 1칸이면 막대로 보인다.

| 격자 | 총 층수 | 두개골 30cm | 맨팔 7.5cm | 종아리 7.5cm | 판정 |
|---|---|---|---|---|---|
| 7.5 cm (적과 통일) | 21.5 | 4칸 | **1칸** ❌ | 1칸 ❌ | 사지가 막대 — 불가 |
| 5 cm (환경과 통일) | 32 | 6칸 | 1.5칸 ❌ | 1.5칸 ❌ | 사지 미달 + 환경과 같은 픽셀 크기라 벽에 묻힌다 |
| **3.75 cm** ✅ | **43** | 8칸 | **2칸** ✅ | 2칸 ✅ | 최소선 충족 + 적의 정확히 1/2 = 의도된 위계 |
| 2.5 cm (무기와 통일) | 64.5 | 12칸 | 3칸 | 3칸 | 여유는 크나 원거리에서 "픽셀"이 아니라 "매끈한 로우폴리"로 읽힌다 |

근거 3줄(핵심원칙 4 형식):
1. **제1원리** — 20m에서 아군 3명을 적 200~300 속에서 읽어야 한다. 적(7.5)의 정확히 절반이면 같은 거리에서 아군이 2배 세밀하게 읽혀 "덩어리=적 / 히어로=아군"이 형태만으로 갈린다.
2. **엔진 기본값·기존 인프라** — `ArtDirection.md §B-3` 격자표에 행을 추가할 뿐 기존 세 행(5/7.5/2.5)은 건드리지 않는다. I4(한 오브젝트 한 격자)는 오브젝트 **안**의 규칙이라 클래스 추가는 위반이 아니다.
3. **프로젝트 제약** — 캐릭터 격자는 콜리전과 무관(판정은 캡슐이 진다) → ADR 0012 불변식 2 무접촉.

### §2-1 높이 배분 (43층 × 3.75cm = 161.25cm)

| 부위 | 층 | cm |
|---|---|---|
| 신발(밑창~발목) | 4 | 15.00 |
| 다리(발목~골반) | 19 | 71.25 |
| 몸통(골반~어깨) | 11 | 41.25 |
| 목 | 1 | 3.75 |
| 두개골(턱~정수리) | 8 | 30.00 |
| **합계** | **43** | **161.25** |

### §2-2 폭·깊이

| 부위 | 칸 | cm | 비고 |
|---|---|---|---|
| 헤드 매스(머리카락 포함) 폭×깊이×높이 | 11 × 11 × 12 | 41.25 × 41.25 × 45 | **어깨(8칸)보다 넓다** = 스플래툰 실루엣의 핵심 |
| 두개골 폭 | 7 | 26.25 | |
| 어깨 폭 | 8 | 30.00 | 좁다 |
| 몸통 깊이 | 5 | 18.75 | |
| 팔 길이(어깨~손끝) | 16 | 60.00 | |
| 맨팔 굵기 | 2 | 7.50 | 🔴 **2칸이 절대 하한** |
| 소매 포함 팔 굵기 | 3 | 11.25 | |
| 손 | 2 | 7.50 | |
| 허벅지 굵기 | 4 | 15.00 | |
| 종아리 굵기 | 2 | 7.50 | |
| 신발 길이×폭×높이 | 8 × 4 × 4 | 30 × 15 × 15 | 🔵 **과장이 스플래툰 정체성** |

### §2-3 파생값

- 등신 = 43 ÷ 8 = **5.375등신**
- 다리 총(신발 포함) = 23층 = 86.25cm = 전체의 **53.5%**(스플래툰의 긴 다리)
- 실루엣 최대 높이 = 44층 = 165cm(정수리 위 머리카락 1층). 캡슐 162 위로 3cm 나가지만 **콜리전은 캡슐이 지므로 무해**
- **Blender 작업 값 = UE cm ÷ 0.8806** → 161.25 ÷ 0.8806 = **183.1cm**

### §2-4 스플래툰 요소의 번역

| 스플래툰 | 채택 | 번역 |
|---|---|---|
| 촉수 머리카락(큰 헤드 매스) | ✅ 볼륨만 / ❌ 오징어 픽션 | NEON-V 보브 헤어를 헤드 매스 11×11×12칸으로 확대 + 뒤로 흐르는 **굵은 가닥 2~3개**(끝을 뭉툭하게, **최소 3칸 두께** — 가늘어지면 복셀에서 계단으로 부서진다). 모티프 = 오징어가 아니라 **뉴럴 링크 케이블 다발**(NEON-V 원안 "Neural Link" + ADR 0016 D6 다이브 픽션 정합) |
| 과장된 신발 | ✅ 적극 채택 | 30cm(8칸). **아래쪽 실루엣 앵커**가 생겨 20m에서 팀원 발 위치가 바닥에 안 묻힌다 |
| 좁은 어깨·가는 사지 | ✅ | §2-2 스펙표. 단 2칸 하한 사수 |
| 짧은 목·큰 머리 | ✅ | 로비 근거리에서 바이저가 화면을 채운다 = 미소녀 채널 |
| 잉크·캡·오징어 이빨 | ❌ | 픽션 불일치 |

### §2-5 신발 색 주의

🔴 `ArtDirection.md §A-3-2` 바닥 배선이 밝은 시안(`#39B8B0`/`#5FE0D2`)이다. 신발을 아군 시안으로 칠하면 **발이 배선에 묻힌다**(ADR 0016 검증 시나리오의 "아군 아웃라인이 배선과 안 섞이는가"가 이미 경고한 항목). → **신발 본체 = 어두운 대역, 액센트만 보라 `#8B6BFF` 얇게. 시안 대면적 금지.**

### §2-6 색 번역표

| 레퍼런스(NEON-V) | 번역 | 근거 |
|---|---|---|
| `#00F0FF` 시안 발광 트림 | `#4FD8FF`(팀원) / `#8B6BFF`(자기 자신) | `§A-3-5` 예약색. **단일 요소로 묶어 런타임 팀 컬러 교체** |
| `#00B0FF` 보조 블루 | `#2E9BFF` 저채도 | `§A-3-5` |
| `#1A1F2E` / `#0A0E16` 의상 다크 | 그대로 | 이미 캐릭터 대역 안 |
| `#FFFFFF` 순백 | **금지** → `#EAF6FF` | `§A-1` 절대규칙 1(0%·100% 금지) |
| 살색 | V 45~55 / S 12~20 저채도 뉴트럴 | `§B-13`(신설) |
| 주황·빨강·마젠타 | **전면 금지** | `§A-3-4` 적 예약 대역 |

## §3 복셀화 트랙 (열린 결정)

| | 트랙 A — 복셀 네이티브 저작 | 트랙 B — 보통 메시 + 복셀 셰이더 | 트랙 B′ — 보통 저작 → 오프라인 복셀 베이크 |
|---|---|---|---|
| 방식 | 처음부터 3.75cm 격자로(쩝쩝이·드론 방식) | 매끈한 메시에 머티리얼로 픽셀 스냅 | 매끈하게 만들고 Blender Remesh > Blocks 3.75cm 로 굽는다 |
| ADR 0016 지위 | D1·D7 ①② 정본 경로 | 🔴 **안 E — 이미 기각됨** | ✅ D7 ③ 채택된 후보 경로 |
| 실루엣 | 진짜 각짐 | 🔴 안 바뀜 — 윤곽은 매끈, 표면만 계단 → ADR 원문 "가짜 픽셀로 읽힌다" | 진짜 각짐 |
| Meshy 적합 | ❌ AI는 복셀 캐릭터를 잘 못 만든다 | ✅ | ✅ AI가 가장 잘하는 영역 |
| 리깅·스키닝 | ❌ 2칸 두께 팔은 오토리그가 어렵다 | ✅ 표준 | 🟡 매끈한 상태로 리그 → 베이크 후 웨이트 전송 |
| tris | 최소(greedy meshing) | 최대 | 중간(Remesh 밀도로 제어) |

**트랙 B 기각 사유는 비용이 아니라 정합성이다** — 우리 화면은 복셀 적(7.5) + 복셀 환경 면(5) 위에서 돌아가고, 그 안에 윤곽만 매끈한 캐릭터를 놓으면 20m에서 "복셀 세계 속 이물질"로 읽힌다. (WPO 로 월드 격자에 정점을 스냅해 실루엣까지 바꾸는 변형도 있으나, 스키닝 뒤에 걸리는 값이라 캐릭터가 움직이면 표면이 헤엄치고 결과가 정점 밀도에 종속된다 — ADR 0016 I6 이 1P 무기에 대해 이미 경고한 현상.)

🔑 **삼면도는 세 트랙 공통이라 트랙 결정을 지금 할 필요가 없다.** 매끈하게 그린 삼면도는 세 트랙 모두를 먹인다(Meshy Voxel 프리셋도 매끈한 입력을 받는다). 반대로 각진 복셀 삼면도로는 트랙 B′의 매끈한 모델을 못 얻는다 — 그래서 §4 프롬프트는 **매끈한 그림체**를 요구한다. 게다가 드론 행이 지금 정확히 A vs B′를 A/B 테스트 중이다(`Docs/MeshyDronePipeline_ResumePrompt.md` §2 4단계 · §6 결과표, 이 문서 작성 시점 미기입) — 그 답이 나오면 캐릭터도 같이 정해진다.

**잠정 권고 = 트랙 B′.**

### §3-1 스키닝 통합 경로 (구 문서 §3 경로 1~3 계승 — 트랙과는 다른 축)

복셀화 트랙(A/B/B′)과는 별개 축이다: **어떤 트랙으로 메시를 만들든, 그 메시를 Blu 스켈레톤(108본)에 입히는 방법**은 아래 세 가지가 그대로 후보다(구 경로 4 "복셀 변환"만 위 §3 트랙 A/B/B′ 표로 세분됐고, 이 표는 스키닝 방법만 다룬다).

| # | 경로 | 살아남는 것 | 비용·위험 |
|---|---|---|---|
| 1 | **Blu 스켈레톤에 새 메시 스키닝**(Blender: 정렬 → 웨이트 전이 → FBX) | 애니·ABP·1P 파이프라인 전부 | 사람이 Blender 에서 며칠. 비율 차이만큼 웨이트 손질. 권장 후보 |
| 2 | Meshy 자동 리깅(제공 시) + IK 리타게팅 | 새 스켈레톤 | 리타게터 무음 실패(`ik-retargeter-op-chain-mapping`) + 슬라이드 발 접지·벽 자세 재손질. 1 보다 크고 회귀 위험 높음 |
| 3 | 정적 포즈 프록시(로비 전시·초상화·팀원 마커) | 리깅 불필요 | 게임플레이 몸체는 안 바뀜. 결정 전 임시 활용 가능 |

이 경로 결정도 **삼면도 이후**(§8②) — 이 문서는 착수하지 않는다.

**실측 절차(구 문서 §3-1 계승, 다음 세션 — 트랙·경로 결정 전에 먼저 한다)**:
1. Meshy 산출물(GLB/FBX) → Blender(`F:\Blender\blender.exe` 5.1) 임포트, `NeonV_locomotion.blend`(블랜더 repo `C:\Users\koras\Desktop\작업\개발작업\블랜더`)의 Blu 아마추어 옆에 놓는다. **키 183.1cm(Blender 작업 값, §2-3)로 맞춘다.**
2. 대조 항목: 어깨폭·팔 길이·다리 길이·머리 크기 비율 차 / 폴리 수 / 텍스처 유무 / 닫힌 메시인지(머리카락 가닥·백 모듈 분리 여부).
3. 보고 형식: 경로 1(직접 스키닝) 예상 공수(시간), 경로 2(자동 리깅+리타깃) 예상 공수, 어느 부위가 스키닝 위험인지 — **숫자로**. 트랙·경로 결정은 사용자.

## §4 삼면도 이미지AI 프롬프트

두 언어로 준비한다. 이미지 생성 AI 는 사용자 계정에서 실행(§9) — 아래 프롬프트를 그대로 붙여 넣는다.

> ⚠️ **이미지 AI 는 숫자 비례를 지키지 않는다.** "5.375등신"·"53%" 같은 수치는 걸어 두되 기대하지 말고,
> 실제로 형태를 잡는 것은 **시각적 대체 지시**다 — "머리 폭 > 어깨 폭", "보브가 얼굴보다 넓다", "부츠가 과장되게 크다".
> 수치 준수 여부는 그림에서 못 읽으므로 **검수는 §6 체크리스트로** 한다.

**한국어**
> 게임 캐릭터 디자인 시트, **정면·측면·후면 3뷰**를 한 장에 나란히, 같은 배율과 같은 높이 정렬(세 뷰 모두 정수리와 발바닥이 같은 수평선), 균일한 회색 단색 배경, 그림자 없음, 글자·치수선·화살표 없음.
> 포즈: A포즈 — 양팔을 몸통에서 약 45도로 자연스럽게 벌리고 손가락은 편 채(주먹 아님), 다리는 어깨너비로 살짝 벌린다. 무기를 들지 않은 빈손. 정면은 무표정 또는 살짝 결연한 표정.
> 대상: 사이버펑크 그리드 스트라이커 캐릭터. **옆으로 볼륨 있게 부푼** 짧은 단발 보브 헤어 — 길이는 어깨에 안 닿지만 **폭은 얼굴보다 눈에 띄게 넓다** — 에 뒤로 흐르는 두껍고 뭉툭한 머리카락 가닥 2~3줄(가는 잔머리 없이 케이블 다발처럼 굵게), 눈을 덮는 랩어라운드 바이저(각진 렌즈, 헤드셋 밴드). 크롭 재킷(배꼽이 드러나는 길이, 카라를 세운 형태)과 그 아래 스트랩형 가슴 보호대, 하이웨이스트 핫팬츠(파우치 달린 택티컬 벨트), 허벅지에 스트랩형 다리 기어, 발목까지 오는 과장되게 큰 플랫폼 컴뱃 부츠.
> 비례(정확히): 전체 5.375등신 — 머리(머리카락 포함)가 크고, **머리 폭이 어깨 폭보다 넓다**(가장 중요한 규칙). 어깨는 좁게, 팔다리는 가늘되 완전한 막대는 아니게(원통형 두께감 유지). 다리(허벅지+종아리+부츠)가 전체 키의 약 53%.
> 색: 의상은 짙은 남색·검정. 재킷 솔기·벨트·부츠·다리 기어를 따라 흐르는 발광 트림 라인은 밝은 하늘색 시안 한 가지로 통일(다른 요소와 분리되는 단일 색 — 나중에 교체됨). 허리 뒤로 흐르던 리본은 완전히 제거하고 대신 등에 사각형 발광 패널(파워 유닛 모듈)을 넣는다. 보조 색은 살짝 어두운 블루. 흰색 대신 아주 옅은 오프화이트. 피부는 채도를 크게 낮춘 창백하고 중립적인 톤(붉은기 없이). **주황·빨강·마젠타·핑크는 화면에 한 픽셀도 넣지 않는다.**
> 스타일과 금지: 매끈한 셀 셰이딩 애니메 스타일(각진 복셀이나 픽셀 형태 아님), 평면 채색 + 최소한의 음영. 무기(총·검·수류탄) 들지 않기, 흐르는 리본 없음, 촉수·오징어·잉크·모자 모티프 없음, 순백·순흑 없음, 배경 소품 없음.

**영어**
> Game character design sheet: **three views — front, side, and back** — side by side on one canvas, same scale, aligned to the same height (top of head and sole of feet on the same horizontal line in all three), flat neutral-gray background, no shadows, no text, no dimension lines or arrows.
> Pose: A-pose — both arms held out from the torso at roughly 45 degrees, fingers open and relaxed (not fists), legs slightly apart at shoulder width. Empty hands, no weapon held. Neutral or mildly determined front-facing expression.
> Subject: a cyberpunk "grid striker" character. A short bob haircut **with strong lateral volume** — it does not reach the shoulders in length, but it is **noticeably wider than the face** — with 2-3 thick, blunt-tipped strands of hair flowing backward (no thin wispy strands — thick like a bundle of cables), a wraparound visor covering the eyes (angular lenses, headset band). A cropped tactical jacket (short enough to bare the midriff, popped collar) over a strapped chest-armor piece, high-waisted hot pants with a pouched tactical belt, strapped leg gear on the bare thighs, and exaggeratedly large platform combat boots reaching the ankle.
> Proportions (exact): overall body is 5.375 heads tall — the head (hair included) is large, and **the head's width is wider than the shoulders** (the single most important rule). Shoulders are narrow, limbs are slim but not stick-thin (keep visible cylindrical thickness). Legs (thighs + calves + boots) make up about 53% of total height.
> Colors: clothing stays dark navy/near-black. All glowing trim lines along the jacket seams, belt, boots, and leg gear are unified into one bright sky-cyan accent color (a single, cleanly separable color — it will be recolored later). The old flowing ribbon at the back of the waist is completely removed; replace it with a rectangular glowing panel on the back (a "power unit" module). Secondary accents are a slightly darker muted blue. Any white is a very pale, faintly cool off-white, never pure white. Skin is a strongly desaturated, neutral pale tone (no rosy warmth). **No orange, red, magenta, or pink anywhere in the image.**
> Style and exclusions: smooth cel-shaded anime style (not blocky voxels or pixel art), flat coloring with minimal shading. No weapon in hand (gun/blade/grenade), no flowing ribbon, no tentacle/squid/ink/cap motifs, no pure white/pure black, no background props.

칸 수 주석(문서 참고용 — 프롬프트 문장에는 넣지 않는다, §2 발췌): 헤드 매스 11×11×12칸(41.25×41.25×45cm) vs 어깨 8칸(30cm) · 다리 총 23층(86.25cm) = 53.5% · 등신 43÷8 = 5.375 · 맨팔 2칸(7.5cm) 하한.

## §5 금지 목록

- **무기 3종**(펄스-V 라이플 · 그리드 블레이드 · 데이터 스파이크 수류탄 — 원안 『WEAPON LOADOUT』의 그 무기들) — 별도 액터로 소켓 부착이라 몸에 융합되면 안 된다. 삼면도에는 **그리지 않는다**(빈손).
- 흐르는 시안 리본(원안의 허리 뒤 긴 띠) — 사용자 결정 ④로 완전 제거, 등 백 모듈 발광 패널로 대체(§2-6).
- 오징어·촉수·잉크·캡(모자) 모티프 — 스플래툰에서 가져오는 건 체형뿐, 픽션은 가져오지 않는다.
- 주황·빨강·마젠타·핑크 계열 — `ArtDirection.md §A-3-4` 적 예약 대역.
- 순백(`#FFFFFF`)·순흑 — `§A-1` 절대규칙 1.
- 사진 질감·노이즈 텍스처 — `§A-5` · ADR 0016 I7(텍스처 없음, 평면 채색만).
- 배경 소품·글자·치수선·화살표.

## §6 판정 체크리스트

- [ ] 3뷰(정면·측면·후면) 배율·높이 일치
- [ ] 🔴 **세 뷰의 디자인이 같은 캐릭터인가** — 한 장 3뷰의 최대 실패 모드다. 이미지 AI 는 후면 뷰에서 요소를 조용히 바꾼다(등 백 모듈이 사라짐 · 부츠 색·형태가 달라짐 · 머리카락 가닥 수가 바뀜 · 벨트 파우치 위치 이동). **Meshy multi-view 는 세 장이 같은 캐릭터라고 가정**하므로 여기가 어긋나면 3D 가 뭉개진다. 부위별로 세 뷰를 하나씩 대조할 것
- [ ] A포즈(팔 45도, 손가락 편 상태, 다리 살짝 벌림)
- [ ] 맨팔 굵기가 2칸(7.5cm) 아래로 안 내려갔는가(막대처럼 가늘지 않은가)
- [ ] 헤드 매스(머리카락 포함) 폭이 어깨보다 넓은가
- [ ] 신발 길이 30cm 급의 과장이 유지됐는가
- [ ] 무기 0(빈손)
- [ ] 리본 0
- [ ] 촉수·오징어·잉크·캡 요소 0
- [ ] 주황·빨강·마젠타 픽셀 0
- [ ] 순백 픽셀 0
- [ ] 배경 단색·글자 없음
- [ ] 바이저가 정면 중앙

실패 시 재지시 요령 = **"이 그림에서 X만 제거하고 나머지는 한 픽셀도 바꾸지 마라"**(제거·유지 편집이 이미지 AI 가 가장 잘하는 종류다). **재스타일 요청("다시 그려줘", "스타일 바꿔줘")은 형태를 흐린다** — 하지 말 것.

## §7 크롭 절차

이미지 AI 는 한 장에 3뷰를 함께 그려야 일관성(같은 얼굴·같은 의상)이 유지된다. Meshy Multi-view 입력은 뷰당 별도 파일을 받으므로, 나온 한 장을 3장으로 잘라 `Docs/Handoff/NeonV_Arcade/`에 저장한다.

**선례 재료표**(`Docs/Handoff/NeonV_Tripo/` — 재정의된 구 Tripo 트랙이 남긴 재료. 이번 삼면도는 새 이미지라 좌표는 다시 재야 하지만 **방식은 그대로 계승**한다):

| 파일 | 무엇 |
|---|---|
| `NeonV_CharacterSheet_source.png` | 사용자가 준 캐릭터 시트 원본(1672×941). MODEL SHEET 패널 = 정면/측면/후면, 세 뷰의 머리 꼭대기·발바닥 y 가 같다(배율 정합) |
| `NeonV_front.png` / `NeonV_side.png` / `NeonV_back.png` | 다시점 3D화 입력용 1024² 3장. 원본에서 자른 좌표 = 정면 (655,95)-(845,540) · 측면 (850,95)-(945,540) · 후면 (965,95)-(1140,540), 캐릭터 높이 = 캔버스 88% 로 통일, 배경 = 패널 내부 평균색 (2,14,29) |

권장 파일명(이번 삼면도용, 계승 규약 — 강제 아님): `NeonVArcade_source.png` + `NeonVArcade_front.png` / `NeonVArcade_side.png` / `NeonVArcade_back.png` → `Docs/Handoff/NeonV_Arcade/`.

## §8 다음 행에 넘길 것

① **Meshy 프롬프트 토론** — 이번 세션 범위 밖(사용자 지시). 삼면도가 나온 뒤 별도 세션에서.
② **복셀화 트랙 A/B′ 결정 + 스키닝 통합 경로 결정**(§3·§3-1) — 드론 A/B 테스트(`Docs/MeshyDronePipeline_ResumePrompt.md §6`) 결과가 나오면 함께 확정.
③ **알려진 절벽 — 스켈레탈 메시 정합**: 캐릭터는 정적 메시가 아니라 스켈레탈 메시라, Meshy 산출물에서 리깅·스키닝·기존 Blu 스켈레톤(108본, 본 이름·계층·ref pose) 정합이 전부 사람 손이다. §1의 함정 6종이 전부 이 단계에 있다. 이 문서(삼면도)는 이 절벽 앞에서 멈춘다 — 실측 절차·Blender 작업 파일 경로는 §3-1 에 준비돼 있다.

## §9 범위 밖

이 세션이 하지 않은 것: 이미지 생성 실행(사용자 계정) · Meshy 실행(사용자 계정) · Blender 정렬·리깅·스키닝 · BP/DataAsset 변경 · 에디터 기동(사용자가 켠다) · 코드·에셋 수정(이 세션은 문서 3건만 — `ArtDirection.md`·ADR 0016·이 문서).
