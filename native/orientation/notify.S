@ Replaces ngigameorientation's window-server mode selection, which is unused once every request
@ keeps the landscape app UI orientation. A change between portrait and landscape requests is
@ reported to the game's own window group as a screen device change, the event NGI display
@ observers react to.
.syntax unified
.arch armv5t
.thumb
.text
.global ngage_orientation_notify
.type ngage_orientation_notify, %function
.thumb_func
ngage_orientation_notify:
    blx ngage_coe_static
    movs r6, r0
    ldr r0, [r0, #0x24]
    movs r1, #2
    blx ngage_set_orientation
    @ Orientations 1 and 4 are portrait; only a portrait/landscape change is reported.
    movs r1, #5
    ldr r0, [r4, #0x14]
    str r5, [r4, #0x14]
    ands r0, r1
    beq 1f
    movs r0, #1
1:  movs r2, r5
    ands r2, r1
    beq 2f
    movs r2, #1
2:  cmp r0, r2
    beq .Ldone
    sub sp, #0x30
    movs r0, #20
    str r0, [sp]
    movs r0, #0
    str r0, [sp, #4]
    str r0, [sp, #8]
    str r0, [sp, #12]
    movs r0, r6
    adds r0, #0x38
    blx ngage_window_group_identifier
    movs r1, r0
    movs r0, r6
    adds r0, #0x2c
    mov r2, sp
    blx ngage_send_event
    add sp, #0x30
.Ldone:
    b ngage_orientation_exit
.size ngage_orientation_notify, .-ngage_orientation_notify
