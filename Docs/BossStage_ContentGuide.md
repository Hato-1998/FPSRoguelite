# 보스 스테이지 — 콘텐츠 저작 가이드 (사용자 작업)

> 코드 쪽(스테이지 라우팅·아레나 역할·사전 파킹·검증기)은 `phase/boss-stage-transition` 에서 끝났다.
> 이 문서는 **에디터에서 사람이 해야 하는 것**만 순서대로 담는다. 순서가 중요하다 — 아래 번호대로 하면
> 중간에 되돌아갈 일이 없다.
>
> 관련: [ADR 0010 D6](Architecture/0010-arena-topology-and-stage-transition.md) · [ADR 0012](Architecture/0012-authored-arena-sublevel-and-collision-bake.md) · `Docs/SSOT/RunFlow.md` §2-7

---

## 0. 이 작업이 만드는 것

```
전투 아레나(L_Map_1 / L_Map_2) ──억제기 파괴──► 다음 전투 아레나 ──억제기 파괴──► …
                                    │
                        런 시계가 BossTime 에 도달
                                    ▼
                        보스 아레나(L_Map_Boss)  ← 억제기 없음 = 나가는 길 없음
                                    │
                              보스 처치 → 결과 화면 → 로비
```

**억제기 순환은 보스 아레나를 건너뛴다.** 아레나 액터의 새 필드 `아레나 역할` 이 그 갈림길이다.
보스 아레나로 가는 유일한 길은 런 디렉터가 `BossTime` 에 거는 전환 하나뿐이고, 그 전환은
**보스가 생기기 전에** 끝난다(보스는 스왑이 옮겨주지 않으므로 순서가 뒤집히면 옛 아레나에 홀로 남는다).

---

## 1. 🔴 먼저 되돌릴 것 — 덮인 베이크

`Content/Maps/Data/DA_Map_2_ArenaBake.uasset` 이 **L_Map_Boss 로 구워져 있다**(에셋 안의 `소스 레벨` =
`/Game/Maps/L_Map_Boss`). L_Map_Boss 를 L_Map_2 의 복제로 만들면서 `베이크 데이터` 참조까지 복제됐고,
새 레벨에서 굽는 순간 **L_Map_2 의 장애물 마스크가 덮였다** — 그 맵에서 적이 벽을 통과한다.

되돌리는 법(에디터 닫고):

```bash
git checkout f105933a -- Content/Maps/Data/DA_Map_2_ArenaBake.uasset
```

> ⚠️ `git checkout --`(커밋 지정 없이)로는 안 된다. PC 이동을 위해 이 깨진 상태가 `a6b1edc3` 에
> **이미 커밋됐기 때문**이다 — 커밋 지정 없이 부르면 깨진 판이 그대로 돌아온다. `f105933a` 는
> 이 사고 직전의 마지막 정상 커밋이다.

`f105933a` 의 판이 L_Map_2 의 올바른 베이크이고 `L_Map_2.umap` 은 그 뒤로 바뀌지 않았으므로, **재베이크는
필요 없다.** 복구 후 L_Map_2 를 열어 데이터 검증(아래 §9)이 초록인지만 확인하면 된다.

> 이 사고는 이제 검증기가 잡는다 — 베이크 에셋의 `소스 레벨` 이 그 레벨과 다르면 **에러**로 뜬다.

## 2. L_Map_3 리다이렉터 껍질 삭제

`Content/Maps/L_Map_3.umap` 은 실제 맵이 아니라 **1.2KB 짜리 이름변경 흔적**(`ObjectRedirector` →
`L_Map_Boss`)이다. PC 이동 스냅샷(`a6b1edc3`)에 함께 담겼으므로 이제는 추적되는 파일이다 — `git rm` 으로 지운다.

```bash
git rm Content/Maps/L_Map_3.umap
```

## 3. 🔴 L_Map_Boss 의 위치를 비어 있는 자리로 옮긴다

**복제된 레벨이라 L_Map_2 와 같은 월드 좌표에 서 있을 가능성이 매우 높다.** 아레나는 같은 Z 평면의
서로 겹치지 않는 XY 자리에 놓여야 한다(ADR 0012) — 셀 격자가 겹치면 "이 좌표는 어느 아레나 것인가"
(`ContainsWorldLocation`)가 두 아레나 모두에 참이 되고, 스폰 게이트·마커 소속·베이크 해시가 전부 흔들린다.

