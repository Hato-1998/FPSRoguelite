# render_chomper_blockout.py — 복셀 유령 "쩝쩝이" 의 **곡면 블록아웃**을 Blender 로 짓고 직교 3면(+¾) 렌더
#   F:\Blender\blender.exe -b -P Scripts/render_chomper_blockout.py -- [출력폴더=Saved/EnemyVoxel/Blockout]
#
# 용도: 이미지 AI(Nano Banana/GPT Image) → Tripo 다시점 입력의 **설계도**. 팔 2개·발 없음·입 위치·비율을
# 그림으로 고정하는 것이 목적이라 매력은 필요 없고 정확성만 필요하다. 렌더는 Workbench 플랫 조명 + 외곽선.
# 비율은 gen_voxel_chomper.py 의 격자(폭 120 · 높이 165 · 절단면 z=-37.5 · 눈 z -7.5~30 · 팔 z -22.5~-7.5)를
# 그대로 cm 단위로 옮겼다. 복셀 계단만 곡면으로 바뀐다.
import bpy, math, os, sys

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
OUT = os.path.abspath(argv[0] if argv else os.path.join("Saved", "EnemyVoxel", "Blockout"))
os.makedirs(OUT, exist_ok=True)

C = {  # gen_voxel_chomper.PREVIEW_KD 와 동일
    "head": (0.98, 0.46, 0.76), "jaw": (0.92, 0.38, 0.68), "skirt": (0.86, 0.31, 0.63),
    "arm": (0.95, 0.42, 0.72), "sclera": (0.97, 0.97, 1.0), "pupil": (0.16, 0.04, 0.14),
    "dark": (0.17, 0.05, 0.15), "tooth": (1.0, 1.0, 0.94),
}
MATS = {}


def mat(name):
    if name not in MATS:
        m = bpy.data.materials.new(name)
        m.diffuse_color = (*C[name], 1.0)
        MATS[name] = m
    return MATS[name]


def finish(obj, color, smooth=True):
    obj.data.materials.append(mat(color))
    if smooth:
        for p in obj.data.polygons:
            p.use_smooth = True
    return obj


def sphere(loc, r, color, scale=(1, 1, 1), rot=(0, 0, 0)):
    bpy.ops.mesh.primitive_uv_sphere_add(radius=r, location=loc, segments=48, ring_count=24)
    o = bpy.context.active_object
    o.scale = scale
    o.rotation_euler = rot
    return finish(o, color)


def cyl(loc, r, depth, color, rot=(0, 0, 0), scale=(1, 1, 1)):
    bpy.ops.mesh.primitive_cylinder_add(radius=r, depth=depth, location=loc, vertices=64)
    o = bpy.context.active_object
    o.rotation_euler = rot
    o.scale = scale
    return finish(o, color)


def box(loc, size, color, rot=(0, 0, 0)):
    bpy.ops.mesh.primitive_cube_add(size=1, location=loc)
    o = bpy.context.active_object
    o.scale = (size[0] / 2, size[1] / 2, size[2] / 2)
    o.rotation_euler = rot
    return finish(o, color, smooth=False)


def wavy_fringe(r, z_top, z_base, depth, teeth, n=144):
    """윗변은 평평(z_top), 밑변은 cos 파형(z_base .. z_base-depth)인 닫힌 링 메시."""
    import bmesh
    bm = bmesh.new()
    top, bot = [], []
    for i in range(n):
        a = 2 * math.pi * i / n
        x, y = r * math.cos(a), r * math.sin(a)
        zb = z_base - depth * (0.5 + 0.5 * math.cos(teeth * a))
        top.append(bm.verts.new((x, y, z_top)))
        bot.append(bm.verts.new((x, y, zb)))
    for i in range(n):
        j = (i + 1) % n
        bm.faces.new((top[i], top[j], bot[j], bot[i]))
    me = bpy.data.meshes.new("fringe")
    bm.to_mesh(me)
    o = bpy.data.objects.new("fringe", me)
    bpy.context.scene.collection.objects.link(o)
    return finish(o, "skirt")


