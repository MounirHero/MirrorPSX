// SPDX-License-Identifier: GPL-3.0-or-later
// Host SysV ABI test: capture exactly the registers the libkernel gate sees.
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
uintptr_t mirror_identity_syscall_entry;
long captured[7];
extern long mirror_kernel_syscall6(long, long, long, long, long, long, long);
extern void capture_gate(void);
__asm__(".text\n"
        ".globl capture_gate\n"
        "capture_gate:\n"
        "movq %rax, captured(%rip)\n"
        "movq %rdi, captured+8(%rip)\n"
        "movq %rsi, captured+16(%rip)\n"
        "movq %rdx, captured+24(%rip)\n"
        "movq %r10, captured+32(%rip)\n"
        "movq %r8, captured+40(%rip)\n"
        "movq %r9, captured+48(%rip)\n"
        "xorq %rax, %rax\n"
        "ret\n");
int main(void) {
  assert(mirror_kernel_syscall6(202, 11, 22, 33, 44, 55, 66) == -78);
  mirror_identity_syscall_entry = (uintptr_t)capture_gate;
  assert(mirror_kernel_syscall6(202, 11, 22, 33, 44, 55, 66) == 0);
  assert(captured[0] == 202);
  for (int i = 1; i < 7; ++i) assert(captured[i] == i * 11);
  puts("PS5 syscall gate: all six argument registers and unavailable gate passed");
}
