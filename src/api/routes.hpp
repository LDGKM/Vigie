#pragma once

#include <crow.h>

#include "config.hpp"
#include "core/assistant.hpp"

namespace vigie {

void register_routes(crow::SimpleApp& app, const Config& config, const Assistant& assistant);

}  // namespace vigie
