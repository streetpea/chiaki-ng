; Vertex shader para el blit de video (blit de textura con matriz de
; proyeccion). Basado en textureShader.vsh del driver Wii U de SDL2
; (zlib license, Copyright (C) 2022 GaryOderNichts) — sin cambios
; respecto al original salvo este comentario.
; Se dibuja un QUADS fullscreen; la proyeccion es identidad porque los
; vertices ya van en coordenadas NDC (-1..1).

; $MODE = "UniformRegister"

; $SPI_VS_OUT_CONFIG.VS_EXPORT_COUNT = 1
; $NUM_SPI_VS_OUT_ID = 1
; v_color
; $SPI_VS_OUT_ID[0].SEMANTIC_0 = 0
; v_texCoord
; $SPI_VS_OUT_ID[0].SEMANTIC_1 = 1

; C0
; $UNIFORM_VARS[0].name = "u_projection"
; $UNIFORM_VARS[0].type = "mat4"
; $UNIFORM_VARS[0].count = 1
; $UNIFORM_VARS[0].block = -1
; $UNIFORM_VARS[0].offset = 0

; R1
; $ATTRIB_VARS[0].name = "a_position"
; $ATTRIB_VARS[0].type = "vec2"
; $ATTRIB_VARS[0].location = 0
; R2
; $ATTRIB_VARS[1].name = "a_color"
; $ATTRIB_VARS[1].type = "vec4"
; $ATTRIB_VARS[1].location = 1
; R3
; $ATTRIB_VARS[2].name = "a_texcoord"
; $ATTRIB_VARS[2].type = "vec2"
; $ATTRIB_VARS[2].location = 2

00 CALL_FS NO_BARRIER
01 ALU:
    0  x: MUL    ____,   1.0f, C3.x
       y: MUL    ____,   1.0f, C3.y
       z: MUL    ____,   1.0f, C3.z
       w: MUL    ____,   1.0f, C3.w
    1  x: MULADD R127.x, R1.y, C1.x, PV0.x
       y: MULADD R127.y, R1.y, C1.y, PV0.y
       z: MULADD R127.z, R1.y, C1.z, PV0.z
       w: MULADD R127.w, R1.y, C1.w, PV0.w
    2  x: MULADD R1.x,   R1.x, C0.x, PV0.x
       y: MULADD R1.y,   R1.x, C0.y, PV0.y
       z: MULADD R1.z,   R1.x, C0.z, PV0.z
       w: MULADD R1.w,   R1.x, C0.w, PV0.w
02 EXP_DONE: POS0, R1
03 EXP: PARAM0, R2 NO_BARRIER
04 EXP_DONE: PARAM1, R3.xy00 NO_BARRIER
END_OF_PROGRAM
