#include <algorithm>
#include <chrono>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <json.hpp>

#include "corpus/embedder.hpp"
#include "corpus/reranker.hpp"
#include "corpus/search.hpp"
#include "tools/console.hpp"

namespace fs = std::filesystem;
using nlohmann::json;
using namespace vigie::corpus;

namespace {

// Compare sans tenir compte des majuscules ni du type d'apostrophe.
std::string normalize(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text.compare(i, 3, "’") == 0) {
            out += '\'';
            i += 2;
        } else {
            const char c = text[i];
            out += c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
        }
    }
    return out;
}

bool section_matches(const std::string& section, const json& accepted) {
    for (const json& item : accepted) {
        const std::string value = item.get<std::string>();
        if (value == "*" || section == value || section.rfind(value + " ", 0) == 0) {
            return true;
        }
    }
    return false;
}

bool contains_evidence(const std::string& text, const std::string& evidence) {
    const std::string haystack = normalize(text);
    std::istringstream alternatives(evidence);
    std::string alternative;
    while (std::getline(alternatives, alternative, '|')) {
        if (haystack.find(normalize(alternative)) != std::string::npos) {
            return true;
        }
    }
    return false;
}

// Rang du premier passage correct, ou 0 si aucun passage retourné ne convient.
std::size_t correct_rank(const std::vector<SearchHit>& hits, const json& expected) {
    for (std::size_t i = 0; i < hits.size(); ++i) {
        if (hits[i].document_id == expected.at("document").get<std::string>() &&
            section_matches(hits[i].section, expected.at("sections")) &&
            contains_evidence(hits[i].text, expected.at("evidence").get<std::string>())) {
            return i + 1;
        }
    }
    return 0;
}

double percent(std::size_t count, std::size_t total) {
    return total == 0 ? 0.0 : 100.0 * static_cast<double>(count) / static_cast<double>(total);
}

double percentile_95(std::vector<double> values) {
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const auto index = static_cast<std::size_t>(0.95 * static_cast<double>(values.size() - 1));
    return values.at(index);
}

}  // namespace

