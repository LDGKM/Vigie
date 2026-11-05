#pragma once

#include <optional>
#include <string>
#include <vector>

#include "corpus/search.hpp"

namespace vigie {

struct Source {
    std::string document_id;
    std::string title;
    std::string issuer;
    std::string section;
    std::optional<int> page;
    std::string excerpt;
    std::string url;
    std::optional<std::string> published;
    std::optional<std::string> version;
    std::string version_status;
};

struct Answer {
    std::string text;
    std::vector<Source> sources;
    std::string engine;
    std::string status;
};

// Point d'entrée du moteur : l'API HTTP ne connaît que cette classe.
// Le modèle de langage viendra rédiger la réponse sans changer les routes.
class Assistant {
public:
    explicit Assistant(corpus::SearchIndex index);

    // Lève corpus::IndexUnavailable si la base documentaire n'est pas prête.
    Answer answer(const std::string& question) const;

private:
    corpus::SearchIndex index_;
};

}  // namespace vigie
