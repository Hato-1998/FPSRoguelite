# 반동 P2 검증용 — 라이플 heat-확산 프로파일 저작 (헤드리스, -run=pythonscript).
# FRuntimeFloatCurve.EditorCurveData 는 UPROPERTY()(비노출)라 set_editor_property 불가 →
# 구조체 import_text(T3D 텍스트)로 주입(텍스트 시리얼라이저는 비노출 UPROPERTY도 처리). export_text 로 read-back.
# 기존 블룸 근사: 25heat/°, 발당 +8heat(≈0.32°), 최대 100heat(=4°), 냉각 150heat/s(≈6°/s), grace 0.15s. 멱등.
import unreal

def log(m): unreal.log_warning("[RECOIL_P2] " + str(m))
def err(m): unreal.log_error("[RECOIL_P2][ERR] " + str(m))

EAL = unreal.EditorAssetLibrary
RIFLE = "/Game/Weapons/DataTable/DA_Weapon_Rifle"

# (heat, value) 키. X축=현재 heat(플러그인 자기참조 곡선).
SHOT_TO_HEAT   = [(0.0, 8.0),   (100.0, 8.0)]    # 발당 고정 +8 heat
HEAT_TO_SPREAD = [(0.0, 0.0),   (100.0, 4.0)]    # heat0→0°(앵커), 100→4° 선형
HEAT_TO_COOL   = [(0.0, 150.0), (100.0, 150.0)]  # 150 heat/초 상수(≈6°/s)
MAX_HEAT = 100.0
COOL_DELAY = 0.15

def rtc_text(pairs):
    keys = ",".join("(Time=%f,Value=%f,InterpMode=RCIM_Linear)" % (float(t), float(v)) for (t, v) in pairs)
    return "(EditorCurveData=(Keys=(%s)))" % keys

def set_runtime_curve(owner, prop, pairs):
    rtc = unreal.RuntimeFloatCurve()
    rtc.import_text(rtc_text(pairs))
    owner.set_editor_property(prop, rtc)

def key_count(owner, prop):
    # export_text 의 'Time=' 개수로 키 개수 근사 검증.
    rtc = owner.get_editor_property(prop)
    txt = rtc.export_text()
    return txt.count("Time=")

try:
    w = EAL.load_asset(RIFLE)
    if not w:
        err("라이플 DA 로드 실패: %s" % RIFLE)
    else:
        set_runtime_curve(w, "ShotToHeatCurve", SHOT_TO_HEAT)
        set_runtime_curve(w, "HeatToSpreadAngleCurve", HEAT_TO_SPREAD)
        set_runtime_curve(w, "HeatToCooldownPerSecondCurve", HEAT_TO_COOL)
        w.set_editor_property("MaxRecoilHeat", float(MAX_HEAT))
        w.set_editor_property("RecoilHeatCooldownDelay", float(COOL_DELAY))
        EAL.save_asset(RIFLE, only_if_is_dirty=False)
        n1 = key_count(w, "ShotToHeatCurve")
        n2 = key_count(w, "HeatToSpreadAngleCurve")
        n3 = key_count(w, "HeatToCooldownPerSecondCurve")
        mh = w.get_editor_property("MaxRecoilHeat")
        cd = w.get_editor_property("RecoilHeatCooldownDelay")
        log("RIFLE heat 프로파일 저장: ShotToHeat=%d, HeatToSpread=%d, HeatToCool=%d keys | MaxHeat=%s CoolDelay=%s" % (n1, n2, n3, mh, cd))
        # 참고용 export 덤프(눈으로 곡선 확인)
        log("ShotToHeat export = %s" % w.get_editor_property("ShotToHeatCurve").export_text())
        log("HeatToSpread export = %s" % w.get_editor_property("HeatToSpreadAngleCurve").export_text())
        if n1 == 2 and n2 == 2 and n3 == 2:
            log("===RIFLE_HEAT_OK===")
        else:
            err("키 개수 불일치(각 2 기대): %d/%d/%d" % (n1, n2, n3))
except Exception as e:
    err("RIFLE heat 저작 예외: %s" % e)

log("===RECOIL_P2_DONE===")
