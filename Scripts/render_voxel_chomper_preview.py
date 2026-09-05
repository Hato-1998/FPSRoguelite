# render_voxel_chomper_preview.py — 복셀 유령 "쩝쩝이" 미리보기 렌더 (Blender 헤드리스)
#   F:\Blender\blender.exe -b -P Scripts/render_voxel_chomper_preview.py -- [입력폴더=Saved/EnemyVoxel]
# gen_voxel_chomper.py 가 낸 *_preview_closed.obj / *_preview_open.obj 를 읽어 PNG 5장을 같은 폴더에 쓴다.
# 육안 판정용(실루엣·눈·입선·열림 시 이빨/혀/코어). UE 룩과는 무관 — 색은 mtl 의 Kd 그대로.
import bpy, math, os, sys

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
IN_DIR = os.path.abspath(argv[0] if argv else os.path.join("Saved", "EnemyVoxel"))

VIEWS = {
    # name: (camera location cm, look-at cm). +X 가 정면.
    "closed_front":  ((420, 0, 20), (0, 0, 0)),
    "closed_3q":     ((330, -300, 130), (0, 0, 0)),
    "closed_back":   ((-330, 280, 120), (0, 0, 0)),
    "open_front":    ((420, 0, 40), (0, 0, -10)),
    "open_3q":       ((320, -290, 150), (0, 0, -10)),
}


def clear_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)


def import_obj(path):
    bpy.ops.wm.obj_import(filepath=path, forward_axis='Y', up_axis='Z')
    return [o for o in bpy.context.scene.objects if o.type == 'MESH']


def tune_materials():
    for m in bpy.data.materials:
        if not m.use_nodes:
            continue
        bsdf = m.node_tree.nodes.get("Principled BSDF")
        if not bsdf:
            continue
        bsdf.inputs["Roughness"].default_value = 0.55
        if m.name.startswith("E4_"):   # 코어 발광
            bsdf.inputs["Emission Color"].default_value = (1.0, 0.12, 0.48, 1.0)
            bsdf.inputs["Emission Strength"].default_value = 6.0


def setup_world_and_lights():
    scn = bpy.context.scene
    world = bpy.data.worlds.new("W")
    scn.world = world
    world.use_nodes = True
    bg = world.node_tree.nodes.get("Background")
    bg.inputs[0].default_value = (0.02, 0.015, 0.04, 1.0)
    bg.inputs[1].default_value = 1.0
    # 키 라이트(정면 위) + 필(뒤) — 복셀 계단이 읽히게 그림자 살림
    def sun(name, rot, energy, color=(1, 1, 1)):
        d = bpy.data.lights.new(name, 'SUN')
        d.energy = energy
        d.color = color
        d.angle = math.radians(6)
        o = bpy.data.objects.new(name, d)
        o.rotation_euler = rot
        scn.collection.objects.link(o)
    sun("Key", (math.radians(50), math.radians(-15), math.radians(35)), 3.5)
    sun("Fill", (math.radians(60), math.radians(20), math.radians(200)), 1.2, (0.7, 0.75, 1.0))
    # 바닥판(z = -80cm 바닥 기준 = 유령 밑면) — 크기 감각용
    bpy.ops.mesh.primitive_plane_add(size=1000, location=(0, 0, -80.5))
    floor = bpy.context.active_object
    fm = bpy.data.materials.new("Floor")
    fm.use_nodes = True
    fm.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (0.08, 0.07, 0.13, 1)
    floor.data.materials.append(fm)


def render(name, cam_loc, look_at, out_path):
    scn = bpy.context.scene
    cam_data = bpy.data.cameras.new(name)
    cam_data.lens = 45
    cam = bpy.data.objects.new(name, cam_data)
    scn.collection.objects.link(cam)
    cam.location = cam_loc
    dx, dy, dz = (look_at[0] - cam_loc[0], look_at[1] - cam_loc[1], look_at[2] - cam_loc[2])
    import mathutils
    cam.rotation_euler = mathutils.Vector((dx, dy, dz)).to_track_quat('-Z', 'Y').to_euler()
    scn.camera = cam
    scn.render.engine = 'BLENDER_EEVEE'
    scn.render.resolution_x = 900
    scn.render.resolution_y = 900
    scn.render.filepath = out_path
    scn.render.image_settings.file_format = 'PNG'
    bpy.ops.render.render(write_still=True)
    print(f"[render] {out_path}")


def run(pose):
    clear_scene()
    objs = import_obj(os.path.join(IN_DIR, f"SM_EnemyVoxel_Chomper_preview_{pose}.obj"))
    assert objs, f"OBJ 임포트 실패: {pose}"
    tune_materials()
    setup_world_and_lights()
    for name, (loc, at) in VIEWS.items():
        if name.startswith(pose):
            render(name, loc, at, os.path.join(IN_DIR, f"preview_{name}.png"))


run("closed")
run("open")
