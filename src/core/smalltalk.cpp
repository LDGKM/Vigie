#include "core/smalltalk.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <string_view>
#include <vector>

namespace vigie {
namespace {

constexpr std::size_t kMaximumChatWords = 6;

// Minuscules et mots séparés ; les lettres accentuées restent dans les mots.
std::vector<std::string> words_of(const std::string& message) {
    std::vector<std::string> words;
    std::string word;
    for (const char raw : message + ' ') {
        const auto character = static_cast<unsigned char>(raw);
        if (std::isalnum(character) || (character & 0x80) != 0) {
            word += static_cast<char>(std::tolower(character));
        } else if (!word.empty()) {
            words.push_back(std::move(word));
            word.clear();
        }
    }
    return words;
}

bool has(const std::vector<std::string>& words, std::string_view word) {
    return std::find(words.begin(), words.end(), word) != words.end();
}

bool has_phrase(const std::vector<std::string>& words, std::initializer_list<std::string_view> phrase) {
    if (phrase.size() > words.size()) {
        return false;
    }
    for (std::size_t start = 0; start + phrase.size() <= words.size(); ++start) {
        if (std::equal(phrase.begin(), phrase.end(), words.begin() + static_cast<std::ptrdiff_t>(start))) {
            return true;
        }
    }
    return false;
}

constexpr std::array<std::string_view, 6> kGreetings = {"bonjour", "salut", "bonsoir", "hello", "coucou", "hey"};

bool is_greeting(const std::string& word) {
    return std::find(kGreetings.begin(), kGreetings.end(), word) != kGreetings.end();
}

const char* kPresentation =
    "Je suis Vigie, un assistant de finance numérique. Je réponds à tes questions à partir de plus de 140 documents "
    "fiables : textes européens, fiches de la Banque de France, de Service Public et de l'AMF. Chaque réponse "
    "cite ses sources, et je te dis quand je ne trouve pas l'information. Je peux aussi simuler un plan "
    "d'épargne sur plusieurs années avec toi. Je ne te dirai jamais quoi acheter ou vendre.";

}  // namespace

std::string without_greeting(const std::string& message) {
    const std::vector<std::string> words = words_of(message);
    if (words.size() < 3 || !is_greeting(words.front())) {
        return message;
    }
    std::size_t position = message.find_first_of(" ,!.");
    position = message.find_first_not_of(" ,!.", position);
    if (position == std::string::npos) {
        return message;
    }
    // « Bonjour Vigie, ... » : le nom est retiré avec la salutation.
    std::string rest = message.substr(position);
    if (rest.size() > 5 && (rest.rfind("Vigie", 0) == 0 || rest.rfind("vigie", 0) == 0)) {
        rest = rest.substr(rest.find_first_not_of(" ,!.", 5) == std::string::npos ? rest.size()
                                                                                  : rest.find_first_not_of(" ,!.", 5));
    }
    return rest.empty() ? message : rest;
}

std::optional<ChatReply> small_talk(const std::string& message) {
    const std::vector<std::string> words = words_of(message);
    if (words.empty()) {
        return std::nullopt;
    }

    // Un projet d'épargne passe par le simulateur, quelle que soit la longueur du message.
    // « Plan d'épargne » seul désigne souvent un produit (PEL, PEA, PER) : il ne déclenche pas le simulateur.
    if (has_phrase(words, {"plan", "financier"}) || has_phrase(words, {"planifier"}) || has_phrase(words, {"projet", "d", "épargne"}) ||
        has_phrase(words, {"faire", "un", "budget"}) || has_phrase(words, {"simuler", "mon", "épargne"})) {
        return ChatReply{ChatKind::PlanOffer,
                         "Construisons ton plan ensemble. Je vais te poser quelques questions sur tes revenus, tes "
                         "dépenses et tes objectifs, puis je calculerai l'évolution de ton épargne année par année. "
                         "Ce sera une simulation selon tes hypothèses, pas un conseil en placement."};
    }

    if (words.size() > kMaximumChatWords) {
        return std::nullopt;
    }

    if (has(words, "merci")) {
        return ChatReply{ChatKind::Reply, "Avec plaisir. Tu peux me poser une autre question quand tu veux."};
    }
    if (has_phrase(words, {"au", "revoir"}) || has(words, "bye") || has_phrase(words, {"à", "plus"}) ||
        has_phrase(words, {"bonne", "journée"}) || has_phrase(words, {"bonne", "soirée"})) {
        return ChatReply{ChatKind::Reply, "À bientôt ! Je reste disponible pour tes questions de finance."};
    }
    if (has_phrase(words, {"qui", "es", "tu"}) || has_phrase(words, {"tu", "es", "qui"}) ||
        has_phrase(words, {"présente", "toi"}) || has_phrase(words, {"c", "est", "quoi", "vigie"}) ||
        has_phrase(words, {"qu", "est", "ce", "que", "vigie"})) {
        return ChatReply{ChatKind::Reply, kPresentation};
    }
    if (has_phrase(words, {"que", "sais", "tu", "faire"}) || has_phrase(words, {"tu", "sais", "faire", "quoi"}) ||
        has(words, "aide") || has(words, "help") || has_phrase(words, {"tu", "peux", "faire", "quoi"})) {
        return ChatReply{ChatKind::Reply,
                         std::string(kPresentation) +
                             " Exemples : « Qu'est-ce qu'un PEA ? », « Quel est le plafond du livret A ? », ou "
                             "« Faisons un plan financier »."};
    }
    if (has_phrase(words, {"ça", "va"}) || has_phrase(words, {"comment", "vas", "tu"}) ||
        has_phrase(words, {"comment", "allez", "vous"})) {
        return ChatReply{ChatKind::Reply, "Très bien, merci ! Que veux-tu savoir aujourd'hui ?"};
    }
    if (is_greeting(words.front()) && words.size() <= 3) {
        return ChatReply{ChatKind::Reply,
                         "Bonjour ! Je suis Vigie. Pose-moi une question de finance, ou dis « plan financier » pour "
                         "simuler ton épargne sur plusieurs années."};
    }
    return std::nullopt;
}

}  // namespace vigie
