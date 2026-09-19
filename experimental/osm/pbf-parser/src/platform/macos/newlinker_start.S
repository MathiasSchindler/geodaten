.globl __start
.p2align 2
__start:
    bl _main
    mov x16, #1
    svc #0x80
1:
    b 1b