.globl _start
.text
.p2align 2
_start:
    bl _main
    mov x16, #1
    svc #0x80