def build():
    R = 60.0
    # 몸통: 턱(-60..-37.5) / 머리 원통(-37.5..22) / 돔(22 위, 반구를 z 0.75 로 눌러 정수리 67)
    cyl((0, 0, -48.75), R, 22.5, "jaw")
    cyl((0, 0, -7.75), R, 59.5, "head")
    sphere((0, 0, 22), R, "head", scale=(1, 1, 0.75))
    # 치마: 밑단이 9개 파형으로 갈라진 링(격자판의 파형 이빨) — bmesh 로 직접 짓는다
    wavy_fringe(R, z_top=-60, z_base=-70, depth=12.5, teeth=9)
    cyl((0, 0, -65), R - 2, 10, "skirt")  # 안쪽 바닥(치마 속 비침 방지)
    # 정수리 술(불꽃) — 살짝 뒤로 눕힘
    bpy.ops.mesh.primitive_cone_add(radius1=10, radius2=0, depth=24, location=(-3, 0, 76), vertices=32)
    tuft = bpy.context.active_object
    tuft.rotation_euler = (0, math.radians(-12), 0)
    finish(tuft, "head")
    # 얼굴(+X 정면). 눈 = 표면에 묻힌 납작한 공, 동공 = 안쪽 아래, 눈썹 = 바깥이 올라간 막대
    for s in (1, -1):
        sphere((52, s * 22, 11), 16, "sclera", scale=(0.45, 1.0, 1.15))
        sphere((61, s * 15, 5), 5.5, "pupil", scale=(0.6, 1, 1))
        box((55, s * 25, 27), (6, 30, 6), "dark", rot=(math.radians(24 * s), 0, 0))  # 바깥(|y| 큰 쪽)이 위로 = 화난 눈썹
    # 팔: ㄱ자로 앞으로 뻗음(2개만)
    for s in (1, -1):
        cyl((0, s * 68, -15), 8, 22, "arm", rot=(math.radians(90), 0, 0))
        cyl((17, s * 79, -15), 8, 34, "arm", rot=(0, math.radians(90), 0))
        sphere((36, s * 79, -17), 9.5, "arm")
    # 입: 닫힌 입선(정면 ±60° 호를 짧은 막대로) + 살짝 비치는 이빨
    for deg in range(-64, 65, 3):  # 3° 간격 짧은 막대를 겹쳐 연속 호로(점선이면 이미지 AI 가 박음질로 읽는다)
        a = math.radians(deg)
        box((60.5 * math.cos(a), 60.5 * math.sin(a), -37.5), (4, 4.2, 3.5), "dark", rot=(0, 0, a))
    for deg in (-40, -14, 14, 40):
        a = math.radians(deg)
        box((60.5 * math.cos(a), 60.5 * math.sin(a), -40.5), (4, 6, 4), "tooth", rot=(0, 0, a))


def camera(name, loc, target, ortho=True, scale=200):
    cam = bpy.data.cameras.new(name)
    cam.type = 'ORTHO' if ortho else 'PERSP'
    cam.ortho_scale = scale
    cam.lens = 50
    o = bpy.data.objects.new(name, cam)
    bpy.context.scene.collection.objects.link(o)
    o.location = loc
    import mathutils
    d = mathutils.Vector(target) - mathutils.Vector(loc)
    o.rotation_euler = d.to_track_quat('-Z', 'Y').to_euler()
    return o


def main():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scn = bpy.context.scene
    build()
    world = bpy.data.worlds.new("W")
    scn.world = world
    world.color = (0.42, 0.42, 0.45)
    scn.render.engine = 'BLENDER_WORKBENCH'
    sh = scn.display.shading
    sh.light = 'FLAT'
    sh.color_type = 'MATERIAL'
    sh.show_object_outline = True
    sh.object_outline_color = (0.06, 0.02, 0.05)
    sh.show_shadows = False
    sh.show_cavity = False
    scn.display.render_aa = '8'
    scn.render.resolution_x = scn.render.resolution_y = 1024
    scn.render.image_settings.file_format = 'PNG'
    scn.render.film_transparent = False
    views = {
        "front": ((400, 0, 0), (0, 0, 0), True),
        "side":  ((0, -400, 0), (0, 0, 0), True),
        "back":  ((-400, 0, 0), (0, 0, 0), True),
        "3q":    ((330, -300, 140), (0, 0, -5), False),
    }
    for name, (loc, tgt, ortho) in views.items():
        scn.camera = camera(name, loc, tgt, ortho)
        scn.render.filepath = os.path.join(OUT, f"blockout_{name}.png")
        bpy.ops.render.render(write_still=True)
        print(f"[blockout] {scn.render.filepath}")


main()
