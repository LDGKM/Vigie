#pragma once

#include <string>
#include <vector>

namespace vigie {

struct Source {
    std::string title;
    std::string url;
    std::string excerpt;
};

struct Answer {
    std::string text;
    std::vector<Source> sources;
    std::string engine;
};

// Point d'entrée du moteur : l'API HTTP ne connaît que cette classe.
// La recherche et le modèle de langage viendront la remplacer sans changer les routes.
class Assistant {
public:
    Answer answer(const std::string& question) const;
};

}  // namespace vigie
