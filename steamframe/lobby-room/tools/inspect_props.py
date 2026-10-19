import bpy, glob, os, sys, mathutils
root = sys.argv[sys.argv.index("--")+1]
for path in sorted(glob.glob(os.path.join(root, "*", "*.gltf"))):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=path)
    objs = [o for o in bpy.context.scene.objects if o.type == "MESH"]
    lo = mathutils.Vector((1e9,)*3); hi = -lo
    tris = 0
    for o in objs:
        for c in o.bound_box:
            w = o.matrix_world @ mathutils.Vector(c)
            lo = mathutils.Vector(map(min, lo, w)); hi = mathutils.Vector(map(max, hi, w))
        tris += sum(len(p.vertices) - 2 for p in o.data.polygons)
    mats = {m.name: (m.blend_method if hasattr(m, "blend_method") else "", ) for o in objs for m in o.data.materials if m}
    print("PROP", os.path.basename(path), "objs", len(objs), "tris", tris, "size", tuple(round(v,2) for v in hi-lo), "min", tuple(round(v,2) for v in lo), "mats", mats, "uvs", [len(o.data.uv_layers) for o in objs])
