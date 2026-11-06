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
    ldr lr, =FactoryReturn
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

@ Wrapped object vtables log entry only and tail-call with the caller's lr,
@ so methods that leave still unwind normally.
.macro SLOT index
.global VtableThunk\index
.type VtableThunk\index, %function
VtableThunk\index:
    push {r0-r3, ip, lr}
    mov r0, #\index
    mov r1, sp
    bl VtableResolve
    mov ip, r0
    pop {r0-r3}
    add sp, sp, #4
    pop {lr}
    bx ip
.size VtableThunk\index, .-VtableThunk\index
.endm
@ Slots whose results matter also return through FactoryReturn; the caller's
@ lr is kept on the shadow stack.
.macro SLOTRET index
.global VtableThunk\index
.type VtableThunk\index, %function
VtableThunk\index:
    push {r0-r3, ip, lr}
    mov r0, #\index
    mov r1, sp
    bl VtableResolveReturn
    mov ip, r0
    pop {r0-r3}
    add sp, sp, #8
    ldr lr, =FactoryReturn
    bx ip
.size VtableThunk\index, .-VtableThunk\index
.endm

SLOT 0
SLOT 1
SLOT 2
SLOT 3
SLOT 4
SLOT 5
SLOTRET 6
SLOT 7
SLOT 8
SLOT 9
SLOT 10
SLOT 11
SLOT 12
SLOT 13
SLOT 14
SLOT 15
SLOT 16
SLOTRET 17
SLOT 18
SLOT 19
SLOTRET 20
SLOT 21
SLOT 22
SLOT 23
SLOTRET 24
SLOT 25
SLOTRET 26
SLOT 27
SLOT 28
SLOT 29

.ltorg

