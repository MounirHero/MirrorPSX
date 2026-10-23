// SPDX-License-Identifier: GPL-3.0-or-later
#define __PROSPERO__ 1
#include "platform/ps5_broker.hpp"
#include "broker_config.hpp"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <cstdio>
#include <openprospero/companion_privileged.h>
#include <sys/socket.h>
#include <unistd.h>
namespace mirror {
bool prepare_ps5_network(std::string &error) {
  static bool prepared = false;
  if (prepared)
    return true;
  uint32_t addresses[2] = {0x7f000001, 0};
  const int fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd >= 0) {
    sockaddr_in route{}, local{};
    route.sin_len = sizeof(route);
    route.sin_family = AF_INET;
    route.sin_port = htons(53);
    route.sin_addr.s_addr = htonl(0x08080808);
    socklen_t size = sizeof(local);
    if (connect(fd, reinterpret_cast<sockaddr *>(&route), sizeof(route)) == 0 &&
        getsockname(fd, reinterpret_cast<sockaddr *>(&local), &size) == 0)
      addresses[1] = ntohl(local.sin_addr.s_addr);
    close(fd);
  }
  const int32_t own_pid = getpid();
  int last_error = -1;
  // External service only: never upload, spawn, kill or replace a broker here.
  // Connected UDP validates the reply peer; SDK validates command/PID/frame.
  for (uint32_t address : addresses) {
    if (!address)
      continue;
    for (uint16_t port = 9069; port <= 9072; ++port) {
      op_companion_privileged_endpoint endpoint{address, port,
                                                MIRROR_BROKER_TOKEN, 400, 1};
      op_companion_privileged_reply reply{};
      last_error = op_companion_ps5_promote(&endpoint, own_pid, &reply);
      if (last_error == 0) {
        if (reply.rootdir || reply.jaildir || reply.credentials ||
            getuid() != 0) {
          error = "Il broker non ha completato i permessi di rete.";
          return false;
        }
        std::fprintf(
            stderr, "[MirrorPSX] External broker promoted own PID=%d port=%u\n",
            own_pid, port);
        prepared = true;
        return true;
      }
    }
  }
  std::fprintf(stderr, "[MirrorPSX] External broker unavailable: result=%d\n",
               last_error);
  error = "Avvia il broker OpenProspero esterno, poi premi Riprova.";
  return false;
}
} // namespace mirror
