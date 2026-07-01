# Game.md — FPSRoguelite SSOT 허브 (구조도 + 라우팅 가이드)

> **이 문서는 SSOT(Single Source of Truth)의 허브다.** 장르 정체성·문서 체계·빠른 참조만 본문에 두고, **도메인 상세는 `Docs/SSOT/`로 분할**했다(문서 비대화 방지). 작업에 맞는 파일만 읽어라(아래 §0-1 라우팅).
> 작업 시작 전 **이 허브(Game.md) + `PROGRESS.md`(라이브 진행현황) + 해당 도메인 파일**을 읽는다. 설계 변경은 **해당 도메인 파일(또는 이 허브)을 먼저 갱신**한 뒤 코드/에셋을 수정한다.
> 엔진: **UE 5.7** (`D:\UnrealEngine\UE_5.7`) / 최종 확정일 기준: 2026-05-30 / 설계 잠금 버전: v6(Locked)
> ⚠️ **섹션 번호(§1, §2-4-1, §5-2 …)는 분할 후에도 보존**된다. 소스 주석·교차참조의 "§x"는 아래 라우팅으로 해당 파일을 찾는다.

---

## 0. 문서 체계 (어디에 무엇이 있는가)

| 문서 | 역할 | 갱신 주체 |
|---|---|---|
| **Game.md** (이 문서) | SSOT 허브 = 장르 정체성(§1) + 문서 체계(§0) + 빠른 참조(§9) + **라우팅 가이드** | 우리(세션) |
| **`Docs/SSOT/*.md`** | 도메인별 SSOT 본문(기획·기술·구조·성능·규칙·로드맵). 섹션번호 보존 | 우리(세션) |
| **PROGRESS.md** | 휘발성 진행현황·핸드오프(완료/진행중/다음순서/검증법) | 우리(세션) |
| `CLAUDE.md` / `AGENTS.md` | 진입 포인터(≤10줄). "Game.md·PROGRESS.md 읽기" + 절대금지 3줄 | 거의 불변 |
| `GameConfirm.md` | **다른 AI가 작성**하는 리뷰/추가제안 문서. 우리는 만들지 않음(§10, `Docs/SSOT/Workflow.md`) | 외부 AI |
| `Docs/ConsultLoop.md` | **컨설팅 토론 프로토콜** — 백엔드(Claude)×클라(Codex) 라이브 토론. 트리거 `/consult <주제>`, 산출 `Docs/Review/`(프롬프트 매니저 인입원, §10) | 우리(세션) |

→ **AI가 읽는 본문 = 이 허브 + PROGRESS.md + 작업 관련 `Docs/SSOT/` 파일.**

### 0-1. 🧭 라우팅 가이드 (작업 → 읽을 파일)

| 작업 종류 | 읽을 파일 | 담긴 섹션 |
|---|---|---|
| 런 루프·XP/레벨업·프리즈·미션/스케줄·보스·메타 | [`Docs/SSOT/RunFlow.md`](Docs/SSOT/RunFlow.md) | §2-1, §2-2, §2-7, §2-8, §2-11, §2-12 |
| 무기·카드·모디파이어/Fragment·사격 감각 | [`Docs/SSOT/CombatWeaponCard.md`](Docs/SSOT/CombatWeaponCard.md) | §2-3, §2-4(+2-4-1·2-4-2), §2-5 |
| 적 스웜·스폰·발사체·데미지 브릿지·네트워크 | [`Docs/SSOT/Enemy.md`](Docs/SSOT/Enemy.md) | §2-6, §2-10 |
| 카메라·생존(DBNO)·대시·게임필/피드백·HUD | [`Docs/SSOT/PlayerFeel.md`](Docs/SSOT/PlayerFeel.md) | §2-9, §2-13, §2-14 |
| 신규 클래스·모듈·폴더 구조·기술 채택 | [`Docs/SSOT/Architecture.md`](Docs/SSOT/Architecture.md) | §3, §4(+4-1·4-2) |
| 성능·복제 예산·Significance·플로우필드 | [`Docs/SSOT/Performance.md`](Docs/SSOT/Performance.md) | §5(+5-1·5-2) |
| **모든 코드 작업(필독)** — 환경·빌드·브랜치·모델정책·리뷰 | [`Docs/SSOT/Workflow.md`](Docs/SSOT/Workflow.md) | §6(+6-1~6-7), §10 |
| 진행상황·로드맵·재미 게이트·플레이스홀더 전환 | [`Docs/SSOT/Roadmap.md`](Docs/SSOT/Roadmap.md) | §7(+7-1~7-5), §8 |

