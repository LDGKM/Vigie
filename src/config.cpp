#include "config.hpp"

#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string_view>

namespace vigie {
namespace {

// Cherche vigie-donnees installé avec uv dans un dossier voisin du projet.
std::string find_data_server() {
    const std::filesystem::path parent = std::filesystem::path(VIGIE_DEFAULT_ROOT).parent_path();
    for (const char* folder : {"vigie_donnees", "vigie-donnees"}) {
#ifdef _WIN32
        const std::filesystem::path program = parent / folder / ".venv" / "Scripts" / "vigie-donnees.exe";
#else
        const std::filesystem::path program = parent / folder / ".venv" / "bin" / "vigie-donnees";
#endif
        std::error_code error;
        if (std::filesystem::exists(program, error)) {
            return '"' + program.string() + '"';
        }
    }
    return {};
}

}  // namespace

Config load_config() {
    Config config;
    config.web_dir = VIGIE_DEFAULT_WEB_DIR;
    config.database_path = std::string(VIGIE_DEFAULT_ROOT) + "/data/index/vigie.db";
    config.model_path = std::string(VIGIE_DEFAULT_ROOT) + "/models/bge-m3-Q4_K_M.gguf";
    config.reranker_path = std::string(VIGIE_DEFAULT_ROOT) + "/models/bge-reranker-v2-m3-Q4_K_M.gguf";
    config.llm_path = std::string(VIGIE_DEFAULT_ROOT) + "/models/Qwen3.5-4B-Q4_K_M.gguf";

    if (const char* web_dir = std::getenv("VIGIE_WEB_DIR")) {
        config.web_dir = web_dir;
    }

    if (const char* database_path = std::getenv("VIGIE_DB_PATH")) {
        config.database_path = database_path;
    }

    if (const char* model_path = std::getenv("VIGIE_MODEL_PATH")) {
        config.model_path = model_path;
    }

    if (const char* reranker_path = std::getenv("VIGIE_RERANKER_PATH")) {
        config.reranker_path = reranker_path;
    }

    if (const char* llm_path = std::getenv("VIGIE_LLM_PATH")) {
        config.llm_path = llm_path;
    }

    // VIGIE_DATA_SERVER : commande du serveur de données, ou « off » pour s'en passer.
    if (const char* command = std::getenv("VIGIE_DATA_SERVER")) {
        config.data_server_command = std::string_view(command) == "off" ? "" : command;
    } else {
        config.data_server_command = find_data_server();
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
