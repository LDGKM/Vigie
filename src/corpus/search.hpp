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
    double coverage = 0.0;
};

struct SearchOptions {
    std::size_t limit = 5;
    std::size_t candidates = 60;
    std::size_t per_document = 3;
    double minimum_coverage = 0.6;
    double maximum_coverage_gap = 0.2;
};

// Levée si la base n'existe pas ou doit être reconstruite avec vigie_index.
class IndexUnavailable : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Extrait les mots utiles d'une question : sans mots vides, accents ni pluriel simple.
std::vector<std::string> extract_terms(std::string_view question);

class SearchIndex {
public:
    explicit SearchIndex(std::filesystem::path database_path);

    // Une liste vide signifie qu'aucun passage ne répond assez à la question.
    std::vector<SearchHit> search(std::string_view question, const SearchOptions& options = {}) const;

private:
    std::filesystem::path database_path_;
};

}  // namespace vigie::corpus
