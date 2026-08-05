"""미사용 에셋을 지운다 — **기본은 드라이런**. 실제 삭제는 `-- apply` 를 줘야 한다.

남길 것의 판정은 두 갈래이고, 둘 다 **의존성 폐포**까지 간다:
  ① 도달 = 우리가 만든 폴더(맵·BP·DA…)에서 하드+소프트 참조를 따라간 것
  ② 명시 보관 = "이 폴더는 지금 참조가 없어도 남긴다" 고 정한 것 (무기 파츠·1인칭 팔)

🚨 폴더만 남기면 안 된다. 무기 메시는 머티리얼·텍스처를 **다른 폴더에서** 물고 있어서,
   폴더 단위로만 자르면 무기가 회색 덩어리가 된다. 그래서 명시 보관도 폐포로 확장한다.

🪤 지우는 범위(DELETE_SCOPES)에 없는 팩은 **손대지 않는다.** "미도달"이라고 자동으로
   지우지 않는다 — 도시 툴처럼 폴더째 스캔하는 사용처는 레지스트리에 안 잡히기 때문이다.

🪤 파이썬 커맨드렛은 `sys.argv` 로 인자를 못 받는다(`-run=pythonscript -script=` 는 스크립트
   경로만 읽는다). 그래서 스위치는 **엔진 커맨드라인에서 직접** 읽는다.

실행:
  UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script="<이 파일>"           (드라이런)
  UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script="<이 파일>" -FPSRApply (실제 삭제)
"""
import json
import os

import unreal

APPLY = "-fpsrapply" in unreal.SystemLibrary.get_command_line().lower()
OUT = "E:/Git_Project/FPSRoguelite/Saved/NeonV/cleanup_plan.json"

# 우리가 만든 것 = 루트. 여기서 참조가 뻗어나간다.
OURS = ["/Game/Maps", "/Game/Cards", "/Game/Weapons", "/Game/Mission", "/Game/Input",
        "/Game/UI", "/Game/Character", "/Game/Actors", "/Game/Game", "/Game/Config",
        "/Game/Tools", "/Game/SpawnPoints", "/Game/Materials", "/Game/Audio",
        "/Game/Blockout", "/Game/LevelPrototyping", "/Game/Developers", "/Game/Python"]

# 참조가 없어도 남긴다 (+ 이들이 물고 있는 것 전부)
KEEP_ROOTS = [
    # 1인칭 팔 — 사용자 결정 2026-08-05: 4종 다 남긴다
    "/Game/LowPolyAnimatedModernGuns/Art/Characters/Meshes",
    # 무기 소스 — 무기 DA 8개가 아직 미배정이라 파츠를 통째로 남긴다 (사용자 결정)
    "/Game/PolygonMilitary/Meshes/Weapons",
]

# 지울 범위. **여기 없는 팩은 안 건드린다.**
DELETE_SCOPES = [
    "/Game/Assets/Environment/ModularSciFiStation",
    "/Game/Assets/Characters/Anime_Girl_Character_-_Blu-6ccdbbe7",
    "/Game/LowPolyAnimatedModernGuns",
    "/Game/Rifle_01",
    "/Game/_SyntyPilot",
    "/Game/PolygonCyberCity",
    "/Game/PolygonScifi",
    "/Game/PolygonMilitary",
]


def log(m):
    unreal.log("[CLEAN] %s" % m)


ar = unreal.AssetRegistryHelpers.get_asset_registry()
ar.scan_paths_synchronous(["/Game"], force_rescan=False)
while ar.is_loading_assets():
    pass

all_pkgs = sorted({str(a.package_name) for a in ar.get_all_assets()})
log("전체 %d 패키지" % len(all_pkgs))

opts = unreal.AssetRegistryDependencyOptions(
    include_soft_package_references=True, include_hard_package_references=True,
    include_searchable_names=False, include_soft_management_references=False,
    include_hard_management_references=False)


def under(pkg, roots):
    return any(pkg == r or pkg.startswith(r + "/") for r in roots)


def closure(seeds):
    seen, stack = set(seeds), list(seeds)
    while stack:
        cur = stack.pop()
        for d in (ar.get_dependencies(cur, opts) or []):
            ds = str(d)
            if ds.startswith("/Game") and ds not in seen:
                seen.add(ds)
                stack.append(ds)
    return seen


reached = closure([p for p in all_pkgs if under(p, OURS)])
log("도달(우리 루트 기준) %d" % len(reached))

keep_seeds = [p for p in all_pkgs if under(p, KEEP_ROOTS)]
keep_extra = closure(keep_seeds)
log("명시 보관 %d -> 폐포 %d" % (len(keep_seeds), len(keep_extra)))

KEEP = reached | keep_extra
targets = [p for p in all_pkgs if under(p, DELETE_SCOPES)]
to_delete = sorted(p for p in targets if p not in KEEP)
kept_in_scope = sorted(p for p in targets if p in KEEP)
log("삭제 범위 안 %d — 남김 %d / 삭제 %d"
    % (len(targets), len(kept_in_scope), len(to_delete)))

by_scope = {}
for s in DELETE_SCOPES:
    d = [p for p in to_delete if under(p, [s])]
    k = [p for p in kept_in_scope if under(p, [s])]
    if not d and not k:
        continue
    by_scope[s] = {"delete": len(d), "keep": len(k)}
    log("  %-58s 삭제 %5d · 남김 %4d" % (s, len(d), len(k)))
    if k and len(k) <= 50:
        for p in k:
            log("        남김: %s" % p)

plan = {"apply": APPLY, "delete_count": len(to_delete), "by_scope": by_scope,
        "kept_in_scope": kept_in_scope, "delete_list": to_delete}
os.makedirs(os.path.dirname(OUT), exist_ok=True)
with open(OUT, "w", encoding="utf-8") as f:
    json.dump(plan, f, ensure_ascii=False, indent=1)
log("계획 -> %s" % OUT)

if not APPLY:
    log("드라이런이다 — 아무것도 안 지웠다. 실제 삭제는 `-- apply`")
    log("CLEAN_DRYRUN_DONE")
    raise SystemExit(0)

# --- 실제 삭제 ---
ok, fail = 0, []
for i, pkg in enumerate(to_delete):
    try:
        if unreal.EditorAssetLibrary.delete_asset(pkg):
            ok += 1
        else:
            fail.append(pkg)
    except Exception as e:  # noqa: BLE001
        fail.append("%s (%s)" % (pkg, e))
    if (i + 1) % 500 == 0:
        log("  %d/%d 삭제..." % (i + 1, len(to_delete)))
log("삭제 완료 %d / 실패 %d" % (ok, len(fail)))
for f_ in fail[:20]:
    unreal.log_warning("[CLEAN] 실패: %s" % f_)
plan["deleted"] = ok
plan["failed"] = fail
with open(OUT, "w", encoding="utf-8") as f:
    json.dump(plan, f, ensure_ascii=False, indent=1)
log("CLEAN_APPLY_DONE")
