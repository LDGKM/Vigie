#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

#include "corpus/embedder.hpp"
#include "corpus/reranker.hpp"
#include "corpus/search.hpp"

using namespace vigie::corpus;

// Outil de diagnostic : affiche ce que la recherche retrouve pour une question.
int main(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "Usage : vigie_search <base> <modele> <question> [reclasseur]\n";
        return 2;
    }

    try {
        const Embedder embedder{std::filesystem::path(argv[2])};
        std::unique_ptr<Reranker> reranker;
        if (argc > 4) {
            reranker = std::make_unique<Reranker>(std::filesystem::path(argv[4]));
        }
        const SearchIndex index{std::filesystem::path(argv[1]), &embedder, reranker.get()};
        const auto hits = index.search(argv[3]);
        if (hits.empty()) {
            std::cout << "NON TROUVE\n";
        }
        for (const SearchHit& hit : hits) {
            std::cout << hit.document_id << " | " << hit.section << " | " << hit.coverage << " | " << hit.similarity << " | " << hit.rerank_score << '\n';
        }
    } catch (const std::exception& error) {
        std::cerr << "Erreur : " << error.what() << '\n';
        return 1;
    }
    return 0;
}
