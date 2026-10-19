# Saves a PNG preview of a baked EXR through the AgX view transform: Blender -b --python exr_preview.py -- <in.exr> <out.png> [exposure stops]
import bpy, sys
a = sys.argv[sys.argv.index("--") + 1:]
img = bpy.data.images.load(a[0])
s = bpy.context.scene
s.view_settings.view_transform = "AgX"
s.view_settings.exposure = float(a[2]) if len(a) > 2 else 0.0
s.render.image_settings.file_format = "PNG"
img.save_render(a[1], scene=s)
