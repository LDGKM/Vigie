#pragma once

#include <string>

#include <json.hpp>

namespace vigie {

// Ce que Vigie doit faire d'une question avant de chercher dans ses documents.
struct ToolRoute {
    enum class Kind {
        Documents,  // Aucune donnée en direct : recherche dans le corpus.
        Call,       // Appeler l'outil `tool` du serveur vigie-donnees avec `arguments`.
        Ask,        // Il manque une information : `message` la demande à la personne.
    };

    Kind kind = Kind::Documents;
    std::string tool;
    nlohmann::json arguments = nlohmann::json::object();
    std::string message;
};

// Choisit l'outil d'après la question, sans modèle de langage : un petit modèle choisit mal
// ses outils et peut inventer des arguments. Les arguments viennent tous du texte de la question.
// Les demandes d'explication (« qu'est-ce que », « comment », « pourquoi ») restent au corpus.
ToolRoute route_question(const std::string& question);

}  // namespace vigie
