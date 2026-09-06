# render_voxel_drone_preview.py — 복셀 드론 미리보기 렌더 (Blender 헤드리스)
#   F:\Blender\blender.exe -b -P Scripts/render_voxel_drone_preview.py -- [입력폴더=Saved/EnemyVoxel]
# gen_voxel_drone.py 가 낸 SM_EnemyVoxel_Drone_preview.obj(+mtl) 를 읽어 PNG 5장(정면·측면·평면·¾·밑면)을 같은 폴더에 쓴다.
# 육안 판정용(실행 문서 §6: 쿼드 드론으로 읽히는가 · 코어 1개가 읽힘점인가). 색 = mtl Kd, 코어·라이트 = Emission.
import bpy, math, os, sys

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
IN_DIR = os.path.abspath(argv[0] if argv else os.path.join("Saved", "EnemyVoxel"))

VIEWS = {  # name: (camera cm, look-at cm). +X 정면. 드론 크기 120×120×45 라 카메라를 쩝쩝이보다 가깝게
    "front":  ((330, 0, 30), (0, 0, 0)),
    "side":   ((0, -330, 30), (0, 0, 0)),
    "top":    ((60, 0, 330), (0, 0, 0)),
    "3q":     ((240, -220, 140), (0, 0, 0)),
    "bottom": ((180, -150, -160), (0, 0, 0)),
}


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
        if m.name.startswith("E3_") or m.name.startswith("E7_"):   # 코어·라이트 = 뜨거운 쪽 이미시브
            bsdf.inputs["Emission Color"].default_value = (1.0, 0.23, 0.31, 1.0)
            bsdf.inputs["Emission Strength"].default_value = 6.0


def setup_world_and_lights():
    scn = bpy.context.scene
    world = bpy.data.worlds.new("W")
    scn.world = world
    world.use_nodes = True
    bg = world.node_tree.nodes.get("Background")
    bg.inputs[0].default_value = (0.02, 0.015, 0.04, 1.0)
    bg.inputs[1].default_value = 1.0

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
    bpy.ops.mesh.primitive_plane_add(size=1000, location=(0, 0, -260))   # 바닥은 밑면 카메라(-160)보다 아래
    floor = bpy.context.active_object
    fm = bpy.data.materials.new("Floor")
    fm.use_nodes = True
    fm.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (0.08, 0.07, 0.13, 1)
    floor.data.materials.append(fm)


def render(name, cam_loc, look_at, out_path):
    import mathutils
    scn = bpy.context.scene
    cam_data = bpy.data.cameras.new(name)
    cam_data.lens = 45
    cam = bpy.data.objects.new(name, cam_data)
    scn.collection.objects.link(cam)
    cam.location = cam_loc
    d = mathutils.Vector(look_at) - mathutils.Vector(cam_loc)
    cam.rotation_euler = d.to_track_quat('-Z', 'Y').to_euler()
    scn.camera = cam
    scn.render.engine = 'BLENDER_EEVEE'
    scn.render.resolution_x = scn.render.resolution_y = 900
    scn.render.filepath = out_path
    scn.render.image_settings.file_format = 'PNG'
    bpy.ops.render.render(write_still=True)
    print(f"[render] {out_path}")


bpy.ops.wm.read_factory_settings(use_empty=True)
objs = import_obj(os.path.join(IN_DIR, "SM_EnemyVoxel_Drone_preview.obj"))
assert objs, "OBJ import failed"
tune_materials()
setup_world_and_lights()
for name, (loc, at) in VIEWS.items():
    render(name, loc, at, os.path.join(IN_DIR, f"drone_{name}.png"))