int main(int argc, char** argv) {
    vigie::tools::use_utf8_console();

    try {
        fs::path root = VIGIE_DEFAULT_ROOT;
        std::string questions_file = "questions.json";
        bool keywords_only = false;
        bool rerank = true;
        SearchOptions options;
        for (int i = 1; i < argc; ++i) {
            const std::string argument = argv[i];
            if (argument == "--keywords") {
                keywords_only = true;
            } else if (argument == "--no-rerank") {
                rerank = false;
            } else if (argument == "--candidates" && i + 1 < argc) {
                options.rerank_candidates = static_cast<std::size_t>(std::stoul(argv[++i]));
            } else if (argument == "--no-glossary") {
                options.use_glossary = false;
            } else if (argument == "--rerank-chars" && i + 1 < argc) {
                options.rerank_text_limit = static_cast<std::size_t>(std::stoul(argv[++i]));
            } else if (argument == "--questions" && i + 1 < argc) {
                questions_file = argv[++i];
            } else if (argument == "--min-similarity" && i + 1 < argc) {
                options.minimum_similarity = std::stod(argv[++i]);
            } else if (argument == "--min-rerank" && i + 1 < argc) {
                options.minimum_rerank_score = std::stod(argv[++i]);
            } else {
                root = argument;
            }
        }

        std::ifstream file(root / "data" / "eval" / questions_file, std::ios::binary);
        if (!file) {
            throw std::runtime_error("data/eval/" + questions_file + " introuvable");
        }
        const json data = json::parse(file);

        std::unique_ptr<Embedder> embedder;
        if (!keywords_only) {
            embedder = std::make_unique<Embedder>(root / "models" / "bge-m3-Q4_K_M.gguf");
        }
        std::unique_ptr<Reranker> reranker;
        if (!keywords_only && rerank) {
            reranker = std::make_unique<Reranker>(root / "models" / "bge-reranker-v2-m3-Q4_K_M.gguf");
        }
        const SearchIndex index(root / "data" / "index" / "vigie.db", embedder.get(), reranker.get());

        std::vector<double> durations;
        const auto timed_search = [&](const std::string& question) {
            const auto start = std::chrono::steady_clock::now();
            std::vector<SearchHit> hits = index.search(question, options);
            const std::chrono::duration<double, std::milli> elapsed = std::chrono::steady_clock::now() - start;
            durations.push_back(elapsed.count());
            return hits;
        };

        std::size_t rank_1 = 0;
        std::size_t rank_3 = 0;
        std::size_t rank_5 = 0;
        std::size_t right_document = 0;
        std::size_t wrongly_refused = 0;
        double reciprocal_sum = 0.0;
        json details = json::array();

        std::cout << "Echecs :\n";
        for (const json& item : data.at("questions")) {
            const std::vector<SearchHit> hits = timed_search(item.at("question").get<std::string>());
            const std::size_t rank = correct_rank(hits, item.at("expected"));

            rank_1 += rank == 1 ? 1 : 0;
            rank_3 += rank >= 1 && rank <= 3 ? 1 : 0;
            rank_5 += rank >= 1 && rank <= 5 ? 1 : 0;
            reciprocal_sum += rank > 0 ? 1.0 / static_cast<double>(rank) : 0.0;
            wrongly_refused += hits.empty() ? 1 : 0;

            const std::string document = item.at("expected").at("document").get<std::string>();
            const bool document_found = std::any_of(hits.begin(), hits.end(), [&](const SearchHit& hit) {
                return hit.document_id == document;
            });
            right_document += document_found ? 1 : 0;

            json sections = json::array();
            for (const SearchHit& hit : hits) {
                sections.push_back(hit.document_id + " | " + hit.section);
            }
            details.push_back({{"id", item.at("id")},
                               {"rank", rank},
                               {"best_score", hits.empty() ? 0.0 : hits.front().rerank_score},
                               {"results", sections}});

            if (rank == 0) {
                std::cout << "  " << item.at("id").get<std::string>() << " : "
                          << (hits.empty() ? std::string("refusee") : hits.front().document_id + " | " + hits.front().section)
                          << '\n';
            }
        }

        std::size_t correctly_refused = 0;
        for (const json& item : data.at("out_of_corpus")) {
            const std::vector<SearchHit> hits = timed_search(item.at("question").get<std::string>());
            const bool refused = hits.empty();
            correctly_refused += refused ? 1 : 0;
            if (!refused) {
                std::cout << "  " << item.at("id").get<std::string>() << " : reponse donnee a tort\n";
            }
            details.push_back({{"id", item.at("id")},
                               {"refused", refused},
                               {"best_score", hits.empty() ? 0.0 : hits.front().rerank_score}});
        }

        const std::string mode = keywords_only ? "keywords" : reranker ? "reranked" : "hybrid";
        const std::size_t covered = data.at("questions").size();
        const std::size_t outside = data.at("out_of_corpus").size();
        const double mean_reciprocal_rank = covered == 0 ? 0.0 : reciprocal_sum / static_cast<double>(covered);
        const double latency = percentile_95(durations);

        std::cout << std::fixed << std::setprecision(1)
                  << "\nMode : " << mode
                  << "\nPassage correct au rang 1 : " << percent(rank_1, covered) << " %"
                  << "\nPassage correct dans les 3 premiers : " << percent(rank_3, covered) << " %"
                  << "\nPassage correct dans les 5 premiers : " << percent(rank_5, covered) << " %"
                  << "\nBon document dans les resultats : " << percent(right_document, covered) << " %"
                  << "\nQuestions couvertes refusees : " << wrongly_refused << '/' << covered
                  << "\nHors corpus correctement refusees : " << correctly_refused << '/' << outside
                  << std::setprecision(3) << "\nRang reciproque moyen : " << mean_reciprocal_rank
                  << std::setprecision(0) << "\nTemps de recherche (95 %) : " << latency << " ms\n";

        const fs::path output = root / "data" / "eval" / ("results-" + mode + ".json");
        std::ofstream(output, std::ios::binary) << json({{"summary",
                                                          {{"recall_at_1", percent(rank_1, covered)},
                                                           {"recall_at_3", percent(rank_3, covered)},
                                                           {"recall_at_5", percent(rank_5, covered)},
                                                           {"document_found", percent(right_document, covered)},
                                                           {"wrongly_refused", wrongly_refused},
                                                           {"correctly_refused", correctly_refused},
                                                           {"mean_reciprocal_rank", mean_reciprocal_rank},
                                                           {"latency_p95_ms", latency}}},
                                                         {"details", details}})
                                                       .dump(2);
    } catch (const std::exception& error) {
        std::cerr << "Erreur : " << error.what() << '\n';
        return 1;
    }

    return 0;
}