1. `L_Map_2` 와 `L_Map_Boss` 의 `AFPSRArenaActor` 위치를 나란히 확인한다.
2. 같거나 격자가 겹치면 **L_Map_Boss 레벨의 액터 전체를 통째로** 빈 자리로 옮긴다
   (Levels 패널에서 L_Map_Boss 만 선택 가능하게 만들고 전체 선택 → 이동, 또는 레벨 트랜스폼 사용).
   아레나가 160 × 160 m 이므로 한 변당 최소 200 m 는 떨어뜨린다.
3. 옮긴 뒤 §5 에서 어차피 다시 굽는다.

## 4. 아레나 액터 설정 (L_Map_Boss)

`L_Map_Boss` 의 `AFPSRArenaActor` 디테일 패널 → `아레나` 카테고리:

| 항목 | 값 | 왜 |
|---|---|---|
| **아레나 역할** | **`보스`** | 이것 하나가 억제기 순환에서 제외시키고 BossTime 전환의 목적지로 만든다 |
| **시작 시 활성** | **끈다(false)** | 켜져 있으면 런이 보스 스테이지에서 시작한다. 검증기가 에러로 잡는다 |
| **스테이지 순서** | **다른 아레나와 겹치지 않는 값**(예: `100`) | 복제본이라 지금은 L_Map_2 와 같은 값일 것이다. 겹치면 보스 아레나 조회가 엉뚱한 레벨을 집는다 |
| **베이크 데이터** | **§5 에서 새로 만들 전용 에셋** | 지금은 `DA_Map_2_ArenaBake` 를 가리키고 있다(§1 의 원인) |
| 아레나 파라미터 | 복제된 그대로 두면 된다 | |

**억제기(`BP_Inhibitor`)는 이 레벨에 하나도 없어야 한다.** 현재 없는 것으로 확인됐다 — 그대로 두면 된다.
(있으면 검증기가 에러로 잡는다. 보스가 뜨기 *전* 전환 구간에 부수면 파티가 보스 아레나 밖으로 나가 버린다.)

## 5. 전용 베이크 에셋 + 중앙 발판 콜리전

**순서: 발판을 먼저 놓고, 그 다음에 굽는다.** 굽기가 레벨 콜리전을 읽기 때문이다.

### 5-1. 중앙 발판 콜리전

보스 캡슐은 오브젝트 타입이 `Pawn` 이라 **플레이어는 막지만 스웜 적은 통과한다.** 그리고 플로우필드는
액터가 아니라 **구워진 마스크**만 보므로, 보스를 스폰해도 적 길찾기는 보스의 존재를 모른다. 25 m 짜리
구조물을 적이 뚫고 걸어 나오는 그림이 된다.

→ 아레나 정중앙에 **지름 25 m · 높이 약 2 m** 의 `WorldStatic` 콜리전을 놓는다(메시는 안 보이게 해도 된다).

- 🔴 **반드시 `L_Map_Boss` 서브레벨에 놓는다 — 지속 레벨(`L_Arena`)이 아니다.** 베이크는 아레나 액터의
  **자기 레벨만** 훑는다(`FPSRArenaBakeHash.cpp:151` — `Actor->GetLevel() != ArenaLevel` 이면 건너뛴다).
  지속 레벨에 놓으면 **발판이 마스크에 안 들어간다.** 그런데 물리적으로는 존재하므로 **플레이어와 총알은
  정상적으로 막혀** PIE 에서 잘 되는 것처럼 보이고, **스웜 적만 조용히 통과한다** — 발판을 놓은 목적이
  정확히 그건데 그것만 실패한다. (실사고 2026-08-29)
  - 이미 잘못 놓았으면: 액터 선택 → Levels 패널에서 `L_Map_Boss` 를 더블클릭해 Current Level 로 만든 뒤
    `Actor > Level > Move Selected Actors to Level` → **그 다음 반드시 다시 굽는다.**
  - 확인법: Levels 패널에서 `L_Map_Boss` 만 숨겼을 때 발판도 같이 사라지면 소속이 맞다.
