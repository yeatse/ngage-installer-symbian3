@ ngidisplay tells display observers when the focused game window becomes visible. A game that
@ requested portrait at start-up pumps events until the display change arrives and is not ready
@ for this notification, so the first one that follows a portrait request is not delivered.
.syntax unified
.arch armv5t
.thumb
.text
.global ngage_display_visible
.type ngage_display_visible, %function
.thumb_func
ngage_display_visible:
    @ The orientation table size, non-zero and otherwise unused, marks the notification as pending.
    ldr r2, [r4, #0x68]
    cmp r2, #0
    beq 1f
    ldr r0, [r4]
    ldr r3, [r0, #0x24]
    movs r0, r4
    add r1, sp, #0x28
    add r2, sp, #0x24
    blx r3
    @ Orientations 1 and 4 are portrait.
    ldr r0, [sp, #0x24]
    movs r1, #5
    tst r0, r1
    beq 1f
    movs r0, #0
    str r0, [r4, #0x68]
    b ngage_display_skip
1:  b ngage_display_notify
.size ngage_display_visible, .-ngage_display_visible
