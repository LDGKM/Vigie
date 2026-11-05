#include <exception>
#include <filesystem>
#include <iostream>
#include <string>

#include "corpus/search.hpp"

using namespace vigie::corpus;

// Outil de diagnostic : affiche ce que la recherche retrouve pour une question.
int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage : vigie_search <base> <question>\n";
        return 2;
    }

    try {
        const SearchIndex index{std::filesystem::path(argv[1])};
        const auto hits = index.search(argv[2]);
        if (hits.empty()) {
            std::cout << "NON TROUVE\n";
        }
        for (const SearchHit& hit : hits) {
            std::cout << hit.document_id << " | " << hit.section << " | " << hit.coverage << '\n';
        }
    } catch (const std::exception& error) {
        std::cerr << "Erreur : " << error.what() << '\n';
        return 1;
    }
    return 0;
}
