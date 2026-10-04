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
 ; output geom begins. default is (0 + (((1024 - (0 + (((((((((((((((((((((((((((((0) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 1) + 1) + 1) + 1) + 1) + 4) + 1) + 4) + 4) + 1) - 0 + 1))) / 2) / 2))+1
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
 ; inputs: buffer_top, (0 + (((1024 - (0 + (((((((((((((((((((((((((((((0) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 1) + 1) + 1) + 1) + 1) + 4) + 1) + 4) + 4) + 1) - 0 + 1))) / 2) / 2))
 ; ---------------------------------------------------
 ; This is here to avoid code duplication.
 ; Does vertex xform, constant color store, and tex coords
 ; for use in the backface-culled quad and triangle renderers
 ; where several vertices are processed per loop iteration.
 .init_vf_all
 .init_vi_all
 .name vsmGeneralTri
 --enter
 --endenter
 ; ------------------------ initialization ---------------------------------
 lq vert_xform_0, (((((((((((((((((((((((0) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 1) + 1) + 1) + 1) + 1)+0(vi00)
 lq vert_xform_1, (((((((((((((((((((((((0) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 1) + 1) + 1) + 1) + 1)+1(vi00)
 lq vert_xform_2, (((((((((((((((((((((((0) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 1) + 1) + 1) + 1) + 1)+2(vi00)
 lq vert_xform_3, (((((((((((((((((((((((0) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 1) + 1) + 1) + 1) + 1)+3(vi00)
 --cont
 ; -------------------- transform & texture loop ---------------------------
main_loop_lid:
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
 ; input/output ptrs and strides (increments to next vertex data)
 xtop buffer_top
 iaddiu next_input, buffer_top, ((0 + 1) + 4)
 iaddiu next_output, buffer_top, (0 + (((1024 - (0 + (((((((((((((((((((((((((((((0) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 1) + 1) + 1) + 1) + 1) + 4) + 1) + 4) + 4) + 1) - 0 + 1))) / 2) / 2))+1
 ilw.x num_verts, 0(buffer_top)
 ; when to stop
 iadd last_input, next_input, num_verts
 iadd last_input, last_input, num_verts
 iadd last_input, last_input, num_verts
 ; fill in the nloop field in the giftag and store at
 ; top of the output buffer
 lq gif_tag6, (((((((((((((((((((((((((((0) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 1) + 1) + 1) + 1) + 1) + 4) + 1) + 4) + 4)(vi00)
 mtir eop6, gif_tag6x
 ior eop6, eop6, num_verts
 mfir.x gif_tag6, eop6
 sq gif_tag6, -1(next_output)
 ilw.w z_sign_mask, (0)(vi00)
 lq.w bfc_multiplier, (0)(vi00)
 lq.xyz global_amb, ((((((((((((((((((0) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 6)(vi00)
 lq.xyz material_amb, ((((((((((((((((((((0) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 1) + 1)(vi00)
 mul.xyz global_amb, global_amb, material_amb
 lq.xyz material_emm, (((((((((((((((((((0) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 1)(vi00)
 add.xyz const_color, material_emm, global_amb
 iaddiu adc_bit, vi00, 0x7fff
 iaddiu adc_bit, adc_bit, 1
xform_loop_lid: --LoopCS 1,3
 ; the first two are never drawn (always have adc bit set)
 ; xform/clip vertex
 lq.xyz vert, 0(next_input)
 mulax acc, vert_xform_0, vert
 madday acc, vert_xform_1, vert
 maddaz acc, vert_xform_2, vert
 maddw xformed_vert_1, vert_xform_3, vf00
 div q, vf00w, xformed_vert_1w
 mulq.xyz xformed_vert_1, xformed_vert_1, q
 ; FIXME: visible vertices are now in range (+-320, +-112, +-2^24-1)
 ; add screen offsets to xformed vertex
 add.xyz gs_vert_1, xformed_vert_1, gs_offsets
 ; convert to 4-bit fixed-point
 ftoi4.xyz gs_vert_1, gs_vert_1
 ; constant color
 sq.xyz const_color, 1+0(next_output)
 ; texture coords
 lq.xyz tex_stq, 2+0(next_input)
 ; normalize stq
 mulq.xyz tex_stq, tex_stq, q
 sq.xyz tex_stq, 0+0(next_output)
 mul.xyz clip_vert18, xformed_vert_1, clip_scales
 clipw.xyz clip_vert18, clip_scalesw
 mfir.w gs_vert_1, adc_bit
 sq gs_vert_1, 2+0(next_output)
 ; xform/clip vertex
 lq.xyz vert, 3(next_input)
 mulax acc, vert_xform_0, vert
 madday acc, vert_xform_1, vert
 maddaz acc, vert_xform_2, vert
 maddw xformed_vert_2, vert_xform_3, vf00
 div q, vf00w, xformed_vert_2w
 mulq.xyz xformed_vert_2, xformed_vert_2, q
 ; FIXME: visible vertices are now in range (+-320, +-112, +-2^24-1)
 ; add screen offsets to xformed vertex
 add.xyz gs_vert_2, xformed_vert_2, gs_offsets
 ; convert to 4-bit fixed-point
 ftoi4.xyz gs_vert_2, gs_vert_2
 ; constant color
 sq.xyz const_color, 1+3(next_output)
 ; texture coords
 lq.xyz tex_stq, 2+3(next_input)
 ; normalize stq
 mulq.xyz tex_stq, tex_stq, q
 sq.xyz tex_stq, 0+3(next_output)
 mul.xyz clip_vert29, xformed_vert_2, clip_scales
 clipw.xyz clip_vert29, clip_scalesw
 mfir.w gs_vert_2, adc_bit
 sq gs_vert_2, 2+3(next_output)
 ; xform/clip vertex
 lq.xyz vert, 3+3(next_input)
 mulax acc, vert_xform_0, vert
 madday acc, vert_xform_1, vert
 maddaz acc, vert_xform_2, vert
 maddw xformed_vert_3, vert_xform_3, vf00
 div q, vf00w, xformed_vert_3w
 mulq.xyz xformed_vert_3, xformed_vert_3, q
 ; FIXME: visible vertices are now in range (+-320, +-112, +-2^24-1)
 ; add screen offsets to xformed vertex
 add.xyz gs_vert_3, xformed_vert_3, gs_offsets
 ; convert to 4-bit fixed-point
 ftoi4.xyz gs_vert_3, gs_vert_3
 ; constant color
 sq.xyz const_color, 1+3+3(next_output)
 ; texture coords
 lq.xyz tex_stq, 2+3+3(next_input)
 ; normalize stq
 mulq.xyz tex_stq, tex_stq, q
 sq.xyz tex_stq, 0+3+3(next_output)
 mul.xyz clip_vert40, xformed_vert_3, clip_scales
 clipw.xyz clip_vert40, clip_scalesw
 ; backface/frontface cull
 ; this screen triangle's normal
 sub.xyz delta_141, xformed_vert_1, xformed_vert_2
 sub.xyz delta_241, xformed_vert_3, xformed_vert_2
 ; bfc_multiplier is 1 to cull back-facing polys, -1 for front
 mulw.xyz delta_141, delta_141, bfc_multiplier
 opmula.xyz acc, delta_141, delta_241
 opmsub.xyz bfc_normal41, delta_241, delta_141
 ; get sign of normal
 fmand z_sign, z_sign_mask
 ; clip test last three vertices
 fcand vi01, 0x03ffff
 iand vi01, vi01, do_clipping
 ; draw this tri?
 ior new_adc_bit, vi01, z_sign
 iaddiu new_adc_bit, new_adc_bit, 0x7fff
 mfir.w gs_vert_3, new_adc_bit
 sq gs_vert_3, 2+3+3(next_output)
 iaddiu next_input, next_input, 3
 iaddiu next_input, next_input, 3
 iaddiu next_input, next_input, 3
 iaddiu next_output, next_output, 3
 iaddiu next_output, next_output, 3
 iaddiu next_output, next_output, 3
 ibne next_input, last_input, xform_loop_lid
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
 iaddiu next_output, buffer_top, (0 + (((1024 - (0 + (((((((((((((((((((((((((((((0) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 1) + 1) + 1) + 1) + 1) + 4) + 1) + 4) + 4) + 1) - 0 + 1))) / 2) / 2))+1
 ilw.x num_verts, 0(buffer_top)
 ; when to stop
 iadd last_input, next_input, num_verts
 iadd last_input, last_input, num_verts
 iadd last_input, last_input, num_verts
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
 mul.xyz intensity64, vert_to_light, normal
 adday.z acc, intensity64, intensity64
 maddx.z intensity64, ones, intensity64
 ; clamp intens >= 0.0 (don't let light be sucked away...)
 maxx.z intensity64, intensity64, vf00
 ; modulate the light diffuse color by the intensity
 mulz.xyz local_diff64, light_diff, intensity64
 ; modulate local diffuse light by material diffuse
 mula.xyz acc, local_diff64, material_diff
 mul.xyz temp67, half_angle, normal
 mr32.xyw temp67, temp67
 addax.w acc, temp67, temp67
 maddy.w intensity66, vf00, temp67
 maxx.w intensity66, intensity66, vf00
 mul.w intensity66, intensity66, intensity66
 mul.w intensity66, intensity66, intensity66
 mul.w intensity66, intensity66, intensity66
 mul.w intensity66, intensity66, intensity66
 mul.w intensity66, intensity66, intensity66
 maddaw.xyz acc, local_spec, intensity66
 madd.xyz vert_color, light_amb, material_amb
 ; add to previous lighting calculations (other lights, global amb + emission)
 lq.xyz total_rgb69, 1(next_output)
 add.xyz vert_color, total_rgb69, vert_color
 sq.xyz vert_color, 1+0(next_output)
 iaddiu next_input, next_input, 3
 iaddiu next_output, next_output, 3
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
 ibeq num_pt_lights, vi00, done_lid
 maxw ones, vf00, vf00
pt_light_loop_lid:
 ; input/output ptrs and strides (increments to next vertex data)
 xtop buffer_top
 iaddiu next_input, buffer_top, ((0 + 1) + 4)
 iaddiu next_output, buffer_top, (0 + (((1024 - (0 + (((((((((((((((((((((((((((((0) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 1) + 1) + 1) + 1) + 1) + 4) + 1) + 4) + 4) + 1) - 0 + 1))) / 2) / 2))+1
 ilw.x num_verts, 0(buffer_top)
 ; when to stop
 iadd last_input, next_input, num_verts
 iadd last_input, last_input, num_verts
 iadd last_input, last_input, num_verts
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
 mul.xyz temp93, atten, atten_coeff
 mulax.w acc, vf00, temp93
 madday.w acc, vf00, temp93
 maddz.w atten, vf00, temp93
 ; dot normal with light direction
 mul.xyz temp95, vert_to_light, normal
 mulax.w acc, vf00, temp95
 madday.w acc, vf00, temp95
 maddz.w intensity94, vf00, temp95
 ; clamp intens >= 0.0 (don't let light be sucked away...)
 maxx.w intensity94, intensity94, vf00
 ; modulate the light diffuse color by the intensity
 mulw.xyz local_diff94, light_diff, intensity94
 ; modulate local diffuse light by material diffuse
 mula.xyz acc, local_diff94, material_diff
 add.xyz half_angle, vert_to_eye, vert_to_light
 esadd p, half_angle
 mfp.w half_angle, p
 ersqrt p, half_anglew
 mfp.w half_angle, p
 mulw.xyz half_angle, half_angle, half_angle
 mul.xyz temp98, half_angle, normal
 mulax.w acc, vf00, temp98
 madday.w acc, vf00, temp98
 maddz.w intensity97, vf00, temp98
 maxx.w intensity97, intensity97, vf00
 mul.w intensity97, intensity97, intensity97
 mul.w intensity97, intensity97, intensity97
 mul.w intensity97, intensity97, intensity97
 mul.w intensity97, intensity97, intensity97
 mul.w intensity97, intensity97, intensity97
 maddaw.xyz acc, local_spec, intensity97
 madd.xyz vert_color, light_amb, material_amb
 div q, vf00w, attenw
 mulq.xyz vert_color, vert_color, q
 lq.xyz total_rgb101, 1(next_output)
 add.xyz vert_color, total_rgb101, vert_color
 sq.xyz vert_color, 1+0(next_output)
 iaddiu next_input, next_input, 3
 iaddiu next_output, next_output, 3
 ibne next_input, last_input, pt_light_vert_loop_lid
 iaddiu light_ptr_ptr, light_ptr_ptr, 1
 ilw.y light_ptr, 0(light_ptr_ptr)
 isubiu num_pt_lights, num_pt_lights, 1
 ibne num_pt_lights, vi00, pt_light_loop_lid
 ; -------------------- done! -------------------------------
done_lid:
 ; clamp and convert to fixed-point
 ; clamp and convert to fixed-point colors that have been accumulated to mem
 xtop buffer_top
 iaddiu next_output, buffer_top, (0 + (((1024 - (0 + (((((((((((((((((((((((((((((0) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 1) + 1) + 1) + 1) + 1) + 4) + 1) + 4) + 4) + 1) - 0 + 1))) / 2) / 2))+1
 ; when to stop
 iadd last_output, next_output, num_verts
 iadd last_output, last_output, num_verts
 iadd last_output, last_output, num_verts
 ; the alpha value of a vert is the mat diffuse alpha
 loi 128.0
 lq.w vert_color109, (((((((((((((((((((((0) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 1) + 1) + 1)(vi00)
 muli.w vert_color109, vert_color109, i
 loi 255.0
 minii.w vert_color109, vert_color109, i
 ftoi0.w vert_color109, vert_color109
 final_loop_lid:
 --LoopCS 1,3
 --LoopExtra 1
 lq.xyz vert_color109, 1+0(next_output)
 minii.xyz vert_color109, vert_color109, i
 ftoi0.xyz vert_color109, vert_color109
 sq vert_color109, 1+0(next_output)
 iaddiu next_output, next_output, 3
 ibne next_output, last_output, final_loop_lid
 ; ---------------- kick packet to GS -----------------------
 iaddiu packet_start117, buffer_top, (0 + (((1024 - (0 + (((((((((((((((((((((((((((((0) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 1) + 1) + 1) + 1) + 1) + 4) + 1) + 4) + 4) + 1) - 0 + 1))) / 2) / 2))
 xgkick packet_start117
 --cont
 b main_loop_lid
