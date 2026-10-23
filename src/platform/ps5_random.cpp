// SPDX-License-Identifier: GPL-3.0-or-later
// OpenSSL 3.x RAND compatibility bridge to the console's native RNG.
#include "platform/ps5_random.hpp"
#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <openssl/crypto.h>
#include <openssl/rand.h>
extern "C" int sceKernelLoadStartModule(const char *, size_t, const void *,
                                        unsigned, void *, int *);
extern "C" int sceKernelDlsym(int, const char *, void **);
extern "C" intptr_t mirror_kernel_syscall6(intptr_t, intptr_t, intptr_t,
                                           intptr_t, intptr_t, intptr_t,
                                           intptr_t);
namespace {
std::atomic<bool> ready{false};
using NativeRandom = int (*)(uint8_t *, size_t);
NativeRandom native_random = nullptr;
NativeRandom selected_random = nullptr;
int kernel_random(uint8_t *output, size_t count) {
  // FreeBSD KERN_ARND is the kernel entropy route used by OpenSSL's BSD
  // seeding code. Use the validated libkernel gate, not the unresolved sysctl
  // import. Read-only query; no permissions or kernel state are changed.
  const int mib[] = {1, 37}; // CTL_KERN, KERN_ARND
  size_t received = count;
  const auto result =
      mirror_kernel_syscall6(202, reinterpret_cast<intptr_t>(mib), 2,
                             reinterpret_cast<intptr_t>(output),
                             reinterpret_cast<intptr_t>(&received), 0, 0);
  if (result != 0 || received != count) {
    std::fprintf(
        stderr,
        "[MirrorPSX] RNG kernel error: result=%lld errno=%d bytes=%zu/%zu\n",
        static_cast<long long>(result), errno, received, count);
    return -1;
  }
  return 0;
}
bool resolve_random() {
  if (native_random)
    return true;
  // Same bounded system-library path search and kernel NID lookup used by the
  // OpenProspero AGC loader. A linkable stub is not proof of a runtime export.
  const char *paths[] = {"/system/priv/lib/libSceRandom.sprx",
                         "/system/common/lib/libSceRandom.sprx",
                         "/system_ex/priv_ex/lib/libSceRandom.sprx",
                         "/system_ex/common_ex/lib/libSceRandom.sprx"};
  // __op_nid_encode("sceRandomGetRandomNumber") from OpenProspero's nid.c.
  constexpr const char *nid = "PI7jIZj4pcE";
  for (const auto *path : paths) {
    int start_result = 0;
    const int handle =
        sceKernelLoadStartModule(path, 0, nullptr, 0, nullptr, &start_result);
    std::fprintf(stderr, "[MirrorPSX] RNG module %s: handle=%d start=%d\n",
                 path, handle, start_result);
    if (handle <= 0)
      continue;
    void *address = nullptr;
    const int result = sceKernelDlsym(handle, nid, &address);
    std::fprintf(stderr,
                 "[MirrorPSX] RNG export lookup: result=%d available=%d\n",
                 result, address != nullptr);
    if (result == 0 && address) {
      native_random = reinterpret_cast<NativeRandom>(address);
      return true;
    }
  }
  return false;
}
int random_bytes(unsigned char *output, int count) {
  if (count < 0 || (count && !output) || !ready.load())
    return 0;
  const int requested = count;
  auto *cursor = output;
  while (count) {
    const size_t bytes = count > 64 ? 64 : size_t(count);
    if (selected_random(cursor, bytes) != 0) {
      OPENSSL_cleanse(output, size_t(requested));
      ready = false;
      return 0;
    }
    cursor += bytes;
    count -= int(bytes);
  }
  return 1;
}
int seed(const void *, int) { return ready.load() ? 1 : 0; }
int add(const void *, int, double) { return ready.load() ? 1 : 0; }
int status() { return ready.load() ? 1 : 0; }
const RAND_METHOD method{seed, random_bytes, nullptr,
                         add,  random_bytes, status};
} // namespace
bool mirror::initialize_ps5_random() {
  ready = false;
  selected_random = kernel_random;
  unsigned char probe[32]{};
  int result = selected_random(probe, sizeof(probe));
  if (result != 0 && resolve_random()) {
    selected_random = native_random;
    result = selected_random(probe, sizeof(probe));
  }
  std::fprintf(stderr, "[MirrorPSX] RNG source=%s probe=%d\n",
               selected_random == kernel_random ? "kernel KERN_ARND"
                                                : "SceRandom",
               result);
  OPENSSL_cleanse(probe, sizeof(probe));
  ready = result == 0;
  // OpenSSL 3.5 retains this legacy bridge for native platform RNGs. Both
  // public and private byte paths use the OS service, with no timestamp-based
  // fallback.
  if (RAND_set_rand_method(&method) != 1) {
    ready = false;
    return false;
  }
  return ready.load();
}
