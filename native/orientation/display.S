@ Replaces ngidisplay's default-screen rotation query: the reported orientation is the one the
@ game last requested from ngigameorientation, since the window server stays landscape.
.syntax unified
.arch armv5t
.thumb
.text
.global ngage_display_orientation
.type ngage_display_orientation, %function
.thumb_func
ngage_display_orientation:
    ldr r0, .Linterface
    mov r1, sp
    blx ngage_runtime_interface
    movs r7, #1
    cmp r0, #0
    bne .Llandscape
    ldr r0, [sp]
    ldr r1, [r0]
    ldr r3, [r1, #0x18]
    add r1, sp, #4
    blx r3
    ldr r0, [sp]
    ldr r1, [r0]
    ldr r3, [r1, #4]
    blx r3
    @ Only a single orientation bit (1, 2, 4 or 8) is used; anything else reports landscape.
    ldr r1, [sp, #4]
    cmp r1, #8
    bhi .Llandscape
    subs r0, r1, #1
    tst r0, r1
    bne .Llandscape
    cmp r1, #0
    bne .Lmap
.Llandscape:
    movs r1, #2
.Lmap:
    b ngage_display_map
    .p2align 2
.Linterface:
    .word 0x1000631
.size ngage_display_orientation, .-ngage_display_orientation
