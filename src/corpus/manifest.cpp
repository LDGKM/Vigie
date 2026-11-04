#include "corpus/manifest.hpp"

#include <fstream>
#include <set>
#include <stdexcept>

#include <json.hpp>

namespace vigie::corpus {
namespace {

using nlohmann::json;

std::string required(const json& item, const char* key) {
    if (!item.contains(key) || !item.at(key).is_string() || item.at(key).get_ref<const std::string&>().empty()) {
        throw std::runtime_error(std::string("Champ manquant dans une fiche : ") + key);
    }
    return item.at(key).get<std::string>();
}

std::optional<std::string> optional_text(const json& item, const char* key) {
    if (!item.contains(key) || item.at(key).is_null()) {
        return std::nullopt;
    }
    return item.at(key).get<std::string>();
}

}  // namespace

std::vector<DocumentRecord> load_manifest(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Fichier introuvable : " + path.string());
    }

    const json root = json::parse(file);
    std::vector<DocumentRecord> documents;
    std::set<std::string> ids;

    for (const json& item : root.at("documents")) {
        DocumentRecord record;
        record.id = required(item, "id");
        record.title = required(item, "title");
        record.issuer = required(item, "issuer");
        record.theme = required(item, "theme");
        record.language = required(item, "language");
        record.document_type = required(item, "document_type");
        record.format = required(item, "format");
        record.url = required(item, "url");
        record.published = optional_text(item, "published");
        record.version = optional_text(item, "version");
        record.version_status = required(item, "version_status");
        record.reuse = required(item, "reuse");

        if (record.format != "html" && record.format != "pdf") {
            throw std::runtime_error("Format non pris en charge pour " + record.id);
        }
        if (!ids.insert(record.id).second) {
            throw std::runtime_error("Identifiant en double : " + record.id);
        }
        documents.push_back(std::move(record));
    }

    return documents;
}

}  // namespace vigie::corpus
