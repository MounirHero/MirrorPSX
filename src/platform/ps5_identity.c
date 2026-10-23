// Adapted from Hyperion (GPL-3.0-or-later): same OpenProspero identity gate.
// PS5 application imports of getuid/geteuid can remain NULL on firmware 9.40.
// Query the kernel through libkernel instead of inventing a process identity.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/ps5_identity.h"
#include <errno.h>
#include <stddef.h>
#include <stdint.h>

// FreeBSD kernel ABI, also recorded in OpenProspero's compatibility headers.
enum {
  ps5_sys_getpid = 20,
  ps5_sys_getuid = 24,
  ps5_sys_geteuid = 25,
  ps5_sys_getegid = 43,
  ps5_sys_getgid = 47
};

uintptr_t mirror_identity_syscall_entry;
extern long mirror_identity_syscall(long number);

static long read_identity(long number) {
  // The trampoline returns -ENOSYS before initialization. libkernel's gate
  // handles kernel errors; preserve any errno it supplies on a -1 result.
  const long result = mirror_identity_syscall(number);
  if (result < -1 && result >= -4095) {
    errno = (int)-result;
    return -1;
  }
  return result;
}

uid_t __wrap_getuid(void) { return (uid_t)read_identity(ps5_sys_getuid); }

uid_t __wrap_geteuid(void) { return (uid_t)read_identity(ps5_sys_geteuid); }

gid_t __wrap_getgid(void) { return (gid_t)read_identity(ps5_sys_getgid); }

gid_t __wrap_getegid(void) { return (gid_t)read_identity(ps5_sys_getegid); }

int mirror_ps5_identity_initialize(const void *getpid_stub,
                                   pid_t expected_pid) {
  mirror_identity_syscall_entry = 0;
  if (getpid_stub == NULL || expected_pid <= 0) {
    errno = EINVAL;
    return -1;
  }

  // OpenProspero's crt/syscall.c uses this ten-byte getpid prologue.
  // System text is execute-only, so validate with a real getpid query.
  mirror_identity_syscall_entry = (uintptr_t)getpid_stub + 10;
  if (read_identity(ps5_sys_getpid) != expected_pid) {
    mirror_identity_syscall_entry = 0;
    errno = EIO;
    return -1;
  }

  const long queries[] = {ps5_sys_getuid, ps5_sys_geteuid, ps5_sys_getgid,
                          ps5_sys_getegid};
  for (unsigned index = 0; index < sizeof(queries) / sizeof(queries[0]);
       ++index) {
    if (read_identity(queries[index]) < 0) {
      mirror_identity_syscall_entry = 0;
      return -1;
    }
  }
  return 0;
}
