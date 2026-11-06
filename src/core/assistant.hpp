#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "corpus/search.hpp"
#include "llm/generator.hpp"

namespace vigie {

struct Source {
    int number = 0;  // Numéro cité dans la réponse, par exemple [2].
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
    std::string model;   // Vide si aucune IA n'a rédigé la réponse.
    std::string status;  // found, not_found ou unconfirmed.
};

// Point d'entrée du moteur : l'API HTTP ne connaît que cette classe.
class Assistant {
public:
    // Sans générateur, Vigie affiche les passages retrouvés sans les reformuler.
    Assistant(corpus::SearchIndex index, const llm::Generator* generator);

    using PassagesFound = std::function<void(const std::vector<Source>&)>;

    // Lève corpus::IndexUnavailable si la base documentaire n'est pas prête.
    // on_passages est appelé dès la fin de la recherche, avant la rédaction.
    Answer answer(const std::string& question, const PassagesFound& on_passages = {}) const;

private:
    corpus::SearchIndex index_;
    const llm::Generator* generator_;
};

}  // namespace vigie
