#pragma once

#include <optional>
#include <string>

namespace vigie {

enum class ChatKind {
    Reply,     // Échange simple : salutation, remerciement, présentation.
    PlanOffer, // La personne parle d'un projet d'épargne : Vigie propose le simulateur.
};

struct ChatReply {
    ChatKind kind = ChatKind::Reply;
    std::string text;
};

// Répond sans recherche aux messages qui ne demandent aucune information documentaire.
std::optional<ChatReply> small_talk(const std::string& message);

// Retire une salutation en tête : « Bonjour, quel est le plafond du livret A ? ».
std::string without_greeting(const std::string& message);

}  // namespace vigie
