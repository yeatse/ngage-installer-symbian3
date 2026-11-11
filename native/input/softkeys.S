.syntax unified
.arch armv5t
.thumb
.text
@ F1 stands for the keypad's clear key, which the N97 profile does not have.
.macro CHECK_KEYS left, right, clear
    cmp r1, #0x60
    beq \clear
    cmp r1, #0xa4
    beq \left
    cmp r1, #0xa5
    beq \right
    movs r2, #0xf8
    lsls r2, r2, #8
    adds r2, #0x42
    cmp r1, r2
    beq \left
    adds r2, #1
    cmp r1, r2
    beq \right
.endm
.global ngage_softkeys
.type ngage_softkeys, %function
.thumb_func
ngage_softkeys:
    CHECK_KEYS .Lleft, .Lright, .Lclear
    push {r3, lr}
    bl ngage_original_lookup
    pop {r3, pc}
.Lleft:
    movs r0, #1
    lsls r0, r0, #12
    bx lr
.Lright:
    movs r0, #1
    lsls r0, r0, #13
    bx lr
.Lclear:
    movs r0, #1
    lsls r0, r0, #14
    bx lr
.size ngage_softkeys, .-ngage_softkeys
.global ngage_internal_softkeys
.type ngage_internal_softkeys, %function
.thumb_func
ngage_internal_softkeys:
    CHECK_KEYS .Linternal_left, .Linternal_right, .Linternal_clear
    push {r3, lr}
    bl ngage_original_internal_lookup
    pop {r3, pc}
.Linternal_left:
    movs r0, #1
    lsls r0, r0, #12
    movs r1, #0
    bx lr
.Linternal_right:
    movs r0, #1
    lsls r0, r0, #13
    movs r1, #0
    bx lr
.Linternal_clear:
    movs r0, #1
    lsls r0, r0, #14
    movs r1, #0
    bx lr
    .p2align 2
.size ngage_internal_softkeys, .-ngage_internal_softkeys
