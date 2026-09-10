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
 .init_vf_all
 .init_vi_all
 .name vsmFast
 --enter
 --endenter
 ; ------------------------ initialization ---------------------------------
 ; there should be from 0 to 3 directional lights, no other types
 lq vert_xform_0, (((((((((((((((((((((((0) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 1) + 1) + 1) + 1) + 1)+0(vi00)
 lq vert_xform_1, (((((((((((((((((((((((0) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 1) + 1) + 1) + 1) + 1)+1(vi00)
 lq vert_xform_2, (((((((((((((((((((((((0) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 1) + 1) + 1) + 1) + 1)+2(vi00)
 lq vert_xform_3, (((((((((((((((((((((((0) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 1) + 1) + 1) + 1) + 1)+3(vi00)
 ; build a 3x3 matrix with light directions as the rows and a 3x3 matrix with
 ; the 3 cols the light diffuse colors
 ; find the constant color terms (sum of emissive and ambient terms)
 lq.xyz constant_color, (((((((((((((((((((0) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 1)(vi00)
 ; initialize the direction and diffuse color matrices
 move.xyz light_dirs_0, vf00
 move.xyz light_dirs_1, vf00
 move.xyz light_dirs_2, vf00
 move.xyz light_colors_0, vf00
 move.xyz light_colors_1, vf00
 move.xyz light_colors_2, vf00
 ; get number of dir lights
 ilw.x num_dir_lights, (0)(vi00)
 iaddiu light_ptr_ptr, vi00, ((0) + 1)
 ilw.x light_ptr, 0(light_ptr_ptr)
 lq.xyz material_amb, ((((((((((((((((((((0) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 1) + 1)(vi00)
 ibeq num_dir_lights, vi00, finish_init_lid
 lq.xyz material_diff, (((((((((((((((((((((0) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 1) + 1) + 1)(vi00)
 lq.xyz obj_to_world_transpose_0, (((((((((((((((((((((((((0) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 1) + 1) + 1) + 1) + 1) + 4) + 1)+0(vi00)
 lq.xyz obj_to_world_transpose_1, (((((((((((((((((((((((((0) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 1) + 1) + 1) + 1) + 1) + 4) + 1)+1(vi00)
 lq.xyz obj_to_world_transpose_2, (((((((((((((((((((((((((0) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 1) + 1) + 1) + 1) + 1) + 4) + 1)+2(vi00)
 lq.xyz light_dirs_0, 3(light_ptr)
 mulax.xyz acc, obj_to_world_transpose_0, light_dirs_0
 madday.xyz acc, obj_to_world_transpose_1, light_dirs_0
 maddz.xyz light_dirs_0, obj_to_world_transpose_2, light_dirs_0
 lq.xyz light_colors_0, 1(light_ptr)
 mul.xyz light_colors_0, light_colors_0, material_diff
 lq.xyz light_amb, 0(light_ptr)
 mul.xyz local_amb, material_amb, light_amb
 add.xyz constant_color, constant_color, local_amb
 isubiu num_dir_lights, num_dir_lights, 1
 ibeq num_dir_lights, vi00, finish_init_lid
 iaddiu light_ptr_ptr, light_ptr_ptr, 1
 ilw.x light_ptr, 0(light_ptr_ptr)
 lq.xyz light_dirs_1, 3(light_ptr)
 mulax.xyz acc, obj_to_world_transpose_0, light_dirs_1
 madday.xyz acc, obj_to_world_transpose_1, light_dirs_1
 maddz.xyz light_dirs_1, obj_to_world_transpose_2, light_dirs_1
 lq.xyz light_colors_1, 1(light_ptr)
 mul.xyz light_colors_1, light_colors_1, material_diff
 lq.xyz light_amb, 0(light_ptr)
 mul.xyz local_amb, material_amb, light_amb
 add.xyz constant_color, constant_color, local_amb
 isubiu num_dir_lights, num_dir_lights, 1
 ibeq num_dir_lights, vi00, finish_init_lid
 iaddiu light_ptr_ptr, light_ptr_ptr, 1
 ilw.x light_ptr, 0(light_ptr_ptr)
 lq.xyz light_dirs_2, 3(light_ptr)
 mulax.xyz acc, obj_to_world_transpose_0, light_dirs_2
 madday.xyz acc, obj_to_world_transpose_1, light_dirs_2
 maddz.xyz light_dirs_2, obj_to_world_transpose_2, light_dirs_2
 lq.xyz light_colors_2, 1(light_ptr)
 mul.xyz light_colors_2, light_colors_2, material_diff
 lq.xyz light_amb, 0(light_ptr)
 mul.xyz local_amb, material_amb, light_amb
 add.xyz constant_color, constant_color, local_amb
finish_init_lid:
 lq.xyz global_amb, ((((((((((((((((((0) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 6)(vi00)
 mul.xyz local_amb, material_amb, global_amb
 add.xyz constant_color, constant_color, local_amb
 addy.x temp_121, vf00, light_dirs_0
 addx.y light_dirs_0, vf00, light_dirs_1
 add.x light_dirs_1, vf00, temp_121
 addz.x temp_221, vf00, light_dirs_0
 addx.z light_dirs_0, vf00, light_dirs_2
 add.x light_dirs_2, vf00, temp_221
 mr32.x temp121, light_dirs_2
 mr32.w temp121, temp121
 mr32.y light_dirs_2, light_dirs_1
 mr32.z light_dirs_1, temp121
 ; the alpha of a vert is the material diffuse alpha
 lq.w material_diff, (((((((((((((((((((((0) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 1) + 1) + 1)(vi00)
 loi 128.0
 muli.w color, material_diff, i
 loi 255.0
 minii.w color, color, i
 ; use constant_color add to do an ftoi0 on the color
 loi 12582912.0
 addi.xyz constant_color, constant_color, i
 ; convert alpha to 0-bit fixed point
 addi.w color, color, i
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
 ; make translation matrix
 sub trans_0, vf00, vf00
 sub trans_1, vf00, vf00
 sub trans_2, vf00, vf00
 maxw.x trans_0, trans_0, vf00
 maxw.y trans_1, trans_1, vf00
 maxw.z trans_2, trans_2, vf00
 move.xyz trans_3, gs_offsets
 move.w trans_3, vf00
 ; new xform
 mulax acc, trans_0, vert_xform_0
 madday acc, trans_1, vert_xform_0
 maddaz acc, trans_2, vert_xform_0
 maddw new_xform_0, trans_3, vert_xform_0
 mulax acc, trans_0, vert_xform_1
 madday acc, trans_1, vert_xform_1
 maddaz acc, trans_2, vert_xform_1
 maddw new_xform_1, trans_3, vert_xform_1
 mulax acc, trans_0, vert_xform_2
 madday acc, trans_1, vert_xform_2
 maddaz acc, trans_2, vert_xform_2
 maddw new_xform_2, trans_3, vert_xform_2
 mulax acc, trans_0, vert_xform_3
 madday acc, trans_1, vert_xform_3
 maddaz acc, trans_2, vert_xform_3
 maddw new_xform_3, trans_3, vert_xform_3
 --cont
 ; -------------------- transform & texture loop ---------------------------
main_loop_lid:
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
 lq gif_tag32, (((((((((((((((((((((((((((0) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 1) + 1) + 1) + 1) + 1) + 4) + 1) + 4) + 4)(vi00)
 mtir eop32, gif_tag32x
 ior eop32, eop32, num_verts
 mfir.x gif_tag32, eop32
 sq gif_tag32, -1(next_output)
 iaddiu firstVert, buffer_top, ((0 + 1) + 4)
 iaddiu adcsPtr, buffer_top, (0 + 1)
 iaddiu lastAdcsPtr, adcsPtr, 4
 ; see CGeneralRenderer::XferBufferHeader for format
 iaddiu offsetMask, vi00, 0x3ff
 iaddiu second_adc_mask, vi00, 0x800
 iaddiu stop_bit_mask, vi00, 0x400
 iaddiu first_adc, vi00, 0x20
 adcLoop_lid:
 lq strip_boundaries, 0(adcsPtr)
 ; get the offsets
 ftoi0 strip_boundaries, strip_boundaries
 mtir offset, strip_boundariesx
 iand stop_bit, offset, stop_bit_mask
 ibeq stop_bit, stop_bit_mask, adcLoop_done_lid
 iand second_adc, offset, second_adc_mask
 iand offset, offset, offsetMask
 iadd offset, offset, firstVert
 isw.w first_adc, 0(offset)
 isw.w second_adc, 3(offset)
 mtir offset, strip_boundariesy
 iand stop_bit, offset, stop_bit_mask
 ibeq stop_bit, stop_bit_mask, adcLoop_done_lid
 iand second_adc, offset, second_adc_mask
 iand offset, offset, offsetMask
 iadd offset, offset, firstVert
 isw.w first_adc, 0(offset)
 isw.w second_adc, 3(offset)
 mtir offset, strip_boundariesz
 iand stop_bit, offset, stop_bit_mask
 ibeq stop_bit, stop_bit_mask, adcLoop_done_lid
 iand second_adc, offset, second_adc_mask
 iand offset, offset, offsetMask
 iadd offset, offset, firstVert
 isw.w first_adc, 0(offset)
 isw.w second_adc, 3(offset)
 mtir offset, strip_boundariesw
 iand stop_bit, offset, stop_bit_mask
 ibeq stop_bit, stop_bit_mask, adcLoop_done_lid
 iand second_adc, offset, second_adc_mask
 iand offset, offset, offsetMask
 iadd offset, offset, firstVert
 isw.w first_adc, 0(offset)
 isw.w second_adc, 3(offset)
 ; loop
 iaddiu adcsPtr, adcsPtr, 1
 ibne adcsPtr, lastAdcsPtr, adcLoop_lid
 adcLoop_done_lid:
xform_loop_lid:
 --LoopCS 1,3
 ; xform/clip vertex
 lq.xyz vert, 0(next_input)
 mulax acc, new_xform_0, vert
 madday acc, new_xform_1, vert
 maddaz acc, new_xform_2, vert
 maddw xformed_vert, new_xform_3, vf00
 div q, vf00w, xformed_vertw
 mulq.xyz xformed_vert, xformed_vert, q
 ; FIXME: visible vertices are now in range (+-320, +-112, +-2^24-1)
 ftoi4.xyz gs_vert, xformed_vert
 ilw.w strip_adc, 0(next_input)
 ; clip and face culling flags
 iaddiu new_adc42, strip_adc, 0x7fff
 mfir.w gs_vert, new_adc42
 sq gs_vert, 2+0(next_output)
 ; lighting
 lq.xyz normal, 1+0(next_input)
 mulax.xyz acc, light_dirs_0, normal
 madday.xyz acc, light_dirs_1, normal
 maddz.xyz cosines, light_dirs_2, normal
 max.xyz cosines, cosines, vf00
 mulax.xyz acc, light_colors_0, cosines
 madday.xyz acc, light_colors_1, cosines
 maddz.xyz color, light_colors_2, cosines
 add.xyz color, color, constant_color
 miniw.xyz color, color, gs_offsetsw
 sq color, 1+0(next_output)
 ; texture coords
 lq.xyz tex_stq, 2+0(next_input)
 ; normalize stq
 mulq.xyz tex_stq, tex_stq, q
 sq.xyz tex_stq, 0+0(next_output)
 iaddiu next_input, next_input, 3
 iaddiu next_output, next_output, 3
 ibne next_input, last_input, xform_loop_lid
 ; -------------------- done! -------------------------------
done_lid:
 ; ---------------- kick packet to GS -----------------------
 iaddiu packet_start55, buffer_top, (0 + (((1024 - (0 + (((((((((((((((((((((((((((((0) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 6) + 1) + 1) + 1) + 1) + 1) + 4) + 1) + 4) + 4) + 1) - 0 + 1))) / 2) / 2))
 xgkick packet_start55
 --cont
 b main_loop_lid
