#pragma once

#include <optional>
#include <string>

namespace vigie {

// Une question courte qui suit une autre question peut dépendre d'elle : « Et pour le PEL ? ».
bool may_depend_on_previous(const std::string& question);

std::string followup_system_prompt();
std::string followup_user_prompt(const std::string& previous_question, const std::string& question);

// Garde la reformulation seulement si elle ressemble à une question simple. Sinon, rien.
std::optional<std::string> clean_rewrite(const std::string& raw);

}  // namespace vigie
