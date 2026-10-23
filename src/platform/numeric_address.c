// SPDX-License-Identifier: GPL-3.0-or-later
// Numeric peer addresses need no DNS resolver. Preserve IPv6 scope IDs.
#include "numeric_address.h"
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif
#include <stdint.h>
#include <string.h>
int mirror_parse_numeric_address(int family, const char *source, void *output,
                                 int capacity) {
  if (!source || !output)
    return -1;
  if (family == AF_INET) {
    struct sockaddr_in address = {0};
    if (capacity < (int)sizeof(address) ||
        inet_pton(AF_INET, source, &address.sin_addr) != 1)
      return -1;
    address.sin_family = AF_INET;
#if defined(__FreeBSD__) || defined(MIRROR_PS5)
    address.sin_len = sizeof(address);
#endif
    memcpy(output, &address, sizeof(address));
    return sizeof(address);
  }
  if (family != AF_INET6 || capacity < (int)sizeof(struct sockaddr_in6))
    return -1;
  char text[80];
  size_t length = 0;
  while (source[length] && length < sizeof(text) - 1)
    ++length;
  if (source[length])
    return -1;
  memcpy(text, source, length + 1);
  uint32_t scope = 0;
  char *zone = strchr(text, '%');
  if (zone) {
    *zone++ = 0;
    if (!*zone)
      return -1;
    for (; *zone; ++zone) {
      if (*zone < '0' || *zone > '9')
        return -1;
      unsigned digit = (unsigned)(*zone - '0');
      if (scope > (UINT32_MAX - digit) / 10)
        return -1;
      scope = scope * 10 + digit;
    }
    if (!scope)
      return -1;
  }
  struct sockaddr_in6 address = {0};
  if (inet_pton(AF_INET6, text, &address.sin6_addr) != 1)
    return -1;
  address.sin6_family = AF_INET6;
  address.sin6_scope_id = scope;
#if defined(__FreeBSD__) || defined(MIRROR_PS5)
  address.sin6_len = sizeof(address);
#endif
  memcpy(output, &address, sizeof(address));
  return sizeof(address);
}
