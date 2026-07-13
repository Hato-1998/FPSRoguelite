# Plan Consult — 무기 조립 툴 개편 (고정 소켓 + 파츠별 스택 진화)

> `/plan-consult` FULL. 2026-07-13. 자문 전용(코드/에셋 무변경). Codex(gpt-5.5) 적대 2R 수렴. 원시=`Docs/Review/_raw/20260713-17{3622,4032}-*.md`.
> 채택 시 구현은 별도 승인 단계. 관련 메모리: [[weapon-modular-evolution-scope-plan]]·[[synty-modular-parts-shared-origin]]·[[polymorphic-instanced-uobject-direct]]·[[extensibility-first-designer-tooling]]·[[card-pool-routing]].

## 1. Intake
- **Mode: FULL** — 하드트리거 3건(데이터 마이그레이션 · phase 경계 구조변경[커밋된 W-U1 갈아엎음] · 결정축 3+).
- **plan_type: mixed** (backend/system 스키마 primary + tooling/workflow 툴 secondary). 렌즈 = 제2 아키텍트/레드팀 고정, 종료 전 툴 사용성 축 강제.
- **라운드 정책: Deep Delta-Gated** (되돌리기 비용 큼·장기영향). 2R에 10개 divergence 축 소진 → 수렴.

## 2. 결과 플랜 (수렴)

**데이터 모델 (struct — UObject/다형조건 폐기)**
- 기존 필드 `WeaponParts1P`의 struct `FFPSRWeaponPartAttachment`를 **제자리 확장**(필드명 유지 → 구조 파츠 마이그레이션 0):
  - `+ TSoftObjectPtr<UFPSRWeaponFragment> EvolutionFragment;` (null = 순수 구조 파츠)
  - `+ TArray<FFPSRWeaponPartStage> Stages;` — `FFPSRWeaponPartStage { int32 MinStacks(ClampMin 1); TSoftObjectPtr<UStaticMesh> Mesh; FTransform Offset; FFPSRWeaponScopeDescriptor Scope; }`
  - 기존 `{Part(=base mesh), Socket(=고정 마운트), Offset, Scope}` 유지. Base = 0단계.
- **삭제**: `PartRules` 필드 + `UFPSRWeaponPartRule` + `UFPSRWeaponPartCondition` 계층 전체 + `Weapon.Slot.*` 태그. (미커밋·phase브랜치 한정이라 main churn 0.)
- **셀렉터**: 각 엔트리 — `EvolutionFragment` 있으면 winner = `MinStacks ≤ 현재스택` 중 **최고 MinStacks**, 없으면 Base. resolved `FFPSRWeaponPartAttachment{mesh=winner.Mesh, Socket=엔트리 고정소켓, Offset=winner.Offset, Scope=winner.Scope}` 출력. **런타임 `RebuildPartsFromSelection`·W-U2 스코프 코드 무변경.**
- **⚠️ `ComputeSignature` 수정(필수·잠재버그)**: 메시 경로만 해시 → **Offset+Scope(+Socket)까지 해시**. 안 하면 메시 동일·오프셋/스코프만 다른 stage 전환이 리빌드 안 됨 → ADS/FOV/오버레이 stale.

**IsDataValid (저작-타임)**
- MinStacks 중복(동일 엔트리 내) = **ERROR**(비결정 승자). · MinStacks=0 stage = **ERROR**(Base shadow). · EvolutionFragment 있는데 Stages 비었으면 warn(=항상 Base). · stage.Mesh null = warn. · **진화 슬롯의 각 stage 메시가 슬롯이 사이트일 때 AimSocket 보유하는지 = ERROR**(조준감 조용한 회귀 차단). · 도달불가 MinStacks(카드 MaxStacks 초과) = warn.
- 소켓 라벨 **DA 내 중복 = ERROR** + 공백/문자 정규화.

**툴 (조립기)**
- **(요청1) 슬롯별 고정 소켓 이름 필드** — 사용자 입력, 메시 바뀌어도 불변. 베이크 = `SOCKET_Mount_<사용자이름>`(prefix 유지 = 툴 네임스페이스 정리용). 중복/정규화 검증.
- **(요청2) 진화 패널** — 엔트리 선택 → stage 추가(프래그먼트 1개 + MinStacks + 메시) → **기존 단일-파츠 기즈모 재사용**으로 각 stage 프리뷰(컴포넌트 메시만 스왑)·배치 → 베이크가 stage.Offset 기록. stage별 Offset은 데이터에 존재(Synty 파츠 비공유원점).
- 필드 DisplayName/주석/검증문구에서 "always-attached" 삭제 → "1P 파츠 슬롯(구조/진화)"로.

**검증**: 빌드 `-NoXGE` `Result: Succeeded` + validate-data 스모크 + 머지 시 Codex. PIE=사용자.
**§2-A 격리 falsifiable 게이트**: 파츠 셀렉터/파츠 파일에서 `Card`/`Draw`/`Grant`/`Save`/`Replication` 참조 grep = **0**. "최종진화 시 카드 소진"은 파츠系 책임 아님 = 기존 카드 MaxStacks skip의 결과.

**단계**: A 코어(스키마+셀렉터+시그니처+IsDataValid; 빌드/스모크) · B 툴 고정소켓 · C 툴 진화패널(**축소형**: 기존 기즈모 재사용, 새 툴 아님) · D 콘텐츠 재저작+PIE(사용자).

