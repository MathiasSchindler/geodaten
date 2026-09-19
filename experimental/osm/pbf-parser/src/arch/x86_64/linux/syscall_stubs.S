.text

.global linux_syscall0
linux_syscall0:
    mov %rdi, %rax
    syscall
    ret

.global linux_syscall1
linux_syscall1:
    mov %rdi, %rax
    mov %rsi, %rdi
    syscall
    ret

.global linux_syscall2
linux_syscall2:
    mov %rdi, %rax
    mov %rsi, %rdi
    mov %rdx, %rsi
    syscall
    ret

.global linux_syscall3
linux_syscall3:
    mov %rdi, %rax
    mov %rsi, %rdi
    mov %rdx, %rsi
    mov %rcx, %rdx
    syscall
    ret

.global linux_syscall4
linux_syscall4:
    mov %rdi, %rax
    mov %rsi, %rdi
    mov %rdx, %rsi
    mov %rcx, %rdx
    mov %r8, %r10
    syscall
    ret

.global linux_syscall5
linux_syscall5:
    mov %rdi, %rax
    mov %rsi, %rdi
    mov %rdx, %rsi
    mov %rcx, %rdx
    mov %r8, %r10
    mov %r9, %r8
    syscall
    ret

.global linux_syscall6
linux_syscall6:
    mov %rdi, %rax
    mov %rsi, %rdi
    mov %rdx, %rsi
    mov %rcx, %rdx
    mov %r8, %r10
    mov %r9, %r8
    mov 8(%rsp), %r9
    syscall
    ret

.global linux_clone_thread
linux_clone_thread:
    sub $16, %rsi
    mov %rcx, (%rsi)
    mov %r8, 8(%rsi)
    mov %rdx, %r10
    xor %r8, %r8
    mov $56, %rax
    syscall
    test %rax, %rax
    jnz 1f
    pop %rax
    pop %rdi
    call *%rax
    mov %rax, %rdi
    mov $60, %rax
    syscall
2:
    jmp 2b
1:
    ret
