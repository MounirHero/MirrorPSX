// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
namespace mirror {
class Ps5Input {
  int handle_ = -1;
  double retry_at_ = 0;

public:
  Ps5Input();
  ~Ps5Input();
  Ps5Input(const Ps5Input &) = delete;
  Ps5Input &operator=(const Ps5Input &) = delete;
  void update(double now);
};
} // namespace mirror
