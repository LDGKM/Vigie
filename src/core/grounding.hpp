#pragma once

#include <string>
#include <vector>

#include "corpus/search.hpp"

namespace vigie {

std::string grounded_system_prompt();

// Numérote les passages à partir de 1 : ces numéros servent de citations.
std::string grounded_user_prompt(const std::string& question, const std::vector<corpus::SearchHit>& passages);

enum class AnswerCheck {
    Valid,
    NotFound,     // Le modèle indique que les passages ne suffisent pas.
    Unconfirmed,  // Citation absente ou inventée, ou nombre absent des passages cités.
};

struct CheckedAnswer {
    AnswerCheck check = AnswerCheck::Unconfirmed;
    std::vector<int> citations;  // Numéros des passages cités, sans doublon, dans l'ordre croissant.
    std::string text;            // Réponse à afficher, sans ce qui suit la dernière citation.
};

// Contrôle le texte du modèle à partir de ce qui lui a été fourni, sans lui faire confiance.
CheckedAnswer check_answer(const std::string& answer, const std::vector<corpus::SearchHit>& passages);

}  // namespace vigie
