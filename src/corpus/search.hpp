#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace vigie::corpus {

struct SearchHit {
    std::string document_id;
    std::string title;
    std::string issuer;
    std::string url;
    std::optional<std::string> published;
    std::optional<std::string> version;
    std::string version_status;
    std::string section;
    std::optional<int> page;
    std::string text;
    double coverage = 0.0;    // Part de la question retrouvée par mots-clés.
    double similarity = 0.0;  // Proximité de sens avec la question.
    double rerank_score = 0.0;  // Score du reclassement, s'il est actif.
};

struct SearchOptions {
    std::size_t limit = 5;
    std::size_t candidates = 60;
    std::size_t per_document = 3;
    double minimum_coverage = 0.6;
    double maximum_coverage_gap = 0.2;
    double minimum_similarity = 0.60;
    double confirmed_keyword_coverage = 0.7;
    double maximum_similarity_gap = 0.03;
    double rank_fusion_constant = 30.0;
    std::size_t rerank_candidates = 20;
    double minimum_rerank_score = -3.0;
    bool use_glossary = true;
    std::size_t rerank_text_limit = 600;  // Longueur lue par le reclasseur, 0 pour tout lire.
};

// Levée si la base n'existe pas ou doit être reconstruite avec vigie_index.
class IndexUnavailable : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Extrait les mots utiles d'une question : sans mots vides, accents ni pluriel simple.
std::vector<std::string> extract_terms(std::string_view question);

class Embedder;
class Reranker;

// Relie un mot courant au vocabulaire des textes. Il élargit la recherche et ne donne aucune réponse.
struct GlossaryEntry {
    std::vector<std::string> key_terms;
    std::vector<std::string> synonyms;
};

class SearchIndex {
public:
    // Sans modèle de vectorisation, la recherche se limite aux mots-clés.
    explicit SearchIndex(std::filesystem::path database_path, const Embedder* embedder = nullptr,
                         const Reranker* reranker = nullptr);

    // Une liste vide signifie qu'aucun passage ne répond assez à la question.
    std::vector<SearchHit> search(std::string_view question, const SearchOptions& options = {}) const;

    bool uses_semantic_search() const { return embedder_ != nullptr; }
    bool uses_reranking() const { return embedder_ != nullptr && reranker_ != nullptr; }

private:
    std::filesystem::path database_path_;
    const Embedder* embedder_;
    const Reranker* reranker_;
    std::vector<GlossaryEntry> glossary_;  // Lu dans data/corpus/glossary.json, s'il existe.
};

}  // namespace vigie::corpus
