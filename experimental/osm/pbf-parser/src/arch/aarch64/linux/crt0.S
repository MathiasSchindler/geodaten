.global _start
.extern __newos_stack_guard_init
.text
_start:
    ldr x0, [sp]
    add x1, sp, #8
    bl __newos_stack_guard_init
    ldr x0, [sp]
    add x1, sp, #8
    bl main
    mov x8, #93
    svc #0
