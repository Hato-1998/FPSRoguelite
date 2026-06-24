# Headless dump of current balance-relevant DataAsset values (run with editor CLOSED via -run=pythonscript).
import unreal

def p(msg):
    unreal.log_warning("[BALDUMP] " + str(msg))

EAL = unreal.EditorAssetLibrary

def get(obj, name):
    try:
        return obj.get_editor_property(name)
    except Exception as e:
        return "<err:%s>" % e

def aslist(x):
    try:
        return [e for e in x]
    except Exception:
        return []

def names(lst):
    try:
        return [c.get_name() if c else "None" for c in lst]
    except Exception:
        return str(lst)

p("===BALANCE_DUMP_START===")

# --- Weapons ---
for w in ["Rifle","BurstRifle","Sniper","Shotgun","LMG","Bazooka","Grenade","ChargeLaser","Knife"]:
    a = EAL.load_asset("/Game/Weapons/DataTable/DA_Weapon_%s" % w)
    if not a:
        p("WEAPON %s: <not found>" % w); continue
    bs = get(a, "BaseStats")
    p("WEAPON %s: Damage=%s FireRate=%s Mag=%s Pellet=%s ChargeTick=%s Reload=%s Arch=%s" % (
        w, get(bs,"Damage"), get(bs,"FireRate"), get(bs,"MagSize"),
        get(bs,"PelletCount"), get(bs,"ChargeTickDamage"), get(bs,"ReloadTime"), get(bs,"Archetype")))
    p("   %s.WeaponCards=%s" % (w, names(get(a,"WeaponCards"))))

# --- Card pool ---
cp = EAL.load_asset("/Game/Cards/Character/DA_Character_CardPool")
if cp:
    p("CARDPOOL.Cards=%s" % names(get(cp,"Cards")))
    p("CARDPOOL.WeaponUnlockCards=%s" % names(get(cp,"WeaponUnlockCards")))
    p("CARDPOOL weights C=%s R=%s E=%s L=%s" % (get(cp,"CommonWeight"),get(cp,"RareWeight"),get(cp,"EpicWeight"),get(cp,"LegendaryWeight")))
    p("CARDPOOL luck C=%s R=%s E=%s L=%s" % (get(cp,"LuckPerRarity_Common"),get(cp,"LuckPerRarity_Rare"),get(cp,"LuckPerRarity_Epic"),get(cp,"LuckPerRarity_Legendary")))

# --- Run schedule ---
rs = EAL.load_asset("/Game/Game/Data/DA_RunSchedule")
if rs:
    p("RUNSCHED BossTime=%s Base=%s PerMin=%s PerMinAfterBoss=%s Max=%s" % (
        get(rs,"BossTime"), get(rs,"BaseAliveCount"), get(rs,"AliveCountPerMinute"),
        get(rs,"AliveCountPerMinuteAfterBoss"), get(rs,"MaxAliveCount")))
    mw = aslist(get(rs,"MissionWindows"))
    p("   MissionWindows count=%d" % len(mw))
    for i,win in enumerate(mw):
        p("   Window[%d] Min=%s Max=%s Pool=%s" % (i, get(win,"MinTime"), get(win,"MaxTime"), names(get(win,"MissionPool"))))
    bdr = get(rs,"BossDefinition")
    p("   BossDefinition=%s" % (bdr.get_name() if bdr else "None"))

# --- Boss def ---
bd = EAL.load_asset("/Game/Character/Boss/DA_BossDefinition")
if bd:
    p("BOSSDEF MaxHealth=%s bUseBossSpawnPoint=%s" % (get(bd,"MaxHealth"), get(bd,"bUseBossSpawnPoint")))

# --- Mission HoldZone ---
mh = EAL.load_asset("/Game/Mission/Data/DA_Mission_HoldZone")
if mh:
    p("MISSION_HoldZone TimeLimit=%s Display=%s" % (get(mh,"TimeLimit"), get(mh,"DisplayName")))

# --- Sample cards: effects + rarity tiers ---
for cp2 in ["/Game/Cards/Character/Data/DA_Card_Damage",
            "/Game/Cards/Weapons/Card/DA_Card_FireRate_ThisWeapon",
            "/Game/Cards/Weapons/Card/DA_Card_MagSize_ThisWeapon",
            "/Game/Cards/Character/Data/DA_Card_MaxHealth",
            "/Game/Cards/Character/Data/DA_Card_Character_HealthRegen",
            "/Game/Cards/Character/Data/DA_Card_Luck"]:
    c = EAL.load_asset(cp2)
    if not c:
        p("CARD %s: <not found>" % cp2); continue
    p("CARD %s: Group=%s Weight=%s Family=%s" % (cp2.split("/")[-1], get(c,"Group"), get(c,"Weight"), get(c,"CardFamily")))
    effects = aslist(get(c,"Effects"))
    p("   Effects count=%d" % len(effects))
    for j,eff in enumerate(effects):
        if not eff:
            p("   Effect[%d] None" % j); continue
        tinfo = []
        for t in aslist(get(eff,"RarityTiers")):
            tinfo.append("%s=%s" % (get(t,"Rarity"), get(t,"Magnitude")))
        p("   Effect[%d] %s Stat=%s Op=%s ThisWeapon=%s Tiers=%s" % (
            j, eff.get_class().get_name(), get(eff,"Stat"), get(eff,"Op"), get(eff,"bThisWeaponOnly"), tinfo))

# --- Enemy BP health (CDO) ---
bp = EAL.load_asset("/Game/Character/Enemy/BP_EnemyBase")
if bp:
    try:
        cdo = unreal.get_default_object(bp.generated_class())
        hc = cdo.get_editor_property("HealthComponent")
        p("ENEMY_BP HealthComponent.MaxHealth=%s MoveSpeed=%s AttackDamage=%s XPReward=%s" % (
            get(hc,"MaxHealth"), get(cdo,"MoveSpeed"), get(cdo,"AttackDamage"), get(cdo,"XPReward")))
    except Exception as e:
        p("ENEMY_BP dump err: %s" % e)
else:
    p("ENEMY_BP: <not found at /Game/Character/Enemy/BP_EnemyBase>")

p("===BALANCE_DUMP_END===")
