# 인계 — 반동(CrystalRecoil 어댑터) P2·P3·P4 이어서

> 새 세션에 **아래 블록을 그대로 복붙**하면 재개됩니다. 상세 현황은 `PROGRESS.md` 상단 `2026-07-08 l`.

```
[반동 CrystalRecoil 어댑터 P2부터 이어서]  브랜치: phase/recoil-crystalrecoil (origin에 push됨)

■ 먼저 읽기(필독):
- PROGRESS.md 상단 핸드오프 "2026-07-08 l"(반동 P0·P1 완료 + P2/P3/P4 남음)
- Game.md §0-1 라우팅 + Docs/SSOT/CombatWeaponCard.md §2-4-2(사격 감각·캐주얼화 레버) + Docs/SSOT/Performance.md §5(발사체/확산 예산)
- Docs/SSOT/Workflow.md §6(빌드·브랜치·모델정책·리뷰)
- 플러그인 헤더: Plugins/CrystalRecoil/Source/CrystalRecoil/Public/Components/CRRecoilSpreadComponent.h(GetCurrentSpreadAngle·OnHeatChanged·heat 곡선), CRRecoilComponent.h, Data/CRRecoilPattern.h(ConsumeShot)
- 우리 코드: Source/FPSRoguelite/Public|Private/Weapon/FPSRRecoilComponent.*(P1 어댑터), FPSRWeaponFireComponent.*(확산=ComputeSpreadDegrees/GetCurrentSpreadDegrees/GetCurrentBloom/CurrentBloom), FPSRGA_WeaponFire_Hitscan.cpp·FPSRGA_WeaponFire_Projectile.cpp(ComputeSpreadDegrees+GetCurrentBloom 소비), UI/FPSRRunHUDWidget.cpp(~L98 크로스헤어 spread 파라미터)

■ 확정된 설계(변경 금지): 어댑터 방식(플러그인 커널 재사용, 우리가 통합 경계 소유) / 반동+확산 전면 채택 / 절차적 반동 완전 대체하되 ChargeLaser 차징 램프는 커스텀 유지 / 반동은 컨트롤러 적용(서버 조준 정합). P1까지 완료·빌드 통과.

■ 남은 작업:
- P2 확산 단일소스: 발사 GA(Hitscan/Projectile)+HUD가 UFPSRRecoilComponent(=UCRRecoilSpreadComponent)의 GetCurrentSpreadAngle() "하나만" 읽도록 재배선. 우리 CurrentBloom/ComputeSpreadDegrees/GetCurrentSpreadDegrees 제거(또는 GetCurrentSpreadAngle로 위임). OnHeatChanged 델리게이트→크로스헤어 spread. ⚠️서버 확산 패리티: 발사 GA는 서버가 스폰(확산 읽음)인데 heat는 클라 로컬 → 원격클라 서버-read 0 문제. 해법 설계 필요(서버측 accepted-shot에서 heat 갱신 or 서버 스프레드 소스). 이 부분이 P2의 핵심 난제 — 플랜에 명시.
- P3 콘텐츠(반동 복원 지점): 무기별 CRRecoilPattern 저작(Rifle/SMG/LMG/Shotgun/Sniper/Bazooka; ChargeLaser·Knife 없음) + heat 곡선(ShotToHeatCurve/HeatToSpreadAngleCurve/HeatToCooldownPerSecondCurve) 설정 → 무기 DA.RecoilPattern 배선. ⚠️패턴은 UCRRecoilUnitGraph 기반 시각 에디터 저작이라 헤드리스 커맨드릿으로 좌표 저작이 어려울 수 있음 → 인터랙티브 에디터(패턴 에디터) 필요할 가능성. editor-bridge 스킬로 시도→핸드오프.
- P4 정리·검증: 죽은 반동 스탯 필드 정리(카드 레버용 RecoilVertical/SpreadDegrees는 유지). 빌드 -NoXGE + validate-data + PIE 스모크(반동 패턴·확산·크로스헤어·ADS·카드 레버·프리즈/DBNO 게이팅) + Codex 머지게이트(Scripts/codex-review.ps1) → --no-ff 머지(P1~P4 묶어서, P1 단독 머지 금지).

■ 프로세스(메모리): 초안 플랜→Codex 토론(Scripts/consult-codex.ps1, 사용량 없으면 패스, 5분 워치독)→사용자 승인 후 구현. 구현=Sonnet 위임/검증=Opus 직접. 빌드 판정은 로그 "Result:"(-NoXGE, [[build-octobuild-xge-c1076-flaky]]). 착수 전 다른 클론(FPSRoguelite2) 에디터 종료 확인.

■ 현재 상태 주의: P1은 코드만 — 무기 패턴 미저작이라 일반무기 반동 없음(P3서 복원). 미커밋 사용자 WIP 콘텐츠 보존.
```