> 빠른 규칙: **어떤 코드 작업이든 `Workflow.md`(§6)는 먼저** 본다. 그 외엔 위 표에서 작업에 해당하는 1~2개만 읽는다.

---

## 1. ⚠️ 장르 정체성 & 제1원리 제약 — 가장 먼저 읽을 것

이 프로젝트는 **1인칭 FPS × 뱀파이어 서바이벌 × 4인 협동 로그라이트**다.
레퍼런스: **The Spell Brigade (Steam)**.

**핵심 제약 (모든 아키텍처 결정의 기준)**: 적을 **수백 마리(~300-500) 동시에 싸게** 굴려야 한다. 이 한 가지가 표준 FPS/멀티플레이 튜토리얼의 디폴트(무거운 per-actor 시스템)에서 **의도적으로 이탈**하게 만드는 제1원리다 — **액터당 비용 최소화가 다른 모든 편의보다 우선**한다.

| 축 | 무거운 per-actor 접근 (지양) | **이 게임 (Survivor Swarm)** |
|---|---|---|
| 액터 수 | ~12명 | **적 수백 (~300-500)** |
| 액터당 비용 | 높아도 OK | **반드시 최소화** |
| AI | 개별 스마트 (StateTree/BT) | **단순 추격 스티어링 + Flow-Field, 배치** |
| 길찾기 | 에이전트별 NavMesh | **Flow-Field (고정맵 사전계산)** |
| GAS | 모든 액터 | **플레이어 + 보스/엘리트 한정** |
| 적 애니메이션 | 풀 피델리티 | **인스턴싱/VAT + LOD** |
| 리플리케이션 | 플레이어별 고정밀 | **적은 최소상태/Push Model** |

**임의로 도입 금지** (표준 디폴트 과설계 — 제1원리 위배):
- ❌ Lyra 풀 fork → 경량 커스텀 모듈 + 엔진 플러그인 체리픽
- ❌ 모든 적에 GAS/ASC → 적은 경량 `UHealthComponent` + 비-GE 데미지
- ❌ 적별 StateTree/BehaviorTree + NavMesh 길찾기 → Flow-Field + 스티어링, 배치 처리
- ❌ Iris 핵심 의존 → 디폴트는 Push Model, Iris는 P5 평가용·OFF
- ❌ MassEntity 선도입 → 목표 규모 ~500은 풀액터로 충분
- ❌ Server-Side Rewind, Motion Matching, Bhop/Wall-run, PCG, True First Person 풀바디

**GAS는 플레이어(1~4)와 보스/엘리트(소수)에만. 스웜 적에는 절대 붙이지 않는다.**

> 📌 **한 줄 대조**: 위 "지양" 열은 표준 Hero Shooter/Lyra류 튜토리얼 스택의 디폴트다 — 그쪽 기술 스택을 *그대로* 가져오지 말 것(Lyra는 플레이어 측 시스템 한정 교차검증 레퍼런스). 과거 참조하던 PvP Hero Shooter 커리큘럼은 이 프로젝트 기준 아님.

> 🔑 **아키텍처 결정 = 제1원리 우선, 레퍼런스 맹종 금지** — 비자명한 구조 결정은 ① 제1원리 근거 ② (참고) Lyra/UE표준과 같은가·다른가 ③ 이 프로젝트 제약과의 정합을 3줄로 명시. Lyra는 *플레이어 측 시스템*(무기·어빌리티·UI) 한정 교차검증 레퍼런스이며, 적/스케일은 의도적으로 이탈한다. (상세 `CLAUDE.md` 핵심 4원칙)

---

## 9. 확정 사항 / 주의점 (빠른 참조)
- 무기 교체 = 숫자키 **1/2/3**(`IA_EquipSlot1~3`) / 사격 = 좌클릭
- **UE5.7 IMC 매핑은 Python `set_editor_property` 미반영 → 에디터 수동**(IA 에셋 생성은 Python OK)
- 카드선택 = **레벨업/미션클리어 시 전역 프리즈**(적·플레이어 정지)에 전원 선택 → 재개(§2-2, `Docs/SSOT/RunFlow.md`). 오프닝 시드 2장은 런 시작 시. (라운드제·정비시간 폐지 2026-06-04)
- git: 사용자 콘텐츠(L_Sandbox 맵, DA_Weapon_Rifle/Knife @ `Content/Weapons/DataTable/`)는 디스크 존재·**미커밋**(untracked)
