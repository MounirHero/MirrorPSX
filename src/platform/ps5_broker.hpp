// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>
namespace mirror {
// Assets must already be resident: promotion changes the process filesystem root.
bool prepare_ps5_network(std::string &error);
}
