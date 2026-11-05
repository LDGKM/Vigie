#include "corpus/search.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <set>
#include <sstream>
#include <unordered_map>
#include <utility>

#include "corpus/sqlite.hpp"

namespace vigie::corpus {
namespace {

constexpr int kIndexVersion = 2;
constexpr double kEpsilon = 1e-9;
constexpr double kPreamblePenalty = 0.15;
constexpr double kDefinitionBonus = 0.3;
constexpr double kTitleBonus = 0.1;

constexpr auto kStopWords = std::to_array<std::string_view>({
    "a",      "afin",    "ai",       "ainsi",   "as",      "au",     "aujourd", "aura",    "aussi",  "aux",
    "avec",   "avoir",   "c",        "ce",      "ces",     "cet",    "cette",   "comme",   "comment", "combien",
    "d",      "dans",    "de",       "des",     "dire",    "dit",    "doit",    "dois",    "doivent", "donc",
    "dont",   "du",      "elle",     "elles",   "en",      "entre",  "est",     "et",      "etre",    "explique",
    "faire",  "fait",    "hui",      "il",      "ils",     "j",      "je",      "l",       "la",      "le",
    "les",    "leur",    "leurs",    "lui",     "m",       "ma",     "mais",    "mes",     "mon",     "n",
    "ne",     "notre",   "nous",     "on",      "ont",     "ou",     "par",     "pas",     "peut",    "peuvent",
    "plus",   "pour",    "pourquoi", "qu",      "quand",   "que",    "quel",    "quelle",  "quelles", "quels",
    "qui",    "quoi",    "s",        "sa",      "se",      "sera",   "ses",     "si",      "signifie", "son",
    "sont",   "sous",    "sur",      "t",       "ta",      "tes",    "ton",     "tres",    "tu",      "un",
    "une",    "vos",     "votre",    "vous",    "y",
});

bool is_stop_word(std::string_view word) {
    return std::find(kStopWords.begin(), kStopWords.end(), word) != kStopWords.end();
}

// Convertit une lettre accentuée latine (deux octets commençant par 0xC3) en lettre simple.
char fold_latin1_letter(unsigned char second) {
    static constexpr std::string_view kLetters =
        "aaaaaaaceeeeiiiidnooooo*ouuuuypsaaaaaaaceeeeiiiidnooooo/ouuuuypy";
    return second >= 0x80 && second <= 0xBF ? kLetters[second - 0x80] : ' ';
}

std::string normalize_term(std::string word) {
    if (word.size() >= 5 && (word.back() == 's' || word.back() == 'x') && word[word.size() - 2] != 's') {
        word.pop_back();
    }
    return word;
}

bool is_prefix_candidate(const std::string& term) {
    return term.size() >= 4 && std::any_of(term.begin(), term.end(), [](unsigned char c) { return c >= 'a'; });
}

std::string match_expression(const std::string& term) {
    return "\"" + term + "\"" + (is_prefix_candidate(term) ? "*" : "");
}

bool is_ascii_alnum(unsigned char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

// Met le texte en minuscules sans accents et remplace la ponctuation par des espaces.
std::string fold_text(std::string_view text) {
    std::string out;
    out.reserve(text.size());

    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (is_ascii_alnum(c)) {
            out += static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
        } else if (c == 0xC3 && i + 1 < text.size()) {
            const char letter = fold_latin1_letter(static_cast<unsigned char>(text[++i]));
            out += letter >= 'a' && letter <= 'z' ? letter : ' ';
        } else if (c == 0xC5 && i + 1 < text.size() &&
                   (static_cast<unsigned char>(text[i + 1]) == 0x93 || static_cast<unsigned char>(text[i + 1]) == 0x92)) {
            out += "oe";
            ++i;
        } else if (c < 0x80 || c == 0xC2 || c == 0xE2) {
            out += ' ';
            if (c == 0xC2) {
                ++i;
            } else if (c == 0xE2) {
                i += 2;
            }
        } else {
            out += ' ';
        }
    }

    return out;
}

// Part de la question couverte par un passage, en donnant plus de poids aux mots rares :
// un mot absent du corpus, comme « diesel », pèse lourd et fait baisser la couverture.
struct TermCoverage {
    std::unordered_map<int, double> weights_by_chunk;
    double total_weight = 0.0;

