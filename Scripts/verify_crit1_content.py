"""CRIT1 콘텐츠 저작 검증 — 이름 테이블이 아니라 **실제 프로퍼티 값**을 읽는다.

uasset 바이너리 grep 은 "그 이름이 파일 안에 있다"까지만 말한다("있다" 검사는 "맞다" 검사가 아니다).
슬롯 상한 같은 정수나 트리거 enum 값은 그렇게는 못 읽으므로, 에디터를 띄워 프로퍼티를 직접 읽는다.

Scripts/run_verify_crit1_content.bat 로 실행(에디터는 닫혀 있어야 한다).
"""

import unreal

FAIL = []
OK = []


def check(label, condition, detail):
    (OK if condition else FAIL).append("%s — %s" % (label, detail))


rifle = unreal.EditorAssetLibrary.load_asset("/Game/Weapons/DataTable/DA_Weapon_Rifle")
if rifle is None:
    print("[CRIT1-VERIFY] FATAL: DA_Weapon_Rifle 로드 실패")
    raise SystemExit(1)

# ① 프래그먼트 슬롯 상한 = 5 (한 무기 = 한 빌드)
slots = rifle.get_editor_property("max_fragment_slots")
check("슬롯 상한", slots == 5, "MaxFragmentSlots=%s (기대 5)" % slots)

# ② 사이트 파츠 슬롯: 스탯 임계 진화로 이관됐는가 + SniperScope 프래그먼트 참조가 풀렸는가
parts = rifle.get_editor_property("weapon_parts") or []
stat_stage_count = 0
frag_stage_count = 0
sniper_refs = 0
threshold_desc = []
for part in parts:
    evo = part.get_editor_property("evolution_fragment")
    if evo and "SniperScope" in str(evo):
        sniper_refs += 1
    for stage in (part.get_editor_property("stages") or []):
        trigger = stage.get_editor_property("trigger")
        if trigger == unreal.FPSRPartStageTrigger.STAT_THRESHOLD:
            stat_stage_count += 1
            threshold_desc.append("%s %s %s" % (
                stage.get_editor_property("stat_axis"),
                stage.get_editor_property("stat_compare"),
                stage.get_editor_property("stat_value")))
        elif trigger == unreal.FPSRPartStageTrigger.FRAGMENT_STACKS:
            frag_stage_count += 1

check("스코프 진화 트리거", stat_stage_count >= 1,
      "스탯 임계 단계 %d개 %s / 프래그먼트 스택 단계 %d개" % (stat_stage_count, threshold_desc, frag_stage_count))

# "있다" 검사는 "맞다" 검사가 아니다 — 단계가 존재해도 임계값이 도달 불가능하면 스코프는 영원히 안 나온다.
# ADSFieldOfView 는 **도(degree) 단위 절대값**이고 RecomputeResolved 가 [5, 170] 으로 클램한다
# (FPSRWeaponInstance.cpp:387). 기본값의 비율(0.65 같은 값)을 그대로 적으면 조용히 죽는다 — 실제로 밟은 함정이다.
ADS_FOV_CLAMP_MIN = 5.0
base_stats = rifle.get_editor_property("base_stats")
base_ads_fov = base_stats.get_editor_property("ads_field_of_view")
print("[CRIT1-VERIFY] INFO 기본 조준 FOV(BaseStats.ADSFieldOfView) = %.2f도" % base_ads_fov)
for part in parts:
    for stage in (part.get_editor_property("stages") or []):
        if stage.get_editor_property("trigger") != unreal.FPSRPartStageTrigger.STAT_THRESHOLD:
            continue
        if stage.get_editor_property("stat_axis") != unreal.FPSRWeaponStat.ADS_FIELD_OF_VIEW:
            continue
        value = stage.get_editor_property("stat_value")
        compare = stage.get_editor_property("stat_compare")
        if compare == unreal.FPSRStatCompare.LESS_OR_EQUAL:
            reachable = value >= ADS_FOV_CLAMP_MIN and value < base_ads_fov
            detail = ("기준값 %.2f도 — 도달하려면 클램 하한 %.0f도 ≤ 기준값 < 기본 %.1f도. 권장 = 기본×0.65 ≈ %.0f도"
                      % (value, ADS_FOV_CLAMP_MIN, base_ads_fov, base_ads_fov * 0.65))
        else:
            reachable = True
            detail = "기준값 %.2f / 비교 %s (도달성 미판정)" % (value, compare)
        check("스코프 임계값 도달 가능성", reachable, detail)
