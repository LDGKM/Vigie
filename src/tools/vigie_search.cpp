#include <exception>
#include <filesystem>
#include <iostream>
#include <string>

#include "corpus/embedder.hpp"
#include "corpus/search.hpp"

using namespace vigie::corpus;

// Outil de diagnostic : affiche ce que la recherche retrouve pour une question.
int main(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "Usage : vigie_search <base> <modele> <question>\n";
        return 2;
    }

    try {
        const Embedder embedder{std::filesystem::path(argv[2])};
        const SearchIndex index{std::filesystem::path(argv[1]), &embedder};
        const auto hits = index.search(argv[3]);
        if (hits.empty()) {
            std::cout << "NON TROUVE\n";
        }
        for (const SearchHit& hit : hits) {
            std::cout << hit.document_id << " | " << hit.section << " | " << hit.coverage << " | " << hit.similarity << '\n';
        }
    } catch (const std::exception& error) {
        std::cerr << "Erreur : " << error.what() << '\n';
        return 1;
    }
    return 0;
}
