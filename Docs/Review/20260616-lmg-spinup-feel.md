# 컨설트: U16 스핀업 LMG 손맛/체감 설계 검증 (2026-06-16)

> ConsultLoop 시스템 **최초 라이브 토론**(스모크 겸). 백엔드(Claude)×클라이언트(Codex gpt-5.5) 1라운드 수렴.
> 원시 응답: `_raw/20260616-160112-lmg-spinup-feel.md`.

## 범위 / 읽은 컨텍스트
- `PROGRESS.md` U16 섹션 + `Source/.../Weapon/FPSRWeaponFireComponent.cpp`(`ComputeSpinupFireRate`, `SpinupElapsed` 리셋 경로), `FPSRWeaponTypes.h`(`bHasSpinup`/`SpinupFireRateStart`/`SpinupRampTime`).
- 구현 사실: 스핀업 = **클라 로컬 케이던스**(서버는 max-rate anti-abuse 천장만), BaseStats 기반이라 **FireRate 카드 면역**, 비발사 시 `SpinupElapsed` 즉시 0 리셋, 반동/블룸은 FireRate 가속 자동 추종, 스핀업 전용 연출 없음.

## 🔧 백엔드 렌즈 핵심 입장 (Claude)
- 복제 관점: 클라 로컬 케이던스가 옳다(4인 PvE → 스핀업=자기 핸디캡, 서버 추적 실익 無, desync 차단). 시스템 안정적.
- 우려: **체감 공백** — 가속 신호가 케이던스 변화 하나뿐이라 "그냥 처음에 버벅이는 총"으로 오인 위험.
- 우려: **즉시 리셋이 징벌적**으로 느껴질 위험(탄 소진 잦은 LMG).

## 🎮 클라이언트 렌즈 핵심 입장 (Codex)
- 피드백 우선순위: **사운드 피치 램프 > 크로스헤어/블룸 미세표시 > 총신 스핀 VAT > 화면 흔들림**. 군중 슈팅은 화면이 이미 포화 → 시각만으론 스핀업 못 읽음. LMG는 "소리가 빨라/높아짐"이 곧 가속 체감.
- 즉시 리셋은 **유지**하되 "손해 봤다"가 아니라 "기계가 다시 물리는 중"으로 들리게: 리셋 순간 spin-down/brake 사운드(탄소진=dry click+모터꺼짐 / 재장전=모터 죽음 / 교체=즉시컷 구분).
- 반동/블룸 자동 추종은 **1차 구현으로 맞음**. 단 최고속 per-shot 반동 과해지지 않게 DA 값 낮게. 분리 곡선은 지금 필수 아님(필요시 후속).
- 화면 흔들림 점증은 최하위(피로·조준 방해).

## 토론 로그 요약
- R1 백엔드: 시스템 OK + 체감공백/리셋징벌 2대 우려 + 4문항.
- R1 클라(Codex): 4문항 전부 응답(위). **핵심 합의점** = "코드 구조 변경보다 `SpinupAlpha` 기반 사운드/크로스헤어 피드백이 첫 보강."
- 백엔드 수렴 판정: Codex 권고가 전부 **서버 모델·복제 무변경 선**에 머묾(스핀 보존 같은 서버룰 도입 명시 반대) → 백엔드 제약과 충돌 없음. `SpinupAlpha` 노출 = read-only BlueprintPure getter(클라 로컬, 복제 0비용) → 동의. **1라운드 수렴.**

## ✅ 합의 권고
1. **첫 보강 = 사운드 피치 램프**(`SpinupAlpha`로 발사 루프 pitch/volume 상승, 최고속서 안정). 비용 최소·체감 최대.
2. **`SpinupAlpha` getter 노출**(BlueprintPure, `SpinupElapsed/SpinupRampTime` clamp01). 디자이너가 사운드·크로스헤어·총신·머즐을 콘텐츠에서 붙이는 단일 훅.
3. **크로스헤어 미세 피드백**(2순위, V3 크로스헤어 재사용 — Alpha로 gap/두께 미세 변동, 실제 Bloom과 일치시켜 오해 방지).
4. **즉시 리셋 유지 + spin-down/brake 사운드**로 납득(서버룰 변경 X). 상황별(탄소진/재장전/교체) 사운드 구분.
5. **반동/블룸 자동 추종 유지**(1차), DA `RecoilVertical`/`BloomPerShot`/`MaxBloom` 보수적으로.
6. **총신 스핀 VAT = U15 이후**(정체성 강화용, 핵심 체감 수단 아님).

## ⚖️ 미해결 쟁점
- 없음(1라운드 수렴). 분리 반동/블룸 곡선(`SpinupRecoilScaleCurve` 등)은 "초반 묵직·최고속 안정" 분리가 필요해질 때만 — 현재는 **보류**가 양측 합의.

## 🙋 사용자 결정 필요 (튜닝/콘텐츠 — 취향·수치)
1. **스핀업 곡선 시작점/시간**: Codex 권장 `SpinupFireRateStart` = 최대 FireRate의 **50~65%**, `SpinupRampTime` **0.8~1.2초**(1.5s+ 는 답답). DA에서 확정 필요.
2. **탄창 보상 보장**: `SpinupRampTime` ≤ "한 탄창 비우는 시간"의 **20~25%**, 최고속 기준 **5~7초+** 지속 사격 가능하게 매거진 사이즈 조율.
3. 추가 노출 파라미터 범위(아래 액션 1의 어디까지 갈지).

## 📌 액션 아이템
- **(코드, 소규모)** `UFPSRWeaponFireComponent`에 `GetSpinupAlpha()` BlueprintPure 노출 → 사운드/크로스헤어/애님 콘텐츠 훅. 별도 유닛 또는 U15 합류. *반영 SSOT:* `Docs/SSOT/CombatWeaponCard.md`(§2-4-2 손맛) + `PROGRESS.md` U16 후속.
- **(코드, 후속/선택)** DA 노출 곡선 단계적 추가: `SpinupAudioPitch(Min/Max\|Curve)` → `SpinupAudioVolumeCurve` → `SpinupCrosshair*` → `SpinupBarrelSpin*` → (선택) `SpinupRecoil/BloomScaleCurve`. 부담 시 **`SpinupAlpha` 노출만으로 1차 충족**.
- **(콘텐츠)** spin-down/brake 사운드 3종(탄소진/재장전/교체) + 발사 루프 피치 램프 사운드. → U13 오디오 폴리시 연계 가능.
- **(콘텐츠)** DA 튜닝값 확정(위 사용자 결정 1·2).
- **⚠️ (백엔드, P5 연계 — 별도 발견)** `AFPSRCharacter::HandleOutOfHealth`가 현재 로그만 남기고 `StopFiring()` 미호출. DBNO/사망 구현(P5) 시 `WeaponFire->StopFiring()`(또는 `ResetFireState()`)를 연결해야 **스핀업(및 발사상태) 뱅킹**이 사망 너머로 안 샌다. 당장 버그는 아님(DBNO 미구현). → P5 착수 시 체크리스트.

---
*가드레일: 자문 전용 — 위 액션은 코드/에셋을 아직 바꾸지 않음. 채택 시 해당 SSOT 먼저 갱신 후 구현(ConsultLoop §6).*
