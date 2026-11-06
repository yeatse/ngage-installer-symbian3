.syntax unified
.arm
.text
@ Logs a factory call, then tail-calls the real export with the caller's
@ r0-r3 and stack intact. The return goes through FactoryReturn so the
@ result is logged; the caller's lr is kept on a per-thread shadow stack.
.macro FACTORY ordinal
.global FactoryThunk\ordinal
.type FactoryThunk\ordinal, %function
FactoryThunk\ordinal:
    push {r0-r3, ip, lr}
    mov r0, #\ordinal
    mov r1, sp
    bl FactoryResolve
    mov ip, r0
    pop {r0-r3}
    add sp, sp, #8
    adr lr, FactoryReturn
    bx ip
.size FactoryThunk\ordinal, .-FactoryThunk\ordinal
.endm
FACTORY 1
FACTORY 2
FACTORY 3
FACTORY 4
FACTORY 5
FACTORY 6

.type FactoryReturn, %function
FactoryReturn:
    push {r0-r3}
    bl FactoryLeave
    mov ip, r0
    pop {r0-r3}
    bx ip
.size FactoryReturn, .-FactoryReturn
