"""리타게터 op 스택을 나열한다 — 읽기 전용."""
import unreal
rtg = unreal.EditorAssetLibrary.load_asset("/Game/Character/FPArms/Retarget/RTG_PWAS_to_LPAMG")
rc = unreal.IKRetargeterController.get_controller(rtg)
n = rc.get_num_retarget_ops()
unreal.log("[OPS] op 개수 = %d" % n)
for i in range(n):
    name = rc.get_op_name(i)
    on = rc.get_retarget_op_enabled(i)
    parent = rc.get_parent_op_by_name(name)
    unreal.log("[OPS] [%d] %-24s enabled=%s parent=%s" % (i, name, on, parent))
unreal.log("[OPS] OPS_DONE")
