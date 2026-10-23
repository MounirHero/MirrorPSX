// SPDX-License-Identifier: GPL-3.0-or-later
#include "receiver/airplay.hpp"
#include <chrono>
#include <cstdio>
#include <thread>
#ifdef _WIN32
#include <winsock2.h>
#endif
int main() {
  mirror::State state;
  mirror::AirPlay receiver(state);
  for (int attempt = 0; attempt < 2; ++attempt) {
    if (!receiver.start("MirrorPSX Receiver Test")) {
      std::fprintf(stderr, "Receiver startup: %s\n",
                   state.snapshot().error.c_str());
      return 1;
    }
    auto status = state.snapshot();
    if (!status.listening || status.port == 0 ||
        status.name != "MirrorPSX Receiver Test")
      return 2;
#ifdef _WIN32
    auto socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(status.port);
    if (connect(socket, reinterpret_cast<sockaddr *>(&address),
                sizeof(address)))
      return 4;
    const char request[] =
        "POST /pair-pin-start RTSP/1.0\r\nCSeq: 0\r\nContent-Length: 0\r\n\r\n";
    send(socket, request, sizeof(request) - 1, 0);
    char reply[2048];
    DWORD timeout = 2000;
    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<char *>(&timeout), sizeof(timeout));
    if (recv(socket, reply, sizeof(reply), 0) <= 0)
      return 5;
    closesocket(socket);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    if (state.snapshot().pin.size() != 4) {
      std::fprintf(stderr,
                   "Pairing PIN must survive the control socket closing\n");
      return 6;
    }
    mirror::VideoFrame frame;
    frame.width = frame.height = 1;
    frame.rgba.resize(4);
    state.publish(std::move(frame));
    if (!state.snapshot().pin.empty())
      return 7;
#endif
    receiver.stop();
    status = state.snapshot();
    if (status.listening || status.connected || status.port != 0)
      return 3;
  }
  std::puts(
      "receiver startup, dynamic port, pairing PIN lifecycle and restart: "
      "passed");
}
