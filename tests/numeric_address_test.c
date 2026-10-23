// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/numeric_address.h"
#include <arpa/inet.h>
#include <assert.h>
#include <netinet/in.h>
#include <string.h>
int main(void) {
  struct sockaddr_storage storage;
  memset(&storage, 0x42, sizeof(storage));
  assert(mirror_parse_numeric_address(AF_INET6, "fe80::cba:b9f7:e277:fe09%1",
                                      &storage, sizeof(storage)) ==
         sizeof(struct sockaddr_in6));
  struct sockaddr_in6 *v6 = (struct sockaddr_in6 *)&storage;
  assert(v6->sin6_family == AF_INET6 && v6->sin6_scope_id == 1);
  struct in6_addr expected;
  assert(inet_pton(AF_INET6, "fe80::cba:b9f7:e277:fe09", &expected) == 1);
  assert(!memcmp(&expected, &v6->sin6_addr, sizeof(expected)));
  assert(mirror_parse_numeric_address(AF_INET6, "2001:db8::1", &storage,
                                      sizeof(storage)) > 0);
  assert(((struct sockaddr_in6 *)&storage)->sin6_scope_id == 0);
  assert(mirror_parse_numeric_address(AF_INET, "192.168.1.20", &storage,
                                      sizeof(storage)) > 0);
  assert(mirror_parse_numeric_address(AF_INET6, "fe80::1%4294967296", &storage,
                                      sizeof(storage)) < 0);
  assert(mirror_parse_numeric_address(AF_INET6, "fe80::1%", &storage,
                                      sizeof(storage)) < 0);
  assert(mirror_parse_numeric_address(AF_INET6, "fe80::1%abc", &storage,
                                      sizeof(storage)) < 0);
  assert(mirror_parse_numeric_address(AF_INET6, "fe80::1%0", &storage,
                                      sizeof(storage)) < 0);
  assert(mirror_parse_numeric_address(AF_INET6, "bad", &storage,
                                      sizeof(storage)) < 0);
  assert(mirror_parse_numeric_address(AF_INET6, "::1", &storage, 1) < 0);
  assert(mirror_parse_numeric_address(AF_INET, "999.1.1.1", &storage,
                                      sizeof(storage)) < 0);
}
