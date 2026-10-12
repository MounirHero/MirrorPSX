// SPDX-License-Identifier: GPL-3.0-or-later
// Exercises the real UxPlay RTSP pairing callback on the local loopback socket,
// without mDNS advertisement or a live Apple device.
#include "receiver/connection_tracker.hpp"
#include "receiver/state.hpp"
extern "C" {
#include "raop.h"
#include "logger.h"
}
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cctype>
#include <mutex>
#include <string>

namespace {
struct Probe {
    mirror::State state;
    mirror::ConnectionTracker sockets;
    std::mutex mutex;
    std::condition_variable cv;
    bool received_pin = false;
};
void on_pin(void* pointer, char* value) {
    auto& probe = *static_cast<Probe*>(pointer);
    probe.state.update([value](mirror::Snapshot& status) {
        status.pin = value ? value : "";
    });
    {
        std::lock_guard lock(probe.mutex);
        probe.received_pin = true;
    }
    probe.cv.notify_all();
}
void on_open(void* pointer) {
    auto& probe = *static_cast<Probe*>(pointer);
    probe.sockets.opened(probe.state);
}
void on_close(void* pointer) {
    auto& probe = *static_cast<Probe*>(pointer);
    probe.sockets.closed(probe.state);
    probe.cv.notify_all();
}
}

int main() {
#ifdef _WIN32
    WSADATA winsock{};
    assert(WSAStartup(MAKEWORD(2, 2), &winsock) == 0);
#endif
    Probe probe;
    raop_callbacks_t callbacks{};
    callbacks.cls = &probe;
    callbacks.video_process = [](void*, raop_ntp_t*, video_decode_struct*) {};
    callbacks.audio_process = [](void*, raop_ntp_t*, audio_decode_struct*) {};
    callbacks.conn_init = on_open;
    callbacks.conn_destroy = on_close;
    callbacks.display_pin = on_pin;

    raop_t* server = raop_init(&callbacks);
    assert(server);
    raop_set_log_callback(server, [](void*, int, const char*) {}, nullptr);
    raop_set_log_level(server, LOGGER_ERR);
    // No identity file or Bonjour advertisement is created by this test.
    // Deterministic key generation with an empty path is test-only behavior.
    assert(raop_init2(server, 0, "02:4D:50:53:58:FE", "") == 0);
    assert(raop_set_plist(server, "pin", 1) == 0);
    unsigned short port = 0;
    assert(raop_start_httpd(server, &port) >= 0);
    assert(port != 0);

    auto fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#ifdef _WIN32
    assert(fd != INVALID_SOCKET);
#else
    assert(fd >= 0);
#endif
    sockaddr_in endpoint{};
    endpoint.sin_family = AF_INET;
    endpoint.sin_port = htons(port);
    endpoint.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(connect(fd, reinterpret_cast<const sockaddr*>(&endpoint), sizeof(endpoint)) == 0);
    constexpr char request[] =
        "POST /pair-pin-start RTSP/1.0\r\n"
        "CSeq: 1\r\n"
        "User-Agent: MirrorPSX integration test\r\n"
        "Content-Length: 0\r\n\r\n";
    assert(send(fd, request, sizeof(request) - 1, 0) == sizeof(request) - 1);

    {
        std::unique_lock lock(probe.mutex);
        assert(probe.cv.wait_for(lock, std::chrono::seconds(3),
                                 [&]{return probe.received_pin;}));
    }
    const auto pin = probe.state.snapshot().pin;
    assert(pin.size() == 4);
    for (unsigned char digit : pin) assert(std::isdigit(digit));
    assert(probe.state.snapshot().connected);
#ifdef _WIN32
    shutdown(fd, SD_BOTH);
    closesocket(fd);
#else
    shutdown(fd, SHUT_RDWR);
    close(fd);
#endif
    {
        std::unique_lock lock(probe.mutex);
        assert(probe.cv.wait_for(lock, std::chrono::seconds(3),
                                 [&]{return probe.sockets.active() == 0;}));
    }
    assert(!probe.state.snapshot().connected);
    assert(probe.state.snapshot().pin == pin); // Critical regression check.
    raop_destroy(server);
#ifdef _WIN32
    WSACleanup();
#endif
}
