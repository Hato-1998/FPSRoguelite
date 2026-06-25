# Balance pass 2: level-based spawn density anchors on DA_RunSchedule. Run headless (editor CLOSED) via -run=pythonscript.
import unreal
EAL = unreal.EditorAssetLibrary
def log(m): unreal.log_warning("[BAL2B] " + str(m))
def err(m): unreal.log_error("[BAL2B][ERR] " + str(m))

p = "/Game/Game/Data/DA_RunSchedule"
try:
    rs = EAL.load_asset(p)
    def anchor(lv, ct):
        # Keyword construction (NOT set_editor_property on the instance — EditDefaultsOnly blocks instance edits).
        return unreal.FPSRAliveCountAnchor(level=int(lv), count=int(ct))
    rs.set_editor_property("AliveCountByLevel", [anchor(1, 10), anchor(20, 30), anchor(30, 50)])
    EAL.save_asset(p, only_if_is_dirty=False)
    rb = [(a.get_editor_property("Level"), a.get_editor_property("Count")) for a in rs.get_editor_property("AliveCountByLevel")]
    log("AliveCountByLevel readback = %s" % rb)
    log("MaxAliveCount=%s  BaseAliveCount(legacy,inert)=%s  SpawnInterval=%s  MaxSpawnPerTick=%s" % (
        rs.get_editor_property("MaxAliveCount"), rs.get_editor_property("BaseAliveCount"),
        rs.get_editor_property("SpawnIntervalSeconds"), rs.get_editor_property("MaxSpawnPerTick")))
    log("=== BAL2B DONE ===")
except Exception as e:
    err("DENSITY apply: %s" % e)
