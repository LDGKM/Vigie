#pragma once

#include <cstdint>
#include <string>

namespace vigie {

struct Config {
    std::string host = "127.0.0.1";  // Accessible uniquement depuis ce PC.
    std::uint16_t port = 8080;
    std::string web_dir;
    std::string database_path;
};

// Lit VIGIE_PORT, VIGIE_WEB_DIR et VIGIE_DB_PATH. Lève std::invalid_argument si le port est invalide.
Config load_config();

}  // namespace vigie
