#include "core/tool_router.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <ctime>
#include <optional>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

#include "corpus/search.hpp"

namespace vigie {
namespace {

// Le texte replié (minuscules, sans accents ni ponctuation) est entouré d'espaces :
// on cherche ainsi des débuts de mots, par exemple " lep ".
bool contains_any(const std::string& folded, std::initializer_list<std::string_view> words) {
    return std::any_of(words.begin(), words.end(),
                       [&](std::string_view word) { return folded.find(word) != std::string::npos; });
}

bool asks_for_explanation(const std::string& folded) {
    return contains_any(folded, {" qu est ce ", " c est quoi ", " definition ", " definir ", " a quoi sert",
                                 " comment ", " pourquoi ", " explique", " difference ", " fonctionne"});
}

// Espaces insécables et fines remplacées par des espaces simples, pour lire les nombres.
std::string plain_spaces(std::string text) {
    for (const std::string_view special : {std::string_view("\xC2\xA0"), std::string_view("\xE2\x80\xAF")}) {
        for (std::size_t at = text.find(special); at != std::string::npos; at = text.find(special, at)) {
            text.replace(at, special.size(), " ");
        }
    }
    return text;
}

// Nombre écrit à la française : « 200 000 », « 3,5 », « 3.5 ».
constexpr const char* kNumber = R"((\d{1,3}(?: \d{3})+|\d+)(?:[,.](\d+))?)";

double to_number(const std::string& integer_part, const std::string& decimals) {
    std::string digits;
    for (const char character : integer_part) {
        if (character != ' ') {
            digits += character;
        }
    }
    return std::stod(digits + (decimals.empty() ? "" : "." + decimals));
}

std::vector<double> amounts_in_euros(const std::string& text) {
    static const std::regex pattern(std::string(kNumber) + R"(\s*(k\s*€|k\s*euros?|€|euros?\b|eur\b))",
                                    std::regex::icase);
    std::vector<double> amounts;
    for (auto it = std::sregex_iterator(text.begin(), text.end(), pattern); it != std::sregex_iterator(); ++it) {
        const std::smatch& match = *it;
        double value = to_number(match[1].str(), match[2].str());
        if (!match[3].str().empty() && (match[3].str()[0] == 'k' || match[3].str()[0] == 'K')) {
            value *= 1000.0;
        }
        amounts.push_back(value);
    }
    return amounts;
}

std::optional<double> first_percentage(const std::string& text) {
    static const std::regex pattern(std::string(kNumber) + R"(\s*%)");
    std::smatch match;
    if (!std::regex_search(text, match, pattern)) {
        return std::nullopt;
    }
    return to_number(match[1].str(), match[2].str());
}

std::optional<int> duration_in_months(const std::string& text) {
    static const std::regex years(R"((\d{1,3})\s*(ans?|ann(?:é|e)es?)\b)", std::regex::icase);
    static const std::regex months(R"((\d{1,3})\s*mois\b)", std::regex::icase);
    std::smatch match;
    if (std::regex_search(text, match, years)) {
        return std::stoi(match[1].str()) * 12;
    }
    if (std::regex_search(text, match, months)) {
        return std::stoi(match[1].str());
    }
    return std::nullopt;
}

// ---------- Vérification d'un acteur ----------

std::string clean_entity(std::string name) {
    std::string kept;
    for (const char character : name) {
        const auto code = static_cast<unsigned char>(character);
        if (code < 0x20 || code == 0x7F || std::string_view("\"\\<>[]{}`").find(character) != std::string_view::npos) {
            continue;
        }
        kept += character;
    }
    while (!kept.empty() && std::string_view(" .,;:!?'").find(kept.back()) != std::string_view::npos) {
        kept.pop_back();
    }
    while (!kept.empty() && kept.front() == ' ') {
        kept.erase(kept.begin());
    }
    return kept.size() > 120 ? kept.substr(0, 120) : kept;
}

bool starts_with_capital_or_digit(const std::string& word) {
    return !word.empty() && ((word[0] >= 'A' && word[0] <= 'Z') || (word[0] >= '0' && word[0] <= '9'));
}

// Nom qui suit « la société », « la plateforme »... : le premier mot, puis les mots à majuscule.
std::optional<std::string> name_after_keyword(const std::string& text) {
    static const std::regex keyword(
        R"((?:soci(?:é|e)t(?:é|e)|plateforme|entreprise|banque|n(?:é|e)obanque|courtier|prestataire|application|appli|marque|compagnie|fintech|exchange|enseigne)\s+(?:de\s+|d'|d’)?)",
        std::regex::icase);
    std::smatch match;
    if (!std::regex_search(text, match, keyword)) {
        return std::nullopt;
    }
    const std::string rest = match.suffix().str();
    static const std::array<std::string_view, 12> kStops = {"est", "sont", "a", "peut", "est-elle", "est-il",
                                                            "a-t-elle", "a-t-il", "en", "pour", "qui", "-"};
    std::vector<std::string> words;
    std::string word;
    for (std::size_t i = 0; i <= rest.size(); ++i) {
        if (i == rest.size() || rest[i] == ' ' || rest[i] == '?' || rest[i] == ',' || rest[i] == '!') {
            if (!word.empty()) {
                words.push_back(word);
                word.clear();
            }
            if (i < rest.size() && rest[i] != ' ') {
                break;
            }
            continue;
        }
        word += rest[i];
    }
    std::string name;
    for (std::size_t i = 0; i < words.size(); ++i) {
        const std::string& candidate = words[i];
        if (std::find(kStops.begin(), kStops.end(), candidate) != kStops.end()) {
            break;
        }
        if (i > 0 && !starts_with_capital_or_digit(candidate)) {
            break;
        }
        // Le premier mot doit ressembler à un nom propre, pas à « en ligne » ou « que ».
        if (i == 0 && !starts_with_capital_or_digit(candidate)) {
            return std::nullopt;
        }
        name += (name.empty() ? "" : " ") + candidate;
    }
    return name.empty() ? std::nullopt : std::optional(name);
}

// « Binance est-elle autorisée ? », « Est-ce que Revolut est fiable ? ».
std::optional<std::string> name_as_subject(const std::string& text) {
    static const std::regex inverted(
        R"((?:^|[.?!]\s+)(?:[Ll]a\s+|[Ll]e\s+|[Ll]'|[Ll]’)?([A-Z0-9][A-Za-z0-9&.\-]*(?:\s+[A-Z0-9][A-Za-z0-9&.\-]*)*)\s+(?:est|sont|a|peut)(?:-t)?-(?:il|elle|ils|elles)\b)");
    static const std::regex est_ce_que(
        R"([Ee]st-ce\s+qu(?:e\s+|'|’)([A-Z0-9][A-Za-z0-9&.\-]*(?:\s+[A-Z0-9][A-Za-z0-9&.\-]*)*)\s+(?:est|sont|a)\b)");
    std::smatch match;
    if (std::regex_search(text, match, inverted) || std::regex_search(text, match, est_ce_que)) {
        return match[1].str();
    }
    return std::nullopt;
}

std::optional<std::string> entity_in(const std::string& text) {
    static const std::regex email(R"([A-Za-z0-9._%+\-]+@[A-Za-z0-9\-]+(?:\.[A-Za-z0-9\-]+)*\.[A-Za-z]{2,24})");
    static const std::regex domain(R"((?:https?://)?(?:www\.)?((?:[A-Za-z0-9\-]+\.)+[A-Za-z]{2,24})\b)");
    std::smatch match;
    if (std::regex_search(text, match, email)) {
        return match[0].str();
    }
    if (std::regex_search(text, match, domain)) {
        return match[1].str();
    }
    if (auto name = name_after_keyword(text)) {
        return name;
    }
    return name_as_subject(text);
}

std::optional<ToolRoute> route_entity(const std::string& text, const std::string& folded) {
    const bool doubt = contains_any(folded, {" arnaque", " escroc", " fiable", " serieu", " liste noire", " fraud",
                                             " louche", " piege", " confiance", " legit"});
    const bool status = contains_any(folded, {" autorise", " agree", " agrement", " enregistre", " regule",
                                              " habilit", " liste blanche", " registre", " legal"});
    if (!doubt && !status) {
        return std::nullopt;
    }
    if (const auto entity = entity_in(text)) {
        const std::string name = clean_entity(*entity);
        if (name.size() >= 2) {
            return ToolRoute{ToolRoute::Kind::Call, "verifier_entite", {{"nom", name}}, {}};
        }
    }
    // Sans nom, une question générale (« comment reconnaître une arnaque ? ») va au corpus.
    if (doubt && contains_any(folded, {" ce site", " cette plateforme", " cette societe", " cette entreprise",
                                       " ce courtier", " cette appli", " cette application", " cet acteur",
                                       " ce conseiller", " ce mail", " cet e mail", " ce placement", " cette banque"})) {
        return ToolRoute{ToolRoute::Kind::Ask, {}, nlohmann::json::object(),
                         "Je peux vérifier cet acteur dans les listes officielles (liste noire de l'AMF, registres "
                         "REGAFI et MiCA). Donne-moi son nom exact, l'adresse de son site ou son adresse e-mail."};
    }
    return std::nullopt;
}

// ---------- Données officielles ----------

std::optional<ToolRoute> route_savings_rates(const std::string& folded) {
    const bool product = contains_any(
        folded, {" livret a ", " livrets a ", " ldds ", " livret de developpement durable", " lep ",
                 " livret d epargne populaire", " pel ", " plan d epargne logement", " cel ", " compte epargne logement",
                 " livrets reglementes", " epargne reglementee"});
    const bool value = contains_any(folded, {" taux", " plafond", " remuner", " rendement", " rapporte", " interet"});
    if (product && value) {
        return ToolRoute{ToolRoute::Kind::Call, "taux_livrets", nlohmann::json::object(), {}};
    }
    return std::nullopt;
}

std::optional<ToolRoute> route_key_rates(const std::string& folded) {
    const bool named = contains_any(folded, {" taux directeur", " taux de depot", " facilite de depot",
                                             " pret marginal", " refinancement"});
    const bool ecb = contains_any(folded, {" bce ", " banque centrale europeenne"}) && folded.find(" taux") != std::string::npos;
    if (named || ecb) {
        return ToolRoute{ToolRoute::Kind::Call, "taux_directeurs", nlohmann::json::object(), {}};
    }
    return std::nullopt;
}

int current_year_month(int& month) {
    const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    month = local.tm_mon + 1;
    return local.tm_year + 1900;
}

std::optional<ToolRoute> route_inflation(const std::string& folded) {
    if (!contains_any(folded, {" inflation", " hausse des prix", " indice des prix", " ipc "}) ||
        contains_any(folded, {" objectif", " cible", " zone euro"})) {
        return std::nullopt;
    }
    ToolRoute route{ToolRoute::Kind::Call, "inflation", nlohmann::json::object(), {}};
    static const std::regex month_pattern(
        R"( (janvier|fevrier|mars|avril|mai|juin|juillet|aout|septembre|octobre|novembre|decembre)(?: (\d{4}))?)");
    static constexpr std::array<std::string_view, 12> kMonths = {"janvier", "fevrier", "mars",      "avril",
                                                                 "mai",     "juin",    "juillet",   "aout",
                                                                 "septembre", "octobre", "novembre", "decembre"};
    std::smatch match;
    if (std::regex_search(folded, match, month_pattern)) {
        const int month = static_cast<int>(std::find(kMonths.begin(), kMonths.end(), match[1].str()) - kMonths.begin()) + 1;
        int this_month = 1;
        const int this_year = current_year_month(this_month);
        // Sans année, c'est le dernier mois de ce nom déjà passé.
        const int year = match[2].matched ? std::stoi(match[2].str()) : (month <= this_month ? this_year : this_year - 1);
        route.arguments["mois"] = std::to_string(year) + (month < 10 ? "-0" : "-") + std::to_string(month);
    }
    return route;
}

// ---------- Calcul de crédit ----------

std::optional<ToolRoute> route_loan(const std::string& text, const std::string& folded) {
    if (!contains_any(folded, {" mensualit", " rembourse", " par mois"}) ||
        !contains_any(folded, {" credit", " pret ", " prets ", " emprunt"})) {
        return std::nullopt;
    }
    const std::string plain = plain_spaces(text);

    std::optional<double> insurance;
    static const std::regex insurance_pattern(std::string(R"(assurance[^0-9]{0,30})") + kNumber +
                                                  R"(\s*(?:€|euros?))",
                                              std::regex::icase);
    std::smatch match;
    std::string without_insurance = plain;
    if (std::regex_search(plain, match, insurance_pattern)) {
        insurance = to_number(match[1].str(), match[2].str());
        without_insurance = match.prefix().str() + " " + match.suffix().str();
    }

    const std::vector<double> amounts = amounts_in_euros(without_insurance);
    std::optional<double> capital;
    if (!amounts.empty()) {
        capital = *std::max_element(amounts.begin(), amounts.end());
    }
    const std::optional<double> rate = first_percentage(without_insurance);
    const std::optional<int> months = duration_in_months(without_insurance);

    // Aucune valeur : c'est une question générale sur le crédit, pour le corpus.
    if (!capital && !rate && !months) {
        return std::nullopt;
    }

    std::vector<std::string> missing;
    if (!capital) {
        missing.push_back("le montant emprunté");
    }
    if (!rate) {
        missing.push_back("le taux annuel");
    }
    if (!months) {
        missing.push_back("la durée");
    }
    if (!missing.empty()) {
        std::string list;
        for (std::size_t i = 0; i < missing.size(); ++i) {
            list += (i == 0 ? "" : i + 1 == missing.size() ? " et " : ", ") + missing[i];
        }
        return ToolRoute{ToolRoute::Kind::Ask, {}, nlohmann::json::object(),
                         "Pour calculer la mensualité, il me manque " + list +
                             ". Par exemple : « 200 000 € à 3,5 % sur 20 ans »."};
    }
    if (*capital <= 0 || *capital > 10'000'000 || *rate < 0 || *rate > 30 || *months < 1 || *months > 600) {
        return ToolRoute{ToolRoute::Kind::Ask, {}, nlohmann::json::object(),
                         "Ces valeurs sortent des limites du calcul : montant jusqu'à 10 000 000 €, taux de 0 à "
                         "30 %, durée de 1 à 600 mois."};
    }

    ToolRoute route{ToolRoute::Kind::Call, "mensualite_credit",
                    {{"capital", *capital}, {"taux_annuel", *rate}, {"duree_mois", *months}}, {}};
    if (insurance && *insurance >= 0 && *insurance <= 10'000) {
        route.arguments["assurance_mensuelle"] = *insurance;
    }
    return route;
}

}  // namespace

ToolRoute route_question(const std::string& question) {
    const std::string folded = " " + corpus::fold_for_matching(question) + " ";

    if (auto route = route_entity(question, folded)) {
        return *route;
    }
    // Un calcul chiffré reste un calcul, même formulé avec « comment ».
    if (auto route = route_loan(question, folded)) {
        return *route;
    }
    if (asks_for_explanation(folded)) {
        return {};
    }
    if (auto route = route_savings_rates(folded)) {
        return *route;
    }
    if (auto route = route_key_rates(folded)) {
        return *route;
    }
    if (auto route = route_inflation(folded)) {
        return *route;
    }
    return {};
}

}  // namespace vigie
