#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "corpus/chunker.hpp"
#include "corpus/embedder.hpp"
#include "corpus/extract_html.hpp"
#include "corpus/extract_pdf.hpp"
#include "corpus/index_store.hpp"
#include "corpus/manifest.hpp"
#include "tools/console.hpp"

namespace fs = std::filesystem;
using namespace vigie::corpus;

namespace {

std::string read_file(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("fichier introuvable : " + path.string());
    }
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

std::vector<Paragraph> extract(const DocumentRecord& record, const fs::path& path) {
    if (record.format == "pdf") {
        return extract_pdf(path);
    }
    return extract_html(read_file(path));
}

// Une fiche à onglets répète le même texte : un doublon prendrait la place d'un autre passage.
std::vector<Chunk> without_duplicates(std::vector<Chunk> chunks) {
    std::set<std::string> seen;
    std::erase_if(chunks, [&seen](const Chunk& chunk) { return !seen.insert(chunk.section + '\n' + chunk.text).second; });
    return chunks;
}

}  // namespace

int main(int argc, char** argv) {
    vigie::tools::use_utf8_console();

    try {
        const fs::path root = argc > 1 ? fs::path(argv[1]) : fs::path(VIGIE_DEFAULT_ROOT);
        const fs::path corpus_dir = root / "data" / "corpus";
        const fs::path database = root / "data" / "index" / "vigie.db";

        const std::string model_name = "bge-m3-Q4_K_M.gguf";
        const fs::path model_path = root / "models" / model_name;
        if (!fs::exists(model_path)) {
            throw std::runtime_error("modele introuvable : lance scripts\\download_model.ps1");
        }
        const Embedder embedder(model_path);
        const VectorCache cache = load_vector_cache(database, model_name);

        std::vector<IndexedDocument> documents;
        std::size_t total_chunks = 0;
        int failures = 0;

        for (const DocumentRecord& record : load_manifest(corpus_dir / "sources.json")) {
            const fs::path path = corpus_dir / "raw" / (record.id + "." + record.format);
            try {
                std::vector<Chunk> chunks = without_duplicates(make_chunks(extract(record, path)));
                if (chunks.empty()) {
                    throw std::runtime_error("aucun texte extrait");
                }

                std::vector<std::vector<float>> vectors;
                vectors.reserve(chunks.size());
                std::size_t calculated = 0;

                for (const Chunk& chunk : chunks) {
                    const std::string input = chunk.section + " : " + chunk.text;
                    const auto cached = cache.find(input);
                    if (cached != cache.end()) {
                        vectors.push_back(cached->second);
                    } else {
                        vectors.push_back(embedder.embed_passage(input));
                        if (++calculated % 50 == 0) {
                            std::cout << "        " << record.id << " : " << calculated << " vecteurs calcules"
                                      << std::endl;
                        }
                    }
                }

                std::cout << "[OK]    " << record.id << " : " << chunks.size() << " passages" << std::endl;
                total_chunks += chunks.size();
                documents.push_back({record, std::move(chunks), std::move(vectors)});
            } catch (const std::exception& error) {
                std::cout << "[ECHEC] " << record.id << " : " << error.what() << '\n';
                ++failures;
            }
        }

        if (failures > 0) {
            std::cerr << "Index non modifie : corrige les " << failures << " echec(s), puis relance.\n";
            return 1;
        }

        write_index(database, documents, model_name);
        std::cout << documents.size() << " documents, " << total_chunks << " passages : "
                  << database.string() << '\n';
    } catch (const std::exception& error) {
        std::cerr << "Erreur : " << error.what() << '\n';
        return 1;
    }

    return 0;
}
