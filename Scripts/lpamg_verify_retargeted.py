"""리타게팅 결과 검증 — 읽기 전용.

리타게팅의 대표 실패 모드는 크래시가 아니라 **트랙이 조용히 비는 것**이다(체인 매핑이
어긋나면 결과 애니가 생기긴 하는데 해당 본이 안 움직인다). 그래서 본별 트랙 존재를 센다.
"""
import json, os, unreal
OUT = "E:/Git_Project/FPSRoguelite/Saved/NeonV/lpamg_retarget_verify.json"
PAIRS = [("A_FP_Rifle_Pose", "/Game/ProceduralWeaponAnimationSystem/Animations/Poses/Rifle/A_FP_Rifle_Pose"),
         ("A_FP_Rifle_AimPose", "/Game/ProceduralWeaponAnimationSystem/Animations/Poses/Rifle/A_FP_Rifle_AimPose"),
         ("A_FP_RifleReload", "/Game/ProceduralWeaponAnimationSystem/Animations/Reload/A_FP_RifleReload")]
TGT_DIR = "/Game/Character/FPArms/Anims_LPAMG/"
KEY = ("hand_l","hand_r","lowerarm_l","lowerarm_r","upperarm_l","upperarm_r",
       "index_01_l","index_03_l","thumb_01_l","pinky_03_r","clavicle_l","spine_01")
def log(m): unreal.log("[RVER] %s" % m)
rep, fails = {}, []
for name, src in PAIRS:
    s = unreal.EditorAssetLibrary.load_asset(src)
    t = unreal.EditorAssetLibrary.load_asset(TGT_DIR + name + "_LPAMG")
    if not t:
        fails.append("%s_LPAMG 없음" % name); unreal.log_error("[RVER] !! %s_LPAMG 없음" % name); continue
    st = [str(x) for x in unreal.AnimationLibrary.get_animation_track_names(s)] if s else []
    tt = [str(x) for x in unreal.AnimationLibrary.get_animation_track_names(t)]
    missing = [b for b in KEY if b not in tt]
    rep[name] = {"skeleton": t.get_editor_property("skeleton").get_name(),
                 "frames": t.get_editor_property("number_of_sampled_frames"),
                 "length": round(t.get_editor_property("sequence_length"), 3),
                 "src_tracks": len(st), "tgt_tracks": len(tt), "missing_key_bones": missing}
    log("%-22s 스켈레톤 %-22s 프레임 %-5s 길이 %.2fs · 트랙 원본 %d -> 결과 %d"
        % (name, rep[name]["skeleton"], rep[name]["frames"], rep[name]["length"], len(st), len(tt)))
    if missing:
        fails.append("%s: 핵심 본 트랙 없음 %s" % (name, missing))
        unreal.log_error("[RVER] !! %s 핵심 본 트랙 없음: %s" % (name, missing))
    else:
        log("    핵심 본 12개 트랙 전부 있음")
    if s and abs(rep[name]["length"] - s.get_editor_property("sequence_length")) > 0.01:
        fails.append("%s: 길이가 원본과 다르다" % name)
os.makedirs(os.path.dirname(OUT), exist_ok=True)
json.dump({"results": rep, "fails": fails}, open(OUT,"w",encoding="utf-8"), ensure_ascii=False, indent=1)
log("기록 -> %s" % OUT); log("RVER_FAIL" if fails else "RVER_CLEAN")
