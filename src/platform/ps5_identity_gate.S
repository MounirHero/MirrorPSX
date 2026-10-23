// Enter the published libkernel syscall gate, never a nullable identity import.
// SPDX-License-Identifier: GPL-3.0-or-later
.text
.globl mirror_identity_syscall
.type mirror_identity_syscall, @function
mirror_identity_syscall:
    movq %rdi, %rax
    movq mirror_identity_syscall_entry(%rip), %r11
    testq %r11, %r11
    jz .Lidentity_unavailable
    // Identity and getpid syscalls take no arguments.
    jmp *%r11
.Lidentity_unavailable:
    movq $-78, %rax
    ret
.size mirror_identity_syscall, .-mirror_identity_syscall

// Six-argument gate adapted from OpenProspero target/crt/start.S (GPL-3.0).
.globl mirror_kernel_syscall6
.type mirror_kernel_syscall6, @function
mirror_kernel_syscall6:
    movq %rdi, %rax
    movq %rsi, %rdi
    movq %rdx, %rsi
    movq %rcx, %rdx
    movq %r8, %r10
    movq %r9, %r8
    movq 8(%rsp), %r9
    movq mirror_identity_syscall_entry(%rip), %r11
    testq %r11, %r11
    jz .Lkernel_unavailable
    jmp *%r11
.Lkernel_unavailable:
    movq $-78, %rax
    ret
.size mirror_kernel_syscall6, .-mirror_kernel_syscall6
.section .note.GNU-stack,"",@progbits
