// SPDX-License-Identifier: GPL-3.0-or-later
// Loopback-only negative protocol tests against UxPlay's actual RTSP handler.
// These messages must be rejected before SRP/OpenSSL receives short buffers.
extern "C" {
#include "raop.h"
#include "logger.h"
#include <plist/plist.h>
}
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {
bool send_request(unsigned short port, const char* body, unsigned int length) {
    auto fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#ifdef _WIN32
    if (fd == INVALID_SOCKET) return false;
    DWORD timeout = 3000;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
#else
    if (fd < 0) return false;
    timeval timeout{3, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
#endif
    sockaddr_in endpoint{};
    endpoint.sin_family = AF_INET;
    endpoint.sin_port = htons(port);
    endpoint.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bool ok = connect(fd, reinterpret_cast<const sockaddr*>(&endpoint), sizeof(endpoint)) == 0;
    std::string header =
        "POST /pair-setup-pin RTSP/1.0\r\nCSeq: 2\r\n"
        "Content-Type: application/x-apple-binary-plist\r\n"
        "Content-Length: " + std::to_string(length) + "\r\n\r\n";
    std::string packet = header;
    packet.append(body, length);
    size_t offset = 0;
    while (ok && offset < packet.size()) {
        const int written = send(fd, packet.data() + offset,
                                 static_cast<int>(packet.size() - offset), 0);
        if (written <= 0) { ok = false; break; }
        offset += static_cast<size_t>(written);
    }
    char buffer[2048]{};
    std::string response;
    while (ok && response.find("\r\n\r\n") == std::string::npos && response.size() < 8192) {
        const int received = recv(fd, buffer, sizeof(buffer), 0);
        if (received <= 0) { ok = false; break; }
        response.append(buffer, received);
    }
#ifdef _WIN32
    shutdown(fd, SD_BOTH);
    closesocket(fd);
#else
    shutdown(fd, SHUT_RDWR);
    close(fd);
#endif
    // HTTP/RTSP 470 is the existing explicit pairing-auth failure.
    if (!ok || response.find(" 470 ") == std::string::npos) {
        std::fprintf(stderr, "Malformed pair-setup request unexpectedly accepted: %s\n",
                     response.substr(0, 130).c_str());
        return false;
    }
    return true;
}

bool send_plist(unsigned short port, plist_t root) {
    char* bytes = nullptr;
    uint32_t length = 0;
    plist_to_bin(root, &bytes, &length);
    const bool ok = bytes && send_request(port, bytes, length);
    plist_mem_free(bytes);
    plist_free(root);
    return ok;
}

void put_data(plist_t root, const char* key, size_t length) {
    std::vector<char> value(length, '\x01');
    plist_dict_set_item(root, key, plist_new_data(value.data(), value.size()));
}
}

int main() {
    raop_callbacks_t callbacks{};
    callbacks.video_process = [](void*, raop_ntp_t*, video_decode_struct*) {};
    callbacks.audio_process = [](void*, raop_ntp_t*, audio_decode_struct*) {};
    auto* server = raop_init(&callbacks);
    if (!server) return 1;
    raop_set_log_level(server, LOGGER_ERR);
    raop_set_log_callback(server, [](void*, int, const char*) {}, nullptr);
    if (raop_init2(server, 0, "02:4D:50:53:58:FE", "") != 0) return 2;
    unsigned short port = 0;
    if (raop_start_httpd(server, &port) < 0 || port == 0) return 3;
    bool ok = true;
    // A previously accepted prefix 'pi' must not count as method 'pin'.
    plist_t method = plist_new_dict();
    plist_dict_set_item(method, "method", plist_new_string("pi"));
    plist_dict_set_item(method, "user", plist_new_string("test-device"));
    ok &= send_plist(port, method);
    // Missing SRP session and tiny packet blobs must return 470, not dereference
    // a null session or copy data outside the received plist allocation.
    plist_t proof = plist_new_dict();
    put_data(proof, "pk", 1);
    put_data(proof, "proof", 1);
    ok &= send_plist(port, proof);
    plist_t confirm = plist_new_dict();
    put_data(confirm, "epk", 1);
    put_data(confirm, "authTag", 1);
    ok &= send_plist(port, confirm);
    constexpr char garbage[] = "not-a-binary-plist";
    ok &= send_request(port, garbage, sizeof(garbage) - 1);
    raop_destroy(server);
    return ok ? 0 : 4;
}
