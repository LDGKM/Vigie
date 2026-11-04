#pragma once

#include <optional>
#include <string>
#include <vector>

namespace vigie::corpus {

// Une fiche de data/corpus/sources.json.
struct DocumentRecord {
    std::string id;
    std::string title;
    std::string issuer;
    std::string theme;
    std::string language;
    std::string document_type;
    std::string format;
    std::string url;
    std::optional<std::string> published;
    std::optional<std::string> version;
    std::string version_status;
    std::string reuse;
};

// Un bloc de texte lu dans un document, avec son emplacement.
struct Paragraph {
    std::string section;
    std::optional<int> page;
    std::string text;
};

// Un passage citable, tel qu'il sera retrouvé par la recherche.
struct Chunk {
    int position = 0;
    std::string section;
    std::optional<int> page;
    std::string text;
};

}  // namespace vigie::corpus
