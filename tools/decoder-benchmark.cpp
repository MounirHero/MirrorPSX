// SPDX-License-Identifier: GPL-3.0-or-later
#include "receiver/decoder.hpp"
extern "C" {
#include <libavcodec/avcodec.h>
}
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>
int main(int argc, char **argv) {
  if (argc != 2)
    return 1;
  std::ifstream file(argv[1], std::ios::binary);
  std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
  if (bytes.empty())
    return 2;
  const size_t length = bytes.size();
  bytes.resize(length + AV_INPUT_BUFFER_PADDING_SIZE);
  auto *parser = av_parser_init(AV_CODEC_ID_H264);
  auto *context = avcodec_alloc_context3(nullptr);
  std::vector<std::vector<uint8_t>> packets;
  size_t offset = 0;
  while (offset < length) {
    uint8_t *packet = nullptr;
    int size = 0;
    int consumed = av_parser_parse2(parser, context, &packet, &size,
                                    bytes.data() + offset, int(length - offset),
                                    AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
    if (consumed < 0 || (!consumed && !size))
      return 3;
    offset += consumed;
    if (size)
      packets.emplace_back(packet, packet + size);
  }
  uint8_t *packet = nullptr;
  int size = 0;
  av_parser_parse2(parser, context, &packet, &size, nullptr, 0, AV_NOPTS_VALUE,
                   AV_NOPTS_VALUE, 0);
  if (size)
    packets.emplace_back(packet, packet + size);
  av_parser_close(parser);
  avcodec_free_context(&context);
  mirror::State state;
  mirror::Decoder decoder(state);
  auto start = std::chrono::steady_clock::now();
  for (int pass = 0; pass < 3; ++pass) {
    if (!decoder.open())
      return 4;
    for (const auto &p : packets)
      if (!decoder.push(p.data(), int(p.size())))
        return 5;
  }
  double seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - start)
          .count();
  auto status = state.snapshot();
  if (status.frames == 0)
    return 6;
  std::printf("%dx%d: %llu frames, %.3f seconds, %.2f ms/frame, %.1f "
              "frames/sec (decode + RGBA conversion)\n",
              status.width, status.height, (unsigned long long)status.frames,
              seconds, seconds * 1000 / status.frames, status.frames / seconds);
}
