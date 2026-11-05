#include "config.hpp"

#include <charconv>
#include <cstdlib>
#include <stdexcept>
#include <string_view>

namespace vigie {

Config load_config() {
    Config config;
    config.web_dir = VIGIE_DEFAULT_WEB_DIR;
    config.database_path = std::string(VIGIE_DEFAULT_ROOT) + "/data/index/vigie.db";
    config.model_path = std::string(VIGIE_DEFAULT_ROOT) + "/models/multilingual-e5-small-f16.gguf";

    if (const char* web_dir = std::getenv("VIGIE_WEB_DIR")) {
        config.web_dir = web_dir;
    }

    if (const char* database_path = std::getenv("VIGIE_DB_PATH")) {
        config.database_path = database_path;
    }

    if (const char* model_path = std::getenv("VIGIE_MODEL_PATH")) {
        config.model_path = model_path;
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
