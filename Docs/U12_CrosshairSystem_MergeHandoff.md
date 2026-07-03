# 재개 프롬프트 — U12 크로스헤어 시스템 + U17 설정: 최종 커밋 + main 머지 + push

> 새 세션에 **이 파일 통째로** 붙여넣고 시작. 이 세션의 목표 = **구현이 아니라 마무리**(미커밋 선별 커밋 → Codex 머지게이트 → `--no-ff` main 머지 → push → 브랜치 삭제 → 문서). 코드/콘텐츠 재작업 불필요(전부 완료·검증됨).

---

## 0. 먼저 읽기
- `Game.md`(§0-1) + `PROGRESS.md` 최신 핸드오프. 도메인 SSOT: `Docs/SSOT/PlayerFeel.md §2-14`(크로스헤어, U12 반영됨), `Docs/SSOT/Workflow.md §6·§6-7`(머지 규칙).
- 메모리: [[crosshair-parametric-system]](시스템 설계·상태), [[vibeue-material-graph-and-offline-render]](⚠️오프라인 렌더로 색 검증 불가), [[codex-review-gate]]·[[codex-gate-5min-watchdog]](머지게이트), [[ue-editor-file-locks-block-git]](⚠️머지 전 에디터 종료), [[phase-end-commit-user-content]].

## 1. 완료 상태 (브랜치 `phase/settings-system`, main 대비 16커밋, **origin 대비 13커밋 미푸시**)
U17(플레이어 설정) + U12(크로스헤어 시스템) 재정의가 전부 구현·커밋됨:
- **U17→색/두께 설정**: 크기설정(CrosshairScale) **제거**, 색(`CrosshairColor`)+두께(`CrosshairThickness` 0.5~2.0)로 전환. GUS/런HUD/설정위젯. 색 프리셋 버튼=UButton.
- **U12 진실 크로스헤어**: 런HUD가 `GetCurrentSpreadDegrees()`(기본+블룸×ADS) 실제 콘 각도를 화면 투영(FOV/뷰포트/이미지크기600/DPI)→머티 `Spread`=UV반경 → **탄이 크로스헤어 안 착탄**(거리 무관). `CrosshairSizePx` 96→600.
- **발사체 블룸**: `FPSRGA_WeaponFire_Projectile`이 `ComputeSpreadDegrees`(기본+블룸)로(서버권위).
- **CrosshairStyle DataAsset**: `UFPSRCrosshairStyleDataAsset`(Material+bDynamic+DisplayName+IsDataValid), 무기 `CrosshairStyle` 속성, FireComp 스타일우선(레거시 폴백).
- **머티리얼 4종 절차적 SDF**(M_XH_Cross/Ring/BoxDots/Dot): Custom=Float2(coverage,fillFrac), Emissive=lerp(OutlineColor,FillColor,fillFrac), Opacity=coverage. **외곽선+조절 파라미터**. + MI(MI_XH_*) + 스타일 애셋(DA_XH_*) + 전9무기 `CrosshairStyle` 배선(라이플류/스나=Cross, Shotgun=Ring, Bazooka/Grenade=BoxDots, Knife=Dot정적).
- **검증**: 빌드 Succeeded×2 + 헤드리스 Smoke 4/4(CrosshairSettings 색/두께 clamp·왕복, GameplayMessage, ModuleLoads, SaveGame) + **사용자 PIE 확인**(무기별 크로스헤어·진실 확산·색/두께·발사체 블룸). ⚠️오프라인 렌더는 UI emissive 색 미표시라 색은 PIE만(모양/확산/두께는 오프라인 확인).
- 구 warp 크로스헤어(M_DynamicCrosshair 3패치)+MI_Crosshair_*+텍스처=미사용 존치. 레거시 CrosshairMaterial 필드=폴백 유지.

## 2. 남은 작업 (이 세션 수행)

