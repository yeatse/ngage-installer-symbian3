// EKA2 DLL entry; R0 is the module-entry reason (2 on process attach).
.syntax unified
.arch armv5t
.arm
.section .text.startup, "ax", %progbits
.global _E32Startup
.type _E32Startup, %function
_E32Startup:
  .word 0xe31f0000
  cmp r4, #1
  b .Lentry
  .word 0
.Lentry:
  ldr ip, .Lentry_target
  bx ip
  .align 2
.Lentry_target:
  .word NgageHeapEntry
.size _E32Startup, . - _E32Startup
.section .note.GNU-stack, "", %progbits
