#include "corpus/search.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <set>
#include <sstream>
#include <unordered_map>
#include <utility>

#include "corpus/embedder.hpp"
#include "corpus/sqlite.hpp"

namespace vigie::corpus {
namespace {

constexpr int kIndexVersion = 3;
constexpr double kEpsilon = 1e-9;
constexpr double kPreamblePenalty = 0.15;
constexpr double kDefinitionBonus = 0.3;
constexpr double kTitleBonus = 0.1;
constexpr double kPreambleFusionFactor = 0.75;
constexpr double kDefinitionFusionFactor = 1.25;
constexpr double kNamedDocumentFusionFactor = 2.0;

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

// Le sigle d'un texte figure entre parenthèses à la fin de son titre : « (RGPD) », « (MiCA) ».
bool question_names_document(const std::string& title, const std::vector<std::string>& terms) {
    const std::size_t open = title.rfind('(');
    if (open == std::string::npos) {
        return false;
    }
    const std::string folded = " " + fold_text(title.substr(open)) + " ";
    return std::any_of(terms.begin(), terms.end(), [&](const std::string& term) {
        return folded.find(" " + term + " ") != std::string::npos;
    });
}

bool is_preamble(const std::string& section) {
    return section == "Préambule";
}

struct ChunkSimilarity {
    int chunk_id = 0;
    float similarity = 0.0F;
};

// Compare la question à tous les passages : sur quelques milliers de vecteurs, ce calcul
// exact reste instantané et ne manque aucun résultat.
std::vector<ChunkSimilarity> nearest_chunks(Database& database, const std::vector<float>& query, std::size_t count) {
    Statement statement(database, "SELECT chunk_id, vector FROM chunk_vectors");
    std::vector<ChunkSimilarity> results;
    std::vector<float> vector(query.size());

    while (statement.step()) {
        if (statement.blob_size(1) != query.size() * sizeof(float)) {
            throw IndexUnavailable("Vecteurs incompatibles : relance vigie_index");
        }
        std::memcpy(vector.data(), statement.blob(1), statement.blob_size(1));

        float similarity = 0.0F;
        for (std::size_t i = 0; i < query.size(); ++i) {
            similarity += query[i] * vector[i];
        }
        results.push_back({statement.integer(0), similarity});
    }

    const std::size_t kept = std::min(count, results.size());
    std::partial_sort(results.begin(), results.begin() + static_cast<std::ptrdiff_t>(kept), results.end(),
                      [](const ChunkSimilarity& left, const ChunkSimilarity& right) {
                          return left.similarity > right.similarity;
                      });
    results.resize(kept);
    return results;
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

SearchIndex::SearchIndex(std::filesystem::path database_path, const Embedder* embedder)
    : database_path_(std::move(database_path)), embedder_(embedder) {}

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

    const auto read_hit = [](const Statement& row, double chunk_coverage) {
        SearchHit hit;
        hit.document_id = row.text(1);
        hit.title = row.text(2);
        hit.issuer = row.text(3);
        hit.url = row.text(4);
        hit.published = row.optional_text(5);
        hit.version = row.optional_text(6);
        hit.version_status = row.text(7);
        hit.section = row.text(8);
        hit.page = row.optional_int(9);
        hit.text = row.text(10);
        hit.coverage = chunk_coverage;
        return hit;
    };

    struct Candidate {
        int chunk_id = 0;
        SearchHit hit;
    };

    std::vector<Candidate> candidates;
    std::set<int> seen_chunks;
    const auto collect = [&](const std::string& match, std::size_t limit) {
        statement.bind(1, match);
        statement.bind(2, static_cast<int>(limit));
        while (statement.step()) {
            const int chunk_id = statement.integer(0);
            if (seen_chunks.insert(chunk_id).second) {
                candidates.push_back({chunk_id, read_hit(statement, coverage.of(chunk_id))});
            }
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
    std::stable_sort(candidates.begin(), candidates.end(), [&](const Candidate& left, const Candidate& right) {
        return rank(left.hit) > rank(right.hit);
    });

    const double required = terms.size() <= 2 ? 1.0 : options.minimum_coverage;
    double best_coverage = 0.0;
    for (const Candidate& candidate : candidates) {
        best_coverage = std::max(best_coverage, candidate.hit.coverage);
    }

    std::vector<ChunkSimilarity> nearest;
    if (embedder_ != nullptr) {
        nearest = nearest_chunks(database, embedder_->embed_query(question), options.candidates);
    }
    const double best_similarity = nearest.empty() ? 0.0 : nearest.front().similarity;

    // Une correspondance partielle par mots-clés doit être confirmée par le sens, si le modèle est disponible.
    const bool keywords_reliable = embedder_ == nullptr || best_coverage + kEpsilon >= options.confirmed_keyword_coverage ||
                                   best_similarity + kEpsilon >= options.minimum_similarity;

    // Chaque méthode propose sa liste de passages. La fusion favorise ceux qu'elles retiennent toutes les deux.
    std::unordered_map<int, double> fusion;
    std::unordered_map<int, SearchHit> hits_by_chunk;

    if (best_coverage + kEpsilon >= required && keywords_reliable) {
        const double floor = std::max(required, best_coverage - options.maximum_coverage_gap);
        double position = 1.0;
        for (const Candidate& candidate : candidates) {
            if (candidate.hit.coverage + kEpsilon >= floor) {
                fusion[candidate.chunk_id] += 1.0 / (options.rank_fusion_constant + position);
                hits_by_chunk[candidate.chunk_id] = candidate.hit;
                position += 1.0;
            }
        }
    }

    if (!nearest.empty()) {
        Statement chunk(database, R"sql(
            SELECT c.id, c.document_id, d.title, d.issuer, d.url, d.published, d.version, d.version_status,
                   c.section, c.page, c.text
            FROM chunks c JOIN documents d ON d.id = c.document_id
            WHERE c.id = ?
        )sql");

        const double floor = std::max(options.minimum_similarity, best_similarity - options.maximum_similarity_gap);
        double position = 1.0;
        for (const ChunkSimilarity& item : nearest) {
            auto existing = hits_by_chunk.find(item.chunk_id);
            if (existing != hits_by_chunk.end()) {
                existing->second.similarity = item.similarity;
            }
            if (item.similarity + kEpsilon < floor) {
                continue;
            }
            if (existing == hits_by_chunk.end()) {
                chunk.bind(1, item.chunk_id);
                if (chunk.step()) {
                    SearchHit hit = read_hit(chunk, coverage.of(item.chunk_id));
                    hit.similarity = item.similarity;
                    hits_by_chunk[item.chunk_id] = std::move(hit);
                }
                chunk.reset();
            }
            fusion[item.chunk_id] += 1.0 / (options.rank_fusion_constant + position);
            position += 1.0;
        }
    }

    for (auto& [chunk_id, score] : fusion) {
        const SearchHit& hit = hits_by_chunk.at(chunk_id);
        if (is_preamble(hit.section)) {
            score *= kPreambleFusionFactor;
        }
        if (definition && is_definition_section(hit.section)) {
            score *= kDefinitionFusionFactor;
        }
        if (question_names_document(hit.title, terms)) {
            score *= kNamedDocumentFusionFactor;
        }
    }

    std::vector<std::pair<double, int>> ordered;
    ordered.reserve(fusion.size());
    for (const auto& [chunk_id, score] : fusion) {
        ordered.emplace_back(score, chunk_id);
    }
    std::sort(ordered.begin(), ordered.end(), [](const auto& left, const auto& right) {
        return left.first != right.first ? left.first > right.first : left.second < right.second;
    });

    std::vector<SearchHit> hits;
    std::unordered_map<std::string, std::size_t> per_document;
    for (const auto& [score, chunk_id] : ordered) {
        if (hits.size() >= options.limit) {
            break;
        }
        SearchHit& hit = hits_by_chunk.at(chunk_id);
        std::size_t& count = per_document[hit.document_id];
        if (count < options.per_document) {
            ++count;
            hits.push_back(std::move(hit));
        }
    }

    return hits;
}

}  // namespace vigie::corpus