check("SniperScope 프래그먼트 참조 해제", sniper_refs == 0, "남은 참조 %d건 (기대 0)" % sniper_refs)

# ③ 치명타 카드 5장이 미션 풀에 있는가 + SniperScope 카드는 빠졌는가
features = [str(c.get_name()) for c in (rifle.get_editor_property("unlockable_features") or []) if c]
expected = ["DA_CardModifiers_CritOverkill", "DA_CardModifiers_CritLifesteal",
            "DA_CardModifiers_CritOnReload", "DA_CardModifiers_WeakpointCrit",
            "DA_CardModifiers_CritOnSlide"]
missing = [e for e in expected if e not in features]
check("치명타 카드 5장 편입", not missing, "누락 %s / 현재 풀 %s" % (missing or "없음", features))
check("SniperScope 카드 제거", "DA_CardModifiers_SniperScope" not in features, "미션 풀 = %s" % features)

# ④ 치명타 흡혈 프래그먼트의 힐 GE — 없으면 회복이 조용히 no-op 이 된다
leech = unreal.EditorAssetLibrary.load_asset(
    "/Game/Cards/Weapons/Modifiers/DA_Fragment_Rifle_CritLifesteal")
if leech is None:
    check("치명타 흡혈 힐 GE", False, "DA_Fragment_Rifle_CritLifesteal 로드 실패")
else:
    heal = leech.get_editor_property("heal_effect")
    ratio = leech.get_editor_property("heal_ratio")
    check("치명타 흡혈 힐 GE", heal is not None, "HealEffect=%s HealRatio=%s" % (heal, ratio))

# ⑤ 저작-타임 검증기 — WITH_EDITOR IsDataValid 는 빌드·자동화가 절대 못 잡는 계급이다.
# UE5.7 의 is_object_valid 는 usecase 인자가 필수다(EDataValidationUsecase::Manual, DataValidation.h:21).
try:
    validator = unreal.EditorValidatorSubsystem()
    usecase = unreal.DataValidationUsecase.MANUAL
    for path in ["/Game/Weapons/DataTable/DA_Weapon_Rifle",
                 "/Game/Cards/Weapons/Modifiers/DA_Fragment_Rifle_CritLifesteal",
                 "/Game/Cards/Weapons/Modifiers/DA_Fragment_Rifle_CritOnReload",
                 "/Game/Cards/Weapons/Modifiers/DA_Fragment_Rifle_CritOnSlide",
                 "/Game/Cards/Imported/DA_Card_ADSZoom_ThisWeapon"]:
        asset = unreal.EditorAssetLibrary.load_asset(path)
        if asset is None:
            check("IsDataValid %s" % path.rsplit("/", 1)[-1], False, "로드 실패")
            continue
        # 반환은 (EDataValidationResult, warnings[], errors[]) 튜플이다. NOT_VALIDATED 는 "돌 검증기가
        # 없었다"라는 뜻이지 실패가 아니다 — 튜플 자체를 불리언으로 쓰면 항상 참이라 검사가 죽는다.
        result, warns, errors = validator.is_object_valid(asset, usecase)
        label = "IsDataValid %s" % path.rsplit("/", 1)[-1]
        detail = "%s / errors=%s / warnings=%s" % (result, list(errors), list(warns))
        check(label, result != unreal.DataValidationResult.INVALID and not list(errors), detail)
except Exception as error:
    FAIL.append("IsDataValid 실행 자체 실패 — %s" % error)

def report():
    print("[CRIT1-VERIFY] ===== PASS %d / FAIL %d =====" % (len(OK), len(FAIL)))
    for line in OK:
        print("[CRIT1-VERIFY] OK   " + line)
    for line in FAIL:
        print("[CRIT1-VERIFY] FAIL " + line)
    print("[CRIT1-VERIFY] DONE")

report()
