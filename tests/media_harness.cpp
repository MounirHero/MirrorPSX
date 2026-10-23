// SPDX-License-Identifier: GPL-3.0-or-later
#include "receiver/dlna.hpp"
#include <iostream>
int main() {
  mirror::State state;
  mirror::MediaPlayer player(state);
  mirror::DlnaReceiver receiver(state, player);
  if (!receiver.start())
    return 1;
  std::cout << state.snapshot().media_address << std::endl;
  std::string command;
  while (std::getline(std::cin, command) && command != "quit") {
    auto s = state.snapshot();
    auto m = player.status();
    std::cout << s.frames << ' ' << s.connected << ' ' << s.media_paused << ' '
              << m.transport << ' ' << m.position << ' ' << m.duration << ' '
              << !m.error.empty() << std::endl;
  }
  receiver.stop();
}
