#include "config.hpp"

#include <charconv>
#include <cstdlib>
#include <stdexcept>
#include <string_view>

namespace vigie {

Config load_config() {
    Config config;
    config.web_dir = VIGIE_DEFAULT_WEB_DIR;

    if (const char* web_dir = std::getenv("VIGIE_WEB_DIR")) {
        config.web_dir = web_dir;
    }

    if (const char* port = std::getenv("VIGIE_PORT")) {
        const std::string_view text(port);
        std::uint16_t value = 0;
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
        if (error != std::errc{} || end != text.data() + text.size() || value == 0) {
            throw std::invalid_argument("VIGIE_PORT doit etre un nombre entre 1 et 65535");
        }
        config.port = value;
    }

    return config;
}

}  // namespace vigie
