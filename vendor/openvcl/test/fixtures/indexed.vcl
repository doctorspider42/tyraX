 ; this is slower than above, but uses a lower inst in place of
 ; one upper
 ;
 ;
 ; looping
 ;
 ;
 ; ---------------------------------------------------
 ; prepare to loop over the directional lights (parallel)
 ;
 ; outputs: num_dir_lights
 ; light_ptr - ptr to the next light
 ; light_ptr_ptr - ptr to the next light ptr
 ; ---------------------------------------------------
 ; loop over the directional lights
 ;
 ; modifies: num_dir_lights
 ; ---------------------------------------------------
 ; prepare to loop over the point lights
 ;
 ; outputs: num_pt_lights
 ; light_ptr - ptr to the next light
 ; light_ptr_ptr - ptr to the next light ptr
 ; ---------------------------------------------------
 ; loop over the point lights
 ;
 ; modifies: num_pt_lights
 ; ---------------------------------------------------
 ; increment the next light pointer
 ;
 ; modifies: light_ptr, light_ptr_ptr
 ;
 ;
 ; loading
 ;
 ;
 ; ---------------------------------------------------
 ; load macros - pretty self-explanatory
 ;
 ; for colors, the value "max" is either 128.0 or 255.0 because
 ; of the way textures are handled (when texturing is enabled
 ; and modulated by the vertex color, 128.0 is unity and 255.0 is
 ; around 2.0).
 ; vertex-to-viewpoint vector
 ; global ambient color (rgb, 0.0-1.0)
 ; per-vertex color
 ; material colors
 ; material ambient (rgb, 0.0-1.0)
 ; material diffuse (rgb, 0.0-1.0)
 ; material specular (rgb, 0.0-1.0)
 ; material emmisive (rgb, 0.0-max)
 ; light attributes
 ; light ambient (rgb, 0.0-max)
 ; light diffuse (rgb, 0.0-max)
 ; light specular (rgb, 0.0-max)
 ; light position (xyz)
 ; light attenuation coefficients (constant, linear, quadratic)
 ; load transpose of object to world transform
 ; load the world to object space transform
 ;
 ;
 ; clipping
 ;
 ;
 ; ---------------------------------------------------
 ; initialize various clipping constants and clip_mask
 ;
 ; outputs: clip_scales
 ; modifies: i
 ; ---------------------------------------------------
 ; do a clipping test on vertex.
 ;
 ; params: vert - the vertex
 ; outputs: vi01 - contains clip result (1 or 0)
 ;
 ;
 ; face culling
 ;
 ;
 ; ---------------------------------------------------
 ; init backface culling.
 ;
 ; outputs: z_sign_mask
 ; bfc_multiplier - 1.0 to cull back-facing polys, else -1.0
 ; ---------------------------------------------------
 ; backface cull a triangle
 ;
 ; inputs: bfc_multiplier, z_sign_mask
 ; params: xformed_vert_? - the transformed vertices of the triangle
 ; outputs: z_sign - the sign of the tri normal's z component
 ; ---------------------------------------------------
 ; init backface culling for strips (where each loop iteration relies on the
 ; previous iteration).
 ;
 ; outputs: old_delta - really only assigns these two values
 ; old_vert to make vcl happy
 ; z_sign_switch - a switch to flip the sense of the bfc test
 ; for each tri in a strip
 ; <"init_bfc" called>
 ; ---------------------------------------------------
 ; backface cull a strip vertex. This is different from a triangle
 ; because the backfacing direction flips at each vertex, and
 ; because one vertex is processed per loop iteration.
 ;
 ; inputs: old_vert, old_delta, bfc_multiplier,
 ; z_sign_mask, z_sign_switch
 ; params: xformed_vert - the current transformed vertex..duh
 ; strip_adc - set by "set_strip_adcs" and loaded by "load_strip_adc"
 ; outputs: z_sign
 ;
 ;
 ; setting the adc bit
 ;
 ;
 ; ---------------------------------------------------
 ; Set the adc bits at the beginnings of strips in this buffer.
 ; Multiple strips can be packed in each vu memory buffer, so this
 ; routine is called to set the adc bits of vertices on strip boundaries
 ; so that the strips don't all run together.
 ;
 ; inputs: buffer_top, ((0 + 1) + 4), (0 + 1)
 ; internal; used below
 ; the real macro
 ; ---------------------------------------------------
 ; set the adc bit based on (f)rustum cull, (b)ackface cull,
 ; and (s)trip boundaries (which implies that this macro is intended
 ; for strips).
 ;
 ; inputs: vi01 - clip results
 ; z_sign - sign of face normalz
 ; params: vert - the vertex in question
 ; strip_adc - set by "set_strip_adcs" and loaded by "load_strip_adc"
 ; ---------------------------------------------------
 ; set the adc bit based on (f)rustum cull and (s)trip boundaries.
 ;
 ; inputs: vi01 - clip results
 ; params: vert - the vertex in question
 ; strip_adc - set by "set_strip_adcs" and loaded by "load_strip_adc"
 ; ---------------------------------------------------
 ; set the adc bit based on (f)rustum and (b)ackface cull.
 ;
 ; inputs: vi01 - clip results
 ; z_sign - sign of face normalz
 ; params: vert - the vertex in question
 ; ---------------------------------------------------
 ; set the adc bit based on (s)trip boundaries.
 ;
 ; params: vert - the vertex in question
 ; strip_adc - set by "set_strip_adcs" and loaded by "load_strip_adc"
 ; ---------------------------------------------------
 ; initializes some constants for transforming vertices and
 ; clamping colors (FIXME)
 ; ---------------------------------------------------
 ; load/store
 ; These next macros load and store the next normal, tex coord, etc.
 ; and are pretty self explanatory.
 ;
 ; params: offset - the offset in quads to add to the
 ; pointer before loading. Default is 0.
 ; ---------------------------------------------------
 ; loads the vertex transform from memory
 ;
 ; params: xfrm - the base name of the matrix (will be xfrm[0-3])
 ; ---------------------------------------------------
 ; load the adc value that was set by "set_strip_adcs"
 ;
 ; params strip_adc - the name of the register to assign the value to
 ;
 ;
 ; transforming
 ;
 ;
 ; ---------------------------------------------------
 ; apply the given perspective transform to the given vertex.
 ;
 ; params: xformed_vert - where to put result
 ; vert_xform - name of xform, where columns are vert_xform[0-3]
 ; vert - the vertex to transform (NOTE: w field is forced to 1.0)
 ; modifies: q
 ; ---------------------------------------------------
 ; convert a transformed vertex to gs coords and format (add offset
 ; and convert to fixed-point).
 ;
 ; inputs: gs_offsets[xyz] - translation to gs coord space
 ; params: gs_vert - the result
 ; vert - the vertex to convert
 ; ---------------------------------------------------
 ; Do the perspective multiply on the texture coordinates for perspective-
 ; correct textures.
 ;
 ; params: output - the result
 ; tex_stq - the texture stq. q (the z field) must be 1.0.
 ; q - the recip of the w field of the transformed vertex
 ; (q in the case of "xform_vert")
 ; ---------------------------------------------------
 ; initialize the output buffer (where the packet is written).
 ; Needs to be called immediately after init_io or init_o
 ; ---------------------------------------------------
 ; internal - used by init_io_loop and init_o_loop
 ; calculates the offset of the last vertex in a buffer
 ;
 ; inputs: num_verts
 ; params: last - the return value is written here
 ; first - pointer to first vert in buffer
 ; num_quads - number of quads per vertex
 ; ---------------------------------------------------
 ; outputs: num_verts - the total number of vertices
 ; ---------------------------------------------------
 ; get ready to loop over inputs and write to output buffer.
 ;
 ; params: input_addr - a *constant* address in vu mem where
 ; input geom begins. default is ((0 + 1) + 4)
 ; output_addr - a *constant* address in vu mem where
 ; output geom begins. default is (0 + (((((((((((((((((((((((((((((0) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 1) + 1) + 1) + 1) + 1) + 4) + 1) + 4) + 4) + 1) - 0 + 1))+1
 ; outputs: buffer_top - the starting offset of this buffer
 ; num_verts - the total number of vertices
 ; next_input
 ; next_output
 ; last_input - a pointer to the last vertex
 ; ---------------------------------------------------
 ; get ready to loop over inputs only.
 ;
 ; inputs:
 ; outputs: (same as above) buffer_top, next_input, last_input
 ; ---------------------------------------------------
 ; get ready to loop over outputs only.
 ;
 ; inputs: num_verts
 ; outputs: (same as above) buffer_top, next_output, last_output
 ; ---------------------------------------------------
 ; increments the current input pointer
 ;
 ; params: num - the number of vertices to increment (**not qwords**)
 ; default is 1.
 ; ---------------------------------------------------
 ; increments the current output pointer
 ;
 ; params: num - the number of vertices to increment (**not qwords**)
 ; default is 1.
 ; ---------------------------------------------------
 ; increments both input and output pointers
 ;
 ; params: num - the number of vertices to increment (**not qwords**)
 ; default is 1.
 ; ---------------------------------------------------
 ; do next iteration of loop
 ;
 ; inputs: next_output, last_output
 ; params: loop_target - the branch target
 ; ---------------------------------------------------
 ; kicks the output packet to the gs
 ;
 ; inputs: buffer_top, (0 + (((((((((((((((((((((((((((((0) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 1) + 1) + 1) + 1) + 1) + 4) + 1) + 4) + 4) + 1) - 0 + 1))
 ; ---------------------------------------------------
 ; This is here to avoid code duplication.
 ; Does vertex xform, constant color store, and tex coords
 ; for use in the backface-culled quad and triangle renderers
 ; where several vertices are processed per loop iteration.
 .init_vf_all
 .init_vi_all
 .name vsmIndexed
 --enter
 --endenter
 ; ------------------------ initialization ---------------------------------
 lq vert_xform_0, (((((((((((((((((((((((0) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 1) + 1) + 1) + 1) + 1)+0(vi00)
 lq vert_xform_1, (((((((((((((((((((((((0) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 1) + 1) + 1) + 1) + 1)+1(vi00)
 lq vert_xform_2, (((((((((((((((((((((((0) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 1) + 1) + 1) + 1) + 1)+2(vi00)
 lq vert_xform_3, (((((((((((((((((((((((0) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 1) + 1) + 1) + 1) + 1)+3(vi00)
 --cont
main_loop_lid:
 ; -------------------- lighting -------------------------------
lighting_lid:
 lq.xyz material_amb, ((((((((((((((((((((0) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 1) + 1)(vi00)
 lq.xyz material_diff, (((((((((((((((((((((0) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 1) + 1) + 1)(vi00)
 lq.xyz material_spec, ((((((((((((((((((((((0) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 1) + 1) + 1) + 1)(vi00)
 lq.xyz vert_to_eye, ((((((((((((((((((((((((0) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 1) + 1) + 1) + 1) + 1) + 4)(vi00)
 ; ---------- directional lights -----------------
 ; get number of dir lights
 ilw.x num_dir_lights, (0)(vi00)
 iaddiu light_ptr_ptr, vi00, ((0) + 1)
 ilw.x light_ptr, 0(light_ptr_ptr)
 ibeq num_dir_lights, vi00, pt_lights_lid
 maxw ones, vf00, vf00
dir_light_loop_lid:
 ; input/output ptrs and strides (increments to next vertex data)
 xtop buffer_top
 iaddiu next_input, buffer_top, ((0 + 1) + 4)
 ilw.x num_verts, 0(buffer_top)
 ; when to stop
 iadd last_input, next_input, num_verts
 iadd last_input, last_input, num_verts
 iadd last_input, last_input, num_verts
 ; color accumulation buffer
 iaddiu next_color_acc, buffer_top, (((0 + 1) + 4) + ((1024 - (0 + (((((((((((((((((((((((((((((0) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 1) + 1) + 1) + 1) + 1) + 4) + 1) + 4) + 4) + 1) - 0 + 1))) * 3 / 17))
 lq.xyz light_amb, 0(light_ptr)
 lq.xyz light_diff, 1(light_ptr)
 lq.xyz light_spec, 2(light_ptr)
 mul.xyz local_spec, light_spec, material_spec
 ; transform light direction into object space
 lq.xyz vert_to_light, 3(light_ptr)
 lq.xyz obj_to_world_transpose_0, (((((((((((((((((((((((((0) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 1) + 1) + 1) + 1) + 1) + 4) + 1)+0(vi00)
 lq.xyz obj_to_world_transpose_1, (((((((((((((((((((((((((0) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 1) + 1) + 1) + 1) + 1) + 4) + 1)+1(vi00)
 lq.xyz obj_to_world_transpose_2, (((((((((((((((((((((((((0) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 1) + 1) + 1) + 1) + 1) + 4) + 1)+2(vi00)
 mulax.xyz acc, obj_to_world_transpose_0, vert_to_light
 madday.xyz acc, obj_to_world_transpose_1, vert_to_light
 maddz.xyz vert_to_light, obj_to_world_transpose_2, vert_to_light
 ; in non-local viewer mode for infinite lights, the half-angle vec is fixed
 add.xyz half_angle, vert_to_eye, vert_to_light
 esadd p, half_angle
 mfp.w half_angle, p
 ersqrt p, half_anglew
 mfp.w half_angle, p
 mulw.xyz half_angle, half_angle, half_angle
dir_light_vert_loop_lid: --LoopCS 1,3
 lq.xyz normal, 1+0(next_input)
 ; dot normal with light direction
 mul.xyz intensity18, vert_to_light, normal
 adday.z acc, intensity18, intensity18
 maddx.z intensity18, ones, intensity18
 ; clamp intens >= 0.0 (don't let light be sucked away...)
 maxx.z intensity18, intensity18, vf00
 ; modulate the light diffuse color by the intensity
 mulz.xyz local_diff18, light_diff, intensity18
 ; modulate local diffuse light by material diffuse
 mula.xyz acc, local_diff18, material_diff
 mul.xyz temp21, half_angle, normal
 mr32.xyw temp21, temp21
 addax.w acc, temp21, temp21
 maddy.w intensity20, vf00, temp21
 maxx.w intensity20, intensity20, vf00
 mul.w intensity20, intensity20, intensity20
 mul.w intensity20, intensity20, intensity20
 mul.w intensity20, intensity20, intensity20
 mul.w intensity20, intensity20, intensity20
 mul.w intensity20, intensity20, intensity20
 maddaw.xyz acc, local_spec, intensity20
 madd.xyz vert_color, light_amb, material_amb
 ; add to previous lighting calculations (other lights, global amb + emission)
 lq.xyz total_rgb23, 0(next_color_acc)
 add.xyz vert_color, total_rgb23, vert_color
 sqi.xyz vert_color, (next_color_acc++)
 iaddiu next_input, next_input, 3
 ibne next_input, last_input, dir_light_vert_loop_lid
 ; loop over lights
 iaddiu light_ptr_ptr, light_ptr_ptr, 1
 ilw.x light_ptr, 0(light_ptr_ptr)
 isubiu num_dir_lights, num_dir_lights, 1
 ibne num_dir_lights, vi00, dir_light_loop_lid
 ; ---------- point lights -----------------
pt_lights_lid:
 ; get number of point lights
 ilw.y num_pt_lights, (0)(vi00)
 iaddiu light_ptr_ptr, vi00, ((0) + 1)
 ilw.y light_ptr, 0(light_ptr_ptr)
 ibeq num_pt_lights, vi00, done_lighting_lid
 maxw ones, vf00, vf00
pt_light_loop_lid:
 ; input/output ptrs and strides (increments to next vertex data)
 xtop buffer_top
 iaddiu next_input, buffer_top, ((0 + 1) + 4)
 ilw.x num_verts, 0(buffer_top)
 ; when to stop
 iadd last_input, next_input, num_verts
 iadd last_input, last_input, num_verts
 iadd last_input, last_input, num_verts
 ; color accumulation buffer
 iaddiu next_color_acc, buffer_top, (((0 + 1) + 4) + ((1024 - (0 + (((((((((((((((((((((((((((((0) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 1) + 1) + 1) + 1) + 1) + 4) + 1) + 4) + 4) + 1) - 0 + 1))) * 3 / 17))
 lq.xyz light_amb, 0(light_ptr)
 lq.xyz light_diff, 1(light_ptr)
 lq.xyz light_spec, 2(light_ptr)
 mul.xyz local_spec, light_spec, material_spec
 lq.xyz atten_coeff, 5(light_ptr)
 ; transform light position to object space
 lq.xyz light_pos, 3(light_ptr)
 lq.xyz world_to_obj_0, ((((((((((((((((((((((((((0) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 1) + 1) + 1) + 1) + 1) + 4) + 1) + 4)+0(vi00)
 lq.xyz world_to_obj_1, ((((((((((((((((((((((((((0) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 1) + 1) + 1) + 1) + 1) + 4) + 1) + 4)+1(vi00)
 lq.xyz world_to_obj_2, ((((((((((((((((((((((((((0) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 1) + 1) + 1) + 1) + 1) + 4) + 1) + 4)+2(vi00)
 lq.xyz world_to_obj_3, ((((((((((((((((((((((((((0) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 1) + 1) + 1) + 1) + 1) + 4) + 1) + 4)+3(vi00)
 mulax.xyz acc, world_to_obj_0, light_pos
 madday.xyz acc, world_to_obj_1, light_pos
 maddaz.xyz acc, world_to_obj_2, light_pos
 maddw.xyz light_pos, world_to_obj_3, vf00
pt_light_vert_loop_lid:
 --LoopCS 1,3
 --LoopExtra 5
 lq.xyz normal, 1+0(next_input)
 lq.xyz vert, 0(next_input)
 ; get unit vector from vert to light
 sub.xyz vert_to_light, light_pos, vert
 ; normalize and cache distances for attenuation factor
 mul.xyz atten, vert_to_light, vert_to_light
 adday.z acc, atten, atten
 maddx.z atten, ones, atten
 sqrt q, attenz
 addw.x atten, vf00, vf00
 addq.y atten, vf00, q
 div q, vf00w, atteny
 mulq.xyz vert_to_light, vert_to_light, q
 mul.xyz temp44, atten, atten_coeff
 mulax.w acc, vf00, temp44
 madday.w acc, vf00, temp44
 maddz.w atten, vf00, temp44
 ; dot normal with light direction
 mul.xyz temp46, vert_to_light, normal
 mulax.w acc, vf00, temp46
 madday.w acc, vf00, temp46
 maddz.w intensity45, vf00, temp46
 ; clamp intens >= 0.0 (don't let light be sucked away...)
 maxx.w intensity45, intensity45, vf00
 ; modulate the light diffuse color by the intensity
 mulw.xyz local_diff45, light_diff, intensity45
 ; modulate local diffuse light by material diffuse
 mula.xyz acc, local_diff45, material_diff
 add.xyz half_angle, vert_to_eye, vert_to_light
 esadd p, half_angle
 mfp.w half_angle, p
 ersqrt p, half_anglew
 mfp.w half_angle, p
 mulw.xyz half_angle, half_angle, half_angle
 mul.xyz temp49, half_angle, normal
 mulax.w acc, vf00, temp49
 madday.w acc, vf00, temp49
 maddz.w intensity48, vf00, temp49
 maxx.w intensity48, intensity48, vf00
 mul.w intensity48, intensity48, intensity48
 mul.w intensity48, intensity48, intensity48
 mul.w intensity48, intensity48, intensity48
 mul.w intensity48, intensity48, intensity48
 mul.w intensity48, intensity48, intensity48
 maddaw.xyz acc, local_spec, intensity48
 madd.xyz vert_color, light_amb, material_amb
 div q, vf00w, attenw
 mulq.xyz vert_color, vert_color, q
 lq.xyz total_rgb52, 0(next_color_acc)
 add.xyz vert_color, total_rgb52, vert_color
 sqi.xyz vert_color, (next_color_acc++)
 iaddiu next_input, next_input, 3
 ibne next_input, last_input, pt_light_vert_loop_lid
 iaddiu light_ptr_ptr, light_ptr_ptr, 1
 ilw.y light_ptr, 0(light_ptr_ptr)
 isubiu num_pt_lights, num_pt_lights, 1
 ibne num_pt_lights, vi00, pt_light_loop_lid
done_lighting_lid:
 ; -------------------- transform & texture loop ---------------------------
 ; gs offsets to center xformed vertex in gs coord space, also color clamping constant
 loi 2047.5
 addi.xy gs_offsets, vf00, i
 lq.w temp, ((((((((((((((((((0) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 6)(vi00)
 mr32.z gs_offsets, temp
 loi 12583167.0 ; clamp colors to 255 (255 + 12582912)
 maxi.w gs_offsets, vf00, i
 fcset 0x000
 ; clip scales and value to clip against (2048 - arbitrary)
 lq.xyz clip_scales, ((((((((((((((((((((((((((((0) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 1) + 1) + 1) + 1) + 1) + 4) + 1) + 4) + 4) + 1)(vi00)
 loi 2048.0
 maxi.w clip_scales, vf00, i
 ilw.w do_clipping, ((((((((((((((((((((((((((((0) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 1) + 1) + 1) + 1) + 1) + 4) + 1) + 4) + 4) + 1)(vi00)
 ; init_io_loop ((0 + 1) + 4), (((0 + 1) + 4) + ((1024 - (0 + (((((((((((((((((((((((((((((0) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 1) + 1) + 1) + 1) + 1) + 4) + 1) + 4) + 4) + 1) - 0 + 1))) * 3 / 17))
 xtop buffer_top
 iaddiu next_output, vi00, ((0 + (((((((((((((((((((((((((((((0) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 1) + 1) + 1) + 1) + 1) + 4) + 1) + 4) + 4) + 1) - 0 + 1)) + 1)
 iaddiu input_start, buffer_top, ((0 + 1) + 4)
 iaddiu color_start, buffer_top, (((0 + 1) + 4) + ((1024 - (0 + (((((((((((((((((((((((((((((0) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 1) + 1) + 1) + 1) + 1) + 4) + 1) + 4) + 4) + 1) - 0 + 1))) * 3 / 17))
 ; set up index-decompression
 iaddiu next_index, buffer_top, ((0 + 1) + 4)
 iaddiu first_index_mask, vi00, 0xff
 ; constant to shift right
 loi 253.0 ; = 2^8 - 3 = shift fraction part right 8 bits
 maxi.w index_constants, vf00, i
 loi 3.0
 maxi.z index_constants, vf00, i
 loi 255.0
 maxi.y index_constants, vf00, i
 ; init_out_buf
 ; get num indices (stored as num/2)
 ilw.y num_indices_d2, 0(buffer_top)
 ; 2 indices per w-field
 iadd last_index, next_index, num_indices_d2
 ; get real # of indices
 ilw.z num_indices, 0(buffer_top)
 ; set nloop and copy giftag
 lq gif_tag, (((((((((((((((((((((((((((0) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 1) + 1) + 1) + 1) + 1) + 4) + 1) + 4) + 4)(vi00)
 mtir eop, gif_tagx
 ior eop, eop, num_indices
 mfir.x gif_tag, eop
 mfir.w gif_tag, next_output
 sq gif_tag, (0 + (((((((((((((((((((((((((((((0) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 1) + 1) + 1) + 1) + 1) + 4) + 1) + 4) + 4) + 1) - 0 + 1))(vi00)
 ; for the final color conversion
 ; the alpha value of a vert is the mat diffuse alpha
 loi 128.0
 lq.w vert_color, (((((((((((((((((((((0) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 1) + 1) + 1)(vi00)
 muli.w vert_color, vert_color, i
 loi 255.0
 minii.w vert_color, vert_color, i
 ftoi0.w vert_color, vert_color
 maxi.w max_color_val, vf00, i
 ; init_bfc_strip
 ; wait for any other buffers to finish..
 iaddiu zero_giftag, vi00, (((((((((((((((((((((((((((0) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 1) + 1) + 1) + 1) + 1) + 4) + 1) + 4) + 4)
 xgkick zero_giftag
xform_loop_lid: --LoopCS 1,3
 ; get first index
 ilw.w first_index, 0(next_index)
 iand first_index, first_index, first_index_mask
 iadd first_offset, first_index, first_index
 iadd first_offset, first_offset, first_index
 ; get second index
 ; the first and second indices are in the form 0x3f80ssff
 lqi.w indices, (next_index++)
 addy.w second_ind, indices, index_constantsy
 mtir second_index, second_indw
 mulz.w second_off, indices, index_constantsz
 add.w second_off, second_off, index_constantsw
 mtir second_offset, second_offw
 ; ---------- first vert -----------------
 iadd next_input, first_offset, input_start
 iadd next_color, first_index, color_start
 ; xform/clip vertex
 lq.xyz vert, 0(next_input)
 mulax acc, vert_xform_0, vert
 madday acc, vert_xform_1, vert
 maddaz acc, vert_xform_2, vert
 maddw xformed_vert, vert_xform_3, vf00
 div q, vf00w, xformed_vertw
 mulq.xyz xformed_vert, xformed_vert, q
 ; FIXME: visible vertices are now in range (+-320, +-112, +-2^24-1)
 ; add screen offsets to xformed vertex
 add.xyz gs_vert, xformed_vert, gs_offsets
 ; convert to 4-bit fixed-point
 ftoi4.xyz gs_vert, gs_vert
 ; load_strip_adc strip_adc
 ; bfc_strip_vert xformed_vert, vi00
 mul.xyz clip_vert65, xformed_vert, clip_scales
 clipw.xyz clip_vert65, clip_scalesw
 fcand vi01, 0x003ffff
 iand vi01, vi01, do_clipping
 ; set_adc_fbs gs_vert, vi00
 ; clip and face culling flags
 ior new_adc66, vi01, vi00
 iaddiu new_adc66, new_adc66, 0x7fff
 mfir.w gs_vert, new_adc66
 sq gs_vert, 2+0(next_output)
 ; color
 lq.xyz vert_color, (next_color)
 miniw.xyz vert_color, vert_color, max_color_valw
 ftoi0.xyz vert_color, vert_color
 sq vert_color, 1+0(next_output)
 ; texture coords
 lq.xyz tex_stq, 2+0(next_input)
 ; normalize stq
 mulq.xyz tex_stq, tex_stq, q
 sq.xyz tex_stq, 0+0(next_output)
 ; ---------- second vert -----------------
 iadd next_input, second_offset, input_start
 iadd next_color, second_index, color_start
 iaddiu next_output, next_output, 3
 ; xform/clip vertex
 lq.xyz vert, 0(next_input)
 mulax acc, vert_xform_0, vert
 madday acc, vert_xform_1, vert
 maddaz acc, vert_xform_2, vert
 maddw xformed_vert, vert_xform_3, vf00
 div q, vf00w, xformed_vertw
 mulq.xyz xformed_vert, xformed_vert, q
 ; FIXME: visible vertices are now in range (+-320, +-112, +-2^24-1)
 ; add screen offsets to xformed vertex
 add.xyz gs_vert, xformed_vert, gs_offsets
 ; convert to 4-bit fixed-point
 ftoi4.xyz gs_vert, gs_vert
 ; load_strip_adc strip_adc
 ; bfc_strip_vert xformed_vert, vi00
 mul.xyz clip_vert77, xformed_vert, clip_scales
 clipw.xyz clip_vert77, clip_scalesw
 fcand vi01, 0x003ffff
 iand vi01, vi01, do_clipping
 ; set_adc_fbs gs_vert, vi00
 ; clip and face culling flags
 ior new_adc78, vi01, vi00
 iaddiu new_adc78, new_adc78, 0x7fff
 mfir.w gs_vert, new_adc78
 sq gs_vert, 2+0(next_output)
 ; color
 lq.xyz vert_color, (next_color)
 miniw.xyz vert_color, vert_color, max_color_valw
 ftoi0.xyz vert_color, vert_color
 sq vert_color, 1+0(next_output)
 ; texture coords
 lq.xyz tex_stq, 2+0(next_input)
 ; normalize stq
 mulq.xyz tex_stq, tex_stq, q
 sq.xyz tex_stq, 0+0(next_output)
 ; --------- end loop ---------------
 iaddiu next_output, next_output, 3
 ibne next_index, last_index, xform_loop_lid
 ; ---------------- kick packet to GS -----------------------
 iaddiu packet_start85, vi00, (0 + (((((((((((((((((((((((((((((0) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 1) + 1) + 1) + 1) + 1) + 4) + 1) + 4) + 4) + 1) - 0 + 1))
 xgkick packet_start85
 --cont
 b main_loop_lid
