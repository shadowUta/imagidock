"""Runs only in a disposable Blender process with --disable-autoexec.

Never saves the source blend. Existing visible geometry is framed by a
temporary camera; existing materials remain intact.
"""
import math
import sys
from pathlib import Path
import bpy
from mathutils import Vector

arguments = sys.argv[sys.argv.index("--") + 1:]
output, size = Path(arguments[0]).resolve(), int(arguments[1])
scene = bpy.context.scene
points = [obj.matrix_world @ Vector(point) for obj in scene.objects
          if obj.type in ("MESH", "CURVE", "SURFACE", "META", "FONT") and not obj.hide_render
          for point in obj.bound_box]
if not points:
    raise RuntimeError("Blend contains no visible geometry")
minimum = Vector(tuple(min(p[i] for p in points) for i in range(3)))
maximum = Vector(tuple(max(p[i] for p in points) for i in range(3)))
center, extent = (minimum + maximum) * 0.5, max((maximum - minimum).length, 0.1)
camera_data = bpy.data.cameras.new("ImagiDock preview camera")
camera = bpy.data.objects.new("ImagiDock preview camera", camera_data)
scene.collection.objects.link(camera)
camera.location = center + Vector((1.2, -1.6, 1.0)).normalized() * extent * 1.7
camera.rotation_euler = (center - camera.location).to_track_quat("-Z", "Y").to_euler()
camera_data.clip_end = max(1000, extent * 10)
scene.camera = camera
for index, direction in enumerate(((1, -2, 3), (-2, -1, 1), (1, 2, 2))):
    data = bpy.data.lights.new("ImagiDock light", "AREA")
    data.energy = 500 * extent * extent
    data.shape = "DISK"
    data.size = extent * 2
    light = bpy.data.objects.new(f"ImagiDock light {index}", data)
    scene.collection.objects.link(light)
    light.location = center + Vector(direction) * extent
    light.rotation_euler = (center - light.location).to_track_quat("-Z", "Y").to_euler()
for engine in ("BLENDER_EEVEE_NEXT", "BLENDER_EEVEE"):
    try:
        scene.render.engine = engine
        break
    except TypeError:
        continue
else:
    raise RuntimeError("This Blender installation has no supported Eevee renderer")
scene.render.use_compositing = False
scene.render.use_sequencer = False
scene.render.resolution_x = scene.render.resolution_y = size
scene.render.resolution_percentage = 100
scene.render.image_settings.file_format = "PNG"
scene.render.image_settings.color_mode = "RGBA"
scene.render.film_transparent = True
scene.render.filepath = str(output)
scene.render.use_file_extension = True
bpy.ops.render.render(write_still=True)
