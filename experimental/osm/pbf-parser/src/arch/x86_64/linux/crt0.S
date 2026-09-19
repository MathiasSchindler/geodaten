.global _start
#ifndef NEWOS_DISABLE_STACK_GUARD_INIT
.extern __newos_stack_guard_init
#endif
.text
_start:
#ifndef NEWOS_DISABLE_STACK_GUARD_INIT
    mov (%rsp), %rdi
    lea 8(%rsp), %rsi
    call __newos_stack_guard_init
#endif
    mov (%rsp), %rdi
    lea 8(%rsp), %rsi
    call main
    mov %rax, %rdi
    mov $60, %rax
    syscall