### 2-1. 미커밋 콘텐츠 **선별** 커밋 (⚠️ 에디터 열려 있으면 diff만, 커밋은 OK)
`git status --short`로 확인. 착수 시점 예상 목록(사용자가 추가 튜닝했을 수 있음):
- ✅ **커밋 대상(의도)**: `Content/UI/WBP_Settings.uasset`(사용자: 두께 슬라이더 `CrosshairThicknessSlider` + 색 버튼 `ColorPreset{White,Green,Cyan,Red,Yellow}`), 크로스헤어 튜닝(`M_XH_Cross`/`MI_XH_Cross`/`M_XH_Dot` 등 파라미터 조정), `DA_Weapon_Rifle`(재튜닝 시).
- ⚠️ **검토 후 결정**: `Content/Maps/L_MainMenu.umap`(에디터 렌더/로드 churn 가능성 높음 — 실변경 없으면 `git checkout --`로 제외), `Content/Character/Player/BP_FPSRPlayer.uasset`·`Content/UI/HUD/WBP_HitMarker.uasset`(크로스헤어 무관 — 사용자 의도 확인, 무관/스퓨리어스면 제외). 사용자 지침="사용자 수정까지 전부"지만 **스퓨리어스 에디터 dirty는 제외**가 안전.
- 커밋 메시지 예: `feat(U12): 크로스헤어 시스템 콘텐츠 마무리 — WBP_Settings 색/두께 UI + MI 튜닝 (사용자 저작)`.

### 2-2. Codex 머지게이트 ([[codex-review-gate]])
- `powershell -File Scripts\codex-review.ps1 -Base main` (5분 워치독 — 출력 없으면 스킵·진행, [[codex-gate-5min-watchdog]]). C++ diff 리뷰. P1/P2 나오면 교정 후 재검, 없으면 진행.

### 2-3. PROGRESS/TaskPrompts 갱신 (이미 이 세션서 선반영됨 — 확인·보완)
- `PROGRESS.md` 최상단 핸드오프가 U12 완료 반영하는지 확인.
- `Docs/TaskPrompts_Master.md §B`: U17 행 `🔶`→`✅`(완료·머지), U12 행(있으면) 상태 반영. 없으면 U12 완료 한 줄.

### 2-4. **에디터 종료** → main 머지 → push → 브랜치 삭제 ([[ue-editor-file-locks-block-git]], Workflow.md §6-7)
```
# UE 에디터 완전 종료 확인 (Get-Process UnrealEditor)
git checkout main
git pull                      # origin/main 최신화 (있으면)
git merge --no-ff phase/settings-system     # 충돌 시: 에디터 락/uasset — checkout -f 복구
git push origin main
git branch -d phase/settings-system
git push origin --delete phase/settings-system   # origin 원격 브랜치 삭제(있으면)
```
- ⚠️ .uasset 충돌/`unable to unlink` = 에디터 락. 종료 후 재시도, 복구=`git checkout -f`.

## 3. 조절/구조 참고
- **유저 설정**(WBP_Settings→GUS): 색(색 버튼)+두께 배수(슬라이더). `UFPSRGameUserSettings::CrosshairColor/CrosshairThickness`, 런HUD가 매프레임 머티 `FillColor`/`Thickness`/`Spread` 세팅.
- **디자이너 기본값**(각 MI_XH_*): `ArmLength`(Cross 팔길이)·`ArmThickness`/`RingThickness`/`BoxThickness`/`DotRadius`(기본두께)·`OutlineWidth`(외곽선두께)·`OutlineColor`(외곽선색 기본검정)·`GapCos`(링 갭각)·`CornerFrac`(박스코너)·`DotOffset`(박스점).
- 무기별 크로스헤어 바꾸기 = 무기 DataAsset `CrosshairStyle`에 다른 `DA_XH_*` 지정(예: 라이플에 샷건 = DA_XH_Ring).
- 핵심 C++: `FPSRRunHUDWidget.cpp`(ComputeSpreadUV 진실투영·ApplyCrosshairAppearance), `FPSRGameUserSettings`, `FPSRCrosshairStyleDataAsset`, `FPSRGA_WeaponFire_Projectile.cpp`(블룸).
- 엔진 UE 5.7 `D:\UnrealEngine\UE_5.7`. 빌드(에디터 종료): `Build.bat FPSRogueliteEditor Win64 Development -Project=E:/Git_Project/FPSRoguelite/FPSRoguelite.uproject -WaitMutex`. 스모크: `UnrealEditor-Cmd.exe <proj> -ExecCmds="Automation RunTests FPSRoguelite.Smoke; Quit" -unattended -nullrhi -nosound -stdout`.

## 4. 완료 후
- PROGRESS.md 핸드오프에 "U12+U17 main 머지 완료" 기록. 다음=2차 잔여(U15 1P무기애님/U19 3P팀원애님/U20 적애님) — `Docs/TaskPrompts_Master §C`.
