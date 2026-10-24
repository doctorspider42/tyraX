"""Original PS2 fieldstone. Run with Blender -b --factory-startup --python FILE.

Cycles bakes procedural mineral colour and occlusion to a shared 128px atlas.
No downloaded textures; the saved .blend remains editable.
"""
from pathlib import Path
import math, random
import bpy
from mathutils import Vector

HERE=Path(__file__).resolve().parent
OUT=HERE.parent/'res/models'
rng=random.Random(8217)
bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)
bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=2, radius=1)
rock=bpy.context.object; rock.name='Weathered fieldstone'
for v in rock.data.vertices:
    x,y,z=v.co; r=rng.uniform(.88,1.10)
    v.co=(x*.74*r+.09*z,y*.65*r,max(-.12,z*.48*r+.36))
mod=rock.modifiers.new('Broad fractured planes','DECIMATE');mod.ratio=.7
bpy.ops.object.modifier_apply(modifier=mod.name)
bpy.ops.object.mode_set(mode='EDIT');bpy.ops.mesh.select_all(action='SELECT')
bpy.ops.uv.smart_project(angle_limit=1.15,island_margin=.035)
bpy.ops.object.mode_set(mode='OBJECT')
mat=bpy.data.materials.new('Granite with weathered mineral seams');mat.use_nodes=True
rock.data.materials.append(mat)
n=mat.node_tree.nodes;l=mat.node_tree.links;bs=n.get('Principled BSDF')
tex=n.new('ShaderNodeTexNoise');tex.inputs['Scale'].default_value=5.5;tex.inputs['Detail'].default_value=5
ramp=n.new('ShaderNodeValToRGB');ramp.color_ramp.elements[0].position=.25;ramp.color_ramp.elements[0].color=(.075,.085,.073,1)
ramp.color_ramp.elements[1].position=.75;ramp.color_ramp.elements[1].color=(.39,.37,.31,1)
l.new(tex.outputs['Fac'],ramp.inputs[0])
grain=n.new('ShaderNodeTexNoise');grain.inputs['Scale'].default_value=95;grain.inputs['Detail'].default_value=2
mix=n.new('ShaderNodeMixRGB');mix.blend_type='MULTIPLY';mix.inputs[0].default_value=.3
l.new(ramp.outputs[0],mix.inputs[1]);l.new(grain.outputs['Fac'],mix.inputs[2])
ao=n.new('ShaderNodeAmbientOcclusion');ao.inputs['Distance'].default_value=.65
shade=n.new('ShaderNodeMixRGB');shade.blend_type='MULTIPLY';shade.inputs[0].default_value=.65
l.new(mix.outputs[0],shade.inputs[1]);l.new(ao.outputs['Color'],shade.inputs[2])
l.new(shade.outputs[0],bs.inputs['Base Color']);bs.inputs['Roughness'].default_value=.95
img=bpy.data.images.new('Baked stone',128,128,alpha=False)
target=n.new('ShaderNodeTexImage');target.image=img;n.active=target
scene=bpy.context.scene;scene.render.engine='CYCLES';scene.cycles.samples=32
scene.render.bake.use_pass_direct=False;scene.render.bake.use_pass_indirect=False
scene.render.bake.use_pass_color=True;scene.render.bake.margin=4
bpy.ops.object.bake(type='DIFFUSE')
img.filepath_raw=str(OUT/'rock.png');img.file_format='PNG';img.save()
l.new(target.outputs['Color'],bs.inputs['Base Color'])

def export(obj,stem):
    mesh=obj.data;mesh.calc_loop_triangles();uv=mesh.uv_layers.active.data
    lines=['# Original Blender fieldstone; Y-up','mtllib rock.mtl','o '+stem,'usemtl stone']
    for v in mesh.vertices: lines.append('v %.6f %.6f %.6f'%(v.co.x,v.co.z,-v.co.y))
    for loop in mesh.loops: lines.append('vt %.6f %.6f'%tuple(uv[loop.index].uv))
    for tri in mesh.loop_triangles:
        lines.append('f '+' '.join(f'{mesh.loops[i].vertex_index+1}/{i+1}' for i in tri.loops))
    (OUT/(stem+'.obj')).write_text('\n'.join(lines)+'\n')
    print(stem,len(mesh.loop_triangles),'triangles')
export(rock,'rock')
far=rock.copy();far.data=rock.data.copy();bpy.context.collection.objects.link(far)
bpy.context.view_layer.objects.active=far
mod=far.modifiers.new('Distant silhouette','DECIMATE');mod.ratio=.22
bpy.ops.object.modifier_apply(modifier=mod.name);export(far,'rock-far');far.hide_render=True;far.hide_set(True)
(OUT/'rock.mtl').write_text('newmtl stone\nKd 1 1 1\nmap_Kd rock.png\n')
bpy.ops.mesh.primitive_plane_add(size=200,location=(0,0,-.125))
ground=bpy.context.object;gm=bpy.data.materials.new('Studio');gm.diffuse_color=(.12,.15,.10,1);ground.data.materials.append(gm)
bpy.ops.object.light_add(type='AREA',location=(1,-3,5));bpy.context.object.data.energy=450;bpy.context.object.data.shape='DISK';bpy.context.object.data.size=4
bpy.ops.object.camera_add(location=(2.2,-3,1.8));cam=bpy.context.object;cam.rotation_euler=(Vector((0,0,.3))-cam.location).to_track_quat('-Z','Y').to_euler();cam.data.type='ORTHO';cam.data.ortho_scale=2.5;scene.camera=cam
scene.world.color=(.25,.25,.25);scene.render.resolution_x=800;scene.render.resolution_y=600;scene.render.resolution_percentage=100
scene.render.filepath=str(HERE/'rock-preview.png');scene.view_settings.view_transform='AgX'
img.pack();bpy.ops.wm.save_as_mainfile(filepath=str(HERE/'rock.blend'))
bpy.ops.render.render(write_still=True)