**범위 밖(DEFER)**: PostLoad 자동 마이그레이션(불요=필드명 유지) · 다형 조건 · stage별 독립 소켓(공유 고정소켓만) · 다중 프래그먼트 조건.

## 3. 수렴 로그 (초안→최종)
- **R1**: ① UObject 슬롯 전면화 = 과설계 → **struct + `{Fragment,MinStacks}`** 로 다형조건 폐기(=Instanced-in-struct 문제·마이그레이션 대부분 소멸) [수용]. ② PostLoad 자동변환 = 지뢰 → 필드명 유지로 마이그레이션 회피 [수용, R1안보다 더 강하게]. ③ Phase C 스코프 폭탄 → 축소(기존 기즈모 재사용) [수용]. ④ §2-A 정적 include 게이트 추가 [수용]. ⑤ 다형조건 = 지금 요구엔 보일러플레이트, 확장성은 셀렉터 경계로 [수용, 단 표준지침과 충돌 → §4].
- **R2**(divergence): ⑥ **`ComputeSignature` 메시-only = 잠재버그**, Offset/Scope 포함 [수용·필수]. ⑦ MinStacks=0/중복/음수 = ERROR급 [수용]. ⑧ 소켓 사용자라벨 충돌(중복/정규화/rename 고아) → 검증+정규화, Codex는 display-only+GUID 권고 [부분수용 → §4]. ⑨ **stage 메시별 AimSocket 존재 검증** 없으면 ADS 조용한 회귀 [수용]. ⑩ 커밋된 W-U1 클래스 삭제 vs dormant-keep → phase브랜치 미머지라 **삭제** [수용, churn 수용].
- 기각: 없음(전 지적이 delta로 전환).

## 4. 미해결 · 사용자 결정 필요
### D1. 다형 조건 폐기 (표준 지침 충돌)
- **경위**: R1-⑤/R2에서 "한 파츠=한 카드·스택" 명세엔 `UFPSRWeaponPartCondition` 폴리모픽이 죽은 무게라는 지적. 백엔드 이유=저작/검증/직렬화 표면↓·Instanced-in-struct 회피. Codex 이유=확장성은 데이터에 노출된 다형객체가 아니라 셀렉터 경계로 더 싸게 보장. **충돌**: 표준 메모리 [[extensibility-first-designer-tooling]]=폴리모픽 서브클래스 선호.
- **왜 사람이**: 사용자의 명시 스코프(스택 단일조건)가 표준 확장성 지침을 **이 기능에 한해** 덮는지 확인 필요. 되돌리기 비용 낮지만 방향 결정.
- **선택지/기준**: (a) **폐기[권장]** — 사용자 Q2 명세 직결·최소 표면. 향후 비스택 조건 필요 시 셀렉터에 추가(중앙 1곳). (b) 유지 — 표준 지침 고수, 대신 툴 UI 복잡·Instanced-in-struct 위험 재도입.

### D2. 소켓 = 실제 이름 지정 vs display-only 라벨
- **경위**: R2-C에서 사용자 free-form 라벨을 소켓명으로 직접 구우면 중복/정규화/rename 고아 위험. Codex 권고=내부 소켓 id는 `SOCKET_Mount_{index/GUID}`, 사용자 라벨은 표시 전용.
- **왜 사람이**: 사용자 요청1 원문="이름을 사용자가 지정"과 **직접 상충**. 안전 vs 요청 원의(原意).
- **선택지/기준**: (a) **사용자가 실제 소켓 이름 지정[권장, 요청 존중]** + DA내 중복 ERROR·정규화·prefix 유지. (b) Codex안 — 라벨 표시전용·소켓 id 내부 GUID(가장 안전하나 "내가 지은 이름이 소켓에 안 박힘").

## 5. 검증 상태
- **확인됨(코드 관찰)**: 카드 MaxStacks skip=`FPSRCardSubsystem.cpp:437`. `HasFragment`=스택 카운트=`FPSRWeaponPartCondition.cpp:24`. 런타임 셀렉터 출력형=`FFPSRWeaponPartAttachment`(RebuildPartsFromSelection:1263). `ComputeSignature` 메시-only=`FPSRWeaponPartSelector.cpp:59`(← 잠재버그 확인). 툴이 WeaponParts1P만 다룸·소켓명=컴포넌트명 유도=`FPSRWeaponAssemblerHelpers.cpp:92`.
- **추정**: struct 내 stage 배열이 Details/툴 저작에서 안정(=Instanced 없어 리스크 낮음). PostLoad 불요(필드명 유지).
- **검증 필요(빌드/PIE)**: `ComputeSignature` 확장이 진화/역진화 리빌드 정상. 각 stage AimSocket 검증이 조준감 회귀 차단.
- **자문 전용**: 빌드/테스트 미실행.
- **반증예측**: 이 플랜이 맞다면 A단계 후 빌드/스모크 통과 + 셀렉터 단위성 유지. 틀리다면 **첫 PIE 신호** = 저격 2스택 진화→ADS→프래그먼트 제거로 역진화 시 FOV/오버레이/조준선이 현재 stage와 불일치(=Signature/AimSocket 구멍).