    double of(int chunk_id) const {
        const auto found = weights_by_chunk.find(chunk_id);
        return found != weights_by_chunk.end() && total_weight > 0.0 ? found->second / total_weight : 0.0;
    }
};

TermCoverage compute_coverage(Database& database, const std::vector<std::string>& terms) {
    Statement total(database, "SELECT count(*) FROM chunks");
    total.step();
    const double chunk_count = total.real(0);

    Statement statement(database, "SELECT rowid FROM chunks_fts WHERE chunks_fts MATCH ?");
    TermCoverage coverage;

    for (const std::string& term : terms) {
        std::vector<int> chunks;
        statement.bind(1, match_expression(term));
        while (statement.step()) {
            chunks.push_back(statement.integer(0));
        }
        statement.reset();

        const double weight = std::log(1.0 + chunk_count / (static_cast<double>(chunks.size()) + 1.0));
        coverage.total_weight += weight;
        for (const int chunk : chunks) {
            coverage.weights_by_chunk[chunk] += weight;
        }
    }

    return coverage;
}

// Une demande de définition vise d'abord les articles « Définitions » ou « Objet ».
bool asks_for_definition(std::string_view question) {
    const std::string lowered = fold_text(question);
    return lowered.find("qu est ce") != std::string::npos || lowered.find("definition") != std::string::npos ||
           lowered.find("definir") != std::string::npos || lowered.find("c est quoi") != std::string::npos;
}

bool is_definition_section(const std::string& section) {
    return section.find("Définitions") != std::string::npos || section.find(": Objet") != std::string::npos;
}

// Un texte nommé dans la question, comme « RGPD », passe avant ceux qui en parlent seulement.
bool title_mentions_term(const std::string& title, const std::vector<std::string>& terms) {
    const std::string folded = " " + fold_text(title) + " ";
    return std::any_of(terms.begin(), terms.end(), [&](const std::string& term) {
        return folded.find(" " + term + " ") != std::string::npos;
    });
}

bool is_preamble(const std::string& section) {
    return section == "Préambule";
}

void check_index_version(Database& database) {
    Statement statement(database, "PRAGMA user_version");
    if (!statement.step() || statement.integer(0) != kIndexVersion) {
        throw IndexUnavailable("Base a reconstruire avec vigie_index");
    }
}

}  // namespace

std::vector<std::string> extract_terms(std::string_view question) {
    std::vector<std::string> terms;
    std::set<std::string> seen;

    std::istringstream words{fold_text(question)};
    std::string word;
    while (words >> word) {
        if (!is_stop_word(word)) {
            std::string term = normalize_term(word);
            if (seen.insert(term).second) {
                terms.push_back(std::move(term));
            }
        }
    }

    return terms;
}

SearchIndex::SearchIndex(std::filesystem::path database_path) : database_path_(std::move(database_path)) {}

std::vector<SearchHit> SearchIndex::search(std::string_view question, const SearchOptions& options) const {
    const std::vector<std::string> terms = extract_terms(question);
    if (terms.empty()) {
        return {};
    }

    std::error_code error;
    if (!std::filesystem::exists(database_path_, error)) {
        throw IndexUnavailable("Base introuvable : lance vigie_index");
    }

    // Une connexion par recherche : le serveur reste sûr à plusieurs fils d'exécution,
    // et une base reconstruite est prise en compte sans le redémarrer.
    Database database(database_path_, Database::Mode::ReadOnly);
    check_index_version(database);

    const TermCoverage coverage = compute_coverage(database, terms);

    std::string expression;
    for (const std::string& term : terms) {
        expression += (expression.empty() ? "" : " OR ") + match_expression(term);
    }

    Statement statement(database, R"sql(
        SELECT c.id, c.document_id, d.title, d.issuer, d.url, d.published, d.version, d.version_status,
               c.section, c.page, c.text
        FROM chunks_fts
        JOIN chunks c ON c.id = chunks_fts.rowid
        JOIN documents d ON d.id = c.document_id
        WHERE chunks_fts MATCH ?
        ORDER BY bm25(chunks_fts, 2.0, 4.0, 1.0)
        LIMIT ?
    )sql");

    std::vector<SearchHit> candidates;
    std::set<int> seen_chunks;
    const auto collect = [&](const std::string& match, std::size_t limit) {
        statement.bind(1, match);
        statement.bind(2, static_cast<int>(limit));
        while (statement.step()) {
            const int chunk_id = statement.integer(0);
            if (!seen_chunks.insert(chunk_id).second) {
                continue;
            }
            SearchHit hit;
            hit.document_id = statement.text(1);
            hit.title = statement.text(2);
            hit.issuer = statement.text(3);
            hit.url = statement.text(4);
            hit.published = statement.optional_text(5);
            hit.version = statement.optional_text(6);
            hit.version_status = statement.text(7);
            hit.section = statement.text(8);
            hit.page = statement.optional_int(9);
            hit.text = statement.text(10);
            hit.coverage = coverage.of(chunk_id);
            candidates.push_back(std::move(hit));
        }
        statement.reset();
    };

    const bool definition = asks_for_definition(question);
    if (definition) {
        collect("{section} : (\"definition\"* OR \"objet\") AND (" + expression + ")", options.candidates);
    }
    collect(expression, options.candidates);

    // Si la question nomme un texte, on cherche aussi les autres mots à l'intérieur de ce texte.
    if (terms.size() >= 2) {
        for (const std::string& named : terms) {
            std::string others;
            for (const std::string& term : terms) {
                if (term != named) {
                    others += (others.empty() ? "" : " OR ") + match_expression(term);
                }
            }
            collect("{title} : " + match_expression(named) + " AND (" + others + ")", options.candidates);
        }
    }

    // À couverture proche, le dispositif d'un texte passe avant ses considérants.
    const auto rank = [&](const SearchHit& hit) {
        double value = hit.coverage;
        if (title_mentions_term(hit.title, terms)) {
            value += kTitleBonus;
        }
        if (is_preamble(hit.section)) {
            value -= kPreamblePenalty;
        }
        if (definition && is_definition_section(hit.section)) {
            value += kDefinitionBonus;
        }
        return value;
    };
    std::stable_sort(candidates.begin(), candidates.end(),
                     [&](const SearchHit& left, const SearchHit& right) { return rank(left) > rank(right); });

    const double required = terms.size() <= 2 ? 1.0 : options.minimum_coverage;
    double best_coverage = 0.0;
    for (const SearchHit& candidate : candidates) {
        best_coverage = std::max(best_coverage, candidate.coverage);
    }

    std::vector<SearchHit> hits;
    if (best_coverage + kEpsilon < required) {
        return hits;
    }

    const double floor = std::max(required, best_coverage - options.maximum_coverage_gap);
    std::unordered_map<std::string, std::size_t> per_document;
    for (SearchHit& candidate : candidates) {
        if (hits.size() >= options.limit) {
            break;
        }
        if (candidate.coverage + kEpsilon < floor) {
            continue;
        }
        std::size_t& count = per_document[candidate.document_id];
        if (count < options.per_document) {
            ++count;
            hits.push_back(std::move(candidate));
        }
    }

    return hits;
}

}  // namespace vigie::corpus
