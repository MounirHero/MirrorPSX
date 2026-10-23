// SPDX-License-Identifier: GPL-3.0-or-later
// Host model of the native RNG API; deterministic bytes exist only in this
// test.
#include "platform/ps5_random.hpp"
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <openssl/evp.h>
#include <openssl/rand.h>
static unsigned calls = 0, fail_at = 0;
static bool missing_module = false, missing_export = false,
            native_unavailable = false;
static bool kernel_available = false, kernel_short_read = false;
extern "C" intptr_t mirror_kernel_syscall6(intptr_t number, intptr_t mib_ptr,
                                           intptr_t mib_size, intptr_t output,
                                           intptr_t size_ptr, intptr_t new_data,
                                           intptr_t new_size) {
  const auto *mib = reinterpret_cast<const int *>(mib_ptr);
  auto *size = reinterpret_cast<size_t *>(size_ptr);
  assert(number == 202 && mib_size == 2 && mib[0] == 1 && mib[1] == 37);
  assert(output && *size <= 64 && !new_data && !new_size);
  if (!kernel_available)
    return -1;
  ++calls;
  if (calls == fail_at)
    return -1;
  if (kernel_short_read)
    --*size;
  std::memset(reinterpret_cast<void *>(output), 0x42, *size);
  return 0;
}
static int native_random(unsigned char *data, size_t size) {
  ++calls;
  assert(data && size <= 64);
  if (calls == fail_at || native_unavailable)
    return -1;
  std::memset(data, 0x42, size);
  return 0;
}
extern "C" int sceKernelLoadStartModule(const char *path, size_t count,
                                        const void *args, unsigned flags,
                                        void *options, int *result) {
  assert(count == 0 && !args && !flags && !options && result);
  *result = 0;
  return !missing_module &&
                 !std::strcmp(path, "/system/common/lib/libSceRandom.sprx")
             ? 42
             : -1;
}
extern "C" int sceKernelDlsym(int handle, const char *symbol, void **address) {
  assert(handle == 42 && !std::strcmp(symbol, "PI7jIZj4pcE"));
  *address = missing_export ? nullptr : reinterpret_cast<void *>(native_random);
  return missing_export ? -1 : 0;
}
int main() {
  missing_module = true;
  assert(!mirror::initialize_ps5_random() && calls == 0);
  missing_module = false;
  missing_export = true;
  assert(!mirror::initialize_ps5_random() && calls == 0);
  missing_export = false;
  fail_at = 1;
  assert(!mirror::initialize_ps5_random());
  std::array<unsigned char, 128> data{};
  assert(RAND_bytes(data.data(), data.size()) == 0);
  fail_at = 0;
  assert(mirror::initialize_ps5_random());
  auto before = calls;
  assert(RAND_bytes(data.data(), data.size()) == 1 && calls == before + 2);
  for (auto byte : data)
    assert(byte == 0x42);
  before = calls;
  assert(RAND_priv_bytes(data.data(), 80) == 1 && calls == before + 2);
  EVP_PKEY_CTX *context = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, nullptr);
  EVP_PKEY *key = nullptr;
  assert(context && EVP_PKEY_keygen_init(context) == 1);
  before = calls;
  assert(EVP_PKEY_keygen(context, &key) == 1 && calls > before);
  EVP_PKEY_free(key);
  EVP_PKEY_CTX_free(context);
  fail_at = calls + 2;
  data.fill(0x33);
  assert(RAND_bytes(data.data(), data.size()) == 0);
  for (auto byte : data)
    assert(byte == 0);
  assert(RAND_status() == 0);
  assert(RAND_bytes(data.data(), 16) == 0);
  // No SPRX needed when the kernel provides complete entropy reads.
  kernel_available = true;
  missing_module = true;
  fail_at = 0;
  assert(mirror::initialize_ps5_random());
  before = calls;
  assert(RAND_priv_bytes(data.data(), data.size()) == 1 && calls == before + 2);
  context = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, nullptr);
  key = nullptr;
  assert(context && EVP_PKEY_keygen_init(context) == 1);
  before = calls;
  assert(EVP_PKEY_keygen(context, &key) == 1 && calls > before);
  EVP_PKEY_free(key);
  EVP_PKEY_CTX_free(context);
  kernel_short_read = true;
  native_unavailable = true;
  data.fill(0x33);
  assert(RAND_bytes(data.data(), data.size()) == 0);
  for (auto byte : data)
    assert(byte == 0);
  assert(!mirror::initialize_ps5_random());
  assert(RAND_status() == 0);
  std::puts("Native RNG bridge: probe, bounded requests, private bytes, "
            "Ed25519 and fail-closed cleanup passed");
}
