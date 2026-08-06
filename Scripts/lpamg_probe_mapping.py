import unreal
def log(m): unreal.log("[MAP] %s" % m)
rtg = unreal.EditorAssetLibrary.load_asset("/Game/Character/FPArms/Retarget/RTG_PWAS_to_LPAMG")
rc = unreal.IKRetargeterController.get_controller(rtg)
names = {}
for tag, which in (("소스", unreal.RetargetSourceOrTarget.SOURCE), ("타깃", unreal.RetargetSourceOrTarget.TARGET)):
    rig = rc.get_ik_rig(which)
    c = unreal.IKRigController.get_controller(rig)
    ch = [str(x.chain_name) for x in c.get_retarget_chains()]
    names[tag] = ch
    log("%s %s · 루트 %s · 체인 %d: %s" % (tag, rig.get_name(), c.get_retarget_root(), len(ch), sorted(ch)))
ops = [str(rc.get_op_name(i)) for i in range(rc.get_num_retarget_ops())]
fk = next((o for o in ops if o.lower().startswith("fk chains")), None)
log("FK op = %s" % fk)
m = 0
for t in sorted(names["타깃"]):
    s = rc.get_source_chain(t, fk)
    ok = s is not None and str(s) not in ("None", "")
    m += 1 if ok else 0
    log("   %-12s <- %s %s" % (t, s, "" if ok else "❌"))
log("매핑 %d/%d" % (m, len(names["타깃"])))
log("MAP_DONE")
