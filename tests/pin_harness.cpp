// SPDX-License-Identifier: GPL-3.0-or-later
#include "receiver/airplay.hpp"
#include <iostream>
#include <string>
int main() {
  mirror::State state;
  mirror::AirPlay receiver(state);
  if (!receiver.start("MirrorPSX PIN test")) return 1;
  std::cout << state.snapshot().port << std::endl;
  std::string command;
  while (std::getline(std::cin, command) && command != "quit")
    std::cout << state.snapshot().pin << std::endl;
  receiver.stop();
}
