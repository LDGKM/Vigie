#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string_view>
#include <vector>

namespace vigie::corpus {

// Transforme un texte en vecteur de sens avec un modèle local, sur le processeur.
// Le modèle e5 attend le préfixe « query: » pour une question et « passage: » pour un document.
class Embedder {
public:
    // Lève std::runtime_error si le modèle est absent ou illisible.
    explicit Embedder(const std::filesystem::path& model_path);
    ~Embedder();
    Embedder(const Embedder&) = delete;
    Embedder& operator=(const Embedder&) = delete;

    std::size_t dimensions() const;
    std::vector<float> embed_query(std::string_view text) const;
    std::vector<float> embed_passage(std::string_view text) const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace vigie::corpus
