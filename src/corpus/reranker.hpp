#pragma once

#include <filesystem>
#include <memory>
#include <string_view>

namespace vigie::corpus {

// Évalue directement si un passage répond à une question, avec un modèle local.
// Plus lent que la recherche : il ne sert qu'à reclasser quelques passages déjà retenus.
class Reranker {
public:
    // Lève std::runtime_error si le modèle est absent ou illisible.
    explicit Reranker(const std::filesystem::path& model_path);
    ~Reranker();
    Reranker(const Reranker&) = delete;
    Reranker& operator=(const Reranker&) = delete;

    // Plus le score est élevé, plus le passage répond à la question.
    float score(std::string_view question, std::string_view passage) const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace vigie::corpus
