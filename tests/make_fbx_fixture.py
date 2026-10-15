"""Run in Blender --background --factory-startup to produce original FBX test data."""
from pathlib import Path
import bpy

root=Path(__file__).resolve().parents[1]/'.local'/'fbx-fixture'
root.mkdir(parents=True,exist_ok=True)
bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)
bpy.ops.mesh.primitive_cube_add(size=2)
cube=bpy.context.object
material=bpy.data.materials.new('Fixture texture material')
material.use_nodes=True
image=bpy.data.images.new('Fixture albedo',width=16,height=16,alpha=True)
image.pixels=[.1,.55,.9,1.0]*256
image.filepath_raw=str(root/'颜色.png')
image.file_format='PNG'
image.save()
texture=material.node_tree.nodes.new('ShaderNodeTexImage')
texture.image=image
principled=material.node_tree.nodes.get('Principled BSDF')
material.node_tree.links.new(texture.outputs['Color'],principled.inputs['Base Color'])
cube.data.materials.append(material)
bpy.ops.export_scene.fbx(filepath=str(root/'有贴图模型.fbx'),use_selection=True,path_mode='RELATIVE',embed_textures=False)
