#pragma once

#include <string>

namespace vigie::llm {

// Rédige un texte. Vigie ne dépend que de cette interface :
// une API distante pourra remplacer le modèle local sans toucher à l'assistant.
class Generator {
public:
    virtual ~Generator() = default;

    // Lève std::runtime_error si la rédaction échoue.
    virtual std::string generate(const std::string& system_prompt, const std::string& user_prompt) const = 0;
    virtual std::string name() const = 0;
};

}  // namespace vigie::llm
