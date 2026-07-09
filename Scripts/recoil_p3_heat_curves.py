# 반동 P3 — 전 원거리 무기 heat-확산 프로파일 저작 (헤드리스, -run=pythonscript).
# 규약: heat = 확산 도(°) (스케일 1:1). 기존 블룸 값에서 그대로 마이그레이션:
#   MaxRecoilHeat = MaxBloom, ShotToHeat = 상수(BloomPerShot), HeatToSpreadAngle = 항등(0,0)->(Max,Max),
#   HeatToCooldownPerSecond = 상수(BloomRecoveryRate), Delay = 0.15(연사 중 확산 유지 grace; 튜닝 노브).
# ChargeLaser(base-only)·Knife(근접)는 제외(빈 프로파일 유지). MaxBloom<=0 또는 BloomPerShot<=0 = 동적확산 없음(스킵).
# FRuntimeFloatCurve.EditorCurveData는 UPROPERTY()(비노출) → import_text T3D 주입. 멱등.
import unreal

def log(m): unreal.log_warning("[RECOIL_P3] " + str(m))
def err(m): unreal.log_error("[RECOIL_P3][ERR] " + str(m))

EAL = unreal.EditorAssetLibrary
DELAY = 0.15

# 대상 무기(원거리, 동적확산). ChargeLaser/Knife 제외.
WEAPONS = ["Rifle", "SMG", "LMG", "Shotgun", "Sniper", "Bazooka"]

def const_curve_text(cap, val):
    return "(EditorCurveData=(Keys=((Time=0.000000,Value=%f,InterpMode=RCIM_Linear),(Time=%f,Value=%f,InterpMode=RCIM_Linear))))" % (float(val), float(cap), float(val))

def identity_curve_text(cap):
    return "(EditorCurveData=(Keys=((Time=0.000000,Value=0.000000,InterpMode=RCIM_Linear),(Time=%f,Value=%f,InterpMode=RCIM_Linear))))" % (float(cap), float(cap))

def set_curve(owner, prop, text):
    rtc = unreal.RuntimeFloatCurve()
    rtc.import_text(text)
    owner.set_editor_property(prop, rtc)

def clear_curve(owner, prop):
    owner.set_editor_property(prop, unreal.RuntimeFloatCurve())

ok = 0
for name in WEAPONS:
    path = "/Game/Weapons/DataTable/DA_Weapon_%s" % name
    try:
        w = EAL.load_asset(path)
        if not w:
            err("%s: 로드 실패 %s" % (name, path)); continue
        bs = w.get_editor_property("BaseStats")
        arche = bs.get_editor_property("Archetype")
        spread = float(bs.get_editor_property("SpreadDegrees"))
        per = float(bs.get_editor_property("BloomPerShot"))
        mx = float(bs.get_editor_property("MaxBloom"))
        rec = float(bs.get_editor_property("BloomRecoveryRate"))
        fr = float(bs.get_editor_property("FireRate"))
        log("%s | Archetype=%s SpreadDeg=%.3f BloomPerShot=%.3f MaxBloom=%.3f Recover=%.3f/s FireRate=%.2f"
            % (name, arche, spread, per, mx, rec, fr))

        if mx <= 0.0 or per <= 0.0:
            # 동적확산 의도 없음(예: 정밀무기). 빈 프로파일 유지 → base SpreadDegrees만.
            clear_curve(w, "ShotToHeatCurve"); clear_curve(w, "HeatToSpreadAngleCurve"); clear_curve(w, "HeatToCooldownPerSecondCurve")
            EAL.save_asset(path, only_if_is_dirty=False)
            log("  -> 동적확산 없음(MaxBloom/BloomPerShot<=0): 빈 프로파일(base 확산만). 저장.")
            ok += 1
            continue

        cool = rec if rec > 0.0 else 1.0  # 0이면 냉각 안 됨(폭주) 방지 최소값
        set_curve(w, "ShotToHeatCurve", const_curve_text(mx, per))          # 발당 +per(°)
        set_curve(w, "HeatToSpreadAngleCurve", identity_curve_text(mx))     # heat(°) = spread(°)
        set_curve(w, "HeatToCooldownPerSecondCurve", const_curve_text(mx, cool))  # rec °/s
        w.set_editor_property("MaxRecoilHeat", float(mx))
        w.set_editor_property("RecoilHeatCooldownDelay", float(DELAY))
        EAL.save_asset(path, only_if_is_dirty=False)
        # read-back
        e1 = w.get_editor_property("ShotToHeatCurve").export_text()
        log("  -> heat=도 프로파일 저장: Max=%.3f ShotToHeat=%.3f Cool=%.3f/s Delay=%.2f | ShotToHeat=%s"
            % (mx, per, cool, DELAY, e1))
        ok += 1
    except Exception as e:
        err("%s: 예외 %s" % (name, e))

log("완료: %d/%d 무기 처리" % (ok, len(WEAPONS)))
if ok == len(WEAPONS):
    log("===RECOIL_P3_ALL_OK===")
log("===RECOIL_P3_DONE===")
