"""프로젝트에서 **실제로 도달하지 않는** 에셋을 찾는다 — 읽기 전용. 삭제는 안 한다.

왜 문자열 검색이 아니라 이걸 쓰나: `.uasset` 안의 경로 문자열을 grep 하면 ①리다이렉터·소프트
참조를 놓치고 ②이름이 비슷한 다른 에셋에 걸린다. 지우는 판단의 근거로는 약하다.
에셋 레지스트리의 의존성 그래프는 엔진이 쿠킹에 쓰는 바로 그 자료다.

판정:
  루트 = 맵 전부 + **우리가 만든 폴더 전부**(팩이 아닌 것) + 에셋매니저가 스캔하는 경로
  도달 = 루트에서 하드+소프트 의존성을 따라간 폐포
  후보 = 팩 폴더에 있으면서 도달하지 않은 것

🪤 에디터 전용 참조도 포함한다(덜 지우는 쪽으로 틀리게). 리다이렉터는 도달로 친다.

실행: UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script="<이 파일>" -unattended -nopause
"""
import json
import os

import unreal

OUT = "E:/Git_Project/FPSRoguelite/Saved/NeonV/unused_audit.json"

# 우리가 만든 것 = 무조건 루트. 여기 있는 건 후보로 안 올린다.
OURS = ["/Game/Maps", "/Game/Cards", "/Game/Weapons", "/Game/Mission", "/Game/Input",
        "/Game/UI", "/Game/Character", "/Game/Actors", "/Game/Game", "/Game/Config",
        "/Game/Tools", "/Game/SpawnPoints", "/Game/Materials", "/Game/Audio",
        "/Game/Blockout", "/Game/LevelPrototyping", "/Game/Developers", "/Game/Python"]

# 팩(=정리 대상). 여기 있는 것 중 도달 못 한 게 삭제 후보다.
PACKS = ["/Game/Assets", "/Game/LowPolyAnimatedModernGuns", "/Game/PolygonCyberCity",
         "/Game/PolygonMilitary", "/Game/Synty", "/Game/Rifle_01", "/Game/Characters",
         "/Game/StylizedRenderingSystem", "/Game/PolygonScifi",
         "/Game/ProceduralWeaponAnimationSystem", "/Game/PolygonParticleFX",
         "/Game/_SyntyPilot"]


def log(m):
    unreal.log("[AUDIT] %s" % m)


ar = unreal.AssetRegistryHelpers.get_asset_registry()
log("에셋 레지스트리 스캔...")
ar.scan_paths_synchronous(["/Game"], force_rescan=False)
while ar.is_loading_assets():
    pass

all_assets = ar.get_all_assets()
log("전체 에셋 %d" % len(all_assets))

pkg_of = {}
for a in all_assets:
    p = str(a.package_name)
    pkg_of.setdefault(p, []).append(str(a.asset_class_path.asset_name))

opts = unreal.AssetRegistryDependencyOptions(
    include_soft_package_references=True,
    include_hard_package_references=True,
    include_searchable_names=False,
    include_soft_management_references=False,
    include_hard_management_references=False)


def under(pkg, roots):
    return any(pkg == r or pkg.startswith(r + "/") for r in roots)


roots = sorted(p for p in pkg_of if under(p, OURS))
log("루트(우리 폴더) %d 패키지" % len(roots))

# --- 의존성 폐포 ---
seen, stack = set(roots), list(roots)
steps = 0
while stack:
    cur = stack.pop()
    steps += 1
    deps = ar.get_dependencies(cur, opts)
    if not deps:
        continue
    for d in deps:
        ds = str(d)
        if ds in seen or not ds.startswith("/Game"):
            continue
        seen.add(ds)
        stack.append(ds)
log("도달 %d 패키지 (탐색 %d회)" % (len(seen), steps))

# --- 팩 안에서 미도달 = 후보 ---
report = {"total_assets": len(all_assets), "reached": len(seen), "packs": {}}
grand_keep, grand_drop = 0, 0
for pack in PACKS:
    inside = [p for p in pkg_of if under(p, [pack])]
    if not inside:
        continue
    keep = sorted(p for p in inside if p in seen)
    drop = sorted(p for p in inside if p not in seen)
    grand_keep += len(keep)
    grand_drop += len(drop)
    # 남는 것이 어느 하위 폴더에 몰려 있나 (지울 때 폴더 단위로 자를 수 있게)
    sub = {}
    for p in keep:
        rel = p[len(pack) + 1:]
        sub[rel.split("/")[0] if "/" in rel else "(루트)"] = \
            sub.get(rel.split("/")[0] if "/" in rel else "(루트)", 0) + 1
    report["packs"][pack] = {"total": len(inside), "keep": len(keep), "drop": len(drop),
                             "keep_by_subfolder": sub, "keep_list": keep}
    log("%-45s 전체 %5d · 남김 %4d · 삭제후보 %5d" % (pack, len(inside), len(keep), len(drop)))
    if keep and len(keep) <= 60:
        for p in keep:
            log("      남김: %s" % p)

log("합계 — 남김 %d / 삭제후보 %d" % (grand_keep, grand_drop))
report["grand_keep"] = grand_keep
report["grand_drop"] = grand_drop
report["reached_list"] = sorted(seen)

os.makedirs(os.path.dirname(OUT), exist_ok=True)
with open(OUT, "w", encoding="utf-8") as f:
    json.dump(report, f, ensure_ascii=False, indent=1)
log("기록 -> %s" % OUT)
log("AUDIT_DONE")
