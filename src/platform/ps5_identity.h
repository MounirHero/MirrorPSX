// Native identity queries for the PS5 application profile.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

// Bind the same libkernel syscall entry used by OpenProspero's payload CRT.
// Call on the main thread before starting Vulkan or any worker threads.
int mirror_ps5_identity_initialize(const void *getpid_stub, pid_t expected_pid);

#ifdef __cplusplus
}
#endif