- **높이 2 m 인 이유**: 60 cm 이상이어야 플로우필드가 벽으로 인식하고(ADR 0010 불변식 4), 낮아야
  **총알이 그 위를 지나 보스 몸통에 맞는다**. 보스 전체 높이(50 m)로 막으면 벽 트레이스가 총알을
  먼저 멈춰 **보스를 아예 못 잡는다.**
- 45 ~ 60 cm 는 저작 금지 구간이다(적이 끼는 밴드).
- 지름은 보스 원기둥과 같게 — 25 m = 2500 cm.
- 콜리전 요건(`FPSRArenaBakeHash.cpp:59-73` `ContributesToBake`): 오브젝트 타입 **`WorldStatic`** +
  **`QueryOnly` 또는 `QueryAndPhysics`**. `PhysicsOnly`·`NoCollision` 은 프로브가 통과해 베이크에 안 들어간다.

### 5-2. 베이크

1. 콘텐츠 브라우저에서 `Content/Maps/Data/` 에 **`DA_Map_Boss_ArenaBake`**(`UFPSRArenaBakeDataAsset`)를 새로 만든다.
2. L_Map_Boss 의 아레나 액터 `베이크 데이터` 에 건다.
3. `Tools > FPSR > 아레나 베이크` 실행.
4. 에셋의 `소스 레벨` 이 `/Game/Maps/L_Map_Boss` 인지, `소스 액터 수` 가 1이나 0이 아닌지 눈으로 확인한다.

## 6. 보스 스폰포인트

아레나 정중앙에 `AFPSRBossSpawnPoint` 를 놓는다.

- **Z** = 아레나 바닥 + **보스 캡슐 반높이**(§7 기준 2500 cm). 캡슐 중심이 스폰 좌표이므로,
  바닥에 맞추면 보스가 절반 묻힌다(스캐폴드 보스는 중력이 꺼져 있어 저절로 내려앉지 않는다).
- 안 놓아도 동작은 한다 — 런 디렉터가 **아레나 중심 + 캡슐 반높이**로 폴백한다(검증기는 경고만 띄운다).
  위치를 정확히 잡고 싶을 때만 놓으면 된다.

## 7. BP_Boss — 원기둥 몸통

`Content/Character/Boss/BP_Boss` 를 연다. (C++ 기본값을 BP CDO 에서 덮는 것이라 코드 수정은 필요 없다.)

| 컴포넌트 | 항목 | 값 |
|---|---|---|
| `CapsuleComponent`(루트) | Capsule Radius | **1250** |
| | Capsule Half Height | **2500** |
| `BodyMesh` | Static Mesh | `/Engine/BasicShapes/Cylinder` |
| | Relative Scale | **(25, 25, 50)** |
| | Relative Location | 원기둥 피벗에 따라 조정 — 아래 참고 |

- 엔진 기본 원기둥은 한 변 100 cm 짜리라 스케일 (25, 25, 50) = **지름 2500 cm × 높이 5000 cm** = 25 m × 50 m.
- **Relative Location Z**: 원기둥 피벗이 **중앙**이면 `0`, **바닥**이면 `-2500`. 뷰포트에서 캡슐과
  몸통 위아래가 맞는지 눈으로 맞추는 게 제일 빠르다.
- `BodyMesh` 는 콜리전이 없다(캡슐이 피격 볼륨). 그대로 둔다.
- 체력은 `DA_BossDefinition` 이 주도한다 — 25 m 짜리 표적은 맞히기 쉬우므로 체력을 다시 볼 것.

## 8. 지속 레벨 등록 + 스케줄 값

### 8-1. `L_Arena` Levels 패널에 `L_Map_Boss` 추가

**`L_Map_2` 의 스트리밍 설정을 그대로 복사한다** — 특히 "패키지는 로드하되 보이지는 않게"여야 한다.
런 디렉터가 로스터에서 보스 아레나를 찾으려면 **패키지가 로드돼 있어야** 하고(가시성은 필요 없다),
가시화는 `BossTime − 리드타임` 에 코드가 알아서 요청한다.

