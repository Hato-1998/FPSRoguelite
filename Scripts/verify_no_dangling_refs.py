"""정리 후 **남은 에셋이 사라진 것을 가리키고 있지 않은지** 검사한다 — 읽기 전용.

삭제 판정은 "우리 루트에서 도달 안 함" 이었다. 그 판정이 맞았다면 남은 것 중 누구도 지워진 것을
참조하지 않아야 한다. 그게 아니면 화면에 회색 덩어리·빈 슬롯이 나오고, 원인을 나중에 찾기 어렵다.
에셋을 로드하지 않고 레지스트리 의존성만 훑으므로 빠르다.

실행: UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script="<이 파일>" -unattended -nopause
"""
import json
import os

import unreal

OUT = "E:/Git_Project/FPSRoguelite/Saved/NeonV/dangling_check.json"
CONTENT = "E:/Git_Project/FPSRoguelite/Content/"


def log(m):
    unreal.log("[DANGLE] %s" % m)


ar = unreal.AssetRegistryHelpers.get_asset_registry()
log("레지스트리 재스캔(강제)...")
ar.scan_paths_synchronous(["/Game"], force_rescan=True)
while ar.is_loading_assets():
    pass

pkgs = sorted({str(a.package_name) for a in ar.get_all_assets()})
log("남은 패키지 %d" % len(pkgs))


def exists(pkg):
    base = CONTENT + pkg[len("/Game/"):]
    return os.path.exists(base + ".uasset") or os.path.exists(base + ".umap")


opts = unreal.AssetRegistryDependencyOptions(
    include_soft_package_references=True, include_hard_package_references=True,
    include_searchable_names=False, include_soft_management_references=False,
    include_hard_management_references=False)

dangling = {}
checked = 0
for p in pkgs:
    checked += 1
    for d in (ar.get_dependencies(p, opts) or []):
        ds = str(d)
        if not ds.startswith("/Game/"):
            continue
        if not exists(ds):
            dangling.setdefault(p, []).append(ds)

log("검사 %d 패키지 · 끊어진 참조를 가진 에셋 %d개" % (checked, len(dangling)))
for p, ds in list(dangling.items())[:30]:
    log("  %s -> %s" % (p, ds[:4]))

os.makedirs(os.path.dirname(OUT), exist_ok=True)
with open(OUT, "w", encoding="utf-8") as f:
    json.dump({"checked": checked, "dangling_count": len(dangling), "dangling": dangling},
              f, ensure_ascii=False, indent=1)
log("기록 -> %s" % OUT)
log("DANGLE_FAIL" if dangling else "DANGLE_CLEAN")