> 등록을 빠뜨리면 로스터에 안 잡히고, 클라이언트에도 복제되지 않는다. 그 경우 코드는 30초를 기다린 뒤
> **현재 아레나에 보스를 스폰하고** 로그에 에러를 남긴다 — 조용히 실패하지는 않는다.

### 8-2. `DA_RunSchedule`

| 항목 | 값 |
|---|---|
| **보스 아레나 사전 파킹 리드(초)** | 기본 `45`. 압축 테스트 스케줄(BossTime 300s 등)에서도 그대로 두면 된다 — 리드가 BossTime 보다 크면 자동으로 "런 시작 시 파킹"으로 내려간다 |
| 그레이스 창 | `StageGraceSeconds` 를 **그대로 공유한다**(사용자 결정: 일반 전환과 동일). 따로 저작할 값 없음 |

## 9. 검증

1. **데이터 검증** — `L_Map_2`, `L_Map_Boss` 각각에서 에디터 데이터 검증을 돌린다. 새로 잡히는 것들:
   - 베이크 에셋의 `소스 레벨` 불일치 → 에러
   - 보스 아레나에 억제기 → 에러
   - 보스 아레나에 `시작 시 활성` → 에러
   - 보스 아레나에 보스 스폰포인트 없음 → 경고(정상 폴백)
2. **PIE 스모크** — `FPSR.RunTimeScale` 로 빨리 감거나 짧은 BossTime 스케줄로:
   - 억제기를 여러 번 부숴도 **보스 아레나로 안 간다**(전투 아레나만 순환)
   - BossTime 에 도달하면 페이드 → 암전 → **보스 아레나** → 페이드인 → 그 다음에 보스 등장
   - 보스 아레나엔 억제기가 없다 = 나갈 수 없다
   - 보스 처치 → 결과 화면 → 로비
3. **로그로 확인할 줄**
   - `[Run] Boss arena (stage order N) pre-park requested at t=…`
   - `[Run] BossTime reached (t=…) — transitioning to the boss arena (stage order N) before spawning.`
   - `[StageDirector] Swap complete: … -> …L_Map_Boss… (stage N, seed …)`
   - `[StageDirector] Landed in the boss arena — no successor to park (the run ends here).`
   - `[Run] Boss stage reached — spawning the boss here.`

---

## 알려진 한계 (이번 패스에서 감수한 것)

- **공중 적은 발판(2 m) 위를 날아 보스 몸통에 겹쳐 보일 수 있다.** 화이트박스 단계에서 감수한다.
  없애려면 보스가 스폰될 때 자기 발자국을 플로우필드에 런타임 스탬프하는 방식이 필요한데, 그건
  플로우필드의 generation/recompute 계약을 건드리는 별도 작업이다.
- **보스 스왑도 `StageIndex` 를 +1 한다** → `StageDifficulty` 앵커의 마릿수 가산·배수가 보스 스테이지에도
  적용된다. 의도된 동작이지만(보스전 = 가장 빡센 스테이지), 밸런싱 때 이 사실을 기억할 것.
- **결산 내용은 이번 범위 밖**이다(사용자 결정 4). 기존 결과 위젯 → 로비 복귀로 루프만 닫는다.
  재화·해금은 메타 프로그레션 실물화(M3) 트랙.

---

## 부록 — 다른 PC에서 이어받기

```bash
git clone https://github.com/Hato-1998/FPSRoguelite.git
cd FPSRoguelite
git lfs install
git checkout phase/boss-stage-transition
git lfs pull
```

이미 클론이 있으면:

```bash
git fetch origin
git checkout phase/boss-stage-transition
git lfs pull
```

그 다음 **에디터를 열기 전에** 위 §1(베이크 원복)·§2(리다이렉터 삭제)를 먼저 처리한다.
C++ 는 새 UPROPERTY(`아레나 역할`)와 새 UENUM 이 들어갔으므로 **에디터 첫 실행 전에 빌드해야 한다**
(라이브 코딩 금지 — `Docs/SSOT/Workflow.md` §6-6):

```bash
"<엔진루트>/Engine/Build/BatchFiles/Build.bat" FPSRogueliteEditor Win64 Development -Project="<클론>/FPSRoguelite.uproject" -WaitMutex
```
